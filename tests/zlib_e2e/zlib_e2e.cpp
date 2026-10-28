// End-to-end test of the zlib module (not built by CMake; see tests/zlib_e2e/README.md): maps default.xbe at its
// addresses, replaces the title CRT's malloc/free/stdio with host stand-ins, and runs inflate,
// deflate and zip scenarios through the title's entry points - first with the original code,
// then with one decompiled function (or all) patched in at its address. Every scenario's
// results and the whole allocation arena must be the same.
#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "functions.h"

namespace {

std::vector<uint8_t> read_file(const char* path) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path, "rb");
    if (!f) return v;
    std::fseek(f, 0, SEEK_END);
    v.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
    std::fclose(f);
    return v;
}
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// ---- arena allocator and stdio stand-ins -------------------------------------------
constexpr size_t kArena = 16u << 20;
uint8_t* g_arena;
size_t g_used;
FILE* g_files[16];
std::string g_dir;

extern "C" __attribute__((cdecl)) void* sa_malloc(uint32_t size) {
    size_t at = (g_used + 15) & ~size_t{15};
    if (at + size > kArena) return nullptr;
    g_used = at + size;
    return g_arena + at;
}
extern "C" __attribute__((cdecl)) void sa_free(void*) {}
extern "C" __attribute__((cdecl)) uint32_t sa_fopen(const char* path, const char*) {
    for (uint32_t i = 1; i < 16; i++)
        if (!g_files[i]) {
            g_files[i] = std::fopen((g_dir + "/" + path).c_str(), "rb");
            return g_files[i] ? 0x1000 + i : 0;
        }
    return 0;
}
FILE* host_file(uint32_t h) { return (h >= 0x1000 && h < 0x1010) ? g_files[h - 0x1000] : nullptr; }
extern "C" __attribute__((cdecl)) int sa_fclose(uint32_t h) {
    FILE* f = host_file(h);
    if (!f) return -1;
    std::fclose(f);
    g_files[h - 0x1000] = nullptr;
    return 0;
}
extern "C" __attribute__((cdecl)) uint32_t sa_fread(void* buf, uint32_t size, uint32_t count, uint32_t h) {
    FILE* f = host_file(h);
    return f ? static_cast<uint32_t>(std::fread(buf, size, count, f)) : 0;
}
extern "C" __attribute__((cdecl)) int sa_fseek(uint32_t h, int32_t off, int whence) {
    FILE* f = host_file(h);
    return f ? std::fseek(f, off, whence) : -1;
}
extern "C" __attribute__((cdecl)) int32_t sa_ftell(uint32_t h) {
    FILE* f = host_file(h);
    return f ? static_cast<int32_t>(std::ftell(f)) : -1;
}

void patch_jmp(uint32_t at, const void* target) {
    uint8_t* code = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(at));
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (at + 5));
    code[0] = 0xe9;
    std::memcpy(code + 1, &rel, 4);
}

// ---- the image -----------------------------------------------------------------------
bool map_image(const char* path) {
    std::vector<uint8_t> x = read_file(path);
    if (x.empty()) return false;
    const uint32_t base = rd32(&x[0x104]), count = rd32(&x[0x11c]), headers = rd32(&x[0x120]);
    uint32_t hi = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* h = &x[headers - base + i * 0x38];
        hi = std::max(hi, rd32(h + 4) + std::max(rd32(h + 8), rd32(h + 16)));
    }
    hi = (hi + 0xfff) & ~0xfffu;
    void* at = mmap(reinterpret_cast<void*>(uintptr_t{0x10000}), hi - 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (at != reinterpret_cast<void*>(uintptr_t{0x10000})) return false;
    std::memcpy(at, x.data(), 0x1000);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* h = &x[headers - base + i * 0x38];
        std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(rd32(h + 4))), &x[rd32(h + 12)], rd32(h + 16));
    }
    patch_jmp(0x001f0d14, reinterpret_cast<void*>(&sa_malloc));
    patch_jmp(0x001f0c69, reinterpret_cast<void*>(&sa_free));
    patch_jmp(0x001effb3, reinterpret_cast<void*>(&sa_fopen));
    patch_jmp(0x001f0790, reinterpret_cast<void*>(&sa_fclose));
    patch_jmp(0x001f019a, reinterpret_cast<void*>(&sa_fread));
    patch_jmp(0x001f06fb, reinterpret_cast<void*>(&sa_fseek));
    patch_jmp(0x001f062b, reinterpret_cast<void*>(&sa_ftell));
    return true;
}

