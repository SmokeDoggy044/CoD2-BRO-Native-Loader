// --check-crt part 2: our XAPI functions (title_xapi.cpp) against the title's on tables of inputs. Most of them are
// reached only by save games and the save list's dates, which no recording covers.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "title_crt.h"
#include "title_data.h"
#include "title_savegame.h"
#include "title_stdio.h"
#include "title_host.h"
#include "title_xapi.h"

namespace game {

namespace {

void add_line(std::string& report, const char* format, ...) {
    char line[1400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    report += line;
    report += '\n';
}

// xorshift, the same sequence every run.
struct Random {
    uint32_t state = 0x2545f491;
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

}  // namespace

int32_t check_title_xapi(std::string& report) {
    int32_t failures = 0;
    Random random;
    // XB_CHECK_NATIVE_FILES=1: ours on the native platform layer (--own-main: our files, settings, clock,
    // calendar, string conversion), the title's on the kernel.
    const bool native = std::getenv("XB_CHECK_NATIVE_FILES") != nullptr;
    set_native_platform(native);

    // Threads
    if (title_function<uint32_t XB_STDCALL()>(0x001e7904)() != GetCurrentThreadId()) {
        ++failures;
        add_line(report, "GetCurrentThreadId differs");
    }

    // File times <-> system times
    {
        const auto orig_to_system = title_function<uint32_t XB_STDCALL(const uint32_t*, uint16_t*)>(0x001e7062);
        const auto orig_to_file = title_function<uint32_t XB_STDCALL(const uint16_t*, uint32_t*)>(0x001e70cd);
        uint32_t differences = 0;
        for (int i = 0; i < 400; ++i) {
            uint32_t time[2] = {random.next(), random.below(0x02000000)};
            if (i == 0) time[0] = time[1] = 0;
            uint16_t a[8], b[8];
            std::memset(a, 0xcc, sizeof a);
            std::memset(b, 0xcc, sizeof b);
            const uint32_t ra = orig_to_system(time, a);
            const uint32_t rb = FileTimeToSystemTime(time, b);
            if (ra != rb || std::memcmp(a, b, sizeof a) != 0) {
                if (++differences <= 5)
                    add_line(report, "FileTimeToSystemTime %08x%08x: title %u %u-%u-%u, ours %u %u-%u-%u", time[1],
                             time[0], ra, a[0], a[1], a[3], rb, b[0], b[1], b[3]);
            }
            // Back again, and a damaged copy (fields out of range, weekday ignored).
            uint16_t st[8];
            std::memcpy(st, a, sizeof st);
            if (i % 3 == 1) st[random.below(8)] = static_cast<uint16_t>(random.below(70));
            uint32_t fa[2] = {0xcccccccc, 0xcccccccc}, fb[2] = {0xcccccccc, 0xcccccccc};
            SetLastError(0);
            const uint32_t sa = orig_to_file(st, fa);
            const uint32_t ea = GetLastError();
            SetLastError(0);
            const uint32_t sb = SystemTimeToFileTime(st, fb);
            const uint32_t eb = GetLastError();
            if (sa != sb || ea != eb || std::memcmp(fa, fb, sizeof fa) != 0) {
                if (++differences <= 5)
                    add_line(report, "SystemTimeToFileTime %u-%u-%u %u:%u: title %u (%u) %08x%08x, ours %u (%u) %08x%08x",
                             st[0], st[1], st[3], st[4], st[5], sa, ea, fa[1], fa[0], sb, eb, fb[1], fb[0]);
            }
        }
        failures += static_cast<int32_t>(differences);
        add_line(report, "file/system times: 400 times both ways, %u difference(s)", differences);
    }

    // Settings and the time zone
    {
        const auto orig_query = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t*, void*, uint32_t, uint32_t*)>(0x001ed6ef);
        uint32_t differences = 0;
        for (uint32_t index : {0x0u, 0x1u, 0x2u, 0x3u, 0x4u, 0x7u, 0x8u, 0x9u, 0xau, 0xbu, 0x10u, 0x11u, 0x100u, 0x101u,
                               0x103u, 0x104u, 0xffu, 0xfffu}) {
            for (uint32_t size : {4u, 0x100u}) {
                uint8_t a[0x100], b[0x100];
                std::memset(a, 0xcc, sizeof a);
                std::memset(b, 0xcc, sizeof b);
                uint32_t ta = 0x77, tb = 0x77, la = 0x77, lb = 0x77;
                const uint32_t ra = orig_query(index, &ta, a, size, &la);
                const uint32_t rb = XQueryValue(index, &tb, b, size, &lb);
                if (ra != rb || ta != tb || la != lb || std::memcmp(a, b, sizeof a) != 0) {
                    ++differences;
                    add_line(report, "XQueryValue %x size %x: title %u type %u len %u, ours %u type %u len %u", index,
                             size, ra, ta, la, rb, tb, lb);
                }
            }
        }
        const auto orig_tz_query = title_function<uint32_t XB_STDCALL(uint8_t*, uint32_t*)>(0x001e6fb0);
        const auto orig_tz = title_function<uint32_t XB_STDCALL(uint8_t*)>(0x001e7383);
        const auto orig_bias = title_function<void XB_STDCALL(uint32_t*)>(0x001e74df);
        const auto orig_local = title_function<uint32_t XB_STDCALL(const uint32_t*, uint32_t*)>(0x001e75b4);
        {
            uint8_t a[0xac], b[0xac];
            std::memset(a, 0xcc, sizeof a);
            std::memset(b, 0xcc, sizeof b);
            uint32_t fa = 7, fb = 7;
            const uint32_t ra = orig_tz_query(a, &fa);
            const uint32_t rb = XapipQueryTimeZoneInformation(b, &fb);
            if (ra != rb || fa != fb || std::memcmp(a, b, sizeof a) != 0) {
                ++differences;
                add_line(report, "XapipQueryTimeZoneInformation: title %u flag %u, ours %u flag %u", ra, fa, rb, fb);
            }
            std::memset(a, 0xcc, sizeof a);
            std::memset(b, 0xcc, sizeof b);
            const uint32_t za = orig_tz(a);
            const uint32_t zb = GetTimeZoneInformation(b);
            if (za != zb || std::memcmp(a, b, sizeof a) != 0) {
                ++differences;
                add_line(report, "GetTimeZoneInformation: title %u, ours %u", za, zb);
            }
            add_line(report, "time zone: bias %d min, standard %u/%u, daylight %u/%u, id %u", *reinterpret_cast<int32_t*>(b),
                     *reinterpret_cast<uint16_t*>(b + 0x46), *reinterpret_cast<uint16_t*>(b + 0x4a),
                     *reinterpret_cast<uint16_t*>(b + 0x9a), *reinterpret_cast<uint16_t*>(b + 0x9e), zb);
            uint32_t ba[2] = {1, 2}, bb[2] = {1, 2};
            orig_bias(ba);
            XapipGetTimeZoneBias(bb);
            if (ba[0] != bb[0] || ba[1] != bb[1]) {
                ++differences;
                add_line(report, "XapipGetTimeZoneBias: title %08x%08x, ours %08x%08x", ba[1], ba[0], bb[1], bb[0]);
            }
            for (int i = 0; i < 20; ++i) {
                const uint32_t time[2] = {random.next(), random.below(0x02000000)};
                uint32_t la[2] = {0, 0}, lb[2] = {0, 0};
                const uint32_t ra2 = orig_local(time, la);
                const uint32_t rb2 = FileTimeToLocalFileTime(time, lb);
                if (ra2 != rb2 || la[0] != lb[0] || la[1] != lb[1]) {
                    ++differences;
                    add_line(report, "FileTimeToLocalFileTime differs");
                }
            }
        }
        // The cutover dates on their own (the console's settings usually have no daylight dates).
        const auto orig_cutover =
            title_function<uint8_t XB_STDCALL(const uint16_t*, uint32_t*, const uint32_t*, uint8_t)>(0x001e7179);
        uint32_t cutover_differences = 0;
        for (int i = 0; i < 3000; ++i) {
            uint16_t cut[8] = {};
            if (i % 10 == 0) {
                cut[0] = static_cast<uint16_t>(1995 + random.below(30));
                cut[3] = static_cast<uint16_t>(1 + random.below(31));
            } else {
                cut[3] = static_cast<uint16_t>(random.below(8));  // week, 0 and 6..7 invalid
            }
            cut[1] = static_cast<uint16_t>(random.below(14));
            cut[2] = static_cast<uint16_t>(random.below(8));
            cut[4] = static_cast<uint16_t>(random.below(25));
            cut[5] = static_cast<uint16_t>(random.below(60));
            cut[6] = static_cast<uint16_t>(random.below(60));
            cut[7] = static_cast<uint16_t>(random.below(1000));
            // now: 1995..2035, sometimes on the cutover's own month and day
            uint16_t now_fields[8];
            uint32_t now[2] = {random.next(), 0x01b00000 + random.below(0x00e00000)};
            FileTimeToSystemTime(now, now_fields);
            if (i % 4 == 0 && cut[1] >= 1 && cut[1] <= 12) {
                now_fields[1] = cut[1];
                now_fields[3] = static_cast<uint16_t>(1 + random.below(28));
                now_fields[4] = cut[4] < 24 ? cut[4] : 0;
                SystemTimeToFileTime(now_fields, now);
            }
            const uint8_t this_year = static_cast<uint8_t>(random.below(2));
            uint32_t a[2] = {0xcccccccc, 0xcccccccc}, b[2] = {0xcccccccc, 0xcccccccc};
            const uint8_t ra = orig_cutover(cut, a, now, this_year);
            const uint8_t rb = RtlpCutoverTimeToSystemTime(cut, b, now, this_year);
            if (ra != rb || a[0] != b[0] || a[1] != b[1]) {
                if (++cutover_differences <= 5)
                    add_line(report,
                             "RtlpCutoverTimeToSystemTime %u-%u dow %u week %u %u:%u now %08x%08x this_year %u: title %u "
                             "%08x%08x, ours %u %08x%08x",
                             cut[0], cut[1], cut[2], cut[3], cut[4], cut[5], now[1], now[0], this_year, ra, a[1], a[0],
                             rb, b[1], b[0]);
            }
        }
        differences += cutover_differences;
        failures += static_cast<int32_t>(differences);
        add_line(report, "settings + time zone: 36 queries, 3000 cutover dates, %u difference(s)", differences);
    }

    // Disk sizes
    {
        const auto orig_sector = title_function<uint32_t XB_STDCALL(const char*)>(0x001e9d67);
        const auto orig_cluster = title_function<uint32_t XB_STDCALL(const char*)>(0x001e9cc8);
        const auto orig_free = title_function<uint32_t XB_STDCALL(const char*, uint32_t*, uint32_t*, uint32_t*)>(0x001e9827);
        uint32_t differences = 0;
        for (const char* path : {"T:\\", "U:\\", "D:\\", "Z:\\", "T:\\no_such_dir\\", "Q:\\", "E:\\", "D:\\nope\\x\\"}) {
            set_native_platform(false);  // the title's on the kernel (its code reaches our C rows too)
            SetLastError(0);
            const uint32_t sa = orig_sector(path);
            const uint32_t ea = GetLastError();
            const uint32_t ca = orig_cluster(path);
            uint32_t fa[6] = {1, 2, 3, 4, 5, 6}, fb[6] = {1, 2, 3, 4, 5, 6};
            const uint32_t ra = orig_free(path, fa, fa + 2, fa + 4);
            const uint32_t na = orig_free(path, nullptr, nullptr, nullptr);
            set_native_platform(native);
            SetLastError(0);
            const uint32_t sb = XGetDiskSectorSizeA(path);
            const uint32_t eb = GetLastError();
            const uint32_t cb = XGetDiskClusterSizeA(path);
            const uint32_t rb = GetDiskFreeSpaceExA(path, fb, fb + 2, fb + 4);
            const uint32_t nb = GetDiskFreeSpaceExA(path, nullptr, nullptr, nullptr);
            set_native_platform(false);
            if (sa != sb || ea != eb || ca != cb || ra != rb || na != nb || std::memcmp(fa, fb, sizeof fa) != 0) {
                ++differences;
                add_line(report, "disk %s: title sector %u (%u) cluster %u free %u, ours sector %u (%u) cluster %u free %u",
                         path, sa, ea, ca, ra, sb, eb, cb, rb);
            }
        }
        set_native_platform(native);
        failures += static_cast<int32_t>(differences);
        add_line(report, "disk sizes: 8 paths, %u difference(s)", differences);
    }

    // DeleteFileA (a Replaced row: the title's address is ours too, so this checks behaviour only)
    {
        uint32_t differences = 0;
        TitleFile* f = title_fopen("T:\\xapicheck.bin", "wb");
        if (f) {
            title_fwrite("x", 1, 1, f);
            title_fclose(f);
        }
        const uint32_t first = DeleteFileA("T:\\xapicheck.bin");
        SetLastError(0);
        const uint32_t second = DeleteFileA("T:\\xapicheck.bin");
        const uint32_t error = GetLastError();
        if (!f || first != 1 || second != 0 || error != 2) {
            ++differences;
            add_line(report, "DeleteFileA: created %d, first %u, second %u error %u", f != nullptr, first, second, error);
        }
        failures += static_cast<int32_t>(differences);
    }

    // UTF-8 and ANSI to UTF-16
    {
        const auto orig_utf8 = title_function<int32_t XB_STDCALL(const char*, int32_t, uint16_t*, int32_t)>(0x001e9496);
        const auto orig_mb =
            title_function<int32_t XB_STDCALL(uint32_t, uint32_t, const char*, int32_t, uint16_t*, int32_t)>(0x001e96a4);
        uint32_t differences = 0;
        static const char* const fixed[] = {"", "abc", "\xc3\xa9t\xc3\xa9", "\xe2\x82\xac 5", "\xf0\x9f\x98\x80!",
                                            "\xf8\x88\x80\x80\x80", "\xfc\x84\x80\x80\x80\x80", "\xc3", "\xe2\x82",
                                            "\xff\xfe", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
        for (int i = 0; i < 4000; ++i) {
            char text[40];
            int32_t n;
            if (i < static_cast<int>(sizeof fixed / sizeof *fixed) * 8) {
                const char* s = fixed[i / 8];
                n = static_cast<int32_t>(std::strlen(s));
                std::memcpy(text, s, static_cast<size_t>(n) + 1);
            } else {
                n = static_cast<int32_t>(random.below(24));
                for (int32_t k = 0; k < n; ++k) {
                    const uint32_t r = random.below(10);
                    text[k] = static_cast<char>(r < 4 ? 0x20 + random.below(0x5f) : 0x80 + random.below(0x80));
                }
                text[n] = 0;
            }
            const uint32_t pick = random.below(6);
            const int32_t length = pick == 0 ? -1 : pick == 1 ? n : static_cast<int32_t>(random.below(static_cast<uint32_t>(n) + 2));
            const int32_t out_size = random.below(4) == 0 ? 0 : static_cast<int32_t>(random.below(30));
            uint16_t a[64], b[64];
            std::memset(a, 0xcc, sizeof a);
            std::memset(b, 0xcc, sizeof b);
            const uint32_t code_page = (i & 3) == 0 ? 0u : (i & 3) == 1 ? 0xfde9u : (i & 3) == 2 ? 0xfde9u : 0xfde8u;
            SetLastError(0);
            const int32_t ra = (i & 1) ? orig_utf8(text, length, a, out_size) : orig_mb(code_page, 0, text, length, a, out_size);
            const uint32_t ea = GetLastError();
            SetLastError(0);
            const int32_t rb = (i & 1) ? UTF8ToUnicode(text, length, b, out_size)
                                       : MultiByteToWideChar(code_page, 0, text, length, b, out_size);
            const uint32_t eb = GetLastError();
            if (ra != rb || ea != eb || std::memcmp(a, b, sizeof a) != 0) {
                if (++differences <= 5)
                    add_line(report, "%s cp %x \"%s\" length %d out %d: title %d (%u), ours %d (%u)",
                             (i & 1) ? "UTF8ToUnicode" : "MultiByteToWideChar", code_page, text, length, out_size, ra,
                             ea, rb, eb);
            }
        }
        failures += static_cast<int32_t>(differences);
        add_line(report, "UTF8ToUnicode/MultiByteToWideChar: 4000 strings, %u difference(s)", differences);
    }
    set_native_platform(false);
    failures += check_title_savegame(report);
    return failures;
}

}  // namespace game
