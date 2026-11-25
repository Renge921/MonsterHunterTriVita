#include "render_diag.h"
#include "character_creation.h"
#include "diag_log.h"
#include "memory.h"
#include "../native/mh3_model_resources.h"
#include "../native/mh3_model_state.h"
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

#ifdef MH3_DEVELOPMENT

namespace {
std::atomic<uint32_t> counters[RenderDiag::CounterCount];
std::atomic<uint32_t> frames{0};
std::atomic<uint32_t> reports{0};
std::atomic<uint32_t> notes{0};

constexpr uint32_t NodeIdentity = 0x808a0490;
constexpr uint32_t NodeCount = 0x808a0498;

constexpr const char* kNames[RenderDiag::CounterCount] = {
    "player_filtered", "visibility_rejected", "enqueued", "scene_drawn", "scene_skipped", "dispatch",     "dispatch_invisible", "dispatch_noshape",
    "dispatch_nosource", "dispatch_noframe",   "dispatch_recorded", "submit", "submit_empty", "submit_noframe", "submit_asset", "submit_culled",
    "submit_drawn", "submit_nouniforms", "submit_noview", "submit_nomaterial", "submit_noblocks",
};

uint32_t Take(RenderDiag::Counter counter) {
    return counters[counter].exchange(0, std::memory_order_relaxed);
}

constexpr uint32_t SceneRootSlot = 0x808A04CC;
constexpr uint32_t ScreenRecordSlot = 0x808A1394;
constexpr uint32_t ScreenCameraIds = 0x80769448;
constexpr uint32_t CameraState = 0x80869FE0;
constexpr size_t CameraStateBytes = 0x1A34;

// One CameraData record from the root's 32-slot array (+F8, stride 268).
void DumpCamera(uint32_t root, uint32_t index, const char* label) {
    if (!root || index >= 32) {
        MH3DiagLog("MH3 diag: %s idx=%u unavailable root=%08x\n", label, index, root);
        return;
    }

    const uint32_t address = root + 0xF8 + index * 268;
    mh3_native_camera camera{};
    const mh3_res_span span{Memory::GetPointer(address, 268), 268};

    if (mh3_camera_read(span, &camera) != MH3_RES_OK) {
        MH3DiagLog("MH3 diag: %s idx=%u addr=%08x read failed flags=%08x pos=%g,%g,%g target=%g,%g,%g fovy=%g near=%g far=%g\n", label, index, address,
                   Memory::Read32(address + 112), Memory::ReadFloat32(address + 116), Memory::ReadFloat32(address + 120), Memory::ReadFloat32(address + 124),
                   Memory::ReadFloat32(address + 140), Memory::ReadFloat32(address + 144), Memory::ReadFloat32(address + 148), Memory::ReadFloat32(address + 172),
                   Memory::ReadFloat32(address + 180), Memory::ReadFloat32(address + 184));
        return;
    }

    MH3DiagLog("MH3 diag: %s idx=%u addr=%08x flags=%08x type=%u pos=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f up=%.3f,%.3f,%.3f twist=%.1f fovy=%.3f aspect=%.3f near=%.3f far=%.3f viewport=%.1f,%.1f,%.1f,%.1f scissor=%u,%u,%u,%u\n", label,
               index, address, camera.flags, camera.projection_type, camera.position.v[0], camera.position.v[1], camera.position.v[2], camera.target.v[0],
               camera.target.v[1], camera.target.v[2], camera.up.v[0], camera.up.v[1], camera.up.v[2], camera.twist, camera.fovy, camera.aspect,
               camera.near_plane, camera.far_plane, camera.viewport[0], camera.viewport[1], camera.viewport[2], camera.viewport[3], camera.scissor[0],
               camera.scissor[1], camera.scissor[2], camera.scissor[3]);

    MH3DiagLog("MH3 diag: %s idx=%u view=%.4f,%.4f,%.4f,%.4f / %.4f,%.4f,%.4f,%.4f / %.4f,%.4f,%.4f,%.4f\n", label, index, camera.view.v[0][0],
               camera.view.v[0][1], camera.view.v[0][2], camera.view.v[0][3], camera.view.v[1][0], camera.view.v[1][1], camera.view.v[1][2],
               camera.view.v[1][3], camera.view.v[2][0], camera.view.v[2][1], camera.view.v[2][2], camera.view.v[2][3]);
}

void DumpPlayerRecord(uint32_t base, const char* label) {
    const uint32_t camwork = Memory::Read32(base + 1172);

    MH3DiagLog("MH3 diag: %s base=%08x p0=%.3f,%.3f,%.3f p12=%.3f,%.3f,%.3f eye=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f fovA=%.3f fovB=%.3f camwork=%08x mode=%u\n",
               label, base, Memory::ReadFloat32(base), Memory::ReadFloat32(base + 4), Memory::ReadFloat32(base + 8),
               Memory::ReadFloat32(base + 12), Memory::ReadFloat32(base + 16), Memory::ReadFloat32(base + 20),
               Memory::ReadFloat32(base + 76), Memory::ReadFloat32(base + 80), Memory::ReadFloat32(base + 84),
               Memory::ReadFloat32(base + 88), Memory::ReadFloat32(base + 92), Memory::ReadFloat32(base + 96),
               Memory::ReadFloat32(base + 52), Memory::ReadFloat32(base + 48), camwork, Memory::Read8(base + 1179));

    if (!camwork)
        return;

    MH3DiagLog("MH3 diag: %s camwork eye=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f fovA=%.3f fovB=%.3f\n", label,
               Memory::ReadFloat32(camwork), Memory::ReadFloat32(camwork + 4), Memory::ReadFloat32(camwork + 8),
               Memory::ReadFloat32(camwork + 12), Memory::ReadFloat32(camwork + 16), Memory::ReadFloat32(camwork + 20),
               Memory::ReadFloat32(camwork + 0x34), Memory::ReadFloat32(camwork + 0x30));
}

// The player work (PLW) the camera record points at: the u16 0x0101 marker
// while the player is in use, its position, and the MHchar it plays.
void DumpPlayerWork(uint32_t base, const char* label) {
    const uint32_t plw = Memory::Read32(base + 1172);

    if (!plw) {
        MH3DiagLog("MH3 diag: %s plw=0\n", label);
        return;
    }

    const uint32_t charp = Memory::Read32(plw + 0x13C);
    MH3DiagLog("MH3 diag: %s plw=%08x active=%04x b0=%u b1=%u state=%u steps=%d timer=%d pos=%.3f,%.3f,%.3f char=%08x motion=%u\n",
               label, plw, Memory::Read16(plw), Memory::Read8(plw), Memory::Read8(plw + 1), Memory::Read8(plw + 5),
               int32_t(Memory::Read32(plw + 0x20)), int32_t(Memory::Read32(plw + 0x28)),
               Memory::ReadFloat32(plw + 0x3C), Memory::ReadFloat32(plw + 0x40), Memory::ReadFloat32(plw + 0x44),
               charp, charp ? Memory::Read16(charp + 0x54) : 0);
}

void DumpCameraState() {
    const uint32_t selected = Memory::Read16(CameraState + 2);
    mh3_matrix34 view{};

    if (mh3_scene_view_matrix({Memory::GetPointer(CameraState, CameraStateBytes), CameraStateBytes}, &view) != MH3_RES_OK) {
        MH3DiagLog("MH3 diag: camera state selected=%u view read failed\n", selected);
        return;
    }

    MH3DiagLog("MH3 diag: camera state selected=%u view=%.4f,%.4f,%.4f,%.4f / %.4f,%.4f,%.4f,%.4f / %.4f,%.4f,%.4f,%.4f\n", selected, view.v[0][0],
               view.v[0][1], view.v[0][2], view.v[0][3], view.v[1][0], view.v[1][1], view.v[1][2], view.v[1][3], view.v[2][0], view.v[2][1],
               view.v[2][2], view.v[2][3]);
}

struct FogDiagState {
    uint32_t type = 0;
    float start = 0, end = 0, near_plane = 0, far_plane = 0;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool valid = false;
};

struct OverlayDiagEntry {
    char kind[24]{};
    uint32_t mode = 0;
    float w = 0, h = 0;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    uint32_t count = 0;
};

struct LightDiagState {
    uint32_t valid_mask = 0;
    uint32_t diff_color = 0;
    uint32_t spec_color = 0;
    uint8_t amb_r = 0, amb_g = 0, amb_b = 0, amb_a = 0;
    float light_colors[8][4]{};
    float light_dirs[8][3]{};
    bool valid = false;
};

struct BlendDrawEntry {
    uint32_t blend_mode = 0, src_factor = 0, dst_factor = 0;
    uint8_t depth_test = 0, textures = 0, lit = 0, model = 0;
    uint32_t sample_model_id = 0, sample_shape_id = 0;
    uint32_t draws = 0;
    uint64_t triangles = 0;
};

std::mutex diagExtraLock;
FogDiagState lastFog{};
LightDiagState lastLights{};
constexpr unsigned kOverlayLimit = 8;
OverlayDiagEntry overlays[kOverlayLimit]{};
unsigned overlayCount = 0;
constexpr unsigned kBlendLimit = 12;
BlendDrawEntry blendDraws[kBlendLimit]{};
unsigned blendDrawCount = 0;

void DumpSceneEffectsAndExtras() {
    constexpr uint32_t kEffects = 0x80753e18;
    const uint32_t skip = Memory::Read8(0x8075024c);
    const uint32_t paused = Memory::Read8(0x80768ad8);
    const uint32_t grab0 = Memory::Read8(0x80769310);
    const uint32_t grab1 = Memory::Read8(0x80769311);
    const uint32_t charFlag = Memory::Read8(0x80769317);
    const uint32_t mode = Memory::Read8(kEffects + 0x13c);
    const uint32_t capOff = Memory::Read8(kEffects + 0x11c);
    const uint32_t mask = Memory::Read8(kEffects + 2);
    const uint32_t rgba = Memory::Read32(kEffects + 0x13d);
    const uint32_t id = Memory::Read32(kEffects + 0x134);

    MH3DiagLog("MH3 diag: effects skip=%u paused=%u grab=%u/%u charf=%u mode=%u capOff=%u mask=%02x rgba=%08x id=%08x\n",
               skip, paused, grab0, grab1, charFlag, mode, capOff, mask, rgba, id);

    for (uint32_t slot = 1; slot < 6; ++slot) {
        const uint32_t stage = Memory::Read8(kEffects + 4 + slot);
        if (!stage)
            continue;
        const bool active = (mask & (1u << (stage & 31))) != 0;
        const uint32_t fn = Memory::Read32(kEffects + 12 + stage * 4);
        const uint32_t arg = Memory::Read32(kEffects + 36 + stage * 4);
        uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        if (arg && Memory::Contains(arg, 0x38)) {
            b0 = Memory::Read8(arg);
            b1 = Memory::Read8(arg + 1);
            b2 = Memory::Read8(arg + 2);
            b3 = Memory::Read8(arg + 3);
            b4 = Memory::Read8(arg + 4);
            b5 = Memory::Read8(arg + 5);
            b6 = Memory::Read8(arg + 6);
        }
        MH3DiagLog("MH3 diag: effect slot=%u stage=%u active=%d fn=%08x arg=%08x bytes=%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
                   slot, stage, active ? 1 : 0, fn, arg, b0, b1, b2, b3, b4, b5, b6);
        if (fn == 0x802939a0 && arg && Memory::Contains(arg, 0x38)) {
            MH3DiagLog("MH3 diag: glare id=%08x levels=%d start=%dx%d out=%dx%d shift=%u low=%u high=%u darken=%.3f brighten=%.3f\n",
                       Memory::Read32(arg + 8), int32_t(Memory::Read32(arg + 0x10)),
                       int16_t(Memory::Read16(arg + 0x1c)), int16_t(Memory::Read16(arg + 0x1e)),
                       int16_t(Memory::Read16(arg + 0x14)), int16_t(Memory::Read16(arg + 0x16)),
                       Memory::Read32(arg + 0x18), Memory::Read32(arg + 0x28), Memory::Read32(arg + 0x2c),
                       Memory::ReadFloat32(arg + 0x30), Memory::ReadFloat32(arg + 0x34));
        } else if (fn == 0x80294398 && arg && Memory::Contains(arg, 0x44)) {
            MH3DiagLog("MH3 diag: gradfade en=%u mode=%u mid=%d/%d c0=%08x c1=%08x c2=%08x c3=%08x c4=%08x c5=%08x\n",
                       Memory::Read8(arg), Memory::Read8(arg + 1),
                       int16_t(Memory::Read16(arg + 0x24)), int16_t(Memory::Read16(arg + 0x26)),
                       Memory::Read32(arg + 4), Memory::Read32(arg + 8), Memory::Read32(arg + 12),
                       Memory::Read32(arg + 16), Memory::Read32(arg + 20), Memory::Read32(arg + 24));
        }
    }

    std::lock_guard guard(diagExtraLock);
    if (lastFog.valid) {
        MH3DiagLog("MH3 diag: fog type=%u start=%.2f end=%.2f near=%.2f far=%.2f rgba=%u,%u,%u,%u\n",
                   lastFog.type, lastFog.start, lastFog.end, lastFog.near_plane, lastFog.far_plane,
                   lastFog.r, lastFog.g, lastFog.b, lastFog.a);
    }
    if (lastLights.valid) {
        MH3DiagLog("MH3 diag: lights mask=%02x diff=%08x spec=%08x amb=%u,%u,%u,%u\n",
                   lastLights.valid_mask, lastLights.diff_color, lastLights.spec_color,
                   lastLights.amb_r, lastLights.amb_g, lastLights.amb_b, lastLights.amb_a);
        for (unsigned i = 0; i < 8; ++i) {
            if (lastLights.valid_mask & (1u << i)) {
                MH3DiagLog("MH3 diag: light[%u] rgba=%.3f,%.3f,%.3f,%.3f dir=%.3f,%.3f,%.3f\n", i,
                           lastLights.light_colors[i][0], lastLights.light_colors[i][1], lastLights.light_colors[i][2], lastLights.light_colors[i][3],
                           lastLights.light_dirs[i][0], lastLights.light_dirs[i][1], lastLights.light_dirs[i][2]);
            }
        }
    }
    for (unsigned i = 0; i < overlayCount; ++i) {
        const auto& o = overlays[i];
        MH3DiagLog("MH3 diag: overlay kind=%s mode=%u size=%.1fx%.1f rgba=%u,%u,%u,%u count=%u\n",
                   o.kind, o.mode, o.w, o.h, o.r, o.g, o.b, o.a, o.count);
    }
    overlayCount = 0;
    for (unsigned i = 0; i < blendDrawCount; ++i) {
        const auto& b = blendDraws[i];
        MH3DiagLog("MH3 diag: blend bm=%u src=%u dst=%u z=%u tex=%u lit=%u mdl=%u sample=%u/%u draws=%u tris=%llu\n",
                   b.blend_mode, b.src_factor, b.dst_factor, b.depth_test, b.textures, b.lit, b.model,
                   b.sample_model_id, b.sample_shape_id, b.draws, static_cast<unsigned long long>(b.triangles));
    }
    blendDrawCount = 0;
}
} // namespace

