// Frame-count timers, RMHP08. C reconstructions of three game functions that
// convert seconds to frames with the game's frame rate (0x8065B4D0, the f32
// 60.0 / vcnt that the main loop computes once at boot: 30.0), decompiled
// with Ghidra (scripts/dol2elf.py -> ELF import) while tracing what ties
// game speed to the frame rate. Game logic is fixed-step: each of these
// timers counts down by 1 per frame, so they run at real speed only while
// the frame pacer holds 30 fps.
//
// Float math follows the PPC code exactly: each fmuls rounds to single in the
// order the game multiplies, and fctiwz truncates toward zero.
#include "abi_bridge.h"
#include "game_step.h"
#include "memory.h"
#include <initializer_list>

namespace {

constexpr uint32_t kFrameRate = 0x8076940C; // f32 frames per second
float FrameRate() {
    return Memory::ReadFloat32(kFrameRate);
}

float F32(uint32_t address) {
    return Memory::ReadFloat32(address);
}

struct Guest {
    CpuContext* cpu;
    uint32_t operator()(uint32_t fn, std::initializer_list<uint32_t> ints = {}, std::initializer_list<double> floats = {}) {
        uint32_t r = 3, f = 1;

        for (uint32_t v : ints)
            cpu->gpr[r++] = v;

        for (double v : floats)
            cpu->fpr[f++].d = v;

        InvokeIndirectCpu(fn, cpu);
        return cpu->gpr[3];
    }
};

// Guest stack frame below the caller's (keep the back chain aligned).
void PushFrame(CpuContext* ctx, uint32_t sp) {
    ctx->gpr[1] = (sp - 0x30) & ~0xFu;
    Memory::Write32(ctx->gpr[1], sp);
}

} // namespace

// 0x80167EB8(obj, kind).
extern "C" void MH3_TimedCheckByKind(CpuContext* ctx) {
    const CpuContext saved = *ctx;
    const uint32_t obj = saved.gpr[3];
    const uint8_t kind = uint8_t(saved.gpr[4]);
    PushFrame(ctx, saved.gpr[1]);
    Guest game{ctx};
    uint32_t result = 0;

    if (kind <= 5) {
        // Seconds per kind at 0x808A5D84, 0x808A5D8C..0x808A5D9C; factor at 0x808A5D88.
        static constexpr uint32_t kSeconds[] = {0x808A5D84, 0x808A5D8C, 0x808A5D90, 0x808A5D94, 0x808A5D98, 0x808A5D9C};
        const float frames = F32(kSeconds[kind]) * (F32(0x808A5D88) * FrameRate());
        const int32_t gap = int32_t(game(0x801F5F20)) - int32_t(game(0x801F5E78));
        result = double(gap) < double(frames) ? 1u : 0u;
    } else if (kind == 6) {
        result = 1;
        const uint8_t id10 = Memory::Read8(obj + 10), id2 = Memory::Read8(obj + 2);

        const uint32_t work = game(0x802BCF94, {3});
        const uint32_t count = game(0x802BCFD0, {3}) & 0xFFFFu;

        for (uint32_t i = 0, p = work; i < count && result; ++i, p += 0xB18)
            if (Memory::Read8(p) && Memory::Read8(p + 0x1E1) == id10 && Memory::Read8(p + 3) == id2)
                result = 0;
        for (uint32_t p = 0x807387E8; p < 0x807387E8 + 128 * 0x44 && result; p += 0x44)
            if (Memory::Read8(p) && Memory::Read8(p + 10) == id10 && Memory::Read8(p + 2) == id2 && Memory::Read8(p + 0xC) == 1)
                result = 0;
    }

    *ctx = saved;
    ctx->gpr[3] = result;
}
REGISTER_NATIVE_FUNCTION_AS(0x80167EB8, MH3_TimedCheckByKind, "TimedCheckByKind");