// The module's functions: original bytes, decompiled entry.
struct Mod {
    const char* name;
    uint32_t address;
    const void* code;
    uint8_t saved[5];
};
std::vector<Mod> g_mod;

void set_mode(int only) {  // -1 = all original, -2 = all ours, else that index only
    for (size_t i = 0; i < g_mod.size(); i++) {
        uint8_t* code = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(g_mod[i].address));
        if (only == -2 || static_cast<int>(i) == only)
            patch_jmp(g_mod[i].address, g_mod[i].code);
        else
            std::memcpy(code, g_mod[i].saved, 5);
    }
}

// ---- calls into the title -------------------------------------------------------------
struct ZStream {
    const uint8_t* next_in;
    uint32_t avail_in, total_in;
    uint8_t* next_out;
    uint32_t avail_out, total_out;
    const char* msg;
    void* state;
    uint32_t zalloc, zfree, opaque;
    int32_t data_type;
    uint32_t adler, reserved;
};
template <typename F>
F* at(uint32_t a) {
    return reinterpret_cast<F*>(static_cast<uintptr_t>(a));
}
using Fn2 = int32_t __attribute__((cdecl))(void*, int32_t);
const char* kVersion = reinterpret_cast<const char*>(uintptr_t{0x007baddc});

struct Log {
    std::vector<uint32_t> v;
    void put(uint32_t x) { v.push_back(x); }
    void bytes(const uint8_t* p, size_t n) {
        put(static_cast<uint32_t>(n));
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
        put(h);
    }
    void stream(const ZStream* z) {
        put(z->avail_in), put(z->total_in), put(z->avail_out), put(z->total_out);
        put(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(z->msg))), put(z->adler), put(z->data_type);
    }
};

struct Inputs {
    std::vector<std::vector<uint8_t>> datas;
    struct Case {
        int32_t wbits;
        std::vector<uint8_t> raw, comp;
    };
    std::vector<Case> cases;
} g_in;

int g_fill;
void reset_heap() {
    std::memset(g_arena, g_fill, kArena);
    g_used = 0;
    for (auto& f : g_files)
        if (f) std::fclose(f), f = nullptr;
}

void run_inflate(Log& log, const Inputs::Case& c, uint32_t in_chunk, uint32_t out_chunk, int flush) {
    ZStream* z = static_cast<ZStream*>(sa_malloc(sizeof(ZStream)));
    std::memset(z, 0, sizeof(ZStream));
    uint8_t* out = static_cast<uint8_t*>(sa_malloc(static_cast<uint32_t>(c.raw.size() + 64)));
    int32_t r = at<int32_t __attribute__((cdecl))(ZStream*, int32_t, const char*, int32_t)>(0x00057f00)(
        z, c.wbits, kVersion, 0x38);
    log.put(static_cast<uint32_t>(r));
    if (r != 0) return;
    size_t pos_in = 0, pos_out = 0;
    for (int iter = 0; iter < 400000; iter++) {
        z->next_in = c.comp.data() + pos_in;
        z->avail_in = static_cast<uint32_t>(std::min<size_t>(in_chunk, c.comp.size() - pos_in));
        z->next_out = out + pos_out;
        z->avail_out = static_cast<uint32_t>(std::min<size_t>(out_chunk, c.raw.size() + 64 - pos_out));
        const uint32_t ai = z->avail_in, ao = z->avail_out;
        r = at<Fn2>(0x00058020)(z, flush);
        pos_in += ai - z->avail_in;
        pos_out += ao - z->avail_out;
        log.put(static_cast<uint32_t>(r));
        log.stream(z);
        if (r != 0 && r != -5) break;
        if (r == -5 && pos_in >= c.comp.size()) break;
        if (z->avail_out == 0 && pos_out >= c.raw.size() + 64) break;
    }
    log.bytes(out, pos_out);
    log.put(static_cast<uint32_t>(at<int32_t __attribute__((cdecl))(ZStream*)>(0x00057eb0)(z)));
}