void MH3DiagLog(const char* format, ...) {
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);

#ifdef __vita__
    // Through the file API, closing (and syncing) every line: stderr's writes
    // would stay in the card driver's cache while the game runs.
    static std::mutex logLock;
    std::lock_guard guard(logLock);
    const int fd = sceIoOpen("ux0:/data/mh3-wii/mh3.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);

    if (fd >= 0) {
        // Seek explicitly: some card drivers ignore O_APPEND, and a write at
        // offset 0 would overwrite the boot lines.
        sceIoLseek(fd, 0, SCE_SEEK_END);
        const int written = sceIoWrite(fd, text, std::strlen(text));
        sceIoClose(fd);

        if (written >= 0) {
            sceIoSync("ux0:", 0);
            return;
        }
    }
#endif

    std::fputs(text, stderr);
}

void RenderDiag::Count(Counter counter, uint32_t amount) {
    if (counter < CounterCount)
        counters[counter].fetch_add(amount, std::memory_order_relaxed);
}

bool RenderDiag::WantNote() {
    return notes.load(std::memory_order_relaxed) < 12;
}

void RenderDiag::NoteDispatch(uint32_t material_reference, uint32_t shape_reference, uint32_t shape, uint32_t model, const char* reason) {
    if (notes.fetch_add(1, std::memory_order_relaxed) >= 12)
        return;

    MH3DiagLog("MH3 diag: dispatch material_ref=%08x shape_ref=%08x shape=%08x model=%08x: %s\n", material_reference, shape_reference, shape, model,
               reason ? reason : "");
}

