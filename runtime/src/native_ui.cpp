// RMHP08 application UI paths, reconstructed from their Ghidra C listings.
// This is the guest ABI boundary; typed C writes native attributes directly.
#include "abi_bridge.h"
#include "fatal.h"
#include "guest_call.h"
#include "memory.h"
#include "native_frame_packet.h"
#include "native_graphics.h"
#include "native_shade_snapshot.h"
#include "port_check.h"
#include "render_diag.h"
#include "../native/mh3_ui.h"
#include "../native/mh3_system_ui.h"
#include "../native/adapters/model_state_bridge.h"
#include "../native/adapters/ui_bridge.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint32_t Shape = 0x80753AB0, ScreenWidth = 0x807693FC, ScreenHeight = 0x80769400;
uint32_t uiFlags = 0;
bool uiTextured = false;
uint32_t uiStages = 1;
struct TextureCoordinates {
    uint32_t type = 1, source = 4, matrix = 60;
} uiCoordinates;

uint64_t textureProducerGeneration = 0;
NativeUISourceObserver sourceObserver = nullptr;
uint64_t NextGeneration(uint32_t payload) {
    textureProducerGeneration = std::max(textureProducerGeneration, ReadNativeTextureGeneration(payload)) + 1;

    if (!textureProducerGeneration)
        MH3_THROW(std::runtime_error("Native UI producer generation overflow"));

    return textureProducerGeneration;
}

mh3_native_texture_binding ResourceBinding(uint32_t reference) {
    mh3_native_texture_binding binding{};
    binding.source = ReadNativeResTexture(reference);
    binding.sampler.min_filter = binding.source.levels > 1 ? 5 : 1;
    binding.sampler.mag_filter = 1;
    binding.sampler.edge_lod = 1;
    return binding;
}

void Checked(mh3_res_status status) {
    if (status != MH3_RES_OK)
        MH3_THROW(std::runtime_error("Native UI input failed: " + std::to_string(status)));
}

mh3_res_span Span(uint32_t address, size_t size) {
    return {Memory::GetPointer(address, size), size};
}

void ReadPosition(uint32_t address, float out[3]) {
    for (unsigned i = 0; i < 3; ++i)
        out[i] = Memory::ReadFloat32(address + i * 4);
}

void ReadColor(uint32_t address, uint8_t out[4]) {
    for (unsigned i = 0; i < 4; ++i)
        out[i] = Memory::Read8(address + i);
}

void Identity() {
    float matrix[12];
    mh3_draw_identity_position(matrix);
    SetNativePositionMatrix(matrix);
}

void Ortho(float top, float bottom, float left, float right, float nearPlane, float farPlane) {
    float matrix[16];

    if (!mh3_draw_ortho(matrix, top, bottom, left, right, nearPlane, farPlane))
        MH3_THROW(std::runtime_error("Native UI has a degenerate orthographic projection"));

    SetNativeProjection(matrix, false);
}

void ScreenOrtho(float nearPlane = 0, float farPlane = 1) {
    Ortho(0, Memory::ReadFloat32(ScreenHeight), 0, Memory::ReadFloat32(ScreenWidth), nearPlane, farPlane);
}

void RasterQuad(bool blend, bool depthTest) {
    auto raster = GetNativeDrawState().raster;

    if (blend) {
        raster.blend_mode = 1;
        raster.src_factor = 4;
        raster.dst_factor = 5;
        raster.logic_op = 0;
    }

    raster.color_write = 1;
    raster.cull_mode = 0;
    raster.depth_test = depthTest;
    raster.depth_compare = 7;
    raster.depth_write = 0;
    SetNativeRaster(raster);
    SetNativeDepthCompareBeforeTexture(false);
}

void Invalidate(GuestCall& call, uint32_t fields = 0x7FF) {
    call(0x8052C0C0, {fields});
}

void RestoreWorld(GuestCall& call) {
    uiStages = 1;
    call(0x802AF3B0);
    Invalidate(call);
}

bool Allocate(mh3_ui_primitive primitive, uint32_t count, AuroraNativeGeometry& geometry) {
    uint32_t vertices, indices;
    Checked(mh3_ui_geometry_counts(primitive, count, &vertices, &indices));
    return indices && AllocateNativeGeometry(vertices, indices, geometry);
}

void DrawVertices(mh3_ui_primitive primitive, const mh3_ui_vertex* vertices, uint32_t count, bool textured, float width = 1) {
    if (!NativeGraphicsFrameShouldRender())
        return;

    if (uiStages != 1)
        MH3_THROW(std::runtime_error("Native UI draw requires exactly one color stage"));

    if (textured != uiTextured)
        MH3_THROW(std::runtime_error("Native UI draw disagrees with its color pipeline"));

    AuroraNativeGeometry geometry{};

    if (!Allocate(primitive, count, geometry))
        return;

    Checked(mh3_ui_build_vertices(&GetNativeDrawState(), primitive, vertices, count, width, &geometry));
    SubmitNativeGeometry(geometry, textured);
}

// What this file last published as the material, and the material
// revision and raster right after: while both are unchanged, publishing the
// same again changes nothing.
struct PublishedMaterial {
    uint32_t revision = UINT32_MAX, key = UINT32_MAX;
    mh3_raster_state raster{};
};

PublishedMaterial uiMaterial, uiCoordinatesMaterial;

bool MaterialCurrent(const PublishedMaterial& published, uint32_t key) {
    return published.key == key && published.revision == nativeStateRevision[NativeOwnerIndex(NativeOwner::Game)][NATIVE_STATE_MATERIAL] &&
           !std::memcmp(&published.raster, &GetNativeDrawState().raster, sizeof published.raster);
}

void Remember(PublishedMaterial& published, uint32_t key) {
    published = {nativeStateRevision[NativeOwnerIndex(NativeOwner::Game)][NATIVE_STATE_MATERIAL], key, GetNativeDrawState().raster};
}

// The primitive material: one colour channel from the vertex colours, no
// alpha test, the application pipeline (with a texture stage when textured).
void PublishPrimitiveMaterial(bool textured) {
    auto material = GetNativeMaterialState();
    material.channel_count = 1;
    material.channel[0].color = material.channel[0].alpha = (mh3_pack_channel_control){1, 1, 0, 0, 2, 0};
    material.channel[0].flags |= MH3_CHANNEL_CONTROL_COLOR | MH3_CHANNEL_CONTROL_ALPHA;

    for (unsigned field = MH3_PIXEL_ALPHA_COMPARE0; field <= MH3_PIXEL_ALPHA_OPERATION; ++field) {
        material.pixel.known |= UINT64_C(1) << field;
        material.pixel.values[field] = field == MH3_PIXEL_ALPHA_COMPARE0 || field == MH3_PIXEL_ALPHA_COMPARE1 ? 7 : field == MH3_PIXEL_ALPHA_OPERATION ? 1 : 0;
    }

    mh3_system_ui_application_pipeline(&material, textured);

    if (textured) {
        const uint32_t stage = 0;
        mh3_system_ui_direct_stages(&material, &stage, 1);
    }

    SetNativeMaterial(material);
    Remember(uiMaterial, textured);
}

// A textured primitive's texture is bound with the wrap modes given (clamp by
// default).
void SetupPrimitive(GuestCall& call, uint32_t flags, uint32_t texture, uint32_t palette, const float* overrideMatrix = nullptr, uint32_t wrapS = 0, uint32_t wrapT = 0) {
    uiFlags = flags;

    if (flags & 1) {
        call(0x802AF3B0);
        GuestFrame scratch(call.cpu, 0x40);
        call(0x80375DB4, {scratch[0x10]}); // get_current_view_mtx, pure CPU copy
        float matrix[12];

        for (unsigned i = 0; i < 12; ++i)
            matrix[i] = Memory::ReadFloat32(scratch[0x10] + i * 4);

        SetNativePositionMatrix(matrix);
    } else {
        ScreenOrtho(-10000, 10000);

        if (overrideMatrix)
            SetNativePositionMatrix(overrideMatrix);
        else
            Identity();
    }

    auto raster = GetNativeDrawState().raster;
    raster.cull_mode = 0;
    SetNativeRaster(raster);
    SetNativeDepthCompareBeforeTexture(false);
    const bool textured = (flags & 2) != 0;

    // Consecutive shapes: when neither the material nor the raster changed
    // since this code published the material below, the state holds it
    // already (copying out and comparing the 1.3 KiB state cost the field HUD
    // about a tenth of its draw).
    if (!MaterialCurrent(uiMaterial, textured))
        PublishPrimitiveMaterial(textured);

    uiTextured = textured;
    uiStages = 1;

    if (uiTextured) {

        uiCoordinates = {1, 4, 60};
        auto binding = ResourceBinding(texture);
        binding.map_id = 0;
        binding.sampler.wrap_s = wrapS;
        binding.sampler.wrap_t = wrapT;

        if (binding.source.indexed) {
            auto colors = ReadNativeResPalette(palette);
            BindNativePalette(0, colors);
        }

        StoreNativeTextureObject(Shape + 0xF8, binding);
        BindNativeTexture(binding);
    }
}

