// Native character material preparation, RMHP08 (0x80047DD0).
// Source: Ghidra decompilation plus frame150 producer / frame690 consumer trace.
// The game previously rendered into two EFB rectangles then copied each to
// RGBA8. Publish direct GPU render targets on the game thread. No EFB copy,
// GXDrawDone, content hash, CPU composition, or readback is involved.
#include "abi_bridge.h"
#include "fatal.h"
#include "guest_call.h"
#include "gx_host.h"
#include "native_graphics.h"
#include "../native/mh3_model_state.h"
#include "memory.h"
#include <aurora/generated_texture.hpp>
#include <aurora/native_assets.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
aurora::MaterialSource Snapshot(uint32_t object) {
    const uint32_t image = Memory::Read32(object + 8);

    // GX texture image addresses are physical; host memory reads take the
    // cached alias (0x80000000 + MEM1 or MEM2 offset): on the Vita a release
    // build's Read32 is one add from the cached base (HostCached), and a
    // physical address faulted there (retail, loading).
    const uint32_t address = ((Memory::Read32(object + 12) & 0xffffffu) << 5) | 0x80000000u;
    aurora::MaterialSource source;
    source.width = (image & 1023) + 1;
    source.height = ((image >> 10) & 1023) + 1;
    source.format = (image >> 20) & 15;

    if (source.width != 128 || source.height != 128 || (source.format >= 8 && source.format <= 10))
        MH3_THROW(std::runtime_error("Unexpected character material source: " + std::to_string(source.width) + "x" + std::to_string(source.height) + " format " + std::to_string(source.format)));
#ifdef __vita__
    if (address >= 64 && Memory::Contains(address - 64, 64)) {
        const uint32_t header = address - 64, tag = Memory::Read32(header + 60);

        if ((tag & 0xf0000000u) == AURORA_NATIVE_TEXTURE_TAG && Memory::Read32(header) == 0x54455830 && Memory::Read32(header + 16) == 64 && Memory::Read16(header + 28) == 128 &&
            Memory::Read16(header + 30) == 128 && Memory::Read32(header + 32) == source.format)
            source.nativeAsset = tag & 0x0fffffffu;
    }
#endif
    if (!source.nativeAsset) {
        uint32_t bytes = 128 * 128;

        switch (source.format) {
        case 0:
        case 14:
            bytes /= 2;
            break;
        case 1:
        case 2:
            break;
        case 3:
        case 4:
        case 5:
            bytes *= 2;
            break;
        case 6:
            bytes *= 4;
            break;
        default:
            MH3_THROW(std::runtime_error("Unsupported character material source format"));
        }

        const auto* data = Memory::GetPointer(address, bytes);
        source.bytes.assign(data, data + bytes);
    }

    return source;
}

void PrepareSlot(uint32_t slot) {
    const uint32_t base = Memory::Read8(0x8073D9D0 + slot);
    const uint32_t variant = Memory::Read8(0x8073D5A5 + slot);
    const int detail = int8_t(Memory::Read8(0x8073DA42 + slot));
    const uint32_t patch = Memory::Read8(0x8073DA4C + slot);

    // Preserve the game's float computation and truncation to GXColor alpha (0..100 -> 0..255).
    const float opacity = float(Memory::Read8(0x8073D9DE + slot)) / 100.0f;
    const uint8_t alpha = uint32_t(255.0f * opacity) & 255;

    for (uint32_t output = 0; output < 2; ++output) {
        aurora::MaterialComposition material;
        const uint32_t texture = 0x8073D5B0 + (base + output * 4) * 32;
        material.sources[0] = Snapshot(texture);
        material.sources[1] = Snapshot(texture + 32);
        material.opacity = float(alpha) / 255.f;
        material.optionalOverlay = output == 1 && detail > 0;

        if (material.optionalOverlay)
            material.sources[2] = Snapshot(0x8073D6B0 + (uint32_t(detail) + variant * 12) * 32);

        material.atlas = output == 1;

        if (material.atlas) {
            material.sources[3] = Snapshot(0x8073D6B0);
            material.atlasX = (patch & 3) * 32;
            material.atlasY = (patch >> 2) * 16 + variant * 64;

            if (material.atlasY > 112)
                MH3_THROW(std::runtime_error("Character material atlas region exceeds source"));
        }

        const uint32_t destination = Memory::Read32((output ? 0x8073DA10 : 0x8073D9E8) + 4 * slot);
        const void* data = Memory::GetPointer(destination, 128 * 128 * 4);
        SubmitHostGraphicsTask([owner = slot * 2 + output, data, material = std::move(material)]() mutable { aurora::publish_generated_material(owner, data, std::move(material)); });
    }
#ifdef MH3_DEVELOPMENT
    std::fprintf(stderr, "MH3 material: slot %u, two direct GPU targets, alpha %u, detail %d, atlas %u/%u\n", slot, alpha, detail, patch, variant);
#endif
}
} // namespace

extern "C" void MH3_CharacterMaterials(CpuContext* ctx) {
    GuestFrame frame(ctx, 0x60);
    GuestCall game{ctx};

    if (Memory::Read8(0x8073D588) == 1) {
        for (uint32_t slot = 0; slot < 10; ++slot) {
            if (!Memory::Read8(0x8073D59B + slot) || !Memory::Read8(0x8073DA38 + slot))
                continue;
            // Keep the original non-composition state setup: it disables fog,
            // alpha testing and alpha writes before the later screen clear.
            MH3_DebugDrawSetup(ctx); // dbg_drawgraph_init
            if (NativeGraphicsFrameShouldRender()) {
                PrepareSlot(slot);
                Memory::Write8(0x8073DA38 + slot, 0);
            }
        }

        // Fresh GPU targets and explicit producer revisions replace the old
        // texture-cache invalidation. Skipped frames retain their dirty slots.
    }

    // On Wii GX, 0x801e3714..0x801e3768 drew a full-screen untextured quad (disp_beta)
    // to clear the offscreen scratch surface used during material composition.
    // On Vita, composition targets are published directly to GPU memory via aurora,
    // so invoking disp_beta here directly overwrites the main framebuffer, obliterating
    // the newly-drawn character model. Skip disp_beta on Vita, but preserve GX state invalidation.
    // Memory::Write32(frame[0x8], Memory::Read32(0x80769264));
    // game(0x802C9268, {frame[0x18], frame[0x10], frame[0x8]}); // disp_beta(pos, size, color)
    game(0x8052C0C0, {0x7ff});
    auto material = GetNativeMaterialState();
    material.indirect_count = 0;
    SetNativeMaterial(material);
}
REGISTER_NATIVE_FUNCTION(0x801E2F04, MH3_CharacterMaterials);
