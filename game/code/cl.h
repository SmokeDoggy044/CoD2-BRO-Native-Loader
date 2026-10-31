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
// 0x002ac200 (trap args): the cgame's renderer traps 0x2e-0x54 forwarded to the refexport callbacks at 0x009284f4..
// 0x00928584 (0x2e, 0x54, 0x31 (a 0 inserted as fourth argument), 0x32 return the callback's EAX; the others 0;
// 0x34 passes its eighth argument as a byte and a 0 after it); others __break 1 "Bad cgame system trap" and 0.
uint32_t XB_CDECL CL_CgameSystemCalls(const uint32_t* args);
// 0x002ab470 (x, y, w, h pointers, each may be null): scales x/w by 0x0092bf3c / 640 and y/h by 0x0092bf40 / 480;
// returns h.
float* XB_CDECL SCR_AdjustFrom640(float* x, float* y, float* w, float* h);
// 0x002ab8d0: Q3's CL_AddReliableCommand without the overflow check - the next reliable command slot gets the text
// (_strncpy 127 bytes, NUL-terminated); returns _strncpy's result.
char* XB_CDECL CL_AddReliableCommand(const char* text);
// 0x002d5690 (netadr_t by value, msg): Q3's CL_PacketEvent for the local channel - the packet time; nothing while
// disconnected; NET_CompareAdr against the channel's remote address set: NET_AdrToString only (the debug print is
// gone); else Netchan_Process (0x002b13b0, channel 0x00933608 in EAX, msg in EBX) and, when it accepts the packet,
// the message sequence from the data's first word and CL_ParseServerMessage. Returns the last call's EAX.
uint32_t XB_CDECL CL_PacketEvent(uint32_t from0, uint32_t from1, uint32_t from2, uint32_t from3, uint32_t from4,
                                 uint8_t* msg);
