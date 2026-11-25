// The large monsters' step, RMHP08. C reconstructions from the disassembly:
//
//   0x80143C78 MonstersMove(): the first of the field's two monster passes:
//              each large monster's MonsterStep (not in an event demo, unless
//              +0x46B holds a slot)
//   0x80143DDC MonstersActive(): after the hunters and the field's objects
//              moved, each monster's EnemyActiveStep and each hunter's
//              0x80144BC0
//   0x80138BC4 EnemyActiveStep(em): what only a monster in play does (u8
//              +0x1DE, set by MonsterStep): its handlers 6 and 8, its colours
//   0x80137434 MonsterColours(em): every material's channel 4 alpha and alpha
//              test from the resource, scaled by the fade (+0x1D4). Below 1
//              the material blends, at 1 its pixel copy is the resource's
//              again (0x800E3140 with 0x80), which undoes what a species'
//              handler 8 sets later in the step (enemy_parts.cpp)
//   0x80137980 MonsterStep(em): the first call sets the monster up, later ones
//              run EnemyUpdate; the spawn state (+0x17) then picks what
//              follows, 0 marking it active (+0x1DE)
//   0x801382A0 EnemyUpdate(em): the step: sounds, a crossing between areas,
//              senses (not in a demo), damage (enemy_parts.cpp), handlers 2
//              and 3, the action, the height offset, MonsterColours and the
//              ground
//   0x80136E9C EnemyLight(em, at once): channel 0 of every material from the
//              map's light for the monster (a tint +0x22F, three further under
//              water) over the base, brightened by the zone's colour at its
//              position (0x802C221C), moving to that by 5 a step (+0x75C holds
//              the colour)
//
// +0x764, +0x768 and the sound timers +0x898 count on whole ticks (tick_sites.inc
// had the first), and the light moves by the step's share of 5. The height
// offset's ease and decay are the sites 0x8013896C and 0x801389AC.
#include "abi_bridge.h"
#include "cheats.h"
#include "game_step.h"
#include "guest_call.h"
#include "guest_fields.h"
#include "memory.h"
#include "port_check.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

extern "C" double MH3_PerStep(double amount);
extern "C" double MH3_Decay(double factor);

