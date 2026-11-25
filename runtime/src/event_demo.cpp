// Event demos (the in-engine cutscenes), RMHP08. C reconstructions
// (decompiled with Ghidra) of the event demo driver and its helpers:
//
//   0x803C3FD0  EventDemoDriver: once per step from the field frame
//   0x803C4554  EventDemoTriggered: an entry's demo should start now
//   0x803C46C4  EventDemoUpdate: the playing demo's script clock and hooks
//   0x803C4914  EventDemoFades: screen fades on given demo frames
//   0x803C49FC  EventDemoEffects: effects on given demo frames
//   0x800F1E44  DemoStreamsLoaded: the demo's stream files are read
//   0x800F2D3C  DemoStreamsPrepare
//   0x800F2E9C  DemoStreamsReady: both streams can start
//   0x800F2FD8  DemoStreamsPlay
//
// The driver's state is the block at 0x806D4FA0 (kDemo below). A demo starts
// from one of three entries the field registers: when its area is the
// current one (and the special cases of 0x803C4554 allow it), the driver
// loads the demo's two streams (slot 0 its sound effects, slot 2 its music;
// demo_bgm_load 0x800F19E8 reads them), waits for both to be prepared, starts
// them together, then runs the demo from that same step: the script clock
// (+4) counts from 0 while the camera script plays, and the demo ends when
// the camera script is done.
//
// The audio starts on the step the clock starts, so the cutscene stays in
// sync with its streams as long as the clock counts the game time the audio
// plays in. The clock counts whole ticks (the tick site 0x803C477C in the
// translated original; here `GameStep::Ticks()`). The fades and the effects
// test `clock * 2 == frame` against tables in half-step frames, once a step:
// at more than 30 steps a second the clock holds between ticks and the test
// passed on every step of the tick (the fade restarted, the effect spawned
// several times), and on a step of several ticks a frame could be skipped.
// The ports test every clock value this step's ticks crossed instead. At the
// stock 1/30 s step these are the originals (MH3_PORT_CHECK=EventDemoDriver,
// ... MH3_VARIABLE_STEP=0).
//
// MH3_TRACE=demo logs the demo clock against the audio clock (the mixer's
// rendered ticks since the streams started) and the host's.
#include "abi_bridge.h"
#include "audio/native_audio_mixer.h"
#include "dev_options.h"
#include "game_step.h"
#include "guest_call.h"
#include "memory.h"
#include "port_check.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>

extern "C" {
void func_8038BE8C(CpuContext*);
void func_8038CA40(CpuContext*);
void func_8038CBB0(CpuContext*);
void func_8038CE04(CpuContext*);
void func_8038CEEC(CpuContext*);
void func_802A0A04(CpuContext*);
void func_80284270(CpuContext*);
void func_802843BC(CpuContext*);
void func_802844F0(CpuContext*);
}

