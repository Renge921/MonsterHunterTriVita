// RMHP08 frame boundaries, reconstructed from FrameSetup/DisplayCopy.
// Native rendering replaces viewport setup, cache invalidation, EFB display
// copying and GXDrawDone. The backend submits/presents without a GPU wait.
#include "abi_bridge.h"
#include "fatal.h"
#include "guest_call.h"
#include "memory.h"
#include "native_graphics.h"
#include "ios_host.h"
#include "audio/native_audio.h"
#include "runtime/native/adapters/model_pose_bridge.h"
#include "village_boot.h"
#include <stdexcept>

// GXInit only created the FIFO and initial GPU state, then cleared/copied both
// old XFBs. Aurora owns the actual display surfaces; native frame begin sets
// viewport and clears the color/depth target once admission succeeds.
extern "C" void MH3_NativeGraphicsInit(CpuContext* cpu) {
    const auto mode = Memory::Read32(cpu->gpr[13] - 0x4738);

    if (!mode || !Memory::Contains(mode, 60))
        MH3_THROW(std::runtime_error("Native graphics startup has no render mode"));

    InitializeNativeGraphics(Memory::Read16(mode + 4), Memory::Read16(mode + 6));

    // Verified __GXInitRevisionBits enables XF DUALTEX in the original init.
    PublishNativeModelDualTexture(true);
}
REGISTER_NATIVE_FUNCTION(0x80032B0C, MH3_NativeGraphicsInit);

// Preserve the boot VI configuration, compatibility XFB bookkeeping, and
// initial retraces used by the game's timing. These calls update the emulated
// VI; they do not copy EFBs, submit GXM work, or wait for GPU results.
extern "C" void MH3_NativeDisplaySetup(CpuContext* cpu) {
    GuestFrame frame(cpu, 0x10);
    GuestCall call{cpu};
    const auto sda = frame.Arg(13);
    const auto mode = Memory::Read32(sda - 0x4738);

    if (!mode || !Memory::Contains(mode, 60))
        MH3_THROW(std::runtime_error("Native display setup has no render mode"));

    const auto index = Memory::Read32(sda - 0x4724);

    if (index > 1)
        MH3_THROW(std::runtime_error("Boot VI framebuffer index exceeds its compatibility pair"));

    call(0x8043ABEC, {mode});
    call(0x8043B738, {Memory::Read32(sda - 0x4730 + index * 4)});
    Memory::Write32(sda - 0x4724, index ^ 1);
    Memory::Write32(sda - 0x4728, Memory::Read32(sda - 0x4730 + (index ^ 1) * 4));
    call(0x8043B624);
    call(0x8043A66C);

    if (Memory::Read32(mode) & 1)
        call(0x8043A66C);
}
REGISTER_NATIVE_FUNCTION(0x80032C90, MH3_NativeDisplaySetup);

// 8003fe88 installed 8003fe84 as the GX draw-done callback. Decompilation of
// that four-byte callback confirms it returns without any application work.
// Native buffer retirement uses Aurora's notifications, so no callback is
// installed. Other calls to the SDK callback setter still hit the GX tripwire.
extern "C" void MH3_NativeCompletionSetup(CpuContext*) {}
REGISTER_NATIVE_FUNCTION(0x80032C84, MH3_NativeCompletionSetup);

void MH3StartDrawOverlap(); // game_frame.cpp

extern "C" void MH3_NativeFrameBegin(CpuContext* cpu) {
    NativeAudio::ServiceNativeAudio(cpu);
    PumpIosCompletions(cpu);
    MH3StartDrawOverlap();
    StepVillageBoot(cpu);
    BeginNativeGraphicsFrame();
}
REGISTER_NATIVE_FUNCTION(0x80032D04, MH3_NativeFrameBegin);

extern "C" void MH3_NativeFrameEnd(CpuContext* cpu) {
    NativeAudio::ServiceNativeAudio(cpu);
    PumpIosCompletions(cpu);
    if (!EndOverlappedNativeGraphicsFrame())
        EndNativeGraphicsFrame();
}
REGISTER_NATIVE_FUNCTION(0x80032DDC, MH3_NativeFrameEnd);