namespace {

using namespace GuestFields;

enum : uint32_t {
    kChar = 0x24,       // MHchar
    kPosition = 0x188,  // VEC3
    kPrevious = 0x194,  // VEC3
    kHeight = 0x1AC,    // f32: the height offset
    kFade = 0x1D4,      // f32
    kHalted = 0x46A,    // u8: no senses or damage
    kPlace = 0x1E2,     // u8
    kActive = 0x1DE,    // u8
    kFlags = 0x1C8,     // u32
    kMoved = 0x8C0,     // u8
    kOnce = 0x762,      // u8
    kCount = 0x764,     // s16
    kHeld = 0x766,      // u8
    kHolds = 0x767,     // u8
    kHoldCount = 0x768, // s16
};

enum : uint32_t {
    Handler = 0x8015DD90,
    MaterialCount = 0x802A4AAC,
    MaterialRefresh = 0x802A5238,
    SetWorld = 0x802A2690,
    DemoState4 = 0x8038CD6C,
    NowArea = 0x8037D338,
    MapKind = 0x8037DEF0,
    Rand = 0x802BC314,
    Vec3Set = 0x8028A7F4,
};

// The original's x - 1: by the step's whole ticks down to 0, then by 1.
int32_t CountDown(uint32_t a) {
    const int32_t x = S16(a);
    const int32_t value = x > 0 ? std::max(x - int32_t(GameStep::Ticks()), 0) : x - 1;
    W16(a, uint32_t(value));
    return value;
}

void Hold(uint32_t em) {
    if (R8(em + kHeld) != 0)
        return;

    if (R8(em + kHolds) == 1) {
        W8(em + kHeld, 1);
        Copy3(em + 0x778, em + 0x76C);
    }

    if (S16(em + kHoldCount) > 0)
        CountDown(em + kHoldCount);
}

void Countdown764(GuestCall& game, uint32_t em) {
    if (S16(em + kCount) <= -1)
        return;

    if (game.Call<0x80168D14>({em, 0}) == 1) {
        W8(em + kOnce, 1);
        return;
    }

    const bool first = R8(em + kOnce) == 0;

    if (CountDown(em + kCount) >= 1)
        return;

    if (first) {
        W8(em + kOnce, 1);
        W16(em + kCount, 2);
    } else
        game.Call<0x8016E98C>({em});
}

void ActiveStep(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    const uint32_t em = frame.Arg(3);

    if (R8(em + kActive) == 0)
        return;

    const bool moved = R8(em + kMoved) != 0;

    if (moved) {
        for (uint32_t k = 0; k < 12; k += 4)
            WF(em + 0x188 + k, RF(em + 0x188 + k) + RF(em + 0x8B4 + k));
        game.Call<0x80173FA4>({em, 1});
        game.Call<0x80162ED0>({em});
        game.Call<0x8016A034>({em});
    }

    int32_t spot = -1;

    if (R8(em + kPlace) == 1 && R8(em + 0x1E3) == 1) {
        spot = int32_t(game.Call<0x80173E18>({em, 0}));

        if (spot != -1)
            game.Call<0x80173F58>({em, 0x400, uint32_t(spot)});
    }

    if (moved || spot != -1) {
        game.Call<0x80174E68>({em});
        game.Call<SetWorld>({em + kChar});
    }

    game.Call<0x80162F30>({em});
    Hold(em);
    game.Call<0x801751AC>({em});
    game.Call<0x8017211C>({em});
    Countdown764(game, em);

    if (const uint32_t handler = game.Call<Handler>({em, 6}))
        game(handler, {em});

    game.Call<0x8016DB2C>({em});
    const uint32_t flags = MemoryInline::FlatRead32(em + kFlags);

    if (!(flags & 1))
        game.Call<0x80172D88>({em, 0});
    else if (flags & 0x40000) {
        const bool dead = game.Call<0x80165DF8>({em}) == 1;

        for (uint32_t i = 0; i < game.Call<MaterialCount>({em + kChar}); ++i) {
            if (dead)
                game.Call<MaterialRefresh>({em + kChar, i, 0x10});
            else
                game.Call<0x802A5080>({em + kChar, i, 2});
        }
    }

    if (const uint32_t handler = game.Call<Handler>({em, 8}))
        game(handler, {em});

    game.Call<0x80169CD8>({em});

    if (R8(em + kPlace) == 4)
        W16(em + 0x79C, 1);
}

void Colours(CpuContext* ctx) {
    GuestFrame frame(ctx, 0xB0);
    GuestCall game{ctx};
    const uint32_t em = frame.Arg(3), ch = em + kChar, pix = frame[56], colour = frame[24], copy = frame[12];
    W32(pix, 0);
    const uint32_t count = game.Call<MaterialCount>({ch});

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t mat = game.Call<0x8051BEE8>({ch + 0x114, i});
        const int32_t ofs = int32_t(R32(mat + 0x3C));
        W32(pix, ofs ? mat + uint32_t(ofs) : 0);
        const float fade = MemoryInline::FlatReadFloat32(em + kFade);

        if (game.Call<0x802A4D18>({ch, i, 4, colour}) == 1) {
            W8(colour + 3, uint32_t(int32_t(255.0f * fade)));
            MemoryInline::FlatWrite32(copy, MemoryInline::FlatRead32(colour));
            game.Call<0x802A4AB4>({ch, i, 4, copy, 0});

            if (fade < 1.0f) {
                const uint32_t blend = frame[40], source = frame[36];
                game.Call<0x8051DC50>({pix, blend, source, frame[32], frame[28]});

                if (MemoryInline::FlatRead32(source) != 4 || MemoryInline::FlatRead32(blend) == 0)
                    game.Call<0x802A518C>({ch, i, 1, 4, 5, 0});
            } else
                game.Call<MaterialRefresh>({ch, i, 0x80});
        }

        const uint32_t compare0 = frame[52], reference0 = frame[9], operation = frame[44], compare1 = frame[48], reference1 = frame[8];
        game.Call<0x8051DB9C>({pix, compare0, reference0, operation, compare1, reference1});
        const uint32_t ref0 = R8(reference0), ref1 = R8(reference1);

        if (ref0 != 0 || ref1 != 0xFF)
            game.Call<0x802A50F0>({ch, i, MemoryInline::FlatRead32(compare0), uint32_t(int32_t(float(ref0) * fade)) & 0xFF, MemoryInline::FlatRead32(operation),
                                   MemoryInline::FlatRead32(compare1), uint32_t(int32_t(float(ref1) * fade)) & 0xFF});
    }
}

