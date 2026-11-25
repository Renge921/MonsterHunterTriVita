// RMHP08 light ABI boundary. All light calculations/default construction are
// native C. CPU compatibility records do not submit GX or GPU commands.
#include "abi_bridge.h"
#include "fatal.h"
#include "memory.h"
#include "../native/mh3_lighting.h"
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
void Checked(mh3_res_status status) {
    if (status != MH3_RES_OK)
        MH3_THROW(std::runtime_error("Native light input failed: " + std::to_string(status)));
}

mh3_res_mut_span Record(uint32_t address) {
    return {Memory::GetPointer(address, MH3_LIGHT_RECORD_BYTES), MH3_LIGHT_RECORD_BYTES};
}

mh3_light Read(uint32_t address) {
    const auto record = Record(address);
    mh3_light light{};
    Checked(mh3_light_read({record.data, record.size}, &light));
    return light;
}

void Store(uint32_t address, const mh3_light& light) {
    Checked(mh3_light_store(Record(address), &light));
}
} // namespace

extern "C" void MH3_LightCopy(CpuContext* cpu) {
    const auto source = Record(cpu->gpr[4]);
    Checked(mh3_light_copy_record(Record(cpu->gpr[3]), {source.data, source.size}));
}
REGISTER_NATIVE_FUNCTION(0x8053BFD0, MH3_LightCopy);
extern "C" void MH3_LightClear(CpuContext* cpu) {
    Checked(mh3_light_clear_record(Record(cpu->gpr[3])));
}
REGISTER_NATIVE_FUNCTION(0x8053C01C, MH3_LightClear);
extern "C" void MH3_LightColor(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    std::memcpy(&light.color, Memory::GetPointer(cpu->gpr[4], 4), 4);
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C030, MH3_LightColor);
extern "C" void MH3_LightPosition(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_position(&light, float(cpu->fpr[1].d), float(cpu->fpr[2].d), float(cpu->fpr[3].d));
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C078, MH3_LightPosition);
extern "C" void MH3_LightDirection(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_direction(&light, float(cpu->fpr[1].d), float(cpu->fpr[2].d), float(cpu->fpr[3].d), 0);
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C0B4, MH3_LightDirection);
extern "C" void MH3_LightSpot(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_spot(&light, float(cpu->fpr[1].d), cpu->gpr[4]);
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C0F4, MH3_LightSpot);
extern "C" void MH3_LightAngularAttenuation(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_angular(&light, float(cpu->fpr[1].d), float(cpu->fpr[2].d), float(cpu->fpr[3].d));
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C134, MH3_LightAngularAttenuation);
extern "C" void MH3_LightDistanceAttenuation(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_distance_attenuation(&light, float(cpu->fpr[1].d), float(cpu->fpr[2].d), cpu->gpr[4]);
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C174, MH3_LightDistanceAttenuation);
extern "C" void MH3_LightDistancePolynomial(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_distance(&light, float(cpu->fpr[1].d), float(cpu->fpr[2].d), float(cpu->fpr[3].d));
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C1B4, MH3_LightDistancePolynomial);
extern "C" void MH3_LightSpecularDirection(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_direction(&light, float(cpu->fpr[1].d), float(cpu->fpr[2].d), float(cpu->fpr[3].d), 1);
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C1F4, MH3_LightSpecularDirection);
extern "C" void MH3_LightShininess(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_light_set_shininess(&light, float(cpu->fpr[1].d));
    Store(cpu->gpr[3], light);
}
REGISTER_NATIVE_FUNCTION(0x8053C234, MH3_LightShininess);
extern "C" void MH3_LightGetPosition(CpuContext* cpu) {
    if (!cpu->gpr[4])
        return;

    const auto light = Read(cpu->gpr[3]);
    Checked(mh3_light_store_vector({Memory::GetPointer(cpu->gpr[4], 12), 12}, &light.position));
}
REGISTER_NATIVE_FUNCTION(0x8053C294, MH3_LightGetPosition);
extern "C" void MH3_LightGetDirection(CpuContext* cpu) {
    if (!cpu->gpr[4])
        return;

    const auto light = Read(cpu->gpr[3]);
    const auto direction = mh3_light_ray_direction(&light);
    Checked(mh3_light_store_vector({Memory::GetPointer(cpu->gpr[4], 12), 12}, &direction));
}
REGISTER_NATIVE_FUNCTION(0x8053C2B0, MH3_LightGetDirection);
extern "C" void MH3_LightGetColor(CpuContext* cpu) {
    if (!cpu->gpr[4])
        return;

    const auto light = Read(cpu->gpr[3]);
    std::memcpy(Memory::GetPointer(cpu->gpr[4], 4), &light.color, 4);
}
REGISTER_NATIVE_FUNCTION(0x8053C2CC, MH3_LightGetColor);
extern "C" void MH3_LightApplyView(CpuContext* cpu) {
    auto light = Read(cpu->gpr[3]);
    mh3_matrix34 view{};
    Checked(mh3_matrix_read34({{Memory::GetPointer(cpu->gpr[4], 48), 48}, 1, 48, 1}, 0, &view));
    mh3_light_apply_view(&light, &view);
    Store(cpu->gpr[3], light);
}
// Disabled unported PAL address for RMHJ08: REGISTER_NATIVE_FUNCTION_AS(0x8007A878, MH3_LightApplyView, "LightApplyView");
extern "C" void MH3_LightSettingDefaults(CpuContext* cpu) {
    const auto count = uint16_t(cpu->gpr[6]);
    const auto sets = uint16_t(cpu->gpr[8]);

    if (!count || count > MH3_LIGHT_MAX || !sets || sets > MH3_LIGHT_SET_MAX)
        MH3_THROW(std::runtime_error("Native light setting count exceeds the scene allocation"));

    Checked(mh3_light_setting_store_defaults(
        {Memory::GetPointer(cpu->gpr[3], 16), 16}, {Memory::GetPointer(cpu->gpr[4], size_t(count) * MH3_LIGHT_RECORD_BYTES), size_t(count) * MH3_LIGHT_RECORD_BYTES},
        {Memory::GetPointer(cpu->gpr[5], size_t(count) * 4), size_t(count) * 4},
        {Memory::GetPointer(cpu->gpr[7], size_t(sets) * MH3_LIGHT_SET_RECORD_BYTES), size_t(sets) * MH3_LIGHT_SET_RECORD_BYTES}, count, sets, cpu->gpr[4], cpu->gpr[5], cpu->gpr[7]));
}
REGISTER_NATIVE_FUNCTION(0x8053C2E0, MH3_LightSettingDefaults);
