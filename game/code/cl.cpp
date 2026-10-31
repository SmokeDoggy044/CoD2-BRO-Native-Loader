#include "cl.h"

#include <cstring>

#include "cg_ents.h"
#include "cgame.h"
#include "fe.h"
#include "movie.h"
#include "ngl.h"
#include "ngl_debug.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "r_init.h"
#include "script_string.h"
#include "snd.h"
#include "stream.h"
#include "sv.h"
#include "tr_main.h"
#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kPads = 0x00902400;
constexpr uint32_t kPadSize = 0x58;

uint8_t* pad_block(int32_t pad) { return reinterpret_cast<uint8_t*>(uintptr_t{kPads + uint32_t(pad) * kPadSize}); }

bool pad_input_blocked() { return title_global<uint8_t>(0x00902560) != 0; }

// Per button: a bit of the word at the block offset, or (bit 0) the byte at the offset.
struct ButtonSlot {
    uint8_t offset;
    uint8_t bit;
};
constexpr ButtonSlot kDownSlots[16] = {
    {0x12, 0x04}, {0x12, 0x02}, {0x12, 0x08}, {0x12, 0x01}, {0x04, 0}, {0x02, 0}, {0x03, 0}, {0x05, 0},
    {0x09, 0},    {0x08, 0},    {0x06, 0},    {0x07, 0},    {0x12, 0x80}, {0x12, 0x40}, {0x12, 0x10}, {0x12, 0x20},
};
// The edge blocks (pressed +0x1c, released +0x26): the same order, analog edges at +4..+9.
constexpr ButtonSlot kEdgeSlots[16] = {
    {0, 0x04}, {0, 0x02}, {0, 0x08}, {0, 0x01}, {4, 0}, {2, 0}, {3, 0}, {5, 0},
    {9, 0},    {8, 0},    {6, 0},    {7, 0},    {0, 0x80}, {0, 0x40}, {0, 0x10}, {0, 0x20},
};

uint8_t consume_edge(uint8_t* edges, uint32_t button) {
    if (button > 15) return 0;
    const ButtonSlot slot = kEdgeSlots[button];
    if (slot.bit) {
        uint16_t& word = *reinterpret_cast<uint16_t*>(edges);
        if (!(word & slot.bit)) return 0;
        word = uint16_t(word & ~slot.bit);
        return 1;
    }
    if (!edges[slot.offset]) return 0;
    edges[slot.offset] = 0;
    return 1;
}

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

int32_t title_atol(const char* text) { return title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(text); }

uint32_t cmd_argc() { return title_global<uint32_t>(0x00934248); }
const char* cmd_argv(uint32_t index) { return title_global<const char*>(0x0092df90 + 4 * index); }

int8_t clamp_char(int32_t value) { return static_cast<int8_t>(value < -128 ? -128 : value > 127 ? 127 : value); }

int32_t cvar_integer(uint32_t cvar_pointer) { return field<int32_t>(title_global<uint8_t*>(cvar_pointer), 0x20); }
float cvar_value(uint32_t cvar_pointer) { return field<float>(title_global<uint8_t*>(cvar_pointer), 0x1c); }

// The title CRT's __CIpow (x87 intrinsic: base in ST1, exponent in ST0, result in ST0) of a * b (FLD, FMUL) to the
// power of `exponent`, the result kept at x87 precision.
long double title_ci_pow_of_product(float a, float b, double exponent) {
    long double result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfldl %3\n\tcall *%4\n\tfstpt %0"
                     : "=m"(result)
                     : "m"(a), "m"(b), "m"(exponent), "r"(uintptr_t{0x001f1a6c})
                     : "eax", "ecx", "edx", "st", "st(1)", "memory");
    return result;
}

// sqrt(b * b + a * a) / divisor on the x87 (FMUL, FADDP, the title CRT's __CIsqrt, rounded to float in between).
float title_ci_length_over(float a, float b, float divisor) {
    float length;
    __asm__ volatile("flds %2\n\tfmuls %2\n\tflds %1\n\tfmuls %1\n\tfaddp\n\tcall *%3\n\tfstps %0"
                     : "=m"(length)
                     : "m"(a), "m"(b), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "st(1)", "memory");
    float result;
    __asm__("flds %1\n\tfdivs %2\n\tfstps %0" : "=m"(result) : "m"(length), "m"(divisor) : "st");
    return result;
}

// |value| by clearing the sign bit, as the original does (std::fabs is a host call in unoptimized builds).
float abs_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    bits &= 0x7fffffff;
    std::memcpy(&value, &bits, 4);
    return value;
}

// kbutton_t
constexpr uint32_t kDown0 = 0x00, kDown1 = 0x04, kDowntime = 0x08, kMsec = 0x0c, kActive = 0x10, kWasPressed = 0x14;

}  // namespace

uint8_t XB_STDCALL IN_PadIsConnected(int32_t pad) {
    return *reinterpret_cast<uint32_t*>(pad_block(pad) + 0x4c) != 0;
}

uint32_t XB_STDCALL IN_PadButtonDown(int32_t pad, uint32_t button) {
    if (pad_input_blocked() || button > 15) return 0;
    const ButtonSlot slot = kDownSlots[button];
    const uint8_t value = pad_block(pad)[slot.offset];
    return slot.bit ? ((value & slot.bit) ? 0xff : 0) : value;
}

uint8_t XB_STDCALL IN_PadButtonPressed(int32_t pad, uint32_t button) {
    if (button > 15) return 0;
    const ButtonSlot slot = kEdgeSlots[button];
    const uint8_t* edges = pad_block(pad) + 0x1c;
    return slot.bit ? (edges[0] & slot.bit) != 0 : edges[slot.offset];
}

uint8_t XB_STDCALL IN_PadConsumePressed(int32_t pad, uint32_t button) {
    return consume_edge(pad_block(pad) + 0x1c, button);
}

uint8_t XB_STDCALL IN_PadConsumeReleased(int32_t pad, uint32_t button) {
    return consume_edge(pad_block(pad) + 0x26, button);
}

void XB_STDCALL IN_PadGetStick(int32_t pad, int32_t stick, int32_t* x, int32_t* y) {
    if (pad_input_blocked()) {
        *x = 0;
        *y = 0;
        return;
    }
    const auto* block = reinterpret_cast<const int8_t*>(pad_block(pad));
    *x = block[stick ? 0x0f : 0x0b];
    *y = -block[stick ? 0x11 : 0x0d];
}

PLATFORM_ASM_ENTRY(CL_KeyState_entry,
                   "subl $0x6c, %esp\n"
                   "movups %xmm2, 0x00(%esp)\n"
                   "movups %xmm3, 0x10(%esp)\n"
                   "movups %xmm4, 0x20(%esp)\n"
                   "movups %xmm5, 0x30(%esp)\n"
                   "movups %xmm6, 0x40(%esp)\n"
                   "movups %xmm7, 0x50(%esp)\n"
                   "movss %xmm1, 0x60(%esp)\n"
                   "leal 0x60(%esp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CL_KeyState_Raw) "addl $8, %esp\n"
                   "fstps 0x64(%esp)\n"
                   "movss 0x64(%esp), %xmm0\n"
                   "movss 0x60(%esp), %xmm1\n"
                   "movups 0x00(%esp), %xmm2\n"
                   "movups 0x10(%esp), %xmm3\n"
                   "movups 0x20(%esp), %xmm4\n"
                   "movups 0x30(%esp), %xmm5\n"
                   "movups 0x40(%esp), %xmm6\n"
                   "movups 0x50(%esp), %xmm7\n"
                   "addl $0x6c, %esp\n"
                   "ret")

float XB_CDECL CL_KeyState(uint8_t* key) {
    float xmm1 = 0.0f;
    return CL_KeyState_Raw(key, &xmm1);
}

float XB_CDECL CL_KeyState_Raw(uint8_t* key, float* xmm1) {
    int32_t msec = field<int32_t>(key, kMsec);
    field<int32_t>(key, kMsec) = 0;
    if (field<int32_t>(key, kActive)) {
        const int32_t now = title_global<int32_t>(0x00947710);  // com_frameTime
        const int32_t downtime = field<int32_t>(key, kDowntime);
        if (!downtime)
            msec = now;
        else
            msec += now - downtime;
        field<int32_t>(key, kDowntime) = now;
    }
    const float val = platform_x87_div_by_unsigned(static_cast<float>(msec), title_global<uint32_t>(0x0092f130));
    if (platform_x87_greater(title_global<float>(0x0083eafc), val)) return 0.0f;
    *xmm1 = val;
    const float one = title_global<float>(0x0083eb20);
    return val > one ? one : val;
}

PLATFORM_ASM_ENTRY(IN_KeyDown_entry, "pushl %esi\n" PLATFORM_ASM_CALL(IN_KeyDown) "addl $4, %esp\n"
                                     "ret")

void XB_CDECL IN_KeyDown(uint8_t* key) {
    int32_t k = -1;
    if (cmd_argc() > 1 && cmd_argv(1)[0]) k = title_atol(cmd_argv(1));
    int32_t& down0 = field<int32_t>(key, kDown0);
    int32_t& down1 = field<int32_t>(key, kDown1);
    if (k == down0 || k == down1) return;  // repeating key
    if (!down0)
        down0 = k;
    else if (!down1)
        down1 = k;
    else
        return;  // three keys down
    if (field<int32_t>(key, kActive)) return;
    const char* c = cmd_argc() > 2 ? cmd_argv(2) : &title_global<const char>(0x0082dfeb);
    field<int32_t>(key, kDowntime) = title_atol(c);
    field<int32_t>(key, kActive) = 1;
    field<int32_t>(key, kWasPressed) = 1;
}

PLATFORM_ASM_ENTRY(IN_KeyUp_entry, "pushl %esi\n" PLATFORM_ASM_CALL(IN_KeyUp) "addl $4, %esp\n"
                                   "ret")

void XB_CDECL IN_KeyUp(uint8_t* key) {
    int32_t& down0 = field<int32_t>(key, kDown0);
    int32_t& down1 = field<int32_t>(key, kDown1);
    if (cmd_argc() <= 1 || !cmd_argv(1)[0]) {  // typed manually: clear everything
        down1 = 0;
        down0 = 0;
        field<int32_t>(key, kActive) = 0;
        return;
    }
    const int32_t k = title_atol(cmd_argv(1));
    if (down0 == k)
        down0 = 0;
    else if (down1 == k)
        down1 = 0;
    else
        return;
    if (down0 || down1) return;  // some other key is still holding it down
    field<int32_t>(key, kActive) = 0;
    const char* c = cmd_argc() > 2 ? cmd_argv(2) : &title_global<const char>(0x0082dfeb);
    const int32_t uptime = title_atol(c);
    if (uptime)
        field<int32_t>(key, kMsec) += uptime - field<int32_t>(key, kDowntime);
    else
        field<int32_t>(key, kMsec) += title_global<uint32_t>(0x0092f130) >> 1;
    field<int32_t>(key, kActive) = 0;
}

uint8_t* XB_REGS1 XB_CDECL CL_CmdButtons(uint8_t* cmd) {
    uint8_t* const buttons = &title_global<uint8_t>(0x00934020);
    for (uint32_t i = 0; i < 15; ++i) {
        if (i == 7) continue;
        uint8_t* key = buttons + i * 0x18;
        if (field<int32_t>(key, kActive) || field<int32_t>(key, kWasPressed)) cmd[4 + i / 8] |= uint8_t(1u << (i % 8));
        field<int32_t>(key, kWasPressed) = 0;
    }
    if (title_global<int32_t>(0x0092eff0) && !title_global<int32_t>(0x0092bdf4)) cmd[4] |= 0x80;  // cls.keyCatchers
    if (title_global<int32_t>(0x00961770)) cmd[5] |= 0x04;
    return cmd;
}

PLATFORM_ASM_ENTRY(CL_FinishMove_entry,
                   "pushl %edx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CL_FinishMove) "addl $4, %esp\n"
                   "popl %edx\n"
                   "ret")

void XB_CDECL CL_FinishMove(uint8_t* cmd) {
    cmd[6] = title_global<uint8_t>(0x00961768);
    field<uint32_t>(cmd, 0) = title_global<uint32_t>(0x009606b0);
    const float scale = title_global<float>(0x0083ebd8);
    const float half = title_global<float>(0x0083eb24);
    const float* angles = &title_global<float>(0x00961798);  // cl.viewangles
    for (uint32_t i = 0; i < 3; ++i)
        field<int32_t>(cmd, 8 + 4 * i) = static_cast<int32_t>(angles[i] * scale + half) & 0xffff;
    std::memcpy(cmd + 0x18, &title_global<uint32_t>(0x00961784), 5 * sizeof(uint32_t));
    if (title_global<int32_t>(0x009622b0)) {
        cmd[4] = title_global<uint8_t>(0x009622b4);
        cmd[5] = title_global<uint8_t>(0x009622b8);
        cmd[6] = title_global<uint8_t>(0x009622bc);
        title_global<int32_t>(0x009622b0) = 0;
    }
}

PLATFORM_ASM_ENTRY(CL_GetUserCmd_entry,
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CL_GetUserCmd) "addl $8, %esp\n"
                   "popl %edx\n"
                   "ret")

int32_t XB_CDECL CL_GetUserCmd(int32_t number, uint8_t* out) {
    if (number > title_global<int32_t>(0x009622ac))  // cl.cmdNumber
        title_function<void XB_CDECL(int32_t, const char*, int32_t, int32_t)>(0x002938f0)(  // __break
            1, &title_global<const char>(0x00835374), number, title_global<int32_t>(0x009622ac));
    if (number <= title_global<int32_t>(0x009622ac) - 64 || number <= 0) return 0;
    std::memcpy(out, &title_global<uint8_t>(0x009617ac) + (number & 63) * 0x2c, 0x2c);  // cl.cmds
    return 1;
}

uint32_t XB_CDECL CL_CgameSystemCalls(const uint32_t* args) {
    const auto callback = [](uint32_t slot) { return title_global<uint32_t>(slot); };
    using A2 = uint32_t XB_CDECL(uint32_t, uint32_t);
    using A9 = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    switch (args[0]) {
    case 0x2e:
        return title_function<A2>(callback(0x009284f4))(args[1], args[2]);
    case 0x54:
        return title_function<A2>(callback(0x009284f8))(args[1], args[2]);
    case 0x31:
        return title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(callback(0x00928570))(
            args[1], args[2], args[3], 0, args[4]);
    case 0x32:
        return title_function<A2>(callback(0x00928574))(args[1], args[2]);
    case 0x33:
        title_function<A9>(callback(0x00928578))(args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8],
                                                   args[9]);
        return 0;
    case 0x34:
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                     uint32_t, uint32_t, uint32_t)>(callback(0x00928584))(
            args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8] & 0xff, 0, args[9], args[10]);
        return 0;
    case 0x39:
        title_function<void XB_CDECL()>(callback(0x00928514))();
        return 0;
    case 0x3d:
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(callback(0x00928518))(args[1], args[2], args[3]);
        return 0;
    case 0x3e:
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(callback(0x0092851c))(
            args[1], args[2], args[3], args[4], args[5]);
        return 0;
    case 0x42:
        title_function<void XB_CDECL(uint32_t)>(callback(0x00928524))(args[1]);
        return 0;
    case 0x45:
        title_function<void XB_CDECL(uint32_t)>(callback(0x0092852c))(args[1]);
        return 0;
    case 0x46:
        title_function<A9>(callback(0x00928530))(args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8],
                                                   args[9]);
        return 0;
    default:
        title_function<void XB_CDECL(int32_t, const char*, uint32_t)>(0x002938f0)(  // __break
            1, &title_global<const char>(0x00835248), args[0]);
        return 0;
    }
}

float* XB_CDECL SCR_AdjustFrom640(float* x, float* y, float* w, float* h) {
    const float xscale = static_cast<float>(title_global<int32_t>(0x0092bf3c)) * title_global<float>(0x0083ec14);
    const float yscale = static_cast<float>(title_global<int32_t>(0x0092bf40)) * title_global<float>(0x0083ec10);
    if (x) *x = *x * xscale;
    if (y) *y = *y * yscale;
    if (w) *w = *w * xscale;
    if (h) *h = *h * yscale;
    return h;
}

char* XB_CDECL CL_AddReliableCommand(const char* text) {
    const int32_t sequence = ++title_global<int32_t>(0x0092f5f4);  // clc.reliableSequence
    char* const slot = &title_global<char>(0x0092f5fc) + (sequence & 63) * 128;
    char* const result = title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(slot, text, 127);
    slot[127] = 0;
    return result;  // _strncpy's
}

uint32_t XB_CDECL CL_PacketEvent(uint32_t from0, uint32_t from1, uint32_t from2, uint32_t from3, uint32_t from4,
                                 uint8_t* msg) {
    title_global<uint32_t>(0x0092f5f0) = title_global<uint32_t>(0x0092be98);  // clc.lastPacketTime = cls.realtime
    const uint32_t state = title_global<uint32_t>(0x0092bdf0);
    if (!state) return state;
    const uint32_t from[5] = {from0, from1, from2, from3, from4};
    uint32_t remote[5];
    std::memcpy(remote, &title_global<uint32_t>(0x00933610), sizeof remote);  // clc.netchan.remoteAddress
    const auto* from_bytes = reinterpret_cast<const uint8_t*>(from);
    if (NET_CompareAdr(from_bytes, reinterpret_cast<const uint8_t*>(remote)))
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(NET_AdrToString(from_bytes)));
    TitleRegs regs{};
    regs.eax = 0x00933608;
    regs.ebx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(msg));
    const uint32_t accepted = cg_call_regs(0x002b13b0, &regs, nullptr, 0);  // Netchan_Process
    if (!accepted) return accepted;
    title_global<uint32_t>(0x009315fc) = *field<uint32_t*>(msg, 4);  // clc.serverMessageSequence
    title_global<uint32_t>(0x0092f5f0) = title_global<uint32_t>(0x0092be98);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(CL_ParseServerMessage(msg)));
}

void XB_CDECL CL_ConfigstringModified() {
    const char* const empty = &title_global<const char>(0x0082dfeb);
    const int32_t index = title_atol(cmd_argc() > 1 ? cmd_argv(1) : empty);
    if (index < 0 || index >= 0x400)
        title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, &title_global<const char>(0x008352ec));  // __break
    const char* const text = cmd_argc() > 2 ? cmd_argv(2) : empty;
    uint8_t* const slot = &title_global<uint8_t>(0x009606b8 + 4 * index);  // cl.configstrings[index]
    const char* const base = field<const char*>(slot, 0);
    const char* const old = base ? base + 6 : nullptr;
    if (!std::strcmp(old, text)) return;
    title_function<void XB_THISCALL(uint8_t*, const char*)>(0x0011dd20)(slot, text);  // ScriptString_AssignCString
    if (index == 1) CL_SystemInfoChanged();
}

const char* XB_CDECL CL_SystemInfoChanged() {
    const char* const base = title_global<const char*>(0x009606bc);
    const char* const info = base ? base + 6 : nullptr;
    const auto value = [info](uint32_t key) { return Info_ValueForKey(info, &title_global<const char>(key)); };
    title_global<int32_t>(0x009617a4) = title_atol(value(0x008340dc));  // cl.serverId = "sv_serverid"
    if (!title_atol(value(0x008340d0))) Cvar_SetCheatState();      // "sv_cheats"
    value(0x008340c8);                                                // "sv_paks"
    value(0x008340bc);                                                // "sv_pakNames"
    value(0x008340a8);                                                // "sv_referencedPaks"
    return value(0x00834090);                                         // "sv_referencedPakNames"
}

int32_t XB_REGS1 XB_CDECL CL_ParseSnapshot(uint8_t* msg) {
    uint8_t snap[0x550] = {};
    field<int32_t>(snap, 0x548) = title_global<int32_t>(0x00931600);  // clc.serverCommandSequence
    field<int32_t>(snap, 8) = MSG_ReadLong(msg);                      // serverTime
    const int32_t message_num = title_global<int32_t>(0x009315fc);    // clc.serverMessageSequence
    field<int32_t>(snap, 0xc) = message_num;
    int32_t& readcount = field<int32_t>(msg, 0x10);
    uint8_t flags = field<uint8_t*>(msg, 4)[readcount];
    if (++readcount > field<int32_t>(msg, 0xc)) flags = 0xff;
    field<int32_t>(snap, 4) = flags;
    field<int32_t>(snap, 0) = 1;  // valid
    uint8_t* const client = field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4);
    std::memcpy(snap + 0x20, field<uint8_t*>(client, 0x23c), 0x148 * 4);
    const int32_t result = message_num - (title_global<int32_t>(0x0096016c) + 1);
    std::memcpy(&title_global<uint8_t>(0x00960160), snap, sizeof snap);  // cl.snap
    std::memcpy(&title_global<uint8_t>(0x009622d0), snap, sizeof snap);
    return result;
}

uint8_t* XB_REGS1 XB_CDECL CL_ParseServerMessage(uint8_t* msg) {
    int32_t& readcount = field<int32_t>(msg, 0x10);
    const int32_t& cursize = field<int32_t>(msg, 0xc);
    const auto break_error = [](uint32_t format, int32_t value) {
        title_function<void XB_CDECL(int32_t, const char*, int32_t)>(0x002938f0)(1, &title_global<const char>(format), value);
    };
    const int32_t ack = MSG_ReadLong(msg);
    const int32_t sequence = title_global<int32_t>(0x0092f5f4);  // clc.reliableSequence
    int32_t& acknowledge = title_global<int32_t>(0x0092f5f8);
    acknowledge = ack;
    if (ack < sequence - 64) acknowledge = sequence;
    for (;;) {
        if (readcount > cursize) break_error(0x00834058, 0);  // "read past end of server message"
        uint8_t* const data = field<uint8_t*>(msg, 4);
        uint8_t cmd = data[readcount];
        if (++readcount > cursize) cmd = 0xff;
        if (cmd == 8) return data;  // svc_EOF
        if (cmd == 1) continue;     // svc_nop
        if (cmd == 7) {
            CL_ParseSnapshot(msg);
        } else if (cmd == 5) {  // svc_serverCommand
            uint8_t bytes[4];
            for (uint8_t& b : bytes) b = data[readcount++];
            int32_t seq;
            std::memcpy(&seq, bytes, 4);
            if (readcount > cursize) seq = -1;
            const char* s = MSG_ReadString(msg);
            int32_t& command_sequence = title_global<int32_t>(0x00931600);  // clc.serverCommandSequence
            if (command_sequence >= seq) continue;
            command_sequence = seq;
            char* const slot = &title_global<char>(0x00931608) + (seq & 63) * 128;  // clc.serverCommands
            title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(slot, s, 127);  // _strncpy
            slot[127] = 0;
        } else {
            break_error(0x00834020, cmd);  // "Illegible server message %d"
        }
    }
}

