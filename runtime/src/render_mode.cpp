// Thin RMHJ08 ABI boundary for native display-mode selection. No GX calls.
#include "abi_bridge.h"
#include "fatal.h"
#include "guest_call.h"
#include "memory.h"
#include "native_graphics.h"
#include "widescreen.h"
#include "language.h"
#include "../native/mh3_render_mode.h"
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr uint32_t ModeAddress = 0x80731E80;
constexpr std::array<uint32_t, 3> Defaults{0x80704A20, 0x80704A5C, 0x80704A98};
constexpr int32_t ModePointer = -0x4738;

// .sdata2 f32 16:9 camera aspect (r2 - 0x7FF8).
constexpr uint32_t WideAspect = 0x808A13C8;
constexpr uint32_t ScreenRecord = 0x807693F8;

std::optional<bool> launcherSetting;
std::optional<uint8_t> launcherLanguage;

constexpr uint8_t kEnglish = 1;

// Wii SC language numbers (SC_LANG_*). Japanese ROM is Japanese only (0).
constexpr GameLanguage Languages[]{
    {"Japanese", "ja", 0},
};

void Checked(mh3_res_status status) {
    if (status != MH3_RES_OK)
        MH3_THROW(std::runtime_error("Native render mode input failed: " + std::to_string(status)));
}

mh3_render_mode Read(uint32_t address) {
    mh3_render_mode mode{};
    Checked(mh3_render_mode_read({Memory::GetPointer(address, MH3_RENDER_MODE_BYTES), MH3_RENDER_MODE_BYTES}, &mode));
    return mode;
}
} // namespace

std::optional<bool> WidescreenSetting() {
    if (launcherSetting)
        return launcherSetting;
    // Read at first use.
    static const auto environment = []() -> std::optional<bool> {
        if (const char* value = std::getenv("MH3_WIDESCREEN"); value && *value)
            return value[0] != '0';

        return true;
    }();
    return environment;
}

void SetWidescreenSetting(bool widescreen) {
    launcherSetting = widescreen;
}

// SCGetAspectRatio: the console's IPL.AR item, unless the host sets it.
extern "C" void MH3_SCGetAspectRatio(CpuContext* cpu) {
    uint8_t ratio;

    if (const auto setting = WidescreenSetting()) {
        ratio = *setting;
    } else {
        GuestFrame frame(cpu, 0x10);
        const uint32_t value = frame[8];
        const int found = GuestCall{cpu}(0x8047846C, {value, 1}) != 0; // SCFindU8Item(&value, IPL.AR)
        ratio = mh3_sc_aspect_ratio(found, Memory::Read8(value));
    }

    cpu->gpr[3] = ratio;
}
REGISTER_NATIVE_FUNCTION(0x80478D74, MH3_SCGetAspectRatio);

std::span<const GameLanguage> GameLanguages() {
    return Languages;
}

std::optional<uint8_t> LanguageSetting() {
    if (launcherLanguage)
        return launcherLanguage;

    // Default to Japanese (0) for Japanese ROM
    return 0;
}

void SetLanguageSetting(uint8_t system) {
    launcherLanguage = system;
}

// SCGetLanguage: the console's IPL.LNG item, unless the host sets it.
extern "C" void MH3_SCGetLanguage(CpuContext* cpu) {
    if (const auto setting = LanguageSetting()) {
        cpu->gpr[3] = *setting;
        return;
    }

    cpu->gpr[3] = 0; // Japanese
}
REGISTER_NATIVE_FUNCTION(0x80478DC8, MH3_SCGetLanguage);

