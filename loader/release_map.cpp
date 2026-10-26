#include "release_map.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "log.h"
#include "symbols.h"

namespace loader {

namespace {

constexpr uint32_t kHeadBytes = 32;
constexpr int kMaxLoggedChanges = 40;

uint32_t read32(const uint8_t* p) {
    uint32_t value;
    std::memcpy(&value, p, 4);
    return value;
}

// Walks `size` bytes of code, hashing them (FNV-1a) without what moves with the code:
// the rel32 after an E8/E9 byte (CALL/JMP) and any four bytes that read as an address
// in [low, high). `slot(offset)` sees the offset of each such address.
template <typename Slot>
uint32_t walk_code(const uint8_t* code, uint32_t size, uint32_t low, uint32_t high, Slot&& slot) {
    uint32_t hash = 0x811c9dc5u;
    auto mix = [&hash](uint8_t byte) { hash = (hash ^ byte) * 0x01000193u; };
    for (uint32_t i = 0; i < size;) {
        const uint8_t byte = code[i];
        if ((byte == 0xE8 || byte == 0xE9) && size - i >= 5) {
            mix(byte);
            mix(0xFF);
            i += 5;
            continue;
        }
        if (size - i >= 4) {
            const uint32_t value = read32(code + i);
            if (value >= low && value < high) {
                slot(i);
                mix(0xFE);
                i += 4;
                continue;
            }
        }
        mix(byte);
        ++i;
    }
    return hash;
}

uint32_t code_hash(const ImageBytes& image, uint32_t address, uint32_t size) {
    if (address < image.base || address >= image.end() || size > image.end() - address) return 0;
    return walk_code(image.bytes.data() + (address - image.base), size, image.base, image.end(), [](uint32_t) {});
}

const ImageSection* find_section(const std::vector<ImageSection>& sections, uint32_t address) {
    for (const ImageSection& section : sections)
        if (address >= section.address && address - section.address < section.size) return &section;
    return nullptr;
}

std::string hex_delta(int32_t delta) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%c0x%x", delta < 0 ? '-' : '+',
                  delta < 0 ? 0u - static_cast<uint32_t>(delta) : static_cast<uint32_t>(delta));
    return buffer;
}

ReleaseMap g_map;
size_t g_missed = 0;

}  // namespace

bool is_data_section(std::string_view name) {
    for (const char* data : {".rdata", ".data", ".data1", "XON_RD", ".XTLID", "$$XTIMAGE", "$$XSIMAGE"})
        if (name == data) return true;
    return false;
}

bool ReleaseSignatures::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    *this = {};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string first;
        fields >> first >> std::hex;
        if (first == "image_end") {
            fields >> image_end;
        } else if (first == "section") {
            ImageSection section;
            fields >> section.name >> section.address >> section.size;
            sections.push_back(section);
        } else if (first == "data") {
            DataReference reference;
            fields >> reference.target >> reference.site;
            data.push_back(reference);
        } else {
            FunctionSignature function;
            function.address = static_cast<uint32_t>(std::stoul(first, nullptr, 16));
            fields >> function.length >> function.head >> function.body;
            functions.push_back(function);
        }
        if (!fields) return false;
    }
    std::sort(functions.begin(), functions.end(),
              [](const FunctionSignature& a, const FunctionSignature& b) { return a.address < b.address; });
    std::sort(data.begin(), data.end(),
              [](const DataReference& a, const DataReference& b) { return a.target < b.target; });
    return image_end != 0 && !sections.empty() && !functions.empty();
}

bool ReleaseSignatures::save(const std::filesystem::path& path) const {
    std::FILE* out = std::fopen(path.string().c_str(), "w");
    if (!out) return false;
    std::fprintf(out,
                 "# The supported build's sections, functions (address, length, hashes of the first 32 bytes and\n"
                 "# of all of it) and data references (address, a place in code that holds it), for finding\n"
                 "# them in other releases. Written by `cod_bro_loader --write-signatures`.\n");
    std::fprintf(out, "image_end %08x\n", image_end);
    for (const ImageSection& section : sections)
        std::fprintf(out, "section %s %08x %08x\n", section.name.c_str(), section.address, section.size);
    for (const FunctionSignature& function : functions)
        std::fprintf(out, "%08x %x %08x %08x\n", function.address, function.length, function.head, function.body);
    for (const DataReference& reference : data)
        std::fprintf(out, "data %08x %08x\n", reference.target, reference.site);
    return std::fclose(out) == 0;
}

