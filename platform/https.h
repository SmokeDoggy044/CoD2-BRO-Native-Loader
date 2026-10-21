#pragma once

#include <optional>
#include <string>

namespace platform {

// GET `url` (https) with a timeout of a few seconds. The response body for status 200,
// else nothing (no network, no TLS library, an error status). Blocks: call it from a
// thread of its own.
std::optional<std::string> https_get(const std::string& url, const std::string& user_agent);

}  // namespace platform
