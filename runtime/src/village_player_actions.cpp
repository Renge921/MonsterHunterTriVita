// Village player actions, RMHP08. The village's player (PLW) runs one action
// at a time: each is a state machine over the byte at +5, stepped once per
// logic step, most waiting on a step countdown at +0x28 or on the end of a
// motion. Ported here (each takes the PLW in r3):
//   0x8020A800  idle: its motions by the motion playing; counts steps (+0x28)
//               and idle steps (+0x36, up to 150)
//   0x8020B090  (plw, u8 arg) motion 622 with 0x80342BD4(arg), hold motion 245
//               for 10 steps, then motion 609 once it ends
//   0x8020B188  motion 623, then 210 turned a quarter, then after 60-91 steps
//               (the local player's) action 13
//   0x8020B6CC  motion 630 at a spot (0x8020B61C), then once the village says
//               so (+325) 20 steps and the action ends
//   0x8020B834  (plw, which) motion 631/632 twice, then as 0x8020B6CC
//   0x8020C784, 0x8020C980, 0x8020CE24  motion 1 (with a message: 488, none,
//               491) for 30, 30 or 60 steps, then the action ends
//   0x8020C5EC  motion 1 for 30 steps, fade out (20 steps), move to
//               (1700, 358) and fade in (10), motion 502 with message 487 for
//               150 steps, then the action ends
//   0x8020C830, 0x8020CA14  wait for the village's answer (+302 / +304):
//               yes goes to a spot / on (0x8020FD68, action 19 or 20), no
//               waits 20 steps and ends the action
//   0x8020CC9C  the local player waits up to 600 steps for +1622, then the
//               next action (44 on 1)
//   0x8020D448, 0x8020E3A8  motion 30 / 53; in the special stance
//               (0x80208B24) the action ends after the motion or 1 step
//   0x8020D784  (plw, index) motion from the table at 0x805B99F0, 0x8020DAD0
//               motion 507: after 60 steps action 11 or 0x8020D64C
//   0x8020D10C, 0x8020D260  the run (action 1, 1) and its start (1, 2):
//               motion 3, steered by the stick and pushed forward 1 (the
//               start: 5, at motion speed 1.25) a step
//   0x8020E0A0, 0x8020E1F4  the same in kind 12 (motion 53, or 59 with +20)
// and two helpers of the player's movement (0x802094EC):
//   0x8020A0BC  (plw, s16 steps) GlideTo: a glide to +0x90 over that many
//               steps (at once from 800 away or with 0 steps)
//   0x8020A1BC  each step: the glide's move; another console's player eases
//               its yaw a quarter of the way to the wanted yaw
//
// The originals count the countdowns and counts by 1 per step. Here they
// count the whole ticks of game time in the step (GameStep::Ticks()), so the
// actions take the same time at any step length. The moves' pushes, the
// glide and the yaw ease were amounts per step: here they follow the step's
// length (GameStep::Scale()). At the stock 1/30 s step (one tick) these are
// the originals.
#include "cheats.h"
#include "player_work.h"
#include "port_check.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

// PLW fields of the village's actions (player_work.h has the shared ones).
using namespace Plw;
enum : uint32_t {
    kStarted = 6,         // u8 (idle): 0 on its first step
    kFlag20 = 20,         // u8: the other set of motions (66/63, 59/53)
    kIdleSteps = 0x36,    // s16: steps idle, up to 150
    kPosition = 0x3C,     // VEC3
    kGlideTarget = 0x90,  // VEC3: where GlideTo takes the player
    kGlideMove = 0x9C,    // VEC3: the glide's move a step
    kGlideSteps = 0xB4,   // s16: the glide's steps left
    kMoveWait = 0x266,    // s8: 10 as a move starts; it may stop or change once out (player_timers.cpp)
    kMotionSpeed = 0x354, // f32: the motion's speed (0x8026A320 sets the MHchar's rate to twice it each step)
    kMode = 0xB00,        // u8: 3 runs in motion 39 (0x80208B4C); 2 in kind 12 is the special stance
};