// 0x802EA658(self, player): per-frame skill timers. Returns bit 2 (value 4)
// for skill 0x97 or an active buff on self+0x48, value 2 while skill 0x96
// runs its periodic timer (self+0x184 period, self+0x188 random extra
// window), plus bit 0 when the player's 0x460 timer runs or cat skill 0x31
// sees a counter gap below a threshold.
extern "C" void MH3_PlayerSkillTimers(CpuContext* ctx) {
    const CpuContext saved = *ctx;
    const uint32_t self = saved.gpr[3], player = saved.gpr[4];
    PushFrame(ctx, saved.gpr[1]);
    Guest game{ctx};
    enum : uint32_t {
        Pl_Skill_ck = 0x803400D0,
        Pl_cat_skill_ck = 0x80341C10,
        CounterNow = 0x801F5F20,
        CounterStart = 0x801F5E78,
    };

    const uint32_t kPeriodScale = 0x808A5F94, kPeriodSeconds = 0x808A5FD8, kWindowSeconds = 0x808A5FDC;
    auto s16Positive = [](uint32_t address) {
        return int16_t(Memory::Read16(address)) > 0;
    };

    auto period = [&] {
        return int32_t(F32(kPeriodScale) * (F32(kPeriodSeconds) * FrameRate()));
    };

    uint32_t flags = 0;

    if (game(Pl_Skill_ck, {player, 0x97}) == 1) {
        flags = 4;
    } else {
        // 0x802D7E18, inlined: buff present, of type 5, with time left.
        const uint32_t buff = Memory::Read32(self + 0x48);

        if (Memory::Read8(buff) && Memory::Read8(buff + 0x420) == 5 && s16Positive(buff + 0x422)) {
            flags = 4;
        } else if (game(Pl_Skill_ck, {player, 0x96}) == 1) {
            flags = 2;

            // Both countdowns run once per tick of game time (none on a short
            // step, several on a long one): once per call at the stock step.
            for (uint32_t tick = 0, ticks = GameStep::Ticks(); tick < ticks; ++tick) {
                const int32_t window = int32_t(Memory::Read32(self + 0x188));

                if (window > 0)
                    Memory::Write32(self + 0x188, uint32_t(window - 1));

                const int32_t timer = int32_t(Memory::Read32(self + 0x184));

                if (timer <= 0) {
                    Memory::Write32(self + 0x184, uint32_t(period()));
                } else if (timer - 1 > 0) {
                    Memory::Write32(self + 0x184, uint32_t(timer - 1));
                } else {
                    Memory::Write32(self + 0x184, uint32_t(period()));

                    // 0x8045E620, inlined: the game's LCG rand.
                    const uint32_t seedAddress = saved.gpr[13] - 0x5A60;
                    const uint32_t seed = Memory::Read32(seedAddress) * 0x41C64E6Du + 12345u;
                    Memory::Write32(seedAddress, seed);
                    const int32_t roll = int32_t((seed >> 16) & 0x7FFF);

                    if (roll % 3 != 0)
                        Memory::Write32(self + 0x188, uint32_t(int32_t(F32(kWindowSeconds) * FrameRate())));
                }
            }
        }
    }

    // 0x8027E034, inlined: the player's timer at +0x460 is running.
    if (s16Positive(player + 0x460)) {
        flags |= 1;
    } else if (game(Pl_cat_skill_ck, {player, 0x31}) == 1) {
        const int32_t now = int32_t(game(CounterNow));
        const int32_t gap = now - int32_t(game(CounterStart));

        if (float(gap) < F32(kWindowSeconds) * FrameRate())
            flags |= 1;
    }

    *ctx = saved;
    ctx->gpr[3] = flags;
}
REGISTER_NATIVE_FUNCTION_AS(0x80194018, MH3_PlayerSkillTimers, "PlayerSkillTimers");

// 0x801F3964(self, result):
extern "C" void MH3_QuestResultInit(CpuContext* ctx) {
    const CpuContext saved = *ctx;
    const uint32_t self = saved.gpr[3];
    const int32_t result = int32_t(saved.gpr[4]);
    PushFrame(ctx, saved.gpr[1]);
    Guest game{ctx};
    constexpr uint32_t kQuestResultWork = 0x80768370; // get_qResult_work (0x802C57D4)
    constexpr uint32_t kVsUserWork = 0x807687A8;      // get_vsUser_work(n) (0x802C57E0): n < 2

    Memory::Write32(self + 0x2FF0, uint32_t(result));
    Memory::Write8(self + 2, 0);
    Memory::Write32(self + 0x2FF4, uint32_t(int32_t(F32(0x808A6278) * FrameRate())));
    Memory::Write8(self + 0x2FF8, 0);

    if (result == 1) {
        bool newRecord = false;
        const uint32_t questTime = Memory::Read32(kQuestResultWork + 0x148);
        const uint32_t slot = uint32_t(int32_t(int16_t(Memory::Read16(kQuestResultWork + 0x1E0) - 9000)) * 4);

        for (int32_t i = 0; i < int32_t(Memory::Read8(self + 7)); ++i) {
            // Reset the player's result row (inlined).
            const uint32_t row = self + 0x33DC + uint32_t(i) * 0x18;

            for (uint32_t b : {0u, 2u, 4u, 5u, 9u, 10u})
                Memory::Write8(row + b, 0);

            Memory::Write8(row + 1, uint8_t(i));
            Memory::Write8(row + 3, 2);
            Memory::Write8(row + 6, 4);
            Memory::Write8(row + 7, 4);
            Memory::Write8(row + 8, 0xFF);
            Memory::Write16(row + 12, 0);
            Memory::Write16(row + 14, 0);
            const uint32_t user = (i >= 0 && i < 2) ? kVsUserWork + uint32_t(i) * 0x100 : 0;
            const uint32_t best = user + (Memory::Read8(self + 7) == 1 ? 0x6C : 0x94) + slot;
            const uint32_t stored = Memory::Read32(best);

            if (stored == 0 || questTime < stored) {
                Memory::Write32(best, questTime);
                newRecord = true;
            }
        }

        if (newRecord)
            for (int32_t i = 0; i < int32_t(Memory::Read8(self + 7)); ++i)
                Memory::Write8(self + 0x33E4 + uint32_t(i) * 0x18, 0);
    } else if (result == 8) {
        Memory::Write16(self + 0x306C, 0x5A);
        game(0x802B524C);
    } else if (result == 9) {
        Memory::Write32(self + 0x33D4, 0xFFFFFFFF);
        Memory::Write32(self + 0x33D8, 0);
        Memory::Write8(self + 0x33DF, 4);
        Memory::Write8(self + 0x33DE, 0);
    }

    *ctx = saved;
}
REGISTER_NATIVE_FUNCTION_AS(0x801F3964, MH3_QuestResultInit, "QuestResultInit");