// An area's crossing point: the map's door, in that area's coordinates.
void DoorPoint(GuestCall& game, uint32_t em, uint32_t area, uint32_t out, uint32_t door) {
    const bool far = (game.Call<MapKind>({R8(em + 0x1E0)}) & 0xFF) == 5 && area == 3;
    game.Call<Vec3Set>({door}, {far ? 7000.0 : 0.0, 0.0, far ? 5000.0 : 0.0});
    game.Call<0x8037D888>({out, door, area});
}

// A monster crossing to the area +0x9F7 (u8 +0x1F9 set): by the route's
// distance (kind 8), or over the middle of the two door points.
void CrossAreas(GuestCall& game, GuestFrame& frame, uint32_t em) {
    if (R8(em + 0x1F9) != 1 || R8(em + 0x1E1) == R8(em + 0x9F7))
        return;

    if (R8(em + 0x380) == 8) {
        if (game.Call<0x80165F68>({em}) != 1 || (R8(em + 0x99A) & 1) || R8(em + 0x99B) != 1 || R8(em + 0x382) != 0)
            return;

        game.Call<0x8028BC04>({em + 0x188, em + 0x36C});

        if (game.F1() <= RF(R32(R32(em + 0x9A4) + 4) + 4)) {
            if (R8(em + 0x95C) == 2 || game.Call<0x8017E668>({em}) != 0) {
                const uint32_t route = R32(em + 0x9A4);
                const uint32_t flag = R8(route);

                if (flag == 0xFE || flag == 0xFC)
                    W8(em + 0x1F9, 0);
                else {
                    game.Call<0x8015DABC>({em, R16(route + 8), em + 0x188});
                    W32(em + 0x1C0, R16(R32(em + 0x9A4) + 0xA));
                    game.Call<0x80163124>({em, 0});
                }

                game.Call<0x8017E668>({em});
                game.Call<0x8016E9E0>({em});
            }
        }

        return;
    }

    if (R8(em + 0x1FA) != 1 || R8(em + 0x1FB) != 1)
        return;

    const uint32_t door = frame[140], here = frame[104], there = frame[92], at = frame[80], out = frame[20];
    const uint32_t from = R8(em + 0x1E1), to = R8(em + 0x9F7);
    DoorPoint(game, em, from, here, door);
    DoorPoint(game, em, to, there, door);
    game.Call<0x8037D888>({at, em + kPosition, from});
    float dot = 0.0f;

    for (uint32_t k = 0; k < 12; k += 4) {
        const float middle = 0.5f * (RF(here + k) + RF(there + k));
        dot += (RF(there + k) - middle) * (RF(at + k) - middle);
    }

    if (dot >= 0.0f) {
        game.Call<0x8037D94C>({out, at, to});
        Copy3(em + kPosition, out);
        game.Call<0x8037DC60>({to});
        WF(em + kPosition + 4, game.F1());
        game.Call<0x80163124>({em, 0});
    }
}

// The three sound handles (+0x88C) and their timers (s16 +0x898): a handle is
// dropped when the sound system has none (0x80208334) or its time is out.
void Sounds(GuestCall& game, uint32_t em, bool counts) {
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t timer = em + 0x898 + i * 2, handle = em + 0x88C + i * 4;

        if (counts && S16(timer) > 0)
            W16(timer, uint32_t(std::max(S16(timer) - int32_t(GameStep::Ticks()), 0)));

        const uint32_t sound = R32(handle);

        if (sound == 0xFFFFFFFFu)
            continue;

        const bool alive = game.Call<0x80208334>({sound}) != 0;

        if (alive && S16(timer) >= 1)
            continue;

        if (int32_t(game.Call<0x80207E9C>({sound})) > 0)
            W8(em + 0x89E, R8(em + 0x89E) + 1);

        if (alive)
            game.Call<0x80207DD4>({sound});

        W32(handle, 0xFFFFFFFFu);
    }
}