constexpr uint32_t kVillage = 0x80750120;            // village scene work
constexpr uint32_t kVillageReady = kVillage + 325;   // u8
constexpr uint32_t kVillageText13 = kVillage + 303;  // char[]
constexpr uint32_t kVillageText14 = kVillage + 305;  // char[]
constexpr uint32_t kVillageAnswer = kVillage + 302;  // s8: 1 yes, < 0 no, 0 waiting
constexpr uint32_t kVillageAnswer2 = kVillage + 304; // s8: likewise
constexpr uint32_t kMotionTable = 0x806BAC78;        // s16[3] {motion, blend, flags} per index
constexpr uint32_t kActionDoneTable = 0x806BAC54;    // u16[] next action ids
constexpr uint32_t kSpotTableSession = 0x806BCA50;
constexpr uint32_t kSpotTableLocal = 0x806BCAF0;
constexpr uint32_t kSessionFlag = 0x8076927B;        // system_w+0x7D3 (0x802BDB10: == 1)

enum : uint32_t {
    SetAction = 0x80251DBC,       // (plw, u8, u16 action, u16)
    NextAction = 0x80251F9C,      // (plw, u8, u16 action, u16): +14 = 1, then SetAction
    EndAction = 0x802520AC,       // (plw, u8 flag9, s32 blend, s32 flags): back to standing
    IdleStep = 0x8025761C,        // (plw)
    MessageText = 0x8025A1F0,     // LbStr (u8 table, u16 id) -> char*
    ShowMessage = 0x801B2BD0,     // (0, char*)
    SendText = 0x8039DC70,        // (u8 id, char*)
    ActionArg = 0x800717B8,       // (plw, u8)
    Fade = 0x80294990,            // fade_set (0, 1 out / 2 in)
    SpecialStance = 0x80250454,   // (plw) -> 1 in action 12 with +0xB00 == 2
    StickLevel = 0x8033D48C,      // (plw, stick 0) -> u8: the stick's tilt (+0xD4), 0 none, 1 walk, 3 run
    Answered = 0x80257F98,        // (plw)
    GoToAnswer = 0x80255F58,      // (plw, spot)
    Watch = 0x8018BA50,           // (plw, plw + 1432)
    VecToMh = 0x8028A804,         // vec_to_mh_vec3 (out, VEC3*)
    GlideTo = 0x80251A14,         // (plw, s16 steps): see GlideToPort
    SteerByStick = 0x80338668,    // (plw): the wanted yaw (+0xA8) from the stick's angle and the camera's yaw
    RotVecY = 0x8028BD04,         // rotVecY (VEC3*, u32 yaw)
    CommandBit = 0x803388C8,      // (plw, bit) -> 1 when the command bit (+0xE0) is set
    StanceStep = 0x80257D74,      // (plw): kind 12's step after its actions
    DistanceSquared = 0x8028BB34, // (VEC3* a, VEC3* b) -> |b - a|^2 in f1 (paired: ps0)
};

struct Player : Plw::Player {
    bool Idle(bool alt) const {
        const bool mode3 = Memory::Read8(plw + kMode) == 3;
        if (alt ? mode3 : !mode3)
            return false;

        Set(alt ? 1 : 5, 6, 0);
        return true;
    }
    void End() const { game.Call<EndAction>({plw, 0, 4, 0}); }
    // Tells the other players (in a session) text `id` of the village's.
    void Send(uint32_t id, uint32_t text) const {
        if (Memory::Read8(kSessionFlag) == 1)
            game.Call<SendText>({(Index() + id) & 0xFF, text});
    }

    void Message(uint32_t id) const { game.Call<ShowMessage>({0, game.Call<MessageText>({3, id})}); }
};

uint32_t FindSpot(uint32_t plw, int16_t areaKind, int16_t index) {
    const uint32_t tableBase = Memory::Read8(kSessionFlag) == 1 ? kSpotTableSession : kSpotTableLocal;

    for (uint32_t p = Memory::Read32(tableBase + Memory::Read8(plw + 0x16) * 4); int16_t(Memory::Read16(p)) != 0; p += 0x18) {
        if (int16_t(Memory::Read16(p)) == areaKind && int16_t(Memory::Read16(p + 2)) == index)
            return p;
    }

    return 0;
}

