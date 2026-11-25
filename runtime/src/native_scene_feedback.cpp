// Thin guest ABI adapters for typed scene feedback plans. GPU targets retain
// snapshots; guest texture addresses are identities, never readback buffers.
#include "abi_bridge.h"
#include "fatal.h"
#include "guest_call.h"
#include "native_frame_packet.h"
#include "native_graphics.h"
#include "memory.h"
#include "render_diag.h"
#include "../native/mh3_scene_feedback.h"
#include "../native/adapters/model_state_bridge.h"
#include "../native/adapters/ui_bridge.h"
#include <aurora/native_scene_feedback.hpp>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <array>
#include <vector>
#include <utility>

namespace {
using namespace MemoryInline;
const void *Identity(uint32_t address) {
    if (!address)
        return nullptr;

    return Memory::GetPointer(address, 1);
}

void Require(int success, const char *reason) {
    if (!success)
        MH3_THROW(std::runtime_error(reason));
}

bool Capture(const mh3_scene_capture_plan &plan) {
    aurora::SceneCaptureParameters parameters;
    parameters.width = plan.width;
    parameters.height = plan.height;
    parameters.format = plan.format;
    parameters.box_filter = plan.box_filter;

    for (uint32_t i = 0; i < 4; i++)
        parameters.source[i] = plan.source[i];

    RecordNativeSceneCapture(Identity(plan.identity), parameters);
    return true;
}

void State(const mh3_scene_quad_plan &plan, bool bind = true) {
    SetNativeProjection(plan.projection, false);
    SetNativePositionMatrix(plan.position);
    SetNativeMaterial(plan.material);
    SetNativeRaster(plan.raster);
    mh3_native_texture_binding binding{};
    binding.source.resource_address = binding.source.payload_address = plan.identity;
    binding.source.width = plan.width;
    binding.source.height = plan.height;
    binding.source.format = plan.format;
    binding.source.levels = 1;
    binding.source.mutable_source = 1;
    binding.source.generation = NativeSceneFeedbackVersion(Identity(plan.identity), plan.width, plan.height);
    if (!binding.source.generation)
        binding.source.generation = 1;
    binding.map_id = 0;
    binding.sampler.min_filter = binding.sampler.mag_filter = 1;

    if (!bind || !NativeGraphicsFrameShouldRender())
        return;

    BindNativeTexture(binding);
}

void Emit(const mh3_scene_quad_plan &plan) {
    State(plan);

    if (!NativeGraphicsFrameShouldRender())
        return;

    AuroraNativeMaterialGeometry geometry{};

    if (!AllocateNativeMaterialGeometry(4, 6, geometry))
        return;

    for (uint32_t i = 0; i < 4; i++) {
        auto &v = geometry.vertices[i];
        std::memset(&v, 0, sizeof(v));
        AuroraNativeVertex prepared{};
        mh3_draw_vertex(&GetNativeDrawState(), plan.vertices[i], plan.uv[i], plan.color, &prepared);
        std::memcpy(v.position, prepared.position, 16);
        std::memcpy(v.view_position, plan.vertices[i], 12);
        v.normal[0] = 1;
        std::memcpy(v.color[0], plan.color, 4);
        std::memcpy(v.color[1], plan.color, 4);

        for (uint32_t map = 0; map < 8; map++) {
            v.texcoord[map][0] = plan.uv[i][0];
            v.texcoord[map][1] = plan.uv[i][1];
            v.texcoord[map][2] = 1;
        }
    }

    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    std::memcpy(geometry.indices, indices, 12);
    SubmitNativeMaterialGeometry(geometry);
}

mh3_temporal_feedback_input TemporalInput(uint32_t self) {
    mh3_temporal_feedback_input input{};
    input.identity = FlatRead32(self + 0x10);
    input.frames = FlatRead32(self + 0x34);
    input.screen_width = FlatRead16(0x807693f8);
    input.screen_height = FlatRead16(0x807693fa);
    input.projection_height = FlatReadFloat32(0x80769400);
    input.offset[0] = FlatReadFloat32(self + 8);
    input.offset[1] = FlatReadFloat32(self + 12);
    input.enabled = FlatRead8(self);
    input.mode = FlatRead8(self + 1);
    input.alpha = FlatRead8(self + 2);
    input.amount = FlatRead8(self + 3);
    input.threshold_mode = FlatRead8(self + 4);
    input.threshold_alpha = FlatRead8(self + 5);
    input.threshold_gray = FlatRead8(self + 6);
    input.subtract_scale = uint8_t(FlatRead32(0x808a03b0));

    for (uint32_t i = 0; i < 3; i++)
        input.tint[i] = FlatRead8(self + 0x14 + i);

    return input;
}

// Draws a captured scene back over the frame. `restores` marks the glare's
// draw-back of the capture it has just taken: the original restores the EFB
// its bright pass used as scratch space, but the native bright pass draws into
// its own targets, so the frame is still what was captured. Drawing it back
// unchanged (untinted, replacing) would only cost a full-screen draw and
// lower the frame to the capture's 640x448; its state still applies.
void SceneDrawBack(CpuContext *cpu, uint32_t self, int subtract, bool restores = false) {
    uint32_t id = FlatRead32(self + 0x134);

    if (!id)
        return;

    GuestCall call{cpu};
    bool paused = FlatRead8(0x80768ad8) != 0;
    mh3_scene_quad_plan plan{};
    Require(mh3_scene_capture_quad(id, FlatReadFloat32(self + 0x120), FlatReadFloat32(self + 0x124), FlatReadFloat32(self + 0x128), FlatReadFloat32(self + 0x12c), paused ? 120 : 255, subtract,
                                   &GetNativeDrawState(), &GetNativeMaterialState(), &plan),
            "Invalid captured scene quad");

    if (restores && !paused && !subtract) {
        State(plan);
        return;
    }

    RenderDiag::NoteOverlay(subtract ? "DrawBackSub" : "DrawBack", paused ? 1 : 0,
                            FlatReadFloat32(self + 0x128), FlatReadFloat32(self + 0x12c),
                            plan.color[0], plan.color[1], plan.color[2], plan.color[3]);
    Emit(plan);
}

void SceneCapture(uint32_t self, uint32_t conditional) {
    uint32_t id = FlatRead32(self + 0x134), mode = FlatRead8(self + 0x13c);

    if (!id || (conditional && mode != 0 && mode != 2))
        return;

    float w = FlatReadFloat32(self + 0x120), h = FlatReadFloat32(self + 0x124);

    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > 65535 || h > 65535)
        MH3_THROW(std::runtime_error("Invalid scene capture dimensions"));

