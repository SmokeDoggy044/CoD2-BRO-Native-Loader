#include "cl.h"

#include <cstring>

#include "qcommon.h"
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

}  // namespace game