// The idle action's counts and motions; false when it started action 37.
bool IdleMotions(const Player& p) {
    const uint32_t plw = p.plw;
    p.CountUp();
    const int32_t idle = int16_t(Memory::Read16(plw + kIdleSteps)) + int32_t(GameStep::Ticks());
    Memory::Write16(plw + kIdleSteps, uint16_t(int16_t(idle) < 150 ? idle : 150));

    switch (p.Motion()) {
    case 1:
        if (!p.Idle(false) && p.Timer() > 30)
            p.Set(Memory::Read8(plw + kFlag20) == 0 ? 66 : 63, -6, 0);

        break;
    case 5:
        if (!p.Idle(true) && p.Master() && p.Timer() > 60) {
            p.game.Call<NextAction>({plw, 0, 37, 0});
            return false;
        }

        break;
    case 21:
        if (!p.Idle(true) && p.Ended()) {
            p.Set(5, 0, 145);
            p.Timer(0);
        }

        break;
    case 63:
        if (!p.Idle(false) && p.Ended())
            p.Set(65, -2, 0);

        break;
    case 66:
        if (!p.Idle(false) && p.Ended())
            p.Set(67, -2, 0);

        break;
    case 65:
    case 67:
        p.Idle(false);
        break;
    default:
        if (!p.Idle(false))
            p.SetX(1, -4, 0);

        break;
    }

    return true;
}

// 0x8020A800: idle.
void Idle(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t plw = p.plw;
    const uint32_t state = p.State();

    if (state == 0) {
        p.NextState();
        p.Timer(0);
        p.Flag9(0);
        Memory::Write8(plw + kStarted, 0);
    }

    if (state <= 1 && !IdleMotions(p))
        return;

    if (Memory::Read8(plw + kStarted) == 0)
        Memory::Write8(plw + kStarted, 1);
    else
        game.Call<IdleStep>({plw});
}

// 0x8020B090 (plw, u8 arg).
void HoldAndRelease(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Set(622, 6, 0);
        p.Timer(10);
        game.Call<ActionArg>({p.plw, frame.Arg(4) & 0xFF});
        p.HoldMotion(1);
        p.Flag9(1);
        break;
    case 1:
        if (p.CountDown()) {
            p.NextState();
            p.HoldMotion(0);
        } else {
            p.HoldMotion(1);
        }

        break;
    case 2:
        if (p.Ended()) {
            p.NextState();
            p.SetX(609, 6, 0);
        }

        break;
    }
}

// 0x8020B188.
void TurnAndWait(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t plw = p.plw;

    switch (p.State()) {
    case 0:
        p.NextState();
        p.SetX(623, 4, 0);
        p.Flag9(0);
        break;
    case 1:
        if (!p.Ended())
            break;

        p.NextState();
        p.SetX(210, 0, 146);
        Memory::Write32(plw + kAngle, Memory::Read32(plw + kAngle) + 0x4000);
        Memory::Write32(plw + kAngle2, Memory::Read32(plw + kAngle2) + 0x4000);
        p.Timer(60 + int32_t(game.Call<Rand>({1}) & 31));
        break;
    case 2:
        if (!p.Master() || !p.CountDown())
            break;

        if (p.Master())
            p.Send(1, kVillageText13);

        game.Call<SetAction>({plw, 0, 13, 32});
        break;
    }
}

// The end of 0x8020B6CC and 0x8020B834: once the village is ready, 20 steps.
void WaitForVillage(const Player& p, bool master) {
    if (!master || Memory::Read8(kVillageReady) == 0)
        return;

    p.NextState();
    p.Timer(20);

    if (p.Master())
        p.Send(13, kVillageText14);
}

void LeaveAfterWait(const Player& p) {
    if (!p.CountDown())
        return;

    p.game.Call<NextAction>({p.plw, 0, 0, 0x8000});
    p.End();
}

// 0x8020B6CC.
void GoToSpot(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t plw = p.plw;

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Flag9(0);
        p.SetX(630, 4, 0);

        if (const uint32_t spot = FindSpot(plw, 8, int16_t(p.Index()))) {
            game.Call<VecToMh>({plw + kGlideTarget, spot + 4});
            Memory::Write32(plw + kAngle, Memory::Read32(spot + 16));
            game.Call<GlideTo>({plw, 20});
        }

        break;
    case 1:
        WaitForVillage(p, p.Master());
        break;
    case 2:
        LeaveAfterWait(p);
        break;
    }
}

// 0x8020B834 (plw, which).
void PlayTwiceAndWait(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t motion = frame.Arg(4) ? 632 : 631;

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Timer(0);
        p.Flag9(0);
        p.SetX(motion, 4, 0);
        break;
    case 1:
        if (!p.Ended())
            break;

        p.Set(motion, 18, 344);

        if (!p.Master() || Memory::Read8(kVillageReady) == 0)
            break;

        p.NextState();
        p.Timer(20);
        p.Send(13, kVillageText14);
        break;
    case 2:
        LeaveAfterWait(p);
        break;
    }
}