    mh3_scene_capture_plan plan{id, uint16_t(int32_t(w)), uint16_t(int32_t(h)), 6, {0, 0, w, h}, 0};
    Capture(plan);
}

void GlareBrightPass(uint32_t self) {
    mh3_scene_glare_input input{};
    input.identity = FlatRead32(self + 8);
    input.levels = int32_t(FlatRead32(self + 0x10));
    input.start_width = int16_t(FlatRead16(self + 0x1c));
    input.start_height = int16_t(FlatRead16(self + 0x1e));
    input.shift = FlatRead32(self + 0x18);
    input.low_threshold = FlatRead32(self + 0x28);
    input.high_threshold = FlatRead32(self + 0x2c);
    input.darken = FlatReadFloat32(self + 0x30);
    input.brighten = FlatReadFloat32(self + 0x34);
    input.projection_width = FlatReadFloat32(0x80753f38);
    input.projection_height = FlatReadFloat32(0x80753f3c);
    mh3_scene_glare_plan plan;
    Require(mh3_scene_glare_prepare(&input, &plan), "Invalid glare resource dimensions or threshold");
    /* Authored result dimensions are CPU state even during a skipped draw. */
    Memory::Write16(self + 0x14, plan.width);
    Memory::Write16(self + 0x16, plan.height);
    aurora::SceneGlareParameters parameters;
    parameters.width = plan.width;
    parameters.height = plan.height;
    parameters.low_threshold = plan.low_threshold;
    parameters.high_threshold = plan.high_threshold;
    parameters.darken = plan.darken;
    parameters.brighten = plan.brighten;
    parameters.shift_scale = plan.shift_scale;

    for (uint32_t i = 0; i < plan.level_count; i++) {
        const auto &level = plan.levels[i];
        parameters.levels.push_back({level.source_width, level.source_height, level.width, level.height});
    }

    RecordNativeSceneGlare(Identity(plan.identity), parameters);

    if (plan.level_count) {
        mh3_scene_quad_plan state;
        Require(mh3_scene_capture_quad(input.identity, plan.width, plan.height, input.projection_width, input.projection_height, 255, 0, &GetNativeDrawState(), &GetNativeMaterialState(), &state),
                "Invalid glare inherited quad state");
        State(state);
    }
}

uint32_t CpuRead(void *, uint32_t address, unsigned bytes) {
    if (bytes == 1)
        return FlatRead8(address);

    if (bytes == 2)
        return FlatRead16(address);

    return FlatRead32(address);
}

void CpuWrite(void *, uint32_t address, uint32_t value, unsigned bytes) {
    if (bytes == 1)
        FlatWrite8(address, uint8_t(value));
    else if (bytes == 2)
        Memory::Write16(address, uint16_t(value));
    else
        FlatWrite32(address, value);
}

uint32_t CpuCall(void *context, uint32_t fn, const uint32_t *args, unsigned count) {
    auto cpu = static_cast<CpuContext *>(context);

    if (count > 8)
        MH3_THROW(std::runtime_error("Scene CPU call exceeds guest integer ABI"));

    for (unsigned i = 0; i < count; i++)
        cpu->gpr[3 + i] = args[i];
    // Authored menu printf/sprintf calls clear CR bit6: no floating varargs.
    if (fn == 0x80295348 || fn == 0x8045b5a4)
        cpu->cr &= ~0x02000000u;

    InvokeIndirectCpu(fn, cpu);
    return cpu->gpr[3];
}