void ConfigureDirectCoordinates(uint32_t id, uint32_t type, uint32_t source, uint32_t matrix) {
    if (id != 0 || source != 4 || (type != 0 && type != 1) || (matrix != 60 && matrix != 54))
        MH3_THROW(std::runtime_error("Native UI texture coordinates require an unsupported generator"));

    uiCoordinates = {type, source, matrix};

    // As SetupPrimitive's material: unchanged since published, nothing to do.
    if (MaterialCurrent(uiCoordinatesMaterial, type))
        return;

    auto material = GetNativeMaterialState();
    material.texgen[0].known |= 1u << MH3_TEXGEN_PROJECTION;
    material.texgen[0].values[MH3_TEXGEN_PROJECTION] = type == 0;
    SetNativeMaterial(material);
    Remember(uiCoordinatesMaterial, type);
}

mh3_ui_stream ShapeStream(uint32_t flags, uint32_t count) {
    mh3_ui_stream stream{};
    stream.count = count;
    stream.position_format = flags & 1 ? MH3_UI_POSITION_FLOAT_3 : flags & 0x10000 ? MH3_UI_POSITION_FLOAT_2 : MH3_UI_POSITION_I16_2;
    stream.positions = Span(Memory::Read32(Shape + 8), size_t(count) * (flags & 1 ? 12 : flags & 0x10000 ? 8 : 4));
    stream.color_stride = flags & 0x20000 ? 0 : 4;
    stream.colors = Span(Memory::Read32(Shape + 12), count ? size_t(count - 1) * stream.color_stride + 4 : 0);
    stream.uv_scale[0] = stream.uv_scale[1] = 1;

    if (flags & 2) {
        stream.uv_format = flags & 0x40000 ? MH3_UI_UV_I16 : MH3_UI_UV_FLOAT;
        stream.uvs = Span(Memory::Read32(Shape + 16), size_t(count) * (flags & 0x40000 ? 4 : 8));

        if (flags & 0x40000) {
            const auto binding = ResourceBinding(Shape + 0xE0);

            if (!binding.source.width || !binding.source.height)
                MH3_THROW(std::runtime_error("Native shape texture has zero dimensions"));

            stream.uv_scale[0] = 1.f / binding.source.width;
            stream.uv_scale[1] = 1.f / binding.source.height;
        }

        if (flags & 8) {
            stream.transform_uv = 1;

            for (unsigned i = 0; i < 12; ++i)
                stream.uv_matrix[i] = Memory::ReadFloat32(Shape + 0xA4 + i * 4);
        }
    }

    return stream;
}

void ObserveShape(uint32_t flags, uint32_t count) {
    if (!sourceObserver || !(flags & 2) || !count)
        return;

    if ((flags & 1) || Memory::Read8(Shape + 4) != MH3_UI_QUADS)
        MH3_THROW(std::runtime_error("Native source observer requires application 2D quads"));

    const auto stream = ShapeStream(flags, count);
    auto state = GetNativeDrawState();

    if (!mh3_draw_ortho(state.projection, 0, Memory::ReadFloat32(ScreenHeight), 0, Memory::ReadFloat32(ScreenWidth), -10000, 10000))
        MH3_THROW(std::runtime_error("Native source observer has invalid screen dimensions"));

    state.perspective = 0;

    if (flags & 4)
        for (unsigned i = 0; i < 12; ++i)
            state.position[i] = Memory::ReadFloat32(Shape + 0x74 + i * 4);
    else
        mh3_draw_identity_position(state.position);

    auto binding = ResourceBinding(Shape + 0xE0);
    binding.sampler.wrap_s = Memory::Read32(Shape + 0xD4);
    binding.sampler.wrap_t = Memory::Read32(Shape + 0xD8);
    mh3_native_palette_source palette{};
    const mh3_native_palette_source* colors = nullptr;

    if (binding.source.indexed) {
        palette = ReadNativeResPalette(Shape + 0xE4);
        colors = &palette;
    }

    sourceObserver(stream, state, binding, colors);
}

void DrawBeta(CpuContext* cpu, bool textured) {
    GuestFrame frame(cpu, 0x20);
    GuestCall call{cpu};
    const auto position = frame.Arg(3), size = frame.Arg(4), color = frame.Arg(5), texture = frame.Arg(6);

    if (!NativeGraphicsFrameShouldRender())
        return;

    if (textured)
        ScreenOrtho();
    else {
        call(0x80033134, {frame[0x10]});
        Ortho(0, Memory::ReadFloat32(frame[0x14]), 0, Memory::ReadFloat32(frame[0x10]), 0, 1);
    }

    Identity();
    RasterQuad(true, true);
    uiStages = 1;
    uiTextured = textured;

    if (textured)
        BindNativeTexture(ReadNativeTextureObject(texture));

    float origin[3];
    ReadPosition(position, origin);
    uint8_t rgba[4];
    ReadColor(color, rgba);
    const float w = Memory::ReadFloat32(size), h = Memory::ReadFloat32(size + 4);
    if (w >= 300.0f && h >= 200.0f)
        RenderDiag::NoteOverlay(textured ? "DrawBetaTex" : "DrawBeta", 0, w, h, rgba[0], rgba[1], rgba[2], rgba[3]);
    mh3_ui_vertex vertices[4];
    mh3_ui_rect(vertices, origin[0], origin[1], origin[0] + w, origin[1] + h, origin[2], rgba, 0, 0, 1, 1);
    DrawVertices(MH3_UI_QUADS, vertices, 4, textured);
}
} // namespace

void SetNativeUISourceObserver(NativeUISourceObserver observer) {
    sourceObserver = observer;
}