// 0x8020C784, 0x8020C980, 0x8020CE24: motion 1 for `Steps` steps (with a
// message, the local player's only when MasterOnly), then the action ends.
template <int32_t Steps, uint32_t Text, bool MasterOnly>
void StandFor(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Timer(Steps);
        p.Flag9(0);
        p.SetX(1, 6, 0);

        if (Text && (!MasterOnly || p.Master()))
            p.Message(Text);

        break;
    case 1:
        if (p.CountDown())
            p.End();

        break;
    }
}

// 0x8020C5EC.
void MoveInFade(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t plw = p.plw;

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Timer(30);
        p.Flag9(0);
        p.SetX(1, 6, 0);
        break;
    case 1:
        if (!p.CountDown())
            break;

        p.NextState();
        game.Call<Fade>({0, 1});
        p.Timer(20);
        break;
    case 2:
        if (!p.CountDown())
            break;

        p.NextState();
        game.Call<Fade>({0, 2});
        p.Timer(10);

        // Three calls, each given the one before's result.
        game.Call<0x802C58A0>({88});
        game.Call<0x80218318>();
        game.Call<0x80260F70>();
        Memory::WriteFloat32(plw + 0x3C, 1700.0f);
        Memory::WriteFloat32(plw + 0x40, 358.0f);
        Memory::Write32(plw + kAngle, 0xFC72);
        Memory::Write32(plw + kAngle2, 0xFC72);
        break;
    case 3:
        if (!p.CountDown())
            break;

        p.NextState();
        p.Timer(150);
        p.Set(502, 4, 0);
        p.Message(487);
        game.Call<0x802870A4>();
        break;
    case 4:
        if (p.CountDown())
            p.End();

        break;
    }
}

// 0x8020C830, 0x8020CA14: the local player waits for the village's answer at
// `Answer` (in motion 1, blend `Blend`); the others end the action at once.
template <uint32_t Answer, int32_t Blend, void (*Yes)(const Player&)>
void AwaitAnswer(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Flag9(0);
        p.SetX(1, Blend, 0);
        break;
    case 1: {
        if (!p.Master()) {
            p.End();
            break;
        }

        const int8_t answer = int8_t(Memory::Read8(Answer));

        if (answer == 1) {
            Yes(p);
        } else if (answer < 0) {
            p.Timer(20);
            p.NextState();
        }

        break;
    }
    case 2:
        if (p.CountDown())
            p.End();

        break;
    }
}

void GoToSpotOrSay(const Player& p) {
    const uint32_t spot = FindSpot(p.plw, 5, int16_t(p.Index()));

    if (spot)
        p.game.Call<GoToAnswer>({p.plw, spot});
    else
        p.Send(1, kVillageText13);
}

void AnswerAction(const Player& p) {
    p.game.Call<Answered>({p.plw});
    p.game.Call<SetAction>({p.plw, 0, p.Index() == 0 ? 19u : 20u, 32});
}

// 0x8020CC9C.
void AwaitSignal(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t plw = p.plw;
    Memory::Write8(plw + 1623, 1);
    game.Call<Watch>({plw, plw + 1432});

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Flag9(0);
        p.SetX(1, 6, 0);
        Memory::Write8(plw + 1622, 0);
        p.Timer(0);
        break;
    case 1: {
        if (game.Call<MasterCheck>({plw}) == 0)
            break;

        p.CountUp();
        const uint32_t signal = Memory::Read8(plw + 1622);

        if (p.Timer() <= 600 && signal == 0)
            break;

        if (signal == 1)
            game.Call<NextAction>({plw, 0, 44, 0});
        else
            game.Call<NextAction>({plw, 0, 0, 0x8000});

        break;
    }
    }
}