int32_t XB_REGS1 XB_CDECL CL_GetServerCommand(int32_t number) {
    const char* const s = &title_global<const char>(0x00931608) + (number & 63) * 128;
    title_global<int32_t>(0x00931604) = number;  // clc.lastExecutedServerCommand
    Cmd_TokenizeString(s, 0);
    const char* const cmd = cmd_argc() ? cmd_argv(0) : &title_global<const char>(0x0082dfeb);
    if (!std::strcmp(cmd, &title_global<const char>(0x008352e8))) {  // "cs"
        Cmd_TokenizeString(s, 3);
        CL_ConfigstringModified();
        Cmd_TokenizeString(s, 3);
        return 1;
    }
    if (!std::strcmp(cmd, &title_global<const char>(0x008352d8))) {  // "clientLevelShot"
        if (!cvar_integer(0x0093fcf0)) return 0;                     // com_sv_running
        Cbuf_AddText(&title_global<const char>(0x008352a4));
    }
    return 1;
}

uint32_t XB_CDECL CL_WritePacket() {
    const int32_t state = title_global<int32_t>(0x0092bdf0);  // cls.state
    if (state == 3 || state == 4) return static_cast<uint32_t>(state);
    uint8_t nullcmd[0x2c] = {};
    uint8_t data[0x800] = {};
    uint8_t msg[0x14] = {};  // msg_t: +4 data, +8 maxsize, +0xc cursize, +0x10 readcount
    field<uint8_t*>(msg, 4) = data;
    field<int32_t>(msg, 8) = 0x800;
    int32_t& cursize = field<int32_t>(msg, 0xc);
    MSG_WriteLong(msg, title_global<int32_t>(0x009617a4));  // cl.serverId
    MSG_WriteLong(msg, title_global<int32_t>(0x009315fc));  // clc.serverMessageSequence
    MSG_WriteLong(msg, title_global<int32_t>(0x00931600));  // clc.serverCommandSequence
    for (int32_t i = title_global<int32_t>(0x0092f5f8) + 1; i <= title_global<int32_t>(0x0092f5f4); ++i) {
        data[cursize++] = 2;  // clc_clientCommand
        for (int32_t b = 0; b < 4; ++b) data[cursize++] = static_cast<uint8_t>(static_cast<uint32_t>(i) >> (b * 8));
        MSG_WriteString(&title_global<const char>(0x0092f5fc) + (i & 63) * 128, msg);  // clc.reliableCommands
    }
    const int32_t cmd_number = title_global<int32_t>(0x009622ac);
    int32_t count = cmd_number - title_global<int32_t>(0x009622c0);
    const uint8_t* oldcmd = nullcmd;
    if (count > 32) count = 32;
    if (count >= 1) {
        const bool delta = !cvar_integer(0x00929808) && title_global<int32_t>(0x00960160) &&  // cl_nodelta, cl.snap.valid
                           title_global<int32_t>(0x009315fc) == title_global<int32_t>(0x0096016c);
        data[cursize++] = delta ? 0 : 1;
        data[cursize++] = static_cast<uint8_t>(count);
        for (int32_t i = 0; i < count; ++i) {
            const uint8_t* cmd = &title_global<uint8_t>(0x009617ac) + ((cmd_number - count + i + 1) & 63) * 0x2c;
            MSG_WriteDeltaUsercmd(oldcmd, cmd, msg);
            oldcmd = cmd;
        }
    }
    title_global<uint32_t>(0x009622c8) = title_global<uint32_t>(0x0092be98);  // cls.realtime
    title_global<uint32_t>(0x009622c4) = *reinterpret_cast<const uint32_t*>(oldcmd);
    title_global<int32_t>(0x009622c0) = title_global<int32_t>(0x009622ac);
    data[cursize++] = 3;  // clc_EOF
    return title_function<uint32_t XB_REGS1 XB_CDECL(uint8_t*, int32_t, uint8_t*)>(0x002b1480)(  // Netchan_Transmit
        &title_global<uint8_t>(0x00933608), cursize, data);
}

PLATFORM_ASM_ENTRY(CL_RefPrintf_entry,
                   "leal 12(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl 12(%esp)\n" PLATFORM_ASM_CALL(CL_RefPrintf) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CL_RefPrintf(const char* format, const uint32_t* args) {
    char text[0x1000];
    title_function<int32_t XB_CDECL(char*, const char*, const uint32_t*)>(0x001eea95)(text, format, args);  // _vsprintf
}

uint8_t* XB_CDECL CL_InitInput() {
    static constexpr struct {
        uint32_t name;
        uint32_t function;
    } kCommands[] = {
        {0x00835134, 0x002abe80}, {0x0082d62c, 0x002c7de0}, {0x0083512c, 0x002b48f0},
        {0x00835120, 0x002cd640}, {0x00835114, 0x002cd5f0}, {0x0083510c, 0x002b48e0},
        {0x00835104, 0x002b48d0}, {0x008350fc, 0x002b48c0}, {0x008350f4, 0x002b48b0},
        {0x008350e8, 0x002b48a0}, {0x008350dc, 0x002b4890}, {0x008350d4, 0x002b4880},
        {0x008350cc, 0x002b4870}, {0x008350c4, 0x002b4860}, {0x008350bc, 0x002b4850},
        {0x008350b0, 0x002b4840}, {0x008350a4, 0x002b4830}, {0x0083509c, 0x002b47c0},
        {0x00835094, 0x002b47b0}, {0x00835088, 0x002b4820}, {0x0083507c, 0x002b4810},
        {0x00835070, 0x002b4800}, {0x00835064, 0x002b47f0}, {0x0082dfa8, 0x002b47e0},
        {0x0083505c, 0x002b47d0}, {0x0082d6ac, 0x002b47a0}, {0x00835054, 0x002b4790},
        {0x0082d694, 0x002b4720}, {0x0083504c, 0x002b4710}, {0x0082dfc0, 0x002b4700},
        {0x00835040, 0x002b46f0}, {0x0082d640, 0x002b46a0}, {0x00835038, 0x002b4690},
        {0x0082be38, 0x002b4680}, {0x0083502c, 0x002b4670}, {0x0082be5c, 0x002b4660},
        {0x00835020, 0x002b4650}, {0x00835018, 0x002cd5a0}, {0x00835010, 0x002cd550},
        {0x0082d654, 0x002abec0}, {0x00835008, 0x002abeb0}, {0x00835000, 0x002ac0b0},
        {0x00834ff8, 0x002ac060}, {0x0082d610, 0x002c7d90}, {0x0082bed8, 0x002c7d40},
        {0x00834fe8, 0x002c7cf0}, {0x00834fdc, 0x002c7ca0}, {0x00834fd4, 0x002c7c70},
        {0x00834fc8, 0x002c7c40}, {0x00834fbc, 0x002c7bc0}, {0x00834fb0, 0x002b45f0},
        {0x0082d5ac, 0x002db660}, {0x00834f98, 0x002bb900}, {0x0082d5d4, 0x002db640},
        {0x00834f84, 0x002bb8f0}, {0x0082d678, 0x002d58b0}, {0x00834f74, 0x002cd500},
        {0x00834f64, 0x002cd4d0}, {0x00834f54, 0x002bb8c0}, {0x00834f44, 0x002bb890},
        {0x00834f34, 0x002bb870}, {0x0082d6cc, 0x002bb840}, {0x00834f24, 0x002bb820},
        {0x00834f10, 0x002abe70}, {0x00834efc, 0x002abe60},
    };
    for (const auto& command : kCommands) Cmd_AddCommand(&title_global<const char>(command.name), command.function);
    title_global<uint8_t*>(0x00929808) =
        Cvar_Get(&title_global<const char>(0x00834ef0), &title_global<const char>(0x0082e098), 0);  // cl_nodelta
    uint8_t* const debug_move =
        Cvar_Get(&title_global<const char>(0x00834ee0), &title_global<const char>(0x0082e098), 0);  // cl_debugMove
    title_global<uint8_t*>(0x00935334) = debug_move;
    return debug_move;
}

uint8_t* XB_CDECL IN_InitCvars() {
    const auto get = [](uint32_t name, uint32_t value, uint32_t flags) {
        return Cvar_Get(&title_global<const char>(name), &title_global<const char>(value), flags);
    };
    title_global<uint8_t*>(0x00927c34) = get(0x0083282c, 0x0082e098, 0x21);   // in_mouse "0"
    title_global<uint8_t*>(0x009284dc) = get(0x00832820, 0x0082e058, 0x21);   // in_joystick "1"
    title_global<uint8_t*>(0x00929b54) = get(0x00832808, 0x00832818, 1);      // in_joyBallScale "0.02"
    title_global<uint8_t*>(0x00929b18) = get(0x008327f4, 0x0082e098, 0x100);  // in_debugjoystick "0"
    uint8_t* const threshold = get(0x008327e0, 0x008327f0, 1);                // joy_threshold "35"
    title_global<uint8_t*>(0x00929c30) = threshold;
    return threshold;
}

uint8_t* XB_CDECL CL_InitRenderer() {
    title_function<void XB_CDECL(uint8_t*)>(title_global<uint32_t>(0x009284ec))(&title_global<uint8_t>(0x0092beb8));
    title_global<uint8_t*>(0x0092bf58) = RE_RegisterShader(0, &title_global<const char>(0x00834470));  // "nglWhite"
    uint8_t* const console = RE_RegisterShader(0, &title_global<const char>(0x00834468));             // "console"
    title_global<uint8_t*>(0x0092bf5c) = console;
    title_global<int32_t>(0x0092e848) = 1;
    return console;
}

uint32_t XB_REGS1 XB_CDECL CL_ShutdownCGame(uint32_t eax) {
    title_global<uint32_t>(0x0092bdf4) &= ~8u;  // cls.keyCatchers
    uint8_t*& cgvm = title_global<uint8_t*>(0x0093f7f8);
    if (!cgvm) return eax;
    title_function<int32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, int32_t)>(0x002a84e0)(0, cgvm, 1);  // VM_Call CG_SHUTDOWN
    const uint32_t result = Com_Memset(cgvm, 0, 0x8c);
    title_global<uint8_t*>(0x0095f514) = nullptr;  // currentVM
    cgvm = nullptr;
    return result;
}

int32_t XB_CDECL CL_ResetLocalConnection(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    uint8_t* const qport_cvar = Cvar_FindVar(&title_global<const char>(0x008344a8));  // "net_qport"
    const float qport = qport_cvar ? field<float>(qport_cvar, 0x1c) : 0.0f;
    uint8_t* const conn = &title_global<uint8_t>(0x00933608);
    std::memset(conn, 0, 0x830);
    field<uint32_t>(conn, 0x08) = a;
    field<uint32_t>(conn, 0x0c) = b;
    field<uint32_t>(conn, 0x10) = c;
    field<uint32_t>(conn, 0x14) = d;
    field<uint32_t>(conn, 0x18) = e;
    field<int32_t>(conn, 0x1c) = static_cast<int32_t>(qport);
    field<uint32_t>(conn, 0x24) = 1;
    const int32_t state = title_global<uint32_t>(0x0093f7f8) ? 5 : 1;  // cgvm
    title_global<int32_t>(0x0092bdf0) = state;                          // cls.state
    return state;
}

int32_t XB_REGS3 XB_CDECL CL_GetSnapshot(uint32_t, uint8_t* out, int32_t number) {
    if (number > title_global<int32_t>(0x0096016c))
        title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, &title_global<const char>(0x00835310));  // __break
    const int32_t behind = static_cast<int32_t>(static_cast<uint32_t>(title_global<int32_t>(0x0096016c)) -
                                                static_cast<uint32_t>(number));
    if (behind >= 1 || !title_global<int32_t>(0x009622d0)) return 0;
    const int32_t entities_behind = static_cast<int32_t>(title_global<uint32_t>(0x00961738) -
                                                         title_global<uint32_t>(0x00962814));
    if (entities_behind >= 0x800) return 0;
    field<uint32_t>(out, 0) = title_global<uint32_t>(0x009622d4);
    field<uint32_t>(out, 0x538) = title_global<uint32_t>(0x00962818);
    field<uint32_t>(out, 4) = title_global<uint32_t>(0x009622d8);
    std::memcpy(out + 0x10, &title_global<uint8_t>(0x009622f0), 0x148 * 4);
    return 1;
}

void XB_REGS3 XB_CDECL CL_JoystickMove(uint32_t, uint32_t, uint8_t* cmd) {
    const int32_t speed_active = title_global<int32_t>(0x00933fd0);  // in_speed.active
    const int32_t walk = cvar_integer(0x00929c04);
    if (walk != 1 && (walk == 2 || !(cvar_integer(0x00934c70) ^ speed_active))) cmd[4] |= 0x10;  // cl_run
    float anglespeed = static_cast<float>(title_global<int32_t>(0x0092be90));  // cls.frametime
    if (speed_active) anglespeed *= cvar_value(0x0093e6b4);                   // cl_anglespeedkey
    anglespeed *= title_global<float>(0x0083eb4c);
    platform_keep(anglespeed);
    const int32_t* axes = &title_global<int32_t>(0x00961750);
    auto* move = reinterpret_cast<int8_t*>(cmd);
    move[0x15] = clamp_char(move[0x15] + axes[0]);
    if (title_global<int32_t>(0x009341b0)) {
        float& pitch = title_global<float>(0x00961798);  // cl.viewangles[PITCH]
        pitch = static_cast<float>(axes[1]) * cvar_value(0x00934a60) * anglespeed + pitch;  // cl_pitchspeed
    } else {
        move[0x14] = clamp_char(move[0x14] + axes[1]);
    }
    move[0x16] = clamp_char(move[0x16] + axes[2]);
}

PLATFORM_ASM_ENTRY(CL_KeyMove_entry, "pushl %esi\n" PLATFORM_ASM_CALL(CL_KeyMove) "addl $4, %esp\n"
                                     "ret")

void XB_CDECL CL_KeyMove(uint8_t* cmd) {
    const auto key = [](uint32_t address) { return CL_KeyState(&title_global<uint8_t>(address)); };
    uint8_t stance;
    if (title_global<int32_t>(0x00934180)) {  // in_buttons[14]
        cmd[5] = uint8_t((cmd[5] & 0x3f) | 0x40);
        stance = cmd[5] | 0x02;
    } else if (title_global<int32_t>(0x00934000)) {  // the down key
        cmd[5] = uint8_t((cmd[5] & 0x3f) | 0x80);
        stance = cmd[5] | 0x02;
    } else {
        stance = cmd[5] & 0x3f;
        const int32_t mode = cvar_integer(0x00927b20);
        if (mode == 1)
            stance |= 0x80;
        else if (mode == 2)
            stance |= 0x40;
        cmd[5] = stance;
        stance &= 0xfd;
    }
    cmd[5] = stance;
    const int32_t walk = cvar_integer(0x00929c04);
    const bool walking = walk != 1 && (walk == 2 || !(cvar_integer(0x00934c70) ^ title_global<int32_t>(0x00933fd0)));
    cmd[4] = walking ? (cmd[4] | 0x10) : (cmd[4] & 0xef);

    const float plus = title_global<float>(0x0083ed88);    // 127
    const float minus = title_global<float>(0x0083ed84);   // -127
    int32_t side = 0;
    if (title_global<int32_t>(0x00933fb8)) {  // in_strafe: turning keys move sideways
        side = static_cast<int32_t>(key(0x00933f00) * plus);
        side += static_cast<int32_t>(key(0x00933ee8) * minus);
    }
    side += static_cast<int32_t>(key(0x00933f90) * plus);   // in_moveright
    side += static_cast<int32_t>(key(0x00933f78) * minus);  // in_moveleft
    const int32_t pm_type = title_global<int32_t>(0x009601a4);
    const uint32_t flags = title_global<uint32_t>(0x00960240);
    int32_t up = 0;
    if (pm_type == 2 || pm_type == 3 || ((flags & 0x100000) && !(flags & 0x400000)))
        up = static_cast<int32_t>(key(0x00934008) * plus);
    up += static_cast<int32_t>(key(0x00933fd8) * plus);  // in_up
    if (stance & 0x80) {
        if (title_global<int32_t>(0x00934000))
            up += static_cast<int32_t>(key(0x00933ff0) * minus);  // in_down
        else
            up -= 127;
    } else if (stance & 0x40) {
        up -= 127;
    }
    int32_t forward = static_cast<int32_t>(key(0x00933f18) * plus);  // in_forward
    forward += static_cast<int32_t>(key(0x00933f30) * minus);        // in_back
    if (flags & 0x6000) return;
    auto* move = reinterpret_cast<int8_t*>(cmd);
    move[0x14] = clamp_char(forward);
    move[0x15] = clamp_char(side);
    move[0x16] = clamp_char(up);
}

PLATFORM_ASM_ENTRY(CL_MouseMove_entry, "pushl %edi\n" PLATFORM_ASM_CALL(CL_MouseMove) "addl $4, %esp\n"
                                       "ret")

void XB_CDECL CL_MouseMove(uint8_t* cmd) {
    const float zero = 0.0f;
    const float half = title_global<float>(0x0083eb24);
    int32_t& index = title_global<int32_t>(0x0096174c);
    int32_t* const dx = &title_global<int32_t>(0x0096173c);
    int32_t* const dy = &title_global<int32_t>(0x00961744);
    float mx, my;
    if (cvar_integer(0x00935110)) {  // m_filter
        mx = static_cast<float>(dx[0] + dx[1]);
        my = static_cast<float>(dy[0] + dy[1]);
        mx = mx * half;
        my = my * half;
    } else {
        mx = static_cast<float>(dx[index]);
        my = static_cast<float>(dy[index]);
    }
    index ^= 1;
    dx[index] = 0;
    dy[index] = 0;
    const uint32_t msec = title_global<uint32_t>(0x0092f130);  // frame_msec
    float side, up;
    if (cvar_integer(0x009284dc)) {
        my = my * half;
        const float frame = platform_x87_mul(platform_x87_unsigned_to_float(msec), title_global<float>(0x0083eb4c));
        const float scale = title_global<float>(0x00960208);
        const float one = title_global<float>(0x0083eb20);
        float& boost = title_global<float>(0x00961780);
        const bool flick = platform_x87_greater(abs_bits(mx), title_global<float>(0x0083ed80)) &&
                           !title_global<int32_t>(0x00960680) && !(scale == one);
        if (flick && title_global<int32_t>(0x00960668) != 2) {
            boost = title_global<float>(0x008fef40) * frame + boost;
            const int32_t sign = mx > zero ? 1 : zero > mx ? -1 : 0;
            mx = static_cast<float>(sign) * boost + mx;
        } else if (scale == one) {
            float ramp = ((mx == zero && my == zero) ? zero - frame : frame) + boost;
            boost = ramp;
            const float limit = title_global<float>(0x008fef3c);
            if (zero > ramp) {
                ramp = zero;
                boost = ramp;
            } else if (ramp > limit) {
                ramp = limit;
                boost = ramp;
            }
            ramp = ramp / limit;
            mx = ramp * mx;
            my = ramp * my;
        } else {
            boost = zero;
        }
        side = frame * mx;
        up = frame * my;
    } else {
        const float slow = title_global<float>(0x0083eca0);  // 0.2
        side = mx * slow;
        up = my * slow;
    }
    const float floor_scale = title_global<float>(0x0083ed7c);  // 0.04
    float floor_x = static_cast<float>(title_global<int32_t>(0x008dce74)) * floor_scale;
    float floor_y = static_cast<float>(title_global<int32_t>(0x008dce78)) * floor_scale;
    if (floor_x == zero) floor_x = title_global<float>(0x0083ebf8);  // 0.1
    if (floor_y == zero) floor_y = title_global<float>(0x0083ebf8);
    const float frame_msec = platform_x87_unsigned_to_float(msec);
    const float rate = title_ci_length_over(side, up, frame_msec);
    const float sensitivity = cvar_value(0x0092f5e8) * rate;
    const float gain = title_global<float>(0x00961774);
    float xmove = gain * (sensitivity + floor_x);
    float ymove = gain * (sensitivity + floor_y);
    if ((title_global<uint32_t>(0x009601ac) & 0x4000) && !(title_global<uint32_t>(0x00960240) & 0x106000)) return;
    xmove = xmove * side;
    ymove = ymove * up;
    if (xmove == zero && ymove == zero) return;
    const float milli = title_global<float>(0x0083eb4c);
    const int32_t strafe = title_global<int32_t>(0x00933fb8);  // in_strafe
    auto* move = reinterpret_cast<int8_t*>(cmd);
    if (strafe) {
        move[0x15] = clamp_char(static_cast<int32_t>(cvar_value(0x00934c6c) * xmove) + move[0x15]);  // m_side
    } else {
        const float yaw_limit = title_global<float>(0x0096177c);
        float yaw = cvar_value(0x00929bf8) * xmove;  // m_yaw
        const float limit =
            (yaw_limit == zero ? frame_msec * title_global<float>(0x008fef38) : frame_msec * yaw_limit) * milli;
        const float low = yaw > limit ? limit : yaw;
        const float negative = zero - limit;
        if (negative > low)
            yaw = negative;
        else if (yaw > limit)
            yaw = limit;
        float& angle = title_global<float>(0x0096179c);
        angle = angle - yaw;
    }
    if ((title_global<int32_t>(0x009341b0) || cvar_integer(0x0093f808)) && !strafe) {  // in_mlook, freelook
        const float pitch_limit = title_global<float>(0x00961778);
        float pitch = cvar_value(0x00929c0c) * ymove;  // m_pitch
        if (!(pitch_limit == zero)) {
            const float limit = frame_msec * pitch_limit * milli;
            const float low = pitch > limit ? limit : pitch;
            const float negative = zero - limit;
            if (negative > low)
                pitch = negative;
            else if (pitch > limit)
                pitch = limit;
        }
        float& angle = title_global<float>(0x00961798);
        angle = angle + pitch;
        return;
    }
    move[0x14] = clamp_char(move[0x14] - static_cast<int32_t>(cvar_value(0x0092f13c) * ymove));  // m_forward
}

