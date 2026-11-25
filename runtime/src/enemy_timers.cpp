// Enemy step timers, RMHP08. Small per-step counters and periodic checks of
// the enemy classes, each taking the enemy in r3 (the fields are named for
// what the counters do; their users are the enemy AI):
//   0x801298C8  steps (s16 +0x452), counting up to 27000
//   0x80129A3C  two countdowns (s16 +0x200, +0x202), clamped at 0
//   0x801307C4  steps (s16 +0x440), counting up to 0x7FFF
//   0x801556C8  a 50-step cycle (u16 +0x33C) and a countdown (s16 +0x338)
//   0x80133B64  while a countdown (s16 +0x94C) runs: 0x80133A24 every 32 steps
//   0x80133920  while a countdown (s16 +0x812) runs: 0x80133874 every 32 steps
//               (at 8 mod 32); once it is out, +0x814 = 0
//   0x80132D50  when 0x8012E13C(+485, +486) is 1: 0x80135A54 and 0x80107868
//               (+516) every 32 steps
//   0x8011E750  (enemy, r4, s16 n, f1 scale): every n steps 0x8011E6BC(enemy,
//               r4, s32 +0x7A4 * scale)
//   0x80130D4C  (enemy, s16 r4, s16 n, f1 scale): every n steps
//               0x80130BD0(enemy, r4, u16 +0x8A0 * scale)
//   0x801C8978, 0x8038CC84  every 32 steps while 0x8012ECC4 is 1: effect
//               (23, 28) 50 up, (24, 20) 80 up (0x80105690)
//   0x8015574C  an action (state +5): first 0x801304DC and 0x8012F690(1, 10),
//               then after 150 steps (countdown +0x20) 0x80127FAC ends it
//   0x801311B4  sight: on every other step (at the parity of u8 +2) an awake
//               enemy (+485 not 11 or 12) marks the players it sees in +910
//               (0x80131514 from its eye joint), then may notice a target
//               of the table at 0x806A6AA0
//   0x8035BFC8  every 32 steps, at the enemy's own phase (u8 +2), a living
//               enemy may pick a new target (+827); then the per-step
//               bookkeeping of +829/+831 from +1083..+1085
// The originals move the counters by 1 per step and test the step counter
// (counter % n == k). Here the counters move by the whole ticks of game time
// in the step (GameStep::Ticks()) and the tests pass once every n ticks
// (StepCounter::Every), so they keep time at any step length; at the stock
// 1/30 s step (one tick) these are the originals. A phase the enemy holds
// (counter % n == its field) is out of reach of the step counter's load hooks
// (the phase is read after the counter), so only a port can test it in ticks. (The countdowns of
// 0x80133B64 and 0x80133920 were still counting steps before these ports.)
#include "cheats.h"
#include "game_step.h"
#include "guest_call.h"
#include "memory.h"
#include "port_check.h"
#include "step_counter.h"

#include <algorithm>

namespace {

int32_t S16(uint32_t address) {
    return int16_t(Memory::Read16(address));
}

void S16(uint32_t address, int32_t value) {
    Memory::Write16(address, uint16_t(value));
}

// Counts up by the step's ticks while below `cap`, stopping at it.
void CountUp(uint32_t address, int32_t cap) {
    const int32_t steps = S16(address);

    if (steps < cap)
        S16(address, std::min(steps + int32_t(GameStep::Ticks()), cap));
}

// Counts down by the step's ticks, stopping at 0 (a count below 0 becomes 0).
void CountDown(uint32_t address) {
    S16(address, std::max(S16(address) - int32_t(GameStep::Ticks()), 0));
}

// Disabled for RMHJ08: StepsTo27000 (PAL 0x801298C8) is inlined at JPN 0x80161CF0
// (handled via tick_sites.inc).
// void StepsTo27000(CpuContext* ctx) {
//     CountUp(ctx->gpr[3] + 0x452, 27000);
// }

void TwoCountdowns(CpuContext* ctx) {
    CountDown(ctx->gpr[3] + 0x200);
    CountDown(ctx->gpr[3] + 0x202);
}

void StepsTo7FFF(CpuContext* ctx) {
    CountUp(ctx->gpr[3] + 0x440, 0x7FFF);
}

void CycleAndCountdown(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];

    // The original: + 1, back to 0 at 50.
    const uint32_t cycle = Memory::Read16(self + 0x33C), next = cycle + GameStep::Ticks();
    Memory::Write16(self + 0x33C, uint16_t(next < 50 ? next : cycle < 50 ? next % 50 : 0));

    // The original: - 1 while above 0.
    const int32_t countdown = S16(self + 0x338);