ReleaseSignatures make_signatures(const ImageBytes& image, std::span<const uint32_t> functions) {
    ReleaseSignatures result;
    result.image_end = image.end();
    result.sections = image.sections;
    std::vector<uint32_t> sorted(functions.begin(), functions.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    std::unordered_map<uint32_t, uint32_t> sites;  // data target -> first place that holds it
    for (size_t i = 0; i < sorted.size(); ++i) {
        const ImageSection* section = find_section(image.sections, sorted[i]);
        if (!section || is_data_section(section->name)) continue;
        const uint32_t section_end = section->address + section->size;
        const uint32_t next = i + 1 < sorted.size() ? std::min(sorted[i + 1], section_end) : section_end;
        FunctionSignature function{sorted[i], next - sorted[i], 0, 0};
        const uint8_t* code = image.bytes.data() + (function.address - image.base);
        function.head = code_hash(image, function.address, std::min(kHeadBytes, function.length));
        function.body = walk_code(code, function.length, image.base, image.end(), [&](uint32_t offset) {
            const uint32_t target = read32(code + offset);
            const ImageSection* in = find_section(image.sections, target);
            if (in && is_data_section(in->name)) sites.try_emplace(target, function.address + offset);
        });
        result.functions.push_back(function);
    }
    for (const auto& [target, site] : sites) result.data.push_back({target, site});
    std::sort(result.data.begin(), result.data.end(),
              [](const DataReference& a, const DataReference& b) { return a.target < b.target; });
    return result;
}

void ReleaseMap::build(const ReleaseSignatures& supported, const ImageBytes& image) {
    *this = {};
    built_ = true;
    for (const ImageSection& section : supported.sections) {
        Section entry{section, std::nullopt, std::nullopt};
        for (const ImageSection& here : image.sections)
            if (here.name == section.name) {
                entry.delta = static_cast<int32_t>(here.address - section.address);
                entry.size = here.size;
            }
        sections_.push_back(entry);
    }
    for (const FunctionSignature& signature : supported.functions) functions_.push_back({signature});

    auto matches = [&](const Function& function, int32_t delta, bool whole) {
        const FunctionSignature& s = function.signature;
        const uint32_t here = s.address + static_cast<uint32_t>(delta);
        return whole ? code_hash(image, here, s.length) == s.body
                     : code_hash(image, here, std::min(kHeadBytes, s.length)) == s.head;
    };

    // Each function whose first 32 bytes are unlike any other's is looked for at every
    // byte of the code sections here; found once with all of it alike, it is unchanged.
    std::unordered_map<uint32_t, size_t> unique_heads;
    {
        std::unordered_map<uint32_t, int> count;
        for (const Function& function : functions_)
            if (function.signature.length >= kHeadBytes) ++count[function.signature.head];
        for (size_t i = 0; i < functions_.size(); ++i)
            if (functions_[i].signature.length >= kHeadBytes && count[functions_[i].signature.head] == 1)
                unique_heads.emplace(functions_[i].signature.head, i);
    }
    std::vector<uint32_t> found(functions_.size(), 0);
    std::vector<int> times(functions_.size(), 0);
    for (const ImageSection& section : image.sections) {
        if (is_data_section(section.name) || section.size < kHeadBytes) continue;
        for (uint32_t address = section.address; address <= section.address + section.size - kHeadBytes; ++address) {
            const auto it = unique_heads.find(code_hash(image, address, kHeadBytes));
            if (it == unique_heads.end()) continue;
            found[it->second] = address;
            ++times[it->second];
        }
    }
    for (size_t i = 0; i < functions_.size(); ++i) {
        if (times[i] != 1) continue;
        const int32_t delta = static_cast<int32_t>(found[i] - functions_[i].signature.address);
        if (matches(functions_[i], delta, true)) functions_[i] = {functions_[i].signature, delta, Match::Whole};
    }

    // The others (short, repeated or changed functions) are tried where their
    // neighbours moved to, until that finds no more.
    for (bool progress = true; progress;) {
        progress = false;
        auto try_delta = [&](Function& function, const Function* neighbour) {
            if (!neighbour || function.match == Match::Whole) return;
            if (find_section(supported.sections, neighbour->signature.address) !=
                find_section(supported.sections, function.signature.address))
                return;
            if (matches(function, neighbour->delta, true)) {
                function = {function.signature, neighbour->delta, Match::Whole};
                progress = true;
            } else if (function.match == Match::None && matches(function, neighbour->delta, false)) {
                function = {function.signature, neighbour->delta, Match::Entry};
                progress = true;
            }
        };
        const Function* before = nullptr;
        for (Function& function : functions_) {
            try_delta(function, before);
            if (function.match == Match::Whole) before = &function;
        }
        const Function* after = nullptr;
        for (auto it = functions_.rbegin(); it != functions_.rend(); ++it) {
            try_delta(*it, after);
            if (it->match == Match::Whole) after = &*it;
        }
    }
    for (const Function& function : functions_) {
        if (function.match == Match::Whole) ++unchanged_;
        if (function.match == Match::Entry) ++entry_only_;
    }

    // Data: read from where the same code here holds it.
    for (const DataReference& reference : supported.data) {
        Data entry{reference.target, 0, false};
        const Function* function = function_at(reference.site);
        if (function && function->match == Match::Whole) {
            const uint32_t site = reference.site + static_cast<uint32_t>(function->delta);
            if (site >= image.base && site <= image.end() - 4) {
                const uint32_t target = read32(image.bytes.data() + (site - image.base));
                entry = {reference.target, static_cast<int32_t>(target - reference.target), true};
            }
        }
        data_.push_back(entry);
    }
}

const ReleaseMap::Section* ReleaseMap::section_of(uint32_t address) const {
    for (const Section& section : sections_)
        if (address >= section.supported.address && address - section.supported.address < section.supported.size)
            return &section;
    return nullptr;
}

const ReleaseMap::Function* ReleaseMap::function_at(uint32_t address) const {
    auto it = std::upper_bound(functions_.begin(), functions_.end(), address,
                               [](uint32_t a, const Function& f) { return a < f.signature.address; });
    if (it == functions_.begin()) return nullptr;
    --it;
    return address - it->signature.address < it->signature.length ? &*it : nullptr;
}

std::optional<int32_t> ReleaseMap::data_delta(uint32_t address, const Section& section) const {
    const uint32_t start = section.supported.address, end = start + section.supported.size;
    auto it = std::lower_bound(data_.begin(), data_.end(), address,
                               [](const Data& d, uint32_t a) { return d.target < a; });
    if (it != data_.end() && it->target == address) return it->known ? std::optional(it->delta) : std::nullopt;
    // Between two known variables of this section: theirs, if they moved alike. With
    // one side only (the section's first or last referenced variable), that side's.
    const Data* below = nullptr;
    for (auto b = std::make_reverse_iterator(it); b != data_.rend() && b->target >= start; ++b)
        if (b->known) {
            below = &*b;
            break;
        }
    const Data* above = nullptr;
    for (auto a = it; a != data_.end() && a->target < end; ++a)
        if (a->known) {
            above = &*a;
            break;
        }
    if (below && above) return below->delta == above->delta ? std::optional(below->delta) : std::nullopt;
    if (below) return below->delta;
    if (above) return above->delta;
    return section.delta;
}

std::optional<uint32_t> ReleaseMap::find(uint32_t address) const {
    if (!built_) return address;
    const Section* section = section_of(address);
    if (!section || !section->delta) return std::nullopt;
    std::optional<int32_t> delta;
    if (is_data_section(section->supported.name)) {
        delta = data_delta(address, *section);
    } else if (const Function* function = function_at(address)) {
        const bool entry = address == function->signature.address;
        if (function->match == Match::Whole || (function->match == Match::Entry && entry)) delta = function->delta;
    } else {
        delta = section->delta;  // before the section's first function
    }
    if (!delta) return std::nullopt;
    return address + static_cast<uint32_t>(*delta);
}

uint32_t ReleaseMap::guess(uint32_t address) const {
    if (const std::optional<uint32_t> found = find(address)) return *found;
    auto it = std::upper_bound(functions_.begin(), functions_.end(), address,
                               [](uint32_t a, const Function& f) { return a < f.signature.address; });
    while (it != functions_.begin()) {
        --it;
        if (it->match != Match::None) return address + static_cast<uint32_t>(it->delta);
    }
    const Section* section = section_of(address);
    return section && section->delta ? address + static_cast<uint32_t>(*section->delta) : address;
}

void ReleaseMap::log_summary(const SymbolTable* names) const {
    if (!built_) return;
    LOG_INFO("release", "%zu of %zu functions found unchanged, %zu changed after their start, %zu not found",
             unchanged_, functions_.size(), entry_only_, missing());
    for (const Section& section : sections_) {
        const ImageSection& s = section.supported;
        if (!section.delta) {
            LOG_WARN("release", "  %-10s %08x-%08x: not in this build", s.name.c_str(), s.address, s.address + s.size);
            continue;
        }
        // Runs of the same offset over the section's functions (code) or referenced
        // variables (data).
        std::string runs;
        int count = 0;
        auto add_run = [&](uint32_t first, uint32_t last, std::optional<int32_t> delta) {
            if (++count > 12) return;
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s%08x-%08x %s", runs.empty() ? "" : ", ", first, last,
                          delta ? hex_delta(*delta).c_str() : "?");
            runs += buffer;
        };
        std::optional<uint32_t> first;
        uint32_t last = 0;
        std::optional<int32_t> current;
        auto step = [&](uint32_t address, uint32_t end, std::optional<int32_t> delta) {
            if (first && delta != current) {
                add_run(*first, last, current);
                first.reset();
            }
            if (!first) first = address;
            last = end;
            current = delta;
        };
        if (is_data_section(s.name)) {
            for (const Data& d : data_)
                if (d.target >= s.address && d.target - s.address < s.size)
                    step(d.target, d.target, d.known ? std::optional(d.delta) : std::nullopt);
        } else {
            for (const Function& f : functions_)
                if (f.signature.address >= s.address && f.signature.address - s.address < s.size)
                    step(f.signature.address, f.signature.address + f.signature.length - 1,
                         f.match == Match::Whole ? std::optional(f.delta) : std::nullopt);
        }
        if (first) add_run(*first, last, current);
        if (count > 12) runs += ", ... (" + std::to_string(count) + " runs)";
        const bool resized = section.size && *section.size != s.size;
        if (runs.empty()) runs = hex_delta(*section.delta);
        LOG_INFO("release", "  %-10s %s%s", s.name.c_str(), runs.c_str(), resized ? " (other size)" : "");
    }
    int logged = 0;
    for (const Function& f : functions_) {
        if (f.match == Match::Whole) continue;
        if (++logged > kMaxLoggedChanges) continue;
        const std::string name = names ? names->describe(f.signature.address) : "";
        LOG_INFO("release", "  changed: %08x (%x bytes) %s%s", f.signature.address, f.signature.length,
                 name.c_str(), f.match == Match::Entry ? ", start found" : "");
    }
    if (logged > kMaxLoggedChanges) LOG_INFO("release", "  ... and %d more changed", logged - kMaxLoggedChanges);
}

void set_release_map(const ReleaseMap& map) { g_map = map; }
const ReleaseMap& release_map() { return g_map; }

uint32_t title_address(uint32_t address, const char* what) {
    if (const std::optional<uint32_t> found = g_map.find(address)) return *found;
    ++g_missed;
    LOG_WARN("release", "%08x%s%s: not found in this release (its code or data changed)", address, what ? " " : "",
             what ? what : "");
    return g_map.guess(address);
}

size_t missed_title_addresses() { return g_missed; }

}  // namespace loader