float CpuFloatCall(void *context, uint32_t fn, const uint32_t *args, unsigned count, const float *values, unsigned floats) {
    auto cpu = static_cast<CpuContext *>(context);

    if (count > 8 || floats > 8)
        MH3_THROW(std::runtime_error("Scene CPU float call exceeds guest ABI"));

    for (unsigned i = 0; i < count; i++)
        cpu->gpr[3 + i] = args[i];

    for (unsigned i = 0; i < floats; i++)
        cpu->fpr[1 + i].d = values[i];

    InvokeIndirectCpu(fn, cpu);
    return float(cpu->fpr[1].d);
}

// Resource copies and fills as the original makes them: through memcpy
// (0x80004000) and memset (0x80004350), whose native ports record the copied
// bytes' source and retire what a fill overwrites (model_catalog.cpp,
// native_resource_lifetime.cpp). A host copy lost that: equipping other
// armour moved its part model into the hunter's part buffer with no source,
// and its draw stopped ("Model has no complete native loader provenance").
void CpuCopy(void *context, uint32_t destination, uint32_t source, uint32_t bytes) {
    GuestCall{static_cast<CpuContext *>(context)}.Call<0x80004338>({destination, source, bytes});
}

void CpuZero(void *context, uint32_t destination, uint32_t bytes) {
    GuestCall{static_cast<CpuContext *>(context)}.Call<0x8000443C>({destination, 0, bytes});
}

void CpuStringCopy(void *, uint32_t destination, uint32_t source) {
    unsigned length = 0;

    while (length < 320 && FlatRead8(source + length))
        length++;

    if (length == 320)
        MH3_THROW(std::runtime_error("Equipment resource name exceeds authored scratch buffer"));

    // Into the caller's stack scratch, as strcpy: no resource bytes.
    std::memcpy(Memory::GetPointer(destination, length + 1), Memory::GetPointer(source, length + 1), length + 1);
}

int CpuStringCompare(void *, uint32_t first, uint32_t second) {
    for (uint32_t i = 0; i < 320; i++) {
        const uint8_t a = FlatRead8(first + i), b = FlatRead8(second + i);

        if (a != b || !a)
            return int(a) - int(b);
    }

    MH3_THROW(std::runtime_error("Equipment material name lacks terminator"));
}

void CpuTextureObject(void *, uint32_t object, uint32_t payload) {
    mh3_native_texture_binding binding{};
    binding.source.resource_address = binding.source.payload_address = payload;
    binding.source.width = binding.source.height = 128;
    binding.source.format = 6;
    binding.source.levels = 1;
    binding.source.mutable_source = 1;
    binding.source.generation = ReadNativeTextureGeneration(payload);
    if (!binding.source.generation)
        binding.source.generation = 1;
    binding.sampler.min_filter = binding.sampler.mag_filter = 1;
    binding.sampler.edge_lod = 1;
    StoreNativeTextureObject(object, binding);
}

struct SourceSample {
    bool active = false, covered = false;
    float point[2]{}, rgba[4]{};
} sourceSample;

struct CapturedMenuQuad {
    AuroraNativeVertex vertices[4];
    mh3_draw_state state;
    mh3_native_texture_binding binding;
    bool has_palette = false;
    mh3_native_palette_source palette{};
};

bool menuQuadCaptureActive = false;
std::vector<CapturedMenuQuad> capturedMenuQuads;

void RecordMenuQuads(const AuroraNativeVertex *vertices, uint32_t count, const mh3_draw_state &state, const mh3_native_texture_binding &binding, const mh3_native_palette_source *palette) {
    if (!menuQuadCaptureActive || count % 4 || count > 64)
        return;

    for (uint32_t i = 0; i < count; i += 4) {
        CapturedMenuQuad quad{};
        std::memcpy(quad.vertices, vertices + i, sizeof(quad.vertices));
        quad.state = state;
        quad.binding = binding;
        if (palette) {
            quad.has_palette = true;
            quad.palette = *palette;
        }
        capturedMenuQuads.push_back(quad);
    }
}

void ObserveVertices(const AuroraNativeVertex *vertices, uint32_t count, const mh3_draw_state &state, const mh3_native_texture_binding &binding, const mh3_native_palette_source *palette) {
    if (!sourceSample.active)
        return;

    if (count % 4 || count > 64)
        MH3_THROW(std::runtime_error("Color selector requires ordinary sprite quads"));

    if (binding.sampler.lod_bias != 0 || binding.source.levels > 1)
        MH3_THROW(std::runtime_error("Color selector source requires an explicit mip sampling rule"));

    const size_t bytes = aurora::native_texture_source_size(binding.source.format, binding.source.width, binding.source.height, 1);
    mh3_res_span pixels{Memory::GetPointer(binding.source.payload_address, bytes), bytes}, colors{};

    if (palette)
        colors = {Memory::GetPointer(palette->payload_address, size_t(palette->entries) * 2), size_t(palette->entries) * 2};

    for (uint32_t i = 0; i < count; i += 4) {
        float sampled[4];
        const int result = mh3_scene_source_quad(pixels, colors, palette ? palette->format : 0, &binding, vertices + i, &state, sourceSample.point, sampled);

        if (result < 0)
            MH3_THROW(std::runtime_error("Invalid color selector authored texture sample"));

        if (!result)
            continue;

        if (state.raster.blend_mode == 1 && state.raster.src_factor == 4 && state.raster.dst_factor == 5) {
            for (unsigned k = 0; k < 3; k++)
                sourceSample.rgba[k] = sampled[k] * sampled[3] + sourceSample.rgba[k] * (1 - sampled[3]);

            sourceSample.rgba[3] = sampled[3] + sourceSample.rgba[3] * (1 - sampled[3]);
        } else if ((state.raster.blend_mode == 0) || (state.raster.blend_mode == 1 && state.raster.src_factor == 1 && state.raster.dst_factor == 0)) {
            std::memcpy(sourceSample.rgba, sampled, 16);
        } else
            MH3_THROW(std::runtime_error("Color selector source uses an unsupported composition blend"));

        sourceSample.covered = true;
    }
}

