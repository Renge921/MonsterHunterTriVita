// Shells, objects and enemies moved by velocity, and the player's status
// tint, RMHP08: per-step motion scaled by the step length.
//
// Shells (_SHELL_W: projectiles, thrown and rolling objects; the quest_data
// RSO runs most of them) move once a step through two helpers, decompiled
// from the disassembly:
//   shell_rate_add   (0x802AB3A8): position (+0x18) += velocity (+0x3C)
//   shell_rate_add_g (0x802AB3B8): the same, then velocity += acceleration (+0x48)
// Here both amounts are taken over this step's length s (GameStep::Scale), so
// shells fly and fall at the same speed at any step length. At the stock
// 1/30 s step (s = 1) this is the original.
//
// Enemies and other objects move the same way through VecAddInPlace
// (0x80073FCC: *r3 += *r4 through PSVECAdd, returns r3), called once a step
// with position += velocity or velocity += acceleration from the places in
// kRateCalls (by return address): the enemy helpers 0x8013548C (+0x188 +=
// +0x310, +0x310 += +0x31C; 0x8013547C, the position add alone, is its own
// native below) and 0x80135558 (+0x188 += +0x310 rotated by the
// heading, for flight), the monsters that call it inline (0x801A0CAC,
// a desert enemy's swim, ...), the camera demo actor, the ambient creatures,
// falling debris, the desert object group. For those the addend is taken over this step's length; the
// other 200-odd callers (offsets, collision push-outs, one-off sums) add it
// whole. The desert enemy also takes the distance it moved, the length of
// its velocity (VecMagWrapper 0x80050F88, through PSVECMag), off a budget
// (+0x378) each step: at those three calls the length is scaled likewise.
//
// An enemy's height offset (f32 +0x1AC, the body raised or lowered from its
// position: burrowing, surfacing, landing, hovering) moves through
// EnemyHeightAdd (0x80130460: +0x1AC += f1, returns it). A state sets it
// (0x80130450) when it starts and adds an amount each step until a bound:
// constants (+5, -2, +10 a step, the 0x80154D08 sink by 5), a motion curve
// times the motion rate (0x801356A8), the vertical speed +0x314 (0x80135510),
// a twentieth of a gap. All 37 calls are of that kind, so the amount is taken
// over this step's length. Kind 8's rise at a Tundra quest's start
// (0x8015F884: +10 a step from -1270) was found with a step probe online.
//
// The player's status tint (0x802E77EC, per player from 0x802E79D0) advances
// two u16 phases (+0x13E, +0x140) once a step: by 960, 1456, 512 or 768, or a
// rate from a table (0x805C6434) through three helpers, and sets them to 0
// when the status ends. The wrapper lets the original run, then moves each
// phase on by its share of the advance, GameStep::Share. The colours the
// original derived from the phase this step lead by the rest of one step's
// advance (a 1/30 s lead at most); the phases themselves keep game time.
#include "game_step.h"
#include "memory.h"
#include "port_check.h"
#include "ppc_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace {

constexpr uint32_t kPosition = 0x18, kVelocity = 0x3C, kAcceleration = 0x48; // f32 x, y, z

// v += d * s for the f32 vectors at v and d.
void AddScaled(uint32_t v, uint32_t d, float s) {
    for (uint32_t i = 0; i < 12; i += 4)
        MemoryInline::FlatWriteFloat32(v + i, MemoryInline::FlatReadFloat32(v + i) + MemoryInline::FlatReadFloat32(d + i) * s);
}

void ShellRateAddPort(CpuContext* ctx) {
    const uint32_t shell = ctx->gpr[3];
    AddScaled(shell + kPosition, shell + kVelocity, float(GameStep::Scale()));
}

void ShellRateAddGPort(CpuContext* ctx) {
    const uint32_t shell = ctx->gpr[3];
    const float s = float(GameStep::Scale());
    AddScaled(shell + kPosition, shell + kVelocity, s);
    AddScaled(shell + kVelocity, shell + kAcceleration, s);
}

