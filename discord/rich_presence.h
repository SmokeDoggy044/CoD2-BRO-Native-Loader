#pragma once

// A small, dependency-free Discord Rich Presence client. It speaks Discord's local
// IPC protocol (a Unix domain socket on Linux, a named pipe on Windows) directly:
// no discord-rpc library, no HTTP, no game SDK. The worker thread connects when
// Discord is running, reconnects when it starts later, and publishes whatever the
// status provider returns. See rich_presence.cpp.

#include <cstdint>
#include <functional>
#include <string>

namespace discord {

// App ID of this fork's Discord application (Discord Developer Portal).
constexpr const char* kAppId = "1567972853180010607";

struct Status {
    std::string details;  // first line under the game's name
    std::string state;    // second line (e.g. the map)
    // Extra line for the party/in-game context; empty when unused.
    std::string context;
    bool playing = true;  // false: clear the activity
};

// Optional status source, polled about once a second on the IPC thread. Keep it
// cheap and thread-safe: it may run while the game is drawing a frame.
using StatusProvider = std::function<Status()>;

// Starts the worker. `app_id` is the Discord application ID; empty disables it.
// `instance` distinguishes the launcher from the game ("launcher"/"game").
// Returns false when disabled by the environment (COD_BRO_DISCORD_RPC=0).
bool init(const std::string& app_id, const std::string& instance, StatusProvider provider);

// True when the worker is running (Discord itself may still not be running).
bool active();

// Stops the worker, clears the activity if connected, and joins the thread.
void shutdown();

}  // namespace discord