// 0x8020D448 (then 0x8020F850) and 0x8020E3A8: motion `Motion`; in the
// special stance the action ends once the motion does, or when 1 step of the
// countdown from 10 has passed.
template <uint32_t Motion, bool IdleAfter>
void StanceMotion(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t plw = p.plw;
    const auto special = [&] {
        return game.Call<SpecialStance>({plw}) != 0;
    };

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Flag9(0);
        game.Call<SetActFlags>({plw, 192});
        Memory::Write32(plw + kAngle, Memory::Read32(plw + kAngle2));
        p.Set(Motion, 4, 0);

        if (special())
            p.Timer(10);

        break;
    case 1:
        if (p.Ended()) {
            game.Call<EndAction>({plw, 0, 10, 0});
        } else if (special()) {
            p.Timer(p.Timer() - int32_t(GameStep::Ticks()));

            if (p.Timer() <= 10)
                game.Call<EndAction>({plw, 0, 10, 0});
        }

        break;
    }

    if (IdleAfter)
        game.Call<IdleStep>({plw});
}

// 0x8020D784 (plw, index) and 0x8020DAD0: a motion, then after 60 steps (the
// local player's) action 11 with the stick held, else 0x8020D64C.
void MotionThenAct(const Player& p) {
    p.CountDownToZero();

    if (!p.Master() || p.Timer() > 0)
        return;

    if ((p.game.Call<StickLevel>({p.plw, 0}) & 0xFF) != 0)
        p.game.Call<SetAction>({p.plw, 11, 0, 0});
    else {
        const uint32_t plw = p.plw;
        const uint32_t b = Memory::Read8(plw + 0x5C8);
        Memory::Write8(plw + 0x5C9, 1);

        if (b != 0 && b <= 0x15)
            p.game.Call<SetAction>({plw, 11, Memory::Read16(kActionDoneTable + b * 2), 0});
    }
}

void TableMotion(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};

    switch (p.State()) {
    case 0: {
        p.NextState();
        p.Flag9(0);
        const uint32_t entry = kMotionTable + 6 * frame.Arg(4);
        const auto s16 = [](uint32_t a) {
            return int32_t(int16_t(Memory::Read16(a)));
        };

        p.Set(uint32_t(s16(entry)) & 0xFFFF, s16(entry + 2), s16(entry + 4));
        p.Timer(60);
        break;
    }
    case 1:
        MotionThenAct(p);
        break;
    }
}

void Motion507(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Flag9(0);
        p.Set(507, 4, 0);
        p.Timer(60);
        break;
    case 1:
        MotionThenAct(p);
        break;
    }
}

// The runs: 0x8020D10C (action 1, 1) and its start 0x8020D260 (1, 2), and in
// kind 12 0x8020E0A0 and its start 0x8020E1F4. On the first step each sets
// its motion (a start also the motion's speed) and the wait (+0x266) to 10
// steps. Then, each step, the local player steers by the stick and is pushed
// forward: 1 a step, a start 5. With the stick let go the action stops
// ((1, 3) or (12, 4)) once the wait is out, and a start goes on to the run
// ((1, 1) or (12, 2)) once the wait is out and command bit 0 is clear. The
// motion's root motion (mhchar_motion.cpp) moves the player as well.
//
// The originals push the same amount on every step, so the hunter ran faster
// the shorter the steps were. Here the push is the amount times the step's
// length: the same ground per second at any step length.
enum : uint32_t {
    kYaw = 0x58,       // u32 (u16 angle), as Plw::kAngle2
    kWantedYaw = 0xA8, // u32 (u16 angle), as Plw::kAngle
};

constexpr float kRunPush = 1.0f, kStartPush = 5.0f; // f32 0x8079BFE8, 0x8079C0A0

// Pushes the player forward along its yaw by `forward` times the step's
// length, through the guest VEC3 at `move` (rotVecY turns it).
void PushForward(const Player& p, uint32_t move, float forward) {
    Memory::WriteFloat32(move, 0.0f);
    Memory::WriteFloat32(move + 4, 0.0f);
    Memory::WriteFloat32(move + 8, float(forward * GameStep::Scale()));
    p.game.Call<RotVecY>({move, Memory::Read32(p.plw + kYaw)});

    for (uint32_t k = 0; k < 12; k += 4)
        Memory::WriteFloat32(p.plw + kPosition + k, Memory::ReadFloat32(p.plw + kPosition + k) + Memory::ReadFloat32(move + k));
}

bool MoveWaitOut(const Player& p) {
    return int8_t(Memory::Read8(p.plw + kMoveWait)) <= 0;
}