namespace {

// The driver's state block.
constexpr uint32_t kDemo = 0x807863D0;

enum : uint32_t {
    kState = kDemo + 0x0,    // u8: 0 start, 1 waiting for a trigger, 2 loading, 3 preparing, 4 playing
    kSlot = kDemo + 0x1,     // u8: the playing entry
    kId = kDemo + 0x2,       // u8: the demo, 0xFF none
    kClock = kDemo + 0x4,    // s32 demo steps (EventDemoGetSteps), the script clock
    kForce0E = kDemo + 0x8,  // u8: 1 lets demo 0x0E start
    kNoStream = kDemo + 0x9, // s8: demo_bgm_load's result, nonzero without streams
    kEntries = kDemo + 0xC,  // three entries of 8 bytes
};

// An entry: u8 active, u8 phase (EventDemoUpdate: 0 start, 1 play, 2 end),
// then the address of its data: u8 area, +2 u16 the flag set when it ends,
// +4 u8 the demo.
enum : uint32_t { kActive = 0, kPhase = 1, kData = 4 };
enum : uint32_t { kDataArea = 0, kDataFlag = 2, kDataId = 4 };
constexpr uint32_t kEntryCount = 3;
constexpr uint32_t kEntryBytes = 8;

constexpr uint32_t kEnabled = 0x8077FED8;     // u32 per demo: nonzero when it may play
constexpr uint32_t kDemoVolume = 0x808A7A68;  // f32: the stream gains while a demo plays
constexpr uint32_t kOnlineBusy = 0x8075024C;  // u8: 1 holds every demo in online mode 2
constexpr uint32_t kFadeTable = 0x806E7F70;   // {s8 demo, pad, s16 fade out at, s16 fade in at}, until demo < 1
constexpr uint32_t kKowareTable = 0x806E7F84; // {s16 from, until, every}, until from < 1 (demo 0x17)
constexpr uint32_t kKowareSpots = 0x806E7F98; // VEC3 per kKowareTable entry
constexpr uint32_t kGlowFrom = 0x8089EB10;    // s16 frames (demo 0x1B)
constexpr uint32_t kGlowUntil = 0x8089EB12;
constexpr uint32_t kGlowFlags = 0x807851F1;   // u8: |= 0x81 within them (0x80383960)

// The streams of each demo: u8 music (slot 2), u8 sound effects (slot 0).
constexpr uint32_t kDemoStreams = 0x806BDD14;
constexpr uint32_t kBgmManager = 0x8089E2A8;   // the field music's manager*
constexpr uint32_t kStreamFiles = 0x808A0470;  // u8[2]: the stream slots demo_bgm_load read, 0xFF none

// Online mode bytes (GetOnlineMode, PlayMode_ck, inlined).
constexpr uint32_t kOnlineMode = 0x80768ACC;
constexpr uint32_t kPlayMode = 0x80768ACD;

// The online demo's bytes (0x801843EC and 0x801845CC, inlined).
constexpr uint32_t kOnlineDemo = 0x808A4880 - 17984;

enum : uint32_t {
    MoveWorks = 0x802BCF94,     // get_move_work_adrs (kind) -> the first work
    MoveWorkCount = 0x802BCFD0, // get_move_work_max (kind) -> u16
    AreaNumber = 0x8037D338,    // get_now_areano () -> u8
    PlInEventDemo = 0x8034BFFC, // (0) -> 1
    Arena = 0x8041B014,         // () -> 1 in the arena (player work +0x22E4)
    ArenaFades = 0x8041B058,    // () -> 1: the arena keeps its own fades
    FadeSet = 0x80294990,       // fade_set (fade, kind)
    StreamBgmGain = 0x8036CE68, // set_str_bgm_main_vol (; f1)
    StreamSeGain = 0x8036CE70,  // set_str_se_main_vol (; f1)
    DemoBgmLoad = 0x802A05B0,   // demo_bgm_load (demo) -> nonzero without streams
    StreamsEnd = 0x802A0BE0,    // the demo's streams stop
    OptionValue = 0x802CAC20,   // get_option_cfg (n) -> u8
    BgmVolume = 0x802A14A8,     // set_BGM_volume (volume)
    SeVolume = 0x802A145C,      // set_SE_volume (volume)
    FieldResume = 0x8019EEC0,   // (30)
    CameraStart = 0x8037484C,   // (demo, 0): the demo's camera script
    CameraPlaying = 0x80376638, // () -> 0 once the camera script is done
    CameraLength = 0x803748A0,  // () -> s16 steps
    DemoLight = 0x8037AF58,     // (demo): demo 0x21's light, from the clock
    SetFlag = 0x802C58A0,       // (flag): the story flag the demo sets
    DemoEnded = 0x802C7428,     // (demo)
    OnlineStart0 = 0x80183F10,  // (demo)
    OnlineStart1 = 0x802E0838,
    OnlineStart2 = 0x8037AEB4,
    OnlineStart3 = 0x80383CAC,
    OnlineEnd0 = 0x802E08A8,    // ()
    OnlineEnd1 = 0x8037AF34,
    OnlineEnd2 = 0x801E3EEC,    // (14, 28 after demo 44)
    TourStart = 0x80247328,     // (demo), play mode 5
    TourEnd = 0x802473C4,       // ()
    Koware = 0x80059D68,        // eft028_set_koware (1, VEC3*, 12, 0)
    StreamReady = 0x8029F020,   // srt_ready_ck (slot) -> 1
    PlayStream = 0x8036D220,    // (slot, command): 1 load, 2 prepare, 3 play, 0x28 preload
    StreamStatus = 0x8036D3C0,  // GetStreamStatus (slot) -> 3 prepared
    BgmManagerSync = 0x80284B60,// (manager)
    BgmTrack = 0x80284E74,      // (area, track, u32* volume, u32* other)
    BgmPlay = 0x802845C8,       // (1, volume)
};

constexpr uint8_t kDemoDeviljho = 0x0E, kDemoJhenStart = 0x2B, kDemoJhen = 0x2C, kDemoJhenEnd = 0x2F;
constexpr uint8_t kDemoKoware = 0x17, kDemoGlow = 0x1B;
constexpr uint8_t kKindJhen = 20;
constexpr uint32_t kMoveWorkBytes = 0xB18;

uint8_t Id() {
    return Memory::Read8(kId);
}

int32_t Clock() {
    return int32_t(Memory::Read32(kClock));
}

// The arena demos that play both streams whatever the table says.
bool ArenaStreamsDemo(GuestCall& game, uint8_t id) {
    return game.Call<Arena>() == 1 && (id == kDemoJhen || id == kDemoJhenEnd || id == kDemoJhenStart || id == kDemoDeviljho);
}

// Calls `test(frames)` for each value of `clock * 2` (as the s16 the
// originals compute) that this step's ticks moved the clock onto: the clock
// at the end of each of the step's ticks.
template <typename Test>
void ForCrossedFrames(Test test) {
    const int32_t clock = Clock();

    for (int32_t t = clock - int32_t(GameStep::Ticks()) + 1; t <= clock; ++t)
        test(int32_t(int16_t(t << 1)));
}

// MH3_TRACE=demo: the demo clock against the audio's and the host's.
struct DemoTrace {
    bool playing = false;
    uint32_t audioStart = 0;
    std::chrono::steady_clock::time_point hostStart;
    int32_t logged = 0;

