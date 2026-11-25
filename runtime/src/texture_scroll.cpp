// Scrolling texture effect matrices, RMHP08. Three per-step updates move a
// material's texture effect matrix (its translation, a UV offset) on by a
// constant each step: the matrix itself is the accumulator, read with
// ResMatTexCoordGen::GetEffectMtx and written back with SetEffectMtx. Here the
// offset moves on by that constant times the step length, so the textures
// scroll at the same speed at any step length. At the stock 1/30 s step each
// is the original.
//
//   0x803750E8 Ceadeus (its enemy work; the model at +316 is the work's
//     +0x13C), seen in its lair and its intro cutscene: materials 8 and 9 of
//     the model at +316: V offset + 0.0025 (sda2 -7740) a step, back by 1
//     past 1; U offset a sine of the tick counter (system_w+0xC << 7), here
//     of the counter plus the step's fraction of a tick. Then TEV colours: material 6 register 3 from the
//     bytes at +825..827, and register 0 of materials 0-3 white when +481
//     is set and the act (+485/+486, 0x8012D270) is not 13/2, else grey 150.
//   0x801A9274 when the kind at +480 (through the map at 0x805CF500, 255: as
//     is) is 6 and +481 is 1: material 6 of the model at +316, U offset
//     - const (sda2 -21892) a step, + wrap (-21880) below the limit (-21864).
//     Then material 6 TEV colour register 4 (0x800E2B58 reads it): alpha =
//     const (-20920) * f32 +468, or 0 when not kind 6 / +481 1, written with
//     0x800E2950.
//   0x8031ED54 per step for each object of the list at +56 (count, then
//     pointers from +4), the parts of effect 45 (eft045_set 0x8031EBB8): the
//     object's model (res_eft_model_create 0x800F8EC0, 67, 268); without it
//     the pass ends (0x8031F364, skipping 0x8031F050); else it places the object (0x80041EA4, even
//     objects at +36, odd ones through 0x80051278 / 0x80051454 / 0x80050D7C),
//     scales +28 by a random 0..19 step (0x800CEF20, the game's RNG 0), sets
//     +48 to another random, +56 = 1, +53 = 1, and scrolls material 0 of the
//     model at +280: U offset - const (sda2 -11224) a step, wrapped into
//     [-11240, -11244). The byte at +5 counts passes; the list's s16 at +22 is
//     the object index. The pass count and the random scale and +48 (the
//     parts' flicker) are per tick: the count moves on by the step's whole
//     ticks, each object rolls once per whole tick (twice RNG 0 each, so the
//     stream moves as at 30 Hz), a step without one keeps its last roll, and
//     an object new between ticks shows the next roll without taking it.
//
// ResMatTexCoordGen (nw4r g3d): the handle's data (0x8006E310, 0x800878D4 its
// null check) holds 8 texture coordinate entries of 52 bytes; +171 flags (bit
// 0: identity), +172 the 3x4 effect matrix.
//   0x80095434 GetEffectMtx(handle, index, out): copies entry index's matrix.
//   0x800953A8 SetEffectMtx(handle, index, mtx): stores it (identity for
//     null, setting the flag). Both return 0 for an index past 7.
#include "game_step.h"
#include "guest_call.h"
#include "port_check.h"
#include "res_anm_keys.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

enum : uint32_t {
    GetResMat = 0x805390D4,         // (out, model, material index)
    ResMatTexCoordGen = 0x80538F24, // (resmat, 0) -> handle data
    HandleCopy = 0x8000446C,        // (out, in): *out = *in
    SinFIdx = 0x8053D3D8,           // (f1 fidx) -> f1
    GetTevColor = 0x802A4EE4,       // (mdl, material, register, out)
    SetTevColor = 0x802A4F64,       // (mdl, material, register, color)
    GetTevKColor = 0x802A4D18,      // (mdl, material, register, out) -> 1 if present
    SetTevKColor = 0x802A4AB4,      // (mdl, material, register, color, 0)
    CreateModel = 0x80037504,       // res_eft_model_create (MHchar, 67, 268) -> 0 when it failed
    EndPass = 0x8006D68C,           // (this)
    FinishPass = 0x8006D354,        // (this)
    RotateZXY = 0x8028BFD0,         // rotVecZXY (v, CP_VECTOR angles)
    VecAngles = 0x8028B944,         // calcVecAngXY (v, out x, out y)
};

