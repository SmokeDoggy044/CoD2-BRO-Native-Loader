#pragma once

#include <optional>
#include <string>

namespace platform {

// GET `url` (https), following redirects, in at most `timeout_seconds` (Windows: per
// network step). The response body for status 200, else nothing (no network, no TLS
// library, an error status). Blocks: call it from a thread of its own.
std::optional<std::string> https_get(const std::string& url, const std::string& user_agent,
                                     long timeout_seconds = 10);

}  // namespace platform