extern "C" void MH3_Setup2DProjection(CpuContext*) {
    Ortho(0, 480, 0, 640, 0, 1);
    Identity();
}
REGISTER_NATIVE_FUNCTION(0x802A16DC, MH3_Setup2DProjection);
extern "C" void MH3_UIScreenProjection(CpuContext*) {
    ScreenOrtho(-10000, 10000);
}
// REGISTER_NATIVE_FUNCTION(0x80052770, MH3_UIScreenProjection); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_DrawBeta(CpuContext* cpu) {
    DrawBeta(cpu, false);
}
REGISTER_NATIVE_FUNCTION(0x802C9268, MH3_DrawBeta);
extern "C" void MH3_DrawBetaTexture(CpuContext* cpu) {
    DrawBeta(cpu, true);
}
REGISTER_NATIVE_FUNCTION(0x80034020, MH3_DrawBetaTexture);
extern "C" void MH3_DebugDrawSetup(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto mode = Memory::Read32(frame.Arg(13) - 0x4738);
    const float width = Memory::Read16(mode + 4) * 0.913460016f, height = Memory::Read16(mode + 6);
    Invalidate(call, 4);
    Ortho(-height * 0.5f, height * 0.5f, -width * 0.5f, width * 0.5f, -10, 10);
    Identity();
    auto raster = GetNativeDrawState().raster;
    raster.blend_mode = 1;
    raster.src_factor = 4;
    raster.dst_factor = 5;
    raster.logic_op = 5;
    raster.alpha_write = 0;
    raster.depth_test = 0;
    raster.depth_compare = 7;
    raster.depth_write = 0;
    raster.cull_mode = 2;
    SetNativeRaster(raster);
    uiTextured = false;
    uiStages = 1;
    auto material = GetNativeMaterialState();
    mh3_system_ui_fade_material(&material);
    material.cull_mode = 2;
    const uint32_t stage = 0;
    mh3_system_ui_direct_stages(&material, &stage, 1);
    SetNativeMaterial(material);
    mh3_native_fog fog{};
    SetNativeFog(fog);
}
REGISTER_NATIVE_FUNCTION(0x80034340, MH3_DebugDrawSetup);
extern "C" void MH3_UITextureBind(CpuContext* cpu) {
    auto binding = ResourceBinding(cpu->gpr[3]);
    binding.map_id = cpu->gpr[5];
    binding.tlut_id = cpu->gpr[5];

    if (binding.map_id >= 8)
        MH3_THROW(std::runtime_error("Native UI texture slot is out of range"));

    binding.sampler.wrap_s = cpu->gpr[6];
    binding.sampler.wrap_t = cpu->gpr[7];

    if (binding.source.indexed) {
        if (!cpu->gpr[4] || !Memory::Read32(cpu->gpr[4]))
            return; // original optional indexed palette path
        BindNativePalette(binding.tlut_id, ReadNativeResPalette(cpu->gpr[4]));
    }

    StoreNativeTextureObject(Shape + 0xF8 + binding.map_id * 32, binding);
    BindNativeTexture(binding);
}
// REGISTER_NATIVE_FUNCTION(0x80052C90, MH3_UITextureBind); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_UIColorPipeline(CpuContext*) {
    uiTextured = false;
    uiStages = 1;
    auto material = GetNativeMaterialState();
    mh3_system_ui_application_pipeline(&material, 0);
    SetNativeMaterial(material);
}
// REGISTER_NATIVE_FUNCTION(0x80052F68, MH3_UIColorPipeline); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_UITexturePipeline(CpuContext* cpu) {
    const auto texture = cpu->gpr[3], palette = cpu->gpr[4];
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    cpu->gpr[3] = texture;
    cpu->gpr[4] = palette;
    cpu->gpr[5] = 0;
    cpu->gpr[6] = 0;
    cpu->gpr[7] = 0;
    MH3_UITextureBind(cpu);
    uiTextured = true;
    uiStages = 1;
    ConfigureDirectCoordinates(0, 1, 4, 60);
    auto material = GetNativeMaterialState();
    mh3_system_ui_application_pipeline(&material, 1);
    const uint32_t stage = 0;
    mh3_system_ui_direct_stages(&material, &stage, 1);
    SetNativeMaterial(material);
}
// REGISTER_NATIVE_FUNCTION(0x80052FB4, MH3_UITexturePipeline); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_UITextureCoordinates(CpuContext* cpu) {
    ConfigureDirectCoordinates(cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6]);
}
// REGISTER_NATIVE_FUNCTION(0x80053028, MH3_UITextureCoordinates); // Inlined/Unused in JPN (RMHJ08)
// REGISTER_NATIVE_FUNCTION(0x8005AF74, MH3_UITextureCoordinates); // Inlined/Unused in JPN (RMHJ08)
// REGISTER_NATIVE_FUNCTION(0x8005C564, MH3_UITextureCoordinates); // Inlined/Unused in JPN (RMHJ08)
// REGISTER_NATIVE_FUNCTION(0x80048D3C, MH3_UITextureCoordinates); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_UIStageCount(CpuContext* cpu) {
    const uint32_t count = cpu->gpr[3] & 255;

    if (!count || count > 16)
        MH3_THROW(std::runtime_error("Native UI stage count is outside material storage"));

    uint32_t stages[16];

    for (unsigned i = 0; i < count; ++i) {
        stages[i] = Memory::Read32(0x8056fc80 + i * 4);

        if (stages[i] >= 16)
            MH3_THROW(std::runtime_error("Native UI direct stage table index invalid"));
    }

    uiStages = count;
    auto material = GetNativeMaterialState();
    mh3_system_ui_direct_stages(&material, stages, count);
    SetNativeMaterial(material);
}
// REGISTER_NATIVE_FUNCTION(0x80053034, MH3_UIStageCount); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_UIVertexFormat(CpuContext* cpu) {
    uiFlags = cpu->gpr[3];
}
// REGISTER_NATIVE_FUNCTION(0x800530D0, MH3_UIVertexFormat); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_UIPrimitiveSetup(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    SetupPrimitive(call, frame.Arg(3) & 255, frame.Arg(4), frame.Arg(5));
}
REGISTER_NATIVE_FUNCTION(0x8028D938, MH3_UIPrimitiveSetup);
extern "C" void MH3_UIPrimitiveFinish(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    RestoreWorld(call);
}
// REGISTER_NATIVE_FUNCTION(0x800533D4, MH3_UIPrimitiveFinish); // Inlined/Unused in JPN (RMHJ08)
extern "C" void MH3_DrawLine2D(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};

    if (NativeGraphicsFrameShouldRender()) {
        SetupPrimitive(call, 0, 0, 0);
        mh3_ui_vertex vertices[2]{};

        for (unsigned i = 0; i < 2; ++i) {
            const uint32_t point = frame.Arg(3 + i);
            vertices[i].position[0] = int16_t(Memory::Read16(point));
            vertices[i].position[1] = int16_t(Memory::Read16(point + 2));
            ReadColor(point + 4, vertices[i].color);
        }

        DrawVertices(MH3_UI_LINES, vertices, 2, false, 1);
    }

    RestoreWorld(call);
}
REGISTER_NATIVE_FUNCTION(0x8028DE2C, MH3_DrawLine2D);
extern "C" void MH3_DrawRect2D(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};

    if (NativeGraphicsFrameShouldRender()) {
        SetupPrimitive(call, 0, 0, 0);
        const auto record = frame.Arg(3);
        uint8_t rgba[4];
        ReadColor(record + 8, rgba);
        const int16_t x0 = int16_t(Memory::Read16(record)), y0 = int16_t(Memory::Read16(record + 2));
        const int16_t x1 = int16_t(Memory::Read16(record + 4)), y1 = int16_t(Memory::Read16(record + 6));
        if (std::abs(x1 - x0) >= 300 && std::abs(y1 - y0) >= 200)
            RenderDiag::NoteOverlay("Rect2D", 0, float(std::abs(x1 - x0)), float(std::abs(y1 - y0)), rgba[0], rgba[1], rgba[2], rgba[3]);
        mh3_ui_vertex vertices[4];
        mh3_ui_rect(vertices, x0, y0, x1, y1, 0, rgba, 0, 0, 0, 0);
        DrawVertices(MH3_UI_QUADS, vertices, 4, false);
    }

    RestoreWorld(call);
}
REGISTER_NATIVE_FUNCTION(0x8028DED4, MH3_DrawRect2D);
extern "C" void MH3_DrawRect2DTexture(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};

    if (NativeGraphicsFrameShouldRender()) {
        const auto record = frame.Arg(3), texture = frame.Arg(4), palette = frame.Arg(5);
        SetupPrimitive(call, 2, texture, palette);
        const auto binding = ResourceBinding(texture);

        if (!binding.source.width || !binding.source.height)
            MH3_THROW(std::runtime_error("Native UI texture has zero dimensions"));

        const float du = 1.f / binding.source.width, dv = 1.f / binding.source.height;
        uint8_t rgba[4];
        ReadColor(record + 8, rgba);
        const int16_t x0 = int16_t(Memory::Read16(record)), y0 = int16_t(Memory::Read16(record + 2));
        const int16_t x1 = int16_t(Memory::Read16(record + 4)), y1 = int16_t(Memory::Read16(record + 6));
        if (std::abs(x1 - x0) >= 300 && std::abs(y1 - y0) >= 200)
            RenderDiag::NoteOverlay("Rect2DTex", 0, float(std::abs(x1 - x0)), float(std::abs(y1 - y0)), rgba[0], rgba[1], rgba[2], rgba[3]);
        mh3_ui_vertex vertices[4];
        mh3_ui_rect(vertices, x0, y0, x1, y1, 0, rgba,
                    int16_t(Memory::Read16(record + 12)) * du, int16_t(Memory::Read16(record + 14)) * dv, int16_t(Memory::Read16(record + 16)) * du, int16_t(Memory::Read16(record + 18)) * dv);
        DrawVertices(MH3_UI_QUADS, vertices, 4, true);
    }

    RestoreWorld(call);
}
REGISTER_NATIVE_FUNCTION(0x8028DF94, MH3_DrawRect2DTexture);