// The first step of a kind-1 run: motion 3, or 39 in mode 3 (0x80208B4C).
void StartRun(const Player& p, bool start) {
    const uint32_t plw = p.plw;

    if (Memory::Read8(plw + kMode) == 3) {
        p.SetX(39, 4, 0);

        if (!start)
            return; // (no wait set)
        Memory::WriteFloat32(plw + kMotionSpeed, 1.1f);
    } else if (!start) {
        p.SetX(3, 6, 0);
    } else {
        if (p.Motion() == 42) // after turning round
            p.SetX(3, 0, 38);
        else
            p.SetX(3, 4, 0);

        if (!p.game.Call<SpecialStance>({plw}))
            Memory::WriteFloat32(plw + kMotionSpeed, Cheats::VillageDashSpeed()); // 1.25
    }

    Memory::Write8(plw + kMoveWait, 10);
}

// The first step of a kind-12 run: motion 53, or 59 with +20.
void StartStanceRun(const Player& p, bool start) {
    const uint32_t plw = p.plw;
    p.SetX(Memory::Read8(plw + kFlag20) == 0 ? 53 : 59, 4, 0);

    if (start && !p.game.Call<SpecialStance>({plw}))
        Memory::WriteFloat32(plw + kMotionSpeed, 1.25f);

    Memory::Write8(plw + kMoveWait, 10);
}

template <bool Stance12, bool Start>
void Run(CpuContext* ctx) {
    GuestFrame frame(ctx, 32);
    GuestCall game{ctx};
    const Player p{{game, frame.Arg(3)}};
    const uint32_t plw = p.plw;
    constexpr uint32_t kind = Stance12 ? 12 : 1, run = Stance12 ? 2 : 1, stop = Stance12 ? 4 : 3;

    switch (p.State()) {
    case 0:
        p.NextState();
        p.Flag9(0);

        if (Stance12)
            StartStanceRun(p, Start);
        else
            StartRun(p, Start);

        break;
    case 1:
        if (!p.Master())
            break;

        if ((game.Call<StickLevel>({plw, 0}) & 0xFF) == 0) {
            if (MoveWaitOut(p)) {
                game.Call<SetAction>({plw, kind, stop, 0});

                if (Stance12)
                    return; // (kind 12 skips its step then)
            }

            break;
        }

        game.Call<SteerByStick>({plw});
        PushForward(p, frame[8], !Start ? kRunPush : Stance12 ? kStartPush : Cheats::VillageDashPush());

        if (Start && game.Call<CommandBit>({plw, 0}) == 0 && MoveWaitOut(p))
            game.Call<SetAction>({plw, kind, run, 0});

        break;
    }

    game.Call<Stance12 ? StanceStep : IdleStep>({plw});
}

// 0x8020A0BC GlideTo (plw, s16 steps): glides the player to +0x90 over
// `steps` steps (the move a step in +0x9C, the steps left in +0xB4), or puts
// it there at once when it is 800 or more away or with no steps.
void GlideToPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 48);
    GuestCall game{ctx};
    const uint32_t plw = frame.Arg(3);
    const int16_t steps = int16_t(frame.Arg(4));
    game.Call<DistanceSquared>({plw + kPosition, plw + kGlideTarget});

    // f1 is left as a paired single (ps_sum0): the scalar is its ps0.
    const double distanceSquared = PPC_PsToScalarInline(ctx->fpr[1].d);

    if (distanceSquared >= 800.0f * 800.0f || steps == 0) { // f32 640000 at 0x8079C07C
        Memory::Write16(plw + kGlideSteps, 0);

        for (uint32_t k = 0; k < 12; k += 4) {
            Memory::Write32(plw + kPosition + k, Memory::Read32(plw + kGlideTarget + k)); // Vec3Copy
            Memory::WriteFloat32(plw + kGlideMove + k, 0.0f);
        }

        return;
    }

    Memory::Write16(plw + kGlideSteps, uint16_t(steps));

    for (uint32_t k = 0; k < 12; k += 4)
        Memory::WriteFloat32(plw + kGlideMove + k, (Memory::ReadFloat32(plw + kGlideTarget + k) - Memory::ReadFloat32(plw + kPosition + k)) / float(steps));
}

// The part of its current tick a glide has used up, per PLW, with the
// steps-left count it belongs to (a count GlideTo sets anew starts afresh).
struct Glide {
    int16_t steps = 0;
    double used = 0;
};

std::unordered_map<uint32_t, Glide> glides;

