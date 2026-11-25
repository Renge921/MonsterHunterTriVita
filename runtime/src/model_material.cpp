// Thin RMHP08 material ABI adapters. Native C owns field effects;
// platform mapping and typed backend publication remain at this boundary.
// All 14 verified wrappers form one coordinated family. No GX fallback.
// The helpers take register values, not the context, so the registered bodies
// name only the registers they use and the translator calls them directly as
// precise natives, without the dispatch's register guard.
#include "abi_bridge.h"
#include "fatal.h"
#include "guest_span.h"
#include "memory.h"
#include "native_graphics.h"
#include "runtime/native/adapters/model_catalog_bridge.h"
#include "runtime/native/adapters/model_material_bridge.h"
#include "runtime/native/mh3_model_material_updates.h"
#include "runtime/native/mh3_material_state.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

uint32_t MH3NativeResReference(uint32_t reference);
namespace {
void Checked(mh3_res_status status) {
    if (status != MH3_RES_OK)
        MH3_THROW(std::runtime_error("Native material producer failed: " + std::to_string(status)));
}

uint32_t Data(uint32_t reference) {
    return MH3NativeResReference(reference);
}

void Submit(uint32_t reference, mh3_model_material_block kind, uint32_t count = 1) {
    if ((kind == MH3_MODEL_MATERIAL_INDIRECT || kind == MH3_MODEL_MATERIAL_TEXGEN) && !count)
        return;

    uint32_t address = Data(reference);

    if (kind == MH3_MODEL_MATERIAL_PIPELINE) {
        if (address > UINT32_MAX - 32)
            MH3_THROW(std::runtime_error("Material pipeline address overflows"));

        address += 32;
    }

    OverlayNativeModelMaterialGroup(ReadNativeLiveMaterialBlockView(address, kind).fields, kind, count);
}

uint32_t Copy(uint32_t reference, uint32_t destination, mh3_model_material_block kind) {
    CopyNativeLiveMaterialBlock(destination, Data(reference), kind);
    return destination;
}

template <class Producer>
void Update(uint32_t reference, mh3_model_material_block kind, Producer producer) {
    // Per-frame setters (alpha compare, konst colours) of a block written
    // before: update its fields and bytes in place. Producers validate their
    // arguments before writing either.
    if (auto* live = WritableNativeLiveMaterialBlock(Data(reference), kind)) {
        Checked(producer(&live->fields, mh3_res_mut_span{GuestRange(live->address, live->bytes), live->bytes}, NextNativeMaterialRevision()));
        return;
    }

    auto block = ReadNativeLiveMaterialBlock(Data(reference), kind);
    std::array<uint8_t, 160> compatibility{};

    if (block.bytes > compatibility.size())
        MH3_THROW(std::runtime_error("Invalid CPU material setter group size"));

    auto* output = Memory::GetPointer(block.address, block.bytes);
    std::memcpy(compatibility.data(), output, block.bytes);
    Checked(producer(&block.fields, mh3_res_mut_span{compatibility.data(), block.bytes}, NextNativeMaterialRevision()));

    // Arguments and CPU serialization are validated on temporary owned values.
    // A provenance failure leaves RAM and published typed fields unchanged.
    RecordNativeTypedMaterialWrite(block.address, block.bytes, kind);
    std::memcpy(output, compatibility.data(), block.bytes);
    PublishNativeLiveMaterialBlock(block);
    MarkNativeLiveMaterialBlockWritable(block.address, kind);
}

std::array<uint8_t, 4> Color(uint32_t address) {
    std::array<uint8_t, 4> result{};
    std::memcpy(result.data(), Memory::GetPointer(address, 4), 4);
    return result;
}
} // namespace