extern "C" void MH3_UIShapeSetup(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto flags = Memory::Read32(Shape);
    float matrix[12];
    const float* overrideMatrix = nullptr;

    if (!(flags & 1) && (flags & 4)) {
        for (unsigned i = 0; i < 12; ++i)
            matrix[i] = Memory::ReadFloat32(Shape + 0x74 + i * 4);

        overrideMatrix = matrix;
    }

    // The original binds the shape's texture clamped, then again with its
    // wrap modes: one binding with the wrap modes leaves the same state.
    const bool textured = (flags & 2) != 0;
    SetupPrimitive(call, flags, Shape + 0xE0, Shape + 0xE4, overrideMatrix, textured ? Memory::Read32(Shape + 0xD4) : 0, textured ? Memory::Read32(Shape + 0xD8) : 0);

    if (textured)
        ConfigureDirectCoordinates(0, flags & 8 ? 0 : 1, 4, flags & 8 ? 54 : 60);
}
REGISTER_NATIVE_FUNCTION(0x8028F390, MH3_UIShapeSetup);
extern "C" void MH3_DrawShape(CpuContext* cpu) {
    const auto flags = Memory::Read32(Shape);

    if ((flags & 0x3000) != 0x3000)
        return;

    const auto count = Memory::Read16(Shape + 6);
    ObserveShape(flags, count);

    if (!NativeGraphicsFrameShouldRender())
        return;

    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    call(0x8028F390);
    const auto primitiveValue = Memory::Read8(Shape + 4);

    if (!count) {
        RestoreWorld(call);
        return;
    }

    if (primitiveValue > MH3_UI_TRIANGLE_FAN)
        return; // original unsupported shape selector emits nothing
    const auto primitive = mh3_ui_primitive(primitiveValue);

    if ((flags & 2) && (primitive == MH3_UI_POINTS || primitive == MH3_UI_LINES || primitive == MH3_UI_LINE_STRIP) && Memory::Read8(Shape + 0xDC) != 0)
        MH3_THROW(std::runtime_error("Native UI textured line/point offset requires a pixel offset implementation"));

    const auto stream = ShapeStream(flags, count);
    AuroraNativeGeometry geometry{};

    if (Allocate(primitive, count, geometry)) {
        Checked(mh3_ui_build_stream(&GetNativeDrawState(), primitive, &stream, Memory::Read8(Shape + 5) / 6.f, &geometry));
        if (count >= 3 && geometry.vertices) {
            float minX = geometry.vertices[0].position[0], maxX = minX;
            float minY = geometry.vertices[0].position[1], maxY = minY;
            for (uint32_t i = 1; i < count; ++i) {
                minX = std::min(minX, geometry.vertices[i].position[0]);
                maxX = std::max(maxX, geometry.vertices[i].position[0]);
                minY = std::min(minY, geometry.vertices[i].position[1]);
                maxY = std::max(maxY, geometry.vertices[i].position[1]);
            }
            if ((maxX - minX) >= 300.0f && (maxY - minY) >= 200.0f) {
                const auto& c = geometry.vertices[0].color;
                RenderDiag::NoteOverlay("Shape", primitiveValue, maxX - minX, maxY - minY, c[0], c[1], c[2], c[3]);
            }
        }
        SubmitNativeGeometry(geometry, (flags & 2) != 0);
    }

    RestoreWorld(call);
}
REGISTER_NATIVE_FUNCTION(0x8028F91C, MH3_DrawShape);

extern "C" void MH3_DrawGradientFade(CpuContext* cpu) {
    const auto self = cpu->gpr[3];

    if (!Memory::Read8(self) || !NativeGraphicsFrameShouldRender())
        return;

    GuestFrame frame(cpu, 0x20);
    GuestCall call{cpu};
    call(0x80033134, {frame[0x10]});
    const float width = Memory::ReadFloat32(frame[0x10]), height = Memory::ReadFloat32(frame[0x14]);
    const bool wide = call(0x80033208) != 0;
    const float split = wide ? Memory::ReadFloat32(ScreenHeight) * 0.5f : Memory::ReadFloat32(ScreenHeight);
    Ortho(0, height, 0, width, 0, 1);
    Identity();
    RasterQuad(true, true);
    uiStages = 1;
    uiTextured = false;
    auto raster = GetNativeDrawState().raster;
    const auto mode = Memory::Read8(self + 1);

    if (mode == 1) {
        raster.blend_mode = 1;
        raster.dst_factor = 1;
    } else if (mode == 2)
        raster.blend_mode = 3;

    SetNativeRaster(raster);
    auto drawBand = [&](float top, float middle, float bottom, uint32_t colors) {
        mh3_ui_vertex vertices[8]{};
        const float x[8] = {0, 0, width, width, 0, 0, width, width};
        const float y[8] = {middle, top, top, middle, bottom, middle, middle, bottom};
        const unsigned c[8] = {2, 0, 1, 3, 4, 2, 3, 5};

        for (unsigned i = 0; i < 8; ++i) {
            vertices[i].position[0] = int16_t(int(x[i]));
            vertices[i].position[1] = int16_t(int(y[i]));
            ReadColor(colors + c[i] * 4, vertices[i].color);
        }

        RenderDiag::NoteOverlay("GradFade", mode, width, bottom - top, vertices[1].color[0], vertices[1].color[1], vertices[1].color[2], vertices[1].color[3]);
        DrawVertices(MH3_UI_QUADS, vertices, 8, false);
    };

    drawBand(0, int16_t(Memory::Read16(self + 0x24)), split, self + 4);

    if (wide) {
        const auto middle = int16_t(int16_t(int(split)) + int16_t(Memory::Read16(self + 0x26)));
        drawBand(split, middle, Memory::ReadFloat32(ScreenHeight), self + 0x2C);
    }
}
REGISTER_NATIVE_FUNCTION(0x80294398, MH3_DrawGradientFade);

extern "C" void MH3_DrawUISprites(CpuContext* cpu) {
    if (!NativeGraphicsFrameShouldRender())
        return;

    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto list = Memory::Read32(0x808A01C8 + (frame.Arg(3) & 255) * 4);
    if (!list)
        return;
    bool active = false;

    for (unsigned slot = 0; slot < 32; ++slot)
        if (int16_t(Memory::Read16(list + 64 + slot * 2)) > 0) {
            active = true;
            break;
        }
    if (!active)
        return;

    MH3_DebugDrawSetup(cpu);
    ScreenOrtho();
    Identity();
    RasterQuad(true, true);
    uiStages = 1;
    uiTextured = true;

    for (unsigned slot = 0; slot < 32; ++slot) {
        const auto textureId = int16_t(Memory::Read16(list + slot * 2)), count = int16_t(Memory::Read16(list + 64 + slot * 2));

        if (textureId == -1 || count <= 0)
            continue;

        if (count > 16)
            MH3_THROW(std::runtime_error("Native sprite batch exceeds its CPU quad slots"));

        const auto texture = call(0x802BD974, {uint32_t(textureId)});
        if (!texture)
            continue;
        BindNativeTexture(ReadNativeTextureObject(texture));
        const auto records = list + 128 + slot * 576;
        AuroraNativeGeometry geometry{};

        if (!Allocate(MH3_UI_QUADS, uint32_t(count) * 4, geometry))
            continue;

        for (unsigned quadIndex = 0; quadIndex < unsigned(count); ++quadIndex) {
            const auto record = records + quadIndex * 36;
            uint8_t rgba[4];
            ReadColor(record + 32, rgba);
            const int16_t x0 = int16_t(Memory::Read16(record + 4)), y0 = int16_t(Memory::Read16(record + 8));
            const int16_t x1 = int16_t(Memory::Read16(record + 6)), y1 = int16_t(Memory::Read16(record + 10));
            if (std::abs(x1 - x0) >= 300 && std::abs(y1 - y0) >= 200)
                RenderDiag::NoteOverlay("UISprite", uint32_t(textureId), float(std::abs(x1 - x0)), float(std::abs(y1 - y0)), rgba[0], rgba[1], rgba[2], rgba[3]);
            mh3_ui_vertex vertices[4];
            mh3_ui_rect(vertices, x0, y0, x1, y1, 0, rgba,
                        Memory::ReadFloat32(record + 16), Memory::ReadFloat32(record + 24), Memory::ReadFloat32(record + 20), Memory::ReadFloat32(record + 28));
            AuroraNativeGeometry quadGeometry{geometry.vertices + quadIndex * 4, geometry.indices + quadIndex * 6, 4, 6, geometry.frame, geometry.slot};
            Checked(mh3_ui_build_vertices(&GetNativeDrawState(), MH3_UI_QUADS, vertices, 4, 1, &quadGeometry));

            for (unsigned i = 0; i < 6; ++i)
                quadGeometry.indices[i] += uint16_t(quadIndex * 4);
        }

        SubmitNativeGeometry(geometry, true);
    }
}
REGISTER_NATIVE_FUNCTION(0x800347F8, MH3_DrawUISprites);