    static uint32_t AudioTicks() {
        return NativeAudio::GetMixer().FeedbackState().renderedTicks.load(std::memory_order_acquire);
    }

    void Start(uint8_t id) {
        static const TraceFlag trace("demo");

        if (!trace)
            return;

        playing = true;
        audioStart = AudioTicks();
        hostStart = std::chrono::steady_clock::now();
        logged = 0;
        std::fprintf(stderr, "MH3 demo: %u starts, streams %s\n", unsigned(id), Memory::Read8(kNoStream) ? "none" : "playing");
    }

    void Step(bool ended) {
        if (!playing)
            return;

        const int32_t clock = Clock();

        if (!ended && clock < logged + 30)
            return;

        logged = clock;
        const double demo = clock / 30.0;
        const double audio = (AudioTicks() - audioStart) * 0.003;
        const double host = std::chrono::duration<double>(std::chrono::steady_clock::now() - hostStart).count();
        std::fprintf(stderr, "MH3 demo: %u clock %d (%.3f s) audio %.3f s (%+.3f) host %.3f s (%+.3f)%s\n", unsigned(Id()), clock, demo, audio,
                     audio - demo, host, host - demo, ended ? " ends" : "");

        if (ended)
            playing = false;
    }
};

DemoTrace demoTrace;

// 0x803C4554(entry): 1 when the entry's demo should start: its area is the
// current one, and for demo 0x0E (outside the arena) the start byte is set,
// for demo 0x2C a Jhen Mohran is in that area and no player is in a demo.
// Nothing starts in online mode 2 while kOnlineBusy is set.
uint32_t EventDemoTriggered(GuestCall& game, uint32_t entry) {
    uint32_t work = game.Call<MoveWorks>({3});
    uint32_t count = game.Call<MoveWorkCount>({3}) & 0xFFFF;
    const uint8_t area = uint8_t(game.Call<AreaNumber>());
    const uint32_t data = Memory::Read32(entry + kData);

    if (Memory::Read8(data + kDataArea) != area)
        return 0;

    if (Memory::Read8(kOnlineMode) == 2 && Memory::Read8(kOnlineBusy) == 1)
        return 0;

    const uint8_t id = Memory::Read8(data + kDataId);

    if (id == kDemoDeviljho)
        return game.Call<Arena>() != 0 ? 1 : Memory::Read8(kForce0E) == 1;

    if (id == kDemoJhen) {
        if (game.Call<PlInEventDemo>({0}) == 1)
            return 0;

        for (; count; --count, work += kMoveWorkBytes)
            if (Memory::Read8(work) != 0 && Memory::Read8(work + 3) == kKindJhen && Memory::Read8(work + 0x1E1) == Memory::Read8(data + kDataArea))
                return 1;

        return 0;
    }

    return 1;
}

void EventDemoTriggeredPort(CpuContext* ctx) {
    uint32_t result;
    {
        GuestFrame frame(ctx, 0x20);
        GuestCall game{ctx};
        result = EventDemoTriggered(game, frame.Arg(3));
    }

    ctx->gpr[3] = result; // after the frame restored the GPRs
}

// 0x803C4914(demo): the demo's fades from kFadeTable (fade out 32 frames
// before its first time, fade in at its second) and the fade out 32 frames
// before the camera script ends. Not while the arena keeps its own fades.
void EventDemoFades(GuestCall& game, uint8_t id) {
    if (game.Call<Arena>() == 1 && game.Call<ArenaFades>() == 1)
        return;

    ForCrossedFrames([&](int32_t frames) {
        for (uint32_t e = kFadeTable; int8_t(Memory::Read8(e)) >= 1; e += 6) {
            if (int8_t(Memory::Read8(e)) != int8_t(id))
                continue;

            if (frames == int16_t(Memory::Read16(e + 2)) - 0x20)
                game.Call<FadeSet>({0, 1});
            else if (frames == int16_t(Memory::Read16(e + 4)))
                game.Call<FadeSet>({0, 2});
        }
    });

    const int32_t end = int16_t(game.Call<CameraLength>()) * 2 - 0x20;
    ForCrossedFrames([&](int32_t frames) {
        if (frames == end)
            game.Call<FadeSet>({0, 1});
    });
}

void EventDemoFadesPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    EventDemoFades(game, uint8_t(frame.Arg(3)));
}