void RenderDiag::NoteFog(uint32_t type, float start, float end, float near_plane, float far_plane, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::lock_guard guard(diagExtraLock);
    lastFog = {type, start, end, near_plane, far_plane, r, g, b, a, true};
}

void RenderDiag::NoteLighting(uint32_t valid_mask, uint32_t diff_color, uint32_t spec_color, uint8_t amb_r, uint8_t amb_g, uint8_t amb_b, uint8_t amb_a,
                              const float light_colors[8][4], const float light_dirs[8][3]) {
    std::lock_guard guard(diagExtraLock);
    lastLights.valid_mask = valid_mask;
    lastLights.diff_color = diff_color;
    lastLights.spec_color = spec_color;
    lastLights.amb_r = amb_r;
    lastLights.amb_g = amb_g;
    lastLights.amb_b = amb_b;
    lastLights.amb_a = amb_a;
    for (unsigned i = 0; i < 8; ++i) {
        for (unsigned c = 0; c < 4; ++c)
            lastLights.light_colors[i][c] = light_colors[i][c];
        for (unsigned c = 0; c < 3; ++c)
            lastLights.light_dirs[i][c] = light_dirs[i][c];
    }
    lastLights.valid = true;
}

void RenderDiag::NoteBlendDraw(uint32_t blend_mode, uint32_t src_factor, uint32_t dst_factor, uint8_t depth_test, uint8_t textures, uint8_t lit, uint8_t model, uint32_t model_id, uint32_t shape_id, uint32_t triangles) {
    std::lock_guard guard(diagExtraLock);
    for (unsigned i = 0; i < blendDrawCount; ++i) {
        auto& b = blendDraws[i];
        if (b.blend_mode == blend_mode && b.src_factor == src_factor && b.dst_factor == dst_factor &&
            b.depth_test == depth_test && b.textures == textures && b.lit == lit && b.model == model) {
            ++b.draws;
            b.triangles += triangles;
            return;
        }
    }
    if (blendDrawCount >= kBlendLimit)
        return;
    blendDraws[blendDrawCount++] = {blend_mode, src_factor, dst_factor, depth_test, textures, lit, model, model_id, shape_id, 1, triangles};
}