constexpr uint32_t r2 = 0x808A93C0;           // the sda2 base
constexpr uint32_t kTickCounter = 0x80768AB4; // system_w+0xC (ticks, step_counter.cpp)
constexpr uint32_t kRandSeed = 0x80768AC0;    // system_w+0x18: RNG 0 (s16)
constexpr uint32_t kKindMap = 0x806E4520;     // u8[256], 255: unmapped
constexpr uint32_t kEntrySize = 52, kEntryFlags = 171, kEntryMtx = 172;

float F(uint32_t a) {
    return Memory::ReadFloat32(a);
}

void F(uint32_t a, float v) {
    Memory::WriteFloat32(a, v);
}

float Step() {
    return float(GameStep::Scale());
}

void CopyMtx(uint32_t src, uint32_t dst) {
    for (uint32_t i = 0; i < 48; i += 4)
        Memory::Write32(dst + i, Memory::Read32(src + i));
}

uint32_t GetEffectMtx([[maybe_unused]] CpuContext* ctx, uint32_t handle, uint32_t index, uint32_t out) {
    if (!out || index >= 8)
        return 0;

    CopyMtx(Memory::Read32(handle) + index * kEntrySize + kEntryMtx, out);
    return 1;
}

uint32_t SetEffectMtx([[maybe_unused]] CpuContext* ctx, uint32_t handle, uint32_t index, uint32_t mtx) {
    if (index >= 8)
        return 0;

    const uint32_t entry = Memory::Read32(handle) + index * kEntrySize;

    if (mtx) {
        CopyMtx(mtx, entry + kEntryMtx);
        Memory::Write8(entry + kEntryFlags, Memory::Read8(entry + kEntryFlags) & 0xFE);
    } else {
        for (uint32_t i = 0; i < 12; ++i)
            F(entry + kEntryMtx + 4 * i, i % 5 == 0 ? 1.0f : 0.0f);

        Memory::Write8(entry + kEntryFlags, Memory::Read8(entry + kEntryFlags) | 1);
    }

    return 1;
}

// A material's texture coordinate handle in `handle` (4 bytes), through the
// scratch word `scratch`; false when the model has no such material.
bool MaterialTexCoords(GuestCall& game, uint32_t resmat, uint32_t scratch, uint32_t handle) {
    if (!Memory::Read32(resmat))
        return false;

    Memory::Write32(scratch, game.Call<ResMatTexCoordGen>({resmat, 0}));
    game.Call<HandleCopy>({handle, scratch});
    return true;
}

// 0x800CEF20's step: seed * 176 mod 65363 (a zero seed counts as 1).
uint32_t NextRand(uint32_t seed) {
    return (seed ? seed : 1) * 176 % 65363;
}

// 0x800CEF20 for RNG 0.
uint32_t Rand() {
    const uint32_t seed = NextRand(Memory::Read16(kRandSeed));
    Memory::Write16(kRandSeed, uint16_t(seed));
    return seed;
}

void SeaSurfacePort(CpuContext* ctx) {
    GuestFrame frame(ctx, 192);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const uint32_t model = Memory::Read32(self + 316);
    const uint32_t mat8 = frame[80], mat9 = frame[28], mtx = frame[136];
    game.Call<GetResMat>({mat8, model, 8});
    game.Call<GetResMat>({mat9, model, 9});

    if (Memory::Read32(mat8) && Memory::Read32(mat9)) {
        MaterialTexCoords(game, mat8, frame[12], frame[20]);
        MaterialTexCoords(game, mat9, frame[8], frame[16]);
        GetEffectMtx(ctx, frame[20], 1, mtx);

        // The U offset swings with a sine of the tick counter, 128 index
        // units a tick: here of the step's game time (the counter and the
        // part of a tick past it), so it moves every step instead of holding
        // between ticks.
        const double ticks = double(Memory::Read32(kTickCounter)) + GameStep::Fraction();
        const double fidx = std::fmod(ticks * 128.0, 65536.0) * double(F(r2 - 19168));
        game.Call<SinFIdx>({}, {fidx});
        F(mtx + 12, F(r2 - 19780) * game.F1());
        float v = F(mtx + 28) + F(r2 - 18860) * Step();

        if (v > F(r2 - 19912))
            v -= F(r2 - 19912);

        F(mtx + 28, v);
        SetEffectMtx(ctx, frame[20], 1, mtx);
        SetEffectMtx(ctx, frame[16], 1, mtx);
    }

    const uint32_t mdl = self + 36, color = frame[24];
    game.Call<GetTevColor>({mdl, 6, 3, color});

    for (uint32_t i = 0; i < 3; ++i)
        Memory::Write8(color + i, Memory::Read8(self + 825 + i));

    game.Call<SetTevColor>({mdl, 6, 3, color});
    game.Call<GetTevColor>({mdl, 0, 0, color});
    const bool white = Memory::Read8(self + 481) && !(Memory::Read8(self + 485) == 13 && Memory::Read8(self + 486) == 2);

    for (uint32_t i = 0; i < 3; ++i)
        Memory::Write8(color + i, white ? 255 : 150);

    for (uint32_t reg = 0; reg < 4; ++reg)
        game.Call<SetTevColor>({mdl, reg, 0, color});
}

