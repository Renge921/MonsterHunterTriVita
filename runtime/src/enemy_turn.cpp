// Enemy turning and awareness, RMHP08. Monsters turn toward a target angle a
// step at a time through three helpers (decompiled from the disassembly), and
// spin by an angle over a motion's frames (0x80133FB0, 0x80133EA0, below):
//   0x80133E14 (target, current, max): current moved toward target by at most
//     max (u16 angles, the short way round); the enemy AI calls it once a step
//     from 41 places (0x80133CB4 turning to face the player, ...);
//   0x80134884 (enemy, &angle, diff, step): *angle += or -= step toward diff,
//     or the enemy's goal (+0x37C) when |diff| < step (0x80134B70, 0x80134C44).
//   0x8012F8C4 (enemy, goal f1, step f2): the f32 at +0x464 (a blend weight
//     that 0x801A0CAC, a desert enemy's swim, turns its target angle by)
//     moved toward goal by at most step, then clamped to its range.
// Their amount is per step. Here it is taken over this step's length (the
// angles' GameStep::Share, the weight's step * GameStep::Scale), so monsters
// turn and lean at the same rate at any step length. At the stock 1/30 s step
// these are the originals.
#include "game_step.h"
#include "abi_bridge.h"
#include "guest_call.h"
#include "memory.h"
#include "port_check.h"
#include "ppc_runtime.h"

#include <algorithm>
#include <cstdint>