// VecAddInPlace (PAL 0x80073FCC) and VecMagWrapper (PAL 0x80050F88) remain
// disabled on RMHJ08 because JPN uses direct paired-single velocity helpers
// (e.g. 0x80170F84 EnemyAddVelocity, 0x80170FA8 EnemyAddVelAcc) and PSVECMag.
#if 0
// VecAddInPlace calls that move by a rate, by return address (the bl + 4).
constexpr uint32_t kRateCalls[] = {
    0x801354ACu, 0x801354B8u,                           // 0x8013548C enemy: position += velocity, velocity += acceleration
    0x801355D0u,                                        // 0x80135558 enemy: position += rotated velocity (flying)
    0x80135650u,                                        // 0x8013562C enemy: velocity += acceleration
    0x80135C08u, 0x80135C14u,                           // 0x80135BE8: +0x14 += +0x5C, +0x5C += +0x68
    0x801A07DCu, 0x801A0AE4u, 0x801A0EBCu,              // desert enemy swims: position += velocity
    0x801BA68Cu, 0x801BA6C4u, 0x801BA950u, 0x801BA988u, // 0x801BA438: +0x34C += velocity, velocity += acceleration
    0x801CC344u, 0x801CC368u,                           // 0x801CBD94: position += velocity
    0x801CF5D4u, 0x801CF614u,                           // 0x801CF124: position += velocity
    0x801D2674u, 0x801D2698u,                           // 0x801D2208: position += velocity
    0x802C35C4u, 0x802C35D0u,                           // 0x802C3444: +0x178 += +0x1D4, +0x1D4 += +0x1E0
    0x802D3304u,                                        // 0x802D32E0: +0x1D4 += +0x1E0
    0x802D8578u,                                        // 0x802D853C camera demo actor: +0x178 += +0x470
    0x802F513Cu,                                        // 0x802F511C: position += velocity
    0x80308A18u,                                        // 0x80308960 debris: position += velocity
    0x8031E48Cu,                                        // 0x8031DF1C: +0x14 += +0x20
    0x80328E2Cu,                                        // 0x80328BDC enemy: velocity += acceleration
    0x80369404u, 0x80369818u, 0x80369C54u,              // 0x803692F0 desert object group: position += velocity
};

bool IsRateCall(uint32_t lr) {
    return std::binary_search(std::begin(kRateCalls), std::end(kRateCalls), lr);
}

void VecAddInPlace(uint32_t lr, uint32_t out, uint32_t add) {
    const double s = GameStep::Scale();
    AddScaled(out, add, s != 1.0 && IsRateCall(lr) ? float(s) : 1.0f);
}

void VecAddInPlacePort(CpuContext* ctx) {
    VecAddInPlace(ctx->lr, ctx->gpr[3], ctx->gpr[4]);
}

// VecMagWrapper calls that measure a step's move, by return address.
constexpr uint32_t kStepLengthCalls[] = {0x801A07E4u, 0x801A0AECu, 0x801A0EC4u}; // then +0x378 -= it

// PSVECMag: sqrt(x*x + y*y + z*z) as the paired-single code computes it (the
// same float helpers as its translation), the frsqrte estimate refined by one
// Newton step; 0 for a zero vector.
float VecMag(uint32_t v) {
    const double xy = PPC_PsMulInline(PpcPackPairedInline(MemoryInline::FlatReadFloat32(v), MemoryInline::FlatReadFloat32(v + 4)),
                                      PpcPackPairedInline(MemoryInline::FlatReadFloat32(v), MemoryInline::FlatReadFloat32(v + 4)));
    const double z = PPC_PsFromScalarInline(MemoryInline::FlatReadFloat32(v + 8));
    const double sq = PPC_PsToScalarInline(PPC_PsSum0Inline(PPC_PsMaddInline(z, z, xy), xy, xy));

    if (sq == 0.0)
        return float(sq);

    const double e = PPC_Frsqrte(sq);
    const double refine = PPC_Fnmsubs(PpcFmulsInline(e, e), sq, 3.0); // 3 - e*e*sq
    return float(PpcFmulsInline(sq, PpcFmulsInline(refine, PpcFmulsInline(e, 0.5))));
}

float VecMagWrapper(uint32_t lr, uint32_t v) {
    const float mag = VecMag(v);
    const double s = GameStep::Scale();

    if (s != 1.0 && std::find(std::begin(kStepLengthCalls), std::end(kStepLengthCalls), lr) != std::end(kStepLengthCalls))
        return float(mag * s);

    return mag;
}

void VecMagWrapperPort(CpuContext* ctx) {
    ctx->fpr[1].d = VecMagWrapper(ctx->lr, ctx->gpr[3]);
}
#endif

constexpr uint32_t kTintPhases[] = {0x13E, 0x140}; // u16

} // namespace