    if (countdown > 0)
        S16(self + 0x338, std::max(countdown - int32_t(GameStep::Ticks()), 0));
}

constexpr uint32_t kSystem = 0x80768AA8; // system_w (its address is in r4 at some calls)

void CopyVec(uint32_t dst, uint32_t src) {
    for (uint32_t k = 0; k < 12; k += 4)
        Memory::Write32(dst + k, Memory::Read32(src + k));
}

void SlowCheckWhileCounting(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);

    if (S16(self + 0x94C) <= 0)
        return;

    if (StepCounter::Every(32))
        game.Call<0x8016E7CC>({self, kSystem});
    // Felvine lasts forever (cheats.h): the countdown's store dropped.
    if (Cheats::On(Cheats::Id::FelvineForever))
        return;

    // Read again: the call may change it (and leave it at 0, which the
    // original then takes 1 from as well).
    const int32_t left = S16(self + 0x94C), ticks = int32_t(GameStep::Ticks());
    S16(self + 0x94C, left > 0 ? std::max(left - ticks, 0) : left - ticks);
}

void CheckWhileCounting(CpuContext* ctx) {
    GuestFrame frame(ctx, 32);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const int32_t countdown = S16(self + 0x812);

    if (countdown > 0) {
        S16(self + 0x812, std::max(countdown - int32_t(GameStep::Ticks()), 0));

        const uint32_t kind = Memory::Read8(self + 3);
        const uint32_t mode = Memory::Read8(self + 0x1E5);
        const uint32_t sub = Memory::Read8(self + 0x1E6);

        if (StepCounter::Every(32, 8) && kind != 0x28 && mode != 11 && !(mode == 12 && sub == 0xFF)) {
            const uint32_t vec = frame[8];
            game.Call<0x8028A7F4>({vec}, {0.0, 0.0, 0.0});
            game.Call<0x80171898>({self});
            const float scale = game.F1();
            const uint32_t bone = Memory::Read32(self + 0x204);
            game.Call<0x80046118>({self, vec, bone != 0xFFFFFFFFu ? bone : 2u, 0x12}, {double(scale)});
        }
    }

    if (S16(self + 0x812) <= 0)
        S16(self + 0x814, 0);
}

void PeriodicCheck(CpuContext* ctx) {
    GuestFrame frame(ctx, 48);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const uint32_t mode = Memory::Read8(self + 0x1E5);
    const uint32_t sub = Memory::Read8(self + 0x1E6);

    const bool firstAction = mode == 0xA && (sub == 0x7C || sub == 0x82 || sub == 0x8B || sub == 0x95 || sub == 0x9C || sub == 0xBB || sub == 0xE8 || sub == 0xEE);

    if (firstAction && StepCounter::Every(32)) {
        game.Call<0x8017184C>({self});
        game.Call<0x8004641C>({self, Memory::Read32(self + 516)});
    }

    const bool secondAction = (mode == 0xA && (sub == 0x7B || sub == 0x81 || sub == 0x8A || sub == 0x94 || sub == 0x9B || sub == 0xBA || sub == 0xE7 || sub == 0xED || sub == 0xF8)) ||
                              Memory::Read8(self + 0x8AB) != 0 || (mode == 0xB && sub >= 0x1A && sub < 0x1D);

    if (secondAction && StepCounter::Every(32, 16)) {
        const uint32_t enemyData = game.Call<0x8017E990>({Memory::Read8(self + 3), Memory::Read8(self + 0xA)});
        const uint32_t entry = Memory::Read32(Memory::Read32(enemyData + 0x9C) + 0xC);
        const uint32_t buf = frame[8];

        if (entry != 0) {
            const float x = Memory::ReadFloat32(entry + 4);
            const float y = Memory::ReadFloat32(entry + 8);
            const float z = Memory::ReadFloat32(entry + 12);
            Memory::WriteFloat32(buf, x);
            Memory::WriteFloat32(buf + 4, y);
            Memory::WriteFloat32(buf + 8, z);
            Memory::WriteFloat32(buf + 12, x);
            Memory::WriteFloat32(buf + 16, y);
            Memory::WriteFloat32(buf + 20, z);
            game.Call<0x80046118>({self, buf, Memory::Read32(entry), 0xA}, {double(Memory::ReadFloat32(entry + 0x10))});
        } else {
            Memory::WriteFloat32(buf, 0.0f);
            Memory::WriteFloat32(buf + 4, 100.0f);
            Memory::WriteFloat32(buf + 8, 0.0f);
            Memory::WriteFloat32(buf + 24, 0.0f);
            Memory::WriteFloat32(buf + 28, 100.0f);
            Memory::WriteFloat32(buf + 32, 0.0f);
            game.Call<0x80046118>({self, buf, 2, 0xA}, {2.0});
        }
    }
}