void ObserveSource(const mh3_ui_stream &stream, const mh3_draw_state &state, const mh3_native_texture_binding &binding, const mh3_native_palette_source *palette) {
    if (!sourceSample.active && !menuQuadCaptureActive)
        return;

    if (stream.count % 4 || stream.count > 64)
        MH3_THROW(std::runtime_error("Color selector requires ordinary sprite quads"));

    std::array<AuroraNativeVertex, 64> vertices{};
    std::array<uint16_t, 96> indices{};
    AuroraNativeGeometry geometry{vertices.data(), indices.data(), stream.count, stream.count / 4 * 6, 0, 0};
    Require(mh3_ui_build_stream(&state, MH3_UI_QUADS, &stream, 0, &geometry) == MH3_RES_OK, "Invalid color selector CPU geometry");
    if (menuQuadCaptureActive)
        RecordMenuQuads(vertices.data(), stream.count, state, binding, palette);
    if (sourceSample.active)
        ObserveVertices(vertices.data(), stream.count, state, binding, palette);
}

struct MenuQuadCaptureScope {
    MenuQuadCaptureScope() {
        capturedMenuQuads.clear();
        menuQuadCaptureActive = true;
        SetNativeUISourceObserver(ObserveSource);
    }
    ~MenuQuadCaptureScope() {
        SetNativeUISourceObserver(nullptr);
        menuQuadCaptureActive = false;
        sourceSample.active = false;
        capturedMenuQuads.clear();
    }
};

uint32_t SampleMenuQuadsAtEfb(uint32_t efb_x, uint32_t efb_y) {
    sourceSample = {};
    sourceSample.active = true;
    sourceSample.point[0] = float(efb_x & 0xFFFFu) + .5f;
    sourceSample.point[1] = float(efb_y & 0xFFFFu) + .5f;

    for (const auto &quad : capturedMenuQuads)
        ObserveVertices(quad.vertices, 4, quad.state, quad.binding, quad.has_palette ? &quad.palette : nullptr);

    sourceSample.active = false;
    Require(sourceSample.covered && sourceSample.rgba[3] >= .999f, "Color selector point lacks an opaque authored sprite source");
    uint8_t rgba[4]{};
    for (unsigned k = 0; k < 4; k++)
        rgba[k] = uint8_t(std::lround(std::fmin(std::fmax(sourceSample.rgba[k], 0.0f), 1.0f) * 255.0f));
    return (0xFFu << 24) | (uint32_t(rgba[0]) << 16) | (uint32_t(rgba[1]) << 8) | uint32_t(rgba[2]);
}

void CpuBeginSample(void *, int16_t x, int16_t y) {
    sourceSample = {};
    sourceSample.active = true;
    const float width = FlatReadFloat32(0x807693fc), height = FlatReadFloat32(0x80769400);
    Require(width > 0 && height > 0, "Invalid color selector screen dimensions");
    sourceSample.point[0] = std::floor(float(x) * FlatRead16(0x807693f8) / width) + .5f;
    sourceSample.point[1] = std::floor(float(y) * FlatRead16(0x807693fa) / height) + .5f;
    SetNativeUISourceObserver(ObserveSource);
}

int CpuSample(void *, int16_t, int16_t, uint8_t rgba[4]) {
    SetNativeUISourceObserver(nullptr);
    sourceSample.active = false;

    if (!sourceSample.covered || sourceSample.rgba[3] < .999f)
        MH3_THROW(std::runtime_error("Color selector point lacks an opaque authored sprite source"));

    for (unsigned k = 0; k < 4; k++)
        rgba[k] = uint8_t(std::lround(std::fmin(std::fmax(sourceSample.rgba[k], 0), 1) * 255));

    return 1;
}

struct SourceSampleReset {
    ~SourceSampleReset() {
        SetNativeUISourceObserver(nullptr);
        sourceSample.active = false;
    }
};

mh3_scene_cpu_ops CpuOps(CpuContext *cpu) {
    return {cpu, CpuRead, CpuWrite, CpuCall, CpuCopy, CpuZero, CpuStringCopy, CpuStringCompare, CpuTextureObject, CpuBeginSample, CpuSample, CpuFloatCall};
}
} // namespace

