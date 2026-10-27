#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace loader {

class SymbolTable;

// Another release of the game can have the same code at other addresses (issue #9:
// the European French/Spanish/Italian disc's multiplayer XBE has 0x30 more bytes of
// code, and everything after them moved). Every address the port uses is one of the
// supported build's; ReleaseMap finds where each lies in another build by looking for
// the supported build's functions in it.

struct ImageSection {
    std::string name;
    uint32_t address;
    uint32_t size;
};

// A mapped image, read only: bytes[i] is at address base + i.
struct ImageBytes {
    uint32_t base = 0;
    std::span<const uint8_t> bytes;
    std::vector<ImageSection> sections;
    uint32_t end() const { return base + static_cast<uint32_t>(bytes.size()); }
};

// One function of the supported build (it reaches to the next symbol or its section's
// end): hashes of its first 32 bytes (the instructions that start in them; fewer if it
// is shorter) and of all of it. The hashes are taken instruction by instruction and
// leave out what moves with the code - rel32 targets, 32-bit operands that read as an
// address in the image and jump table entries - so the same code at other addresses
// hashes the same. (Masking any four bytes that read as an address, as 0.1.12 did,
// let a window across two instructions hide the address after it: issue #9.)
struct FunctionSignature {
    uint32_t address;
    uint32_t length;
    uint32_t head;
    uint32_t body;
};

// An address in a data section that code refers to, and one place in a function
// that holds it. Data moves within its section too (the Collector's Edition
// multiplayer XBE has the same code as ours, but part of .data in another order), so
// where a variable is in another build is read from that place in the same code there.
struct DataReference {
    uint32_t target;
    uint32_t site;
};

// The supported build's layout, functions and data references
// (tools/data/<xbe>_signatures.txt, written by `--write-signatures`).
struct ReleaseSignatures {
    uint32_t image_end = 0;
    std::vector<ImageSection> sections;
    std::vector<FunctionSignature> functions;  // ascending, code sections only
    std::vector<DataReference> data;           // ascending by target

    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};

// Sections holding only data (strings, tables, variables, images), by name.
bool is_data_section(std::string_view name);

// The signatures of `functions` (any order; those in data sections are left out).
ReleaseSignatures make_signatures(const ImageBytes& image, std::span<const uint32_t> functions);

class ReleaseMap {
public:
    // Matches the supported build's functions in `image`. Until then, the map is the
    // identity (the supported build itself).
    void build(const ReleaseSignatures& supported, const ImageBytes& image);
    bool identity() const { return !built_; }

    // Where `address` of the supported build lies in this one: a function's entry
    // needs its first bytes found, an address inside it the whole function (unchanged
    // code). A data address is where code that refers to it (or to the nearest
    // variables on both sides, if they moved alike) refers to here. nullopt: in code
    // this release changed, in data whose place is unknown, or in a section it does
    // not have.
    std::optional<uint32_t> find(uint32_t address) const;
    // The same, but in changed code the address moved like the nearest function
    // before it that was found.
    uint32_t guess(uint32_t address) const;

    // How many of the supported build's functions were found unchanged / only their
    // first bytes / not at all.
    size_t unchanged() const { return unchanged_; }
    size_t entry_only() const { return entry_only_; }
    size_t missing() const { return functions_.size() - unchanged_ - entry_only_; }

    // Logs how each section moved (address ranges of the supported build and their
    // offset here) and the functions that changed, named from `names` if given.
    void log_summary(const SymbolTable* names) const;

private:
    enum class Match : uint8_t { None, Entry, Whole };
    struct Function {
        FunctionSignature signature;
        int32_t delta = 0;
        Match match = Match::None;
    };
    struct Section {
        ImageSection supported;
        std::optional<int32_t> delta;  // to the section of the same name here
        std::optional<uint32_t> size;  // its size here
    };
    struct Data {
        uint32_t target;
        int32_t delta;
        bool known;  // false: its site is in changed code, or sites disagree
    };
    const Section* section_of(uint32_t address) const;
    const Function* function_at(uint32_t address) const;
    std::optional<int32_t> data_delta(uint32_t address, const Section& section) const;

    bool built_ = false;
    std::vector<Section> sections_;
    std::vector<Function> functions_;
    std::vector<Data> data_;  // ascending by target
    size_t unchanged_ = 0;
    size_t entry_only_ = 0;
};

// The map the port's title addresses go through (patches.cpp, main.cpp); the
// identity unless the loaded XBE is another build.
void set_release_map(const ReleaseMap& map);
const ReleaseMap& release_map();

// `address` of the supported build in the loaded one. One in code this release
// changed is logged with `what` and counted, and its guess is returned (the patch
// there checks the code it expects where it can).
uint32_t title_address(uint32_t address, const char* what = nullptr);
// How many title_address calls found nothing.
size_t missed_title_addresses();

}  // namespace loader