// The height offset (+0x1AC): large it decays, small it moves to 0 by 10.
void EaseHeight(uint32_t em) {
    const float height = RF(em + kHeight);

    if (std::fabs(height) > 100.0f) {
        WF(em + kHeight, height * float(MH3_Decay(double(0.9f))));
        return;
    }

    const float step = float(MH3_PerStep(10.0));

    if (height > step)
        WF(em + kHeight, height - step);
    else if (height >= -10.0f)
        WF(em + kHeight, 0.0f);
    else
        WF(em + kHeight, height + step);
}

// What runs before the action. False when the monster left the step.
bool BeforeAction(GuestCall& game, GuestFrame& frame, uint32_t em) {
    const bool halted = R8(em + kHalted) != 0;
    const bool counts = game.Call<DemoState4>() == 0 && !halted;
    Sounds(game, em, counts);
    game.Call<0x8015CC10>({em});
    CrossAreas(game, frame, em);

    if (R8(em + 0x1E1) != (game.Call<NowArea>() & 0xFF)) {
        const uint32_t flags = R32(em + kFlags);

        if (!(flags & 8)) {
            W8(em + 0x17, 5);
            return false;
        }

        if (!(flags & 1)) {
            for (const uint32_t field : {0x810u, 0x94Au, 0x94Cu, 0x812u})
                W16(em + field, 0);

            game.Call<0x8016CABC>({em});
        }
    }

    return true;
}

void ClearStepState(GuestCall& game, uint32_t em) {
    game.Call<0x801600EC>({em});

    for (uint32_t k = 0; k < 12; k += 4)
        WF(em + 0x1A0 + k, 0.0f);

    for (const uint32_t field : {0x1DFu, 0x79Au, 0x784u, 0x30Cu, 0x30Du, 0x30Eu, 0x30Fu})
        W8(em + field, 0);

    Copy3(em + kPrevious, em + kPosition);
}

void Senses(GuestCall& game, uint32_t em) {
    // Monster can't see you (US 0x801386BC, EU 0x80138720) takes the branch
    // an event demo takes.
    if (game.Call<DemoState4>() != 0 || R8(em + kHalted) != 0 || Cheats::On(Cheats::Id::MonsterCantSeeYou))
        return;

    game.Call<0x80174F44>({em});
    game.Call<0x80169E08>({em});
    game.Call<0x8016B4C8>({em});
    game.Call<0x801644A4>({em});
    game.Call<0x80164B74>({em});
    game.Call<0x80165398>({em});
    game.Call<0x80165724>({em});
}

// Damage, then what asked for an action.
void Decide(GuestCall& game, uint32_t em) {
    uint32_t reacted = 0;

    if (game.Call<DemoState4>() == 0 && R8(em + kHalted) == 0) {
        game.Call<0x80156B20>({em});
        reacted = game.Call<0x80156BB4>({em});
    }

    if (const uint32_t handler = game.Call<Handler>({em, 2}))
        game(handler, {em});

    if (reacted == 1) {
        game.Call<0x8016E9EC>({em});
        return;
    }

    if (game.Call<0x8016E9F8>({em}) == 1 && R8(em + kHalted) == 0)
        game.Call<0x80176FFC>({em});
    else if (!(R32(em + kFlags) & 8))
        game.Call<0x80160A28>({em, 0});
}

// The action. False when it left the step.
bool RunAction(GuestCall& game, uint32_t em) {
    const uint32_t mode = R8(em + 0x1E5), flags = R32(em + kFlags);

    if (mode - 8 < 2)
        game.Call<0x8016C4C0>({em});
    else if (mode == 0xB) {
        if (!(flags & 1))
            game.Call<0x8016C468>({em});

        if (!(flags & 0x10000))
            game.Call<0x8016C4A8>({em});
    }

    const uint32_t handler = game.Call<Handler>({em, 3});

    if (handler == 0)
        return true;

    if (game.Call<0x80173BD0>({em, handler}) == 0)
        return false;

    if (game.Call<0x8016E9F8>({em}) == 1 && R8(em + kHalted) == 0 && game.Call<0x80176FFC>({em}) == 1)
        game.Call<0x80160078>({em});

    if (game.Call<0x80160090>({em}) == 1) {
        game.Call<0x80160084>({em});

        if (game.Call<0x80173BD0>({em, handler}) == 0)
            return false;
    }

    return true;
}