void Mat6ScrollPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 144);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3), mdl = self + 36, color = frame[24];
    uint32_t kind = Memory::Read8(self + 480);

    if (const uint32_t mapped = Memory::Read8(kKindMap + kind); mapped != 255)
        kind = mapped;

    if (kind == 6 && Memory::Read8(self + 481) == 1) {
        const uint32_t mat = frame[28], mtx = frame[80];
        game.Call<GetResMat>({mat, Memory::Read32(self + 316), 6});

        if (MaterialTexCoords(game, mat, frame[16], frame[20])) {
            GetEffectMtx(ctx, frame[20], 0, mtx);
            float u = F(mtx + 12) - F(r2 - 17444) * Step();

            if (u < F(r2 - 17416))
                u += F(r2 - 17432);

            F(mtx + 12, u);
            SetEffectMtx(ctx, frame[20], 0, mtx);
        }

        if (game.Call<GetTevKColor>({mdl, 6, 4, color}) == 1) {
            const float alpha = F(r2 - 16472) * F(self + 468);
            Memory::Write8(color + 3, uint8_t(int32_t(std::clamp(alpha, -2147483648.0f, 2147483520.0f))));
            Memory::Write32(frame[12], Memory::Read32(color));
            game.Call<SetTevKColor>({mdl, 6, 4, frame[12], 0});
        }
    } else if (game.Call<GetTevKColor>({mdl, 6, 4, color}) == 1) {
        Memory::Write8(color + 3, 0);
        Memory::Write32(frame[8], Memory::Read32(color));
        game.Call<SetTevKColor>({mdl, 6, 4, frame[8], 0});
    }
}

// An effect-45 part's last roll: its scale factor and +48.
struct Flicker {
    float scale;
    uint32_t random;
};

void ObjectListPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 288);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const uint32_t list = Memory::Read32(self + 56);
    const uint32_t ticks = GameStep::Ticks();
    static std::unordered_map<uint32_t, Flicker> rolls;

    if (rolls.size() > 256)
        rolls.clear();

    Memory::Write8(self + 5, uint8_t(Memory::Read8(self + 5) + ticks));
    Memory::Write16(list + 22, 0);
    const float wide = F(r2 - 30216), one = F(r2 - 30212), low = F(r2 - 30208), spread = F(r2 - 30204);
    const float scaleStep = F(r2 - 30196), scaleBase = F(r2 - 30200), speed = F(r2 - 30192);

    for (int32_t i = 0; int32_t(int16_t(i)) < int32_t(Memory::Read32(list)); ++i) {
        const uint32_t obj = Memory::Read32(list + 4 + 4 * uint32_t(i));

        if (game.Call<CreateModel>({obj, 67, 268}) == 0) {
            game.Call<EndPass>({self});
            return;
        }

        for (uint32_t k = 0; k < 12; k += 4)
            Memory::Write32(obj + 4 + k, Memory::Read32(self + 24 + k));

        if ((int16_t(Memory::Read16(list + 22)) & 1) == 0) {
            for (uint32_t k = 0; k < 12; k += 4)
                Memory::Write32(obj + 40 + k, Memory::Read32(self + 36 + k));
        } else {
            const uint32_t v = frame[16];
            F(v, low), F(v + 4, low), F(v + 8, one);
            game.Call<RotateZXY>({v, self + 36});
            for (uint32_t k = 0; k < 12; k += 4)
                F(v + k, F(v + k) * spread);
            game.Call<VecAngles>({v, obj + 40, obj + 44});
            Memory::Write32(obj + 48, 0);
        }

        const auto kept = rolls.find(obj);
        Flicker roll;

        if (ticks) {
            for (uint32_t tick = 0; tick < ticks; ++tick) {
                roll.scale = scaleBase + scaleStep * float(int32_t(Rand() % 20));
                roll.random = Rand();
            }

            rolls[obj] = roll;
        } else if (kept != rolls.end()) {
            roll = kept->second;
        } else {
            const uint32_t first = NextRand(Memory::Read16(kRandSeed));
            roll.scale = scaleBase + scaleStep * float(int32_t(first % 20));
            roll.random = NextRand(first);
            rolls[obj] = roll;
        }

        F(obj + 28, wide), F(obj + 32, wide), F(obj + 36, one);
        for (uint32_t k = 0; k < 12; k += 4)
            F(obj + 28 + k, F(obj + 28 + k) * roll.scale);
        Memory::Write32(obj + 48, roll.random);
        Memory::Write32(obj + 56, 1);
        Memory::Write8(obj + 53, 1);
        Memory::Write16(list + 16 + 2 * uint32_t(i), uint16_t(int16_t(-6 * i)));
        const uint32_t mat = frame[80], mtx = frame[32];
        game.Call<GetResMat>({mat, Memory::Read32(obj + 280), 0});

        if (MaterialTexCoords(game, mat, frame[8], frame[12])) {
            GetEffectMtx(ctx, frame[12], 0, mtx);
            float u = F(mtx + 12) - speed * Step();

            if (u < low)
                u += one;

            if (u >= one)
                u -= one;

            F(mtx + 12, u);
            SetEffectMtx(ctx, frame[12], 0, mtx);
        }

        Memory::Write16(list + 22, uint16_t(int16_t(Memory::Read16(list + 22)) + 1));
    }

    game.Call<FinishPass>({self});
}

void GetEffectMtxPort(CpuContext* ctx) {
    ctx->gpr[3] = GetEffectMtx(ctx, ctx->gpr[3], ctx->gpr[4], ctx->gpr[5]);
}

void SetEffectMtxPort(CpuContext* ctx) {
    ctx->gpr[3] = SetEffectMtx(ctx, ctx->gpr[3], ctx->gpr[4], ctx->gpr[5]);
}

} // namespace