void XB_CDECL CL_AdjustAngles() {
    const auto key = [](uint32_t address) { return CL_KeyState(&title_global<uint8_t>(address)); };
    float* const viewangles = &title_global<float>(0x00961798);
    const float milli = title_global<float>(0x0083eb4c);
    const int32_t frametime = title_global<int32_t>(0x0092be90);  // cls.frametime
    if (cvar_integer(0x00929c28)) {
        const float zero = 0.0f;
        float target[3] = {zero, zero, zero};
        const float down = title_global<float>(0x0083eda4);  // -120
        const float up = title_global<float>(0x0083eb68);    // 120
        if (!(key(0x00933f00) == zero))  // in_right
            target[1] = down;
        else if (!(key(0x00933ee8) == zero))  // in_left
            target[1] = up;
        if (!(key(0x00933f48) == zero))
            target[0] = down;
        else if (!(key(0x00933f60) == zero))
            target[0] = up;
        if (title_global<int32_t>(0x00933fd0)) {  // in_speed
            const float factor = cvar_value(0x0093e6b4);  // cl_anglespeedkey
            target[0] = factor * target[0];
            target[1] = factor * target[1];
            target[2] = factor * target[2];
        }
        float* const speed = &title_global<float>(0x0098b800);
        const float half = title_global<float>(0x0083eb24);
        const float five = title_global<float>(0x0083ece8);
        const float scale = title_global<float>(0x0083ed98);  // 1/120
        const double power = title_global<double>(0x0083ed90);  // 5
        // clamp(pow(|delta| / 120, 5), 0.5, 5), the pow called again for each test like the original
        const auto rate = [&](float delta) {
            const float magnitude = abs_bits(delta);
            if (static_cast<long double>(half) > title_ci_pow_of_product(magnitude, scale, power)) return half;
            if (title_ci_pow_of_product(magnitude, scale, power) > static_cast<long double>(five)) return five;
            return static_cast<float>(title_ci_pow_of_product(magnitude, scale, power));
        };
        for (int32_t i = 0; i < 3; ++i) {
            const float accel = target[i] == zero ? title_global<float>(0x0083ed9c) : title_global<float>(0x0083eda0);
            if (target[i] > speed[i]) {
                const float factor = rate(speed[i] - target[i]);
                speed[i] = static_cast<float>(frametime) * factor * accel * milli + speed[i];
                if (speed[i] > target[i]) speed[i] = target[i];
            } else if (speed[i] > target[i]) {
                const float factor = rate(speed[i] - target[i]);
                speed[i] = speed[i] - static_cast<float>(frametime) * factor * accel * milli;
                if (target[i] > speed[i]) speed[i] = target[i];
            }
        }
        const float ft = static_cast<float>(frametime);
        const float yaw = ft * speed[1] * milli + viewangles[1];
        const float pitch = ft * speed[0] * milli + viewangles[0];
        viewangles[1] = yaw;
        viewangles[0] = pitch;
        return;
    }
    float speed = static_cast<float>(frametime);
    if (title_global<int32_t>(0x00933fd0)) speed *= cvar_value(0x0093e6b4);  // in_speed: cl_anglespeedkey
    speed *= milli;
    if (!title_global<int32_t>(0x00933fb8)) {  // not in_strafe
        const float right = key(0x00933f00) * speed;
        viewangles[1] = viewangles[1] - right * cvar_value(0x00929b1c);  // cl_yawspeed
        const float left = key(0x00933ee8) * speed;
        viewangles[1] = left * cvar_value(0x00929b1c) + viewangles[1];
    }
    const float lookup = key(0x00933f48) * cvar_value(0x00934a60);  // cl_pitchspeed
    viewangles[0] = viewangles[0] - lookup * speed;
    const float lookdown = key(0x00933f60) * cvar_value(0x00934a60);
    viewangles[0] = lookdown * speed + viewangles[0];
}

PLATFORM_ASM_ENTRY(CL_CreateCmd_entry, "pushl %ebx\n" PLATFORM_ASM_CALL(CL_CreateCmd) "addl $4, %esp\n"
                                       "ret")

uint8_t* XB_CDECL CL_CreateCmd(uint8_t* out) {
    float* const viewangles = &title_global<float>(0x00961798);
    const float old_pitch = viewangles[0];
    CL_AdjustAngles();
    uint8_t cmd[0x2c] = {};
    if (!cvar_integer(0x00934a5c)) {
        CL_CmdButtons(cmd);
        CL_KeyMove(cmd);
        CL_MouseMove(cmd);
        CL_JoystickMove(0, 0, cmd);
        const float zero = title_global<float>(0x0083eafc);
        char* const value_zero = &title_global<char>(0x0082e098);  // "0"
        if (!(cvar_value(0x0092f54c) == zero)) {
            viewangles[0] = viewangles[0] + cvar_value(0x0092f54c);
            Cvar_Set2(&title_global<const char>(0x00835158), value_zero, 1);  // "cl_viewPitchCompensate"
        }
        if (!(cvar_value(0x0093ef60) == zero)) {
            viewangles[1] = cvar_value(0x0093ef60) + viewangles[1];
            Cvar_Set2(&title_global<const char>(0x00835140), value_zero, 1);  // "cl_viewYawCompensate"
        }
        const float limit = title_global<float>(0x0083eb64);  // 90
        const float pitch = viewangles[0];
        if (pitch - old_pitch > limit)
            viewangles[0] = old_pitch + limit;
        else if (old_pitch - pitch > limit)
            viewangles[0] = old_pitch - limit;
    }
    CL_FinishMove(cmd);
    std::memcpy(out, cmd, sizeof cmd);
    return out;
}

void XB_CDECL CL_CreateNewCommands() {
    if (title_global<int32_t>(0x0092bdf0) != 2) return;  // cls.state
    const int32_t now = title_global<int32_t>(0x00947710);  // com_frameTime
    uint32_t& frame_msec = title_global<uint32_t>(0x0092f130);
    frame_msec = static_cast<uint32_t>(now - title_global<int32_t>(0x0092e94c));
    if (frame_msec > 100) frame_msec = 100;
    const int32_t number = ++title_global<int32_t>(0x009622ac);  // cl.cmdNumber
    title_global<int32_t>(0x0092e94c) = now;  // old frame time
    uint8_t cmd[0x2c];
    std::memcpy(&title_global<uint8_t>(0x009617ac) + (number & 63) * 0x2c, CL_CreateCmd(cmd), sizeof cmd);
}

PLATFORM_ASM_ENTRY(Key_KeynumToString_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Key_KeynumToString) "addl $8, %esp\n"
                   "ret")

const char* XB_CDECL Key_KeynumToString(int32_t keynum, uint32_t translate) {
    if (keynum == -1) return &title_global<const char>(0x00834560);  // "<KEY NOT FOUND>"
    if (keynum < 0 || keynum > 255) return &title_global<const char>(0x00834550);  // "<OUT OF RANGE>"
    if (translate && cvar_integer(0x0093f7fc) == 1 && keynum >= '0' && keynum <= '9')
        return title_global<const char*>(0x008d3858 + 4 * keynum);
    char* const tinystr = &title_global<char>(0x0098b5c8);
    if (keynum > ' ' && keynum < 0x7f && keynum != '"') {  // printable: the character itself
        tinystr[0] = static_cast<char>(title_function<int32_t XB_CDECL(int32_t)>(0x001efd6d)(keynum));  // _toupper
        tinystr[1] = 0;
        if (keynum != ';' || translate) return tinystr;
    }
    // keynamesType { const char* name; int32_t keynum; }, null-terminated
    for (auto* kn = &title_global<const uint32_t>(translate ? 0x008d3518 : 0x008d30b8); kn[0]; kn += 2)
        if (keynum == static_cast<int32_t>(kn[1])) return reinterpret_cast<const char*>(uintptr_t{kn[0]});
    const int32_t i = keynum >> 4;
    const int32_t j = keynum & 15;
    tinystr[0] = '0';
    tinystr[1] = 'x';
    tinystr[2] = static_cast<char>(i > 9 ? i + 'a' - 10 : i + '0');
    tinystr[3] = static_cast<char>(j > 9 ? j + 'a' - 10 : j + '0');
    tinystr[4] = 0;
    return tinystr;
}

int32_t XB_REGS1 XB_CDECL Char_IsDigit(int32_t c) { return c >= '0' && c <= '9' ? 1 : 0; }

PLATFORM_ASM_ENTRY(Key_StringToKeynum_entry, "pushl %edi\n" PLATFORM_ASM_CALL(Key_StringToKeynum) "addl $4, %esp\n"
                                             "ret")

int32_t XB_CDECL Key_StringToKeynum(const char* str) {
    if (!str || !str[0]) return -1;
    if (!str[1]) return static_cast<signed char>(str[0]);
    if (str[0] == '0' && str[1] == 'x' && std::strlen(str) == 4) {  // a hex number
        const auto digit = [](int32_t c) {
            if (Char_IsDigit(c)) return c - '0';
            return c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0;
        };
        const int32_t high = digit(static_cast<signed char>(str[2]));
        return high * 16 + digit(static_cast<signed char>(str[3]));
    }
    for (auto* kn = &title_global<const uint32_t>(0x008d30b8); kn[0]; kn += 2)  // keynames
        if (!Q_stricmpn(str, reinterpret_cast<const char*>(uintptr_t{kn[0]}), 99999)) return static_cast<int32_t>(kn[1]);
    return -1;
}

PLATFORM_ASM_ENTRY(Key_GetKeyNameForCommand_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Key_GetKeyNameForCommand) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Key_GetKeyNameForCommand(uint8_t* item) {
    static constexpr struct {
        uint32_t field;
        uint32_t command;
    } kCommands[] = {{0x48, 0x0082dfc0}, {0x4c, 0x0082dfb0}, {0x50, 0x0082dfa8}};  // +activate, toggle cl_run, +speed
    for (int32_t k = 0; k < 256; ++k) {
        const char* binding = title_global<const char*>(0x0093425c + 8 * k);  // keys[k].binding
        for (const auto& command : kCommands) {
            char* name = field<char*>(item, command.field);
            if (name[0] || !binding || Q_stricmpn(binding, &title_global<const char>(command.command), 99999)) continue;
            const char* key = Key_KeynumToString(k, 1);
            title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(name, key, 7);  // _strncpy
            name[7] = 0;
            break;
        }
    }
    item[0x54] = 1;
}

void XB_CDECL Key_ClearStates() {
    int32_t& anykeydown = title_global<int32_t>(0x0092eff0);
    anykeydown = 0;
    uint8_t* const keys = &title_global<uint8_t>(0x00934258);  // qkey_t { down, binding }
    for (int32_t i = 0; i < 256; ++i) {
        uint8_t* key = keys + 8 * i;
        if (key[0] & 3) {  // CL_KeyEvent(i, false), inlined
            field<int32_t>(key, 0) = 0;
            if (--anykeydown < 0) anykeydown = 0;
            const bool console_key = i == '`' || i == '~';
            const bool menu_escape = title_global<int32_t>(0x009617a8) && (title_global<uint8_t>(0x0092bdf4) & 1) && i == 0x1b;
            const char* binding = field<const char*>(key, 4);
            if (!console_key && !menu_escape && binding && binding[0] == '+') {
                char cmd[0x400];
                const uint32_t args[4] = {0x008344e8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(binding + 1)),
                                          static_cast<uint32_t>(i), 0};  // "-%s %i %i\n"
                Com_sprintf(cmd, sizeof cmd, args);
                Cbuf_AddText(cmd);
            }
        }
        field<int32_t>(key, 0) = 0;
    }
}

void XB_CDECL CL_DObjCreateSkeleton(uint32_t, DObj* obj) {
    auto* raw = reinterpret_cast<uint8_t*>(obj);
    if (field<uint32_t>(raw, 0x20)) return;
    Z_MallocDebug((uint32_t{raw[0x2f]} << 6) + 0x30);
    DObj_CreateSkeleton(obj);
}

PLATFORM_ASM_ENTRY(CL_DObjGetBoneMatrix_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CL_DObjGetBoneMatrix) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL CL_DObjGetBoneMatrix(uint32_t name, DObj* obj) {
    const int32_t bone = DObj_GetTagIndex(obj, name);
    if (bone < 0) return nullptr;
    auto* raw = reinterpret_cast<uint8_t*>(obj);
    if (!field<uint32_t>(raw, 0x20)) {
        Z_MallocDebug((uint32_t{raw[0x2f]} << 6) + 0x30);
        DObj_CreateSkeleton(obj);
    }
    uint8_t* const skeleton = field<uint8_t*>(raw, 0x20);
    if (!skeleton) return reinterpret_cast<uint8_t*>(uintptr_t(uint32_t(bone) << 6));
    return skeleton + 0x30 + (uint32_t{raw[0x60]} << 6) + (uint32_t(bone) << 6);
}

namespace {

uint8_t* kbutton(uint32_t address) { return &title_global<uint8_t>(address); }

constexpr uint32_t kInUp = 0x00934008, kInUpAlt = 0x00933fd8, kInDown = 0x00933ff0;
constexpr uint32_t kInDownActive = kInDown + kActive;

// -(pitch delta) * the short-to-angle scale, as 0 - x (keeps +0 for a zero delta).
void center_pitch() {
    const float delta = float(title_global<int32_t>(0x009601d4)) * title_global<float>(0x0083ebd4);
    title_global<float>(0x00961798) = 0.0f - delta;
}

void set_stance_cvar(uint32_t name, bool one) {
    Cvar_Set2(&title_global<const char>(name), const_cast<char*>(&title_global<const char>(one ? 0x0082e058 : 0x0082e098)),
              1);
}

}  // namespace

void XB_CDECL IN_CenterView() {
    if (!cvar_integer(0x00929c28)) center_pitch();
}

void XB_CDECL IN_MLookUp() {
    title_global<int32_t>(0x009341b0) = 0;
    if (!cvar_integer(0x0093f808) && !cvar_integer(0x00929c28)) center_pitch();
}

void XB_CDECL IN_MLookDown() { title_global<int32_t>(0x009341b0) = 1; }

void XB_CDECL IN_LeanRightUp() { IN_KeyUp(kbutton(0x00934158)); }
void XB_CDECL IN_LeanRightDown() { IN_KeyDown(kbutton(0x00934158)); }
void XB_CDECL IN_LeanLeftUp() { IN_KeyUp(kbutton(0x00934140)); }
void XB_CDECL IN_LeanLeftDown() { IN_KeyDown(kbutton(0x00934140)); }
void XB_CDECL IN_ReloadUp() { IN_KeyUp(kbutton(0x00934128)); }
void XB_CDECL IN_ReloadDown() { IN_KeyDown(kbutton(0x00934128)); }
void XB_CDECL IN_ActivateUp() { IN_KeyUp(kbutton(0x009340b0)); }
void XB_CDECL IN_ActivateDown() { IN_KeyDown(kbutton(0x009340b0)); }
void XB_CDECL IN_Button0Up() { IN_KeyUp(kbutton(0x00934020)); }
void XB_CDECL IN_Button0Down() { IN_KeyDown(kbutton(0x00934020)); }
void XB_CDECL IN_StrafeUp() { IN_KeyUp(kbutton(0x00933fa8)); }
void XB_CDECL IN_StrafeDown() { IN_KeyDown(kbutton(0x00933fa8)); }
void XB_CDECL IN_SpeedUp() { IN_KeyUp(kbutton(0x00933fc0)); }
void XB_CDECL IN_SpeedDown() { IN_KeyDown(kbutton(0x00933fc0)); }
void XB_CDECL IN_MoverightUp() { IN_KeyUp(kbutton(0x00933f90)); }
void XB_CDECL IN_MoverightDown() { IN_KeyDown(kbutton(0x00933f90)); }
void XB_CDECL IN_MoveleftUp() { IN_KeyUp(kbutton(0x00933f78)); }
void XB_CDECL IN_MoveleftDown() { IN_KeyDown(kbutton(0x00933f78)); }
void XB_CDECL IN_LookdownUp() { IN_KeyUp(kbutton(0x00933f60)); }
void XB_CDECL IN_LookdownDown() { IN_KeyDown(kbutton(0x00933f60)); }
void XB_CDECL IN_LookupUp() { IN_KeyUp(kbutton(0x00933f48)); }
void XB_CDECL IN_LookupDown() { IN_KeyDown(kbutton(0x00933f48)); }
void XB_CDECL IN_BackUp() { IN_KeyUp(kbutton(0x00933f30)); }
void XB_CDECL IN_BackDown() { IN_KeyDown(kbutton(0x00933f30)); }
void XB_CDECL IN_ForwardUp() { IN_KeyUp(kbutton(0x00933f18)); }
void XB_CDECL IN_ForwardDown() { IN_KeyDown(kbutton(0x00933f18)); }
void XB_CDECL IN_RightUp() { IN_KeyUp(kbutton(0x00933f00)); }
void XB_CDECL IN_RightDown() { IN_KeyDown(kbutton(0x00933f00)); }
void XB_CDECL IN_LeftUp() { IN_KeyUp(kbutton(0x00933ee8)); }
void XB_CDECL IN_LeftDown() { IN_KeyDown(kbutton(0x00933ee8)); }

void XB_CDECL IN_UpUp() {
    IN_KeyUp(kbutton(kInUp));
    IN_KeyUp(kbutton(kInUpAlt));
}

// Jump: stands up one stance step (prone -> crouch -> stand) while not holding crouch, else jumps.
void XB_CDECL IN_UpDown() {
    IN_KeyDown(kbutton(kInUp));
    if (title_global<int32_t>(0x00934180) || title_global<int32_t>(kInDownActive)) return;
    const int32_t stance = cvar_integer(0x00927b20);
    if (stance > 1)
        set_stance_cvar(0x008351c0, true);
    else if (stance > 0)
        set_stance_cvar(0x008351c0, false);
    else
        IN_KeyDown(kbutton(kInUpAlt));
}

void XB_CDECL IN_DownUp() {
    IN_KeyUp(kbutton(kInDown));
    set_stance_cvar(0x008351cc, title_global<int32_t>(0x00934180) || title_global<int32_t>(kInDownActive));
}

void XB_CDECL IN_DownDown() {
    IN_KeyDown(kbutton(kInDown));
    set_stance_cvar(0x008351cc, title_global<int32_t>(0x00934180) || title_global<int32_t>(kInDownActive));
}

// keys[256] (qkey_t: bit 0 down, bits 2+ repeats; binding at +4), anykeydown.
void XB_REGS3 XB_CDECL CL_KeyEvent(int32_t down, uint32_t, int32_t key, int32_t time) {
    uint8_t* const keys = &title_global<uint8_t>(0x00934258);
    int32_t& word = field<int32_t>(keys + 8 * key, 0);
    int32_t& anykeydown = title_global<int32_t>(0x0092eff0);
    word = (word & ~3) | (down & 3);
    uint32_t catchers = title_global<uint32_t>(0x0092bdf4);
    if (down) {
        word = ((word & ~3) + 4) | (word & 3);
        const int32_t repeats = word >> 2;
        if (repeats == 1) {
            ++anykeydown;
        } else if (repeats > 1) {
            if (!(catchers & 1) || key == '`' || key == '~' || key == 0x1b) return;
        }
    } else {
        word &= 3;
        if (--anykeydown < 0) anykeydown = 0;
    }
    if (key == 0xa2) {
        if (down && (title_global<uint8_t>(0x00934880) & 3)) return;
    } else if (key == '`' || key == '~') {
        return;
    }
    if (title_global<int32_t>(0x009617a8)) {  // a scripted camera
        if (!(catchers & 3)) {
            if (key == 0x1b || key == ' ' || key == '\r') {
                if (down) {
                    CL_AddReliableCommand(&title_global<const char>(0x0083450c));  // "cameraInterrupt"
                    return;
                }
            } else if (down) {
                return;
            }
        }
        if ((catchers & 1) && key == 0x1b) return;
    }
    int32_t state = title_global<int32_t>(0x0092bdf0);
    if (down && (key < 0x80 || key == 0xc8) && (state == 3 || state == 4) && !catchers) {
        Cvar_Set2(&title_global<const char>(0x00834500), const_cast<char*>(&title_global<const char>(0x0082dfeb)),
                  1);  // nextdemo ""
        state = title_global<int32_t>(0x0092bdf0);
        catchers = title_global<uint32_t>(0x0092bdf4);
        key = 0x1b;
    }
    if (key == 0x1b && down) {
        if (catchers & 8) {
            title_global<uint32_t>(0x0092bdf4) = catchers & ~8u;
            return;
        }
        if (catchers & 2) return;
        if (state == 2) {
            const uint8_t* intro = Cvar_Get(&title_global<const char>(0x008344f4), &title_global<const char>(0x0082e058), 0);
            if (title_global<uint8_t>(0x0095f919) || field<int32_t>(const_cast<uint8_t*>(intro), 0x20)) return;
            title_global<uint8_t>(0x0095f919) = 1;
            TitleRegs regs{};
            regs.esi = title_global<uint32_t>(0x0095f8fc);
            cg_call_regs(0x0032dde0, &regs, nullptr, 0);  // CL_TogglePause
            title_global<uint32_t>(0x00934330) &= 3;     // keys[K_ESCAPE].repeats = 0
        } else if (state == 3 || state == 4) {
            TitleRegs regs{};
            regs.ebx = title_global<uint32_t>(0x0095f4c4);
            cg_call_regs(0x002c8d60, &regs, nullptr, 0);  // S_StopAllSounds
        }
        return;
    }
    const char* const binding = field<const char*>(keys + 8 * key, 4);
    char cmd[0x400];
    if (!down) {
        if (!binding || binding[0] != '+') return;
        const uint32_t args[4] = {0x008344e8, uint32_t(reinterpret_cast<uintptr_t>(binding + 1)), uint32_t(key),
                                  uint32_t(time)};  // "-%s %i %i\n"
        Com_sprintf(cmd, sizeof cmd, args);
        Cbuf_AddText(cmd);
        return;
    }
    if ((catchers & 3) || !state) return;
    if (binding) {
        if (binding[0] == '+') {
            const uint32_t args[4] = {0x008344dc, uint32_t(reinterpret_cast<uintptr_t>(binding)), uint32_t(key),
                                      uint32_t(time)};  // "%s %i %i\n"
            Com_sprintf(cmd, sizeof cmd, args);
            Cbuf_AddText(cmd);
        } else {
            Cbuf_AddText(binding);
            Cbuf_AddText(&title_global<const char>(0x0082d288));  // "\n"
        }
        return;
    }
    // Unbound: Key_KeynumToString inlined into its static buffer; the "unbound" print is gone.
    if (key < 0xc8 || key == -1 || key < 0 || key > 0xff) return;
    char* const name = &title_global<char>(0x0098b5c8);
    if (key > ' ' && key < 0x7f && key != '"') {
        name[0] = char(title_function<int32_t XB_CDECL(int32_t)>(0x001efd6d)(key));  // toupper
        name[1] = 0;
        if (key != ';') return;
    }
    for (const uint8_t* entry = &title_global<uint8_t>(0x008d30b8); field<uint32_t>(const_cast<uint8_t*>(entry), 0);
         entry += 8)
        if (key == field<int32_t>(const_cast<uint8_t*>(entry), 4)) return;
    const int32_t high = key >> 4, low = key & 15;
    name[0] = '0';
    name[1] = 'x';
    name[2] = char(high > 9 ? high + 0x57 : high + 0x30);
    name[3] = char(low > 9 ? low + 0x57 : low + 0x30);
    name[4] = 0;
}

namespace {

void stop_all_sounds() {
    TitleRegs regs{};
    regs.ebx = title_global<uint32_t>(0x0095f4c4);
    cg_call_regs(0x002c8d60, &regs, nullptr, 0);  // S_StopAllSounds (sound system in EBX)
}

void stop_all_channels() {
    title_function<void XB_STDCALL(uint32_t)>(0x002c8c20)(title_global<uint32_t>(0x0095f4c4));  // S_UpdateChannels
}

}  // namespace

void XB_CDECL CL_ClearState() {
    stop_all_sounds();
    stop_all_channels();
    using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
    for (uint32_t slot = 0x009606b8; slot < 0x009616b8; slot += 4) {  // cl.configstrings
        auto* string = &title_global<ScriptString>(slot);
        if (!string->rep) continue;
        ScriptString empty;
        ScriptString_FromRep(&empty, nullptr);
        title_function<Assign>(0x0011d600)(string, &empty);                    // ScriptString_Assign
        title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&empty);  // ScriptString_Release
    }
    std::memset(&title_global<uint8_t>(0x00960160), 0, 0x9b0 * 4);  // cl
    std::memset(&title_global<uint8_t>(0x0092f5f0), 0, 0x1212 * 4);  // clc
}