void run_uncompress(Log& log, const Inputs::Case& c) {
    const uint32_t cap = static_cast<uint32_t>(c.raw.size() + 16);
    uint8_t* out = static_cast<uint8_t*>(sa_malloc(cap));
    uint32_t len = cap;
    log.put(static_cast<uint32_t>(
        at<int32_t __attribute__((cdecl))(uint8_t*, uint32_t*, const uint8_t*, uint32_t)>(0x00057cf0)(
            out, &len, c.comp.data(), static_cast<uint32_t>(c.comp.size()))));
    log.put(len);
    log.bytes(out, std::min<size_t>(len, cap));
}

void run_deflate(Log& log, const std::vector<uint8_t>& d, int level, int wbits, int mem, int strategy,
                 uint32_t in_chunk, uint32_t out_chunk, int flush_every) {
    ZStream* z = static_cast<ZStream*>(sa_malloc(sizeof(ZStream)));
    std::memset(z, 0, sizeof(ZStream));
    const uint32_t cap = static_cast<uint32_t>(d.size() + d.size() / 8 + 4096);
    uint8_t* out = static_cast<uint8_t*>(sa_malloc(cap));
    int32_t r = at<int32_t __attribute__((cdecl))(ZStream*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*,
                                                   int32_t)>(0x00059df0)(z, level, 8, wbits, mem, strategy,
                                                                          kVersion, 0x38);
    log.put(static_cast<uint32_t>(r));
    if (r != 0) return;
    size_t pos_in = 0, pos_out = 0;
    for (int iter = 0; iter < 400000; iter++) {
        const bool last = pos_in + in_chunk >= d.size();
        z->next_in = d.data() + pos_in;
        z->avail_in = static_cast<uint32_t>(std::min<size_t>(in_chunk, d.size() - pos_in));
        z->next_out = out + pos_out;
        z->avail_out = static_cast<uint32_t>(std::min<size_t>(out_chunk, cap - pos_out));
        const uint32_t ai = z->avail_in, ao = z->avail_out;
        int flush = last ? 4 : (flush_every && iter % flush_every == flush_every - 1 ? 1 + (iter / flush_every) % 3 : 0);
        r = at<Fn2>(0x00058880)(z, flush);
        pos_in += ai - z->avail_in;
        pos_out += ao - z->avail_out;
        log.put(static_cast<uint32_t>(r));
        log.stream(z);
        if (r == 1 || (r != 0 && r != -5)) break;
        if (pos_out >= cap) break;
    }
    log.bytes(out, pos_out);
    log.put(static_cast<uint32_t>(at<int32_t __attribute__((cdecl))(ZStream*)>(0x00058b10)(z)));
}

void run_compress2(Log& log, const std::vector<uint8_t>& d, int level) {
    const uint32_t cap = static_cast<uint32_t>(d.size() + d.size() / 8 + 64);
    uint8_t* out = static_cast<uint8_t*>(sa_malloc(cap));
    uint32_t len = cap;
    log.put(static_cast<uint32_t>(
        at<int32_t __attribute__((cdecl))(uint8_t*, uint32_t*, const uint8_t*, uint32_t, int32_t)>(0x00057d90)(
            out, &len, d.data(), static_cast<uint32_t>(d.size()), level)));
    log.put(len);
    log.bytes(out, std::min<size_t>(len, cap));
}

