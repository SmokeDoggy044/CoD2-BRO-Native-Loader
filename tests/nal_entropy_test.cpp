// The NAL entropy mode decoders (game/code/nal_entropy.cpp) against the title's own code:
// the original decoders are pure functions of the stream (no globals, relative jumps
// only), so their bytes run as they are from a copy of default.xbe's .text. Each mode
// decodes the same random streams both ways; the words written, the reader's position and
// the returned zero run must agree.
//
// Needs the game's default.xbe: COD_BRO_XBE, else the path CMake passes in; skipped
// (exit 77) without it.

#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "nal_entropy.h"

namespace {

constexpr uint32_t kCodeStart = 0x0008f440;
constexpr uint32_t kCodeEnd = 0x000930b0;
constexpr uint32_t kTable = 0x008bb4a0;

struct Xbe {
    std::vector<uint8_t> data;
    struct Section {
        uint32_t va, vsize, raw, rsize;
    };
    std::vector<Section> sections;

    bool load(const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        data.resize(static_cast<size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        const bool ok = std::fread(data.data(), 1, data.size(), f) == data.size();
        std::fclose(f);
        if (!ok) return false;
        const uint32_t base = dword_at(0x104);
        const uint32_t count = dword_at(0x11c), headers = dword_at(0x120);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t o = headers - base + i * 0x38;
            sections.push_back({dword_at(o + 4), dword_at(o + 8), dword_at(o + 12), dword_at(o + 16)});
        }
        return true;
    }
    uint32_t dword_at(uint32_t offset) const {
        uint32_t v;
        std::memcpy(&v, data.data() + offset, 4);
        return v;
    }
    const uint8_t* at(uint32_t va, uint32_t size) const {
        for (const Section& s : sections) {
            if (va >= s.va && va + size <= s.va + s.rsize) return data.data() + s.raw + (va - s.va);
        }
        return nullptr;
    }
};

using game::EntropyModeDecoder;
using game::EntropyReader;

struct Result {
    std::vector<uint32_t> out;
    uint32_t ptr;
    uint8_t bit;
    uint32_t left;
};

Result run(EntropyModeDecoder decoder, const uint8_t* stream, uint32_t start_bit, uint32_t count, int32_t stride) {
    Result r;
    r.out.assign(count * (static_cast<uint32_t>(stride) / 4) + 4, 0xcdcdcdcd);
    EntropyReader reader{};
    reader.ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream + start_bit / 8));
    reader.bit = static_cast<uint8_t>(start_bit % 8);
    reader.mode = 0x5a;
    reader.zeros = 0x1234;
    r.left = decoder(&reader, r.out.data(), stride, count);
    r.ptr = reader.ptr - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream));
    r.bit = reader.bit;
    if (reader.mode != 0x5a || reader.zeros != 0x1234) r.left = 0xdeadbeef;  // must not be touched
    return r;
}

uint32_t table_entry(const uint8_t* table, int mode) {
    uint32_t address;
    std::memcpy(&address, table + mode * 4, 4);
    return address;
}

// Modes whose codes include a zero run (0x20-0x2e use the decoders of 0x00-0x0e).
bool has_runs(int mode) {
    switch (mode >= 0x20 && mode < 0x2f ? mode - 0x20 : mode) {
    case 0x01: case 0x06: case 0x07: case 0x09: case 0x0b: case 0x0d: case 0x0e:
    case 0x10: case 0x14: case 0x1d: case 0x32: case 0x35:
        return false;
    default:
        return true;
    }
}

}  // namespace