void XB_CDECL CL_MapLoading() {
    title_global<uint8_t>(0x0095f916) = 1;
    title_global<uint32_t>(0x0092bdf4) = 0;  // cls.keyCatchers
    Cvar_Set2(&title_global<const char>(0x008344c8), const_cast<char*>(&title_global<const char>(0x0082dfeb)),
              1);  // nextmap ""
    const uint8_t* const dedicated = title_global<uint8_t*>(0x00947704);
    if (dedicated && field<int32_t>(const_cast<uint8_t*>(dedicated), 0x20)) {
        Cvar_Set2(&title_global<const char>(0x00835238), const_cast<char*>(&title_global<const char>(0x0082e058)),
                  1);  // r_uiFullScreen 1
        CL_ClearState();
        title_global<int32_t>(0x0092bdf0) = 0;  // CA_UNINITIALIZED
    }
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(
        &title_global<char>(0x0092be00), &title_global<const char>(0x008344bc), 0x7f);  // servername "localhost"
    title_global<char>(0x0092be7f) = 0;
    title_global<int32_t>(0x0092bdf0) = 1;
    title_function<void XB_CDECL()>(0x002f1e60)();  // SCR_UpdateScreen
    stop_all_sounds();
    stop_all_channels();
}

namespace {

constexpr uint32_t kMissionHud = 0x0095f8d0;

int32_t sys_milliseconds() { return title_function<int32_t XB_CDECL()>(0x002b1ca0)(); }

// Title callees run at the ESP the original's calls have (title code below keeps addresses of its stack locals in
// globals, 0x0095e2d0); `top` is the ESP before the call's arguments are pushed.
// Plain arrays: std::initializer_list's out-of-line accessors (unoptimized builds) bind to the loader's copies.
struct Args {
    uint32_t words[3];
    uint32_t count;
};

void call_at(uint32_t top, uint32_t function, const TitleRegs& regs, const Args& args = {{}, 0}) {
    title_call_at_regs(top, function, &regs, args.count, args.words);
}

void draw_mission_overlays(uint32_t top) {
    const TitleRegs regs = {0, 0, 0, 0, kMissionHud, 0};
    if (title_global<uint8_t>(0x0095f919)) call_at(top, 0x003153d0, regs);
    if (title_global<uint8_t>(0x0095f918)) call_at(top, 0x00315440, regs);
}

uint32_t float_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

}  // namespace

void SCR_DrawScreenField(uint32_t entry) {
    call_at(entry, title_global<uint32_t>(0x00928548), {}, Args{{0}, 1});  // re.BeginFrame(0)
    const int32_t state = title_global<int32_t>(0x0092bdf0);   // cls.state
    if (state == 5) return;
    const uint32_t top = entry - 4;  // push esi
    switch (state) {
    case 0:
        title_global<uint32_t>(0x0092bdf4) = 2;  // KEYCATCH_UI
        call_at(top, 0x000c53b0, {});
        Cvar_Set2(&title_global<const char>(0x0082c89c), const_cast<char*>(&title_global<const char>(0x0082e098)),
                  1);  // g_reloading 0
        break;
    case 1:
        draw_mission_overlays(top);
        break;
    case 2:
        call_at(top, 0x0028b340, {0, 0, title_global<uint32_t>(0x009606b0), 0, 0, 0},
                Args{{0, 0, title_global<uint32_t>(0x0092be94)}, 3});  // CG_DrawActiveFrame
        break;
    default:
        call_at(top, 0x002938f0, {}, Args{{0, 0x00833ff8}, 2});  // __break
        break;
    }
    draw_mission_overlays(top);
}

PLATFORM_ASM_ENTRY(SCR_UpdateScreen_entry, "movl %esp, %eax\n"
                                           GAME_GAP_ESP("0x20000")
                                           "pushl %ebp\n"
                                           "movl %eax, %ebp\n"
                                           "pushl %ebp\n" PLATFORM_ASM_CALL(SCR_UpdateScreen) "addl $4, %esp\n"
                                           "popl %ebp\n"
                                           "popl %esp\n"
                                           "ret")

void XB_CDECL SCR_UpdateScreen(uint32_t entry) {
    if (!title_global<int32_t>(0x00933ee4)) return;  // UI not started
    const uint32_t top = entry - 0x8c;                  // sub esp,0x84; push ebx; push esi
    int32_t& state = title_global<int32_t>(0x0092bdf0);
    if (state == 2) {
        int32_t& frame = title_global<int32_t>(0x0093fc28);
        if (++frame == 0) frame = 1;
        title_global<int32_t>(0x0095f4ec) = 1;
    }
    if (!title_global<uint8_t>(0x009403f4)) {
        SCR_DrawScreenField(top - 4);
        call_at(top, title_global<uint32_t>(0x0092854c), {}, Args{{0x009403f8, 0x0093fce8}, 2});  // re.EndFrame
    }
    // static int last = Sys_Milliseconds() on the first call
    uint32_t& guard = title_global<uint32_t>(0x0098f2c4);
    int32_t& last = title_global<int32_t>(0x0098f2c0);
    int32_t previous;
    if (!(guard & 1)) {
        guard |= 1;
        previous = sys_milliseconds();
    } else {
        previous = last;
    }
    const int32_t now = sys_milliseconds();
    last = now;
    const float seconds = float(now - previous) * title_global<float>(0x0083eb4c);
    int32_t& level = title_global<int32_t>(0x0095f920);  // the level to start, -1 = none
    if (level > -1) {
        static constexpr uint32_t kMaps[14] = {0x00833fec, 0x0082bc60, 0x00833fd8, 0x0082bc48, 0x0082bc3c,
                                               0x0082cc0c, 0x0082bc34, 0x0082bc28, 0x0082bc20, 0x0082bcc0,
                                               0x0082bcb8, 0x0082bcac, 0x0082bca0, 0x0082bc90};
        if (level > 13) return;
        char* const cmd = reinterpret_cast<char*>(uintptr_t{top + 0xc});  // the original's local buffer
        title_function<int32_t XB_CDECL(char*, const char*, const char*)>(0x001ee901)(
            cmd, &title_global<const char>(0x00833fe0), &title_global<const char>(kMaps[level]));  // "spmap %s"
        level = -1;
        Cmd_ExecuteRegisteredCommand(cmd);
    }
    const uint32_t time = float_bits(seconds);
    if (title_global<uint8_t>(0x0095f918)) {
        call_at(top, 0x00315560, {0, 0, 0, 0, kMissionHud, 0}, Args{{time}, 1});
    } else if (title_global<uint8_t>(0x0095f919)) {
        call_at(top, 0x00315520, {0, 0, 0, 0, kMissionHud, 0}, Args{{time}, 1});
    } else {
        const uint32_t hud = title_global<uint32_t>(0x0095f8f0);  // g_hudManager, vtable +0xc
        call_at(top, field<uint32_t>(title_global<uint8_t*>(hud), 0xc), {0, 0, hud, 0, 0, 0}, Args{{time}, 1});
    }
    if (state == 2) title_global<int32_t>(0x0095f4ec) = 0;
}

// The C frame sits 0x30000 below the original's (File_PumpRequests' pass-through reads zero
// 128 KB below their ESP in tests: 0x20000 lost the frame, 2 of 302 tests in mission 6);
// `regs` = the entry registers (TitleRegs order).
PLATFORM_ASM_ENTRY(CL_Frame_entry, "pushl %ebp\n"
                                   "movl %esp, %ebp\n"
                                   GAME_GAP_ESP("0x30000")
                                   "pushl %edi\n"
                                   "pushl %esi\n"
                                   "pushl %edx\n"
                                   "pushl %ecx\n"
                                   "pushl %ebx\n"
                                   "pushl %eax\n"
                                   "movl %esp, %ecx\n"
                                   "leal 4(%ebp), %edx\n"
                                   "pushl %ecx\n"
                                   "pushl %edx\n" PLATFORM_ASM_CALL(CL_Frame) "movl %ebp, %esp\n"
                                   "popl %ebp\n"
                                   "ret")

void XB_CDECL CL_Frame(uint32_t entry, const TitleRegs* in) {
    if (!cvar_integer(0x00947704)) return;  // com_cl_running
    // push ebp; and esp,-16; sub esp,0x18; push ebx; push esi
    const uint32_t top = ((entry - 4) & ~uint32_t{15}) - 0x20;
    TitleRegs regs = *in;
    title_global<uint32_t>(top + 4) = in->ebx;
    title_global<uint32_t>(top) = in->esi;
    int32_t& state = title_global<int32_t>(0x0092bdf0);      // cls.state
    uint32_t& catchers = title_global<uint32_t>(0x0092bdf4);  // cls.keyCatchers
    uint32_t& skip_menu = title_global<uint32_t>(0x0092bdf8);
    int32_t msec = int32_t(in->eax);
    if (skip_menu) {
        skip_menu = 0;
    } else if (state == 0 && !(catchers & 2) && !cvar_integer(0x0093fcf0)) {  // com_sv_running
        regs.eax = 0;
        regs.ecx = title_global<uint32_t>(0x0093fcf0);
        regs.ebx = title_global<uint32_t>(0x0095f4c4);
        regs.esi = uint32_t(msec);
        call_at(top, 0x002c8d60, regs);  // S_StopAllSounds (sound system in EBX)
        catchers = 2;                    // KEYCATCH_UI
        call_at(top, 0x000c53b0, regs);  // S_ClearVoiceAttenuation
        Cvar_Set2(&title_global<const char>(0x0082c89c), const_cast<char*>(&title_global<const char>(0x0082e098)),
                  1);  // g_reloading 0
    }
    const int32_t avidemo = cvar_integer(0x009284e0);  // cl_avidemo
    if (avidemo && msec) {
        if (state == 2 || cvar_integer(0x00929bf4)) {
            regs.eax = uint32_t(avidemo);
            regs.ebx = avidemo > 0 ? 0x00834494 : 0x0083447c;  // "screenshot silent\n" / "screenshotjpeg silent\n"
            regs.esi = uint32_t(msec);
            call_at(top, 0x002c6140, regs);  // Cmd_ExecuteString (text in EBX)
        }
        const int32_t fps = cvar_integer(0x009284e0);
        const uint32_t sign = uint32_t(fps >> 31);
        const int32_t magnitude = int32_t((uint32_t(fps) ^ sign) - sign);
        const float timescale = field<float>(title_global<uint8_t*>(0x0093fce4), 0x1c);
        msec = int32_t(title_global<float>(0x0083eb9c) / float(magnitude) * timescale);
        if (!msec) msec = 1;
    }
    title_global<int32_t>(0x0092be9c) = msec;  // cls.realFrametime
    title_global<int32_t>(0x0092be98) += title_global<int32_t>(0x0092be90);
    regs.esi = uint32_t(msec);
    if (state == 2) {
        bool commands = true;
        if (cvar_integer(0x0093fcf0) && cvar_integer(0x0093fcd8) == 1) commands = false;
        if (commands) {
            call_at(top, 0x002cd450, regs);  // CL_CreateNewCommands
            if (state != 3 && state != 4) call_at(top, 0x002b37e0, regs);  // CL_WritePacket
        }
        if (state == 2) {
            if (!title_global<int32_t>(0x00960160)) {  // cl.snap.valid
                call_at(top, 0x002938f0, regs, Args{{1, 0x008351dc}, 2});  // __break
            }
            const uint32_t server_time = title_global<uint32_t>(0x0092f094);
            title_global<uint32_t>(0x009606b0) = server_time;
            title_global<uint32_t>(0x009606b4) = server_time;
        }
    }
    // The loader's --level hook replaces this call's target (loader/patches.cpp kCallClientFrame).
    const uint32_t screen = 0x002f27a9 + title_global<uint32_t>(0x002f27a5);
    regs.eax = uint32_t(state);
    call_at(top, screen, regs);  // SCR_UpdateScreen
    const int32_t paused = cvar_integer(0x0093fcd8);  // cl_paused
    regs.ebx = uint32_t(paused);
    const float seconds = float(paused ? 0 : msec) * title_global<float>(0x0083eb4c);
    const uint32_t time = float_bits(seconds);
    title_global<uint32_t>(top + 0xc) = time;
    call_at(top, 0x000ca690, regs);  // File_PumpRequests
    const uint32_t sound = title_global<uint32_t>(0x0095f4c4);
    call_at(top, 0x002c8c20, regs, Args{{sound}, 1});  // S_UpdateChannels
    const uint32_t banks = title_global<uint32_t>(0x0095f4c0);
    call_at(top, 0x002c8f60, regs, Args{{banks}, 1});  // S_CheckWaveBankNotifies
    if (title_global<uint32_t>(0x0095e608)) {
        const uint8_t* const player = field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4);
        const uint32_t origin = top + 0x10;
        std::memcpy(&title_global<uint8_t>(origin), player + 0x140, 16);
        TitleRegs at_cell = regs;
        at_cell.eax = origin;
        at_cell.edx = origin;
        const uint32_t cell = title_call_at_regs(top, 0x0023cc80, &at_cell, 2, Args{{origin, 0}, 2}.words);  // CM_CellForPoint
        const uint32_t stream = title_global<uint32_t>(0x0095f640);
        const uint32_t view[4] = {stream, cell, origin, 0};
        title_call_at_regs(top, 0x002914f0, &regs, 4, view);  // Stream_SetViewPosition
    }
    regs.esi = time;
    regs.eax = title_global<uint32_t>(0x0095f638);
    call_at(top, 0x00292c20, regs);  // Stream_Update
    regs.eax = title_global<uint32_t>(0x0095f4cc);
    call_at(top, 0x002c8830, regs, Args{{time}, 1});  // S_UpdateCrossfade
    regs.eax = title_global<uint32_t>(0x0095f5b4);
    call_at(top, 0x002a5980, regs, Args{{time}, 1});  // FX_UpdateEffects
    call_at(top, 0x00298810, regs, Args{{title_global<uint32_t>(0x0095f5c4), time}, 2});  // CL_UpdateRumble
    regs.eax = title_global<uint32_t>(0x0095f4f8);
    call_at(top, 0x002ea4f0, regs, Args{{regs.eax, time}, 2});  // S_UpdateAmbientEmitters
    regs.eax = 0x0097a8c0;
    call_at(top, 0x0024b140, regs);  // G_EntityList_Compact
    ++title_global<int32_t>(0x0092be8c);  // cls.framecount
}

PLATFORM_ASM_ENTRY(CL_TogglePause_entry, "pushl %esi\n" PLATFORM_ASM_CALL(CL_TogglePause) "addl $4, %esp\n"
                                         "ret")

void XB_CDECL CL_TogglePause(uint8_t* menu) {
    const auto set_paused = [] {
        Cvar_Set2(&title_global<const char>(0x0082ca00), const_cast<char*>(&title_global<const char>(0x0082e058)),
                  1);  // cl_paused 1
    };
    title_global<uint8_t>(0x0095f919) = 1;
    set_paused();
    uint8_t* const overlay = field<uint8_t*>(field<uint8_t*>(title_global<uint8_t*>(0x0095f8fc), 4), 0);
    title_function<void XB_REGS1 XB_CDECL(uint32_t)>(0x002b6be0)(title_global<uint32_t>(0x0095f4c4));  // S_PauseAllChannels
    set_paused();
    overlay[0x40] = title_global<uint8_t>(0x008dce69);
    uint8_t* const object = field<uint8_t*>(field<uint8_t*>(title_global<uint8_t*>(0x0095f8fc), 4), 0);
    using Show = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
    title_function<Show>(field<uint32_t>(field<uint8_t*>(object, 0), 0x88))(object, 0, 1);
    using Activate = void XB_THISCALL(uint8_t*, uint32_t);
    title_function<Activate>(field<uint32_t>(field<uint8_t*>(menu, 0), 0xc))(menu, 0);
}

// The manager's +0x0c is tail-called at the entry ESP (its sound keeps stack addresses), the screen word still on
// the stack.
PLATFORM_ASM_ENTRY(CL_PauseIntoFrontEnd_entry, "pushl %esi\n" PLATFORM_ASM_CALL(CL_PauseIntoFrontEnd) "addl $4, %esp\n"
                                               "movl (%esi), %eax\n"
                                               "movl %esi, %ecx\n"
                                               "jmp *0xc(%eax)")

void XB_CDECL CL_PauseIntoFrontEnd(uint8_t*) {
    title_global<uint8_t>(0x0095f919) = 1;
    Cvar_Set2(&title_global<const char>(0x0082ca00), const_cast<char*>(&title_global<const char>(0x0082e058)),
              1);  // cl_paused 1
}

PLATFORM_ASM_ENTRY(DifficultyMenu_StartGame_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(DifficultyMenu_StartGame) "addl $8, %esp\n"
                   "ret")

void XB_CDECL DifficultyMenu_StartGame(int32_t difficulty, uint8_t* menu) {
    int32_t& skill = title_global<int32_t>(0x008dce50);
    switch (difficulty) {
    case 0:
        SV_SetSkillHard();
        skill = 0;
        break;
    case 1:
        SV_SetSkillMedium();
        skill = 1;
        break;
    case 2:
        SV_SetSkillEasy();
        skill = 2;
        break;
    default:
        return;
    }
    title_global<uint8_t>(0x00940178) = 1;
    S_StopAllSounds(title_global<uint8_t*>(0x0095f4c4));
    Movie_Stop(0);
    title_global<int32_t>(0x0095f920) = field<int32_t>(menu, 0x48);  // the level to start
    int32_t chapter = field<int32_t>(menu, 0x48);
    int32_t& current = title_global<int32_t>(0x008dce48);  // g_currentChapter
    current = chapter;
    if (chapter < 0) {
        chapter = 0;
        current = 0;
    }
    int32_t& unlocked = title_global<int32_t>(0x008dce44);  // g_chaptersUnlocked
    if (unlocked < chapter) unlocked = chapter;
}

void XB_CDECL AtExit_ReleaseScriptStrings_9616b8() {
    for (uint32_t i = 1; i <= 0x400; ++i)
        title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(0x009616b8 - i * 4);  // ScriptString_Release
}

void XB_CDECL AtExit_StringVector_9600fc() {
    if (uint8_t* const first = title_global<uint8_t*>(0x009600fc)) {
        std_string_DestroyRange(first, title_global<uint8_t*>(0x00960100));
        Z_Free(title_global<uint8_t*>(0x009600fc));
    }
    title_global<uint32_t>(0x009600fc) = 0;
    title_global<uint32_t>(0x00960100) = 0;
    title_global<uint32_t>(0x00960104) = 0;
}

void XB_REGS3 XB_CDECL CL_PauseForDialog(uint32_t, uint8_t keep_running, uint8_t* dialog) {
    const int32_t mode = field<int32_t>(dialog, 0xc);
    const bool developer = cvar_integer(0x0093fcd8) != 0;
    if (mode < 0) dialog[0x20] = developer ? uint8_t(dialog[0x20] | 1) : uint8_t(dialog[0x20] & ~1);
    if (!keep_running && mode != 1) dialog[0x20] = mode == 0 ? uint8_t(dialog[0x20] | 2) : uint8_t(dialog[0x20] & ~2);
    using Activate = void XB_THISCALL(uint8_t*, uint32_t);
    title_function<Activate>(field<uint32_t>(field<uint8_t*>(dialog, 0), 0xc))(dialog, keep_running == 0);
    if (cvar_integer(0x0093fcd8)) return;
    if (title_global<uint32_t>(0x0095f8f8) && title_global<uint8_t>(0x0095f918)) return;
    Cvar_Set2(&title_global<const char>(0x0082ca00), const_cast<char*>(&title_global<const char>(0x0082e058)),
              1);  // cl_paused 1
}

namespace {

// One subtitle line: yellowish unless its flag word is set (then color 0), unsigned pixel position.
void draw_subtitle_line(uint32_t text, uint32_t x, uint32_t y, uint32_t flag) {
    const uint32_t color = title_global<uint16_t>(flag) ? 0 : 0xe0ffff00;
    nglListAddTextf(title_global<uint8_t*>(0x0095f8e0), platform_x87_unsigned_to_float(title_global<uint32_t>(x)),
                    platform_x87_unsigned_to_float(title_global<uint32_t>(y)), 0.0f, color, 1.0f, 1.0f,
                    reinterpret_cast<const char*>(uintptr_t{text}));
}

}  // namespace

void XB_CDECL Subtitle_Draw() {
    if (!title_global<uint8_t>(0x008dce68)) return;  // subtitles on
    const uint32_t first = title_global<uint32_t>(0x0095f474);
    if (!first) return;
    if (!(title_global<float>(0x0092708c) >= title_global<float>(0x00926e74))) return;  // comiss; jb (NaN: off)
    draw_subtitle_line(first, 0x00926e80, 0x00926e2c, 0x00926e78);
    const uint32_t second = title_global<uint32_t>(0x0095f470);
    if (second) draw_subtitle_line(second, 0x00926e28, 0x00927088, 0x00926e40);
}

// The tail jump to SCR_UpdateScreen runs at the entry ESP, as in the original.
PLATFORM_ASM_ENTRY(SCR_UpdateLoadingDots_entry, "pushl %esi\n" PLATFORM_ASM_CALL(SCR_UpdateLoadingDots) "addl $4, %esp\n"
                                                "testl %eax, %eax\n"
                                                "jz 1f\n"
                                                "jmp *%eax\n"
                                                "1: ret")

uint32_t XB_CDECL SCR_UpdateLoadingDots(uint8_t* screen) {
    int32_t& ticks = field<int32_t>(screen, 0x40);
    if (++ticks > 10) {
        int32_t& dot = field<int32_t>(screen, 0x3c);
        ticks = 0;
        if (++dot > 2) dot = 0;
        using Show = void XB_THISCALL(uint8_t*, uint32_t);
        const auto show = [](uint8_t* panel, uint32_t alpha) {
            title_function<Show>(field<uint32_t>(field<uint8_t*>(panel, 0), 0x7c))(panel, alpha);
        };
        for (uint32_t i = 0; i < 3; ++i) show(field<uint8_t*>(screen, 0x44 + i * 4), 0);
        show(field<uint8_t*>(screen, 0x44 + uint32_t(dot) * 4), 0x3f800000);
    }
    return field<int32_t>(title_global<uint8_t*>(0x0095f8fc), 0xc) == 5 ? 0x002f1e60 : 0;
}

const char* XB_REGS3 XB_CDECL Key_TranslateButtonName(const char* name, uint32_t, char* out) {
    using Compare = int32_t XB_CDECL(const char*, const char*);
    using CompareN = int32_t XB_CDECL(const char*, const char*, uint32_t);
    if (title_function<CompareN>(0x001f1486)(name, &title_global<const char>(0x008377b0), 2) == 0) {  // "t:"
        title_function<int32_t XB_CDECL(char*, const char*, const char*)>(0x001ee901)(
            out, &title_global<const char>(0x0082ddd4), name + 2);  // sprintf "%s"
        return out;
    }
    for (uint32_t i = 0; i < 0x13; ++i) {
        if (title_function<Compare>(0x001f1443)(name, title_global<const char*>(0x008d5a98 + i * 8)) == 0)  // __stricmp
            return title_global<const char*>(0x008d5a9c + i * 8);
    }
    return nullptr;
}