void EveryNScaled(CpuContext* ctx) {
    GuestFrame frame(ctx, 32);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const int32_t arg = int32_t(frame.Arg(4));

    if (!StepCounter::Every(uint32_t(int32_t(int16_t(frame.Arg(5))))))
        return;

    const int32_t hp = int32_t(Memory::Read32(self + 0x7A0));
    const int32_t maxHp = int32_t(Memory::Read32(self + 0x7A4));
    const int32_t cap = std::max(hp, int32_t(float(float(maxHp) * ctx->fpr[1].d)));
    int32_t next = std::clamp(hp + arg, 0, cap);
    const int32_t floorHp = int32_t(float(maxHp) * Memory::ReadFloat32(self + 0x7B0));
    if (next < floorHp)
        next = floorHp;
    Memory::Write32(self + 0x7A0, uint32_t(next));
}

void EveryNScaledU16(CpuContext* ctx) {
    GuestFrame frame(ctx, 32);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3), arg = uint32_t(int32_t(int16_t(frame.Arg(4))));

    if (!StepCounter::Every(uint32_t(int32_t(int16_t(frame.Arg(5))))))
        return;

    const float amount = float(float(Memory::Read16(self + 0x8A0)) * ctx->fpr[1].d);
    game.Call<0x8016AE44>({self, arg, uint32_t(int32_t(amount)) & 0xFFFF});
}

void TimedAction(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);

    switch (Memory::Read8(self + 5)) {
    case 0:
        Memory::Write8(self + 5, 1);
        game.Call<0x8016A708>({self, 0});
        Memory::Write32(self + 0x20, 150);
        game.Call<0x80169284>({self, 1, 10, 0});
        break;
    case 1: {
        // The original: - 1, the action ends once it is below 0. Here it ends
        // once the ticks pass 0 (the count stops at -1).
        const int32_t left = int32_t(Memory::Read32(self + 0x20));
        const int32_t now = std::max(left - int32_t(GameStep::Ticks()), std::min(left - 1, -1));
        Memory::Write32(self + 0x20, uint32_t(now));

        if (now < 0)
            game.Call<0x8015FDDC>({self, uint32_t(left)});

        break;
    }
    }
}

// 0x801427C8 (RMHP08 0x8035BFC8).
void RetargetEvery32(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const auto u8 = [&](uint32_t offset) {
        return Memory::Read8(self + offset);
    };

    const auto set = [&](uint32_t offset, uint32_t value) {
        Memory::Write8(self + offset, uint8_t(value));
    };

    const auto alive = [&] {
        return game.Call<0x80165DF8>({self}) == 0;
    };                                                         // em_die_ck
    const auto roll = [&] {                                    // 0x80165CDC (enemy, s16 0..15, 0)
        const uint32_t pick = game.Call<0x802BC314>({0}) & 15; // ran_suu
        game.Call<0x80165CDC>({self, pick, 0});
    };

    const auto newTarget = [&] {
        set(827, 255);
        roll();
        game.Call<0x8016A9DC>({self, 0, 0});
    };

    game.Call<0x8014595C>({self});

    if (u8(10) != 1) {
        // The original: (steps & 31) == +2, never for +2 of 32 or more.
        if (alive() && u8(484) != 1 && u8(2) < 32 && StepCounter::Every(32, u8(2))) {
            if (u8(831) == 2) {
                if (game.Call<0x8020C8A8>({u8(827), u8(481)}) == 0 && u8(826) == 0) {
                    newTarget();
                    set(484, 0);
                    set(831, 0);
                }
            } else if (u8(1083) == 5 && u8(1084) == 1 && game.Call<0x8020C8A8>({u8(827), u8(481)}) == 0) {
                if (game.Call<0x8014544C>({u8(481)}) == 1)
                    game.Call<0x80145710>({self, 1});
                else
                    newTarget();
            }
        }

        if (u8(2099) == 1)
            game.Call<0x801650C4>({33, u8(481)});

        if (alive()) {
            if (u8(1085) == 1 && u8(829) != 1) {
                roll();

                if (u8(831) < 3)
                    set(831, 0);
            }

            if ((u8(1083) == 0 || u8(1083) == 5) && u8(1084) == 0 && u8(484) != 1 && u8(485) != 12) {
                if (game.Call<0x8014544C>({u8(481)}) == 1) {
                    set(831, 0);

                    if (u8(1083) == 0)
                        roll();

                    game.Call<0x80145710>({self, 0});
                } else {
                    game.Call<0x8016A9DC>({self, 0, 0});
                }
            }
        }
    }

    set(829, u8(1085));
}