// 0x8020A1BC, each step from the player's movement (0x802094EC). Another
// console's player turns its yaw a quarter of the way to the wanted yaw (the
// s16 gap / 4, toward 0). Any player in a glide moves by +0x9C and counts
// +0xB4 down. Both were per step: here the yaw closes 1 - (3/4)^s of the gap
// (truncated as the original), and a glide steps a tick of game time at a
// time, moving by the part of the step it spends, so it lasts its steps in
// ticks at any step length. At the stock step this is the original.
void GlideStepPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 16);
    GuestCall game{ctx};
    const uint32_t plw = frame.Arg(3);
    const double s = GameStep::Scale();

    if (game.Call<MasterCheck>({plw}) == 0) {
        const uint32_t yaw = Memory::Read32(plw + kYaw);
        const int32_t gap = int16_t(uint16_t(Memory::Read32(plw + kWantedYaw) - yaw));
        Memory::Write32(plw + kYaw, yaw + uint32_t(int32_t(gap * (1.0 - std::pow(0.75, s)))));
    }

    int16_t steps = int16_t(Memory::Read16(plw + kGlideSteps));

    if (steps <= 0)
        return;

    Glide& glide = glides[plw];

    if (glide.steps != steps)
        glide.used = 0;

    double time = s, moved = 0;

    while (steps > 0 && time > 0) {
        const double need = 1.0 - glide.used;

        if (time >= need) {
            time -= need;
            moved += need;
            glide.used = 0;
            --steps;
        } else {
            glide.used += time;
            moved += time;
            time = 0;
        }
    }

    glide.steps = steps;
    Memory::Write16(plw + kGlideSteps, uint16_t(steps));

    for (uint32_t k = 0; k < 12; k += 4)
        Memory::WriteFloat32(plw + kPosition + k, Memory::ReadFloat32(plw + kPosition + k) + Memory::ReadFloat32(plw + kGlideMove + k) * float(moved));
}

} // namespace

extern "C" void func_802520B4(CpuContext* ctx);
extern "C" void MH3_VillageActionIdle(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionIdle", func_802520B4, Idle);
}
REGISTER_NATIVE_FUNCTION_AS(0x802520B4, MH3_VillageActionIdle, "VillageActionIdle");

extern "C" void func_80252830(CpuContext* ctx);
extern "C" void MH3_VillageActionHoldAndRelease(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionHoldAndRelease", func_80252830, HoldAndRelease);
}
REGISTER_NATIVE_FUNCTION_AS(0x80252830, MH3_VillageActionHoldAndRelease, "VillageActionHoldAndRelease");

extern "C" void func_80252924(CpuContext* ctx);
extern "C" void MH3_VillageActionTurnAndWait(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionTurnAndWait", func_80252924, TurnAndWait);
}
REGISTER_NATIVE_FUNCTION_AS(0x80252924, MH3_VillageActionTurnAndWait, "VillageActionTurnAndWait");

extern "C" void func_80252BB0(CpuContext* ctx);
extern "C" void MH3_VillageActionGoToSpot(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionGoToSpot", func_80252BB0, GoToSpot);
}
REGISTER_NATIVE_FUNCTION_AS(0x80252BB0, MH3_VillageActionGoToSpot, "VillageActionGoToSpot");

extern "C" void func_80252E10(CpuContext* ctx);
extern "C" void MH3_VillageActionPlayTwiceAndWait(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionPlayTwiceAndWait", func_80252E10, PlayTwiceAndWait);
}
REGISTER_NATIVE_FUNCTION_AS(0x80252E10, MH3_VillageActionPlayTwiceAndWait, "VillageActionPlayTwiceAndWait");

// Disabled for RMHJ08: VillageActionStand488 and VillageActionStand491 are inlined into dispatcher 0x80253A5C.

extern "C" void func_802533D0(CpuContext* ctx);
extern "C" void MH3_VillageActionStand(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionStand", func_802533D0, StandFor<30, 0, false>);
}
REGISTER_NATIVE_FUNCTION_AS(0x802533D0, MH3_VillageActionStand, "VillageActionStand");

extern "C" void func_802534CC(CpuContext* ctx);
extern "C" void MH3_VillageActionMoveInFade(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionMoveInFade", func_802534CC, MoveInFade);
}
REGISTER_NATIVE_FUNCTION_AS(0x802534CC, MH3_VillageActionMoveInFade, "VillageActionMoveInFade");

