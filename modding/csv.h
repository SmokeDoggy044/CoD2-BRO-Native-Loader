#pragma once

// The small CSV reader/writer the Modding window uses for its manifests
// (textures.csv, audio.csv). Quoted fields with commas, quotes and newlines work.

#include <cstdint>
#include <string>
#include <vector>

namespace modding {

std::string csv_field(const std::string& text);
std::string csv_line(const std::vector<std::string>& fields);
std::vector<std::string> csv_split(const std::string& line);

std::string hex64(uint64_t value);
uint64_t parse_hex64(const std::string& text);  // 0 on malformed input

}  // namespace modding