// 0x8016B4C8 (RMHP08 0x801311B4).
void Sight(CpuContext* ctx) {
    GuestFrame frame(ctx, 176);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const auto u8 = [&](uint32_t offset) {
        return Memory::Read8(self + offset);
    };

    const uint32_t eye = frame[8], target = frame[20], look = frame[32], mtx = frame[56];
    constexpr uint32_t kPlayerSize = 2848;
    const uint32_t players = game.Call<0x802BCFD0>({2}) & 0xFFFF; // get_move_work_max
    uint32_t player = game.Call<0x802BCF94>({2});                 // get_move_work_adrs
    Memory::Write8(self + 910, 0);

    if (uint8_t(u8(485) - 11) <= 1)
        return;

    if (game.Call<0x8016CA58>({self, 1}) == 1 || game.Call<0x8016CA28>({self}) == 1 || u8(504) == 1)
        return; // em_sleep_ck / inlined checks
    // The eye: the joint EnemyData->+0xA0->+0x18[+0x38B] names, or 50 above +392 with +448.
    const uint32_t enemyData = game.Call<0x8017E990>({u8(3), u8(10)});
    const uint32_t joint = Memory::Read32(Memory::Read32(enemyData + 0xA0) + 0x18) + u8(0x38B) * 0x18;
    uint32_t facing;

    if (Memory::Read32(joint) == 0xFFFFFFFF) {
        CopyVec(eye, self + 392);
        Memory::WriteFloat32(eye + 4, Memory::ReadFloat32(eye + 4) + 50.0f);
        facing = Memory::Read32(self + 448);
    } else {
        game.Call<0x80171798>({self, Memory::Read32(joint), mtx}); // get_joint_wmat_em
        for (uint32_t k = 0; k < 3; ++k)
            Memory::Write32(eye + 4 * k, Memory::Read32(mtx + 12 + 16 * k));

        facing = game.Call<0x8028BAD0>({mtx});
    }

    if (int16_t(Memory::Read16(self + 2326)) > 0)
        return;
    // The original: on the steps where (steps & 1) == (+2 & 1).
    if ((u8(908) & 1) && StepCounter::Every(2, u8(2) & 1)) {
        for (uint32_t i = 0; i < players; ++i, player += kPlayerSize) {
            if (Memory::Read8(player) == 0)
                continue;

            const uint32_t id = Memory::Read8(player + 8);
            bool leaving = false;
            if (game.Call<0x8039BA94>() == 1) {
                const uint32_t mgr = game.Call<0x802BCF94>({0});
                if (mgr && Memory::Read8(mgr + 0x22DD + id) == 4)
                    leaving = true;
            }
            if (leaving || Memory::Read8(player + 10) == 8)
                continue;

            bool canSee = false;
            if (u8(0x1E1) == Memory::Read8(player + 0x16)) {
                if ((game.Call<0x8037DEF0>({u8(0x1E0)}) & 0xFF) == 9) {
                    const uint32_t underwater = game.Call<0x801A08E0>({id}) & 0xFF;
                    const uint32_t inWater = game.Call<0x8037DF0C>({self + 0x188});
                    canSee = underwater == 0 ? (inWater == 1) : (inWater == 0);
                } else {
                    canSee = true;
                }
            }
            if (!canSee)
                continue;

            const float range = Memory::ReadFloat32(self + 1108 + 4 * i);

            if (!(range >= 0.0f))
                continue;

            CopyVec(target, player + 60);
            Memory::WriteFloat32(target + 4, Memory::ReadFloat32(target + 4) + 100.0f);

            if (!(Memory::Read32(self + 456) & 0x8000) && game.Call<0x8016B918>({self, joint, eye, target, facing}, {range}) != 1)
                continue;

            Memory::Write8(self + 910, uint8_t(u8(910) | (1u << i)));
            game.Call<0x8016BCDC>({self, i & 0xFF});
        }
    }

    if (game.Call<0x80155DD4>({self, 4}) == 0 || game.Call<0x80165F68>({self}) == 0)
        return;

    if (game.Call<0x80155DD4>({self, 8}) == 0 && u8(482) == 4)
        return;

    if (!(u8(908) & 2))
        return;
    // The joint's 22-byte record, with 10012 in its last half-word.
    for (uint32_t k = 0; k < 20; k += 4)
        Memory::Write32(look + k, Memory::Read32(joint + k));

    Memory::Write16(look + 20, 10012);

    for (uint32_t entry = 0x80737928, j = 0; j < 8; ++j, entry += 20) {
        if (Memory::Read8(entry + 19) != 1 || Memory::Read8(entry + 18) != u8(481))
            continue;

        if (game.Call<0x8016BA74>({look, eye, entry, facing}, {Memory::ReadFloat32(entry + 12)}) == 1) {
            game.Call<0x8016BB58>({self, 1});
            return;
        }
    }
}

} // namespace