void XB_CDECL SCR_RedrawFrontBuffer() {
    NglQuad quad{};
    nglInitQuad(&quad);
    nglBeginFrame();
    nglSetClearFlags(0);
    nglEndFrame();
    nglResetDeviceBindings();
    nglSetQuadTexture(&quad, nglGetFrontBufferTexture());
    nglSetQuadRect(&quad, 0.0f, 0.0f, 640.0f, 480.0f);
    nglBeginFrame();
    nglSetClearFlags(0xf3);
    nglListAddQuad(&quad);
    nglEndFrame();
    nglResetDeviceBindings();
}

void XB_CDECL CL_Setenv_f() {
    const uint32_t argc = cmd_argc();
    if (int32_t(argc) <= 2) return;
    char buffer[0x400];  // the putenv text; nothing reads it (Q3 called putenv)
    std::strcpy(buffer, argc > 1 ? cmd_argv(1) : &title_global<const char>(0x0082dfeb));
    std::strcat(buffer, &title_global<const char>(0x008344b8));  // "="
    for (uint32_t i = 2; int32_t(i) < int32_t(argc); ++i) {
        std::strcat(buffer, i < argc ? cmd_argv(i) : &title_global<const char>(0x0082dfeb));
        std::strcat(buffer, &title_global<const char>(0x0082d6e4));  // " "
    }
}

void XB_CDECL CL_ShellExecute_URL_f() {
    const char* verb = &title_global<const char>(0x0082dfeb);
    if (cmd_argc() > 1) {
        verb = cmd_argv(1);
        if (!verb) return;
    }
    if (Q_stricmpn(verb, &title_global<const char>(0x00834430), 99999)) return;  // "open"
    const uint32_t argc = cmd_argc();
    if (int32_t(argc) < 4) return;
    title_atol(argc > 3 ? cmd_argv(3) : &title_global<const char>(0x0082dfeb));  // the show flag, unused
}

void XB_CDECL CL_ForwardToServer_f() {
    if (title_global<uint32_t>(0x0092bdf0) != 2 || int32_t(cmd_argc()) <= 1) return;  // CA_CONNECTED
    CL_AddReliableCommand(Cmd_ArgsFrom(1));
}

void XB_CDECL Key_Bindlist_f() {
    for (int32_t i = 0; i < 256; ++i) {
        const char* binding = title_global<const char*>(0x0093425c + 8 * uint32_t(i));
        if (binding && binding[0]) Key_KeynumToString(i, 0);  // the Com_Printf is compiled out
    }
}

void XB_CDECL CL_ShutdownInput() {
    // CL_InitInput's commands without the last six
    static constexpr uint32_t kNames[] = {
        0x00835134, 0x0082d62c, 0x0083512c, 0x00835120, 0x00835114, 0x0083510c, 0x00835104, 0x008350fc,
        0x008350f4, 0x008350e8, 0x008350dc, 0x008350d4, 0x008350cc, 0x008350c4, 0x008350bc, 0x008350b0,
        0x008350a4, 0x0083509c, 0x00835094, 0x00835088, 0x0083507c, 0x00835070, 0x00835064, 0x0082dfa8,
        0x0083505c, 0x0082d6ac, 0x00835054, 0x0082d694, 0x0083504c, 0x0082dfc0, 0x00835040, 0x0082d640,
        0x00835038, 0x0082be38, 0x0083502c, 0x0082be5c, 0x00835020, 0x00835018, 0x00835010, 0x0082d654,
        0x00835008, 0x00835000, 0x00834ff8, 0x0082d610, 0x0082bed8, 0x00834fe8, 0x00834fdc, 0x00834fd4,
        0x00834fc8, 0x00834fbc, 0x00834fb0, 0x0082d5ac, 0x00834f98, 0x0082d5d4, 0x00834f84, 0x0082d678,
        0x00834f74, 0x00834f64, 0x00834f54,
    };
    for (const uint32_t name : kNames) Cmd_RemoveCommand(&title_global<const char>(name));
}

namespace {

// cl_testAnimWeight stepped by the frame time over the rate cvar (0x0093fce4; 0 counts as 1) * 0.002.
float anim_weight_step(float rate) {
    if (rate == 0.0f) rate = 1.0f;
    return static_cast<float>(title_global<int32_t>(0x0092be90)) / rate * 0.002f;  // cls.frametime
}

}  // namespace

void XB_CDECL CL_DecAnimWeight_f() {
    const float weight = cvar_value(0x00929b0c);
    float value = weight - anim_weight_step(cvar_value(0x0093fce4));
    if (0.0f > value) value = 0.0f;
    Cvar_SetValue(&title_global<const char>(0x0083441c), value);  // cl_testAnimWeight
}

void XB_CDECL CL_IncAnimWeight_f() {
    const float weight = cvar_value(0x00929b0c);
    float value = anim_weight_step(cvar_value(0x0093fce4)) + weight;
    if (value > 1.0f) value = 1.0f;
    Cvar_SetValue(&title_global<const char>(0x0083441c), value);  // cl_testAnimWeight
}

namespace {

// Key_SetBinding(key, ""), inlined.
void clear_binding(uint32_t key) {
    const char*& binding = title_global<const char*>(0x0093425c + 8 * key);
    if (binding) title_function<void XB_CDECL(const void*)>(0x000428d0)(binding);  // Z_Free
    binding = CopyString(&title_global<const char>(0x0082dfeb));
    title_global<uint32_t>(0x009403ec) |= 1;  // cvar_modifiedFlags
}

}  // namespace

void XB_CDECL Key_Unbindall_f() {
    for (uint32_t i = 0; i < 256; ++i)
        if (title_global<const char*>(0x0093425c + 8 * i)) clear_binding(i);
}

void XB_CDECL Key_Unbind_f() {
    if (cmd_argc() != 2) return;
    const int32_t key = Key_StringToKeynum(cmd_argv(1));
    if (key == -1) return;
    clear_binding(uint32_t(key));
}

PLATFORM_ASM_ENTRY(CL_ExecControllerConfig_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CL_ExecControllerConfig) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CL_ExecControllerConfig(int32_t style) {
    Cvar_SetValue(&title_global<const char>(0x0082e084), static_cast<float>(style));  // cg_controllerconfig
    uint32_t text = 0x0082dff4;  // "exec style_h.cfg\n"
    if (style == 1)
        text = 0x0082e030;  // style_e
    else if (style == 2)
        text = 0x0082e01c;  // style_f
    else if (style == 3)
        text = 0x0082e008;  // style_g
    Cbuf_AddText(&title_global<const char>(text));
    title_function<void XB_CDECL()>(0x002c62b0)();  // Cbuf_Execute (a tail jump)
}
// ---- Pads (XInput) ----
//
// Per port: the XINPUT_STATE at 0x00902290 (0x16 bytes), the XINPUT_FEEDBACK at 0x009022e8 (0x46 bytes: status
// dword, rumble left/right words at +0x42/+0x44) and the pad block at 0x00902400 (0x58 bytes: the gamepad copy
// +0x00, previous buttons +0x12, analog buttons down +0x14, pressed edges +0x1c, released edges +0x26,
// capabilities +0x30, handle +0x4c, inserted +0x50, removed +0x54). Rumble per pad at 0x00902568: {left current,
// left target, right current, right target} words.

namespace {

constexpr uint32_t kPadStates = 0x00902290, kPadStateSize = 0x16;
constexpr uint32_t kPadFeedback = 0x009022e8, kPadFeedbackSize = 0x46;
constexpr uint32_t kGamepadType = 0x007a9f78;  // XDEVICE_TYPE_GAMEPAD

uint8_t* pad_state(int32_t pad) { return reinterpret_cast<uint8_t*>(uintptr_t{kPadStates + uint32_t(pad) * kPadStateSize}); }
uint8_t* pad_feedback(int32_t pad) {
    return reinterpret_cast<uint8_t*>(uintptr_t{kPadFeedback + uint32_t(pad) * kPadFeedbackSize});
}
uint32_t& pad_handle(int32_t pad) { return field<uint32_t>(pad_block(pad), 0x4c); }

// rep stosd + stosw over count dwords and a word.
void zero_words(uint8_t* p, uint32_t dwords) {
    for (uint32_t i = 0; i < dwords; ++i) field<uint32_t>(p, i * 4) = 0;
    field<uint16_t>(p, dwords * 4) = 0;
}

uint32_t xinput_open(int32_t port) {
    // XINPUT_POLLING_PARAMETERS: fAutoPoll | fInterruptOut, 8 ms in and out.
    uint8_t params[4] = {0x03, 0x08, 0x08, 0x00};
    return title_function<uint32_t XB_STDCALL(uint32_t, int32_t, uint32_t, uint8_t*)>(0x007abf0d)(kGamepadType, port, 0,
                                                                                                 params);
}
void xinput_get_capabilities(uint32_t handle, uint8_t* caps) {
    title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*)>(0x007abf6f)(handle, caps);
}
void xinput_get_state(uint32_t handle, uint8_t* state) {
    title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*)>(0x007ac147)(handle, state);
}
void xinput_set_state(uint32_t handle, uint8_t* feedback) {
    title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*)>(0x007ac1ba)(handle, feedback);
}

// The analog buttons as down flags: over 0x1e, the triggers (6, 7) over 0x80.
void set_analog_down(uint8_t* block, const uint8_t* analog) {
    for (int32_t i = 0; i < 8; ++i) block[0x14 + i] = analog[i] > ((i == 7 || i == 6) ? 0x80 : 0x1e);
}

// A freshly opened pad: capabilities, state, buttons and analog flags.
void pad_read_initial(int32_t port) {
    uint8_t* block = pad_block(port);
    uint8_t* state = pad_state(port);
    xinput_get_capabilities(pad_handle(port), block + 0x30);
    xinput_get_state(pad_handle(port), state);
}

// Sys_QueEvent (0x00222fa0; time in EAX, 0 = now).
void sys_que_event(int32_t time, int32_t type, int32_t value, int32_t value2) {
    title_function<void XB_REGS1 XB_CDECL(int32_t, int32_t, int32_t, int32_t, int32_t, void*)>(0x00222fa0)(
        time, type, value, value2, 0, nullptr);
}

// Sys_QueEvent(0, SE_KEY, key, 0, 0, NULL) inlined: the event ring 0x0095e800 (64 x 0x18 bytes, head 0x0095e7f4,
// tail 0x0095ee04; a full ring drops the oldest event and frees its data).
void queue_key_up(int32_t key) {
    uint32_t& head = title_global<uint32_t>(0x0095e7f4);
    uint32_t& tail = title_global<uint32_t>(0x0095ee04);
    uint8_t* event = reinterpret_cast<uint8_t*>(uintptr_t{0x0095e800 + (head & 0x3f) * 0x18});
    if (static_cast<int32_t>(head - tail) >= 0x40) {
        if (field<uint32_t>(event, 0x14)) title_function<void XB_CDECL(uint32_t)>(0x000428d0)(field<uint32_t>(event, 0x14));
        ++tail;
    }
    ++head;
    field<int32_t>(event, 0x00) = sys_milliseconds();
    field<int32_t>(event, 0x04) = 1;  // SE_KEY
    field<int32_t>(event, 0x08) = key;
    field<int32_t>(event, 0x0c) = 0;
    field<int32_t>(event, 0x10) = 0;
    field<int32_t>(event, 0x14) = 0;
}

}  // namespace

PLATFORM_ASM_ENTRY(IN_PadConsumeAllPressed_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(IN_PadConsumeAllPressed) "addl $4, %esp\n"
                   "ret")

void XB_CDECL IN_PadConsumeAllPressed(int32_t pad) {
    for (uint32_t button = 0; button <= 15; ++button) IN_PadConsumePressed(pad, button);
}

PLATFORM_ASM_ENTRY(IN_JoyApplyThreshold_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(IN_JoyApplyThreshold) "addl $4, %esp\n"
                   "ret")

int32_t XB_CDECL IN_JoyApplyThreshold(int32_t value) {
    const int32_t threshold = static_cast<int32_t>(cvar_value(0x00929c30));
    const uint32_t sign_mask = static_cast<uint32_t>(value >> 31);
    const int32_t magnitude = static_cast<int32_t>((static_cast<uint32_t>(value) ^ sign_mask) - sign_mask);
    if (magnitude < threshold) return 0;
    const float scaled = static_cast<float>(magnitude - threshold) / static_cast<float>(0x80 - threshold) *
                         title_global<float>(0x0083ebe8);  // 128
    const int32_t sign = value >= 0 ? 1 : -1;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(scaled)) * static_cast<uint32_t>(sign));
}

void XB_CDECL IN_PadCheckDeviceChanges() {
    uint32_t insertions, removals;
    title_function<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*)>(0x007ac272)(kGamepadType, &insertions,
                                                                                    &removals);  // XGetDeviceChanges
    for (int32_t port = 0; port < 4; ++port) {
        const uint32_t bit = 1u << port;
        uint8_t* block = pad_block(port);
        field<uint32_t>(block, 0x54) = (removals & bit) != 0;
        if (removals & bit) {
            if (pad_handle(port)) title_function<void XB_STDCALL(uint32_t)>(0x007abf63)(pad_handle(port));  // XInputClose
            pad_handle(port) = 0;
        }
        field<uint32_t>(block, 0x50) = (insertions & bit) != 0;
        if (!(insertions & bit)) continue;
        zero_words(pad_feedback(port), 0x11);
        pad_handle(port) = xinput_open(port);
        if (!pad_handle(port)) continue;
        pad_read_initial(port);
        field<uint16_t>(block, 0x12) = field<uint16_t>(pad_state(port), 4);
        set_analog_down(block, pad_state(port) + 6);
        if (port == 0) title_function<void XB_CDECL(uint32_t)>(0x001fa100)(pad_handle(0));
    }
}

uint8_t* XB_THISCALL IN_PadInit(uint8_t* pads) {
    title_function<void XB_STDCALL(uint32_t, void*)>(0x007ac24b)(0, nullptr);  // XInitDevices
    const uint32_t devices = title_function<uint32_t XB_STDCALL(uint32_t)>(0x007ac250)(kGamepadType);  // XGetDevices
    for (int32_t port = 0; port < 4; ++port) {
        zero_words(pad_state(port), 5);
        uint8_t* block = pad_block(port);
        for (uint32_t i = 0; i < 0x16; ++i) field<uint32_t>(block, i * 4) = 0;
        zero_words(pad_feedback(port), 0x11);
        if (!(devices & (1u << port))) continue;
        pad_handle(port) = xinput_open(port);
        pad_read_initial(port);
        xinput_set_state(pad_handle(port), pad_feedback(port));
        field<uint16_t>(block, 0x12) = field<uint16_t>(pad_state(port), 4);
        set_analog_down(block, pad_state(port) + 6);
    }
    return pads;
}

uint8_t* XB_CDECL IN_PadCreate() {
    uint8_t*& pads = title_global<uint8_t*>(0x00902564);
    if (pads) return pads;
    uint8_t* memory = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(0x18);  // Z_Malloc
    pads = memory ? IN_PadInit(memory) : nullptr;
    return pads;
}

void XB_CDECL IN_PadUpdate() {
    IN_PadCheckDeviceChanges();
    for (int32_t port = 0; port < 4; ++port) {
        if (!pad_handle(port)) continue;
        uint8_t* block = pad_block(port);
        uint8_t* state = pad_state(port);
        xinput_get_state(pad_handle(port), state);
        for (uint32_t i = 0; i < 0x10; i += 4) field<uint32_t>(block, i) = field<uint32_t>(state, 4 + i);
        field<uint16_t>(block, 0x10) = field<uint16_t>(state, 0x14);
        uint8_t* live = title_global<uint8_t*>(0x0095f6fc);  // g_liveMgr
        bool taken = false;
        if (!title_global<uint8_t>(0x0095f538) && live && live[0x44cb]) {
            const uint32_t method = field<uint32_t>(field<uint8_t*>(live, 0), 0x18);
            taken = title_function<uint8_t XB_THISCALL(uint8_t*, int32_t, uint8_t*)>(method)(live, port, state) != 0;
        }
        if (taken) {
            title_global<uint8_t>(0x00902560) = 1;
        } else {
            const uint16_t previous = field<uint16_t>(block, 0x12), buttons = field<uint16_t>(block, 0);
            title_global<uint8_t>(0x00902560) = 0;
            field<uint16_t>(block, 0x1c) = static_cast<uint16_t>(~previous & buttons);
            field<uint16_t>(block, 0x26) = static_cast<uint16_t>(~buttons & previous);
        }
        field<uint16_t>(block, 0x12) = field<uint16_t>(block, 0);
        for (int32_t i = 0; i < 8; ++i) {
            const uint8_t down = block[2 + i] > ((i == 7 || i == 6) ? 0x80 : 0x1e);
            if (!pad_input_blocked()) {
                const uint8_t was = block[0x14 + i];
                block[0x1e + i] = !was && down;
                block[0x28 + i] = was && !down;
            }
            block[0x14 + i] = down;
        }
    }
}

uint8_t XB_STDCALL IN_PadReturnFalse(int32_t, uint32_t) { return 0; }

void XB_STDCALL IN_SetRumbleMotor(int32_t pad, int32_t motor, float strength) {
    const int32_t value = platform_x87_mul_ftol(strength, title_global<float>(0x0083eeec));  // 65535
    uint16_t* rumble = &title_global<uint16_t>(0x00902568 + uint32_t(pad) * 8);
    uint16_t* motor_words = rumble + (motor != 1 ? 2 : 0);  // {current, target}
    motor_words[1] = static_cast<uint16_t>(value);
    uint8_t* feedback = pad_feedback(pad);
    if (field<uint32_t>(feedback, 0)) return;  // the last set is still pending
    rumble[0] = field<uint16_t>(feedback, 0x42);
    rumble[2] = field<uint16_t>(feedback, 0x44);
    if (motor_words[0] == motor_words[1]) return;
    zero_words(feedback, 0x11);
    field<uint16_t>(feedback, 0x42) = rumble[1];
    field<uint16_t>(feedback, 0x44) = rumble[3];
    if (pad_handle(pad)) xinput_set_state(pad_handle(pad), feedback);
}

void XB_CDECL IN_PadStopAllRumble() {
    for (int32_t port = 0; port < 4; ++port) {
        uint8_t* feedback = pad_feedback(port);
        if (field<uint32_t>(feedback, 0)) continue;
        zero_words(feedback, 0x11);
        if (pad_handle(port)) xinput_set_state(pad_handle(port), feedback);
    }
}

void XB_REGS1 XB_CDECL IN_StopAllRumble(uint8_t* rumble) {
    for (int32_t motor = 0; motor < 2; ++motor) {
        const int32_t pad = title_global<int32_t>(0x008dcf18);
        if (pad != -1) {
            IN_PadCreate();
            IN_SetRumbleMotor(pad, motor, 0.0f);
        }
        // Frees the list of queued rumbles (head at +8 + 16 * motor, its end sentinel 4 bytes on).
        uint8_t* list = rumble + 8 + motor * 0x10;
        uint32_t first = field<uint32_t>(list, 0);
        const uint32_t end = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list + 4));
        uint32_t next = first ? *reinterpret_cast<uint32_t*>(uintptr_t{first}) : 0;
        if (first == end) {
            next = 0;
            first = 0;
        }
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x000248f0)(first, next, end, 0,
                                                                                                   0);
    }
}

void XB_CDECL IN_SprintUp() {}

void XB_CDECL IN_SprintDown() {}

// The pad's keys as they map to key events: buttons 0-3 = 0x9c, 0x9b, 0x9d, 0x9a; 14 = escape, 15 = tab; the
// rest 0xcf + button.
namespace {
int32_t pad_button_key(uint32_t button) {
    switch (button) {
    case 0: return 0x9c;
    case 1: return 0x9b;
    case 2: return 0x9d;
    case 3: return 0x9a;
    case 5: return 0xd4;
    case 14: return 0x1b;
    case 15: return 0x09;
    default: return static_cast<int32_t>(button + 0xcf);
    }
}
}  // namespace

void XB_CDECL IN_PadQueueEvents() {
    const int32_t pad = title_global<int32_t>(0x008d50d8);
    IN_PadCreate();
    for (uint32_t button = 0; button <= 15; ++button) IN_PadConsumePressed(pad, button);
    static const int32_t kKeys[6] = {0x1b, 0x09, 0x9c, 0x9d, 0x9b, 0x9a};
    for (int32_t key : kKeys) queue_key_up(key);
    for (int32_t i = 0; i <= 15; ++i) queue_key_up(0xcf + i);
}

void XB_CDECL IN_Frame() {
    if (!cvar_integer(0x009284dc)) return;
    IN_PadCreate();
    IN_PadUpdate();
    uint8_t& active = title_global<uint8_t>(0x008d50dc);
    if (title_global<int32_t>(0x008d50d8) != -1) {
        IN_PadCreate();
        if (!IN_PadIsConnected(title_global<int32_t>(0x008d50d8))) {
            active = 0;
            return;
        }
    }
    if (!active) IN_PadQueueEvents();
    const int32_t selected = title_global<int32_t>(0x008d50d8);
    const uint8_t keys_off = title_global<uint8_t>(0x0095f919);
    const int32_t pad = selected != -1 ? selected : 0;
    active = 1;
    if (title_global<uint8_t>(0x0095f538)) return;
    if (!keys_off) {
        int32_t x, y;
        IN_PadCreate();
        IN_PadGetStick(pad, 1, &x, &y);
        x = IN_JoyApplyThreshold(x);
        y = IN_JoyApplyThreshold(y);
        if (x || y) sys_que_event(0, 3, x, y);
        IN_PadCreate();
        IN_PadGetStick(pad, 0, &x, &y);
        x = IN_JoyApplyThreshold(x);
        y = IN_JoyApplyThreshold(y);
        sys_que_event(pad, 4, 1, static_cast<int32_t>(0u - static_cast<uint32_t>(y)));  // time = the pad (as the original)
        sys_que_event(pad, 4, 0, x);
    }
    for (uint32_t button = 0; button <= 15; ++button) {
        const int32_t key = pad_button_key(button);
        IN_PadCreate();
        if (IN_PadButtonPressed(pad, button)) {
            title_global<int32_t>(0x008dcf18) = pad;
            if (!keys_off) sys_que_event(0, 1, key, 1);
        }
        IN_PadCreate();
        if (IN_PadConsumeReleased(pad, button)) queue_key_up(key);
    }
}

// --- Loading screen spinner, subtitles during loads, configstrings, the text field ---