#ifdef MH3_DEVELOPMENT
// The MH3 feedback temporal line (graphics_host.cpp): enabled calls, those
// that drew the previous frame's image back, and the last call's settings.
TemporalFeedbackStatistics temporalStatistics;

TemporalFeedbackStatistics TakeTemporalFeedbackStatistics() {
    return std::exchange(temporalStatistics, {});
}
#endif

extern "C" void MH3_NativeFeedbackThreshold(CpuContext *cpu);
extern "C" void MH3_NativeTemporalFeedback(CpuContext *cpu) {
    uint32_t self = cpu->gpr[3];
    auto input = TemporalInput(self);
    mh3_temporal_feedback_plan plan;
    Require(mh3_scene_temporal_prepare(&input, &GetNativeDrawState(), &GetNativeMaterialState(), &plan), "Invalid temporal feedback resource");

    if (!plan.enabled)
        return;
#ifdef MH3_DEVELOPMENT
    ++temporalStatistics.calls;
    temporalStatistics.previous += plan.draw_previous;
    temporalStatistics.mode = input.mode;
    temporalStatistics.alpha = input.alpha;
    temporalStatistics.amount = input.amount;
#endif

    FlatWrite32(self + 0x34, plan.frames);
    const bool history = NativeSceneFeedbackVersion(Identity(plan.capture.identity), plan.capture.width, plan.capture.height) != 0;
    /* CPU counters still advance across skipped frames. The first admitted
     * capture initializes GPU history before any previous-image draw exists. */
    State(plan.initial, false);

    if (plan.frames != 1)
        State(plan.previous, history);

    if (plan.draw_previous && history) {
        RenderDiag::NoteOverlay("TempPrev", input.mode, float(input.screen_width), input.projection_height,
                                input.tint[0], input.tint[1], input.tint[2], input.alpha);
        Emit(plan.previous);
    }

    Capture(plan.capture);
    State(plan.frames == 1 ? plan.initial : plan.previous, false);

    if (plan.draw_threshold) {
        RenderDiag::NoteOverlay("TempThresh", input.threshold_mode, float(input.screen_width), input.projection_height,
                                input.threshold_gray, input.threshold_gray, input.threshold_gray, input.threshold_alpha);
        Emit(plan.threshold);
    } else
        MH3_NativeFeedbackThreshold(cpu);
}

extern "C" void MH3_NativeFeedbackThreshold(CpuContext *cpu) {
    auto input = TemporalInput(cpu->gpr[3]);
    input.enabled = 1;

    // The plan's threshold quad draws only with a threshold mode; without
    // one only the history image is bound, and no plan is built.
    if (input.threshold_mode) {
        mh3_temporal_feedback_plan plan;
        Require(mh3_scene_temporal_prepare(&input, &GetNativeDrawState(), &GetNativeMaterialState(), &plan), "Invalid feedback threshold resource");
        Emit(plan.threshold);
    } else {
        Require(mh3_scene_temporal_valid(&input), "Invalid feedback threshold resource");
        /* The original binds the history image before testing the mode. */
        mh3_native_texture_binding binding{};
        binding.source.resource_address = binding.source.payload_address = input.identity;
        binding.source.width = input.screen_width / 2;
        binding.source.height = input.screen_height / 2;
        binding.source.format = 4;
        binding.source.levels = 1;
        binding.source.mutable_source = 1;
        binding.source.generation = NativeSceneFeedbackVersion(Identity(input.identity), binding.source.width, binding.source.height);
        if (!binding.source.generation)
            binding.source.generation = 1;
        binding.sampler.min_filter = binding.sampler.mag_filter = 1;

        if (NativeGraphicsFrameShouldRender()) {
            BindNativeTexture(binding);
        }
    }
}

extern "C" void MH3_NativeSceneCapture(CpuContext *cpu) {
    SceneCapture(cpu->gpr[3], cpu->gpr[4]);
}

extern "C" void MH3_NativeSceneDrawBack(CpuContext *cpu) {
    GuestFrame frame(cpu, 0x10);
    SceneDrawBack(cpu, frame.Arg(3), uint8_t(frame.Arg(4)) == 1);
}

extern "C" void MH3_NativeSavedSceneDraw(CpuContext *cpu) {
    uint32_t id = FlatRead32(0x80753f50);

    if (!id)
        return;

    mh3_scene_quad_plan plan;
    Require(mh3_scene_capture_quad(id, FlatReadFloat32(0x80753f38), FlatReadFloat32(0x80753f3c), FlatReadFloat32(0x80753f40), FlatReadFloat32(0x80753f44), uint8_t(cpu->gpr[3]) ? 200 : 255, 0,
                                   &GetNativeDrawState(), &GetNativeMaterialState(), &plan),
            "Invalid saved scene quad");
    RenderDiag::NoteOverlay("SavedScene", uint8_t(cpu->gpr[3]),
                            FlatReadFloat32(0x80753f40), FlatReadFloat32(0x80753f44),
                            plan.color[0], plan.color[1], plan.color[2], plan.color[3]);
    Emit(plan);
}