extern "C" void func_8016EBF4(CpuContext* ctx);
extern "C" void MH3_EnemyTurnToward(CpuContext* ctx) {
    ctx->gpr[5] = uint16_t(GameStep::Share(int32_t(uint16_t(ctx->gpr[5]))));
    func_8016EBF4(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016EBF4, MH3_EnemyTurnToward, "EnemyTurnToward");

// In RMHJ08, EnemyTurnStep is inlined into its callers (0x8016F5D8 and
// 0x8016FC3C); both call 0x8016F3BC solely to compute the per-step turn
// amount before comparing and stepping the angle.
extern "C" void func_8016F3BC(CpuContext* ctx);
extern "C" void MH3_EnemyTurnStep(CpuContext* ctx) {
    func_8016F3BC(ctx);
    ctx->gpr[3] = uint16_t(GameStep::Share(int32_t(uint16_t(ctx->gpr[3]))));
}
REGISTER_NATIVE_FUNCTION_AS(0x8016F3BC, MH3_EnemyTurnStep, "EnemyTurnStep");

extern "C" void func_80169A08(CpuContext* ctx);
extern "C" void MH3_EnemyBlendToward(CpuContext* ctx) {
    ctx->fpr[2].d = float(ctx->fpr[2].d * GameStep::Scale());
    func_80169A08(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x80169A08, MH3_EnemyBlendToward, "EnemyBlendToward");

// 0x80133FB0 (enemy, s32 angle; f1 first, f2 last frame), decompiled with
// Ghidra: while the motion is in [first, last] (em_frame_check mode 4), the
// yaw (s32 +0x1C0, 65536 a turn) turns by 2 * angle / (last - first) a call,
// truncated: the angle spread over the window at the motion's 2 frames a
// tick. Six kinds' spinning attacks call it once a step: Deviljho's tail spin
// (0x801D8F00, action 7.1: half a turn over frames 24-100, 862 a call), and
// 0x8014BE5C, 0x80158CAC, 0x801616C4, 0x801725E8, 0x801BDB28. At 60 or 120
// steps a second they spun two or four times as fast as the motion. Here the
// yaw turns by the step's share of the per-tick amount; at the stock 1/30 s
// step this is the original (MH3_PORT_CHECK=EnemyTurnOverMotion).
namespace {
void TurnOverMotionPort(CpuContext* ctx) {
    GuestCall game{ctx};
    const uint32_t enemy = ctx->gpr[3];
    const int32_t angle = int32_t(ctx->gpr[4]);
    const float first = float(ctx->fpr[1].d), last = float(ctx->fpr[2].d);

    if (game.Call<0x80169ABC>({enemy, 4}, {first, last}) != 1) // em_frame_check
        return;

    const int32_t perTick = int32_t(2.0f * (float(angle) / (last - first)));
    Memory::Write32(enemy + 0x1C0, uint32_t(int32_t(Memory::Read32(enemy + 0x1C0)) + GameStep::Share(perTick)));
}

// 0x80133EA0 (enemy, s32 angle; f1 first, f2 last frame), decompiled with
// Ghidra: while the motion is in [first, last + rate], the yaw turns by the
// angle times the frames the motion advanced this step inside [first, last],
// over last - first (truncated). The original takes the advance as the
// motion's rate (0x8012F948, frames a tick), so at 60 or 120 steps a second
// every turn of its 75 callers (spins and turns within an attack's frames,
// most monsters) was two or four times as fast; the advance is the rate
// times the step's length (GameStep::Scale) here. At the stock step this is
// the original (MH3_PORT_CHECK=EnemyTurnWhile).
void TurnWhilePort(CpuContext* ctx) {
    GuestCall game{ctx};
    const uint32_t enemy = ctx->gpr[3];
    const int32_t angle = int32_t(ctx->gpr[4]);
    const float first = float(ctx->fpr[1].d), last = float(ctx->fpr[2].d);
    game.Call<0x80169A88>({enemy});
    float advance = game.F1() * float(GameStep::Scale());

    if (game.Call<0x80169ABC>({enemy, 4}, {first, last + advance}) != 1) // em_frame_check
        return;

    game.Call<0x80169A90>({enemy});
    const float frame = game.F1(), past = frame - first;

    if (past < 0.0f)
        advance += past;
    else if (const float over = frame + advance - last; over > 0.0f)
        advance -= over;

    const int32_t turn = int32_t(advance * (float(angle) / (last - first)));
    Memory::Write32(enemy + 0x1C0, uint32_t(int32_t(Memory::Read32(enemy + 0x1C0)) + turn));
}

// 0x80137E34 (enemy), decompiled with Ghidra: the pitch (+0x1BC) and the roll
// (+0x1C4, u16 angles) brought back toward level by 0x100 a call (an angle
// within that set to 0) while +0x1E2 is 1-4: the pitch unless 0x80133CAC
// (enemy) says it is held, the roll unless +0x784 is set or +0x1E2 and +0x1E3
// are both 1; at other +0x1E2 values both are set to 0. The enemies' movement
// step (0x801382A0) calls it once a step, so at 60 or 120 steps a second a
// swimming or flying monster (Gobul after a lunge, ...) levelled two or four
// times as fast. Here by the step's share of 0x100; at the stock step this is
// the original (MH3_PORT_CHECK=EnemyLevel).
uint32_t Level(uint32_t angle, uint32_t amount) {
    const uint32_t low = angle & 0xFFFF;

    if (low < 0x8000)
        return low < amount ? 0 : angle - amount;

    return low + amount <= 0x10000 ? angle + amount : 0;
}

void LevelPort(CpuContext* ctx) {
    GuestCall game{ctx};
    const uint32_t enemy = ctx->gpr[3];
    const uint32_t mode = Memory::Read8(enemy + 0x1E2);

    if (mode - 1 > 3) {
        Memory::Write32(enemy + 0x1BC, 0);
        Memory::Write32(enemy + 0x1C4, 0);
        return;
    }

    const uint32_t amount = uint32_t(GameStep::Share(0x100));

    if (game.Call<0x8016EA84>({enemy}) == 0)
        Memory::Write32(enemy + 0x1BC, Level(Memory::Read32(enemy + 0x1BC), amount));

    if (Memory::Read8(enemy + 0x784) == 0 && (mode != 1 || Memory::Read8(enemy + 0x1E3) != 1))
        Memory::Write32(enemy + 0x1C4, Level(Memory::Read32(enemy + 0x1C4), amount));
}
} // namespace

extern "C" void func_8016EC7C(CpuContext* ctx);
extern "C" void MH3_EnemyTurnWhile(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyTurnWhile", func_8016EC7C, TurnWhilePort);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016EC7C, MH3_EnemyTurnWhile, "EnemyTurnWhile");

extern "C" void func_8016ED78(CpuContext* ctx);
extern "C" void MH3_EnemyTurnOverMotion(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyTurnOverMotion", func_8016ED78, TurnOverMotionPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016ED78, MH3_EnemyTurnOverMotion, "EnemyTurnOverMotion");

extern "C" void func_80173D00(CpuContext* ctx);
extern "C" void MH3_EnemyLevel(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyLevel", func_80173D00, LevelPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x80173D00, MH3_EnemyLevel, "EnemyLevel");

// In RMHJ08, the per-step callers inline AwarenessAdd (0x80164468) and
// GaugeAdd390/3A0/3A4 (0x801646EC/0x80164728/0x8016475C) directly inside
// AwarenessStep (0x801644A4) and EnemyTargetGauges (0x80164B74); the standalone
// Add helpers are only called on damage events. Scale per-step deltas directly
// inside both ports instead.
namespace {
void AddGaugeClamped(uint32_t addr, int32_t amount) {
    const int32_t next = int32_t(Memory::Read32(addr)) + GameStep::Share(amount);
    Memory::Write32(addr, uint32_t(std::clamp(next, 0, 20000)));
}

void AwarenessStepPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x30);
    GuestCall game{ctx};
    const uint32_t em = frame.Arg(3);

    if (Memory::Read8(em + 0x95C) == 3)
        return;

    const uint32_t enemyData = game.Call<0x8017E990>({Memory::Read8(em + 3), Memory::Read8(em + 0xA)});
    const uint32_t awarenessBlock = Memory::Read32(Memory::Read32(enemyData + 0xA0) + 0x20);
    const uint32_t ranges = Memory::Read32(awarenessBlock);
    const uint32_t custom = Memory::Read32(awarenessBlock + 4);

    int32_t r20 = int32_t(Memory::Read32(0x8062E320u));
    int32_t r31 = int32_t(Memory::Read32(0x8062E324u));
    int32_t r30 = int32_t(Memory::Read32(0x8062E328u));
    int32_t r29 = int32_t(Memory::Read32(0x8062E32Cu));
    int32_t r25 = -2;
    int32_t r24 = -5;
    int32_t r23 = 100;

    if (custom != 0) {
        r20 = int32_t(Memory::Read32(custom));
        r31 = int32_t(Memory::Read32(custom + 4));
        r30 = int32_t(Memory::Read32(custom + 8));
        r29 = int32_t(Memory::Read32(custom + 0xC));
        r25 = int32_t(Memory::Read32(custom + 0x10));
        r24 = int32_t(Memory::Read32(custom + 0x14));
        r23 = int32_t(Memory::Read32(custom + 0x18));
    }

    const uint32_t hunterCount = game.Call<0x802BCFD0>({2}) & 0xFFFF;
    uint32_t hunter = game.Call<0x802BCF94>({2});

    for (uint32_t i = 0; i < hunterCount; ++i, hunter += 0xB20) {
        if (!Memory::Read8(hunter) || game.Call<0x80165F70>({Memory::Read8(hunter + 8)}) != 0)
            continue;

        int32_t delta = 0;

        if (game.Call<0x80165E80>({em, hunter}) != 0 && game.Call<0x80168D14>({em, 0}) == 1) {
            delta = r24;
        } else {
            const float dist = Memory::ReadFloat32(em + 0x454 + i * 4);

            if (!(dist >= 0.0f))
                delta = r24;
            else if (dist <= Memory::ReadFloat32(ranges))
                delta = r20;
            else if (dist <= Memory::ReadFloat32(ranges + 4))
                delta = r31;
            else if (dist <= Memory::ReadFloat32(ranges + 8))
                delta = r30;
            else if (dist <= Memory::ReadFloat32(ranges + 0xC))
                delta = r29;
            else
                delta = r25;

            if (Memory::Read8(hunter + 9) == 1) {
                if (delta > 0)
                    delta /= 2;
            } else if (Memory::Read8(em + 0x43D) == 2 && game.Call<0x8016BD44>({em, i & 0xFF}) == 1) {
                delta += r23;
            }
        }

        AddGaugeClamped(em + 0x428 + i * 4, delta);
    }
}

void TargetGaugesPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    const uint32_t em = frame.Arg(3);
    const uint32_t rates = Memory::Read32(Memory::Read32(game.Call<0x8015D8D8>({em}) + 0xA0) + 0x24); // EnemyData
    const auto table = [&](uint32_t offset, uint32_t standard) {
        const uint32_t own = rates ? Memory::Read32(rates + offset) : 0;
        return own ? own : standard;
    };

    const uint32_t hunters = table(0, 0x806A8E58u);
    const uint32_t hunterCount = game.Call<0x802BCFD0>({2}) & 0xFFFF; // get_move_work_max
    uint32_t hunter = game.Call<0x802BCF94>({2});                     // get_move_work_adrs

    for (uint32_t i = 0; i < hunterCount; ++i, hunter += 0xB20) {
        if (!Memory::Read8(hunter) || game.Call<0x80165F70>({Memory::Read8(hunter + 8)}) != 0)
            continue;

        int32_t amount = int32_t(game.Call<0x80164798>({em, hunters, hunter, 1, i}));

        if (amount > 0) {
            if (game.Call<0x803400D0>({hunter, 12}) == 1) // Pl_Skill_ck
                amount = int32_t(float(amount) * 1.5f);

            if (game.Call<0x803400D0>({hunter, 11}) == 1) {
                amount = int32_t(float(amount) * 0.8f);

                if (amount < 1)
                    amount = 1;
            }
        }

        AddGaugeClamped(em + 0x390 + i * 4, amount);
    }

    constexpr uint32_t kComrade = 0x80769510u;

    if (Memory::Read8(kComrade)) {
        const int32_t amount = Memory::Read8(kComrade + 0x171) == 5 ? -5 : int32_t(game.Call<0x80164798>({em, table(4, 0x806A8E94u), kComrade, 2, 0}));
        AddGaugeClamped(em + 0x3A0, amount);
    }

    const uint32_t monsters = table(8, 0x806A8ECCu);
    const uint32_t monsterCount = game.Call<0x802BCFD0>({3}) & 0xFFFF;
    uint32_t other = game.Call<0x802BCF94>({3});

    for (uint32_t i = 0; i < monsterCount; ++i, other += 0xB18) {
        if (Memory::Read8(em + 2) == i || !Memory::Read8(other) || Memory::Read8(em + 3) == Memory::Read8(other + 3) || game.Call<0x80165D54>({em}) == 1 ||
            uint8_t(Memory::Read8(other + 0x1E5) - 0xB) < 2) {
            Memory::Write32(em + i * 4 + 0x3A4, 0);
            continue;
        }

        const int32_t amount = int32_t(game.Call<0x80164798>({em, monsters, other, 3, i}));
        AddGaugeClamped(em + 0x3A4 + i * 4, amount);
    }
}
} // namespace

extern "C" void func_801644A4(CpuContext* ctx);
extern "C" void MH3_EnemyAwarenessStep(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyAwarenessStep", func_801644A4, AwarenessStepPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x801644A4, MH3_EnemyAwarenessStep, "EnemyAwarenessStep");

extern "C" void func_80164B74(CpuContext* ctx);
extern "C" void MH3_EnemyTargetGauges(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyTargetGauges", func_80164B74, TargetGaugesPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x80164B74, MH3_EnemyTargetGauges, "EnemyTargetGauges");