using GetInfo = int32_t __attribute__((regparm(1), cdecl))(void*, void*, uint32_t*, char*, uint32_t, void*, uint32_t,
                                                           char*, uint32_t);

void run_zip(Log& log, const char* name) {
    void* s = at<void* __attribute__((cdecl))(const char*)>(0x000579f0)(name);  // unzOpen
    log.put(s != nullptr);
    if (!s) return;
    const uint32_t first_pos = reinterpret_cast<uint32_t*>(s)[5];
    std::vector<uint32_t> positions;
    positions.reserve(128);
    for (int k = 0; k < 64; k++) {
        positions.push_back(reinterpret_cast<uint32_t*>(s)[5]);
        uint8_t info[0x50];
        uint32_t internal = 0;
        char fname[300], comment[300];
        uint8_t extra[64];
        std::memset(fname, 0x55, sizeof fname);
        std::memset(comment, 0x55, sizeof comment);
        const uint32_t sizes[] = {256, 5, 0};
        const int32_t r = at<GetInfo>(0x00056e10)(s, info, &internal, k % 4 == 3 ? nullptr : fname, sizes[k % 3],
                                                  k % 2 ? extra : nullptr, k % 3 ? 64 : 3, comment, sizes[(k + 1) % 3]);
        log.put(static_cast<uint32_t>(r));
        log.bytes(info, sizeof info);
        log.put(internal);
        log.bytes(reinterpret_cast<uint8_t*>(fname), sizeof fname);
        log.bytes(reinterpret_cast<uint8_t*>(comment), sizeof comment);
        const int32_t o = at<int32_t __attribute__((cdecl))(void*)>(0x00057bd0)(s);  // unzOpenCurrentFile
        log.put(static_cast<uint32_t>(o));
        if (o == 0) {
            static std::vector<uint8_t> buf(200000);
            size_t got = 0;
            const uint32_t chunks[] = {100000, 1, 777, 16384};
            for (int it = 0; it < 300000; it++) {
                const uint32_t want = static_cast<uint32_t>(std::min<size_t>(chunks[(k + it) % 4], buf.size() - got));
                const int32_t n = at<int32_t __attribute__((cdecl))(void*, void*, uint32_t)>(0x00057620)(
                    s, buf.data() + got, want);
                log.put(static_cast<uint32_t>(n));
                log.put(static_cast<uint32_t>(at<int32_t __attribute__((cdecl))(void*)>(0x00057810)(s)));  // unztell
                if (n <= 0) break;
                got += static_cast<uint32_t>(n);
            }
            log.bytes(buf.data(), got);
            if (k % 5 != 4)
                log.put(static_cast<uint32_t>(at<int32_t __attribute__((cdecl))(void*)>(0x000578f0)(s)));
        }
        const int32_t nx = at<int32_t __attribute__((cdecl))(void*)>(0x00057260)(s);  // unzGoToNextFile
        log.put(static_cast<uint32_t>(nx));
        if (nx != 0) break;
    }
    // Back to earlier files by position.
    for (uint32_t p : {positions.back(), first_pos, positions[positions.size() / 2], 0x12345u}) {
        log.put(static_cast<uint32_t>(at<int32_t __attribute__((cdecl))(void*, uint32_t)>(0x000572f0)(s, p)));
        log.put(reinterpret_cast<uint32_t*>(s)[6]);
        const int32_t o = at<int32_t __attribute__((cdecl))(void*)>(0x00057bd0)(s);
        log.put(static_cast<uint32_t>(o));
        if (o == 0) {
            uint8_t buf[3000];
            const int32_t n = at<int32_t __attribute__((cdecl))(void*, void*, uint32_t)>(0x00057620)(s, buf, sizeof buf);
            log.put(static_cast<uint32_t>(n));
            if (n > 0) log.bytes(buf, static_cast<size_t>(n));
        }
    }
    log.put(static_cast<uint32_t>(at<int32_t __attribute__((cdecl))(void*)>(0x00057b90)(s)));  // unzClose
}

