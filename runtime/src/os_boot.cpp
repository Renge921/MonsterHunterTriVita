#include "abi_bridge.h"
#include "fatal.h"
#include "memory.h"
#include <stdexcept>

// RMHJ08 OSExceptionInit, verified from main.dol at 0x80421C48.
// The original copies the PPC exception vectors and debugger trampoline into
// low memory. Native execution uses host exceptions instead. Preserve the
// guest handler table consumed by __OSSet/GetExceptionHandler, but do not
// install or execute hardware vector code.
extern "C" void MH3_OSExceptionInit(CpuContext*) {
    constexpr uint32_t table = 0x80003000;
    constexpr uint32_t defaultHandler = 0x80421FB0;
    Memory::Write32(0x808A0728, table); // r13 - 0x4158 (RMHJ08)
    for (uint32_t i = 0; i < 15; ++i) {
        Memory::Write32(table + i * 4, defaultHandler);
    }
}
REGISTER_NATIVE_FUNCTION(0x80421C48, MH3_OSExceptionInit);

extern "C" void MH3_OSDefaultExceptionHandler(CpuContext*) {
    MH3_THROW(std::runtime_error("MH3 invoked its default PowerPC exception handler"));
}
REGISTER_NATIVE_FUNCTION(0x80421FB0, MH3_OSDefaultExceptionHandler);

// __OSInterruptInit (0x80427F34). The guest table, masks, and external
// interrupt-vector binding are observable to the translated OS. Hollywood
// mask-register writes are replaced by host-service scheduling.
extern "C" void MH3_OSInterruptInit(CpuContext*) {
    constexpr uint32_t table = 0x80003040;
    Memory::Write32(0x808A07B8, table); // r13 - 0x40C8
    for (uint32_t i = 0; i < 32; ++i)
        Memory::Write32(table + i * 4, 0);

    Memory::Write32(0x800000C4, 0xFFFFFFF0);
    Memory::Write32(0x800000C8, 0);
    Memory::Write32(0x80003010, 0x804285F8); // exception 4: __OSDispatchInterrupt
}
REGISTER_NATIVE_FUNCTION(0x80427F34, MH3_OSInterruptInit);

// SetInterruptMask receives the remaining hardware groups in r3 and effective
// mask in r4. The translated callers already store the OS's mask in low RAM.
// Host services consult that state; there are no physical masks to program.
// Returning no remaining groups completes the callers' programming loop.
extern "C" void MH3_SetInterruptMask(CpuContext* ctx) {
    ctx->gpr[3] = 0;
}
REGISTER_NATIVE_FUNCTION(0x80427FF8, MH3_SetInterruptMask);

// __OSInitMemoryProtection configures MI registers and PPC BAT mappings.
// Host RAM access is already checked by memory.cpp. Retain the guest interrupt
// bindings and shutdown-list registration, which are independently observable.
extern "C" void MH3_OSInitMemoryProtection(CpuContext* ctx) {
    if (Memory::Read32(0x808A07C0)) // r13 - 0x40C0
        return;

    Memory::Write32(0x800000C4, Memory::Read32(0x800000C4) | 0xF0000000);

    for (uint32_t i = 0; i < 5; ++i)
        Memory::Write32(0x80003040 + i * 4, 0x80428994);

    ctx->gpr[3] = 0x806F0998;
    InvokeIndirectCpu(0x80429418, ctx); // OSRegisterShutdownFunction
    Memory::Write32(0x800000C4, Memory::Read32(0x800000C4) & ~0x08000000u);
    Memory::Write32(0x808A07C0, 1);
}
REGISTER_NATIVE_FUNCTION(0x80428FA0, MH3_OSInitMemoryProtection);

// OSGetResetCode: retain the SDK's saved restart code, otherwise report a
// cold host launch instead of reading the physical PI reset-code register.
extern "C" void MH3_OSGetResetCode(CpuContext* ctx) {
    ctx->gpr[3] = Memory::Read32(0x807BBA80) ? Memory::Read32(0x807BBA84) | 0x80000000u : 0;
}
REGISTER_NATIVE_FUNCTION(0x80429DFC, MH3_OSGetResetCode);

// OSGetConsoleType (0x8042205C): Return retail Wii console type (0)
// instead of reading the physical DI console type register 0xCD006024.
extern "C" void MH3_OSGetConsoleType(CpuContext* ctx) {
    ctx->gpr[0] = 0;
    ctx->gpr[3] = 0;
}
REGISTER_NATIVE_FUNCTION(0x8042205C, MH3_OSGetConsoleType);