int main() {
    const char* path = std::getenv("COD_BRO_XBE");
#ifdef COD_BRO_DEFAULT_XBE
    if (!path) path = COD_BRO_DEFAULT_XBE;
#endif
    Xbe xbe;
    if (!path || !xbe.load(path)) {
        std::printf("nal_entropy_test: no default.xbe (set COD_BRO_XBE) - skipped\n");
        return 77;
    }
    const uint8_t* code = xbe.at(kCodeStart, kCodeEnd - kCodeStart);
    const uint8_t* table = xbe.at(kTable, 64 * 4);
    if (!code || !table) {
        std::printf("nal_entropy_test: decoder code or table not found in %s\n", path);
        return 1;
    }
    auto* copy = static_cast<uint8_t*>(
        mmap(nullptr, kCodeEnd - kCodeStart, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (copy == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }
    std::memcpy(copy, code, kCodeEnd - kCodeStart);

    std::mt19937 rng(12345);
    std::vector<uint8_t> stream(200 * 4 + 64);
    int failures = 0, tested = 0;
    for (int mode = 0; mode < 64; ++mode) {
        const EntropyModeDecoder ours = game::kEntropyModeDecoders[mode];
        if (!ours) continue;
        uint32_t address;
        std::memcpy(&address, table + mode * 4, 4);
        const auto original = reinterpret_cast<EntropyModeDecoder>(copy + (address - kCodeStart));
        ++tested;
        int mode_failures = 0, runs_left = 0;
        for (int iteration = 0; iteration < 20000 && mode_failures < 3; ++iteration) {
            // Streams biased towards zero bits now and then (long codes, runs).
            const uint32_t density = rng() % 4;
            const uint32_t start_bit = rng() % 64;
            const uint32_t count = iteration % 50 == 0 ? 0 : 1 + rng() % (iteration % 7 == 0 ? 200 : 20);
            // Only the bytes a test can reach (codes are at most 32 bits; the original reads
            // up to two words ahead).
            const size_t used = (start_bit + count * 32) / 8 + 16;
            for (size_t i = 0; i < used; ++i) {
                uint8_t& b = stream[i];
                b = static_cast<uint8_t>(rng());
                if (density == 1) b &= static_cast<uint8_t>(rng() & rng());
                if (density == 2) b |= static_cast<uint8_t>(rng());
            }
            if (density == 3) {
                // Mode 0x3d's zero-run code (0x80000000) on its 32-bit code boundaries.
                for (uint32_t k = 0; k < count; ++k) {
                    if (!(rng() & 1)) continue;
                    for (uint32_t i = 0; i < 32; ++i) {
                        const uint32_t pos = start_bit + k * 32 + i;
                        const uint8_t mask = static_cast<uint8_t>(1u << (pos % 8));
                        stream[pos / 8] = static_cast<uint8_t>(i == 31 ? stream[pos / 8] | mask : stream[pos / 8] & ~mask);
                    }
                }
            }
            const int32_t stride = (iteration & 1) ? 4 : 12;
            const Result a = run(original, stream.data(), start_bit, count, stride);
            const Result b = run(ours, stream.data(), start_bit, count, stride);
            if (a.left) ++runs_left;
            if (a.out != b.out || a.ptr != b.ptr || a.bit != b.bit || a.left != b.left) {
                ++mode_failures;
                std::printf("mode %02x (0x%08x): start bit %u count %u stride %d: original ptr+%u bit %u left %u, ours "
                            "ptr+%u bit %u left %u\n",
                            mode, address, start_bit, count, stride, a.ptr, a.bit, a.left, b.ptr, b.bit, b.left);
                for (size_t i = 0; i < a.out.size(); ++i) {
                    if (a.out[i] != b.out[i]) {
                        std::printf("  word %zu: original %08x ours %08x\n", i, a.out[i], b.out[i]);
                        break;
                    }
                }
            }
        }
        // Every mode but the all-zero ones has a zero run that can end past the count.
        const bool zeros_only = address == table_entry(table, 0) || address == table_entry(table, 0x1e);
        if (!zeros_only && !runs_left && has_runs(mode)) {
            std::printf("mode %02x: no test ended inside a zero run\n", mode);
            ++mode_failures;
        }
        failures += mode_failures;
    }
    std::printf("nal_entropy_test: %d modes tested, %d failures\n", tested, failures);
    return failures ? 1 : 0;
}
