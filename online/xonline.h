#pragma once

// Xbox Live / network (XONLINE, XNET libraries) replacement. Xbox Live is gone:
// XOnlineStartup fails and other XONLINE functions trap by name if a title reaches
// them. System link runs on a virtual LAN over the host's network (lan.h).

#include <span>

#include "kernel.h"
#include "lan.h"

namespace online {

std::span<const xbox::KernelImpl> xonline_functions();
// Call before the title starts the network (optional; default: LanConfig{}).
void configure_network(const LanConfig& config);
// XNET: the network stack (sockets, XNet addresses and keys) on the host's network.
std::span<const xbox::KernelImpl> xnet_functions();

}  // namespace online