// 0x002cd690: Q3's CL_ConfigstringModified for "cs <index> <text>" - __break 1 outside 0-0x3ff; a changed string is
// stored in cl.configstrings[index] (script strings at 0x009606b8, text at +6; ScriptString_AssignCString), and
// index 1 (system info) runs CL_SystemInfoChanged.
void XB_CDECL CL_ConfigstringModified();
// 0x002cce00: Q3's CL_SystemInfoChanged - cl.serverId from "sv_serverid" of the system info (0x009606bc + 6, null
// when unset), Cvar_SetCheatState unless "sv_cheats", then the four pak keys looked up for nothing (they rotate
// Info_ValueForKey's buffers); returns the last lookup.
const char* XB_CDECL CL_SystemInfoChanged();
// 0x002cce80 (msg in EAX): the single-player CL_ParseSnapshot - a fresh valid snapshot with the server time
// (MSG_ReadLong), flags (an inlined MSG_ReadByte), the message number and the server command sequence, and the
// player state copied from the server's (cgame client 0x0095f534 +4, +0x23c); stored as cl.snap and its copy
// 0x009622d0. Returns the message number - (cl.snap.messageNum + 1) computed from the old snapshot.
int32_t XB_REGS1 XB_CDECL CL_ParseSnapshot(uint8_t* msg);
// 0x002d54c0 (msg in EAX): Q3's CL_ParseServerMessage for the local server - the reliable acknowledge (MSG_ReadLong,
// at least clc.reliableSequence - 64), then commands until svc_EOF 8 (__break 1 past the end): 1 nop, 5 a server
// command (inlined MSG_ReadLong, MSG_ReadString; newer ones stored in clc.serverCommands[seq & 63], 127 bytes),
// 7 CL_ParseSnapshot, others __break 1. Returns the message's data pointer (left in EAX by the last byte read).
uint8_t* XB_REGS1 XB_CDECL CL_ParseServerMessage(uint8_t* msg);
// 0x002d59f0 (command number in EAX): Q3's CL_GetServerCommand - tokenizes the stored command (last executed
// 0x00931604); "cs": re-tokenized with 3 tokens around CL_ConfigstringModified; "clientLevelShot": 0 without a
// running server, else queues the levelshot commands; returns 1.
int32_t XB_REGS1 XB_CDECL CL_GetServerCommand(int32_t number);
// 0x002b37e0: Q3's CL_WritePacket - not in cls.state 3/4 (returns it). A 0x800-byte message: server id, message and
// command sequences, the unacknowledged reliable commands (2, number, text), then the last up to 32 user commands
// since the last packet (0x009622c0) delta-coded from a null command (1 = no delta: cl_nodelta 0x00929808, no
// valid snapshot or an older one; else 0; then the count), the packet bookkeeping (0x009622c0/c4/c8), clc_EOF 3,
// and Netchan_Transmit on the local channel 0x00933608 (its result returned).
uint32_t XB_CDECL CL_WritePacket();
// 0x002ab760 (level, format, arguments): the renderer's print callback - formats into a 0x1000-byte local with the
// title's _vsprintf and drops it. The asm entry hands the format and a pointer to the arguments.
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_RefPrintf(const char* format, const uint32_t* args);
extern "C" void CL_RefPrintf_entry(void);
// 0x002e23b0: Q3's CL_InitInput - the 65 input commands (+moveup, +leanleft, raisestance, +sprint ...; table from
// the original's push sequence), then the cl_nodelta / cl_debugMove cvars ("0"); returns cl_debugMove.
uint8_t* XB_CDECL CL_InitInput();
// 0x002be180: the input cvars (in_mouse, in_joystick, in_joyBallScale, in_debugjoystick, joy_threshold - returned).
uint8_t* XB_CDECL IN_InitCvars();
// 0x002d5650: the renderer's registration start (callback 0x009284ec with 0x0092beb8), the "nglWhite" and
// "console" shaders (0x0092bf58/5c), 0x0092e848 = 1; returns the console shader.
uint8_t* XB_CDECL CL_InitRenderer();
// 0x002b4910 (EAX passes through when there is no cgame): Q3's CL_ShutdownCGame - key catcher 8 off, then with a
// cgame VM: VM_Call(CG_SHUTDOWN 1), the VM record cleared (0x8c bytes, Com_Memset's result returned), currentVM and
// cgvm null.
uint32_t XB_REGS1 XB_CDECL CL_ShutdownCGame(uint32_t eax);
// 0x002c1b80 (five cdecl words): clears the local connection block 0x00933608 (0x830 bytes), stores the words at
// +8..+0x18, the "net_qport" value truncated at +0x1c and 1 at +0x24; cls.state 5 with a cgame VM, else 1
// (returned).
int32_t XB_CDECL CL_ResetLocalConnection(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
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

// 0x002abe80: +centerview: pitch = -(the delta angle) unless the cvar at 0x00929c28 (CL_JoystickMove's pitch-target mode) is set.
void XB_CDECL IN_CenterView();
// 0x002ac060: -mlook: clears the mouse-look flag, then centers the pitch like IN_CenterView unless freelook is on.
void XB_CDECL IN_MLookUp();
// 0x002ac0b0: +mlook: sets the mouse-look flag.
void XB_CDECL IN_MLookDown();
// 0x002b4650: -leanright: IN_KeyUp on its kbutton (0x00934158).
void XB_CDECL IN_LeanRightUp();
// 0x002b4660: +leanright: IN_KeyDown on its kbutton (0x00934158).
void XB_CDECL IN_LeanRightDown();
// 0x002b4670: -leanleft: IN_KeyUp on its kbutton (0x00934140).
void XB_CDECL IN_LeanLeftUp();
// 0x002b4680: +leanleft: IN_KeyDown on its kbutton (0x00934140).
void XB_CDECL IN_LeanLeftDown();
// 0x002b4690: -reload: IN_KeyUp on its kbutton (0x00934128).
void XB_CDECL IN_ReloadUp();
// 0x002b46a0: +reload: IN_KeyDown on its kbutton (0x00934128).
void XB_CDECL IN_ReloadDown();
// 0x002b46f0: -activate: IN_KeyUp on its kbutton (0x009340b0).
void XB_CDECL IN_ActivateUp();
// 0x002b4700: +activate: IN_KeyDown on its kbutton (0x009340b0).
void XB_CDECL IN_ActivateDown();
// 0x002b4790: -button0: IN_KeyUp on its kbutton (0x00934020).
void XB_CDECL IN_Button0Up();
// 0x002b47a0: +button0: IN_KeyDown on its kbutton (0x00934020).
void XB_CDECL IN_Button0Down();
// 0x002b47b0: -strafe: IN_KeyUp on its kbutton (0x00933fa8).
void XB_CDECL IN_StrafeUp();
// 0x002b47c0: +strafe: IN_KeyDown on its kbutton (0x00933fa8).
void XB_CDECL IN_StrafeDown();
// 0x002b47d0: -speed: IN_KeyUp on its kbutton (0x00933fc0).
void XB_CDECL IN_SpeedUp();
// 0x002b47e0: +speed: IN_KeyDown on its kbutton (0x00933fc0).
void XB_CDECL IN_SpeedDown();
// 0x002b47f0: -moveright: IN_KeyUp on its kbutton (0x00933f90).
void XB_CDECL IN_MoverightUp();
// 0x002b4800: +moveright: IN_KeyDown on its kbutton (0x00933f90).
void XB_CDECL IN_MoverightDown();
// 0x002b4810: -moveleft: IN_KeyUp on its kbutton (0x00933f78).
void XB_CDECL IN_MoveleftUp();
// 0x002b4820: +moveleft: IN_KeyDown on its kbutton (0x00933f78).
void XB_CDECL IN_MoveleftDown();
// 0x002b4830: -lookdown: IN_KeyUp on its kbutton (0x00933f60).
void XB_CDECL IN_LookdownUp();
// 0x002b4840: +lookdown: IN_KeyDown on its kbutton (0x00933f60).
void XB_CDECL IN_LookdownDown();
// 0x002b4850: -lookup: IN_KeyUp on its kbutton (0x00933f48).
void XB_CDECL IN_LookupUp();
// 0x002b4860: +lookup: IN_KeyDown on its kbutton (0x00933f48).
void XB_CDECL IN_LookupDown();
// 0x002b4870: -back: IN_KeyUp on its kbutton (0x00933f30).
void XB_CDECL IN_BackUp();
// 0x002b4880: +back: IN_KeyDown on its kbutton (0x00933f30).
void XB_CDECL IN_BackDown();
// 0x002b4890: -forward: IN_KeyUp on its kbutton (0x00933f18).
void XB_CDECL IN_ForwardUp();
// 0x002b48a0: +forward: IN_KeyDown on its kbutton (0x00933f18).
void XB_CDECL IN_ForwardDown();
// 0x002b48b0: -right: IN_KeyUp on its kbutton (0x00933f00).
void XB_CDECL IN_RightUp();
// 0x002b48c0: +right: IN_KeyDown on its kbutton (0x00933f00).
void XB_CDECL IN_RightDown();
// 0x002b48d0: -left: IN_KeyUp on its kbutton (0x00933ee8).
void XB_CDECL IN_LeftUp();
// 0x002b48e0: +left: IN_KeyDown on its kbutton (0x00933ee8).
void XB_CDECL IN_LeftDown();
// 0x002b48f0: -moveup: releases both jump buttons.
void XB_CDECL IN_UpUp();
// 0x002c7de0: +moveup: jump, or one stance step up through cl_stance while not crouching.
void XB_CDECL IN_UpDown();
// 0x002cd5f0: -movedown: releases crouch; cl_stanceTemp = whether crouch/prone is still held.
void XB_CDECL IN_DownUp();
// 0x002cd640: +movedown: presses crouch; cl_stanceTemp the same way.
void XB_CDECL IN_DownDown();
// 0x002cd0c0 (down in EAX, key in ECX, time on the stack): Q3's CL_KeyEvent without the console, menu and
// message-mode paths: repeats/anykeydown, the camera interrupt (space/enter/escape while a scripted camera runs),
// escape pausing the game (CL_TogglePause unless introScreen) and "+cmd key time" / "-cmd key time" for bindings.
void XB_REGS3 XB_CDECL CL_KeyEvent(int32_t down, uint32_t edx_unused, int32_t key, int32_t time);

// 0x002cd020: Q3's CL_ClearState: stops all sounds/channels, empties every set configstring, clears cl and clc.
void XB_CDECL CL_ClearState();
// 0x002f2890: Q3's CL_MapLoading for the local server (cvar 0x00947704 set: r_uiFullScreen 1 and a clean client
// first), then servername "localhost", CA_CONNECTING, one SCR_UpdateScreen and the sounds stopped.
void XB_CDECL CL_MapLoading();

// 0x002f1d90 (CALL in its first 5 bytes: a C helper, not registered): re.BeginFrame(0), then by cls.state the
// front end (0), the mission overlays (1) or CG_DrawActiveFrame (2), and the mission overlays at 0x0095f8d0 again.
// `entry` = the ESP the original would have at its entry.
void SCR_DrawScreenField(uint32_t entry);
// 0x002f1e60: the client frame's screen update once the UI is up: SCR_DrawScreenField + re.EndFrame (unless
// 0x009403f4), the frame time since the last call, a queued level start ("spmap <map>" from the 14-entry mission
// table) and the HUD update (mission HUD at 0x0095f8d0 or g_hudManager's vtable +0xc) with that time.
extern "C" PLATFORM_HIDDEN void XB_CDECL SCR_UpdateScreen(uint32_t entry);
extern "C" void SCR_UpdateScreen_entry(void);
struct TitleRegs;
// 0x002f2630 (msec in EAX): Q3's CL_Frame: the return to the front end (sounds stopped, KEYCATCH_UI, g_reloading 0),
// cl_avidemo screenshots and fixed frame time, cls.realtime, user commands and the packet while in game, the screen
// update (through whatever the call at 0x002f27a4 targets: the loader's --level hook), file requests, sound channels,
// the streaming view position from the player's cell, crossfade, effects, rumble, ambient emitters and the entity
// list compaction. `entry` = the original's ESP at its entry, `regs` its registers there.
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_Frame(uint32_t entry, const TitleRegs* regs);
extern "C" void CL_Frame_entry(void);

// 0x0032dde0 (menu in ESI): escape in game: cl_paused 1, the sound channels paused, the pause overlay shown
// (byte +0x40 from 0x008dce69, vtable +0x88 (0, 1)) and the menu activated (vtable +0xc (0)).
extern "C" void CL_TogglePause_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_TogglePause(uint8_t* menu);
// 0x0032de50 (screen manager in ESI, ret 4 through its tail-called method): the in-game screens shown (0x0095f919 =
// 1), cl_paused 1, then the manager's +0x0c (screen).
extern "C" void CL_PauseIntoFrontEnd_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_PauseIntoFrontEnd(uint8_t* mgr);  // the part before the tail call
// 0x0032d800 (difficulty in EAX, menu in ESI): skill hard / medium / easy for 0 / 1 / 2 (0x008dce50 = it; others:
// nothing), 0x00940178 = 1, every sound stopped, Movie_Stop(0), the level to start (0x0095f920) and the current
// chapter = +0x48 (at least 0), the unlocked chapters raised to it.
extern "C" PLATFORM_HIDDEN void XB_CDECL DifficultyMenu_StartGame(int32_t difficulty, uint8_t* menu);
extern "C" void DifficultyMenu_StartGame_entry(void);
// 0x00379900 (atexit): ScriptString_Release of the 0x400 strings below 0x009616b8, last first.
void XB_CDECL AtExit_ReleaseScriptStrings_9616b8();
// 0x003799c0 (atexit): the string vector 0x009600fc (first, last, end) destroyed and freed, its words cleared.
void XB_CDECL AtExit_StringVector_9600fc();
// 0x0032dec0 (keep_running in DL, dialog in ECX): sets the dialog's flag bits from its mode (+0xc) and the cvar at
// 0x0093fcd8, activates it (vtable +0xc) and pauses the game (cl_paused 1) unless that cvar is set or the mission
// overlay is up.
void XB_REGS3 XB_CDECL CL_PauseForDialog(uint32_t eax_unused, uint8_t keep_running, uint8_t* dialog);

// 0x0030fca0: with subtitles on (0x008dce68), a first line (0x0095f474) and the timer 0x0092708c at or past
// 0x00926e74: nglListAddTextf on the HUD list 0x0095f8e0 for the first line and, if set, the second (0x0095f470) at
// their unsigned pixel positions, color 0xe0ffff00 unless the line's flag word is set.
void XB_CDECL Subtitle_Draw();

// 0x0033ac10 (loading screen in ESI): every 11th call steps the dot counter (+0x3c, 0..2) and shows only that dot
// (vtable +0x7c (0) on the three dot panels +0x44..+0x4c, then (1.0) on the current one); then tail-jumps to
// SCR_UpdateScreen when the front end (0x0095f8fc) is in state 5. The C returns the tail-jump target (0 = none).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SCR_UpdateLoadingDots(uint8_t* screen);
extern "C" void SCR_UpdateLoadingDots_entry(void);

// 0x00292ee0 (name in EAX, out buffer in ECX): "t:<text>" -> sprintf(out, "%s", text), returns out; else the value
// of the 19-entry button name table 0x008d5a98 {name, value} matching name (stricmp), 0 if none.
const char* XB_REGS3 XB_CDECL Key_TranslateButtonName(const char* name, uint32_t unused_edx, char* out);

// 0x00293720: the front buffer redrawn as a 640x480 quad (an empty frame first, then one with clear flags 0xf3).
void XB_CDECL SCR_RedrawFrontBuffer();
// 0x002ab7c0: Q3's CL_Setenv_f without the putenv: "name=value ..." built from the arguments in a local buffer only.
void XB_CDECL CL_Setenv_f();
// 0x002b31f0: Q3's CL_ShellExecute_URL_f without the shell call: for "open" with 3+ arguments only _atol(argv 3).
void XB_CDECL CL_ShellExecute_URL_f();
// 0x002b32b0: Q3's CL_ForwardToServer_f: Cmd_ArgsFrom(1) as a reliable command (CL_AddReliableCommand, inlined)
// when connected (cls.state 2) with an argument.
void XB_CDECL CL_ForwardToServer_f();
// 0x002b3370: Q3's Key_Bindlist_f with the Com_Printf compiled out: Key_KeynumToString(key, 0) of every bound key.
void XB_CDECL Key_Bindlist_f();
// 0x002b3580: Q3's CL_ShutdownInput - Cmd_RemoveCommand for the first 59 of CL_InitInput's commands.
void XB_CDECL CL_ShutdownInput();
// 0x002ccf40 / 0x002ccfb0: cl_testAnimWeight -/+ cls.frametime / rate (cvar 0x0093fce4, 0 = 1) * 0.002, clamped
// to 0 / 1.0 (Cvar_SetValue).
void XB_CDECL CL_DecAnimWeight_f();
void XB_CDECL CL_IncAnimWeight_f();
// 0x002c1d60: Q3's Key_Unbindall_f: every bound key set to a "" copy (Key_SetBinding inlined: Z_Free, CopyString,
// cvar_modifiedFlags |= 1).
void XB_CDECL Key_Unbindall_f();
// 0x002c1db0: Q3's Key_Unbind_f: with exactly one argument, that key (Key_StringToKeynum) set to "" as above.
void XB_CDECL Key_Unbind_f();
// 0x00331030 (style in ESI): cg_controllerconfig = style, then "exec style_e/f/g.cfg" for 1/2/3 (style_h
// otherwise) and Cbuf_Execute (a tail jump).
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_ExecControllerConfig(int32_t style);
extern "C" void CL_ExecControllerConfig_entry(void);

// The pads (XInput). The pad object IN_PadCreate returns (0x00902564, 0x18 bytes) holds nothing: the methods
// work on the per-port globals (cl.cpp) and ignore ECX.
// 0x00011b80 (pad in EDI): takes the pressed edges of all 16 buttons.
extern "C" PLATFORM_HIDDEN void XB_CDECL IN_PadConsumeAllPressed(int32_t pad);
extern "C" void IN_PadConsumeAllPressed_entry(void);
// 0x0001ec70 (value in ESI): a stick axis with the dead zone (cvar 0x00929c30) cut out, rescaled to +-128.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL IN_JoyApplyThreshold(int32_t value);
extern "C" void IN_JoyApplyThreshold_entry(void);
// 0x00055bc0: XGetDeviceChanges: closes removed pads, opens inserted ones (port 0: 0x001fa100(handle)).
void XB_CDECL IN_PadCheckDeviceChanges();
// 0x00055d10: XInitDevices, clears every port's state and opens the attached pads. Returns `pads`.
uint8_t* XB_THISCALL IN_PadInit(uint8_t* pads);
// 0x00055e40: the pad object, created (Z_Malloc, IN_PadInit) on first use; null when that fails.
uint8_t* XB_CDECL IN_PadCreate();
// 0x00055e70: device changes, then per open pad: XInputGetState, the pressed/released edges (unless
// g_liveMgr takes the input: 0x00902560 = 1), analog buttons as down flags with edges.
void XB_CDECL IN_PadUpdate();
// 0x00056570: false (unhookable: ret in the first 5 bytes).
uint8_t XB_STDCALL IN_PadReturnFalse(int32_t pad, uint32_t button);
// 0x00056a30: motor 1 = left, else right; strength 0..1. Sends the new values once the last set is done.
void XB_STDCALL IN_SetRumbleMotor(int32_t pad, int32_t motor, float strength);
// 0x00056ae0: zero rumble on every pad with no set pending.
void XB_CDECL IN_PadStopAllRumble();
// 0x00298d30 (rumble state in EAX): both motors off on the last rumbled pad (0x008dcf18) and frees the two
// queued-rumble lists (+8, +0x18) with 0x000248f0.
void XB_REGS1 XB_CDECL IN_StopAllRumble(uint8_t* rumble);
// 0x002abeb0 / 0x002abec0: empty (bare ret, unhookable).
void XB_CDECL IN_SprintUp();
void XB_CDECL IN_SprintDown();
// 0x002b7200: takes the active pad's pressed edges and queues key-up events for every pad key.
void XB_CDECL IN_PadQueueEvents();
// 0x002bdf10: per frame: pad update; the active pad (0x008d50d8, -1 = pad 0) gone: 0x008d50dc = 0; first frame
// back: IN_PadQueueEvents. Unless 0x0095f538: stick events (3 = move stick, 4 = look stick axes) and key events
// for the buttons (unless 0x0095f919: no stick or key-down events).
void XB_CDECL IN_Frame();

struct ScriptString;

// Loading screen spinner and text, configstrings, the text field, rumble.
// 0x00292e60: the spinner icon (scr_loadingIconImages[scr_loadingIconFrame]) as a quad at the rect 0x008fef54..60.
void XB_CDECL SCR_DrawLoadingIcon();
// 0x002960a0 / 0x00298170 (full in EBX): steps the 8-frame spinner (one frame per 0.15 s of the RDTSC clock, own
// statics each), then with full: device bindings reset, one frame (clear 0xf3, no depth) with the icon; without:
// the icon added twice. The second also draws the "Loading ..." text first.
void XB_CDECL SCR_UpdateLoadingIcon(uint8_t full);
extern "C" PLATFORM_HIDDEN void XB_CDECL SCR_UpdateLoadingIcon_2(uint32_t ebx);
extern "C" void SCR_UpdateLoadingIcon_2_entry(void);
// 0x00296200: a dark full-screen quad (0x96000000, z 300) and the localized "Loading ..." centered at y 220 in
// ui_fonts[0], both added twice (clear flags 3).
void XB_CDECL SCR_DrawLoadingText();
// 0x002aac90: refimport: cm entry `index` (16 bytes at +0x14; entry 0 when out of range).
uint8_t* XB_CDECL CL_RefCM_GetEntry(uint32_t index);
// 0x002ab710: refimport: VM_Call(cgvm, 8, value).
uint32_t XB_CDECL CL_CGameCall8(int16_t value);
// 0x002ac450: every configstring set to "", then VM_Call(cgvm, 0x14).
void XB_CDECL CL_ClearConfigstrings();
// 0x002b3250: refimport: Sys_Milliseconds() scaled by the cvar at 0x0093fce4 (CALL in its first 5 bytes).
int32_t XB_CDECL CL_ScaledMilliseconds();
// 0x002bb7c0: refimport: the "arialblack_14" entry of ui_fonts (null when R_FindFont gives 7 or 8).
uint8_t* XB_CDECL CL_RefGetFont();
// 0x002cd090 (char in EAX): CL_CharEvent on the field 0x0092e830 unless '`'/'~', only with the console catcher or
// cls.state 0.
uint32_t XB_REGS1 XB_CDECL CL_CharEventFiltered(uint32_t ch);
// 0x002d55d0: cgame notified (0x16, 0x0092f094), then each configstring that differs from strings[i] assigned and
// reported (VM_Call 0x15, i); CL_SystemInfoChanged (tail jump).
void XB_CDECL CL_RefreshConfigstrings(const ScriptString* strings);
// 0x00331f70: the subtitle clock 0x0092708c += ms / 1000 (x87); past the shown line's end (0x00926e24) the next
// line (Subtitle_Start with the category at 0x00926e30).
void XB_CDECL SCR_UpdateLoadingSubtitle(int32_t ms);
// 0x00331fb0: while a movie plays (0x0095f454, not 0x0095f916): its clock 0x0095f45c advanced by the RDTSC ticks
// since 0x0095f468 (in ms), the subtitles by that * 1.015, then File_PumpRequests (tail jump).
void XB_CDECL SCR_LoadingPump();
// 0x002c1e00 (field in ESI): Q3's Field_AdjustScroll on pixel widths (re function 0x00928570: text, mode, the
// field's sizes, count): scroll and characters shown (+8) so the cursor stays inside the width (+0xc).
extern "C" PLATFORM_HIDDEN void XB_CDECL Field_AdjustScroll(uint8_t* field);
extern "C" void Field_AdjustScroll_entry(void);
// 0x002c77d0 (field in EAX, char in EBX): Q3's Field_CharEvent: ctrl-v (nothing), ctrl-c, backspace, ctrl-a,
// ctrl-e, printable characters inserted or overstruck (0x0092858c), 255 at most.
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_CharEvent(uint8_t* field, int32_t ch);
extern "C" void CL_CharEvent_entry(void);
// 0x002c1c20: Q3's Key_Bind_f without the prints: binds argv 1 (tolower) to the rest of the arguments; with
// 0x008dce70 a command containing "+speed" becomes "toggle cl_run".
void XB_CDECL Key_Bind_f();
// 0x00298810: per motor, the queued rumbles advanced by dt (patterns 'a'..'z' over their duration, envelopes with
// attack/sustain/release; finished ones freed), the strongest level * scale sent to the last used pad (0x008dcf18;
// zero when the cvar 0x0093fcd8 is set, rumble 0x008dce72 is off or cls.state is 5).
void XB_STDCALL CL_UpdateRumble(uint8_t* rumble, float dt);

// 0x002b2c60 (character in ECX): the character (va "%c") drawn by the re function 0x00928578 at x, y + 16 scaled
// from 640x480, 8 wide and a third high, white.
extern "C" PLATFORM_HIDDEN void XB_CDECL SCR_DrawSmallChar(int32_t x, int32_t y, int32_t ch);
extern "C" void SCR_DrawSmallChar_entry(void);
// 0x002f06b0: the "FrontEnd" zone from the stream map (0x0095f4f0); unless already loaded (slot +0x28 loaded with
// +0xdc set) or 0x0095ee01: request counter 0x008d9d38 + 1, priority +0x34 = 0, Stream_StartRequest.
uint8_t* XB_CDECL CL_LoadFrontEndZone();
// 0x002f0a50: Q3's CL_StartHunkUsers while com_cl_running: cl_xmodelcheck (0x008db11c), the renderer once
// (cls.rendererStarted), the front-end zone once (0x0092be88).
void XB_CDECL CL_StartHunkUsers();
// 0x002982d0: the four spinner images (0x1080-byte DDS copies from 0x008d5b38.., Mem_AlignedAlloc) loaded as
// spinner_a..d into the 8 icon frames a a a b c d c b.
void XB_CDECL SCR_InitLoadingSpinner();
// 0x002b2880: cubemapShot <size> <name> [water x y z x y z | fresnel a b]: water writes the six faces with the
// re function 0x00928564; otherwise each face is rendered (CG_DRAW_ACTIVE_FRAME with face and size) and written
// by 0x00928560 as env/<name><suffix>.tga with the fresnel values (default 1, 1.333).
void XB_CDECL CL_CubemapShot_f();
// 0x002f10d0: Q3's CL_InitRef: the refimport table (0x22 words; words 0x12 and 0x17 never set), GetRefAPI(14),
// re = its 0x28 words, cl_paused 0.
void XB_CDECL CL_InitRef();
// 0x002f2db0: Q3's CL_InitCGame: cl.mapname "maps/<mapname>.bsp" from the serverinfo configstring, VM_Create
// ("cgame", CL_CgameSystemCalls), CG_INIT, re.EndRegistration (0x00928510), Sys_Milliseconds calls around them.
uint32_t XB_CDECL CL_InitCGame();
// 0x002f08e0: Q3's CL_Shutdown: debug draw, client state, cgame, re.Shutdown(1), the front-end return state, input
// and the 18 client commands removed, cl_running 0, cls cleared.
void XB_CDECL CL_Shutdown();
// 0x002f0750: KEYCATCH_UI off; with 0x0095ee01 the profile settings (0x008dce30), skill and the front end opened
// for the chapter (theatre background); with the mission overlay (0x0095f918) it is destroyed and the front end
// opened likewise, the front-end zone waited for (priority FLT_MAX).
void XB_CDECL CL_ApplyFrontEndReturnState();
// 0x0032df90 (menu in ESI, dt on the stack, ret 4): the pause overlay's update: resume (flag 8: 0x0095f900 vtable
// +0xc (-1), sounds and cl_paused 0 unless its flag 1), children shown, slide timer +0x40 and the slide-in/out
// position +0x3c (3 per 0.01 s) sent to child 3 (vtable +0x8c), both backgrounds' colour (vtable +0x90).
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_UpdatePauseOverlay(uint8_t* menu, float dt);
extern "C" void CL_UpdatePauseOverlay_entry(void);
// 0x00332230 (name in ESI): opens the movie, waits for its buffers, subtitles "MOVIE", then frames (identity view
// moved 10 along z) until the last frame or a skip; Movie_Stop(1).
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_PlayMovie(const char* name);
extern "C" void CL_PlayMovie_entry(void);
// 0x00332e20 (profile in EDI): sound attenuation mode, volumes (+0x58 / 50), inverted look (m_pitch sign),
// sensitivities (+0x44/+0x48 / 25), the active pad, g_vehControlMode (+0x3c) and the controller style config.
extern "C" PLATFORM_HIDDEN void XB_CDECL CL_ApplyProfileSettings(uint8_t* profile);
extern "C" void CL_ApplyProfileSettings_entry(void);
// 0x0032eac0: the "INGAME_TWO_YEARS" card: a UIText over black, faded in over 2 s, shown to 3 s, faded out by 5 s
// (RDTSC clock), a frame per step. The text object is not freed.
void XB_CDECL SCR_DrawLoadingScreen();
// 0x0032ed10 (alpha in EAX): "INGAME_LOADING" over a black quad of that alpha, two frames.
void XB_REGS1 XB_CDECL SCR_DrawLaunchLoadingScreen(uint32_t alpha);

// 0x002f20a0: Q3's CL_Init: cls.state 0, CL_InitInput, the client cvars (sensitivities from the profile defaults
// 0x008dce74/78, m_pitch = its sign * 0.7), the client commands, CL_InitRef, UI started (0x00933ee4), Cbuf_Execute,
// cl_running 1.
void XB_CDECL CL_Init();

// 0x0001d1b0 (ESI = cl, the client state 0x00960160; returned): its constructor, run by the
// static initializer 0x00376450 before the title's main - a few fields cleared and the
// configstrings (+0x558, 0x400 script strings) constructed empty.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL ClientActive_Ctor(uint8_t* cl);

extern "C" void ClientActive_Ctor_entry(void);

}  // namespace game