void Show(GuestCall& game, uint32_t em) {
    const bool shown = game.Call<0x80165FD0>({em, 12, 255}) == 0 &&
                       ((game.Call<0x80165E40>({em}) == 1 && R8(em + 1) != 0) || game.Call<0x801719C0>({em, 0}) == 1);
    W8(em + 0x58, shown);
    game.Call<0x80173300>({em});
    const float fade = RF(em + kFade);

    if (fade == 0.0f)
        W8(em + 0x58, 0);
    else
        W8(em + 0x59, fade != 1.0f);

    const uint32_t flags = R32(em + kFlags);
    game.Call<0x802A24A8>({em + kChar, flags & 0x401 ? 0u : 0x180u});
    Copy3(em + kPosition, em + 0x28);
}

// Slides along the ground's normal (+0x230) after a move on land or in water.
void Slide(GuestCall& game, uint32_t em, uint32_t placeBefore) {
    const uint32_t place = R8(em + kPlace);
    double rate = 0.0;

    if (place == 0 && placeBefore != 1)
        rate = double(0.3f);
    else if (place == 2) {
        game.Call<0x8016A528>({em});

        if (RF(em + 0x198) < 10.0f + (RF(em + 0x20C) + game.F1()))
            rate = double(0.6f);
    }

    if (rate != 0.0)
        game.Call<0x8028D3EC>({em + kPrevious, em + kPosition, em + 0x230, em + kPosition}, {rate});

    for (uint32_t k = 0; k < 12; k += 4)
        WF(em + 0x470 + k, RF(em + kPosition + k) - RF(em + kPrevious + k));
}

void Update(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x110);
    GuestCall game{ctx};
    const uint32_t em = frame.Arg(3);

    if (!BeforeAction(game, frame, em))
        return;

    ClearStepState(game, em);
    const uint32_t placeBefore = R8(em + kPlace);
    game.Call<0x8016EA6C>({em});

    if (R8(em + kHolds) == 1)
        Copy3(em + 0x778, em + 0x76C);

    W8(em + kHolds, 1);
    W8(em + 0x740, 0);
    W8(em + 0x43F, 0);
    game.Call<0x80161C6C>({em});
    Senses(game, em);
    game.Call<0x8016C83C>({em});
    Decide(game, em);

    if (!RunAction(game, em))
        return;

    if (R8(em + 0x784) != 0) {
        game.Call<0x8016C528>({em});
        game.Call<0x80165D0C>({em});
        game.Call<0x8016C450>({em});
    }

    game.Call<0x80174E68>({em});

    if (R8(em + kPlace) == 3)
        game.Call<0x8016A6CC>({em});

    if (R8(em + kPlace) != 4 && R8(em + 0x1DF) == 0)
        EaseHeight(em);

    game.Call<0x801717B8>({em});
    WF(em + 0x1A4, RF(em + 0x1A4) + RF(em + kHeight) * game.F1());
    game.Call<0x802A275C>({em + kChar, em + 0x1A0});
    Show(game, em);
    Slide(game, em, placeBefore);
    game.Call<0x80173FA4>({em, 0});
    game.Call<0x80162ED0>({em});
    game.Call<0x8016A034>({em});
    game.Call<0x80173D00>({em});
    game.Call<0x80174E68>({em});
    game.Call<SetWorld>({em + kChar});

    for (uint32_t i = 0; i < 2; ++i)
        game.Call<0x80174ED8>({em, i});
}

// Spawn states 1, 2, 4: the monster's level and size drawn for its id.
void DrawLevel(GuestCall& game, uint32_t em) {
    const uint32_t id = R16(em + 0x1A);
    W8(em + 0x7C8, game.Call<0x8017FF74>({id, game.Call<Rand>({1}) & 0xFFFF}));
    game.Call<0x80180104>({id, game.Call<Rand>({1}) & 0xFFFF});
    WF(em + 0x1D0, game.F1());
}