// 0x803C49FC(demo): demo 0x17 breaks rocks (eft028_set_koware) at the spots
// of kKowareTable, every `every` frames in each range; demo 0x1B sets the glow
// flags every step within its range (a flag, so not by the ticks).
void EventDemoEffects(GuestCall& game, uint32_t vec, uint8_t id) {
    if (id == kDemoKoware) {
        ForCrossedFrames([&](int32_t frames) {
            uint32_t spot = kKowareSpots;

            for (uint32_t e = kKowareTable; int16_t(Memory::Read16(e)) > 0; e += 6, spot += 12) {
                const int32_t from = int16_t(Memory::Read16(e)), until = int16_t(Memory::Read16(e + 2)), every = int16_t(Memory::Read16(e + 4));

                if (frames < from || frames >= until || (every ? (frames - from) % every : frames - from) != 0)
                    continue;

                Memory::WriteFloat32(vec, Memory::ReadFloat32(spot));
                Memory::WriteFloat32(vec + 4, Memory::ReadFloat32(spot + 4));
                Memory::WriteFloat32(vec + 8, Memory::ReadFloat32(spot + 8));
                game.Call<Koware>({1, vec, 12, 0});
            }
        });
    } else if (id == kDemoGlow) {
        const int32_t frames = int16_t(Clock() << 1);

        if (int16_t(Memory::Read16(kGlowFrom)) <= frames && frames < int16_t(Memory::Read16(kGlowUntil)))
            Memory::Write8(kGlowFlags, Memory::Read8(kGlowFlags) | 0x81);
    }
}

void EventDemoEffectsPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x30);
    GuestCall game{ctx};
    EventDemoEffects(game, frame[8], uint8_t(frame.Arg(3)));
}