static void PopulateUITextureRecord(GuestCall& call, uint32_t tpl, uint32_t index, uint32_t record, bool mutableSource) {
    const auto descriptor = call(0x804C4284, {tpl, index});
    const auto header = Memory::Read32(descriptor);
    mh3_ui_texture_header image{};
    Checked(mh3_ui_texture_header_read(Span(header, 36), &image));
    mh3_native_texture_binding binding{};
    binding.source.resource_address = header;
    binding.source.payload_address = image.payload_address;
    binding.source.width = image.width;
    binding.source.height = image.height;
    binding.source.format = image.format;
    binding.source.levels = 1;
    binding.source.generation = NextGeneration(image.payload_address);
    if (!binding.source.generation)
        binding.source.generation = 1;
    binding.source.mutable_source = mutableSource;
    binding.sampler.wrap_s = image.wrap_s;
    binding.sampler.wrap_t = image.wrap_t;
    binding.sampler.min_filter = binding.sampler.mag_filter = 1;
    Memory::Write32(record + 0x24, image.payload_address);
    Memory::Write16(record + 0x28, uint16_t(image.width));
    Memory::Write16(record + 0x2A, uint16_t(image.height));
    Memory::Write32(record + 0x2C, image.format);
    Memory::Write32(record + 0x30, image.wrap_s);
    Memory::Write32(record + 0x34, image.wrap_t);
    MarkNativeTextureProduced(image.payload_address, binding.source.generation);
    StoreNativeTextureObject(record + 4, binding);
    Memory::Write8(record, 1);
}

static uint32_t BuildUITextureTPL(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto tpl = frame.Arg(3), id = frame.Arg(4);
    call(0x804C4170, {tpl}); // TPLBind relocates CPU pointers; no rendering

    if (id >= 32)
        return 0;

    uint32_t record = 0x80768B64 + id * 56;
    const uint32_t count = Memory::Read32(tpl + 4);
    uint32_t created = 0;

    for (uint32_t index = 0; index < count; ++index) {
        if (id + index >= 32)
            break;

        if (!Memory::Read8(record)) {
            if (id == 6 && index == 2)
                record = 0x80768DCC;

            PopulateUITextureRecord(call, tpl, index, record, false);
            ++created;
        }

        record += 56;
    }

    return created;
}

extern "C" void MH3_UITextureTPL(CpuContext* cpu) {
    cpu->gpr[3] = BuildUITextureTPL(cpu);
}
REGISTER_NATIVE_FUNCTION(0x802BD744, MH3_UITextureTPL);

static uint32_t BuildUITextureTPLBuffer(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto tpl = frame.Arg(3);
    uint32_t record = frame.Arg(4);
    call(0x804C4170, {tpl}); // TPLBind relocates CPU pointers; no rendering
    const uint32_t count = Memory::Read32(tpl + 4);
    uint32_t created = 0;

    for (uint32_t index = 0; index < count; ++index) {
        PopulateUITextureRecord(call, tpl, index, record, false);
        ++created;
        record += 56;
    }

    return created;
}

extern "C" void MH3_UITextureTPLBuffer(CpuContext* cpu) {
    cpu->gpr[3] = BuildUITextureTPLBuffer(cpu);
}
REGISTER_NATIVE_FUNCTION(0x802BD884, MH3_UITextureTPLBuffer);

// Explicit producer hook for any CPU code replacing font atlas pixels. Font
// lifecycle construction above publishes the initial generation. The RMHP08
// glyph registration helper8005bb54 returns without modifying atlas pixels.
void NativeUIFontAtlasProduced(uint32_t payload) {
    MarkNativeTextureProduced(payload, NextGeneration(payload));
}

static uint32_t ProduceFontAtlas(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto destination = frame.Arg(3);
    const auto bytes = call(0x80041050, {0x8057D690, destination}); // verified pure CPU word-stream decoder
    if (bytes < 64 || Memory::Read32(destination) != 0x0020AF30 || Memory::Read32(destination + 4) != 1)
        MH3_THROW(std::runtime_error("Native font producer did not decode its single-image TPL"));

    const auto table = Memory::Read32(destination + 8);

    if (table > bytes - 8)
        MH3_THROW(std::runtime_error("Native font producer table exceeds its decoded bytes"));

    const auto header = Memory::Read32(destination + table);

    if (header > bytes - 36)
        MH3_THROW(std::runtime_error("Native font producer header exceeds its decoded bytes"));

    const auto payload = Memory::Read32(destination + header + 8);

    if (payload >= bytes)
        MH3_THROW(std::runtime_error("Native font producer image exceeds its decoded bytes"));

    NativeUIFontAtlasProduced(destination + payload);
    return bytes;
}

extern "C" void MH3_FontAtlasDecode(CpuContext* cpu) {
    cpu->gpr[3] = ProduceFontAtlas(cpu);
}
// REGISTER_NATIVE_FUNCTION(0x8004119C, MH3_FontAtlasDecode); // Inlined/Unused in JPN (RMHJ08)

extern "C" void MH3_UIShapeTextureObject(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x20);
    GuestCall call{cpu};
    const auto id = frame.Arg(3) & 0xFFFF;

    if (id >= 33)
        MH3_THROW(std::runtime_error("Native shape texture resource id is out of range"));

    call(0x8028E40C, {id});
    Memory::Write32(frame[0x10], Memory::Read32(Shape + 0x258 + id * 4));
    Memory::Write32(frame[0x14], Memory::Read32(Shape + 0x2DC + id * 4));
    Memory::Write32(frame.Arg(4), Shape + 0xF8);
    Memory::Write32(frame.Arg(5), Shape + 0x1F8);
    Memory::Write32(Shape + 0xD4, 0);
    Memory::Write32(Shape + 0xD8, 0);
    auto binding = ResourceBinding(frame[0x10]);
    binding.map_id = 0;
    binding.tlut_id = 0;
    binding.sampler.wrap_s = binding.sampler.wrap_t = 0;

    if (binding.source.indexed) {
        if (!Memory::Read32(frame[0x14])) {
            Memory::Write32(frame.Arg(5), 0);
            return;
        }

        const auto palette = ReadNativeResPalette(frame[0x14]);
        StoreNativePaletteObject(Shape + 0x1F8, palette);
        BindNativePalette(0, palette);
    } else
        Memory::Write32(frame.Arg(5), 0);

    StoreNativeTextureObject(Shape + 0xF8, binding);
}
// REGISTER_NATIVE_FUNCTION(0x80054CD8, MH3_UIShapeTextureObject); // Inlined/Unused in JPN (RMHJ08)

// HUD parts' text: the font queue positions a part appended (per bank) and
// its anchor, which the flush draws them with (BeginNativeHudPart). Cleared
// with the queue each frame (FontStackReset).
namespace {
struct GlyphAnchor {
    uint32_t bank, begin, end, anchor;
};

constexpr uint32_t kFontAtlasWidth = 768;
constexpr uint32_t kFontAtlasHeight = 384;
constexpr uint32_t kFontAtlasBytes = kFontAtlasWidth * kFontAtlasHeight / 2;
constexpr uint32_t kFontSlotCacheCapacity = 1024;

struct FontGlyphSlotEntry {
    uint32_t dstOffset = 0xFFFFFFFFu;
    uint32_t src = 0;
    uint32_t firstWord = 0;
    uint32_t midWord = 0;
};

struct FontBankGlyphCache {
    uint32_t payload = 0;
    uint32_t glyphW = 0;
    uint32_t glyphH = 0;
    uint32_t rowTiles = 0;
    uint32_t lutWord = 0;
    uint32_t used = 0;
    std::array<FontGlyphSlotEntry, kFontSlotCacheCapacity> slots{};

    bool MatchesConfig(uint32_t p, uint32_t w, uint32_t h, uint32_t rt, uint32_t lut) const {
        return payload == p && glyphW == w && glyphH == h && rowTiles == rt && lutWord == lut;
    }

    void ClearSlots() {
        used = 0;
        for (auto& s : slots)
            s.dstOffset = 0xFFFFFFFFu;
    }

    void Reset(uint32_t p, uint32_t w, uint32_t h, uint32_t rt, uint32_t lut) {
        payload = p;
        glyphW = w;
        glyphH = h;
        rowTiles = rt;
        lutWord = lut;
        ClearSlots();
    }

    static uint32_t StartIndex(uint32_t dstOffset) {
        return ((dstOffset >> 5) * 2654435761u) & (kFontSlotCacheCapacity - 1);
    }

    bool Matches(uint32_t dstOffset, uint32_t src, uint32_t firstWord, uint32_t midWord) const {
        const uint32_t start = StartIndex(dstOffset);
        for (uint32_t probe = 0; probe < kFontSlotCacheCapacity; ++probe) {
            const auto& e = slots[(start + probe) & (kFontSlotCacheCapacity - 1)];
            if (e.dstOffset == dstOffset)
                return e.src == src && e.firstWord == firstWord && e.midWord == midWord;
            if (e.dstOffset == 0xFFFFFFFFu)
                return false;
        }
        return false;
    }