extern "C" void MH3_NativeGlareBrightPass(CpuContext *cpu) {
    GlareBrightPass(cpu->gpr[3]);
}

extern "C" void MH3_NativeGlareDraw(CpuContext *cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    uint32_t self = frame.Arg(3);

    const bool captures = FlatRead8(0x80768ad8) == 0 && FlatRead8(0x80753f34);

    if (captures)
        SceneCapture(0x80753e18, 0);

    if (!FlatRead8(self))
        return;

    GlareBrightPass(self);
    SceneDrawBack(cpu, 0x80753e18, 0, captures);
    mh3_scene_quad_plan plan;
    Require(mh3_scene_capture_quad(FlatRead32(self + 8), FlatRead16(self + 0x14), FlatRead16(self + 0x16), FlatReadFloat32(0x80753f40), FlatReadFloat32(0x80753f44), 255, 0, &GetNativeDrawState(),
                                   &GetNativeMaterialState(), &plan),
            "Invalid glare composite quad");
    for (uint32_t i = 0; i < 3; i++)
        plan.color[i] = FlatRead8(self + 2 + i);

    plan.raster.src_factor = FlatRead8(self + 5);
    plan.raster.dst_factor = FlatRead8(self + 6);
    RenderDiag::NoteOverlay("GlareComp", (uint32_t(plan.raster.src_factor) << 8) | plan.raster.dst_factor,
                            float(FlatRead16(self + 0x14)), float(FlatRead16(self + 0x16)),
                            plan.color[0], plan.color[1], plan.color[2], plan.color[3]);
    Emit(plan);
}

extern "C" void MH3_EffectTexCoordGenerator(CpuContext *cpu);
extern "C" void MH3_NativeSceneTexgen(CpuContext *cpu) {
    cpu->gpr[7] = 0;
    cpu->gpr[8] = 125;
    MH3_EffectTexCoordGenerator(cpu);
}

extern "C" void MH3_NativeSavedSceneCapture(CpuContext *) {
    const uint32_t id = FlatRead32(0x80753f50);

    if (!id)
        return;

    const float w = FlatReadFloat32(0x80753f38), h = FlatReadFloat32(0x80753f3c);

    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > 65535 || h > 65535)
        MH3_THROW(std::runtime_error("Invalid saved capture dimensions"));

    Capture({id, uint16_t(int32_t(w)), uint16_t(int32_t(h)), 6, {0, 0, w, h}, 0});
}

extern "C" void MH3_NativeGrabScreen(CpuContext *cpu) {
    GuestFrame frame(cpu, 0x20);
    GuestCall call{cpu};

    if (FlatRead8(0x80768ad8) || FlatRead8(0x80768aad))
        return;

    call.Call<0x80033134>({0x80753f40});
    call.Call<0x8037C1A0>();
    call.Call<0x802926C0>({0x80753e18});
    call.Call<0x80292738>({0x80753e18});

    if (FlatRead8(0x80769310) != 1)
        return;

    const uint32_t player = (FlatRead32(0x80768b4c) ? FlatRead32(FlatRead32(0x80768b4c) + 16) : 0);

    if (player && FlatRead8(player + 0x112))
        return;

    if (call.Call<0x80294B78>({0}) == 2)
        return;

    // Overlapped, logic runs while the draw thread still records the previous
    // packet (the scene this grab copies): the clone joins the next packet.
    if (NativeGraphicsFrameShouldRender()) {
        const void *destination = Identity(FlatRead32(0x80753f50)), *source = Identity(FlatRead32(0x80753f4c));

        if (NativeGraphicsLogicPhase())
            QueueNativeSceneClone(destination, source);
        else
            Require(RecordNativeSceneClone(destination, source), "GrabScreen source was not captured");
    }

    FlatWrite8(0x80769310, 2);
}