// 0x803C46C4(entry) -> 1 when the demo is over. Phase 0 starts the camera
// script (and the online or tour hooks) and the clock at 0; phase 1 counts
// the clock and runs the demo's per-step hooks until the camera script is
// done, then sets the entry's flag and fades in; phase 2 clears the entry.
uint32_t EventDemoUpdate(GuestCall& game, uint32_t entry) {
    const uint8_t phase = Memory::Read8(entry + kPhase);

    if (phase == 0) {
        game.Call<CameraStart>({Id(), 0});

        if (Memory::Read8(kOnlineMode) == 1) {
            game.Call<OnlineStart0>({Id()});
            Memory::Write8(kOnlineDemo + 5, 1);
            Memory::Write8(kOnlineDemo + 4, Id());
            game.Call<OnlineStart1>({Id()});
            game.Call<OnlineStart2>({Id()});
            game.Call<OnlineStart3>({Id()});
        } else if (Memory::Read8(kPlayMode) == 5) {
            game.Call<TourStart>({Id()});
        }

        Memory::Write32(kClock, 0);
        Memory::Write8(entry + kPhase, uint8_t(phase + 1));
        return 0;
    }

    if (phase == 1) {
        Memory::Write32(kClock, uint32_t(Clock() + int32_t(GameStep::Ticks())));
        game.Call<DemoLight>({Id()});
        game.Call<0x8038CE04>({Id()});
        game.Call<0x8038CEEC>({Id()});

        if (game.Call<CameraPlaying>() != 0)
            return 0;

        Memory::Write8(entry + kPhase, uint8_t(phase + 1));

        if (game.Call<Arena>() == 0) {
            game.Call<SetFlag>({Memory::Read16(Memory::Read32(entry + kData) + kDataFlag)});
            game.Call<DemoEnded>({Id()});
            game.Call<FadeSet>({0, 2});
        }

        if (Memory::Read8(kOnlineMode) == 1) {
            Memory::Write8(kOnlineDemo + 6, 1);
            game.Call<OnlineEnd0>();
            game.Call<OnlineEnd1>();
            game.Call<OnlineEnd2>({Id() == kDemoJhen ? 28u : 14u});
        } else if (Memory::Read8(kPlayMode) == 5) {
            game.Call<TourEnd>();
        }

        return 0;
    }

    if (phase == 2) {
        Memory::Write8(entry + kPhase, 0);
        Memory::Write8(entry + kActive, 0);
        Memory::Write8(kId, 0xFF);
        Memory::Write32(kClock, 0);
        return 1;
    }

    return 0;
}

void EventDemoUpdatePort(CpuContext* ctx) {
    uint32_t result;
    {
        GuestFrame frame(ctx, 0x10);
        GuestCall game{ctx};
        result = EventDemoUpdate(game, frame.Arg(3));
    }

    ctx->gpr[3] = result; // after the frame restored the GPRs
}

// 0x800F1E44() -> 1 once both stream files are read. The original tests the
// first slot's byte for both (r13-17384, not the loop's pointer).
uint32_t DemoStreamsLoaded(GuestCall& game) {
    uint32_t done = 0;

    for (uint32_t i = 0; i < 2; ++i)
        if (Memory::Read8(kStreamFiles + i) == 0xFF || game.Call<StreamReady>({Memory::Read8(kStreamFiles)}) == 1)
            ++done;

    return done == 2;
}

void DemoStreamsLoadedPort(CpuContext* ctx) {
    uint32_t result;
    {
        GuestFrame frame(ctx, 0x20);
        GuestCall game{ctx};
        result = DemoStreamsLoaded(game);
    }

    ctx->gpr[3] = result; // after the frame restored the GPRs
}

// 0x800F2D3C(demo): asks the loaded streams to preload; demo 0x2C outside
// the arena also switches the field music to its next track.
void DemoStreamsPrepare(GuestCall& game, uint32_t sp, uint8_t id) {
    const uint32_t manager = Memory::Read32(kBgmManager);
    const uint8_t music = Memory::Read8(kDemoStreams + id * 2u), effects = Memory::Read8(kDemoStreams + id * 2u + 1);

    if (effects && game.Call<StreamReady>({0}) == 1)
        game.Call<PlayStream>({0, 0x28});

    if (music && game.Call<StreamReady>({2}) == 1)
        game.Call<PlayStream>({2, 0x28});

    if (game.Call<Arena>() == 1) {
        if (id == kDemoJhen || id == kDemoJhenEnd || id == kDemoJhenStart || id == kDemoDeviljho)
            game.Call<PlayStream>({2, 0x28});

        return;
    }

    if (id != kDemoJhen)
        return;

    game.Call<BgmManagerSync>({manager});

    if (Memory::Read8(manager + 0x1D9) == 0 || int16_t(Memory::Read16(manager + 0x1DC)) == -1)
        return;

    game.Call<BgmTrack>({Memory::Read8(manager + 0x1E2), uint32_t(int32_t(int16_t(Memory::Read16(manager + 0x1DC)))), sp + 12, sp + 8});
    Memory::Write16(manager + 0x5A, Memory::Read16(manager + 0x1DC));
    Memory::Write16(manager + 0x54, 0);
    Memory::Write8(manager + 0x5E, uint8_t(Memory::Read32(sp + 12)));
    Memory::Write8(manager + 0x62, uint8_t(Memory::Read32(sp + 8)));
    Memory::Write8(manager + 0x4D, 1);
    game.Call<BgmPlay>({1, Memory::Read8(manager + 0x5E)});
    Memory::Write8(manager + 0x58, Memory::Read8(manager + 0x5E));
}