// Disabled for RMHJ08: EnemyStepsTo27000 (PAL 0x801298C8) is inlined at 0x80161CF0 (tick_sites.inc).

extern "C" void func_80161E08(CpuContext* ctx);
extern "C" void MH3_EnemyTwoCountdowns(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyTwoCountdowns", func_80161E08, TwoCountdowns);
}
REGISTER_NATIVE_FUNCTION_AS(0x80161E08, MH3_EnemyTwoCountdowns, "EnemyTwoCountdowns");

extern "C" void func_8016AA08(CpuContext* ctx);
extern "C" void MH3_EnemyStepsTo7FFF(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyStepsTo7FFF", func_8016AA08, StepsTo7FFF);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016AA08, MH3_EnemyStepsTo7FFF, "EnemyStepsTo7FFF");

extern "C" void func_80086278(CpuContext* ctx);
extern "C" void MH3_EnemyCycleAndCountdown(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyCycleAndCountdown", func_80086278, CycleAndCountdown);
}
REGISTER_NATIVE_FUNCTION_AS(0x80086278, MH3_EnemyCycleAndCountdown, "EnemyCycleAndCountdown");

extern "C" void func_8016E934(CpuContext* ctx);
extern "C" void MH3_EnemySlowCheckWhileCounting(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemySlowCheckWhileCounting", func_8016E934, SlowCheckWhileCounting);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016E934, MH3_EnemySlowCheckWhileCounting, "EnemySlowCheckWhileCounting");

extern "C" void func_8016E600(CpuContext* ctx);
extern "C" void MH3_EnemyCheckWhileCounting(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyCheckWhileCounting", func_8016E600, CheckWhileCounting);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016E600, MH3_EnemyCheckWhileCounting, "EnemyCheckWhileCounting");

extern "C" void func_8016DB2C(CpuContext* ctx);
extern "C" void MH3_EnemyPeriodicCheck(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyPeriodicCheck", func_8016DB2C, PeriodicCheck);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016DB2C, MH3_EnemyPeriodicCheck, "EnemyPeriodicCheck");

extern "C" void func_80155E78(CpuContext* ctx);
extern "C" void MH3_EnemyEveryNScaled(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyEveryNScaled", func_80155E78, EveryNScaled);
}
REGISTER_NATIVE_FUNCTION_AS(0x80155E78, MH3_EnemyEveryNScaled, "EnemyEveryNScaled");

extern "C" void func_8016B078(CpuContext* ctx);
extern "C" void MH3_EnemyEveryNScaledU16(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyEveryNScaledU16", func_8016B078, EveryNScaledU16);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016B078, MH3_EnemyEveryNScaledU16, "EnemyEveryNScaledU16");

// Disabled for RMHJ08: PAL PeriodicEffect50 (0x801C8978) and PeriodicEffect80 (0x8038CC84)
// are inlined into larger state machine functions (0x80094730 and 0x800C25E4), whose
// step-counter modulo checks (0x80094758 and 0x800C260C) are already hooked via step_counter_sites.inc.

extern "C" void func_800862B0(CpuContext* ctx);
extern "C" void MH3_EnemyTimedAction(CpuContext* ctx) {
    if (Memory::Read8(ctx->gpr[3] + 0x1E6) == 0) {
        PortCheck::Call(ctx, "EnemyTimedAction", func_800862B0, TimedAction);
        return;
    }

    func_800862B0(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x800862B0, MH3_EnemyTimedAction, "EnemyTimedAction");

extern "C" void func_801427C8(CpuContext* ctx);
extern "C" void MH3_EnemyRetargetEvery32(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyRetargetEvery32", func_801427C8, RetargetEvery32);
}
REGISTER_NATIVE_FUNCTION_AS(0x801427C8, MH3_EnemyRetargetEvery32, "EnemyRetargetEvery32");

extern "C" void func_8016B4C8(CpuContext* ctx);
extern "C" void MH3_EnemySight(CpuContext* ctx) {
    PortCheck::Fuzz(ctx, 'E', ctx->gpr[3]);
    PortCheck::Call(ctx, "EnemySight", func_8016B4C8, Sight);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016B4C8, MH3_EnemySight, "EnemySight");