// One scenario = one function of work on a fresh heap; returns the log plus the arena hash.
struct Scenario {
    std::string label;
    void (*fn)(Log&, size_t);
    size_t arg;
};
std::vector<Scenario> g_sc;

void add_scenarios() {
    for (size_t i = 0; i < g_in.cases.size(); i++) {
        g_sc.push_back({"inflate#" + std::to_string(i), [](Log& l, size_t a) {
                            const auto& c = g_in.cases[a];
                            run_inflate(l, c, 1u << 30, 1u << 30, 2);
                            if (c.raw.size() < 20000 || a % 9 == 0) {
                                run_inflate(l, c, 7, 13, 2);
                                run_inflate(l, c, 1000, 300, 4);
                            }
                            if (c.raw.size() < 3000) run_inflate(l, c, 1, 64, 0);
                            if (c.wbits == 15) run_uncompress(l, c);
                        },
                        i});
    }
    size_t k = 0;
    for (size_t di = 0; di < g_in.datas.size(); di++)
        for (int level = 0; level <= 9; level++)
            for (int v = 0; v < 3; v++) {
                const size_t a = di * 1000 + static_cast<size_t>(level) * 10 + static_cast<size_t>(v);
                g_sc.push_back({"deflate#" + std::to_string(k++), [](Log& l, size_t a) {
                                    const auto& d = g_in.datas[a / 1000];
                                    const int level = static_cast<int>(a / 10 % 100), v = static_cast<int>(a % 10);
                                    const int wbits[] = {15, -15, 9, 12, -11};
                                    const int mem[] = {8, 9, 1, 4};
                                    run_deflate(l, d, level, wbits[(level + v) % 5], mem[(level + 2 * v) % 4], v % 3,
                                                1u << 30, 1u << 30, 0);
                                    if (d.size() < 80000) {
                                        run_deflate(l, d, level, wbits[(level + v + 1) % 5], mem[(level + v) % 4],
                                                    (v + 1) % 3, 997, 500, 3);
                                        run_deflate(l, d, level, 15, 8, 0, 5000, 61, 7);
                                    }
                                    if (v == 0) run_compress2(l, d, level);
                                },
                                a});
            }
    for (int z = 0; z < 6; z++)
        g_sc.push_back({"zip#" + std::to_string(z), [](Log& l, size_t a) {
                            char name[32];
                            std::snprintf(name, sizeof name, "test%zu.zip", a);
                            run_zip(l, name);
                        },
                        static_cast<size_t>(z)});
    g_sc.push_back({"zip#missing", [](Log& l, size_t) { run_zip(l, "missing.zip"); }, 0});
}