extern "C" void MH3_MatPixelSubmit(CpuContext* cpu) {
    Submit(cpu->gpr[3], MH3_MODEL_MATERIAL_PIXEL);
} // PAL 80094480 -> JPN 8051D2B8
REGISTER_NATIVE_FUNCTION_AS(0x8051D2B8, MH3_MatPixelSubmit, "MatPixelSubmit");
extern "C" void MH3_MatColorSubmit(CpuContext* cpu) {
    Submit(cpu->gpr[3], MH3_MODEL_MATERIAL_COLORS);
} // PAL 8009454C -> JPN 8051D2E8
REGISTER_NATIVE_FUNCTION_AS(0x8051D2E8, MH3_MatColorSubmit, "MatColorSubmit");
extern "C" void MH3_MatIndirectSubmit(CpuContext* cpu) {
    Submit(cpu->gpr[3], MH3_MODEL_MATERIAL_INDIRECT, uint8_t(cpu->gpr[4]));
} // PAL 80096634 -> JPN 8051E030
REGISTER_NATIVE_FUNCTION_AS(0x8051E030, MH3_MatIndirectSubmit, "MatIndirectSubmit");
extern "C" void MH3_MatTexgenSubmit(CpuContext* cpu) {
    Submit(cpu->gpr[3], MH3_MODEL_MATERIAL_TEXGEN, uint8_t(cpu->gpr[4]));
} // PAL 80097210 -> JPN 8051E8BC
REGISTER_NATIVE_FUNCTION_AS(0x8051E8BC, MH3_MatTexgenSubmit, "MatTexgenSubmit");
extern "C" void MH3_MatPipelineSubmit(CpuContext* cpu) {
    Submit(cpu->gpr[3], MH3_MODEL_MATERIAL_PIPELINE);
} // PAL 8009A244 -> JPN 8051D154
REGISTER_NATIVE_FUNCTION_AS(0x8051D154, MH3_MatPipelineSubmit, "MatPipelineSubmit");
extern "C" void MH3_MatPixelCopy(CpuContext* cpu) {
    cpu->gpr[3] = Copy(cpu->gpr[3], cpu->gpr[4], MH3_MODEL_MATERIAL_PIXEL);
} // PAL 800946D8 -> JPN 8051D318
REGISTER_NATIVE_FUNCTION_AS(0x8051D318, MH3_MatPixelCopy, "MatPixelCopy");
extern "C" void MH3_MatColorCopy(CpuContext* cpu) {
    cpu->gpr[3] = Copy(cpu->gpr[3], cpu->gpr[4], MH3_MODEL_MATERIAL_COLORS);
} // PAL 80094724 -> JPN 8051D354
REGISTER_NATIVE_FUNCTION_AS(0x8051D354, MH3_MatColorCopy, "MatColorCopy");
extern "C" void MH3_MatIndirectCopy(CpuContext* cpu) {
    cpu->gpr[3] = Copy(cpu->gpr[3], cpu->gpr[4], MH3_MODEL_MATERIAL_INDIRECT);
} // PAL 80094770 -> JPN 8051D390
REGISTER_NATIVE_FUNCTION_AS(0x8051D390, MH3_MatIndirectCopy, "MatIndirectCopy");
extern "C" void MH3_MatTexgenCopy(CpuContext* cpu) {
    cpu->gpr[3] = Copy(cpu->gpr[3], cpu->gpr[4], MH3_MODEL_MATERIAL_TEXGEN);
} // PAL 800947BC -> JPN 8051D3CC
REGISTER_NATIVE_FUNCTION_AS(0x8051D3CC, MH3_MatTexgenCopy, "MatTexgenCopy");
extern "C" void MH3_MatAlphaCompare(CpuContext* cpu) {
    Update(cpu->gpr[3], MH3_MODEL_MATERIAL_PIXEL, [&](auto* state, auto bytes, uint64_t revision) {
        return mh3_model_material_alpha_compare(state, bytes, cpu->gpr[4], uint8_t(cpu->gpr[5]), cpu->gpr[6], cpu->gpr[7], uint8_t(cpu->gpr[8]), revision);
    });
} // PAL 80095F94 -> JPN 8051DC2C
REGISTER_NATIVE_FUNCTION_AS(0x8051DC2C, MH3_MatAlphaCompare, "MatAlphaCompare");
extern "C" void MH3_MatBlend(CpuContext* cpu) {
    Update(cpu->gpr[3], MH3_MODEL_MATERIAL_PIXEL,
           [&](auto* state, auto bytes, uint64_t revision) { return mh3_model_material_blend(state, bytes, cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], revision); });
} // PAL 80096100 -> JPN 8051DCF4
REGISTER_NATIVE_FUNCTION_AS(0x8051DCF4, MH3_MatBlend, "MatBlend");
extern "C" void MH3_MatTevColor(CpuContext* cpu) {
    const auto color = Color(cpu->gpr[5]);
    Update(cpu->gpr[3], MH3_MODEL_MATERIAL_COLORS, [&](auto* state, auto bytes, uint64_t revision) { return mh3_model_material_tev_color(state, bytes, cpu->gpr[4], color.data(), revision); });
} // PAL 80096300 -> JPN 8051DE60
REGISTER_NATIVE_FUNCTION_AS(0x8051DE60, MH3_MatTevColor, "MatTevColor");
extern "C" void MH3_MatKonstColor(CpuContext* cpu) {
    const auto color = Color(cpu->gpr[5]);
    Update(cpu->gpr[3], MH3_MODEL_MATERIAL_COLORS, [&](auto* state, auto bytes, uint64_t revision) { return mh3_model_material_konst_color(state, bytes, cpu->gpr[4], color.data(), revision); });
} // PAL 80096548 -> JPN 8051DFAC
REGISTER_NATIVE_FUNCTION_AS(0x8051DFAC, MH3_MatKonstColor, "MatKonstColor");
extern "C" void MH3_MatIndirectMatrix(CpuContext* cpu) {
    std::array<float, 12> matrix{};

    for (unsigned i = 0; i < 12; i++)
        matrix[i] = Memory::ReadFloat32(cpu->gpr[5] + i * 4);

    Update(cpu->gpr[3], MH3_MODEL_MATERIAL_INDIRECT,
           [&](auto* state, auto bytes, uint64_t revision) { return mh3_model_material_indirect_matrix(state, bytes, cpu->gpr[4], matrix.data(), int8_t(cpu->gpr[6]), revision); });
} // PAL 80096C10 -> JPN 8051E514
REGISTER_NATIVE_FUNCTION_AS(0x8051E514, MH3_MatIndirectMatrix, "MatIndirectMatrix");
