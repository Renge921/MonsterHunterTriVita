// RMHJ08 (Monster Hunter Tri JPN) sharpness gauge color & aura enhancement.
// Hooks 0x8019BD28 (1P/2P sharpness icon + glow draw) so that all 7 sharpness
// tiers (red, orange, yellow, green, blue, white, purple) visibly wrap the
// blade icon in their corresponding sharpness color with a subtle pulsing glow,
// similar to MH Portable 3rd and later titles.
#include "abi_bridge.h"
#include "guest_call.h"
#include "memory.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace {

// RMHJ08 addresses verified from main.dol disassembly of 0x8019BD28:
constexpr uint32_t kBodySpriteTable = 0x806AB3FCu; // [mode][7] of u16 (mode0: 0x0BA9..0x0BA7, mode1: 0x0CF4..0x0CF2)
constexpr uint32_t kAuraSpriteTable = 0x806AB438u; // [mode] of { u32 auraId, u32 skillFxId } (0x0BAA/0x0BAB, 0x0CF5/0x0CF6)

constexpr uint32_t kGetHudSprite = 0x801B4308u;     // (u16 id, s16* outPos) -> sprite*
constexpr uint32_t kDrawHudSprite = 0x801B45D4u;    // (const sprite*, const s16* offset)
constexpr uint32_t kDrawHudSpriteById = 0x801B4728u; // (u16 id, const s16* offset)
constexpr uint32_t kDrawHudAnimSprite = 0x801B4DA8u; // (u16 id, u16 frame, const s16* offset)
constexpr uint32_t kSceneBlendMode = 0x802AF1D4u;   // (srcFactor, dstFactor, blendMode)
constexpr uint32_t kCheckPlayerFlag = 0x80348F34u;  // (plw, flag)

constexpr uint32_t kPlwSharpnessLevel = 0x56Bu; // u8: 0=red, 1=orange, 2=yellow, 3=green, 4=blue, 5=white, 6=purple
constexpr uint32_t kHudFrameCounter = 0x190u;   // u32
constexpr uint32_t kSpriteSizeBytes = 0x24u;
constexpr uint32_t kSpriteColorOffset = 0x1Cu;
constexpr uint32_t kSpriteScaleXOffset = 0x0Cu;
constexpr uint32_t kSpriteScaleYOffset = 0x0Eu;

struct SharpnessStyle {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t backAlphaMin;
    uint8_t backAlphaMax;
    uint8_t tintAlphaMin;
    uint8_t tintAlphaMax;
    uint8_t glowAlphaMin;
    uint8_t glowAlphaMax;
};

// Vivid 3rd/4th-gen inspired palette covering all 7 tiers (0=red .. 6=purple).
// Stock MH3 had 0x00000000 for orange (1) and yellow (2), leaving them uncolored.
constexpr SharpnessStyle kSharpnessStyles[7] = {
    {0xFF, 0x20, 0x18, 150, 210,  75, 110,  80, 165}, // 0: Red
    {0xFF, 0x70, 0x10, 155, 215,  80, 115,  75, 155}, // 1: Orange
    {0xFF, 0xD8, 0x14, 155, 215,  80, 115,  70, 150}, // 2: Yellow
    {0x38, 0xDC, 0x20, 150, 210,  75, 110,  80, 165}, // 3: Green
    {0x24, 0x78, 0xFF, 155, 215,  80, 115,  85, 175}, // 4: Blue
    {0xF0, 0xF6, 0xFF, 135, 195,  55,  90,  95, 190}, // 5: White
    {0xC8, 0x30, 0xFF, 155, 215,  80, 115,  85, 175}, // 6: Purple
};

uint32_t PackRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | uint32_t(a);
}

uint8_t LerpU8(uint8_t a, uint8_t b, float t) {
    const float v = float(a) + (float(b) - float(a)) * t;
    if (v <= 0.0f)
        return 0;
    if (v >= 255.0f)
        return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

uint16_t ScaleU16(uint16_t value, float factor) {
    const float scaled = float(value) * factor;
    if (scaled >= 65535.0f)
        return 65535u;
    return static_cast<uint16_t>(scaled + 0.5f);
}

void CopySpriteToGuest(uint32_t dst, uint32_t src) {
    for (uint32_t off = 0; off < kSpriteSizeBytes; off += 4)
        Memory::Write32(dst + off, Memory::Read32(src + off));
}

#ifdef MH3_DEVELOPMENT
int TestSharpnessOverride() {
    static int cached = -2;
    if (cached == -2) {
        cached = -1;
        if (const char* env = std::getenv("MH3_TEST_SHARPNESS")) {
            char* end = nullptr;
            const long v = std::strtol(env, &end, 10);
            if (end != env && v >= 0 && v <= 6)
                cached = static_cast<int>(v);
        }
    }
    return cached;
}
#endif

} // namespace