extern "C" void func_8025365C(CpuContext* ctx);
extern "C" void MH3_VillageActionAwaitSpot(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionAwaitSpot", func_8025365C, AwaitAnswer<kVillageAnswer, -6, GoToSpotOrSay>);
}
REGISTER_NATIVE_FUNCTION_AS(0x8025365C, MH3_VillageActionAwaitSpot, "VillageActionAwaitSpot");

extern "C" void func_80253814(CpuContext* ctx);
extern "C" void MH3_VillageActionAwaitAnswer(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionAwaitAnswer", func_80253814, AwaitAnswer<kVillageAnswer2, 6, AnswerAction>);
}
REGISTER_NATIVE_FUNCTION_AS(0x80253814, MH3_VillageActionAwaitAnswer, "VillageActionAwaitAnswer");

extern "C" void func_80253950(CpuContext* ctx);
extern "C" void MH3_VillageActionAwaitSignal(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionAwaitSignal", func_80253950, AwaitSignal);
}
REGISTER_NATIVE_FUNCTION_AS(0x80253950, MH3_VillageActionAwaitSignal, "VillageActionAwaitSignal");

extern "C" void func_80254BF0(CpuContext* ctx);
extern "C" void MH3_VillageActionStanceMotion30(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionStanceMotion30", func_80254BF0, StanceMotion<30, true>);
}
REGISTER_NATIVE_FUNCTION_AS(0x80254BF0, MH3_VillageActionStanceMotion30, "VillageActionStanceMotion30");

extern "C" void func_80255B04(CpuContext* ctx);
extern "C" void MH3_VillageActionStanceMotion53(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionStanceMotion53", func_80255B04, StanceMotion<53, false>);
}
REGISTER_NATIVE_FUNCTION_AS(0x80255B04, MH3_VillageActionStanceMotion53, "VillageActionStanceMotion53");

extern "C" void func_80254DF4(CpuContext* ctx);
extern "C" void MH3_VillageActionTableMotion(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionTableMotion", func_80254DF4, TableMotion);
}
REGISTER_NATIVE_FUNCTION_AS(0x80254DF4, MH3_VillageActionTableMotion, "VillageActionTableMotion");

extern "C" void func_80254F10(CpuContext* ctx);
extern "C" void MH3_VillageActionMotion507(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionMotion507", func_80254F10, Motion507);
}
REGISTER_NATIVE_FUNCTION_AS(0x80254F10, MH3_VillageActionMotion507, "VillageActionMotion507");

extern "C" void func_802548E4(CpuContext* ctx);
extern "C" void MH3_VillageActionRun(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionRun", func_802548E4, Run<false, false>);
}
REGISTER_NATIVE_FUNCTION_AS(0x802548E4, MH3_VillageActionRun, "VillageActionRun");

extern "C" void func_80254A1C(CpuContext* ctx);
extern "C" void MH3_VillageActionRunStart(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionRunStart", func_80254A1C, Run<false, true>);
}
REGISTER_NATIVE_FUNCTION_AS(0x80254A1C, MH3_VillageActionRunStart, "VillageActionRunStart");

extern "C" void func_80255824(CpuContext* ctx);
extern "C" void MH3_VillageActionStanceRun(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionStanceRun", func_80255824, Run<true, false>);
}
REGISTER_NATIVE_FUNCTION_AS(0x80255824, MH3_VillageActionStanceRun, "VillageActionStanceRun");

extern "C" void func_80255960(CpuContext* ctx);
extern "C" void MH3_VillageActionStanceRunStart(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageActionStanceRunStart", func_80255960, Run<true, true>);
}
REGISTER_NATIVE_FUNCTION_AS(0x80255960, MH3_VillageActionStanceRunStart, "VillageActionStanceRunStart");

extern "C" void func_80251A14(CpuContext* ctx);
extern "C" void MH3_VillageGlideTo(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageGlideTo", func_80251A14, GlideToPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x80251A14, MH3_VillageGlideTo, "VillageGlideTo");

extern "C" void func_80251B1C(CpuContext* ctx);
extern "C" void MH3_VillageGlideStep(CpuContext* ctx) {
    PortCheck::Call(ctx, "VillageGlideStep", func_80251B1C, GlideStepPort);
}
REGISTER_NATIVE_FUNCTION_AS(0x80251B1C, MH3_VillageGlideStep, "VillageGlideStep");