extern "C" void func_8051D7C0(CpuContext* ctx);
extern "C" void MH3_TexCoordGetEffectMtx(CpuContext* ctx) {
    PortCheck::Call(ctx, "TexCoordGetEffectMtx", func_8051D7C0, GetEffectMtxPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x8051D7C0, MH3_TexCoordGetEffectMtx, "TexCoordGetEffectMtx");

extern "C" void func_8051D744(CpuContext* ctx);
extern "C" void MH3_TexCoordSetEffectMtx(CpuContext* ctx) {
    PortCheck::Call(ctx, "TexCoordSetEffectMtx", func_8051D744, SetEffectMtxPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x8051D744, MH3_TexCoordSetEffectMtx, "TexCoordSetEffectMtx");

extern "C" void func_8010D524(CpuContext* ctx);
extern "C" void MH3_SeaSurfaceUpdate(CpuContext* ctx) {
    PortCheck::Call(ctx, "SeaSurfaceUpdate", func_8010D524, SeaSurfacePort);
}
REGISTER_NATIVE_FUNCTION_AS(0x8010D524, MH3_SeaSurfaceUpdate, "SeaSurfaceUpdate");

extern "C" void func_8012C884(CpuContext* ctx);
extern "C" void MH3_Mat6ScrollUpdate(CpuContext* ctx) {
    PortCheck::Call(ctx, "Mat6ScrollUpdate", func_8012C884, Mat6ScrollPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x8012C884, MH3_Mat6ScrollUpdate, "Mat6ScrollUpdate");

extern "C" void func_8006D050(CpuContext* ctx);
extern "C" void MH3_ObjectListScrollUpdate(CpuContext* ctx) {
    PortCheck::Call(ctx, "ObjectListScrollUpdate", func_8006D050, ObjectListPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x8006D050, MH3_ObjectListScrollUpdate, "ObjectListScrollUpdate");

// The texture-SRT animation objects' (AnmObjTexSrtRes) SetFrame and
// UpdateFrame, called through: the build's generated dispatch profile names
// these natives, and a build without them stops at the first quest ("omits
// translated winner 0x8006d668").
extern "C" void func_805249A0(CpuContext* ctx);
extern "C" void MH3_AnmTexSrtSetFrame(CpuContext* ctx) {
    func_805249A0(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x805249A0, MH3_AnmTexSrtSetFrame, "AnmTexSrtSetFrame");

extern "C" void func_80524B24(CpuContext* ctx);
extern "C" void MH3_AnmTexSrtUpdateFrame(CpuContext* ctx) {
    func_80524B24(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x80524B24, MH3_AnmTexSrtUpdateFrame, "AnmTexSrtUpdateFrame");

// The texture-SRT animation objects' GetResult (vtable +0x38) and Bind,
// C reconstructions from the disassembly (g3d_anmtexsrt.cpp, the debug
// build's pointer and bounds assertions dropped):
//
//   0x80524CFC AnmObjTexSrtRes::GetResult(this, result, mat) -> result: +0x14
//              u16 per model material, the animation's material (0x4000 or
//              0x8000: none, the result's first word 0); +0x30 a cache of
//              0xE8-byte results, else the evaluation at the FrameCtrl's
//              frame (+0x18, rate +0x1C) by ResAnmTexSrt (+0x2C)
//              GetAnmResult (0x8051FD58). Here the FrameCtrl's rate and the
//              frame on its grid are the key hold (res_anm_keys.h) while it
//              evaluates.
//   AnmObjTexSrtOverride::GetResult and AnmObjTexSrtRes::Bind remain unported on RMHJ08.
namespace {

enum : uint32_t {
    TexSrtAnmResult = 0x8051FD58,       // ResAnmTexSrt::GetAnmResult(res, result, index, f1 frame)
};

enum : uint32_t {
    kAnmMatFlags = 0x14, // u16 per model material
    kFrameCtrl = 0x18,   // FrameCtrl: +0 frame, +4 rate, +8 start
    kRate = 0x1C,
    kStart = 0x8,
    kRes = 0x2C,
    kResultCache = 0x30,
    kResultBytes = 0xE8,
};

uint32_t TexSrtResult(GuestCall& game, uint32_t self, uint32_t result, uint32_t material) {
    const uint32_t index = Memory::Read16(Memory::Read32(self + kAnmMatFlags) + material * 2);

    if (index & 0xC000) {
        Memory::Write32(result, 0);
        return result;
    }

    if (const uint32_t cache = Memory::Read32(self + kResultCache))
        return cache + index * kResultBytes;

    const double frame = Memory::ReadFloat32(self + kFrameCtrl);
    const float rate = Memory::ReadFloat32(self + kRate);
    float atTick = float(frame);

    // At a variable step: the frame on the grid of the frames a tick from the
    // start, the ones the fixed step, starting there on a tick, shows (0, 2,
    // 4, ... at the maps' rate 2).
    if (GameStep::Variable()) {
        const float start = Memory::ReadFloat32(self + kFrameCtrl + kStart), grid = rate > 1.0f ? rate : 1.0f;
        atTick = start + std::floor((float(frame) - start) / grid) * grid;
    }

    const ResAnmKeys::HoldSpan hold(rate, atTick);
    game.Call<TexSrtAnmResult>({self + kRes, result, index}, {frame});
    return result;
}

// The frame restores r3: the result goes in after it.
void TexSrtResultPort(CpuContext* ctx) {
    uint32_t result;
    {
        GuestFrame frame(ctx, 0x20);
        GuestCall game{ctx};
        result = TexSrtResult(game, frame.Arg(3), frame.Arg(4), frame.Arg(5));
    }

    ctx->gpr[3] = result;
}

} // namespace

extern "C" void func_80524CFC(CpuContext* ctx);
extern "C" void MH3_AnmTexSrtResGetResult(CpuContext* ctx) {
    PortCheck::Call(ctx, "AnmTexSrtResGetResult", func_80524CFC, TexSrtResultPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x80524CFC, MH3_AnmTexSrtResGetResult, "AnmTexSrtResGetResult");

// Disabled unported PAL address for RMHJ08: REGISTER_NATIVE_FUNCTION_AS(0x8006C6CC, MH3_AnmTexSrtOverrideGetResult, "AnmTexSrtOverrideGetResult");
// Disabled unported PAL address for RMHJ08: REGISTER_NATIVE_FUNCTION_AS(0x8006D7AC, MH3_AnmTexSrtResBind, "AnmTexSrtResBind");