    void Record(uint32_t dstOffset, uint32_t src, uint32_t firstWord, uint32_t midWord) {
        if (used >= kFontSlotCacheCapacity - 64)
            ClearSlots();
        const uint32_t start = StartIndex(dstOffset);
        for (uint32_t probe = 0; probe < kFontSlotCacheCapacity; ++probe) {
            auto& e = slots[(start + probe) & (kFontSlotCacheCapacity - 1)];
            if (e.dstOffset == dstOffset || e.dstOffset == 0xFFFFFFFFu) {
                if (e.dstOffset == 0xFFFFFFFFu)
                    ++used;
                e = {dstOffset, src, firstWord, midWord};
                return;
            }
        }
    }
};

struct FontAtlasState {
    uint32_t payload = 0;
    uint64_t generation = 0;
    uint64_t writeEpoch = 0;
};

// The Japanese build (RMHJ08) writes glyphs into a single dynamic I4 atlas
// buffer (e.g. 0x816be000) every frame, while the render thread decodes the
// bound texture one or more frame packets later. Without a per-packet snapshot,
// the next frame's MH3_FontGlyphWrite overwrites the atlas while the previous
// packet is still being decoded (causing dialogue-adjacent panels such as quest
// / request details to flicker as glyph slot indices shift). Keep immutable
// shared snapshots per packet slot (zero copy on steady frames where generation
// is unchanged, and supporting multiple flushes within a single frame).
struct FontAtlasSnapshot {
    std::shared_ptr<const uint8_t[]> bytes;
    size_t size = 0;
    uint32_t address = 0;
    uint64_t generation = 0;
};

struct FontPacketSlot {
    uint64_t sequence = UINT64_MAX;
    std::vector<FontAtlasSnapshot> entries;
};

std::mutex fontGlyphCacheMutex;
std::atomic<uint64_t> fontAtlasWriteEpoch[2]{{1}, {1}};
FontBankGlyphCache fontBankGlyphCache[2]{};
FontAtlasState fontAtlasCache[2]{};
std::array<FontPacketSlot, kNativeFramePacketSlots> fontPacketSlots{};
FontAtlasSnapshot fontLatestSnapshot[2]{};

void CopyFontAtlasSnapshot(uint32_t bank, uint32_t payload, uint64_t generation, size_t bytes) {
    if (!NativeFramePacketRecording())
        return;

    auto& slot = fontPacketSlots[NativeFramePacketSlot()];
    const uint64_t sequence = NativeFramePacketSequence();
    if (slot.sequence != sequence) {
        slot.sequence = sequence;
        slot.entries.clear();
    }

    for (const auto& entry : slot.entries) {
        if (entry.address == payload && entry.generation == generation && entry.size >= bytes)
            return;
    }

    auto& latest = fontLatestSnapshot[bank & 1u];
    if (latest.address != payload || latest.generation != generation || latest.size < bytes) {
        std::shared_ptr<uint8_t[]> copy(new uint8_t[bytes]);
        std::memcpy(copy.get(), Memory::GetPointer(payload, bytes), bytes);
        latest = {std::move(copy), bytes, payload, generation};
    }

    slot.entries.push_back(latest);
}
} // namespace

const uint8_t* NativeFontAtlasCopy(uint32_t address, uint64_t generation, size_t bytes) {
    const auto& slot = fontPacketSlots[NativeRenderPacketSlot()];
    for (const auto& entry : slot.entries) {
        if (entry.address == address && entry.generation == generation && entry.size >= bytes)
            return entry.bytes.get();
    }
    return nullptr;
}

namespace {

std::vector<GlyphAnchor> glyphAnchors;
uint32_t partAnchor = 0, partQueued[2] = {};

uint32_t GlyphAnchorAt(uint32_t bank, uint32_t position) {
    for (const auto& a : glyphAnchors)
        if (a.bank == bank && position >= a.begin && position < a.end)
            return a.anchor;

    // A part that flushes its own text: what it queued so far.
    if (partAnchor && bank < 2 && position >= partQueued[bank])
        return partAnchor;

    return 0;
}
} // namespace

void BeginNativeHudPart(uint32_t anchor) {
    // Only a wider target places parts: elsewhere the draws keep one state.
    if (!NativeWindowAspect())
        anchor = 0;

    partAnchor = anchor;
    SetNativeHudAnchor(anchor);

    if (!anchor)
        return;

    if (const auto font = Memory::Read32(0x808A03B8))
        for (uint32_t bank = 0; bank < 2; ++bank)
            partQueued[bank] = Memory::Read16(font + 0xA894 + bank * 2);
}

void EndNativeHudPart() {
    if (!partAnchor)
        return;

    if (const auto font = Memory::Read32(0x808A03B8))
        for (uint32_t bank = 0; bank < 2; ++bank)
            if (const uint32_t end = Memory::Read16(font + 0xA894 + bank * 2); end > partQueued[bank])
                glyphAnchors.push_back({bank, partQueued[bank], end, partAnchor});

    partAnchor = 0;
    SetNativeHudAnchor(0);
}

extern "C" void func_80295524(CpuContext*);
extern "C" void MH3_FontGlyphWrite(CpuContext* cpu) {
    const uint32_t font = cpu->gpr[3], src = cpu->gpr[4], dst = cpu->gpr[5];
    const uint32_t bank = font ? (Memory::Read32(font + 0x50) & 1u) : 0u;
    const uint32_t payload = font ? Memory::Read32(font + 0x38 + bank * 4) : 0u;
    if (payload != 0 && dst >= payload && dst + 4 <= payload + kFontAtlasBytes) {
        const uint32_t glyphW = Memory::Read32(font + 8);
        const uint32_t glyphH = Memory::Read32(font + 12);
        const uint32_t rowTiles = Memory::Read32(font + 0xB4B0);
        const uint32_t lutAddr = cpu->gpr[13] - 25816;
        const uint32_t lutWord = Memory::Contains(lutAddr, 4) ? Memory::Read32(lutAddr) : 0u;
        const uint32_t dstOffset = dst - payload;
        const auto readMid = [](uint32_t addr, uint32_t offset) -> uint32_t {
            return (offset + 20 <= kFontAtlasBytes) ? Memory::Read32(addr + 16) : 0u;
        };
        const uint32_t firstWord = Memory::Read32(dst);
        const uint32_t midWord = readMid(dst, dstOffset);
        {
            std::lock_guard<std::mutex> lock(fontGlyphCacheMutex);
            auto& bc = fontBankGlyphCache[bank];
            if (!bc.MatchesConfig(payload, glyphW, glyphH, rowTiles, lutWord))
                bc.Reset(payload, glyphW, glyphH, rowTiles, lutWord);
            else if (bc.Matches(dstOffset, src, firstWord, midWord))
                return;
        }
        func_80295524(cpu);
        const uint32_t newFirst = Memory::Read32(dst);
        const uint32_t newMid = readMid(dst, dstOffset);
        {
            std::lock_guard<std::mutex> lock(fontGlyphCacheMutex);
            auto& bc = fontBankGlyphCache[bank];
            if (!bc.MatchesConfig(payload, glyphW, glyphH, rowTiles, lutWord))
                bc.Reset(payload, glyphW, glyphH, rowTiles, lutWord);
            bc.Record(dstOffset, src, newFirst, newMid);
        }
        fontAtlasWriteEpoch[bank].fetch_add(1, std::memory_order_release);
        return;
    }
    func_80295524(cpu);
    fontAtlasWriteEpoch[bank].fetch_add(1, std::memory_order_release);
}
REGISTER_NATIVE_FUNCTION_AS(0x80295524, MH3_FontGlyphWrite, "FontGlyphWrite");