void Respawn(GuestCall& game, uint32_t em) {
    DrawLevel(game, em);
    game.Call<0x802A8330>({em, 4, 0});
    game.Call<0x8015CE80>({em, 1});
}

void Place(GuestCall& game, uint32_t em) {
    game.Call<0x8016C468>({em});
    game.Call<0x8016C4A8>({em});
    game.Call<SetWorld>({em + kChar});
}

// In play the monster goes active. True when it was set up anew.
bool AfterUpdate(GuestCall& game, uint32_t em) {
    switch (R8(em + 0x17)) {
    case 0:
        if (R8(em + 0x11) == 2)
            game.Call<0x80167CA8>({em, 1});
        else
            W8(em + kActive, 1);

        return false;
    case 1:
        W8(em + 0x16, R8(em + 0x16) + 1);
        W8(em + 0x12, 0);

        if (R32(em + kFlags) & 8)
            Respawn(game, em);
        else {
            game.Call<0x80182C48>({em, 2});
            game.Call<0x8015CE80>({em, 1});
        }

        return true;
    case 2:
        if (!(R32(em + kFlags) & 8))
            return false;

        W8(em + 0x12, R8(em + 0x12) + 1);
        Respawn(game, em);
        return true;
    case 3:
        game.Call<0x80167CA8>({em, 0});
        return false;
    case 4:
        if (R32(em + kFlags) & 8)
            DrawLevel(game, em);

        game.Call<0x802A8330>({em, 4, 1});
        game.Call<0x80167CA8>({em, 0});
        return false;
    case 5:
        game.Call<0x80167CA8>({em, 2});
        return false;
    default:
        return false;
    }
}

void Step(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    const uint32_t em = frame.Arg(3);
    W8(em + kActive, 0);

    switch (R8(em + 4)) {
    case 0:
        W8(em + 4, 1);
        game.Call<0x80173BC8>({em});
        Place(game, em);
        break;
    case 1:
        if (R8(em + 0xAEE) != 0) {
            const uint32_t flags = R32(em + kFlags);

            if ((flags & 8) && (flags & 1))
                game.Call<0x802A8330>({em, 3, 0});

            W8(em + 0xAEE, 0);
        }

        game.Call<0x8017413C>({em});

        if (AfterUpdate(game, em))
            Place(game, em);

        if (R8(em + 0x58) != 0) {
            const uint32_t obj = R32(frame.Arg(13) - 0x43B4);
            game(R32(R32(obj) + 0x34), {obj, R32(obj + 0xE4), R32(em + 0x13C)});
        }

        break;
    case 2:
        game.Call<0x80174E1C>({em});
        break;
    case 3:
        game.Call<0x80174E2C>({em});
        break;
    default:
        break;
    }
}

// x * 255 / the base, 255 without a base.
int32_t Tint(uint32_t base, uint32_t tint) {
    return base ? int16_t((tint * 255u) / base) : 255;
}

void Clamp255(int32_t (&c)[3]) {
    for (int32_t& v : c)
        v = std::clamp(v, 0, 255);
}

void MapLight(GuestCall& game, uint32_t em, int32_t (&c)[3]) {
    uint32_t base = 0, own = R8(em + 0x22F);

    if ((R16(em + 0x228) & 1) != 0 && RF(em + kPosition + 4) < RF(em + 0x210)) {
        base = 3;
        own += 3;
    }

    const uint32_t a = game.Call<0x8037DA50>({base, 1}), b = game.Call<0x8037DA50>({own & 0xFF, 1});
    int32_t top = 0;

    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t shift = 24 - i * 8;
        c[i] = Tint((a >> shift) & 0xFF, (b >> shift) & 0xFF);

        if (c[i] > 255 && c[i] > top)
            top = c[i];
    }

    if (top > 255)
        for (int32_t& v : c)
            v = int16_t(v * 255 / top);

    Clamp255(c);
}