void RenderDiag::NoteOverlay(const char* kind, uint32_t mode, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!kind)
        return;
    std::lock_guard guard(diagExtraLock);
    for (unsigned i = 0; i < overlayCount; ++i) {
        auto& o = overlays[i];
        if (std::strncmp(o.kind, kind, sizeof(o.kind)) == 0 && o.mode == mode && o.r == r && o.g == g && o.b == b && o.a == a) {
            ++o.count;
            return;
        }
    }
    if (overlayCount >= kOverlayLimit)
        return;
    auto& o = overlays[overlayCount++];
    std::snprintf(o.kind, sizeof(o.kind), "%s", kind);
    o.mode = mode;
    o.w = w;
    o.h = h;
    o.r = r;
    o.g = g;
    o.b = b;
    o.a = a;
    o.count = 1;
}

namespace {
// The models dispatch drew since the last report: which they are, how many
// shapes each covers, and whether they are skinned.
struct ModelCensus {
    uint32_t model, shapes, indices, skin;
};

std::mutex censusLock;
constexpr unsigned kCensusLimit = 8;
ModelCensus census[kCensusLimit];
unsigned censusCount = 0;

// One-shot probes, keyed by model id. Render thread only, but guarded like the
// census so a probe can never race a report.
struct ProbeSet {
    uintptr_t models[kCensusLimit];
    unsigned count;
};

std::mutex probeLock;
ProbeSet poseProbes, drawProbes, calcWorldProbes;

bool TakeProbe(ProbeSet& set, uintptr_t model) {
    std::lock_guard guard(probeLock);

    for (unsigned i = 0; i < set.count; ++i)
        if (set.models[i] == model)
            return false;

    if (set.count == kCensusLimit)
        return false;

    set.models[set.count++] = model;
    return true;
}
} // namespace