extern "C" void MH3_FontFlush(CpuContext* cpu) {
    const auto font = Memory::Read32(0x808A03B8);

    if (!font)
        return;

    const auto queued = Memory::Read32(font + 0xA85C), flushed = Memory::Read32(font + 0xB4CC);

    if (!queued || queued == flushed)
        return;

    const int16_t end[2] = {int16_t(Memory::Read16(font + 0xA894)), int16_t(Memory::Read16(font + 0xA896))};
    const int16_t begin[2] = {int16_t(Memory::Read16(font + 0xB4C8)), int16_t(Memory::Read16(font + 0xB4CA))};

    if (begin[0] >= end[0] && begin[1] >= end[1])
        return;

    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};

    if (NativeGraphicsFrameShouldRender()) {
        MH3_DebugDrawSetup(cpu);
        ScreenOrtho();
        Identity();
        RasterQuad(true, true);
        uiStages = 1;
        uiTextured = true;
        const uint32_t bank = Memory::Read32(font + 0x50) & 1u;
        const uint32_t payload = Memory::Read32(font + 0x38 + bank * 4);
        if (payload) {
            const uint64_t writeEpoch = fontAtlasWriteEpoch[bank].load(std::memory_order_acquire);
            auto& cache = fontAtlasCache[bank];
            if (cache.payload != payload || cache.generation == 0 || cache.writeEpoch != writeEpoch) {
                uint64_t generation = NextGeneration(payload);
                if (!generation)
                    generation = 1;
                cache.payload = payload;
                cache.generation = generation;
                cache.writeEpoch = writeEpoch;
                MarkNativeTextureProduced(payload, generation);
            }
            mh3_native_texture_binding binding{};
            binding.source.resource_address = payload;
            binding.source.payload_address = payload;
            binding.source.width = 768;
            binding.source.height = 384;
            binding.source.format = 0; // GX_TF_I4
            binding.source.levels = 1;
            binding.source.min_lod = binding.source.max_lod = 0;
            binding.source.mutable_source = 1;
            binding.source.generation = cache.generation;
            binding.sampler.wrap_s = 0;
            binding.sampler.wrap_t = 0;
            binding.sampler.min_filter = binding.sampler.mag_filter = 1;
            CopyFontAtlasSnapshot(bank, payload, cache.generation, kFontAtlasBytes);
            BindNativeTexture(binding);
        }
        const auto records = Memory::Read32(font + 0x44);

        for (unsigned layer = 0; layer < 2; ++layer) {
            if (begin[layer] >= end[layer])
                continue;

            if (begin[layer] < 0 || end[layer] > 512)
                MH3_THROW(std::runtime_error("Native font glyph batch exceeds its CPU index array"));

            // Runs of glyphs that share their HUD part's anchor.
            for (uint32_t at = uint32_t(begin[layer]); at < uint32_t(end[layer]);) {
                const uint32_t anchor = GlyphAnchorAt(layer, at);
                uint32_t next = at + 1;

                while (next < uint32_t(end[layer]) && GlyphAnchorAt(layer, next) == anchor)
                    ++next;

                const uint32_t count = next - at;
                const auto indexAddress = font + 0xA898 + layer * 0x400 + at * 2;
                uint32_t largest = 0;

                for (uint32_t index = 0; index < count; ++index)
                    largest = std::max(largest, uint32_t(Memory::Read16(indexAddress + index * 2)));

                if (largest >= Memory::Read32(font + 0xA858))
                    MH3_THROW(std::runtime_error("Native font glyph index exceeds its CPU records"));

                AuroraNativeGeometry geometry{};
                SetNativeHudAnchor(anchor);

                if (Allocate(MH3_UI_QUADS, count * 4, geometry)) {
                    Checked(mh3_ui_build_glyphs(&GetNativeDrawState(), Span(records, size_t(largest + 1) * 84), Span(indexAddress, count * 2), count, &geometry));
                    SubmitNativeGeometry(geometry, true);
                }

                at = next;
            }

            SetNativeHudAnchor(0);
        }
    }

    // CPU submission cursors advance even when frame admission/draw capacity skips.
    for (unsigned layer = 0; layer < 2; ++layer)
        if (begin[layer] < end[layer])
            Memory::Write16(font + 0xB4C8 + layer * 2, uint16_t(end[layer]));
    Invalidate(call);
    Memory::Write32(font + 0xB4CC, queued);
}
REGISTER_NATIVE_FUNCTION(0x80295AF0, MH3_FontFlush);

// 0x8029501C flfntStackReset: empties the font's draw stacks before a frame's
// logic, once a frame. Each of the two stack banks (+0xA894, +0xB4C8 u16
// counts) gets its 16 runs of 32 u16 slots (+0xA898, 0x400 bytes each) set
// to 0xFFFF; the 512 u16 entries at +0xB0A0 and the words +0xA890,
// +0xB098, +0xA85C (queued), +0xB4CC (flushed), +0xA87C and +0xA880 are
// zeroed; +0x44 points at bank +0x50's 0xA800-byte buffer at +0x54.
namespace {
void FontStackReset(CpuContext*) {
    const uint32_t font = Memory::Read32(0x808A03B8);
    glyphAnchors.clear();

    if (!font)
        return;

    for (uint32_t bank = 0; bank < 2; ++bank) {
        Memory::Write16(font + 0xA894 + bank * 2, 0);
        Memory::Write16(font + 0xB4C8 + bank * 2, 0);
    }

    std::memset(Memory::GetPointer(font + 0xA898, 0x800), 0xFF, 0x800);
    Memory::Write32(font + 0xA890, 0);
    std::memset(Memory::GetPointer(font + 0xB0A0, 0x400), 0, 0x400);

    for (const uint32_t word : {0xB098u, 0xA85Cu, 0xB4CCu, 0xA87Cu, 0xA880u})
        Memory::Write32(font + word, 0);

    Memory::Write32(font + 0x44, font + Memory::Read32(font + 0x50) * 0xA800 + 0x54);
}
} // namespace

extern "C" void MH3_FontStackReset(CpuContext* cpu) {
    FontStackReset(cpu);
}
REGISTER_NATIVE_FUNCTION(0x8029501C, MH3_FontStackReset);

// net_data.rso, the online menus (connection, terms, CAPCOM ID), draws its
// sprites with GX itself. Two overlay functions, decompiled with Ghidra and
// read from their disassembly, enter here through the overlay's dispatch
// records (scripts/translate_rso_overlays.py, NATIVE_PORTS); the overlay
// loads at the demo module's base, so they are not registered by address.
//
// NetMenuDrawSetup (0x8097F9A0): DebugDrawSetup, then a screen-space
// orthographic projection (C_MTXOrtho(0, screen height, 0, screen width, 0,
// 1)), an identity position matrix, no culling, depth test ALWAYS without
// writes, one texture coordinate from TEX0 (identity), one direct TEV stage
// modulating texture 0 by the vertex colour, and the vertex format s16 XY
// position, RGBA8 colour and f32 ST. The copy filter it turns off and the
// texture cache it invalidates have no native state: the display copy has no
// vertical filter and textures bind by object.
extern "C" void MH3_NetMenuDrawSetup(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x80);
    GuestCall call{cpu};
    MH3_DebugDrawSetup(cpu); // DebugDrawSetup
    ScreenOrtho();
    Identity();
    RasterQuad(false, true);

    if (!MaterialCurrent(uiMaterial, true))
        PublishPrimitiveMaterial(true);

    ConfigureDirectCoordinates(0, 1, 4, 60);
    uiTextured = true;
    uiStages = 1;
}

// NetMenuDrawSprites(atlases, sprites, count) (0x8097FB58). An atlas is 56
// bytes: its GXTexObj at +0x10, its width and height (s16) at +0x34/+0x36.
// A sprite is 16 bytes: atlas (u8), part (u8), anchor (u8), additive (u8),
// x, y, width, height (s16) and an RGBA8 colour. The part's texel rectangle
// (u, v, width, height: s16) comes from the overlay's per-atlas tables
// (0x8098A1B8). Anchor 0 shifts the sprite by half the screen's extra width
// over 640, anchor 2 by all of it, and anchor 3 left by 107 on a 640-wide
// screen. Each sprite is one textured quad with source-alpha blending, or
// additive blending.
extern "C" void MH3_NetMenuDrawSprites(CpuContext* cpu) {
    constexpr uint32_t kPartTables = 0x8098A1B8;
    GuestFrame frame(cpu, 0x80);
    GuestCall call{cpu};
    const uint32_t atlases = frame.Arg(3), sprites = frame.Arg(4);
    const int32_t count = int32_t(frame.Arg(5));
    call(0x80033134, {frame[0x18]}); // get_ScreenSize(MH_VEC2*)
    const int16_t shift = int16_t(int32_t((Memory::ReadFloat32(frame[0x18]) - 640.0f) * 0.5f));

    for (int32_t i = 0; i < count; ++i) {
        const uint32_t sprite = sprites + uint32_t(i) * 16;
        const uint8_t atlasIndex = Memory::Read8(sprite), part = Memory::Read8(sprite + 1), anchor = Memory::Read8(sprite + 2);
        int16_t x = int16_t(Memory::Read16(sprite + 4));
        const int16_t y = int16_t(Memory::Read16(sprite + 6)), width = int16_t(Memory::Read16(sprite + 8)), height = int16_t(Memory::Read16(sprite + 10));

        if (anchor == 0)
            x = int16_t(x + shift);

        if (anchor == 2)
            x = int16_t(x + int16_t(shift * 2));

        if (anchor == 3 && shift == 0)
            x = int16_t(x - 107);

        const uint32_t rectangle = Memory::Read32(kPartTables + atlasIndex * 4u) + part * 8u;
        const int16_t u = int16_t(Memory::Read16(rectangle)), v = int16_t(Memory::Read16(rectangle + 2));
        const uint32_t atlas = atlases + atlasIndex * 56u;
        const float atlasWidth = float(int16_t(Memory::Read16(atlas + 0x34))), atlasHeight = float(int16_t(Memory::Read16(atlas + 0x36)));
        const float s0 = float(u) / atlasWidth, t0 = float(v) / atlasHeight;
        const float s1 = float(u + int16_t(Memory::Read16(rectangle + 4))) / atlasWidth, t1 = float(v + int16_t(Memory::Read16(rectangle + 6))) / atlasHeight;

        // GXSetBlendMode(BLEND, SRCALPHA, INVSRCALPHA or ONE, CLEAR).
        auto raster = GetNativeDrawState().raster;
        raster.blend_mode = 1;
        raster.src_factor = 4;
        raster.dst_factor = Memory::Read8(sprite + 3) ? 1 : 5;
        raster.logic_op = 0;
        SetNativeRaster(raster);
        BindNativeTexture(ReadNativeTextureObject(atlas + 0x10));
        uint8_t rgba[4];
        ReadColor(sprite + 12, rgba);
        mh3_ui_vertex vertices[4];
        mh3_ui_rect(vertices, x, y, int16_t(x + width), int16_t(y + height), 0, rgba, s0, t0, s1, t1);
        DrawVertices(MH3_UI_QUADS, vertices, 4, true);
    }
}