void ZoneLight(GuestCall& game, uint32_t em, uint32_t zone, int32_t (&c)[3]) {
    if (game.Call<0x8037ACF8>({em + kPosition, zone}) == 0)
        return;

    const uint32_t top = std::max({R8(zone), R8(zone + 1), R8(zone + 2)});
    const float strength = float(top) / 255.0f;

    for (int32_t& v : c) {
        const int32_t half = int16_t(int32_t(0.5f * float(255 - v)));
        v = int16_t(int32_t(float(v) + float(half) * strength));
    }

    Clamp255(c);
}

void Light(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x90);
    GuestCall game{ctx};
    const uint32_t em = frame.Arg(3), ch = em + kChar;
    const bool atOnce = (frame.Arg(4) & 0xFF) == 1;
    const uint32_t count = game.Call<MaterialCount>({ch});
    int32_t target[3];
    MapLight(game, em, target);
    ZoneLight(game, em, frame[0x18], target);
    const uint32_t now = R32(em + 0x75C);
    const int32_t move = GameStep::Share(5);
    uint32_t word = 0xFF;

    for (uint32_t i = 0; i < 3; ++i) {
        const int32_t was = int32_t((now >> (24 - i * 8)) & 0xFF), goal = target[i];
        const int32_t value = atOnce ? goal : goal < was ? std::max(was - move, goal) : std::min(was + move, goal);
        word |= uint32_t(value & 0xFF) << (24 - i * 8);
    }

    for (uint32_t i = 0; i < count; ++i) {
        W32(frame[8], word);
        game.Call<0x802A4AB4>({ch, i, 0, frame[8], 0});
    }

    W32(em + 0x75C, word);
}

// Disabled for RMHJ08: MonstersMove (0x80181D1C) is variable-step independent and inlines surrounding helpers.

void MonstersActive(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    uint32_t monster = game.Call<0x802BCF94>({3});
    uint32_t hunter = game.Call<0x802BCF94>({2});
    const uint32_t monsters = game.Call<0x802BCFD0>({3}) & 0xFFFF;

    for (uint32_t i = 0; i < monsters; ++i, monster += 0xB18)
        if (R8(monster))
            game.Call<0x80174B38>({monster});

    const uint32_t hunters = game.Call<0x802BCFD0>({2}) & 0xFFFF;

    for (uint32_t i = 0; i < hunters; ++i, hunter += 0xB20)
        if (R8(hunter))
            game.Call<0x80183130>({hunter});
}

} // namespace

void MH3_MonsterColoursPort(CpuContext* ctx) {
    Colours(ctx);
}

void MH3_MonsterStepPort(CpuContext* ctx) {
    Step(ctx);
}

extern "C" void func_8017413C(CpuContext* ctx);
extern "C" void MH3_EnemyUpdate(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyUpdate", func_8017413C, Update);
}
REGISTER_NATIVE_FUNCTION_AS(0x8017413C, MH3_EnemyUpdate, "EnemyUpdate");

extern "C" void func_80172D88(CpuContext* ctx);
extern "C" void MH3_EnemyLight(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyLight", func_80172D88, Light);
}
REGISTER_NATIVE_FUNCTION_AS(0x80172D88, MH3_EnemyLight, "EnemyLight");

extern "C" void MH3_EnemyMark79C(CpuContext* ctx) {
    W16(ctx->gpr[3] + 0x79C, 1);
}
REGISTER_NATIVE_FUNCTION_AS(0x80172CC4, MH3_EnemyMark79C, "EnemyMark79C");

extern "C" void func_80174B38(CpuContext* ctx);
extern "C" void MH3_EnemyActiveStep(CpuContext* ctx) {
    PortCheck::Call(ctx, "EnemyActiveStep", func_80174B38, ActiveStep);
}
REGISTER_NATIVE_FUNCTION_AS(0x80174B38, MH3_EnemyActiveStep, "EnemyActiveStep");

// Disabled for RMHJ08: MonstersMove (0x80181D1C) is variable-step independent and inlines surrounding helpers.

extern "C" void func_80181FC4(CpuContext* ctx);
extern "C" void MH3_MonstersActive(CpuContext* ctx) {
    PortCheck::Call(ctx, "MonstersActive", func_80181FC4, MonstersActive);
}
REGISTER_NATIVE_FUNCTION_AS(0x80181FC4, MH3_MonstersActive, "MonstersActive");