namespace {

// The title clock (RDTSC) through XAPI QueryPerformanceCounter (0x001ed6ca), whose RDTSC site the loader patches
// like the inline ones the original uses here.
uint64_t loading_clock() {
    uint32_t counter[2] = {0, 0};
    title_function<uint32_t XB_STDCALL(uint32_t*)>(0x001ed6ca)(counter);
    return (uint64_t{counter[1]} << 32) | counter[0];
}

// acc + float(ticks) / 733333.3 * 0.001 on the x87: the unsigned 64-bit tick count as FILD of the low 63 bits plus
// 2^63 for the top bit (stored as float), then FDIV, FMUL, FADD without storing in between.
float loading_seconds_add(uint64_t ticks, float acc) {
    const uint32_t low_part[2] = {static_cast<uint32_t>(ticks), static_cast<uint32_t>(ticks >> 32) & 0x7fffffffu};
    const uint32_t sign_part[2] = {0, static_cast<uint32_t>(ticks >> 32) & 0x80000000u};
    float seconds;
    __asm__("fildll %1\n\tfildll %2\n\tfchs\n\tfaddp %%st, %%st(1)\n\tfstps %0"
            : "=m"(seconds)
            : "m"(low_part), "m"(sign_part)
            : "st", "st(1)");
    float result;
    __asm__("flds %1\n\tfdivs %2\n\tfmuls %3\n\tfadds %4\n\tfstps %0"
            : "=m"(result)
            : "m"(seconds), "m"(title_global<float>(0x0083ed30)), "m"(title_global<float>(0x0083eb4c)), "m"(acc)
            : "st");
    return result;
}

// The spinner's clock: 0.15 s per frame of the 8-frame icon. `guard`/`ticks`/`time` are the caller's statics.
void loading_icon_step(uint32_t guard_address, uint32_t ticks_address, uint32_t time_address) {
    uint32_t& guard = title_global<uint32_t>(guard_address);
    uint64_t last;
    if (!(guard & 1)) {
        guard |= 1;
        last = loading_clock();
    } else {
        last = (uint64_t{title_global<uint32_t>(ticks_address + 4)} << 32) | title_global<uint32_t>(ticks_address);
    }
    const uint64_t now = loading_clock();
    float& time = title_global<float>(time_address);
    time = loading_seconds_add(now - last, time);
    int32_t steps = 0;
    const float step = title_global<float>(0x0083ed2c);  // 0.15
    if (platform_x87_greater_equal(time, step)) {
        float rest = time;
        do {
            rest -= step;
            ++steps;
        } while (rest >= step);
        time = rest;
    }
    int32_t& frame = title_global<int32_t>(0x0095f604);  // scr_loadingIconFrame
    frame = (frame + steps) & 7;
    title_global<uint32_t>(ticks_address) = static_cast<uint32_t>(now);
    title_global<uint32_t>(ticks_address + 4) = static_cast<uint32_t>(now >> 32);
}

void loading_icon_draw(bool full, bool text) {
    if (full) nglResetDeviceBindings();
    for (int32_t i = full ? 1 : 2; i > 0; --i) {
        if (full) {
            nglBeginFrame();
            nglSetClearFlags(0xf3);
            nglSetSceneDepthEnable(0);
        }
        if (text) SCR_DrawLoadingText();
        SCR_DrawLoadingIcon();
        if (full) nglEndFrame();
    }
}

// MSVC's unsigned to double (FILD, FADD 2^32 when negative, FSTP QWORD) times 0.5 (FMUL QWORD), _ftol.
int32_t unsigned_half_ftol(uint32_t value) {
    double converted;
    if (static_cast<int32_t>(value) < 0)
        __asm__("fildl %1\n\tfaddl %2\n\tfstpl %0" : "=m"(converted) : "m"(value), "m"(title_global<double>(0x0083ecc0))
                : "st");
    else
        __asm__("fildl %1\n\tfstpl %0" : "=m"(converted) : "m"(value) : "st");
    int64_t result;
    uint16_t control, chop;
    __asm__("fnstcw %0" : "=m"(control));
    chop = static_cast<uint16_t>(control | 0x0c00);
    __asm__("fldl %1\n\tfmull %2\n\tfldcw %3\n\tfistpll %0\n\tfldcw %4"
            : "=m"(result)
            : "m"(converted), "m"(title_global<double>(0x0083ecb0)), "m"(chop), "m"(control)
            : "st");
    return static_cast<int32_t>(result);
}

// _ftol(a * b) with a float a and a double b (FLD, FMUL QWORD, chopping FISTP through 64 bits).
int32_t mul_double_ftol(float a, double b) {
    int64_t result;
    uint16_t control, chop;
    __asm__("fnstcw %0" : "=m"(control));
    chop = static_cast<uint16_t>(control | 0x0c00);
    __asm__("flds %1\n\tfmull %2\n\tfldcw %3\n\tfistpll %0\n\tfldcw %4"
            : "=m"(result)
            : "m"(a), "m"(b), "m"(chop), "m"(control)
            : "st");
    return static_cast<int32_t>(result);
}

// acc + ms * 0.001 (FILD, FMUL QWORD, FADD) on the x87, stored as float.
float add_milliseconds(int32_t ms, float acc) {
    float result;
    __asm__("fildl %1\n\tfmull %2\n\tfadds %3\n\tfstps %0"
            : "=m"(result)
            : "m"(ms), "m"(title_global<double>(0x0083ef88)), "m"(acc)
            : "st");
    return result;
}

uint32_t vm_call1(uint32_t command) {
    return title_function<uint32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, uint32_t)>(0x002a84e0)(
        0, title_global<uint8_t*>(0x0093f7f8), command);  // VM_Call(cgvm, command)
}

uint32_t vm_call2(uint32_t command, uint32_t arg) {
    return title_function<uint32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, uint32_t, uint32_t)>(0x002a84e0)(
        0, title_global<uint8_t*>(0x0093f7f8), command, arg);
}

}  // namespace

void XB_CDECL SCR_DrawLoadingIcon() {
    NglQuad quad{};
    nglInitQuad(&quad);
    nglSetQuadRect(&quad, title_global<float>(0x008fef60), title_global<float>(0x008fef58),
                   title_global<float>(0x008fef5c), title_global<float>(0x008fef54));
    nglSetQuadTexture(&quad, title_global<NglTexture*>(0x0095f5e4 + 4 * title_global<uint32_t>(0x0095f604)));
    nglSetQuadBlend(&quad, 2, 0);
    nglListAddQuad(&quad);
}

void XB_CDECL SCR_UpdateLoadingIcon(uint8_t full) {
    loading_icon_step(0x0098b6e0, 0x0098b6d8, 0x0098ad40);
    loading_icon_draw(full != 0, false);
}

PLATFORM_ASM_ENTRY(SCR_UpdateLoadingIcon_2_entry, "pushl %ebx\n" PLATFORM_ASM_CALL(SCR_UpdateLoadingIcon_2) "addl $4, %esp\n"
                                                  "ret")

void XB_CDECL SCR_UpdateLoadingIcon_2(uint32_t ebx) {
    loading_icon_step(0x0098ba60, 0x0098ba58, 0x0098ad3c);
    loading_icon_draw(static_cast<uint8_t>(ebx) != 0, true);
}

void XB_CDECL SCR_DrawLoadingText() {
    const char* text;
    switch (title_global<int32_t>(0x0095f5dc)) {  // g_languageIndex
    case 1: text = &title_global<const char>(0x00837768); break;  // "Laden ..."
    case 2: text = &title_global<const char>(0x0083773c); break;  // "Chargement en cours"
    case 3: text = &title_global<const char>(0x00837730); break;  // "Cargando"
    case 4: text = &title_global<const char>(0x00837750); break;  // "Caricamento in corso"
    default: text = &title_global<const char>(0x00837774); break;  // "Loading ..."
    }
    NglQuad quad{};
    nglInitQuad(&quad);
    nglSetQuadRect(&quad, 0.0f, 0.0f, 640.0f, 480.0f);
    nglSetQuadColor(&quad, 0x96000000);
    nglSetQuadZ(&quad, 300.0f);
    nglSetQuadBlend(&quad, 2, 0);
    if (!title_global<uint8_t*>(0x0095f8d4)) return;  // ui_fonts[0]
    int32_t width, height;
    nglGetTextExtentf(title_global<uint8_t*>(0x0095f8d4), &width, &height, text);
    const int32_t x = unsigned_half_ftol(static_cast<uint32_t>(0x280 - width));
    for (int32_t i = 2; i != 0; --i) {
        nglSetClearFlags(3);
        nglListAddQuad(&quad);
        nglListAddTextf(title_global<uint8_t*>(0x0095f8d4), platform_x87_unsigned_to_float(static_cast<uint32_t>(x)),
                        220.0f, 0.0f, 0xe0ffffff, 1.0f, 1.0f, text);
    }
}

uint8_t* XB_CDECL CL_RefCM_GetEntry(uint32_t index) {
    uint8_t* const cm = title_global<uint8_t*>(0x0095f6b8);
    if (index >= field<uint32_t>(cm, 0x10)) index = 0;
    return field<uint8_t*>(cm, 0x14) + index * 16;
}

uint32_t XB_CDECL CL_CGameCall8(int16_t value) { return vm_call2(8, static_cast<uint32_t>(int32_t{value})); }

void XB_CDECL CL_ClearConfigstrings() {
    for (uint32_t slot = 0x009606b8; slot < 0x009616b8; slot += 4)  // cl.configstrings
        title_function<void XB_THISCALL(ScriptString*, const char*)>(0x0011dd20)(
            &title_global<ScriptString>(slot), &title_global<const char>(0x0082dfeb));  // ScriptString_AssignCString ""
    vm_call1(0x14);
}

int32_t XB_CDECL CL_ScaledMilliseconds() {
    return static_cast<int32_t>(static_cast<float>(sys_milliseconds()) * cvar_value(0x0093fce4));
}

uint8_t* XB_CDECL CL_RefGetFont() {
    const int32_t font = R_FindFont(&title_global<const char>(0x0082d248), 0);  // "arialblack_14"
    if (font == 7 || font == 8) return nullptr;
    return title_global<uint8_t*>(0x0095f8d4 + 4 * font);  // ui_fonts
}

uint32_t XB_REGS1 XB_CDECL CL_CharEventFiltered(uint32_t ch) {
    if (ch == '`' || ch == '~') return ch;
    if (!(title_global<uint8_t>(0x0092bdf4) & 1)) {  // cls.keyCatchers & KEYCATCH_CONSOLE
        const int32_t state = title_global<int32_t>(0x0092bdf0);
        if (state) return static_cast<uint32_t>(state);
    }
    CL_CharEvent(&title_global<uint8_t>(0x0092e830), static_cast<int32_t>(ch));
    return 0;
}

void XB_CDECL CL_RefreshConfigstrings(const ScriptString* strings) {
    if (title_global<uint8_t*>(0x0093f7f8)) vm_call2(0x16, title_global<uint32_t>(0x0092f094));
    for (uint32_t index = 0; index < 0x400; ++index) {
        auto* string = &title_global<ScriptString>(0x009606b8 + 4 * index);  // cl.configstrings
        const ScriptString* source = &strings[index];
        if (string->rep && ScriptString_EqualsString(string, source)) continue;
        title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(string, source);  // Assign
        if (title_global<uint8_t*>(0x0093f7f8)) vm_call2(0x15, index);
    }
    CL_SystemInfoChanged();
}

void XB_CDECL SCR_UpdateLoadingSubtitle(int32_t ms) {
    float& time = title_global<float>(0x0092708c);
    time = add_milliseconds(ms, time);
    if (!title_global<uint32_t>(0x0095f474)) return;  // no line shown
    if (time > title_global<float>(0x00926e24))       // its end
        Subtitle_Start(nullptr, &title_global<const char>(0x00926e30));
}

void XB_CDECL SCR_LoadingPump() {
    if (title_global<uint8_t>(0x0095f916)) return;
    if (!title_global<uint32_t>(0x0095f454)) return;  // no movie
    const uint32_t now = static_cast<uint32_t>(loading_clock());
    uint32_t& last = title_global<uint32_t>(0x0095f468);
    const float ms = static_cast<float>(static_cast<int32_t>(now - last)) * title_global<float>(0x0083ed64);
    float& movie_time = title_global<float>(0x0095f45c);
    movie_time = movie_time + ms;
    last = now;
    const int32_t step = mul_double_ftol(ms, title_global<double>(0x0083efe8));
    SCR_UpdateLoadingSubtitle(step);
    title_function<void XB_CDECL()>(0x000ca690)();  // File_PumpRequests
}

// The text field (Q3's field_t, extended): +0 cursor, +4 scroll, +8 characters shown, +0xc width (pixels),
// +0x10 / +0x14 the text size arguments (+0x14 / 48), +0x18 scaled from 640x480, +0x1c the text (256).
namespace {

int32_t field_text_width(const char* text, int32_t mode, float scale_x, float scale_y, int32_t count) {
    using Width = int32_t XB_CDECL(const char*, int32_t, float, float, int32_t);
    return title_function<Width>(title_global<uint32_t>(0x00928570))(text, mode, scale_x, scale_y, count);  // re.
}

}  // namespace

PLATFORM_ASM_ENTRY(Field_AdjustScroll_entry, "pushl %esi\n" PLATFORM_ASM_CALL(Field_AdjustScroll) "addl $4, %esp\n"
                                             "ret")

void XB_CDECL Field_AdjustScroll(uint8_t* f) {
    int32_t& cursor = field<int32_t>(f, 0);
    int32_t& scroll = field<int32_t>(f, 4);
    int32_t& shown = field<int32_t>(f, 8);
    char* const text = reinterpret_cast<char*>(f + 0x1c);
    float scale_x = field<float>(f, 0x14) * title_global<float>(0x0083ebbc);  // / 48
    float scale_y = field<float>(f, 0x10);
    float width = static_cast<float>(field<int32_t>(f, 0xc));
    int32_t mode = 0;
    if (field<int32_t>(f, 0x18)) {
        const float sx = title_global<float>(0x0083ec08) / static_cast<float>(title_global<int32_t>(0x0092bf40));
        const float sy = title_global<float>(0x0083ec0c) / static_cast<float>(title_global<int32_t>(0x0092bf3c));
        scale_x = sx * scale_x;
        scale_y = sy * scale_y;
        width = sy * width;
        mode = 5;
    }
    if (width > static_cast<float>(field_text_width(text, mode, scale_x, scale_y, 0))) {
        scroll = 0;
        shown = Loc_CountChars(0, text);
        return;
    }
    if (width > 0.0f) {
        while (scroll > 0) {
            const float before = static_cast<float>(field_text_width(text + scroll - 1, mode, scale_x, scale_y, 0));
            if (before >= width) break;
            scroll = scroll - 1;
            if (!(width > before)) break;
        }
    }
    for (;;) {
        const uint32_t from_scroll = static_cast<uint32_t>(field_text_width(text + scroll, mode, scale_x, scale_y, 0));
        int32_t diff = static_cast<int32_t>(
            from_scroll - static_cast<uint32_t>(field_text_width(text + cursor, mode, scale_x, scale_y, 0)));
        if (diff < 0) {
            if (scroll == 0) {
                diff = 0;
            } else {
                scroll = scroll - 1;
                continue;
            }
        } else if (static_cast<float>(diff) >= width) {
            scroll = scroll + 1;
        }
        if (static_cast<float>(diff) >= width) continue;
        break;
    }
    const int32_t rest = static_cast<int32_t>(std::strlen(text + scroll));
    shown = cursor - scroll;
    if (!(width > 0.0f)) return;
    while (shown < rest) {
        const float drawn = static_cast<float>(field_text_width(text + scroll, mode, scale_x, scale_y, shown + 1));
        if (width > drawn) ++shown;
        if (!(width > drawn)) break;
    }
}

PLATFORM_ASM_ENTRY(CL_CharEvent_entry, "pushl %ebx\n"
                                       "pushl %eax\n" PLATFORM_ASM_CALL(CL_CharEvent) "addl $8, %esp\n"
                                       "ret")

void XB_CDECL CL_CharEvent(uint8_t* f, int32_t ch) {
    int32_t& cursor = field<int32_t>(f, 0);
    char* const text = reinterpret_cast<char*>(f + 0x1c);
    int32_t length = static_cast<int32_t>(std::strlen(text));
    if (ch == 0x16) {  // ctrl-v: no clipboard
        Field_AdjustScroll(f);
        return;
    }
    if (ch == 3) {  // ctrl-c: clear
        std::memset(text, 0, 0x100);
        cursor = 0;
        field<int32_t>(f, 4) = 0;
        field<int32_t>(f, 8) = 0x100;
        Field_AdjustScroll(f);
        return;
    }
    if (ch == 8) {  // backspace
        if (cursor > 0) {
            std::memmove(text + cursor - 1, text + cursor, static_cast<size_t>(length - cursor + 1));
            cursor = cursor - 1;
        }
        Field_AdjustScroll(f);
        return;
    }
    if (ch == 1) {  // ctrl-a: home
        cursor = 0;
        field<int32_t>(f, 4) = 0;
        return;
    }
    if (ch == 5) {  // ctrl-e: end
        cursor = length;
        Field_AdjustScroll(f);
        return;
    }
    if (ch < 0x20) return;
    if (title_global<int32_t>(0x0092858c)) {  // key_overstrikeMode
        if (cursor == 0xff) return;
        text[cursor] = static_cast<char>(ch);
    } else {
        if (length == 0xff) return;
        std::memmove(text + cursor + 1, text + cursor, static_cast<size_t>(length - cursor + 1));
        text[cursor] = static_cast<char>(ch);
    }
    cursor = cursor + 1;
    ++length;
    if (cursor == length) text[cursor] = 0;
    Field_AdjustScroll(f);
}

void XB_CDECL Key_Bind_f() {
    const uint32_t argc = cmd_argc();
    if (static_cast<int32_t>(argc) < 2) return;
    const char* const name = argc > 1 ? cmd_argv(1) : &title_global<const char>(0x0082dfeb);
    int32_t key = Key_StringToKeynum(name);
    if (key == -1) return;
    key = title_function<int32_t XB_CDECL(int32_t)>(0x001eed48)(key);  // tolower
    if (argc == 2) return;  // (the binding is printed in Q3)
    char command[0x400];
    command[0] = 0;
    for (uint32_t i = 2; i < argc; ++i) {
        std::strcat(command, i < cmd_argc() ? cmd_argv(i) : &title_global<const char>(0x0082dfeb));
        if (i != argc - 1) std::strcat(command, " ");
    }
    if (title_global<uint8_t>(0x008dce70) && std::strstr(command, &title_global<const char>(0x0082dfa8)))  // "+speed"
        title_function<int32_t XB_CDECL(char*, const char*, const char*)>(0x001ee901)(
            command, &title_global<const char>(0x0082ddd4), &title_global<const char>(0x0082dfb0));  // "toggle cl_run"
    if (key == -1) return;
    const char*& binding = title_global<const char*>(0x0093425c + 8 * static_cast<uint32_t>(key));
    if (binding) title_function<void XB_CDECL(const void*)>(0x000428d0)(binding);  // Z_Free
    binding = CopyString(command);
    title_global<uint32_t>(0x009403ec) |= 1;  // cvar_modifiedFlags
}

// The rumble state: per motor (left, right) a list of queued rumbles at +4 + 0x10 * motor (count, then the list
// head; the sentinel node at +0xc). A queued rumble: +0 next, +4 prev, +0xc time, +0x10 scale, +0x14 level,
// +0x18 attack, +0x1c sustain end, +0x20 release end, +0x24 duration (pattern) / looping (envelope), +0x28 the
// pattern ('a'..'z' = 0..1, ScriptString), +0x2c pattern flag.
namespace {

void rumble_unlink(uint8_t* node, uint8_t* list) {
    uint8_t* const next = field<uint8_t*>(node, 0);
    uint8_t* const prev = field<uint8_t*>(node, 4);
    field<uint8_t*>(next, 4) = prev;
    field<uint8_t*>(field<uint8_t*>(node, 4), 0) = field<uint8_t*>(node, 0);
    field<uint8_t*>(node, 0) = nullptr;
    field<uint8_t*>(node, 4) = nullptr;
    --field<int32_t>(list, -4);
    title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(node + 0x28);  // ScriptString_Release
    title_function<void XB_THISCALL(uint32_t, uint8_t*)>(0x00044910)(title_global<uint32_t>(0x0095f5c8),
                                                                     node);  // SmallHeap_Free
}

// (t / duration) * float(unsigned count) on the x87 (FLD, FDIV, FILD (+2^32), FMUL), stored as float.
float rumble_pattern_position(float t, float duration, uint32_t count) {
    const float converted = platform_x87_unsigned_to_float(count);
    float result;
    __asm__("flds %1\n\tfdivs %2\n\tfmuls %3\n\tfstps %0" : "=m"(result) : "m"(t), "m"(duration), "m"(converted)
            : "st");
    return result;
}

uint32_t pattern_length(const uint8_t* node) {
    const uint8_t* const rep = field<uint8_t*>(const_cast<uint8_t*>(node), 0x28);
    return rep ? field<uint16_t>(const_cast<uint8_t*>(rep), 2) : 0;
}

const char* pattern_text(const uint8_t* node) {
    uint8_t* const rep = field<uint8_t*>(const_cast<uint8_t*>(node), 0x28);
    return rep ? reinterpret_cast<const char*>(rep + 6) : nullptr;
}

float pattern_level(const char* text, int32_t index) {
    return static_cast<float>(static_cast<int32_t>(static_cast<signed char>(text[index])) - 0x61) /
           title_global<float>(0x0083ebc4);  // 25
}

}  // namespace

void XB_STDCALL CL_UpdateRumble(uint8_t* rumble, float dt) {
    for (int32_t motor = 0; motor < 2; ++motor) {
        uint8_t* const list = rumble + 8 + 0x10 * motor;
        float strongest = 0.0f;
        uint8_t* node = field<uint8_t*>(list, 0);
        uint8_t* after = node ? field<uint8_t*>(node, 0) : nullptr;
        if (node != list + 4 && after) {
            uint8_t* next = nullptr;
            for (;;) {
                if (after) {
                    next = after;
                    after = field<uint8_t*>(after, 0);
                }
                const float t = field<float>(node, 0xc) + dt;
                float level = 0.0f;
                field<float>(node, 0xc) = t;
                if (field<uint8_t>(node, 0x2c)) {
                    const float duration = field<float>(node, 0x24);
                    if (t >= duration) {
                        rumble_unlink(node, list);
                    } else if (t >= 0.0f) {
                        const float position = rumble_pattern_position(t, duration, pattern_length(node) - 1);
                        const int32_t index = static_cast<int32_t>(static_cast<int64_t>(position));
                        if (static_cast<uint32_t>(index) < pattern_length(node) - 2) {
                            const float fraction = position - static_cast<float>(index);
                            const float a = pattern_level(pattern_text(node), index);
                            const float b = pattern_level(pattern_text(node), index + 1);
                            level = (b - a) * fraction + a;
                        } else {
                            level = pattern_level(pattern_text(node), index);
                        }
                    }
                } else {
                    const float peak = field<float>(node, 0x14);
                    // +0x24 compared (UCOMISS) only after each time test; NaN counts as set.
                    if (t >= field<float>(node, 0x20) && field<float>(node, 0x24) != 0.0f) {
                        rumble_unlink(node, list);
                    } else if (t >= field<float>(node, 0x1c) && field<float>(node, 0x24) != 0.0f) {
                        const float span = field<float>(node, 0x20) - field<float>(node, 0x1c);
                        const float fade = (t - field<float>(node, 0x1c)) / span;
                        level = (0.0f - peak) * fade + peak;
                    } else if (t >= field<float>(node, 0x18)) {
                        level = peak;
                    } else if (t >= 0.0f) {
                        const float rise = t / field<float>(node, 0x18);
                        level = (peak - 0.0f) * rise + 0.0f;
                    }
                }
                const float scaled = field<float>(node, 0x10) * level;
                if (scaled > strongest) strongest = scaled;
                if (!after) break;
                node = next;
            }
        }
        if (cvar_integer(0x0093fcd8) || !title_global<uint8_t>(0x008dce72) || title_global<int32_t>(0x0092bdf0) == 5)
            strongest = 0.0f;
        const int32_t pad = title_global<int32_t>(0x008dcf18);
        if (pad != -1) {
            IN_PadCreate();
            IN_SetRumbleMotor(pad, motor, strongest);
        }
    }
}