extern "C" void MH3_DrawSharpnessGauge(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x60);
    GuestCall call{cpu};

    const uint32_t hudCtx = frame.Arg(3);
    const uint32_t posPtr = frame.Arg(4);
    const uint32_t mode = frame.Arg(5) ? 1u : 0u;

    const uint32_t plw = hudCtx ? Memory::Read32(hudCtx) : 0u;
    if (!plw)
        return;

    uint32_t level = Memory::Read8(plw + kPlwSharpnessLevel);
#ifdef MH3_DEVELOPMENT
    if (const int overrideLevel = TestSharpnessOverride(); overrideLevel >= 0)
        level = static_cast<uint32_t>(overrideLevel);
#endif
    if (level > 6u)
        level = 6u;

    // Look up the dedicated blade-glow sprite (0x0BAA for 1P, 0x0CF5 for 2P).
    const uint32_t auraEntryAddr = kAuraSpriteTable + mode * 8u;
    const uint32_t auraSpriteId = Memory::Read32(auraEntryAddr) & 0xFFFFu;
    const uint32_t srcSprite = call(kGetHudSprite, {auraSpriteId, 0u});

    const uint32_t frameCounter = Memory::Read32(hudCtx + kHudFrameCounter);
    const float phase = static_cast<float>(frameCounter & 0x7Fu) * (6.283185307179586f / 128.0f);
    const float pulse = 0.5f + 0.5f * std::sin(phase);
    const SharpnessStyle& style = kSharpnessStyles[level];
    const uint32_t spriteBuf = frame[0x10];

    // 1. Back aura: slightly expanded colored halo behind the blade so the
    //    sharpness color clearly wraps the outline without obscuring it.
    if (srcSprite) {
        CopySpriteToGuest(spriteBuf, srcSprite);
        const uint16_t origScaleX = Memory::Read16(srcSprite + kSpriteScaleXOffset);
        const uint16_t origScaleY = Memory::Read16(srcSprite + kSpriteScaleYOffset);
        Memory::Write16(spriteBuf + kSpriteScaleXOffset, ScaleU16(origScaleX, 1.07f));
        Memory::Write16(spriteBuf + kSpriteScaleYOffset, ScaleU16(origScaleY, 1.15f));
        const uint8_t backAlpha = LerpU8(style.backAlphaMin, style.backAlphaMax, pulse);
        call(kSceneBlendMode, {4u, 5u, 1u}); // SRCALPHA, INVSRCALPHA, BLEND
        Memory::Write32(spriteBuf + kSpriteColorOffset, PackRgba(style.r, style.g, style.b, backAlpha));
        call(kDrawHudSprite, {spriteBuf, posPtr});
    }

    // 2. Base blade icon (preserves sharp black outlines, handle detail, and
    //    chipped blade shapes for red/orange).
    const uint32_t bodySpriteId = Memory::Read16(kBodySpriteTable + mode * 14u + level * 2u);
    call(kDrawHudSpriteById, {bodySpriteId, posPtr});

    // 3. Front tint + additive pulsing glow on the blade itself.
    if (srcSprite) {
        CopySpriteToGuest(spriteBuf, srcSprite);

        // Subtle alpha tint so the blade body carries the sharpness hue even
        // against bright sky/water/snow backgrounds while keeping outlines crisp.
        const uint8_t tintAlpha = LerpU8(style.tintAlphaMin, style.tintAlphaMax, pulse);
        call(kSceneBlendMode, {4u, 5u, 1u}); // SRCALPHA, INVSRCALPHA, BLEND
        Memory::Write32(spriteBuf + kSpriteColorOffset, PackRgba(style.r, style.g, style.b, tintAlpha));
        call(kDrawHudSprite, {spriteBuf, posPtr});

        // Additive pulsing glow matching stock MH3's luminous style across all tiers.
        const uint8_t glowAlpha = LerpU8(style.glowAlphaMin, style.glowAlphaMax, pulse);
        Memory::Write32(spriteBuf + kSpriteColorOffset, PackRgba(style.r, style.g, style.b, glowAlpha));
        call(kSceneBlendMode, {4u, 1u, 1u}); // SRCALPHA, ONE, BLEND (additive)
        call(kDrawHudSprite, {spriteBuf, posPtr});

        // Restore standard HUD alpha blending.
        call(kSceneBlendMode, {4u, 5u, 1u});
    }

    // 4. Preserve original sharpness-protection / skill animation overlay.
    if (call(kCheckPlayerFlag, {plw, 0x300u}) != 0u) {
        const uint32_t fxSpriteId = Memory::Read32(kAuraSpriteTable + mode * 8u + 4u) & 0xFFFFu;
        call(kDrawHudAnimSprite, {fxSpriteId, frameCounter & 0xFFFFu, posPtr});
    }
}
REGISTER_NATIVE_FUNCTION(0x8019BD28, MH3_DrawSharpnessGauge);
