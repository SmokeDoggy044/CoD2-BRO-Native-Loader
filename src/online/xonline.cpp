// Xbox Live replacement. See xonline.h.

#include "xonline.h"

#include "compiler.h"
#include "log.h"
#include "types.h"

namespace online {

namespace {

constexpr uint32_t kXOnlineErrorNotReady = 0x80151000;  // XONLINE_E_... (no network)
constexpr uint32_t kSilentLogonNoAccounts = 0x80151081;  // XONLINE_E_SILENTLOGON_NO_ACCOUNTS

uint32_t XB_STDCALL XOnlineStartup(xbox::XbPtr /*parameters*/) {
    LOG_INFO("online", "XOnlineStartup: no network (Xbox Live is not available)");
    return kXOnlineErrorNotReady;
}

uint32_t XB_STDCALL XOnlineCleanup() { return 0; }

// Accounts stored on the console: none.
uint32_t XB_STDCALL XOnlineGetUsers(xbox::XbPtr /*users*/, xbox::XbPtr count) {
    *xbox::from_xbox_ptr<uint32_t>(count) = 0;
    return 0;
}

// The users logged on to Xbox Live: nobody (null).
xbox::XbPtr XB_STDCALL XOnlineGetLogonUsers() { return 0; }

// Logs on the console's first stored account: there are none (see XOnlineGetUsers).
uint32_t XB_STDCALL XOnlineSilentLogon(xbox::XbPtr /*service_ids*/, uint32_t /*service_count*/,
                                       xbox::XbPtr /*event*/, xbox::XbPtr /*task*/) {
    return kSilentLogonNoAccounts;
}

// A game invite the player accepted in the dashboard (which would boot the title into
// that session): never. S_FALSE leaves the title's XONLINE_ACCEPTED_GAMEINVITE as is.
uint32_t XB_STDCALL XOnlineFriendsGetAcceptedGameInvite(xbox::XbPtr /*invite*/) { return 1; }

#define XONLINE(name) {#name, reinterpret_cast<void*>(&name)}

const xbox::KernelImpl kFunctions[] = {
    XONLINE(XOnlineCleanup),
    XONLINE(XOnlineFriendsGetAcceptedGameInvite),
    XONLINE(XOnlineGetLogonUsers),
    XONLINE(XOnlineGetUsers),
    XONLINE(XOnlineSilentLogon),
    XONLINE(XOnlineStartup),
};

}  // namespace

std::span<const xbox::KernelImpl> xonline_functions() { return kFunctions; }

}  // namespace online