void DemoStreamsPreparePort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    DemoStreamsPrepare(game, frame[0], uint8_t(frame.Arg(3)));
}

// 0x800F2E9C(demo) -> 1 when every stream the demo uses is prepared
// (status 3); one still loading (1, 2) is asked to prepare.
uint32_t DemoStreamsReady(GuestCall& game, uint8_t id) {
    bool music = Memory::Read8(kDemoStreams + id * 2u) != 0;
    const bool effects = Memory::Read8(kDemoStreams + id * 2u + 1) != 0;

    if (ArenaStreamsDemo(game, id))
        music = true;

    if (!music && !effects)
        return 1;

    uint32_t ready = 0;

    for (const auto& [used, slot] : {std::pair{effects, 0u}, std::pair{music, 2u}}) {
        if (!used) {
            ++ready;
            continue;
        }

        const uint32_t status = game.Call<StreamStatus>({slot});

        if (status - 1 < 2)
            game.Call<PlayStream>({slot, 2});

        if (status == 3)
            ++ready;
    }

    return ready == 2;
}

void DemoStreamsReadyPort(CpuContext* ctx) {
    uint32_t result;
    {
        GuestFrame frame(ctx, 0x20);
        GuestCall game{ctx};
        result = DemoStreamsReady(game, uint8_t(frame.Arg(3)));
    }

    ctx->gpr[3] = result; // after the frame restored the GPRs
}

// 0x800F2FD8(demo): starts the prepared streams.
void DemoStreamsPlay(GuestCall& game, uint8_t id) {
    bool music = Memory::Read8(kDemoStreams + id * 2u) != 0;
    const bool effects = Memory::Read8(kDemoStreams + id * 2u + 1) != 0;

    if (ArenaStreamsDemo(game, id))
        music = true;

    if (effects && game.Call<StreamStatus>({0}) == 3)
        game.Call<PlayStream>({0, 3});

    if (music && game.Call<StreamStatus>({2}) == 3)
        game.Call<PlayStream>({2, 3});
}

void DemoStreamsPlayPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    DemoStreamsPlay(game, uint8_t(frame.Arg(3)));
}

// 0x803C3FD0: the driver, once per step (field_frame.cpp's frame moves).
// The original first calls IsEventDemoState4 and drops the result.
void EventDemoDriver(GuestCall& game) {
    uint8_t state = Memory::Read8(kState);

    if (state == 0) {
        Memory::Write8(kState, 1);
        return;
    }

    if (state == 1) {
        for (uint32_t i = 0; i < kEntryCount; ++i) {
            const uint32_t entry = kEntries + i * kEntryBytes;

            if (Memory::Read8(entry + kActive) == 0)
                continue;

            const uint8_t id = Memory::Read8(Memory::Read32(entry + kData) + kDataId);

            if (Memory::Read32(kEnabled + id * 4u) == 0 || int32_t(game.Call<0x8038CA40>({entry})) <= 0)
                continue;

            const double volume = Memory::ReadFloat32(kDemoVolume);
            game.Call<StreamBgmGain>({}, {volume});
            game.Call<StreamSeGain>({}, {volume});
            Memory::Write8(kState, 2);
            Memory::Write8(kSlot, uint8_t(i));
            Memory::Write8(kId, Memory::Read8(Memory::Read32(entry + kData) + kDataId));
            game.Call<FadeSet>({0, 2});
            Memory::Write8(kNoStream, uint8_t(game.Call<DemoBgmLoad>({Id()})));
            return;
        }

        return;
    }

    if (state == 2) {
        game.Call<FadeSet>({0, 2});

        if (Memory::Read8(kNoStream) != 0) {
            Memory::Write8(kState, 3);
        } else if (game.Call<0x802A0A04>() != 0) {
            game.Call<0x80284270>({Id()});
            Memory::Write8(kState, 3);
        }

        return;
    }

    if (state == 3) {
        game.Call<FadeSet>({0, 2});

        if (Memory::Read8(kNoStream) == 0) {
            if (game.Call<0x802843BC>({Id()}) == 0)
                return;

            game.Call<0x802844F0>({Id()});
        }

        game.Call<BgmVolume>({uint8_t(game.Call<OptionValue>({17}))});
        game.Call<SeVolume>({uint8_t(game.Call<OptionValue>({18}))});
        Memory::Write8(kState, 4);
        state = 4;
        demoTrace.Start(Id());
    }

    if (state != 4)
        return;

    const bool ended = game.Call<0x8038CBB0>({kEntries + Memory::Read8(kSlot) * kEntryBytes}) != 0;
    demoTrace.Step(ended);

    if (ended) {
        Memory::Write8(kState, 1);
        game.Call<StreamsEnd>();
        game.Call<FieldResume>({30});
    }
}

