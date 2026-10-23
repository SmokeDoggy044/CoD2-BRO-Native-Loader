#pragma once

// Client-side engine code (Q3's cl_*.c, the input layer under it): pads, key states, user commands.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "dobj.h"

namespace game {

// The pads: four 0x58-byte blocks at 0x00902400 (+2..+9 analog button bytes, +0xb/+0xd/+0xf/+0x11 stick high
// bytes, +0x12 digital buttons, +0x1c/+0x26 pressed/released edge words with the analog edges in the bytes after,
// +0x4c the XInput handle). Button numbers 0-15: A B X Y (bits 4 2 8 1, then analog), the analog bytes, then bits
// 0x80 0x40 0x10 0x20. The byte at 0x00902560 blocks pad input.

// 0x000560e0: whether the pad has an XInput handle.
uint8_t XB_STDCALL IN_PadIsConnected(int32_t pad);
// 0x00056100: 0xff when the button is held (analog buttons: the byte's value); 0 while pad input is blocked.
uint32_t XB_STDCALL IN_PadButtonDown(int32_t pad, uint32_t button);
// 0x00056580: whether the button went down since the edges were last cleared.
uint8_t XB_STDCALL IN_PadButtonPressed(int32_t pad, uint32_t button);
// 0x00056720 / 0x000562d0: takes the button's pressed / released edge (clears it); whether it was set.
uint8_t XB_STDCALL IN_PadConsumePressed(int32_t pad, uint32_t button);
uint8_t XB_STDCALL IN_PadConsumeReleased(int32_t pad, uint32_t button);
// 0x000569c0: the left (stick 0) or right stick's high bytes, y negated; zeros while pad input is blocked.
void XB_STDCALL IN_PadGetStick(int32_t pad, int32_t stick, int32_t* x, int32_t* y);

// Q3's kbutton_t (0x18 bytes): down[2] key numbers, downtime, msec, active, wasPressed. The command buttons are 15
// of them at 0x00934020 (in_buttons).

// 0x002abed0 (key in EAX; result in XMM0): Q3's CL_KeyState - the fraction of the frame (frame_msec, unsigned)
// the key was down, msec / frame_msec on the x87, clamped to 0..1 (NaN passes the clamps). It touches only XMM0
// and XMM1 (the unclamped fraction, unless negative), and the title's callers keep constants in XMM2-XMM6 across
// it (CL_KeyMove, CL_AdjustAngles): the asm entry saves them (CL_KeyState_Raw hands the XMM1 value back).
extern "C" PLATFORM_HIDDEN float XB_CDECL CL_KeyState(uint8_t* key);
extern "C" PLATFORM_HIDDEN float XB_CDECL CL_KeyState_Raw(uint8_t* key, float* xmm1);
extern "C" void CL_KeyState_entry(void);
// 0x002abfe0 / 0x002abf50 (key in ESI): Q3's IN_KeyDown / IN_KeyUp - the key number from argv[1] (-1 / clear
// both when there is none), the down/up time from argv[2].
extern "C" PLATFORM_HIDDEN void XB_CDECL IN_KeyDown(uint8_t* key);
extern "C" void IN_KeyDown_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL IN_KeyUp(uint8_t* key);
extern "C" void IN_KeyUp_entry(void);
// 0x002abc30 (command in EAX): Q3's CL_CmdButtons - the 16-bit button word at +4 from in_buttons 0-6 and 8-14
// (active or pressed since the last command; clears wasPressed), bit 7 when 0x0092eff0 is set and no key catcher,
// bit 10 from 0x00961770. EAX is never written: CL_CreateCmd reads the command back from it.
uint8_t* XB_REGS1 XB_CDECL CL_CmdButtons(uint8_t* cmd);
// 0x002abb30 (command in EDX): Q3's CL_FinishMove - server time, weapon byte, the view angles as 16-bit
// ANGLE2SHORT (x 182.044, + 0.5, truncated), five movement floats at +0x18 and the pending byte overrides at
// +4..+6 (0x009622b0 set; cleared). EDX is kept (CL_CreateCmd copies the command from it): asm entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_FinishMove(uint8_t* cmd);
extern "C" void CL_FinishMove_entry(void);
// 0x002ac630 (command number in EAX, out in EDX): Q3's CL_GetUserCmd - __break 1 past cl_cmdNumber; 0 when the
// command left the 64-entry backup (or the number is not positive), else copies the 0x2c bytes and returns 1.
// EDX is kept (CG_InterpolatePlayerState reads the command from it): asm entry.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CL_GetUserCmd(int32_t number, uint8_t* out);
extern "C" void CL_GetUserCmd_entry(void);
// 0x002ac510 (number in ECX, out in EDX): Q3's CL_GetSnapshot - __break 1 past the latest message number
// (0x0096016c); only the latest snapshot is kept: 0 for older numbers, an invalid snapshot (0x009622d0) or one whose
// entities left the 0x800-entry parse buffer (0x00961738 - 0x00962814); else copies flags, server time, the entity
// count (+0x538) and the 0x520-byte block from 0x009622f0 to +0x10, and returns 1.
int32_t XB_REGS3 XB_CDECL CL_GetSnapshot(uint32_t unused_eax, uint8_t* out, int32_t number);
// 0x002b3e60 (command in ECX): Q3's CL_JoystickMove - the walking bit 0x10 (cvar 0x00929c04 2, or not 1 and
// cl_run equal to in_speed), the joystick axes 0x00961750/54/58 added to rightmove/forwardmove/upmove with
// ClampChar; while mlook (0x009341b0) is active the pitch axis turns cl.viewangles[0] by cls.frametime x
// cl_anglespeedkey (in_speed) x 0.001 x cl_pitchspeed instead.
void XB_REGS3 XB_CDECL CL_JoystickMove(uint32_t unused_eax, uint32_t unused_edx, uint8_t* cmd);
// 0x002b3f40 (command in ESI): Q3's CL_KeyMove - the stance bits of byte +5 (0x40 crouch / 0x80 prone from
// in_buttons[14] / the down key (0x00933ff0) held, with 0x02, else from cvar 0x00927b20 without it), the walking bit,
// then forwardmove/rightmove/upmove from the key fractions x 127 (strafe turns left/right into side moves; the jump
// key 0x00934008 counts only in pm_type 2/3 or with flags 0x100000 without 0x400000; crouch/prone push up down
// by 127). Nothing is stored for the moves while the player flags (0x00960240) have 0x6000.
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_KeyMove(uint8_t* cmd);
extern "C" void CL_KeyMove_entry(void);
// 0x002b39e0 (command in EDI): Q3's CL_MouseMove. The mouse delta (m_filter 0x00935110: the average of both
// frames' 0x0096173c/0x00961744, halved), then with cvar 0x009284dc either a flick boost (|x| > 110, 0x00960680 and
// 0x00960668 permitting, scale 0x00960208 not 1: the boost 0x00961780 grows by 0x008fef40 per second and adds
// to x with x's sign) or, at scale 1, a ramp (0x00961780 grows while moving, shrinks at rest, 0..0x008fef3c,
// scales both axes), times the frame time; without it x 0.2. The sensitivity (cvar 0x0092f5e8 x speed, the
// length over frame_msec via the title's __CIsqrt, plus the 0.04 x 0x008dce74/78 floors, 0.1 when 0) x
// 0x00961774 then gives the moves: side move with in_strafe, else a yaw turn clamped to frame_msec x 0.001 x
// 0x0096177c (0x008fef38 when 0); a pitch turn with mlook (clamped by 0x00961778 when set), else forward move.
// Nothing moves when the player flags allow neither (0x009601ac 0x4000 without 0x00960240 0x106000).
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_MouseMove(uint8_t* cmd);
extern "C" void CL_MouseMove_entry(void);
// 0x002c7a90 (out in EBX; returns it): Q3's CL_CreateCmd - CL_AdjustAngles, then unless cvar 0x00934a5c is set
// the buttons, key/mouse/joystick moves, the pitch/yaw compensation cvars 0x0092f54c / 0x0093ef60 (added once,
// then "cl_viewPitchCompensate" / "cl_viewYawCompensate" set to "0") and the 90-degree pitch change clamp;
// CL_FinishMove; copied to out. The loader's PC mouse patch replaces the CL_MouseMove call here (0x002c7ad7):
// it has to move into this function before the module is Replaced.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CL_CreateCmd(uint8_t* out);
extern "C" void CL_CreateCmd_entry(void);
// 0x002cd450: Q3's CL_CreateNewCommands - while connected (cls.state 2): frame_msec = com_frameTime - the last
// frame time (at most 100, unsigned), the next command number, CL_CreateCmd into cl.cmds[number & 63].
void XB_CDECL CL_CreateNewCommands();
// 0x002b4170: Q3's CL_AdjustAngles. With cvar 0x00929c28 set, an accelerated turn: per axis a target speed (-120 /
// 120 from right/left and the two pitch keys, the first key winning; x cl_anglespeedkey with in_speed), and the
// speeds 0x0098b800 move towards it by cls.frametime x 0.001 x (220 from rest, else 70) x clamp(pow(|speed -
// target| / 120, 5), 0.5, 5) (the title's x87 __CIpow, called again for each clamp test) without passing it;
// yaw and pitch then turn by frametime x 0.001 x speed. Otherwise Q3's: cl_yawspeed / cl_pitchspeed x the key
// fractions x frametime (x cl_anglespeedkey) x 0.001, yaw only without in_strafe.
void XB_CDECL CL_AdjustAngles();

// 0x002ab980 (key number in EAX, translate in EDI): Q3's Key_KeynumToString - "<KEY NOT FOUND>" for -1, "<OUT OF
// RANGE>" outside 0-255, the digit names (0x008d3858) when translating with cvar 0x0093f7fc at 1, one upper-case
// character for printable keys (';' only when translating), the key name tables (0x008d3518 translated,
// 0x008d30b8), else "0x" and two hex digits (buffer 0x0098b5c8).
extern "C" PLATFORM_HIDDEN const char* XB_CDECL Key_KeynumToString(int32_t keynum, uint32_t translate);
extern "C" void Key_KeynumToString_entry(void);
// 0x002a9610 (character in EAX): 1 for '0'-'9', else 0.
int32_t XB_REGS1 XB_CDECL Char_IsDigit(int32_t c);
// 0x002b3490 (text in EDI): Q3's Key_StringToKeynum - -1 for null or "", a single character's code, "0x" and two
// hex digits (other characters count 0), else the untranslated key name table (Q_stricmpn), -1 when not found.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Key_StringToKeynum(const char* str);
extern "C" void Key_StringToKeynum_entry(void);
// 0x00319b40 (menu item in EBX): for every key bound to "+activate" / "toggle cl_run" / "+speed" (Q_stricmpn),
// the translated key name into the item's name buffer +0x48 / +0x4c / +0x50 that is still empty (_strncpy 7
// bytes, NUL-terminated); then +0x54 = 1.
extern "C" PLATFORM_HIDDEN void XB_CDECL Key_GetKeyNameForCommand(uint8_t* item);
extern "C" void Key_GetKeyNameForCommand_entry(void);
// 0x002d57c0: Q3's Key_ClearStates - every key up (anykeydown 0x0092eff0), with "-cmd key 0" for held '+' bindings
// except the console keys and ESC while a menu catches keys (0x009617a8).
void XB_CDECL Key_ClearStates();

// 0x0027f8e0 (obj the second cdecl argument): a Z_Malloc of the bone matrices' size (unused), then
// DObj_CreateSkeleton unless the object has a skeleton (+0x20).
void XB_CDECL CL_DObjCreateSkeleton(uint32_t unused, DObj* obj);
// 0x0027fa50 (name hash in EAX, obj in EDI): the tag's bone matrix (skeleton + 0x30 + 0x40 x (bone + the object's
// bone offset +0x60), made first when missing); null for an unknown tag.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CL_DObjGetBoneMatrix(uint32_t name, DObj* obj);
extern "C" void CL_DObjGetBoneMatrix_entry(void);

}  // namespace game