#include "../vita/launcher_font.h"
#include <cstdio>

namespace {

static uint32_t DecodeShiftJisChar(uint16_t code) {
    if (code == 0x8140) return 0x20; // Space
    if (code == 0x8141) return 0x3001; // 、
    if (code == 0x8142) return 0x3002; // 。
    if (code == 0x8148) return 0xFF1F; // ？
    if (code == 0x8149) return 0xFF01; // ！
    if (code == 0x815B) return 0x30FC; // ー
    if (code == 0x8163) return 0x2026; // …
    if (code == 0x8169) return 0xFF08; // （
    if (code == 0x816A) return 0xFF09; // ）
    if (code == 0x8175) return 0x300C; // 「
    if (code == 0x8176) return 0x300D; // 」
    if (code >= 0x824F && code <= 0x8258) return 0xFF10 + (code - 0x824F); // ０-９
    if (code >= 0x8260 && code <= 0x8279) return 0xFF21 + (code - 0x8260); // Ａ-Ｚ
    if (code >= 0x8281 && code <= 0x829A) return 0xFF41 + (code - 0x8281); // ａ-ｚ
    if (code >= 0x829F && code <= 0x82F1) return 0x3041 + (code - 0x829F); // ぁ-ん
    if (code >= 0x8340 && code <= 0x837E) return 0x30A1 + (code - 0x8340); // ァ-ミ
    if (code >= 0x8380 && code <= 0x8396) return 0x30E0 + (code - 0x8380); // ム-ヶ

    // Kanji lookup
    switch (code) {
        case 0x89E6: return 0x753B; // 画
        case 0x89F3: return 0x58CA; // 壊
        case 0x8A4A: return 0x958B; // 開
        case 0x8AC7: return 0x7BA1; // 管
        case 0x8C88: return 0x6C7A; // 決
        case 0x8CB9: return 0x6E90; // 源
        case 0x8D87: return 0x5408; // 合
        case 0x8DEC: return 0x4F5C; // 作
        case 0x8DED: return 0x524A; // 削
        case 0x8E6E: return 0x59CB; // 始
        case 0x8F9C: return 0x9664; // 除
        case 0x8FEA: return 0x5834; // 場
        case 0x9056: return 0x65B0; // 新
        case 0x90AC: return 0x6210; // 成
        case 0x91B6: return 0x5B58; // 存
        case 0x91CC: return 0x4F53; // 体
        case 0x92E8: return 0x5B9A; // 定
        case 0x9364: return 0x96FB; // 電
        case 0x954B: return 0x5FC5; // 必
        case 0x95DB: return 0x4FDD; // 保
        case 0x967B: return 0x672C; // 本
        case 0x96CA: return 0x9762; // 面
        case 0x9776: return 0x8981; // 要
        case 0x979D: return 0x7406; // 理
        default: break;
    }
    return 0x3F; // '?'
}

static std::string ShiftJisToUtf8(const char* sjis) {
    std::string out;
    if (!sjis) return out;

    while (*sjis) {
        uint8_t c1 = static_cast<uint8_t>(*sjis++);
        if (c1 < 0x80) {
            out.push_back(static_cast<char>(c1));
        } else if (c1 >= 0xA1 && c1 <= 0xDF) {
            uint32_t cp = 0xFF61 + (c1 - 0xA1);
            out.push_back(static_cast<char>(0xEF));
            out.push_back(static_cast<char>(0xBD | ((cp >> 6) & 0x01)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if ((c1 >= 0x81 && c1 <= 0x9F) || (c1 >= 0xE0 && c1 <= 0xFC)) {
            if (!*sjis) break;
            uint8_t c2 = static_cast<uint8_t>(*sjis++);
            uint16_t code = (c1 << 8) | c2;
            if (code == 0x8140) {
                out.push_back(' ');
                out.push_back(' ');
                continue;
            }
            uint32_t cp = DecodeShiftJisChar(code);
            if (cp == 0) cp = 0x3F;
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
    }
    return out;
}

static std::unique_ptr<VitaLauncher::Font> s_dialogFont;
static bool s_dialogFontInitialized = false;

static void EnsureDialogFont() {
    if (!s_dialogFontInitialized) {
        s_dialogFontInitialized = true;
        VitaLauncher::Font::Open(s_dialogFont);
    }
}

} // namespace

// 0x802968B4: Dialog / message box string rendering routine (called by 0x80297DE4).
// Replaced native implementation: renders Shift-JIS text via VitaLauncher::Font (ScePvf)
// directly to native UI vertex stream, avoiding legacy GX FIFO writes and enabling
// proper save data dialog interaction.
extern "C" void MH3_DialogDrawString(CpuContext* cpu) {
    const int16_t x = static_cast<int16_t>(cpu->gpr[3]);
    const int16_t y = static_cast<int16_t>(cpu->gpr[4]);
    const uint32_t textPtr = cpu->gpr[5];

    if (!textPtr || !Memory::Contains(textPtr, 1))
        return;

    std::string sjisText;
    for (uint32_t p = textPtr; Memory::Contains(p, 1); ++p) {
        char c = Memory::Read8(p);
        if (!c) break;
        sjisText.push_back(c);
        if (sjisText.size() > 512) break;
    }

#ifdef MH3_DEVELOPMENT
    std::fprintf(stderr, "MH3 DialogDrawString: x=%d y=%d len=%zu text=\"%s\"\n", x, y, sjisText.size(), sjisText.c_str());
#endif

    EnsureDialogFont();
    if (!s_dialogFont)
        return;

    std::string utf8Text = ShiftJisToUtf8(sjisText.c_str());
    if (utf8Text.empty())
        return;

    if (!NativeGraphicsFrameShouldRender())
        return;

    ScreenOrtho();
    Identity();
    RasterQuad(false, true);
    auto material = GetNativeMaterialState();
    mh3_system_ui_fade_material(&material);
    SetNativeMaterial(material);
    uiTextured = false;
    uiStages = 1;

    std::vector<VitaLauncher::Rectangle> rects;
    float drawX = static_cast<float>(x);
    float drawY = static_cast<float>(y);

    size_t lineStart = 0;
    while (lineStart < utf8Text.size()) {
        size_t lineEnd = utf8Text.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = utf8Text.size();

        std::string_view line(&utf8Text[lineStart], lineEnd - lineStart);
        s_dialogFont->Text(rects, drawX, drawY, line, 0xFF000000, 800.0f);
        drawY += 22.0f;
        lineStart = lineEnd + 1;
    }

    if (!rects.empty()) {
        const uint8_t color[4] = {0, 0, 0, 255};
        for (const auto& r : rects) {
            mh3_ui_vertex v[4];
            mh3_ui_rect(v, static_cast<int16_t>(r.x), static_cast<int16_t>(r.y),
                        static_cast<int16_t>(r.x + r.w), static_cast<int16_t>(r.y + r.h),
                        0, color, 0, 0, 0, 0);
            DrawVertices(MH3_UI_QUADS, v, 4, false);
        }
    }
}
REGISTER_NATIVE_FUNCTION(0x802968B4, MH3_DialogDrawString);

// 0x80296744: Dialog quad FIFO burst rendering routine called by 0x802968B4.
extern "C" void MH3_DialogDrawQuad(CpuContext*) {
}
REGISTER_NATIVE_FUNCTION(0x80296744, MH3_DialogDrawQuad);