extern "C" void MH3_NativeSceneFeedbackDispatch(CpuContext *cpu) {
    const uint32_t self = cpu->gpr[3];
    const uint8_t mode = FlatRead8(self + 0x13c);

    if (mode == 0) {
        if (FlatRead8(self + 0x11c) == 0) {
            SceneCapture(self, 0);
        }
    } else if (mode == 2) {
        SceneCapture(self, 1);
        MH3_DebugDrawSetup(cpu);

        GuestCall call{cpu};
        GuestFrame frame(cpu, 0x40);
        const uint32_t sp = cpu->gpr[1];

        Memory::WriteFloat32(sp + 0x20, 0.0f);
        Memory::WriteFloat32(sp + 0x24, 0.0f);
        Memory::WriteFloat32(sp + 0x28, 0.0f);
        Memory::WriteFloat32(sp + 0x18, FlatReadFloat32(self + 0x128));
        Memory::WriteFloat32(sp + 0x1c, FlatReadFloat32(self + 0x12c));
        Memory::Write8(sp + 0x10, FlatRead8(self + 0x13d));
        Memory::Write8(sp + 0x11, FlatRead8(self + 0x13e));
        Memory::Write8(sp + 0x12, FlatRead8(self + 0x13f));
        Memory::Write8(sp + 0x13, 0xff);
        call.Call<0x802C9268>({sp + 0x20, sp + 0x18, sp + 0x10});

        SceneDrawBack(cpu, self, 1);

        const uint8_t alpha = uint8_t(0xff - FlatRead8(self + 0x140));
        Memory::Write8(sp + 0x0c, FlatRead8(self + 0x13d));
        Memory::Write8(sp + 0x0d, FlatRead8(self + 0x13e));
        Memory::Write8(sp + 0x0e, FlatRead8(self + 0x13f));
        Memory::Write8(sp + 0x0f, alpha);
        call.Call<0x802C9268>({sp + 0x20, sp + 0x18, sp + 0x0c});

        SceneCapture(self, 0);
    } else {
        MH3_DebugDrawSetup(cpu);

        GuestCall call{cpu};
        GuestFrame frame(cpu, 0x40);
        const uint32_t sp = cpu->gpr[1];

        Memory::WriteFloat32(sp + 0x20, 0.0f);
        Memory::WriteFloat32(sp + 0x24, 0.0f);
        Memory::WriteFloat32(sp + 0x28, 0.0f);
        Memory::WriteFloat32(sp + 0x18, FlatReadFloat32(self + 0x128));
        Memory::WriteFloat32(sp + 0x1c, FlatReadFloat32(self + 0x12c));
        Memory::Write32(sp + 0x08, FlatRead32(self + 0x13d));
        call.Call<0x802C9268>({sp + 0x20, sp + 0x18, sp + 0x08});

        SceneCapture(self, 0);
    }
}

extern "C" void MH3_NativeSceneEffects(CpuContext *cpu) {
    GuestFrame frame(cpu, 0x20);
    GuestCall call{cpu};

    if (uint8_t(FlatRead8(0x80768acc)) == 2 && FlatRead8(0x8075024c) == 1)
        return;

    MH3_DebugDrawSetup(cpu);

    if (FlatRead8(0x80768ad8) == 1) {
        SceneDrawBack(cpu, 0x80753e18, 0);

        for (uint32_t slot = 1; slot < 6; slot++)
            if (FlatRead8(0x80753e1c + slot) == 2 && (FlatRead8(0x80753e1a) & 4)) {
                const uint32_t fn = FlatRead32(0x80753e2c);

                if (fn)
                    call(fn, {FlatRead32(0x80753e44)});
            }
    } else if (!FlatRead8(0x80769311) || FlatRead8(0x80769310) != 2) {
        cpu->gpr[3] = 0x80753e18;
        MH3_NativeSceneFeedbackDispatch(cpu);

        for (uint32_t slot = 1; slot < 6; slot++) {
            const uint32_t stage = FlatRead8(0x80753e1c + slot);

            if (stage && (uint32_t(FlatRead8(0x80753e1a)) & (1u << (stage & 31)))) {
                const uint32_t fn = FlatRead32(0x80753e24 + stage * 4);

                if (fn)
                    call(fn, {FlatRead32(0x80753e3c + stage * 4)});
            }
        }
    }

    call.Call<0x8052C0C0>({0x7ff});
    auto material = GetNativeMaterialState();
    material.indirect_count = 0;
    SetNativeMaterial(material);
}

extern "C" void func_80238714(CpuContext *ctx);
extern "C" void func_80239040(CpuContext *ctx);
extern "C" void func_80B22E1C(CpuContext *ctx);

extern "C" void MH3_GXPeekARGB(CpuContext *cpu) {
    const uint32_t x = cpu->gpr[3] & 0xFFFFu;
    const uint32_t y = cpu->gpr[4] & 0xFFFFu;
    const uint32_t out = cpu->gpr[5];
    const uint32_t argb = menuQuadCaptureActive ? SampleMenuQuadsAtEfb(x, y) : 0xFF000000u;
    Memory::Write32(out, argb);
}

extern "C" void MH3_NativeInnerColorMenu(CpuContext *cpu) {
    MenuQuadCaptureScope scope;
    func_80238714(cpu);
}

extern "C" void MH3_NativeEquipmentColorMenu(CpuContext *cpu) {
    MenuQuadCaptureScope scope;
    func_80239040(cpu);
}

extern "C" void MH3_NativeDemoSetup(CpuContext *cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    MH3_DebugDrawSetup(cpu);
    mh3_scene_quad_plan plan;
    Require(mh3_scene_demo_setup(FlatReadFloat32(0x807693fc), FlatReadFloat32(0x80769400), &GetNativeDrawState(), &GetNativeMaterialState(), &plan), "Invalid demo projection");
    State(plan, false);
}

