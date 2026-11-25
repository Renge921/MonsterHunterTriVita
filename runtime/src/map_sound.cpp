// Map ambient sounds, RMHP08. C reconstruction of the per-step update of the
// map's sound sources (0x800DFE30, called by the sound task 0x800D83B8),
// decompiled with Ghidra (scripts/dol2elf.py -> ELF import).
//
// Each source in the map's sound table repeats every `interval` steps: a
// countdown reaches 0, the sound is requested on the shell (se) object and the
// countdown restarts. A looping source placed on an area also follows the
// player each step (clamped into its box). The countdown is in steps, so here
// it counts the whole ticks of game time (GameStep::Ticks) instead: sounds
// repeat at the same rate at any step length, while the following still runs
// every step. At the stock 1/30 s step both are the original.
#include "game_step.h"
#include "port_check.h"
#include "guest_call.h"

#include <algorithm>

namespace {

constexpr uint32_t kSeWork = 0x808A04D0;     // se_w*: r13-17328
constexpr uint32_t kSeShell = 0x292E0;       // se_w: shell se object (_se_w*)
constexpr uint32_t kSeLoopHandles = 0x292E4; // se_w: looping source handles [n]
constexpr uint32_t kAreaNow = 0x2927E;       // this: u8 current area
constexpr uint32_t kAreaPrev = 0x2927F;      // this: u8 area last step
constexpr uint32_t kMoveWorkStride = 0xB20;  // player work

// Stereo pair of the village ambience (sda2 VEC3 components).
constexpr uint32_t kAmbienceLeftX = 0x808A6C94, kAmbienceY = 0x808A6C98, kAmbienceLeftZ = 0x808A6C9C;
constexpr uint32_t kAmbienceRightX = 0x808A6CA0, kAmbienceRightZ = 0x808A6CA4;
constexpr uint32_t kSysFlag30 = 0x80768AA8 + 0x30;

// Map sound table: u32 valid, u32 count, then sources.
enum : uint32_t {
    kSources = 8,
    kSourceSize = 72,
    kState = 0,       // u8: 0 unused, 1 armed, 2 done (one-shot played)
    kFollows = 1,     // u8: 1 = clamped into its box around the listener
    kSuppressed = 2,  // u8
    kPosition = 4,    // VEC3
    kBoxMin = 28,     // VEC3
    kBoxMax = 40,     // VEC3
    kInterval = 60,   // s32 steps (0 = one-shot)
    kSoundId = 64,    // u32
    kLoopHandle = 68, // u8 index into se_w's handles, 255 none
    kArea = 69,       // u8
    kCountdown = 70,  // s16 steps
};

// Shell se object.
enum : uint32_t {
    kShellId = 12,
    kShellListener = 24, // VEC3
    kShellListenerDir = 36,
    kShellSlots = 60, // [32] of 80 bytes
};

// Shell sound slot.
enum : uint32_t {
    kSlotPosition = 4,
    kSlotPrevPosition = 16,
    kSlotVoice = 60
};

enum : uint32_t {
    ShellAlloc = 0x802B11D4,        // (8, 0, 0) -> shell
    ResetMapSoundTable = 0x802BC0D0,// (se_w) -> clears map sound table entries
    SceneFlag = 0x80294B78,         // (0) -> state (2 == inactive?)
    DemoState4 = 0x8038CD6C,        // () -> 1 during event demo state 4
    GetMoveWorkAdrs = 0x802BCF94,   // (type) -> work table
    MyPlayerIndex = 0x802BC844,     // () -> s8
    MapSoundTable = 0x802A13DC,     // () -> table or 0
    ShellFind = 0x8029F828,         // (id) -> -1 if gone
    GetNowAreaNo = 0x8037D338,
    GetNowMapNo = 0x8037D328,
    MapKind = 0x8037DEF0,           // (map) -> u8
    AmbienceMuted = 0x801F903C,     // () -> u8
    ShellSeReq = 0x802B8708,        // (shell, VEC3*, u8 bank, u32 no)
    VoicePlaying = 0x80366EC4,      // (voice) -> 0 when stopped
    OnlineMode = 0x802BC6BC,        // () -> u8
    ClampToBox = 0x801A13B4,        // (min, max, point, out)
    ArenaQuestA = 0x802697E0,
    ArenaMonsterAlive = 0x80223BE4, // (i)
    ArenaQuestB = 0x80269770,
    ShellSlotUpdate = 0x802B0CDC,   // (shell, slot)
    ShellSlotFinish1 = 0x802B0284,  // (slot) when +2 == 1
    ShellSlotFinish2 = 0x802B0588,  // (slot) when +2 == 2..4
};

static bool SoundOfflineInline(GuestCall& game) {
    const uint32_t scene0 = game.Call<GetMoveWorkAdrs>({0});
    if (scene0 != 0 && Memory::Read8(scene0 + 0x112) != 0)
        return true;
    const uint8_t online = uint8_t(game.Call<OnlineMode>());
    if (online == 0 || online == 3)
        return false;
    if (Memory::Read8(kSysFlag30) != 0)
        return true;
    if (game.Call<DemoState4>() == 1)
        return false;
    return game.Call<SceneFlag>({0}) == 2;
}

static void ClearMapSoundsInline(GuestCall& game) {
    const uint32_t shell = game.Call<ShellAlloc>({8, 0, 0});
    const uint32_t seWork = Memory::Read32(kSeWork);
    Memory::Write32(seWork + kSeShell, shell);
    for (uint32_t i = 0; i < 32; ++i)
        Memory::Write32(seWork + kSeLoopHandles + i * 4, 0);
    game.Call<ResetMapSoundTable>({seWork});
}

static void ChangeAreaInline(GuestCall& game, uint32_t shell) {
    for (uint32_t i = 0; i < 32; ++i)
        Memory::Write8(shell + kShellSlots + i * 80, 0);
    const uint32_t seWork = Memory::Read32(kSeWork);
    for (uint32_t i = 0; i < 32; ++i)
        Memory::Write32(seWork + kSeLoopHandles + i * 4, 0);
    game.Call<ResetMapSoundTable>({seWork});
}

static uint32_t ShellAllocSlotInline(uint32_t shell) {
    for (uint32_t i = 0; i < 32; ++i) {
        const uint32_t slot = shell + kShellSlots + i * 80;
        if (Memory::Read8(slot) == 0) {
            Memory::Write8(slot + 0, 1);
            Memory::Write8(slot + 2, 0);
            Memory::Write8(slot + 3, 0);
            Memory::Write8(slot + 0x24, 0);
            Memory::Write32(slot + 0x3C, 0);
            Memory::Write8(slot + 0x4A, 0);
            Memory::Write8(slot + 0x4B, 0);
            return slot;
        }
    }
    return 0;
}

void CopyWords(uint32_t dst, uint32_t src, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        Memory::Write32(dst + i * 4, Memory::Read32(src + i * 4));
}

} // namespace