extern "C" void MH3_RenderInit(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto sda = frame.Arg(13);
    const auto customAddress = frame.Arg(3);
    mh3_render_preferences preferences{};
    preferences.progressive = uint8_t(call(0x80478EC0)); // SCGetProgressiveMode
    Memory::Write8(sda - 0x4720, preferences.progressive);
    preferences.widescreen = uint8_t(call(0x80478D74)); // SCGetAspectRatio
    Memory::Write8(sda - 0x471F, preferences.widescreen);

    // The game's 16:9 picture is anamorphic: the EFB stays 640x448 and the
    // display stretches it. Its cameras take the displayed picture's aspect.
    Memory::WriteFloat32(WideAspect, NativeWidescreenAspect());
    preferences.component_cable = uint8_t(call(0x8043BA10)); // VIGetComponentCableStatus
    Memory::Write8(sda - 0x471D, preferences.component_cable);
    call(0x8043B9B0); // VIInit
    preferences.tv_format = uint8_t(call(0x8043B950)); // VIGetTvFormat
    Memory::Write8(sda - 0x471E, preferences.tv_format);

    std::array<mh3_render_mode, 3> defaults{};

    for (size_t index = 0; index < defaults.size(); ++index)
        defaults[index] = Read(Defaults[index]);

    mh3_render_configuration configuration{};
    configuration.mode = Read(ModeAddress);
    mh3_render_mode custom{};

    if (customAddress)
        custom = Read(customAddress);

    const auto status = mh3_render_mode_select(&preferences, defaults.data(), customAddress ? &custom : nullptr, &configuration);

    if (status == MH3_RES_UNSUPPORTED) {
        cpu->cr &= ~(1u << (31 - 6)); // integer-only variadic panic
        call(0x804241EC); // OSPanic
    }

    Checked(status);
    Checked(mh3_render_mode_store({Memory::GetPointer(ModeAddress, MH3_RENDER_MODE_BYTES), MH3_RENDER_MODE_BYTES}, &configuration.mode));

    Memory::Write32(sda + uint32_t(ModePointer), ModeAddress);
    Memory::Write8(sda - 0x4713, configuration.widescreen);
    Memory::Write16(sda - 0x4712, configuration.mode.vi_x_origin);
    Memory::Write16(sda - 0x4710, configuration.mode.vi_y_origin);
    SetNativeDisplayWidescreen(configuration.widescreen);
}
REGISTER_NATIVE_FUNCTION(0x80032160, MH3_RenderInit);

extern "C" void MH3_ApplyWidemode(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x60);
    const auto wide = uint8_t(frame.Arg(3));
    const auto sda = frame.Arg(13);

    if (wide == Memory::Read8(sda - 0x4713))
        return;

    mh3_render_preferences preferences{};
    preferences.progressive = Memory::Read8(sda - 0x4720);
    preferences.tv_format = Memory::Read8(sda - 0x471E);
    preferences.widescreen = wide;
    std::array<mh3_render_mode, 3> defaults{};

    for (size_t i = 0; i < defaults.size(); ++i)
        defaults[i] = Read(Defaults[i]);

    const auto address = Memory::Read32(sda + uint32_t(ModePointer));
    auto mode = Read(address);
    const auto status = mh3_render_mode_widescreen(&preferences, defaults.data(), &mode);

    if (status == MH3_RES_UNSUPPORTED)
        return;

    Checked(status);

    // Publish only the two fields written by the original caller.
    Memory::Write16(address + 14, mode.vi_width);
    Memory::Write16(address + 10, mode.vi_x_origin);
    GuestCall{cpu}(0x8043ABEC, {address}); // VIConfigure

    // Update ScreenRecord structure (0x807793F8)
    Memory::Write16(ScreenRecord + 0x00, 640);
    Memory::Write16(ScreenRecord + 0x02, 448);
    const float width = Memory::ReadFloat32(wide == 1 ? 0x808A13C0 : 0x808A13CC);
    const float height = Memory::ReadFloat32(0x808A13C4);
    const float left = Memory::ReadFloat32(0x808A13D8);
    const float top = Memory::ReadFloat32(0x808A13DC);
    Memory::WriteFloat32(ScreenRecord + 0x04, width);
    Memory::Write32(ScreenRecord + 0x0C, Memory::Read32(wide == 1 ? WideAspect : 0x808A13D0));
    Memory::Write16(ScreenRecord + 0x3C, uint16_t(int32_t(width)));
    Memory::Write16(ScreenRecord + 0x3E, Memory::Read16(address + 6));
    Memory::Write8(ScreenRecord + 0x19, wide == 1);
    Memory::WriteFloat32(ScreenRecord + 0x08, height);
    Memory::WriteFloat32(ScreenRecord + 0x2C, left);
    Memory::WriteFloat32(ScreenRecord + 0x30, top);
    Memory::WriteFloat32(ScreenRecord + 0x34, width - left);
    Memory::WriteFloat32(ScreenRecord + 0x38, height - top);
    Memory::Write8(sda - 0x4713, wide);
    SetNativeDisplayWidescreen(wide == 1);
}
REGISTER_NATIVE_FUNCTION(0x800323E4, MH3_ApplyWidemode);
