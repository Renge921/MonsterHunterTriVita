#pragma once
// Whether the character creation screen (charmake) owns the frame.
//
// RMHJ08: charmake_init (0x8028800C) stores its work pointer at r13 - 0x4500;
// with r13 = 0x808A4880 that is 0x808A0380. charmake_move (0x8028903C) and the
// parameter accessors read the same slot, so once the screen opens the slot
// holds the work pointer. (The PAL build's slot, r13 - 0x4500 with
// r13 = 0x8079B2E0, is not read here: this runtime only loads RMHJ08.)
//
// The game leaves the pointer set after the screen closes, so this answers
// "character creation has run", which is all the render bypasses below need:
// they only ever draw more than the original would.
#include "memory.h"

inline bool MH3CharacterCreationActive() {
    return Memory::Read32(0x808A0380) != 0;
}