static void MapSoundUpdatePort(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x70);
    GuestCall game{ctx};
    const uint32_t self = frame.Arg(3);
    const uint32_t pos = frame[32];
    for (uint32_t i = 0; i < 3; ++i)
        Memory::Write32(pos + i * 4, 0);

    if (SoundOfflineInline(game)) {
        Memory::Write8(self + kAreaPrev, Memory::Read8(self + kAreaNow));
        const uint8_t area = uint8_t(game.Call<GetNowAreaNo>());
        Memory::Write8(self + kAreaNow, area);

        const uint32_t curShell = Memory::Read32(Memory::Read32(kSeWork) + kSeShell);
        if (curShell == 0)
            ClearMapSoundsInline(game);
        else if (Memory::Read8(self + kAreaPrev) != area)
            ChangeAreaInline(game, curShell);

        return;
    }

    uint32_t player = game.Call<GetMoveWorkAdrs>({2});
    player += int32_t(int8_t(game.Call<MyPlayerIndex>())) * kMoveWorkStride;
    const uint32_t table = game.Call<MapSoundTable>();
    uint32_t shell;

    if (table == 0) {
        shell = Memory::Read32(Memory::Read32(kSeWork) + kSeShell);

        if (shell == 0)
            ClearMapSoundsInline(game);

        if (shell == 0)
            return;

        CopyWords(shell + kShellListener, self + 4, 3);
        CopyWords(shell + kShellListenerDir, self + 28, 3);
    } else {
        if (Memory::Read32(table) == 0 || Memory::Read32(table + 4) == 0)
            return;

        shell = Memory::Read32(Memory::Read32(kSeWork) + kSeShell);

        if (shell == 0)
            return;

        if (int32_t(game.Call<ShellFind>({Memory::Read32(shell + kShellId)})) == -1)
            return;

        CopyWords(shell + kShellListener, self + 4, 3);
        CopyWords(shell + kShellListenerDir, self + 28, 3);

        const uint8_t area = Memory::Read8(self + kAreaNow);

        if (area == 0 && uint8_t(game.Call<MapKind>({uint8_t(game.Call<GetNowMapNo>())})) == 1 && uint8_t(game.Call<AmbienceMuted>()) == 0) {
            Memory::Write32(pos + 0, Memory::Read32(kAmbienceLeftX));
            Memory::Write32(pos + 4, Memory::Read32(kAmbienceY));
            Memory::Write32(pos + 8, Memory::Read32(kAmbienceLeftZ));
            game.Call<ShellSeReq>({shell, pos, 6, 0});
            Memory::Write32(pos + 0, Memory::Read32(kAmbienceRightX));
            Memory::Write32(pos + 4, Memory::Read32(kAmbienceY));
            Memory::Write32(pos + 8, Memory::Read32(kAmbienceRightZ));
            game.Call<ShellSeReq>({shell, pos, 6, 1});
        }

        const uint32_t ticks = GameStep::Ticks();
        uint32_t source = table + kSources;

        for (int32_t i = 0; i < int32_t(Memory::Read32(table + 4)); ++i, source += kSourceSize) {
            const uint8_t state = Memory::Read8(source + kState);

            if (state == 0)
                continue;

            if (Memory::Read8(source + kArea) != area) {
                Memory::Write8(source + kState, 1);
                Memory::Write16(source + kCountdown, 0);
                continue;
            }

            if (state == 2)
                continue;

            const int16_t countdown = int16_t(Memory::Read16(source + kCountdown));

            if (countdown > 0) {
                // The original: countdown - 1 per step.
                Memory::Write16(source + kCountdown, uint16_t(countdown - int16_t(std::min<uint32_t>(ticks, uint32_t(countdown)))));

                if (Memory::Read8(source + kSuppressed) != 0 || Memory::Read8(source + kFollows) != 1)
                    continue;

                const uint8_t index = Memory::Read8(source + kLoopHandle);
                const uint32_t seWork = Memory::Read32(kSeWork);
                uint32_t slot = 0;

                if (index != 255) {
                    slot = Memory::Read32(seWork + kSeLoopHandles + index * 4);
                    if (slot != 0 && Memory::Read8(slot) == 0)
                        slot = 0;
                }

                if (index == 255 || slot == 0 || Memory::Read32(slot + kSlotVoice) == 0 || game.Call<VoicePlaying>({Memory::Read32(slot + kSlotVoice)}) == 0) {
                    const uint8_t cur = Memory::Read8(source + kLoopHandle);
                    if (cur != 255)
                        Memory::Write32(seWork + kSeLoopHandles + cur * 4, 0);
                    Memory::Write8(source + kLoopHandle, 255);
                    continue;
                }

                const uint32_t listener = uint8_t(game.Call<OnlineMode>()) == 2 && player != 0 ? player + 0x3C : shell + kShellListener;
                game.Call<ClampToBox>({source + kBoxMin, source + kBoxMax, listener, pos});
                CopyWords(slot + kSlotPosition, pos, 3);
                CopyWords(slot + kSlotPrevPosition, slot + kSlotPosition, 3);
                continue;
            }

            Memory::Write16(source + kCountdown, uint16_t(Memory::Read32(source + kInterval)));

            if (Memory::Read8(source + kSuppressed) != 0)
                continue;

            if (uint8_t(game.Call<MapKind>({uint8_t(game.Call<GetNowMapNo>())})) == 22) { // arena
                const uint32_t id = Memory::Read32(source + kSoundId);

                if (game.Call<ArenaQuestA>() == 0 && id == 4)
                    continue;

                if (game.Call<ArenaMonsterAlive>({0}) == 0 && game.Call<ArenaMonsterAlive>({1}) == 0 && game.Call<ArenaMonsterAlive>({2}) == 0 && id == 5)
                    continue;

                if (game.Call<ArenaQuestB>() == 0 && id == 13)
                    continue;
            }

            const uint32_t slot = ShellAllocSlotInline(shell);

            if (slot) {
                Memory::Write8(slot + 1, 3);
                Memory::Write8(slot + 2, 2);
                Memory::Write32(slot + 40, Memory::Read32(shell + kShellId));
                Memory::Write32(slot + 44, Memory::Read32(source + kSoundId));
                Memory::Write32(slot + 48, 0xFFFFFFFF);
                Memory::Write32(slot + 52, 0xFFFFFFFF);
                Memory::Write32(slot + 56, 0);

                if (Memory::Read8(source + kFollows) == 0) {
                    CopyWords(slot + kSlotPosition, source + kPosition, 3);
                    CopyWords(slot + kSlotPrevPosition, slot + kSlotPosition, 3);
                } else {
                    game.Call<ClampToBox>({source + kBoxMin, source + kBoxMax, shell + kShellListener, pos});
                    CopyWords(slot + kSlotPosition, pos, 3);
                    CopyWords(slot + kSlotPrevPosition, slot + kSlotPosition, 3);
                    Memory::Write8(source + kLoopHandle, 255);
                    const uint32_t seWork = Memory::Read32(kSeWork);
                    uint8_t handle = 255;
                    for (uint8_t h = 0; h < 8; ++h) {
                        if (Memory::Read32(seWork + kSeLoopHandles + h * 4) == 0) {
                            handle = h;
                            break;
                        }
                    }

                    if (handle != 255) {
                        Memory::Write32(seWork + kSeLoopHandles + handle * 4, slot);
                        Memory::Write8(source + kLoopHandle, handle);
                    }
                }
            }

            if (Memory::Read32(source + kInterval) == 0)
                Memory::Write8(source + kState, 2);
        }
    }

    for (uint32_t i = 0; i < 32; ++i) {
        const uint32_t slot = shell + kShellSlots + i * 80;

        if (Memory::Read8(slot) != 0 && uint8_t(Memory::Read8(slot + 1) - 3) < 2) {
            game.Call<ShellSlotUpdate>({shell, slot});
            switch (Memory::Read8(slot + 2)) {
            case 1:
                game.Call<ShellSlotFinish1>({slot});
                break;
            case 2:
            case 3:
            case 4:
                game.Call<ShellSlotFinish2>({slot});
                break;
            }
        }
    }
}

extern "C" void func_802BB5A4(CpuContext* ctx);
extern "C" void MH3_MapSoundUpdate(CpuContext* ctx) {
    PortCheck::Call(ctx, "MapSoundUpdate", func_802BB5A4, MapSoundUpdatePort);
}
REGISTER_NATIVE_FUNCTION_AS(0x802BB5A4, MH3_MapSoundUpdate, "MapSoundUpdate");