extern "C" void MH3_NativeDemoQuad(CpuContext *cpu) {
    const auto binding = ReadNativeTextureObject(cpu->gpr[3]);
    mh3_scene_quad_plan plan{};
    std::memcpy(plan.projection, GetNativeDrawState().projection, 64);
    std::memcpy(plan.position, GetNativeDrawState().position, 48);
    plan.material = GetNativeMaterialState();
    plan.raster = GetNativeDrawState().raster;
    plan.identity = binding.source.payload_address;
    plan.width = binding.source.width;
    plan.height = binding.source.height;
    plan.format = binding.source.format;
    const float uv0[2] = {FlatReadFloat32(cpu->gpr[8]), FlatReadFloat32(cpu->gpr[8] + 4)}, uv1[2] = {FlatReadFloat32(cpu->gpr[9]), FlatReadFloat32(cpu->gpr[9] + 4)};
    const uint32_t rgbaWord = cpu->gpr[10];
    const uint8_t rgba[4] = {uint8_t(rgbaWord >> 24), uint8_t(rgbaWord >> 16), uint8_t(rgbaWord >> 8), uint8_t(rgbaWord)};
    mh3_scene_demo_quad(&plan, int16_t(cpu->gpr[4]), int16_t(cpu->gpr[5]), int16_t(cpu->gpr[6]), int16_t(cpu->gpr[7]), uv0, uv1, rgba, FlatRead8(cpu->gpr[1] + 11) != 0);
    /* Preserve authored object sampler, rather than feedback's fixed clamp. */
    SetNativeRaster(plan.raster);

    if (sourceSample.active || menuQuadCaptureActive) {
        AuroraNativeVertex vertices[4];

        for (unsigned i = 0; i < 4; i++)
            mh3_draw_vertex(&GetNativeDrawState(), plan.vertices[i], plan.uv[i], rgba, &vertices[i]);

        const auto *pal = binding.source.indexed ? GetNativePaletteBinding(binding.tlut_id) : nullptr;
        if (menuQuadCaptureActive)
            RecordMenuQuads(vertices, 4, GetNativeDrawState(), binding, pal);
        if (sourceSample.active)
            ObserveVertices(vertices, 4, GetNativeDrawState(), binding, pal);
    }

    if (!NativeGraphicsFrameShouldRender())
        return;

    BindNativeTexture(binding);
    AuroraNativeMaterialGeometry geometry{};

    if (!AllocateNativeMaterialGeometry(4, 6, geometry))
        return;

    for (unsigned i = 0; i < 4; i++) {
        auto &v = geometry.vertices[i];
        std::memset(&v, 0, sizeof(v));
        AuroraNativeVertex p{};
        mh3_draw_vertex(&GetNativeDrawState(), plan.vertices[i], plan.uv[i], rgba, &p);
        std::memcpy(v.position, p.position, 16);
        std::memcpy(v.view_position, plan.vertices[i], 12);
        v.normal[0] = 1;

        for (unsigned c = 0; c < 2; c++)
            std::memcpy(v.color[c], rgba, 4);

        for (unsigned t = 0; t < 8; t++) {
            v.texcoord[t][0] = plan.uv[i][0];
            v.texcoord[t][1] = plan.uv[i][1];
            v.texcoord[t][2] = 1;
        }
    }

    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    std::memcpy(geometry.indices, indices, 12);
    SubmitNativeMaterialGeometry(geometry);
}

extern "C" void MH3_NativeDemoCharacterMenu(CpuContext *cpu) {
    MenuQuadCaptureScope scope;
    func_80B22E1C(cpu);
}
REGISTER_NATIVE_FUNCTION(0x8044EF1C, MH3_GXPeekARGB);
REGISTER_NATIVE_FUNCTION(0x80292B00, MH3_NativeTemporalFeedback);
REGISTER_NATIVE_FUNCTION(0x802930DC, MH3_NativeFeedbackThreshold);
REGISTER_NATIVE_FUNCTION(0x80293D00, MH3_NativeGlareBrightPass);
REGISTER_NATIVE_FUNCTION(0x80291A0C, MH3_NativeSceneCapture);
REGISTER_NATIVE_FUNCTION(0x80291B0C, MH3_NativeSceneDrawBack);
REGISTER_NATIVE_FUNCTION(0x80291E5C, MH3_NativeSavedSceneDraw);
REGISTER_NATIVE_FUNCTION(0x802921A0, MH3_NativeSavedSceneCapture);
REGISTER_NATIVE_FUNCTION(0x80291940, MH3_NativeGrabScreen);
REGISTER_NATIVE_FUNCTION(0x8029227C, MH3_NativeSceneFeedbackDispatch);
REGISTER_NATIVE_FUNCTION(0x8029278C, MH3_NativeSceneEffects);
REGISTER_NATIVE_FUNCTION_AS(0x80238714, MH3_NativeInnerColorMenu, "NativeInnerColorMenu");
REGISTER_NATIVE_FUNCTION_AS(0x80239040, MH3_NativeEquipmentColorMenu, "NativeEquipmentColorMenu");
REGISTER_NATIVE_FUNCTION(0x80B200BC, MH3_NativeDemoSetup);
REGISTER_NATIVE_FUNCTION(0x80B27FAC, MH3_NativeSceneTexgen);
REGISTER_NATIVE_FUNCTION(0x80B20268, MH3_NativeDemoQuad);
REGISTER_NATIVE_FUNCTION_AS(0x80B22E1C, MH3_NativeDemoCharacterMenu, "NativeDemoCharacterMenu");
