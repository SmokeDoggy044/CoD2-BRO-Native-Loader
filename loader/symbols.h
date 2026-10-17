#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace loader {

// Function names exported from Ghidra (tools/ghidra/ExportSymbols.java), used to
// make logs and crash reports readable. Format: one "hexaddress name" per line.
class SymbolTable {
public:
    bool load(const std::filesystem::path& path);
    size_t size() const { return entries_.size(); }

    // "name+0x12", or the bare hex address if nothing precedes it.
    std::string describe(uint32_t address) const;

    // "00012345 name" of the function `address` lies in (the nearest symbol at or
    // before it), or the bare hex address if nothing precedes it.
    std::string function_at(uint32_t address) const;

    // Addresses of functions called `name`, with or without their library prefix
    // ("DirectSoundCreate" finds "DSOUND::DirectSoundCreate").
    std::vector<uint32_t> find(const std::string& name) const;

    // Addresses of all functions in [start, end), ascending.
    std::vector<uint32_t> in_range(uint32_t start, uint32_t end) const;

private:
    struct Entry {
        uint32_t address;
        std::string name;
    };
    std::vector<Entry> entries_;
};

}  // namespace loader
