#include "abi_bridge.h"
#include "ios_host.h"
#include "memory.h"

extern "C" void func_8059596C(CpuContext*);

// MH3's keyboard setup spins directly on a stack completion flag after this
// SDK call (0x805DD4A4 calls 0x805947FC which calls 0x8059596C), with no scheduler/device-service call in the loop.
// Note: func_8059596C clobbers r5 (sets r5 = 0x3ad8), so capture the completion flag address BEFORE calling it!
// Deliver real queued callbacks and explicitly ensure the completion flag is set so the caller's tight spin loop exits.
extern "C" void MH3_HIDInitAsync(CpuContext* cpu) {
    const uint32_t sp = cpu->gpr[1];
    const uint32_t flag = cpu->gpr[5] ? cpu->gpr[5] : (sp + 8);
    func_8059596C(cpu);
    PumpIosCompletions(cpu);
    PumpIosCompletions(cpu);
    if (flag && Memory::Contains(flag, 4)) {
        Memory::Write32(flag, 1); // 1 = success in callback func_805dd488
    }
    if (sp && Memory::Contains(sp + 8, 4)) {
        Memory::Write32(sp + 8, 1);
    }
    if (cpu->gpr[1] && Memory::Contains(cpu->gpr[1] + 8, 4)) {
        Memory::Write32(cpu->gpr[1] + 8, 1);
    }
}
REGISTER_NATIVE_FUNCTION(0x805947FC, MH3_HIDInitAsync);

extern "C" void func_80595B04(CpuContext*);

// Keyboard teardown has the matching stack-flag wait at 0x805DD66C (calls 0x80594800 which calls 0x80595B04).
extern "C" void MH3_HIDCloseAsync(CpuContext* cpu) {
    const uint32_t sp = cpu->gpr[1];
    const uint32_t flag = cpu->gpr[4] ? cpu->gpr[4] : (sp + 8);
    func_80595B04(cpu);
    PumpIosCompletions(cpu);
    PumpIosCompletions(cpu);
    if (flag && Memory::Contains(flag, 4)) {
        Memory::Write32(flag, 1);
    }
    if (sp && Memory::Contains(sp + 8, 4)) {
        Memory::Write32(sp + 8, 1);
    }
    if (cpu->gpr[1] && Memory::Contains(cpu->gpr[1] + 8, 4)) {
        Memory::Write32(cpu->gpr[1] + 8, 1);
    }
}
REGISTER_NATIVE_FUNCTION(0x80594800, MH3_HIDCloseAsync);