PLATFORM_ASM_ENTRY(SCR_DrawSmallChar_entry, "pushl %ecx\n"
                                            "pushl 12(%esp)\n"
                                            "pushl 12(%esp)\n" PLATFORM_ASM_CALL(SCR_DrawSmallChar) "addl $12, %esp\n"
                                            "ret")

void XB_CDECL SCR_DrawSmallChar(int32_t x, int32_t y, int32_t ch) {
    const float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float scale_x = title_global<float>(0x0083ec0c) / static_cast<float>(title_global<int32_t>(0x0092bf3c));
    const float left = static_cast<float>(x) * scale_x;
    const float width = scale_x * title_global<float>(0x0083ed78);  // 8
    const float scale_y = title_global<float>(0x0083ec08) / static_cast<float>(title_global<int32_t>(0x0092bf40));
    const float top = (static_cast<float>(y) + title_global<float>(0x0083ebb8)) * scale_y;  // + 16
    const float height = scale_y * title_global<float>(0x0083ed74);                           // / 3
    const char* const text = title_function<const char* XB_CDECL(const char*, int32_t)>(0x002a93d0)(
        &title_global<const char>(0x0083401c), ch);  // va("%c")
    using DrawText = void XB_CDECL(float, float, int32_t, float, const float*, const char*, float, int32_t, int32_t);
    title_function<DrawText>(title_global<uint32_t>(0x00928578))(left, top, 5, height, color, text, width, 0, 0);
}

uint8_t* XB_CDECL CL_LoadFrontEndZone() {
    uint8_t* streams = title_global<uint8_t*>(0x0095f638);
    uint8_t* const map = field<uint8_t*>(streams, 0x18);
    uint8_t* zone = nullptr;
    if (map) {
        uint32_t key = 0x00833f7c;  // "FrontEnd"
        TitleRegs regs{};
        regs.ebx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(map + 8));
        const uint32_t args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key))};
        auto* const found = reinterpret_cast<uint32_t*>(uintptr_t{cg_call_regs(0x000249a0, &regs, args, 1)});  // StringMap_Find
        if (found) {
            uint32_t index = *found;
            if (index >= field<uint32_t>(map, 0x10)) index = 0;
            zone = field<uint8_t**>(map, 0x14)[index];
        }
        streams = title_global<uint8_t*>(0x0095f638);
    }
    title_global<uint8_t*>(0x0095f4f0) = zone;
    if (!zone) return zone;
    const int32_t slot = field<int32_t>(zone, 0x28);
    if (slot != -1) {
        uint8_t* const loaded = field<uint8_t*>(streams, 0x38 + 4 * slot);
        if (loaded && !field<uint32_t>(loaded, 0xdc)) return zone;
    }
    if (title_global<uint8_t>(0x0095ee01)) return zone;
    ++title_global<int32_t>(0x008d9d38);
    field<float>(zone, 0x34) = 0.0f;
    title_function<void XB_THISCALL(uint8_t*, uint8_t*)>(0x00292d70)(streams, zone);  // Stream_StartRequest
    return zone;
}

void XB_CDECL CL_StartHunkUsers() {
    const uint8_t* const running = title_global<uint8_t*>(0x00947704);  // com_cl_running
    if (!running || !field<int32_t>(const_cast<uint8_t*>(running), 0x20)) return;
    title_global<int32_t>(0x008db11c) = field<int32_t>(
        Cvar_Get(&title_global<const char>(0x00834458), &title_global<const char>(0x0082e098), 0x21), 0x20);  // cl_xmodelcheck
    if (!title_global<int32_t>(0x0092be80)) {  // cls.rendererStarted
        title_global<int32_t>(0x0092be80) = 1;
        CL_InitRenderer();
    }
    if (!title_global<int32_t>(0x0092be88)) {
        title_global<int32_t>(0x0092be88) = 1;
        CL_LoadFrontEndZone();
    }
}

void XB_CDECL SCR_InitLoadingSpinner() {
    static constexpr uint32_t kImages[4] = {0x008d5b38, 0x008d6bb8, 0x008d7c38, 0x008d8cb8};
    static constexpr uint32_t kNames[4] = {0x008377a4, 0x00837798, 0x0083778c, 0x00837780};  // spinner_a..d
    uint8_t* data[4];
    for (uint32_t i = 0; i < 4; ++i) {
        data[i] = static_cast<uint8_t*>(Mem_AlignedAlloc(0x1080, 0x80, 0x30000));
        std::memcpy(data[i], &title_global<uint8_t>(kImages[i]), 0x1080);
    }
    NglTexture* textures[4];
    for (uint32_t i = 0; i < 4; ++i) {
        alignas(4) uint8_t name[0x20] = {};
        title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(
            name, &title_global<const char>(kNames[i]));  // HashStr_Init
        textures[i] = nglLoadTexture(name, 0, data[i], 0x1080);
        if (i == 0) {
            title_global<NglTexture*>(0x0095f5e4) = textures[0];
            title_global<NglTexture*>(0x0095f5e8) = textures[0];
            title_global<NglTexture*>(0x0095f5ec) = textures[0];
        } else {
            title_global<NglTexture*>(0x0095f5ec + 4 * i) = textures[i];
        }
    }
    title_global<NglTexture*>(0x0095f5fc) = textures[2];  // the 8 frames: a a a b c d c b
    title_global<NglTexture*>(0x0095f600) = textures[1];
}

namespace {

float title_atof(const char* text) { return title_function<float XB_CDECL(const char*)>(0x001ef056)(text); }

int32_t title_stricmp(const char* a, const char* b) {
    return title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(a, b);
}

const char* cube_face_path(const char* name, uint32_t face_slot) {
    return title_function<const char* XB_CDECL(const char*, const char*, const char*)>(0x002a93d0)(
        &title_global<const char>(0x00833fa8), name, title_global<const char*>(face_slot));  // va("env/%s%s.tga")
}

uint32_t bits_of(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

}  // namespace

void XB_CDECL CL_CubemapShot_f() {
    float fresnel[2] = {1.0f, title_global<float>(0x0083ed70)};  // 1.333
    if (!title_global<uint8_t*>(0x0093f7f8)) return;              // cgvm
    const uint32_t argc = cmd_argc();
    if (static_cast<int32_t>(argc) < 3) return;
    const char* const name_arg = argc > 2 ? cmd_argv(2) : &title_global<const char>(0x0082dfeb);
    if (std::strlen(name_arg) > 0x68) return;
    char name[0x80];
    std::strcpy(name, argc > 2 ? cmd_argv(2) : &title_global<const char>(0x0082dfeb));
    const int32_t size = title_atol(argc > 1 ? cmd_argv(1) : &title_global<const char>(0x0082dfeb));
    if (size < 4 || size > 0x400 || (size & (size - 1))) return;
    const uint32_t count = cmd_argc();
    if (count == 10) {  // water: two vectors, the six faces written by re function 0x00928564
        if (title_stricmp(Cmd_Argv(3), &title_global<const char>(0x00833fb8))) return;
        float first[3], second[3];
        first[0] = title_atof(Cmd_Argv(4));
        first[1] = title_atof(Cmd_Argv(5));
        first[2] = title_atof(Cmd_Argv(6));
        second[0] = title_atof(Cmd_Argv(7));
        second[1] = title_atof(Cmd_Argv(8));
        second[2] = title_atof(Cmd_Argv(9));
        int32_t face = 1;
        for (uint32_t slot = 0x008fef20; slot <= 0x008fef34; slot += 4, ++face) {
            using Write = void XB_CDECL(const char*, int32_t, int32_t, const float*, const float*);
            title_function<Write>(title_global<uint32_t>(0x00928564))(cube_face_path(name, slot), size, face, first,
                                                                      second);
        }
        return;
    }
    if (count == 6) {  // fresnel: both values at least 1
        if (title_stricmp(Cmd_Argv(3), &title_global<const char>(0x00833fa0))) return;
        fresnel[0] = title_atof(Cmd_Argv(4));
        fresnel[1] = title_atof(Cmd_Argv(5));
        if (platform_x87_greater(1.0f, fresnel[0])) return;
        if (1.0f > fresnel[1]) return;
    } else if (count != 3) {
        return;
    }
    int32_t& frame = title_global<int32_t>(0x0093fc28);
    if (++frame == 0) frame = 1;
    title_global<int32_t>(0x0095f4ec) = 1;
    int32_t face = 1;
    for (uint32_t slot = 0x008fef20; slot <= 0x008fef34; slot += 4, ++face) {
        title_function<void XB_CDECL(int32_t)>(title_global<uint32_t>(0x00928548))(0);  // re.BeginFrame
        title_function<uint32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, int32_t,
                                                  int32_t)>(0x002a84e0)(
            0, title_global<uint8_t*>(0x0093f7f8), 3, title_global<uint32_t>(0x009606b0), 0, 0, static_cast<uint32_t>(face),
            size);  // VM_Call(cgvm, CG_DRAW_ACTIVE_FRAME, serverTime, 0, 0, face, size)
        title_function<void XB_CDECL(uint32_t, uint32_t)>(title_global<uint32_t>(0x0092854c))(0, 0);  // re.EndFrame
        using Write = void XB_CDECL(const char*, int32_t, int32_t, uint32_t, uint32_t);
        title_function<Write>(title_global<uint32_t>(0x00928560))(cube_face_path(name, slot), size, face,
                                                                  bits_of(fresnel[0]), bits_of(fresnel[1]));
    }
    title_global<int32_t>(0x0095f4ec) = 0;
}

void XB_CDECL CL_InitRef() {
    // refimport_t (0x22 words). Words 0x12 and 0x17 are never set by the original (stale stack there).
    uint32_t imports[0x22] = {};
    imports[0x0b] = 0x002ba5c0;  // Cmd_AddCommand
    imports[0x0c] = 0x002b1dc0;  // Cmd_RemoveCommand
    imports[0x0d] = 0x002aaac0;  // Cmd_Argc
    imports[0x0e] = 0x002aaaa0;  // Cmd_Argv
    imports[0x0f] = 0x002c63a0;  // Cbuf_ExecuteText
    imports[0x00] = 0x002ab760;  // CL_RefPrintf
    imports[0x01] = 0x002938f0;  // __break (Com_Error)
    imports[0x02] = 0x002b3250;  // CL_ScaledMilliseconds
    imports[0x13] = 0x002a1fd0;  // FS_ReadFile
    imports[0x14] = 0x00297a30;  // FS_FreeFile
    imports[0x16] = 0x002979f0;  // FS_FreeFileList
    imports[0x15] = 0x002a1fb0;  // FS_ListFiles
    imports[0x18] = 0x0029d4a0;  // FS_FileExists
    imports[0x19] = 0x002a1b30;  // FS_FOpenFileByMode
    imports[0x1a] = 0x00297d70;  // FS_FCloseFile
    imports[0x1b] = 0x00297af0;  // FS_Read
    imports[0x1c] = 0x00297a60;  // FS_Write
    imports[0x1d] = 0x002aac90;  // CL_RefCM_GetEntry
    imports[0x08] = 0x0029c2d0;  // Cvar_Get
    imports[0x09] = 0x00299710;  // Cvar_FindVar
    imports[0x0a] = 0x0029e6e0;  // Cvar_Set
    imports[0x10] = 0x0029c4f0;  // Cvar_WriteNamedValues
    imports[0x11] = 0x0029e700;  // CG_ShellShock_ParseParams
    imports[0x1e] = 0x002ab710;  // CL_CGameCall8
    imports[0x1f] = 0x0027f8e0;  // CL_DObjCreateSkeleton
    imports[0x20] = 0x002ab470;  // SCR_AdjustFrom640
    imports[0x21] = 0x002bb7c0;  // CL_RefGetFont
    const uint32_t exports = GetRefAPI(imports, 0xe);
    if (!exports)
        title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(0, &title_global<const char>(0x00834438));
    std::memcpy(&title_global<uint8_t>(0x009284e8), reinterpret_cast<const void*>(uintptr_t{exports}), 0x28 * 4);  // re
    Cvar_Set2(&title_global<const char>(0x0082ca00), const_cast<char*>(&title_global<const char>(0x0082e098)),
              1);  // cl_paused 0
}

uint32_t XB_CDECL CL_InitCGame() {
    sys_milliseconds();
    const ScriptString& info = title_global<ScriptString>(0x009606b8);  // configstring 0 (serverinfo)
    const char* const text = info.rep ? reinterpret_cast<const char*>(info.rep) + 6 : nullptr;
    const char* const map = Info_ValueForKey(text, &title_global<const char>(0x00835230));  // "mapname"
    const uint32_t args[2] = {0x00835224, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(map))};  // "maps/%s.bsp"
    Com_sprintf(&title_global<char>(0x009616b8), 0x80, args);  // cl.mapname
    uint8_t* const vm = title_function<uint8_t* XB_CDECL(const char*, uint32_t)>(0x002f2b80)(
        &title_global<const char>(0x0083521c), 0x002ac200);  // VM_Create("cgame", CL_CgameSystemCalls)
    title_global<uint8_t*>(0x0093f7f8) = vm;
    if (!vm) title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, &title_global<const char>(0x00835200));
    title_function<uint32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, uint32_t)>(0x002a84e0)(0, vm, 0);  // VM_Call CG_INIT
    sys_milliseconds();
    title_function<void XB_CDECL()>(title_global<uint32_t>(0x00928510))();  // re.EndRegistration
    const uint8_t* const cvar = title_global<uint8_t*>(0x00934c68);
    if (cvar && !field<int32_t>(const_cast<uint8_t*>(cvar), 0x20)) return static_cast<uint32_t>(sys_milliseconds());
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cvar));
}

void XB_CDECL CL_Shutdown() {
    int32_t& recursive = title_global<int32_t>(0x0098ad30);
    if (recursive) {
        title_function<int32_t XB_CDECL(const char*)>(0x001efc4d)(&title_global<const char>(0x00834170));  // printf
        return;
    }
    recursive = 1;
    DebugDraw_Shutdown();
    const uint8_t* const running = title_global<uint8_t*>(0x00947704);  // com_cl_running
    if (running && field<int32_t>(const_cast<uint8_t*>(running), 0x20)) {
        Cvar_Set2(&title_global<const char>(0x00835238), const_cast<char*>(&title_global<const char>(0x0082e058)),
                  1);  // r_uiFullScreen 1
        CL_ClearState();
        title_global<int32_t>(0x0092bdf0) = 0;  // cls.state
    }
    CL_ShutdownCGame(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(running)));
    const uint32_t shutdown = title_global<uint32_t>(0x009284e8);  // re.Shutdown
    if (shutdown) {
        title_function<void XB_CDECL(int32_t)>(shutdown)(1);
        std::memset(&title_global<uint8_t>(0x009284e8), 0, 0x28 * 4);
    }
    CL_ApplyFrontEndReturnState();
    CL_ShutdownInput();
    static constexpr uint32_t kCommands[18] = {0x008341f0, 0x00834164, 0x00834158, 0x008341e4, 0x0083414c, 0x008341dc,
                                               0x0083413c, 0x00834128, 0x00834118, 0x008341cc, 0x00834114, 0x00834100,
                                               0x008341bc, 0x008340f0, 0x008341ac, 0x0083419c, 0x00834190, 0x008340e8};
    for (uint32_t name : kCommands) Cmd_RemoveCommand(&title_global<const char>(name));
    Cvar_Set2(&title_global<const char>(0x00834184), const_cast<char*>(&title_global<const char>(0x0082e098)),
              1);  // cl_running 0
    std::memset(&title_global<uint8_t>(0x0092bdf0), 0, 0x67 * 4);  // cls
    recursive = 0;
}

namespace {

constexpr uint32_t kFrontEnd = 0x0095f8fc;

// The front end opened over the paused game in `mode` (0x0032de50, front end in ESI: 0x0095f919 = 1, cl_paused 1,
// vtable +0xc (mode)), inlined or called.
void open_front_end(uint32_t mode, bool call) {
    uint8_t* const fe = title_global<uint8_t*>(kFrontEnd);
    if (call) {
        TitleRegs regs{};
        regs.esi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fe));
        const uint32_t args[1] = {mode};
        cg_call_regs(0x0032de50, &regs, args, 1);  // (its tail-called method pops the word)
        return;
    }
    title_global<uint8_t>(0x0095f919) = 1;
    Cvar_Set2(&title_global<const char>(0x0082ca00), const_cast<char*>(&title_global<const char>(0x0082e058)),
              1);  // cl_paused 1
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(field<uint32_t>(field<uint8_t*>(fe, 0), 0xc))(fe, mode);
}

void load_theatre_background() {
    TitleRegs regs{};
    regs.eax = 0x0095f8d0;  // g_uiRoot
    cg_call_regs(0x0031ff30, &regs, nullptr, 0);  // FE_LoadTheatreBackground
}

}  // namespace

void XB_CDECL CL_ApplyFrontEndReturnState() {
    title_global<uint32_t>(0x0092bdf4) &= ~2u;  // cls.keyCatchers
    if (title_global<uint8_t>(0x0095ee01)) {
        CL_ApplyProfileSettings(&title_global<uint8_t>(0x008dce30));
        title_function<void XB_CDECL()>(0x002c62b0)();  // Cbuf_Execute, its tail jump
        if (!title_global<int32_t>(0x008dce50)) SV_SetSkillHard();
        if (title_global<int32_t>(0x008dce50) == 1) SV_SetSkillMedium();
        if (title_global<int32_t>(0x008dce50) == 2) SV_SetSkillEasy();
        const int32_t chapter = title_global<int32_t>(0x008dce48);  // g_currentChapter
        if (chapter <= 0)
            open_front_end(5, false);
        else if (chapter <= 5)
            open_front_end(3, false);
        else
            open_front_end(chapter <= 8 ? 4 : 2, true);
        load_theatre_background();
    }
    if (!title_global<uint8_t>(0x0095f918)) return;
    uint8_t* const overlay = title_global<uint8_t*>(0x0095f8f8);
    title_global<uint8_t>(0x0095f918) = 0;
    if (overlay) title_function<void XB_THISCALL(uint8_t*, int32_t)>(field<uint32_t>(field<uint8_t*>(overlay, 0), 0))(overlay, 1);
    title_global<uint8_t*>(0x0095f8f8) = nullptr;
    if (title_global<uint8_t>(0x0095ee01)) return;
    const int32_t chapter = title_global<int32_t>(0x008dce48);
    if (chapter <= 0)
        open_front_end(5, false);
    else
        open_front_end(chapter <= 5 ? 3 : chapter <= 8 ? 4 : 2, true);
    load_theatre_background();
    uint8_t* const zone = title_global<uint8_t*>(0x0095f4f0);
    field<uint32_t>(zone, 0x34) = title_global<uint32_t>(0x0082eed0);  // FLT_MAX
    TitleRegs regs{};
    regs.eax = title_global<uint32_t>(0x0095f638);
    regs.ebx = field<uint32_t>(zone, 0x28);
    cg_call_regs(0x00292cb0, &regs, nullptr, 0);  // Stream_WaitForLoad
}

PLATFORM_ASM_ENTRY(CL_UpdatePauseOverlay_entry, "pushl 4(%esp)\n"
                                             "pushl %esi\n" PLATFORM_ASM_CALL(CL_UpdatePauseOverlay) "addl $8, %esp\n"
                                             "ret $4")

void XB_CDECL CL_UpdatePauseOverlay(uint8_t* menu, float dt) {
    using Method1 = void XB_THISCALL(uint8_t*, uint32_t);
    auto method = [](uint8_t* object, uint32_t slot) {
        return title_function<Method1>(field<uint32_t>(field<uint8_t*>(object, 0), slot));
    };
    if (field<uint8_t>(menu, 0x44) & 8) {
        uint8_t* const pause = title_global<uint8_t*>(0x0095f900);
        method(pause, 0xc)(pause, 0xffffffff);
        if (!(field<uint8_t>(pause, 0x20) & 1)) {
            TitleRegs regs{};
            regs.eax = title_global<uint32_t>(0x0095f4c4);
            cg_call_regs(0x002b6ba0, &regs, nullptr, 0);  // S_ResumeAllChannels
            Cvar_Set2(&title_global<const char>(0x0082ca00), const_cast<char*>(&title_global<const char>(0x0082e098)),
                      1);  // cl_paused 0
        }
        field<uint8_t>(pause, 0x20) &= 0xfb;
    }
    if (field<float>(menu, 0x40) == 0.0f && (field<uint8_t>(menu, 0x44) & 4)) {  // UCOMISS: NaN is not 0
        for (uint32_t i = 0; i < 4; ++i) {
            uint8_t* const child = field<uint8_t*>(menu, 4 * i);
            method(child, 0x34)(child, 1);
        }
    }
    if (0.0f > field<float>(menu, 0x40)) field<float>(menu, 0x40) = field<float>(menu, 0x40) + dt;
    const uint8_t flags = field<uint8_t>(menu, 0x44);
    bool show = false;
    if ((flags & 4) && (flags & 1) && 0.0f > field<float>(menu, 0x3c)) {
        const float value = dt / title_global<float>(0x0083eb94) * title_global<float>(0x0083ec9c) + field<float>(menu, 0x3c);
        field<float>(menu, 0x3c) = value;
        if (value > 0.0f) field<float>(menu, 0x3c) = 0.0f;
        show = true;
    } else if ((flags & 4) && (flags & 2)) {
        float value = field<float>(menu, 0x3c);
        if (value > field<float>(menu, 0x38)) {
            value = value - dt / title_global<float>(0x0083eb94) * title_global<float>(0x0083ec9c);
            field<float>(menu, 0x3c) = value;
            if (field<float>(menu, 0x38) > value) field<uint32_t>(menu, 0x3c) = field<uint32_t>(menu, 0x38);
            show = true;
        }
    }
    if (show) {
        const float position =
            field<float>(menu, 0x3c) / field<float>(menu, 0x38) * title_global<float>(0x0083ef98) + title_global<float>(0x0083ef94);
        uint8_t* const child = field<uint8_t*>(menu, 0xc);
        uint32_t bits;
        std::memcpy(&bits, &position, 4);
        using Method2 = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
        title_function<Method2>(field<uint32_t>(field<uint8_t*>(child, 0), 0x8c))(child, field<uint32_t>(menu, 0x34), bits);
    }
    const uint32_t color = title_global<uint8_t*>(0x0095f8f8) ? 0xff000000 : 0xffffffff;
    uint8_t* const first = field<uint8_t*>(menu, 0x28);
    method(first, 0x90)(first, color);
    uint8_t* const second = field<uint8_t*>(menu, 0x2c);
    method(second, 0x90)(second, color);
}