bool RenderDiag::WantPoseProbe(uintptr_t model) {
    return TakeProbe(poseProbes, model);
}

bool RenderDiag::WantDrawProbe(uintptr_t model) {
    static bool resetForField = false;
    if (!resetForField && Memory::Read8(0x80768ACC) == 1) {
        resetForField = true;
        std::lock_guard guard(probeLock);
        drawProbes.count = 0;
    }
    return TakeProbe(drawProbes, model);
}

bool RenderDiag::WantCalcWorldProbe(uint32_t mdl) {
    return TakeProbe(calcWorldProbes, mdl);
}

void RenderDiag::NoteModel(uint32_t model, uint32_t shape, uint32_t index_count, uint32_t skin_rows) {
    std::lock_guard guard(censusLock);

    for (unsigned i = 0; i < censusCount; ++i)
        if (census[i].model == model) {
            ++census[i].shapes;
            census[i].indices += index_count;
            census[i].skin = std::max(census[i].skin, skin_rows);
            return;
        }

    if (censusCount == kCensusLimit)
        return;

    census[censusCount++] = {model, 1, index_count, skin_rows};
}

void RenderDiag::Frame() {
    const uint32_t frame = frames.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool character = MH3CharacterCreationActive();

    // Report the first drawn scene, every entry into the creation screen, about
    // once a second while it is open, and a heartbeat otherwise so a log says
    // the game itself is drawing.
    static bool seen = false;
    static bool wasCharacter = false;
    const bool entered = character && !wasCharacter;
    const bool initial = !seen;
    seen = true;
    wasCharacter = character;

    if (!entered && !initial && !(character ? frame % 30 == 0 : frame % 300 == 0))
        return;

    const uint32_t root = Memory::Read32(SceneRootSlot);
    const uint32_t screen = Memory::Read32(ScreenRecordSlot);
    const uint32_t mode = root ? Memory::Read8(root + 244) : 0;
    const uint32_t screenCamera0 = Memory::Read8(ScreenCameraIds);
    const uint32_t screenCamera1 = Memory::Read8(ScreenCameraIds + 1);
    const uint32_t area = Memory::Read8(0x8078AE25);
    const uint32_t map = Memory::Read8(0x8078AE26);
    const uint32_t hud = Memory::Read32(0x806c0148);
    const uint32_t player = Memory::Read8(0x80768ACF);
    const uint32_t splitFlag = Memory::Read8(0x80769412);
    const uint32_t online = Memory::Read8(0x80768ACC);
    const uint32_t gameMode = Memory::Read8(0x80768ACC);
    const uint32_t playMode = Memory::Read8(0x80768ACD);

    char text[512];
    int used = std::snprintf(text, sizeof(text), "MH3 diag: charmake=%d frame=%u root=%08x screen=%08x mode=%u screencam=%u/%u player=%u split=%u online=%u gmode=%u pmode=%u area=%u map=%u hud=%08x sets=%u/%u", character ? 1 : 0,
                             frame, root, screen, mode, screenCamera0, screenCamera1, player, splitFlag, online, gameMode, playMode, area, map, hud, Memory::Read32(NodeCount), Memory::Read32(NodeCount + 4));

    for (unsigned i = 0; i < CounterCount && used > 0 && size_t(used) < sizeof(text); ++i) {
        const int add = std::snprintf(text + used, sizeof(text) - size_t(used), " %s=%u", kNames[i], Take(Counter(i)));

        if (add > 0)
            used += add;
    }

    if (used > 0 && size_t(used) < sizeof(text) - 1) {
        text[used++] = '\n';
        text[used] = '\0';
    }

    MH3DiagLog("%s", text);

    {
        std::lock_guard guard(censusLock);

        for (unsigned i = 0; i < censusCount; ++i) {
            const auto& entry = census[i];
            MH3DiagLog("MH3 diag: model=%08x shapes=%u indices=%u skin=%u\n", entry.model, entry.shapes, entry.indices, entry.skin);
        }

        censusCount = 0;
    }

    // The camera records actually used for the two views (the game's per-player
    // camera IDs), the mode index the root keeps, and the G3D state's selection.
    static uint32_t lastMode = 0xFFFFFFFFu, lastCamera0 = 0xFFFFFFFFu, lastCamera1 = 0xFFFFFFFFu;

    if (initial || mode != lastMode || screenCamera0 != lastCamera0 || screenCamera1 != lastCamera1) {
        lastMode = mode;
        lastCamera0 = screenCamera0;
        lastCamera1 = screenCamera1;
        DumpCamera(root, mode, "mode-camera");
        DumpCamera(root, screenCamera0, "player0-camera");
        DumpCamera(root, screenCamera1, "player1-camera");
        DumpCamera(root, 0, "camera0");
        DumpCameraState();
    }

    if (frame % 300 == 0) {
        DumpCamera(root, 0, "camera0-periodic");
        DumpCamera(root, mode, "mode-camera-periodic");
        DumpCamera(root, screenCamera0, "player0-camera-periodic");
        DumpCamera(root, screenCamera1, "player1-camera-periodic");
        DumpPlayerRecord(0x80780008, "player0-record");
        DumpPlayerRecord(0x80780500, "player1-record");
        DumpPlayerWork(0x80780008, "player0-work");
        DumpPlayerWork(0x80780500, "player1-work");
        DumpCameraState();
        DumpSceneEffectsAndExtras();
    }

    // The scene table once the walk has nodes: whether the character's own
    // nodes are in it, and which flags the walk filters them by.
    if (character && (Memory::Read32(NodeCount) || Memory::Read32(NodeCount + 4)) && reports.fetch_add(1, std::memory_order_relaxed) < 3) {
        unsigned listed = 0;

        for (unsigned set = 0; set < 2; ++set) {
            const uint32_t first = Memory::Read32(NodeIdentity + set * 4);
            const uint32_t count = Memory::Read32(NodeCount + set * 4);

            for (uint32_t i = 0; i < count && i < 32 && listed < 40; ++i) {
                const uint32_t identity = first + i * 52;
                ++listed;

                MH3DiagLog("MH3 diag: node set=%u i=%u id=%08x flags=%04x type=%u category=%u object=%08x progress=%u\n", set, i, identity, Memory::Read16(identity + 30),
                           Memory::Read8(identity + 28), Memory::Read8(identity + 29), Memory::Read32(identity + 24), Memory::Read8(identity + 9));
            }
        }
    }
}
#endif // MH3_DEVELOPMENT
