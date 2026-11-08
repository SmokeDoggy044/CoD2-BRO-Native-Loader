#pragma once

// SHA-256 (FIPS 180-4), to check a downloaded release against its SHA256SUMS.txt.

#include <string>
#include <string_view>

namespace launcher {

// The digest of `data` as 64 lowercase hex digits (sha256sum's form).
std::string sha256_hex(std::string_view data);

}  // namespace launcher
