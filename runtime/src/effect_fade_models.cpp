// Two effect objects that fade five materials of their models in by a key
// table, RMHP08. C reconstructions from the disassembly, run by 0x80118728
// by their kind (the task +3):
//
//   0x80118770 EffectFadeRise(object): over its owner's joint 3, growing then
//              shrinking; ends with the owner or when all five are done
//   0x801194B4 EffectFadeStay(object): at a place that moves in at first
//
// The data (+0x38): s32 models +0, the models +4, their draw objects +0xC,
// five entries of 12 bytes from +0x14 (u8 state, the colour's word at +2, its
// alpha +5, s32 count +8), VEC3 scale +0x50, offset +0x5C. The originals move
// their counts and sizes by 1 and a fixed amount a step; here by the step's
// whole ticks and share.
#include "abi_bridge.h"
#include "game_step.h"
#include "guest_call.h"
#include "guest_fields.h"
#include "port_check.h"

#include <algorithm>
#include <cstdint>

namespace {

using namespace GuestFields;

enum : uint32_t {
    kKind = 0x02,  // u8
    kPhase = 0x06, // u8
    kTimer = 0x10, // s32
    kAt = 0x18,    // VEC3
    kOwner = 0x30,
    kData = 0x38,
    kEntries = 0x14,
    kEntryBytes = 0xC,
    kCount = 5,
    kScale = 0x50,
    kOffset = 0x5C,
};

enum : uint32_t {
    Identity = 0x8042F3A8,
    RotateX = 0x8028ABAC,
    RotateY = 0x8028AB14,
    RotateZ = 0x8028AC44,
    MulVec = 0x8028C3A8,
    DrawRequest = 0x80037B74,
};

constexpr uint32_t kKeys = 0x806808C0, kLengths = 0x80680790; // per kind

// The entries' fades and the materials' konst colour 3. `read`: the original
// reads the colour first.
void Fade(GuestCall& game, GuestFrame& frame, uint32_t object, uint32_t access, bool read) {
    const uint32_t data = R32(object + kData), kind = R8(object + kKind);
    const int32_t ticks = int32_t(GameStep::Ticks());

    for (uint32_t i = 0; i < kCount; ++i) {
        const uint32_t e = data + kEntries + i * kEntryBytes, state = R8(e);
        const int32_t count = int32_t(R32(e + 8));

        if (state == 0) {
            if (count < 1) {
                W32(e + 8, 0);
                W8(e, 1);
            } else
                W32(e + 8, uint32_t(std::max(count - ticks, 0)));
        } else if (state == 1) {
            const int32_t now = count + ticks;
            W32(e + 8, uint32_t(now));
            W8(e + 5, game.Call<0x80038F30>({R32(kKeys + kind * 4), uint32_t(now)}));

            if (int32_t(R32(kLengths + kind * 4)) <= now)
                W8(e, 2);
        }

        game.Call<0x805390D4>({access, R32(R32(data + 4) + 0x118), i});

        if (R32(access) != 0) {
            const uint32_t word = frame[12], colours = frame[16], colour = frame[8];
            W32(word, game.Call<0x8053908C>({access, 0}));
            W32(colours, R32(word));

            if (read)
                game.Call<0x8051DEF4>({colours, 3, frame[20]});

            W32(colour, R32(e + 2));
            game.Call<0x8051DFAC>({colours, 3, colour});
            game.Call<0x8051D264>({colours, 0});
        }
    }
}

bool AllDone(uint32_t data) {
    for (uint32_t i = 0; i < kCount; ++i)
        if (R8(data + kEntries + i * kEntryBytes) != 2)
            return false;

    return true;
}

// The models at the object's matrix (`matrix`, its translation already in),
// spun by the step's share of 0x100.
void DrawModels(GuestCall& game, uint32_t object, uint32_t at, uint32_t matrix) {
    const uint32_t data = R32(object + kData), first = R32(data + 4);
    const uint32_t spin = R32(first + 0x30) + uint32_t(GameStep::Share(0x100));
    W32(first + 0x30, spin);
    W32(object + 0x2C, spin);
    const uint32_t models = R32(data);

    for (uint32_t i = 0; i < models; ++i) {
        const uint32_t model = R32(data + 4 + i * 4);
        Copy3(model + 4, at);
        Copy3(model + 0x1C, data + kScale);
        game.Call<0x802A259C>({model, matrix, 0});
        game.Virtual(R32(data + 0xC + i * 4), 0x24);
        game.Call<DrawRequest>({object, data + 4 + i * 4, 2, 1, 0});
    }
}

void Rise(CpuContext* ctx) {
    GuestFrame frame(ctx, 0xD0);
    GuestCall game{ctx};
    const uint32_t object = frame.Arg(3), data = R32(object + kData), owner = R32(object + kOwner);
    const uint32_t access = frame[48], at = frame[36], matrix = frame[104];

    if (game.Call<0x80037A84>({object, 0}) == 0 || AllDone(data)) {
        W8(object + 1, 0);
        W8(object + 5, R8(object + 5) + 1);
        return;
    }

    Fade(game, frame, object, access, true);
    const int32_t before = int32_t(R32(object + kTimer)), timer = before - int32_t(GameStep::Ticks());
    W32(object + kTimer, uint32_t(timer));
    const float scale = float(GameStep::Scale());

    if (R8(object + kPhase) == 0) {
        WF(data + kScale, RF(data + kScale) + 0.12f * scale);
        WF(data + kScale + 4, RF(data + kScale + 4) + 0.12f * scale);

        if (timer < 0) {
            W8(object + kPhase, 1);
            W32(object + kTimer, 0xE);
        }
    } else if (R8(object + kPhase) == 1) {
        WF(data + kScale, RF(data + kScale) - 0.01f * scale);
        WF(data + kScale + 4, RF(data + kScale + 4) - 0.01f * scale);

        if (timer < 0)
            W8(object + kPhase, 2);

        const uint32_t kind = R8(object + kKind);

        if (kind != 5 && kind != 0xB && before > 10 && timer <= 10 && RF(owner + 0x40) < RF(owner + 100) - 50.0f)
            game.Call<0x80045EF0>({owner, 4});
    }

    W8(object + 0x44, R8(owner + 0x16));
    game.Call<0x802A28C4>({R32(owner + 0x13C) + 4, 3, at});
    Copy3(object + kAt, at);
    game.Call<Identity>({matrix});
    game.Call<RotateX>({R32(object + 0x28), matrix});
    game.Call<RotateY>({R32(object + 0x24), matrix});
    game.Call<RotateX>({R16(data + 0x70), matrix});
    game.Call<RotateZ>({R32(object + 0x2C), matrix});
    Copy3(at, data + kOffset);
    game.Call<MulVec>({at, matrix});
    for (int i = 0; i < 3; ++i)
        Memory::WriteFloat32(object + kAt + i * 4, Memory::ReadFloat32(object + kAt + i * 4) + Memory::ReadFloat32(at + i * 4));

    for (uint32_t k = 0; k < 3; ++k)
        W32(matrix + 0xC + k * 0x10, R32(object + kAt + k * 4));

    DrawModels(game, object, object + kAt, matrix);
}

void Stay(CpuContext* ctx) {
    GuestFrame frame(ctx, 0xE0);
    GuestCall game{ctx};
    const uint32_t object = frame.Arg(3), data = R32(object + kData);
    const uint32_t access = frame[56], offset = frame[44], sum = frame[20], placed = frame[32], matrix = frame[112];
    Fade(game, frame, object, access, false);

    if (AllDone(data)) {
        W8(object + 1, 0);
        W8(object + 5, R8(object + 5) + 1);
        return;
    }

    const uint32_t kind = R8(object + kKind);
    const int32_t timer = int32_t(R32(object + kTimer)), length = int32_t(R32(kLengths + kind * 4));
    float z = RF(data + 0x78) * (float(timer) / float(length));

    if (kind == 7)
        z -= 5000.0f;
    else if (kind == 10)
        z -= 500.0f;

    WF(offset, 0.0f);
    WF(offset + 4, 0.0f);
    WF(offset + 8, z);
    for (int i = 0; i < 3; ++i)
        Memory::WriteFloat32(offset + i * 4, -Memory::ReadFloat32(offset + i * 4));
    W32(object + kTimer, uint32_t(std::min(timer + int32_t(GameStep::Ticks()), 4)));
    game.Call<Identity>({matrix});
    game.Call<0x8028A954>({R32(object + 0x28), matrix}); // RotateX on Stay? -> JPN 0x8028A954
    game.Call<RotateY>({R32(object + 0x24), matrix});
    game.Call<RotateZ>({R32(object + 0x2C), matrix});
    game.Call<MulVec>({offset, matrix});
    for (int i = 0; i < 3; ++i)
        Memory::WriteFloat32(sum + i * 4, Memory::ReadFloat32(object + kAt + i * 4) + Memory::ReadFloat32(offset + i * 4));
    Copy3(placed, sum);

    for (uint32_t k = 0; k < 3; ++k)
        W32(matrix + 0xC + k * 0x10, R32(placed + k * 4));

    DrawModels(game, object, placed, matrix);
}

} // namespace

extern "C" void func_80057E44(CpuContext* ctx);
extern "C" void MH3_EffectFadeRise(CpuContext* ctx) {
    PortCheck::Call(ctx, "EffectFadeRise", func_80057E44, Rise);
}
REGISTER_NATIVE_FUNCTION_AS(0x80057E44, MH3_EffectFadeRise, "EffectFadeRise");

extern "C" void func_80058C5C(CpuContext* ctx);
extern "C" void MH3_EffectFadeStay(CpuContext* ctx) {
    PortCheck::Call(ctx, "EffectFadeStay", func_80058C5C, Stay);
}
REGISTER_NATIVE_FUNCTION_AS(0x80058C5C, MH3_EffectFadeStay, "EffectFadeStay");