PLATFORM_ASM_ENTRY(CL_PlayMovie_entry, "pushl %esi\n" PLATFORM_ASM_CALL(CL_PlayMovie) "addl $4, %esp\n"
                                       "ret")

void XB_CDECL CL_PlayMovie(const char* name) {
    if (title_global<uint8_t>(0x0095f916)) return;
    title_global<uint8_t>(0x0095f46c) = 0;
    if (!Movie_Open(name)) return;
    if (!title_global<Movie*>(0x0095f454)) return;
    while (!Movie_IsReady(title_global<Movie*>(0x0095f454))) title_function<void XB_CDECL()>(0x000ca690)();  // File_PumpRequests
    title_global<uint32_t>(0x0095f468) = static_cast<uint32_t>(loading_clock());
    Subtitle_Start(name, &title_global<const char>(0x0082d7f8));  // "MOVIE"
    nglSetVBlankMode(2);
    bool done = false;
    title_global<float>(0x0095f45c) = 0.0f;
    // The view matrix: rows 0x0082e160/70/80 times lane 0 of 0x0082e140, row 3 = 0x0082e290 with z + 10 (all lanes
    // computed, as MULPS/ADDPS do).
    alignas(16) float view[16];
    const float scale = title_global<float>(0x0082e140);
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t lane = 0; lane < 4; ++lane)
            view[4 * row + lane] = title_global<float>(0x0082e160 + 0x10 * row + 4 * lane) * scale;
    const float z = title_global<float>(0x0082e298);
    float sums[4];
    for (uint32_t lane = 0; lane < 4; ++lane) sums[lane] = title_global<float>(0x0083eb98) + z;
    platform_keep(sums[1]);
    view[12] = title_global<float>(0x0082e290);
    view[13] = title_global<float>(0x0082e294);
    view[14] = sums[0];
    view[15] = title_global<float>(0x0082e29c);
    do {
        nglBeginFrame();
        nglSetPerspective(90.0f, 1.0f, 100.0f);
        nglSetViewMatrix(view);
        if (Movie_DrawFrame() == 3 || Movie_CheckSkipInput() == 1) done = true;
        Subtitle_Draw();
        nglEndFrame();
        SCR_LoadingPump();
    } while (!done);
    Movie_Stop(1);
}

namespace {

// S_SetHandleVolume (0x002bd140): handle in EAX, volume in XMM0.
void set_handle_volume(uint32_t handle, float volume) {
    __asm__ volatile("movss %1, %%xmm0\n\tcall *%2"
                     : "+a"(handle)
                     : "m"(volume), "r"(uintptr_t{0x002bd140})
                     : "ecx", "edx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "cc", "memory");
}

}  // namespace

// The original ends in a tail jump to Cbuf_Execute, which then runs at the entry ESP, where the
// commands it executes lie (cmd's current text, 0x00927f50, points there): the entry jumps there
// too. C callers call Cbuf_Execute after CL_ApplyProfileSettings themselves.
PLATFORM_ASM_ENTRY(CL_ApplyProfileSettings_entry, "pushl %edi\n" PLATFORM_ASM_CALL(CL_ApplyProfileSettings) "addl $4, %esp\n"
                                                  "pushl $0x002c62b0\n"  // Cbuf_Execute
                                                  "ret")

void XB_CDECL CL_ApplyProfileSettings(uint8_t* profile) {
    static constexpr int32_t kAttenuation[4] = {1, 2, 4, 3};
    const uint32_t mode = title_global<uint32_t>(0x008dce84);
    if (mode <= 3) S_SetMasterAttenuation(kAttenuation[mode]);
    const float fifty = title_global<float>(0x0083ec30);
    set_handle_volume(title_global<uint32_t>(0x0095f4cc), static_cast<float>(field<int32_t>(profile, 0x58)) / fifty);
    const float volume = static_cast<float>(field<int32_t>(profile, 0x58)) / fifty;
    uint8_t* const sound = title_global<uint8_t*>(0x0095f4c4);
    float& level = field<float>(sound, 0x6018);
    level = volume;
    if (0.0f >= volume) level = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    if (level >= one) level = one;
    const float rest = one - level;
    title_function<void XB_CDECL(float)>(0x000c6490)(one - rest * rest);  // S_SetMasterVolume
    const uint8_t* const pitch = title_global<uint8_t*>(0x00929c0c);  // m_pitch
    if (pitch) {
        const float value = field<float>(const_cast<uint8_t*>(pitch), 0x1c);
        const bool flip = title_global<uint8_t>(0x008dce71) ? value > 0.0f : 0.0f > value;  // inverted look
        if (flip) Cvar_SetValue(&title_global<const char>(0x0082e07c), field<float>(const_cast<uint8_t*>(pitch), 0x1c) * -1.0f);
    }
    uint8_t* const sensitivity_x = Cvar_Get(&title_global<const char>(0x0082e06c), &title_global<const char>(0x0082e098), 1);
    uint8_t* const sensitivity_y = Cvar_Get(&title_global<const char>(0x0082e05c), &title_global<const char>(0x0082e098), 1);
    const float two = title_global<float>(0x0083eba8);
    field<float>(sensitivity_x, 0x1c) = static_cast<float>(field<int32_t>(profile, 0x44)) / fifty * two;
    field<float>(sensitivity_y, 0x1c) = static_cast<float>(field<int32_t>(profile, 0x48)) / fifty * two;
    const int32_t pad = title_global<int32_t>(0x008dcf18);
    title_global<int32_t>(0x008d50d8) = pad;
    if (pad != -1) field<int32_t>(title_global<uint8_t*>(0x0095f6fc), 0x44d0) = pad;  // g_liveMgr
    Cvar_Set2(&title_global<const char>(0x0082e044),
              const_cast<char*>(&title_global<const char>(field<int32_t>(profile, 0x3c) == 1 ? 0x0082e058 : 0x0082e098)),
              1);  // g_vehControlMode
    switch (title_global<int32_t>(0x008dce7c)) {  // controller style
    case 1: Cbuf_AddText(&title_global<const char>(0x0082e030)); break;  // exec style_e.cfg
    case 2: Cbuf_AddText(&title_global<const char>(0x0082e01c)); break;  // exec style_f.cfg
    case 3: Cbuf_AddText(&title_global<const char>(0x0082e008)); break;  // exec style_g.cfg
    case 4: break;
    default: Cbuf_AddText(&title_global<const char>(0x0082dff4)); break;  // exec style_h.cfg (inlined)
    }
    // Cbuf_Execute follows: a tail jump in the original (CL_ApplyProfileSettings_entry).
}

namespace {

// The localized string `key` (rm_locStrings), "NO STRING" when its entry has none, the key when there is no entry.
const char* localized(const char* key) {
    const uint32_t hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x00024440)(key);  // Loc_HashKey
    const uint8_t* const entry = Zone_FindByHash(title_global<uint8_t*>(0x0095f5d8), hash);
    if (!entry) return key;
    const char* const text = field<const char*>(const_cast<uint8_t*>(entry), 8);
    return text ? text : &title_global<const char>(0x008379b4);
}

// float(value * 0.5) on the x87 (FILD, FSTP QWORD, FLD, FMUL QWORD 0.5, FSTP DWORD).
float int_half_float(int32_t value) {
    double converted;
    __asm__("fildl %1\n\tfstpl %0" : "=m"(converted) : "m"(value) : "st");
    float result;
    __asm__("fldl %1\n\tfmull %2\n\tfstps %0" : "=m"(result) : "m"(converted), "m"(title_global<double>(0x0083ecb0))
            : "st");
    return result;
}

// time + float(float(ticks) / 733333.3) * 0.001 on the x87, each step stored as float as the original does.
float add_tick_seconds(int32_t ticks, float time) {
    float converted, divided, result;
    __asm__("fildl %1\n\tfstps %0" : "=m"(converted) : "m"(ticks) : "st");
    __asm__("flds %1\n\tfdivs %2\n\tfstps %0" : "=m"(divided) : "m"(converted), "m"(title_global<float>(0x0083ed30))
            : "st");
    __asm__("flds %1\n\tfmull %2\n\tfadds %3\n\tfstps %0"
            : "=m"(result)
            : "m"(divided), "m"(title_global<double>(0x0083ef88)), "m"(time)
            : "st");
    return result;
}

// time * 0.5 (FLD, FMUL QWORD) / 1.0 - (time - 3) * 0.5 (FSUB, FMUL QWORD, FSUBR QWORD), stored as float.
float fade_in(float time) {
    float result;
    __asm__("flds %1\n\tfmull %2\n\tfstps %0" : "=m"(result) : "m"(time), "m"(title_global<double>(0x0083ecb0)) : "st");
    return result;
}

float fade_out(float time) {
    float result;
    __asm__("flds %1\n\tfsubs %2\n\tfmull %3\n\tfsubrl %4\n\tfstps %0"
            : "=m"(result)
            : "m"(time), "m"(title_global<float>(0x0083ec9c)), "m"(title_global<double>(0x0083ecb0)),
              "m"(title_global<double>(0x0083eb10))
            : "st");
    return result;
}

}  // namespace

void XB_CDECL SCR_DrawLoadingScreen() {
    NglQuad quad{};
    nglInitQuad(&quad);
    nglSetQuadRect(&quad, 0.0f, 0.0f, 640.0f, 480.0f);
    nglSetQuadColor(&quad, 0xff000000);
    nglSetQuadZ(&quad, 300.0f);
    const char* const text = localized(&title_global<const char>(0x0082d018));  // "INGAME_TWO_YEARS"
    uint8_t* object = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(0x60);  // Z_Malloc
    if (object) object = UIText_Init(object, 3, text, 0, 0, 0, 7, 0x3fd9999a, 0x10, 0, 0xffd2cfb2);  // 1.7, colour
    auto method = [object](uint32_t slot) { return field<uint32_t>(field<uint8_t*>(object, 0), slot); };
    const long double width = title_function<long double XB_THISCALL(uint8_t*)>(method(0xd0))(object);
    const int32_t left = 0x280 - static_cast<int32_t>(static_cast<int64_t>(width));
    title_function<void XB_THISCALL(uint8_t*, float, float)>(method(0x7c))(object, int_half_float(left), 230.0f);
    using SetAlpha = void XB_THISCALL(uint8_t*, uint32_t);
    title_function<SetAlpha>(method(0x88))(object, 0);
    uint32_t last = static_cast<uint32_t>(loading_clock());
    float time = 0.0f;
    float alpha = 0.0f;
    for (;;) {
        uint32_t alpha_bits;
        std::memcpy(&alpha_bits, &alpha, 4);
        title_function<SetAlpha>(method(0x88))(object, alpha_bits);
        nglBeginFrame();
        nglPushScene(0);
        nglSetClearFlags(0xf3);
        nglSetSceneDepthEnable(0);
        nglListAddQuad(&quad);
        nglPopScene();
        nglPushScene(0);
        nglSetClearFlags(0);
        title_function<void XB_THISCALL(uint8_t*)>(method(0x3c))(object);  // draw
        nglPopScene();
        nglEndFrame();
        title_function<void XB_CDECL()>(0x000ca690)();  // File_PumpRequests
        const uint32_t now = static_cast<uint32_t>(loading_clock());
        time = add_tick_seconds(static_cast<int32_t>(now - last), time);
        last = now;
        if (platform_x87_greater(title_global<float>(0x0083eba8), time)) {  // 2 s fade in
            alpha = fade_in(time);
        } else if (title_global<float>(0x0083ec9c) > time) {  // shown until 3 s
            alpha = title_global<float>(0x0083eb20);
        } else if (title_global<float>(0x0083ece8) > time) {  // fade out until 5 s
            alpha = fade_out(time);
        } else {
            return;
        }
    }
}

void XB_REGS1 XB_CDECL SCR_DrawLaunchLoadingScreen(uint32_t alpha) {
    const char* const text = localized(&title_global<const char>(0x0082d06c));  // "INGAME_LOADING"
    int32_t width, height;
    nglGetTextExtentf(title_global<uint8_t*>(0x0095f8e0), &width, &height, text);
    const int32_t x = unsigned_half_ftol(static_cast<uint32_t>(0x280 - width));
    NglQuad quad{};
    nglInitQuad(&quad);
    nglSetQuadRect(&quad, 0.0f, 0.0f, 640.0f, 480.0f);
    nglSetQuadColor(&quad, alpha << 24);
    nglSetQuadZ(&quad, 300.0f);
    nglSetQuadBlend(&quad, 2, 0);
    for (int32_t i = 2; i != 0; --i) {
        nglBeginFrame();
        nglSetClearFlags(3);
        nglListAddQuad(&quad);
        nglListAddTextf(title_global<uint8_t*>(0x0095f8d4), platform_x87_unsigned_to_float(static_cast<uint32_t>(x)),
                        220.0f, 0.0f, 0xe0ffffff, 1.0f, 1.0f, text);
        nglEndFrame();
    }
}

// Q3's CL_Init: cvars (the generated sequence follows the original's calls), commands, the renderer imports.
void XB_CDECL CL_Init() {
    title_global<int32_t>(0x0092bdf0) = 0;  // cls.state
    title_global<int32_t>(0x0092be98) = 0;
    CL_InitInput();
    title_global<uint8_t*>(0x00934c68) = Cvar_Get(&title_global<const char>(0x00834410), &title_global<const char>(0x0082e098), 0x0);
    title_global<uint8_t*>(0x0095f4e4) = Cvar_Get(&title_global<const char>(0x00834404), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x00927de4) = Cvar_Get(&title_global<const char>(0x008343f8), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x009284e0) = Cvar_Get(&title_global<const char>(0x008343ec), &title_global<const char>(0x0082e098), 0x0);
    title_global<uint8_t*>(0x00929bf4) = Cvar_Get(&title_global<const char>(0x008343dc), &title_global<const char>(0x0082e098), 0x0);
    title_global<uint8_t*>(0x00929b1c) = Cvar_Get(&title_global<const char>(0x008343cc), &title_global<const char>(0x008343d8), 0x1);
    title_global<uint8_t*>(0x00934a60) = Cvar_Get(&title_global<const char>(0x008343bc), &title_global<const char>(0x008343d8), 0x1);
    title_global<uint8_t*>(0x0093e6b4) = Cvar_Get(&title_global<const char>(0x008343a4), &title_global<const char>(0x008343b8), 0x0);
    title_global<uint8_t*>(0x00929c04) = Cvar_Get(&title_global<const char>(0x00834394), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x00934c70) = Cvar_Get(&title_global<const char>(0x0082c538), &title_global<const char>(0x0082e058), 0x100);
    title_global<uint8_t*>(0x00927b20) = Cvar_Get(&title_global<const char>(0x008351c0), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x009284d8) = Cvar_Get(&title_global<const char>(0x008351cc), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x00927ddc) = Cvar_Get(&title_global<const char>(0x00834380), &title_global<const char>(0x0082e098), 0x1);
    title_global<uint8_t*>(0x00929b58) = Cvar_Get(&title_global<const char>(0x00834370), &title_global<const char>(0x0082e058), 0x1);
    title_global<uint8_t*>(0x00933e40) = Cvar_Get(&title_global<const char>(0x00834360), &title_global<const char>(0x0082e058), 0x1);
    // Sensitivities from the profile defaults (0x008dce74/78 / 50 * 2).
    const float fifty = title_global<float>(0x0083ec30);
    const float two = title_global<float>(0x0083eba8);
    field<float>(title_global<uint8_t*>(0x00929b58), 0x1c) = static_cast<float>(title_global<int32_t>(0x008dce74)) / fifty * two;
    field<float>(title_global<uint8_t*>(0x00933e40), 0x1c) = static_cast<float>(title_global<int32_t>(0x008dce78)) / fifty * two;
    title_global<uint8_t*>(0x0092f5e8) = Cvar_Get(&title_global<const char>(0x00834350), &title_global<const char>(0x0082e098), 0x1);
    title_global<uint8_t*>(0x0093f808) = Cvar_Get(&title_global<const char>(0x00834344), &title_global<const char>(0x0082e058), 0x1);
    title_global<uint8_t*>(0x00929cd0) = Cvar_Get(&title_global<const char>(0x0083432c), &title_global<const char>(0x0082e058), 0x1);
    title_global<uint8_t*>(0x00927de8) = Cvar_Get(&title_global<const char>(0x008351a8), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x0093efe8) = Cvar_Get(&title_global<const char>(0x00835190), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x0092e824) = Cvar_Get(&title_global<const char>(0x0082e044), &title_global<const char>(0x0082e058), 0x1);
    title_global<uint8_t*>(0x00929c2c) = Cvar_Get(&title_global<const char>(0x00834318), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x00929c28) = Cvar_Get(&title_global<const char>(0x00834300), &title_global<const char>(0x0082e098), 0x100);
    title_global<uint8_t*>(0x0092eff8) = Cvar_Get(&title_global<const char>(0x008342ec), &title_global<const char>(0x0082e098), 0x0);
    title_global<uint8_t*>(0x00934a5c) = Cvar_Get(&title_global<const char>(0x008342e0), &title_global<const char>(0x0082e098), 0x0);
    title_global<uint8_t*>(0x00927b24) = Cvar_Get(&title_global<const char>(0x008342d0), &title_global<const char>(0x0082e098), 0x0);
    title_global<uint8_t*>(0x0092f54c) = Cvar_Get(&title_global<const char>(0x00835158), &title_global<const char>(0x0082e098), 0x40);
    title_global<uint8_t*>(0x0093ef60) = Cvar_Get(&title_global<const char>(0x00835140), &title_global<const char>(0x0082e098), 0x40);
    title_global<uint8_t*>(0x00929c0c) = Cvar_Get(&title_global<const char>(0x0082e07c), &title_global<const char>(0x008342cc), 0x1);
    title_global<uint8_t*>(0x00929bf8) = Cvar_Get(&title_global<const char>(0x008342c4), &title_global<const char>(0x008342cc), 0x1);
    // The look direction from m_pitch's sign: m_pitch = sign * 0.7, then 0x008342c4 = 0.7.
    const float pitch = field<float>(title_global<uint8_t*>(0x00929c0c), 0x1c);
    int32_t sign = 0;
    if (pitch > 0.0f)
        sign = 1;
    else if (0.0f > field<float>(title_global<uint8_t*>(0x00929c0c), 0x1c))
        sign = -1;
    Cvar_SetValue(&title_global<const char>(0x0082e07c), static_cast<float>(sign) * title_global<float>(0x0083ed28));
    Cvar_SetValue(&title_global<const char>(0x008342c4), 0.7f);
    title_global<uint8_t*>(0x0092f13c) = Cvar_Get(&title_global<const char>(0x008342b0), &title_global<const char>(0x008342bc), 0x1);
    title_global<uint8_t*>(0x00934c6c) = Cvar_Get(&title_global<const char>(0x008342a8), &title_global<const char>(0x008342bc), 0x1);
    title_global<uint8_t*>(0x00935110) = Cvar_Get(&title_global<const char>(0x0083429c), &title_global<const char>(0x0082e098), 0x1);
    Cvar_Get(&title_global<const char>(0x0083428c), &title_global<const char>(0x00834298), 0x1);
    title_global<uint8_t*>(0x00927e80) = Cvar_Get(&title_global<const char>(0x00834280), &title_global<const char>(0x0082e058), 0x200);
    title_global<uint8_t*>(0x00934250) = Cvar_Get(&title_global<const char>(0x00834278), &title_global<const char>(0x0082e058), 0x200);
    title_global<uint8_t*>(0x009355d0) = Cvar_Get(&title_global<const char>(0x00834270), &title_global<const char>(0x0082e058), 0x0);
    title_global<uint8_t*>(0x00935120) = Cvar_Get(&title_global<const char>(0x00834260), &title_global<const char>(0x0082e058), 0x1);
    title_global<uint8_t*>(0x00933e48) = Cvar_Get(&title_global<const char>(0x00834254), &title_global<const char>(0x0082e098), 0x1);
    title_global<uint8_t*>(0x0092e820) = Cvar_Get(&title_global<const char>(0x00834248), &title_global<const char>(0x0082e098), 0x200);
    title_global<uint8_t*>(0x00934a64) = Cvar_Get(&title_global<const char>(0x0083423c), &title_global<const char>(0x0082e098), 0x200);
    title_global<uint8_t*>(0x0092e9e0) = Cvar_Get(&title_global<const char>(0x0083422c), &title_global<const char>(0x0082e098), 0x200);
    title_global<uint8_t*>(0x0092f098) = Cvar_Get(&title_global<const char>(0x00834220), &title_global<const char>(0x0082e098), 0x200);
    title_global<uint8_t*>(0x00929b50) = Cvar_Get(&title_global<const char>(0x0083420c), &title_global<const char>(0x0082e098), 0x200);
    Cvar_Get(&title_global<const char>(0x008341f4), &title_global<const char>(0x008341fc), 0x1);
    Cmd_AddCommand(&title_global<const char>(0x008341f0), 0x002b32b0);
    Cmd_AddServerCommand(&title_global<const char>(0x008341e4), 0x002ab7b0);
    Cmd_AddCommand(&title_global<const char>(0x008341dc), 0x002ab7c0);
    Cmd_AddCommand(&title_global<const char>(0x008341cc), 0x002f1e60);
    Cmd_AddCommand(&title_global<const char>(0x008341bc), 0x002b31f0);
    Cmd_AddCommand(&title_global<const char>(0x008341ac), 0x002ccfb0);
    Cmd_AddCommand(&title_global<const char>(0x0083419c), 0x002ccf40);
    title_global<uint8_t*>(0x00929b0c) = Cvar_Get(&title_global<const char>(0x0083441c), &title_global<const char>(0x0082e098), 0x0);
    Cmd_AddCommand(&title_global<const char>(0x00834190), 0x002b2880);
    CL_InitRef();
    title_global<int32_t>(0x00933ee4) = 1;  // UI started
    title_function<void XB_CDECL()>(0x002c62b0)();  // Cbuf_Execute
    Cvar_Set2(&title_global<const char>(0x00834184), const_cast<char*>(&title_global<const char>(0x0082e058)),
              1);  // cl_running 1
}

PLATFORM_ASM_ENTRY(ClientActive_Ctor_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(ClientActive_Ctor) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL ClientActive_Ctor(uint8_t* cl) {
    // Script string fields cleared and the 0x400 configstrings (+0x558, 0x009606b8 in the
    // global) constructed empty (ScriptString_FromRep with no rep).
    auto word = [cl](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(cl + offset); };
    word(0x80) = 0;
    word(0x9c) = 0;
    word(0xb8) = 0;
    word(0x488) = 0;
    word(0x4e0) = 0;
    word(0x4e4) = 0;
    word(0x524) = 0;
    ScriptString* strings = reinterpret_cast<ScriptString*>(cl + 0x558);
    for (int i = 0; i < 0x400; ++i) ScriptString_FromRep(strings + i, nullptr);
    word(0x21f0) = 0;
    word(0x220c) = 0;
    word(0x2228) = 0;
    word(0x25f8) = 0;
    word(0x2650) = 0;
    word(0x2654) = 0;
    word(0x2694) = 0;
    return cl;
}


}  // namespace game