extern "C" void func_80362D28(CpuContext* ctx);
extern "C" void MH3_ShellRateAdd(CpuContext* ctx) {
    PortCheck::Call(ctx, "ShellRateAdd", func_80362D28, ShellRateAddPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x80362D28, MH3_ShellRateAdd, "ShellRateAdd");

extern "C" void func_80362D4C(CpuContext* ctx);
extern "C" void MH3_ShellRateAddG(CpuContext* ctx) {
    PortCheck::Call(ctx, "ShellRateAddG", func_80362D4C, ShellRateAddGPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x80362D4C, MH3_ShellRateAddG, "ShellRateAddG");

// Disabled unported PAL address for RMHJ08: REGISTER_NATIVE_FUNCTION_AS(0x80073FCC, MH3_VecAddInPlace, "VecAddInPlace");

// 0x80170F84: enemy position (+0x188) += velocity (+0x310) (leaves r3/r4 untouched).
// 0x80170FA8: enemy position (+0x188) += velocity (+0x310), velocity += acceleration (+0x31C).
constexpr uint32_t kEnemyPosition = 0x188, kEnemyVelocity = 0x310, kEnemyAcceleration = 0x31C;
static void EnemyAddVelocityPort(CpuContext* ctx) {
    const uint32_t enemy = ctx->gpr[3];
    AddScaled(enemy + kEnemyPosition, enemy + kEnemyVelocity, float(GameStep::Scale()));
}

extern "C" void func_80170F84(CpuContext* ctx);
extern "C" void MH3_EnemyAddVelocity(CpuContext* ctx) {
#ifdef MH3_DEVELOPMENT
    const bool check = PortCheck::Enabled("EnemyAddVelocity");

    if (check) [[unlikely]] {
        PortCheck::RunOutput(ctx, "EnemyAddVelocity", func_80170F84, EnemyAddVelocityPort, {{ctx->gpr[3] + kEnemyPosition, 12}});
        return;
    }
#endif
    EnemyAddVelocityPort(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x80170F84, MH3_EnemyAddVelocity, "EnemyAddVelocity");

static void EnemyAddVelAccPort(CpuContext* ctx) {
    const uint32_t enemy = ctx->gpr[3];
    const float s = float(GameStep::Scale());
    AddScaled(enemy + kEnemyPosition, enemy + kEnemyVelocity, s);
    AddScaled(enemy + kEnemyVelocity, enemy + kEnemyAcceleration, s);
}

extern "C" void func_80170FA8(CpuContext* ctx);
extern "C" void MH3_EnemyAddVelAcc(CpuContext* ctx) {
#ifdef MH3_DEVELOPMENT
    const bool check = PortCheck::Enabled("EnemyAddVelAcc");

    if (check) [[unlikely]] {
        PortCheck::RunOutput(ctx, "EnemyAddVelAcc", func_80170FA8, EnemyAddVelAccPort,
                             {{ctx->gpr[3] + kEnemyPosition, 12}, {ctx->gpr[3] + kEnemyVelocity, 12}});
        return;
    }
#endif
    EnemyAddVelAccPort(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x80170FA8, MH3_EnemyAddVelAcc, "EnemyAddVelAcc");

// Disabled unported PAL address for RMHJ08: REGISTER_NATIVE_FUNCTION_AS(0x80050F88, MH3_VecMagWrapper, "VecMagWrapper");

extern "C" void func_801911BC(CpuContext* ctx);
extern "C" void MH3_PlayerTintPulse(CpuContext* ctx) {
    const uint32_t work = ctx->gpr[3];
    uint16_t before[2];

    for (int i = 0; i < 2; ++i)
        before[i] = Memory::Read16(work + kTintPhases[i]);

    func_801911BC(ctx);

    if (GameStep::Scale() == 1.0)
        return;

    for (int i = 0; i < 2; ++i) {
        const uint16_t after = Memory::Read16(work + kTintPhases[i]);

        if (after == 0 || after == before[i])
            continue; // ended, or not advancing
        const int32_t advance = uint16_t(after - before[i]);
        Memory::Write16(work + kTintPhases[i], uint16_t(before[i] + GameStep::Share(advance)));
    }
}
REGISTER_NATIVE_FUNCTION_AS(0x801911BC, MH3_PlayerTintPulse, "PlayerTintPulse");

extern "C" void func_8016A678(CpuContext* ctx);
extern "C" void MH3_EnemyHeightAdd(CpuContext* ctx) {
    ctx->fpr[1].d = float(ctx->fpr[1].d * GameStep::Scale());
    func_8016A678(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8016A678, MH3_EnemyHeightAdd, "EnemyHeightAdd");
