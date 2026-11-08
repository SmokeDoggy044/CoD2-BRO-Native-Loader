// Xbox input replacement. See xinput.h.

#include "xbox_input.h"

#include <atomic>
#include <cstring>

#include "compiler.h"
#include "devices.h"
#include "dispatcher.h"
#include "log.h"
#include "objects.h"
#include "script.h"

namespace input {

namespace {

using xbox::from_xbox_ptr;
using xbox::XbPtr;

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorIoPending = 997;
constexpr uint32_t kErrorDeviceNotConnected = 1167;
constexpr uint32_t kHandleBase = 0x58494E00;  // 'XIN' + port: opaque handles

InputConfig g_config;
std::atomic<uint32_t> g_packet{0};
std::atomic<uint32_t> g_reported_ports{0};  // connected ports as last told to the title

bool is_gamepad(XbPtr type) { return type != 0 && type == g_config.gamepad_type; }

// XINPUT_GAMEPAD
struct XbGamepad {
    uint16_t buttons;
    uint8_t analog_buttons[8];
    int16_t thumb_lx, thumb_ly, thumb_rx, thumb_ry;
};
static_assert(sizeof(XbGamepad) == 18);

#pragma pack(push, 1)
struct XbInputState {  // XINPUT_STATE
    uint32_t packet_number;
    XbGamepad gamepad;
};
#pragma pack(pop)
static_assert(sizeof(XbInputState) == 22);

#pragma pack(push, 1)
struct XbFeedback {  // XINPUT_FEEDBACK
    uint32_t status;
    XbPtr event;
    uint8_t reserved[54];
    uint16_t left_motor_speed, right_motor_speed;  // XINPUT_RUMBLE
};
#pragma pack(pop)
static_assert(sizeof(XbFeedback) == 66);

uint32_t handle_port(XbPtr handle) { return handle - kHandleBase; }

// --- Devices ----------------------------------------------------------------------------

void XB_STDCALL XInitDevices(uint32_t /*prealloc_count*/, XbPtr /*prealloc_types*/) { init_devices(); }

// Bitmask of connected ports; resets the changes XGetDeviceChanges reports.
uint32_t XB_STDCALL XGetDevices(XbPtr type) {
    if (!is_gamepad(type)) return 0;
    const uint32_t ports = connected_ports();
    g_reported_ports = ports;
    return ports;
}

// Ports plugged in / unplugged since the last XGetDevices/XGetDeviceChanges. Returns
// whether anything changed.
uint32_t XB_STDCALL XGetDeviceChanges(XbPtr type, XbPtr insertions, XbPtr removals) {
    uint32_t inserted = 0, removed = 0;
    if (is_gamepad(type)) {
        const uint32_t ports = connected_ports();
        const uint32_t before = g_reported_ports.exchange(ports);
        inserted = ports & ~before;
        removed = before & ~ports;
    }
    if (insertions) *from_xbox_ptr<uint32_t>(insertions) = inserted;
    if (removals) *from_xbox_ptr<uint32_t>(removals) = removed;
    return (inserted | removed) != 0;
}

// Like XGetDeviceChanges without consuming the changes.
uint32_t XB_STDCALL XPeekDevices(XbPtr type, XbPtr insertions, XbPtr removals) {
    uint32_t inserted = 0, removed = 0;
    if (is_gamepad(type)) {
        const uint32_t ports = connected_ports();
        inserted = ports & ~g_reported_ports;
        removed = g_reported_ports & ~ports;
    }
    if (insertions) *from_xbox_ptr<uint32_t>(insertions) = inserted;
    if (removals) *from_xbox_ptr<uint32_t>(removals) = removed;
    return (inserted | removed) != 0;
}

// --- Gamepads ---------------------------------------------------------------------------

XbPtr XB_STDCALL XInputOpen(XbPtr type, uint32_t port, uint32_t /*slot*/, XbPtr /*polling*/) {
    if (!is_gamepad(type) || port >= kPortCount || !(connected_ports() & (1u << port))) return 0;
    return kHandleBase + port;
}

void XB_STDCALL XInputClose(XbPtr /*handle*/) {}

// XINPUT_CAPABILITIES: subtype, then the controls the device has (all of them).
uint32_t XB_STDCALL XInputGetCapabilities(XbPtr handle, XbPtr capabilities) {
    if (!(connected_ports() & (1u << handle_port(handle)))) return kErrorDeviceNotConnected;
    auto* bytes = from_xbox_ptr<uint8_t>(capabilities);
    bytes[0] = 1;  // XINPUT_DEVSUBTYPE_GC_GAMEPAD
    return kErrorSuccess;
}

uint32_t XB_STDCALL XInputGetState(XbPtr handle, XbPtr state) {
    PadState pad;
    const uint32_t port = handle_port(handle);
    if (!read_port(port, pad)) return kErrorDeviceNotConnected;
    if (port == 0) apply_script(pad);
    auto* s = from_xbox_ptr<XbInputState>(state);
    s->packet_number = ++g_packet;
    s->gamepad.buttons = pad.buttons;
    std::memcpy(s->gamepad.analog_buttons, pad.analog_buttons, sizeof(pad.analog_buttons));
    s->gamepad.thumb_lx = pad.thumb_lx;
    s->gamepad.thumb_ly = pad.thumb_ly;
    s->gamepad.thumb_rx = pad.thumb_rx;
    s->gamepad.thumb_ry = pad.thumb_ry;
    return kErrorSuccess;
}

// Rumble: applied and completed at once.
uint32_t XB_STDCALL XInputSetState(XbPtr handle, XbPtr feedback) {
    auto* f = from_xbox_ptr<XbFeedback>(feedback);
    set_rumble(handle_port(handle), f->left_motor_speed, f->right_motor_speed);
    f->status = kErrorSuccess;
    if (f->event) xbox::set_event_handle(f->event);
    return kErrorIoPending;
}

// --- Memory units: none ---------------------------------------------------------------

uint32_t XB_STDCALL XMountMUA(uint32_t /*port*/, uint32_t /*slot*/, XbPtr /*drive*/) { return kErrorDeviceNotConnected; }
uint32_t XB_STDCALL XUnmountMU(uint32_t /*port*/, uint32_t /*slot*/) { return kErrorDeviceNotConnected; }

#define XPP(name) {#name, reinterpret_cast<void*>(&name)}

const xbox::KernelImpl kFunctions[] = {
    XPP(XGetDeviceChanges),
    XPP(XGetDevices),
    XPP(XInitDevices),
    XPP(XInputClose),
    XPP(XInputGetCapabilities),
    XPP(XInputGetState),
    XPP(XInputOpen),
    XPP(XInputSetState),
    XPP(XMountMUA),
    XPP(XPeekDevices),
    XPP(XUnmountMU),
};

}  // namespace

void configure(const InputConfig& config) { g_config = config; }

std::span<const xbox::KernelImpl> xpp_functions() { return kFunctions; }

}  // namespace input
