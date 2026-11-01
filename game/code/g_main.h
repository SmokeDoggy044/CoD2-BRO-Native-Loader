#pragma once

// Game module: level and client setup (G_InitGame's helpers: clearing the level locals, the
// clients' player state and persistent data) and the shake/rumble manager Com_Init creates.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00024260: allocates the shake/rumble manager (0x24 bytes: a word set to 1, then two empty
// lists {head, sentinel {first, last}}) into shakeRumbleMgr (0x0095f5c4; 0 when out of memory).
void XB_CDECL ShakeRumble_CreateManager();
// 0x00026750 (player state in EAX): clears a player state (0x500 bytes): two vectors
// {0, 0, 0, 1} and {0, 0, 0, 0} (0x0082e290 / 0x0082e110) at +0 and +0x490, the rest zero
// except the padding the original skips (+0x8d..+0x8f, +0x3f8, +0x428, +0x488, +0x4ce).
uint8_t* XB_REGS1 XB_CDECL G_ClearPlayerState(uint8_t* ps);
// 0x00026ae0 (persistent data in EAX): clears a client's persistent block (0x64 bytes).
uint8_t* XB_REGS1 XB_CDECL G_ClearClientPersistant(uint8_t* pers);
// 0x00026b60 (client in ESI): G_ClearPlayerState, G_ClearClientPersistant (+0x520), the rest of
// the client (+0x584..+0x638) zero except the vector {0, 0, 0, 1} at +0x5b0. Returns 0 (EAX).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_ClearClient(uint8_t* client);
extern "C" void G_ClearClient_entry(void);
// 0x00026cc0 (level locals in ESI, 0x00985370): clears the level locals (0x2708 bytes) except
// +0xb4..+0x2b4 and the padding; the script strings +0xb50 and +0xcb8 become "".
extern "C" PLATFORM_HIDDEN void XB_CDECL G_ClearLevelLocals(uint8_t* level);
extern "C" void G_ClearLevelLocals_entry(void);

}  // namespace game
