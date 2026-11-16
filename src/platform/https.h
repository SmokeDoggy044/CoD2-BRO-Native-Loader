#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace platform {

// GET `url` (https), following redirects, in at most `timeout_seconds` (Windows: per
// network step). The response body for status 200, else nothing (no network, no TLS
// library, an error status). Plain http only on 127.0.0.1 (developer test servers). Blocks: call it from a thread
// of its own.
std::optional<std::string> https_get(const std::string& url, const std::string& user_agent,
                                     long timeout_seconds = 10);

// Streams `length` bytes of `url` from byte `offset` into `sink` (texture pack downloads: a range request; a server
// that answers with the whole file has the bytes before `offset` skipped). `sink` returns false to stop (cancel).
// Gives up when nothing arrives for `stall_seconds`. True when all `length` bytes went to `sink`; else `error` says
// why. Developer test servers: plain http on 127.0.0.1 is allowed too. Blocks: a thread of its own.
bool https_download(const std::string& url, const std::string& user_agent, uint64_t offset, uint64_t length,
                    const std::function<bool(const char* data, size_t size)>& sink, std::string& error,
                    long stall_seconds = 30);

}  // namespace platform