void EventDemoDriverPort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x20);
    GuestCall game{ctx};
    EventDemoDriver(game);
}

} // namespace

extern "C" void MH3_EventDemoDriver(CpuContext* ctx) {
    PortCheck::Call(ctx, "EventDemoDriver", func_8038BE8C, EventDemoDriverPort);
}

extern "C" void MH3_EventDemoTriggered(CpuContext* ctx) {
    PortCheck::Call(ctx, "EventDemoTriggered", func_8038CA40, EventDemoTriggeredPort);
}

extern "C" void MH3_EventDemoUpdate(CpuContext* ctx) {
    PortCheck::Call(ctx, "EventDemoUpdate", func_8038CBB0, EventDemoUpdatePort);
}

extern "C" void MH3_EventDemoFades(CpuContext* ctx) {
    PortCheck::Call(ctx, "EventDemoFades", func_8038CE04, EventDemoFadesPort);
}

extern "C" void MH3_EventDemoEffects(CpuContext* ctx) {
    PortCheck::Call(ctx, "EventDemoEffects", func_8038CEEC, EventDemoEffectsPort);
}

extern "C" void MH3_DemoStreamsLoaded(CpuContext* ctx) {
    PortCheck::Call(ctx, "DemoStreamsLoaded", func_802A0A04, DemoStreamsLoadedPort);
}

extern "C" void MH3_DemoStreamsPrepare(CpuContext* ctx) {
    PortCheck::Call(ctx, "DemoStreamsPrepare", func_80284270, DemoStreamsPreparePort);
}

extern "C" void MH3_DemoStreamsReady(CpuContext* ctx) {
    PortCheck::Call(ctx, "DemoStreamsReady", func_802843BC, DemoStreamsReadyPort);
}

extern "C" void MH3_DemoStreamsPlay(CpuContext* ctx) {
    PortCheck::Call(ctx, "DemoStreamsPlay", func_802844F0, DemoStreamsPlayPort);
}

REGISTER_NATIVE_FUNCTION_AS(0x8038BE8C, MH3_EventDemoDriver, "EventDemoDriver");
REGISTER_NATIVE_FUNCTION_AS(0x8038CA40, MH3_EventDemoTriggered, "EventDemoTriggered");
REGISTER_NATIVE_FUNCTION_AS(0x8038CBB0, MH3_EventDemoUpdate, "EventDemoUpdate");
REGISTER_NATIVE_FUNCTION_AS(0x8038CE04, MH3_EventDemoFades, "EventDemoFades");
REGISTER_NATIVE_FUNCTION_AS(0x8038CEEC, MH3_EventDemoEffects, "EventDemoEffects");
REGISTER_NATIVE_FUNCTION_AS(0x802A0A04, MH3_DemoStreamsLoaded, "DemoStreamsLoaded");
REGISTER_NATIVE_FUNCTION_AS(0x80284270, MH3_DemoStreamsPrepare, "DemoStreamsPrepare");
REGISTER_NATIVE_FUNCTION_AS(0x802843BC, MH3_DemoStreamsReady, "DemoStreamsReady");
REGISTER_NATIVE_FUNCTION_AS(0x802844F0, MH3_DemoStreamsPlay, "DemoStreamsPlay");