struct Result {
    std::vector<uint32_t> log;
    uint32_t arena_hash;
    size_t arena_used;
    std::vector<uint8_t> arena;
};
__attribute__((noinline)) void zero_dead_stack() {
    volatile uint8_t pad[256 * 1024];
    std::memset(const_cast<uint8_t*>(pad), 0, sizeof pad);
}
Result run_one(const Scenario& sc) {
    g_fill = (&sc - g_sc.data()) % 2 ? 0x02 : 0;  // 2: deflate's data_type starts as Z_UNKNOWN
    reset_heap();
    Log log;
    log.v.reserve(4u << 20);
    zero_dead_stack();
    sc.fn(log, sc.arg);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < g_used; i++) h = (h ^ g_arena[i]) * 16777619u;
    Result r{log.v, h, g_used, {}};
    if (std::getenv("DIFF")) r.arena.assign(g_arena, g_arena + g_used);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: zlib_e2e default.xbe input-dir [function|all ...]\n");
        return 2;
    }
    g_dir = argv[2];
    if (!map_image(argv[1])) return std::perror("map"), 1;
    g_arena = static_cast<uint8_t*>(mmap(nullptr, kArena, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    for (const auto& f : game::functions())
        if (f.campaign >= 0x00056b80 && f.campaign < 0x0005ddb0) {
            Mod m{f.name, f.campaign, f.code, {}};
            std::memcpy(m.saved, reinterpret_cast<void*>(static_cast<uintptr_t>(f.campaign)), 5);
            g_mod.push_back(m);
        }
    // inputs
    std::vector<uint8_t> d = read_file((g_dir + "/data.bin").c_str());
    size_t p = 4;
    for (uint32_t i = 0, n = rd32(&d[0]); i < n; i++) {
        const uint32_t len = rd32(&d[p]);
        g_in.datas.emplace_back(d.begin() + static_cast<long>(p + 4), d.begin() + static_cast<long>(p + 4 + len));
        p += 4 + len;
    }
    d = read_file((g_dir + "/inflate.bin").c_str());
    p = 4;
    for (uint32_t i = 0, n = rd32(&d[0]); i < n; i++) {
        Inputs::Case c;
        c.wbits = static_cast<int32_t>(rd32(&d[p]));
        const uint32_t rl = rd32(&d[p + 4]), cl = rd32(&d[p + 8]);
        p += 12;
        c.raw.assign(d.begin() + static_cast<long>(p), d.begin() + static_cast<long>(p + rl));
        p += rl;
        c.comp.assign(d.begin() + static_cast<long>(p), d.begin() + static_cast<long>(p + cl));
        p += cl;
        g_in.cases.push_back(std::move(c));
    }
    add_scenarios();
    const char* filter = std::getenv("SC");  // scenario label prefix
    if (std::getenv("DUMP")) {
        set_mode(-1);
        for (const auto& sc : g_sc) {
            if (filter && sc.label.rfind(filter, 0)) continue;
            const Result r = run_one(sc);
            std::printf("%s used=%zu log:", sc.label.c_str(), r.arena_used);
            for (size_t w = 0; w < r.log.size() && w < 40; w++) std::printf(" %x", r.log[w]);
            std::printf(" ... (%zu)\n", r.log.size());
        }
        return 0;
    }

    set_mode(-1);
    std::vector<Result> base;
    for (const auto& sc : g_sc) base.push_back(filter && sc.label.rfind(filter, 0) ? Result{} : run_one(sc));

    if (std::getenv("COUNT")) {
        // Every function ours, each behind a thunk that counts its calls.
        uint8_t* thunks = static_cast<uint8_t*>(
            mmap(nullptr, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        static uint32_t counts[256];
        const bool each = std::getenv("COUNT")[0] == 'e';
        for (size_t only = 0; each && only < g_mod.size(); only++) {
            set_mode(-1);
            uint8_t* t = thunks + only * 16;
            const uint32_t c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&counts[only]));
            t[0] = 0xff, t[1] = 0x05;
            std::memcpy(t + 2, &c, 4);
            const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(g_mod[only].code) -
                                                     (reinterpret_cast<uintptr_t>(t) + 11));
            t[6] = 0xe9;
            std::memcpy(t + 7, &rel, 4);
            patch_jmp(g_mod[only].address, t);
            for (const auto& sc : g_sc) run_one(sc);
        }
        for (size_t i = 0; !each && i < g_mod.size(); i++) {
            uint8_t* t = thunks + i * 16;
            const uint32_t c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&counts[i]));
            t[0] = 0xff, t[1] = 0x05;
            std::memcpy(t + 2, &c, 4);
            const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(g_mod[i].code) -
                                                     (reinterpret_cast<uintptr_t>(t) + 11));
            t[6] = 0xe9;
            std::memcpy(t + 7, &rel, 4);
            patch_jmp(g_mod[i].address, t);
        }
        if (!each)
            for (const auto& sc : g_sc) run_one(sc);
        for (size_t i = 0; i < g_mod.size(); i++) std::printf("%-42s %u\n", g_mod[i].name, counts[i]);
        return 0;
    }
    std::vector<int> modes;
    for (int a = 3; a < argc; a++) {
        if (!std::strcmp(argv[a], "all")) {
            modes.push_back(-2);
            continue;
        }
        if (!std::strcmp(argv[a], "each")) {
            for (size_t i = 0; i < g_mod.size(); i++) modes.push_back(static_cast<int>(i));
            continue;
        }
        for (size_t i = 0; i < g_mod.size(); i++)
            if (!std::strcmp(g_mod[i].name, argv[a])) modes.push_back(static_cast<int>(i));
    }
    int bad_total = 0;
    for (int m : modes) {
        set_mode(m);
        int bad = 0, log_bad = 0;
        std::string first;
        for (size_t i = 0; i < g_sc.size(); i++) {
            if (filter && g_sc[i].label.rfind(filter, 0)) continue;
            const Result r = run_one(g_sc[i]);
            const bool lb = r.log != base[i].log;
            if (lb || r.arena_hash != base[i].arena_hash || r.arena_used != base[i].arena_used) {
                if (!bad) {
                    first = g_sc[i].label + (lb ? " (results)" : " (heap only)");
                    if (lb)
                        for (size_t w = 0; w < std::max(r.log.size(), base[i].log.size()); w++)
                            if (w >= r.log.size() || w >= base[i].log.size() || r.log[w] != base[i].log[w]) {
                                char t[96];
                                std::snprintf(t, sizeof t, " word %zu: %08x vs %08x", w,
                                              w < base[i].log.size() ? base[i].log[w] : 0, w < r.log.size() ? r.log[w] : 0);
                                first += t;
                                break;
                            }
                }
                if (std::getenv("DIFF")) {
                    size_t npad = 0, nother = 0;
                    for (size_t o = 0; o < std::min(r.arena.size(), base[i].arena.size()); o++)
                        if (r.arena[o] != base[i].arena[o]) ((o % 8 == 2 || o % 8 == 3) ? npad : nother)++;
                    if (nother) {
                        std::printf("  %s: %zu other bytes:", g_sc[i].label.c_str(), nother);
                        for (size_t o = 0, k = 0; o < r.arena.size() && k < 4; o++)
                            if (r.arena[o] != base[i].arena[o] && !(o % 8 == 2 || o % 8 == 3))
                                std::printf(" +%zx %02x/%02x", o, base[i].arena[o], r.arena[o]), k++;
                        std::printf("\n");
                    }
                }
                if (false) {
                    int shown = 0;
                    size_t npad = 0, nother = 0;
                    for (size_t o = 0; o < std::min(r.arena.size(), base[i].arena.size()); o++)
                        if (r.arena[o] != base[i].arena[o]) ((o % 8 == 2 || o % 8 == 3) ? npad : nother)++;
                    std::printf("  %zu bytes differ at +2/+3 mod 8, %zu elsewhere\n", npad, nother);
                    for (size_t o = 0; o < std::min(r.arena.size(), base[i].arena.size()) && shown < 12; o++)
                        if (r.arena[o] != base[i].arena[o] && !(o % 8 == 2 || o % 8 == 3)) {
                            std::printf("  arena+%06zx: %02x vs %02x\n", o, base[i].arena[o], r.arena[o]);
                            shown++;
                        }
                }
                bad++;
                log_bad += lb;
            }
        }
        std::printf("%-42s %s", m == -2 ? "all" : g_mod[static_cast<size_t>(m)].name,
                    bad ? "MISMATCH" : "ok");
        if (bad) std::printf(" %d scenarios (%d results), first %s", bad, log_bad, first.c_str());
        std::printf("\n");
        std::fflush(stdout);
        bad_total += bad;
    }
    set_mode(-1);
    std::printf("%zu scenarios, %zu functions\n", g_sc.size(), g_mod.size());
    return bad_total ? 1 : 0;
}
