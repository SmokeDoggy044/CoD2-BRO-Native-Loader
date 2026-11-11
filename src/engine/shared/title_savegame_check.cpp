// --check-crt part 3: the save-game functions (title_savegame.cpp) against the title's. Pure functions on tables of
// inputs; the file-writing ones (Replaced rows: their original address reaches ours in every run) on the same
// operations in a scratch root, against the files and names the save format prescribes.
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "title_crt.h"
#include "title_data.h"
#include "title_savegame.h"
#include "title_stdio.h"
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

struct Random {
    uint32_t state = 0x6b43a9b5;
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// Zeros the stack below the caller, so the title's uninitialized locals (XapiFindValueInMetaFile's window) start
// as zeros like ours.
PLATFORM_NOINLINE void scrub_stack() {
    volatile uint8_t area[0x3000];
    for (uint32_t i = 0; i < sizeof area; ++i) area[i] = 0;
}

std::string narrow(const uint16_t* text) {
    std::string s;
    for (; *text; ++text) s += *text < 0x80 ? static_cast<char>(*text) : '?';
    return s;
}

void write_file(const char* path, const std::vector<uint8_t>& bytes) {
    TitleFile* f = title_fopen(path, "wb");
    if (!f) return;
    if (!bytes.empty()) title_fwrite(bytes.data(), 1, static_cast<uint32_t>(bytes.size()), f);
    title_fclose(f);
}

// The file's bytes, or "missing" (empty with ok false).
std::vector<uint8_t> read_file(const char* path, bool* ok) {
    std::vector<uint8_t> bytes;
    TitleFile* f = title_fopen(path, "rb");
    *ok = f != nullptr;
    if (!f) return bytes;
    uint8_t chunk[256];
    for (uint32_t n; (n = title_fread(chunk, 1, sizeof chunk, f)) != 0;) bytes.insert(bytes.end(), chunk, chunk + n);
    title_fclose(f);
    return bytes;
}

bool exists(const char* path) {
    uint8_t find[0x140];
    const uint32_t handle = FindFirstFileA(path, find);
    if (handle == 0xffffffff) return false;
    CloseHandle(handle);
    return true;
}

// UTF-16LE bytes of a meta file: optional byte order mark, then the text.
std::vector<uint8_t> meta_bytes(bool bom, const std::u16string& text) {
    std::vector<uint8_t> bytes;
    if (bom) bytes.insert(bytes.end(), {0xff, 0xfe});
    for (char16_t c : text) {
        bytes.push_back(static_cast<uint8_t>(c));
        bytes.push_back(static_cast<uint8_t>(c >> 8));
    }
    return bytes;
}

// What one save operation leaves behind: return value, path out, the meta file's bytes and whether the
// directory exists.
struct SaveState {
    uint32_t result = 0;
    uint32_t error = 0;
    char path[0x104] = {};
    bool meta_ok = false;
    std::vector<uint8_t> meta;
    bool directory = false;
};

}  // namespace

int32_t check_title_savegame(std::string& report) {
    int32_t failures = 0;
    Random random;

    // Hash names
    {
        const auto orig_hash = title_function<void XB_STDCALL(const uint16_t*, char*, uint32_t)>(0x001e8b82);
        const auto orig_digit = title_function<uint32_t XB_STDCALL(int32_t)>(0x001e8b6e);
        uint32_t differences = 0;
        for (int32_t d = -20; d < 40; ++d)
            if (orig_digit(d) != HexDigitToChar(d)) ++differences;
        for (int i = 0; i < 3000; ++i) {
            uint16_t name[64];
            const uint32_t n = random.below(48);
            for (uint32_t k = 0; k < n; ++k) {
                const uint32_t r = random.below(4);
                name[k] = static_cast<uint16_t>(r == 0 ? 1 + random.below(0xffff) : r == 1 ? 0xff00 + random.below(0x100)
                                                                                           : 0x20 + random.below(0x5f));
            }
            name[n] = 0;
            char a[24], b[24];
            std::memset(a, 0xcc, sizeof a);
            std::memset(b, 0xcc, sizeof b);
            orig_hash(name, a + 2, 0xf0);
            XapiSaveGameHashEncode(name, b + 2, 0xf0);
            if (std::memcmp(a, b, sizeof a) != 0 && ++differences <= 5)
                add_line(report, "XapiSaveGameHashEncode \"%s\": title %.12s, ours %.12s", narrow(name).c_str(), a + 2,
                         b + 2);
        }
        failures += static_cast<int32_t>(differences);
        add_line(report, "save name hashes: 3000 names, 60 digits, %u difference(s)", differences);
    }

    // Signatures: HMAC pieces, Begin/Update/End of every type, roamable or not, this title or another
    {
        const auto orig_init = title_function<void XB_STDCALL(const uint8_t*, uint32_t, uint8_t*)>(0x001e87bb);
        const auto orig_final = title_function<void XB_STDCALL(uint8_t*, const uint8_t*, uint32_t, uint8_t*)>(0x001e8822);
        const auto orig_convert =
            title_function<uint32_t XB_STDCALL(uint8_t*, const uint8_t*, uint8_t*)>(0x001e8894);
        const auto orig_begin_ex = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x001e8a86);
        const auto orig_begin = title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e8ac7);
        const auto orig_update = title_function<uint32_t XB_STDCALL(uint32_t, const uint8_t*, uint32_t)>(0x001e8adb);
        const auto orig_end = title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*)>(0x001e8b29);
        const uint32_t title_id = title_global<uint32_t>(title_global<uint32_t>(0x00010118) + 8);
        uint32_t differences = 0;
        uint8_t data[600];
        for (int i = 0; i < 200; ++i) {
            for (uint8_t& b : data) b = static_cast<uint8_t>(random.next());
            const uint32_t key_length = random.below(90);
            uint8_t sa[0x74], sb[0x74], da[0x14], db[0x14];
            orig_init(data, key_length, sa);
            XShaHmacInitialize(data, key_length, sb);
            orig_final(sa, data + 100, key_length, da);
            XShaHmacComputeFinal(sb, data + 100, key_length, db);
            if (std::memcmp(da, db, sizeof da) != 0 && ++differences <= 5)
                add_line(report, "XShaHmac key %u differs", key_length);
            uint8_t ca[0x7c] = {}, cb[0x7c] = {}, oa[0x14], ob[0x14];
            const uint32_t ra = orig_convert(ca, data + 200, oa), rb = XapiConvertSignatureToNonRoamable(cb, data + 200, ob);
            if ((ra != rb || std::memcmp(oa, ob, sizeof oa) != 0) && ++differences <= 5)
                add_line(report, "XapiConvertSignatureToNonRoamable differs");
        }
        for (int i = 0; i < 600; ++i) {
            for (uint8_t& b : data) b = static_cast<uint8_t>(random.next());
            static const uint32_t kFlags[] = {0, 1, 2, 4, 3, 5, 0x10};
            const uint32_t flags = kFlags[random.below(7)];
            const uint32_t id = random.below(5) == 0 ? random.next() : title_id;
            const bool plain = random.below(3) == 0;
            SetLastError(0);
            const uint32_t ca = plain ? orig_begin(flags) : orig_begin_ex(flags, id);
            const uint32_t ea = GetLastError();
            SetLastError(0);
            const uint32_t cb = plain ? XCalculateSignatureBegin(flags) : XCalculateSignatureBeginEx(flags, id);
            const uint32_t eb = GetLastError();
            if ((ca == 0xffffffff) != (cb == 0xffffffff) || ea != eb) {
                if (++differences <= 5)
                    add_line(report, "XCalculateSignatureBegin flags %u id %08x: title %08x (%u), ours %08x (%u)", flags,
                             id, ca, ea, cb, eb);
                continue;
            }
            if (ca == 0xffffffff) continue;
            if (std::memcmp(reinterpret_cast<const void*>(static_cast<uintptr_t>(ca)),
                            reinterpret_cast<const void*>(static_cast<uintptr_t>(cb)), 0x7c) != 0 &&
                ++differences <= 5)
                add_line(report, "XCalculateSignatureBegin flags %u: contexts differ", flags);
            uint32_t ua = 0, ub = 0;
            for (uint32_t offset = 0, pieces = random.below(5); pieces; --pieces) {
                const uint32_t size = random.below(200);
                ua |= orig_update(ca, data + offset, size);
                ub |= XCalculateSignatureUpdate(cb, data + offset, size);
                offset = (offset + size) % 300;
            }
            uint8_t sa[0x20], sb[0x20];
            std::memset(sa, 0x5a, sizeof sa);
            std::memset(sb, 0x5a, sizeof sb);
            const bool none = random.below(8) == 0;
            const uint32_t ra = orig_end(ca, none ? nullptr : sa), rb = XCalculateSignatureEnd(cb, none ? nullptr : sb);
            if (ua != ub || ra != rb || std::memcmp(sa, sb, sizeof sa) != 0) {
                if (++differences <= 5)
                    add_line(report, "XCalculateSignature flags %u: title %u/%u, ours %u/%u, signatures %s", flags, ua, ra,
                             ub, rb, std::memcmp(sa, sb, sizeof sa) ? "differ" : "equal");
            }
        }
        // Bad context types: Update/End return 0x32 without touching anything.
        uint32_t bad[0x20] = {3};
        if (orig_update(reinterpret_cast<uintptr_t>(bad), data, 4) != XCalculateSignatureUpdate(reinterpret_cast<uintptr_t>(bad), data, 4) ||
            orig_end(reinterpret_cast<uintptr_t>(bad), data) != XCalculateSignatureEnd(reinterpret_cast<uintptr_t>(bad), data))
            ++differences;
        failures += static_cast<int32_t>(differences);
        add_line(report, "save signatures: 200 HMAC keys, 600 signatures, %u difference(s)", differences);
    }

    // UTF-16 to UTF-8 / ANSI
    {
        const auto orig_utf8 = title_function<int32_t XB_STDCALL(const uint16_t*, int32_t, char*, int32_t)>(0x001e95ec);
        const auto orig_wc = title_function<int32_t XB_STDCALL(uint32_t, uint32_t, const uint16_t*, int32_t, char*,
                                                               int32_t, const char*, uint32_t*)>(0x001e975f);
        uint32_t differences = 0;
        for (int i = 0; i < 4000; ++i) {
            uint16_t text[40];
            const uint32_t n = random.below(24);
            for (uint32_t k = 0; k < n; ++k) {
                const uint32_t r = random.below(6);
                text[k] = static_cast<uint16_t>(r < 2 ? 0x20 + random.below(0x5f)
                                                : r == 2 ? 0x80 + random.below(0x780)
                                                : r == 3 ? 0xd800 + random.below(0x800)
                                                : r == 4 ? 0x80 + random.below(0x80)
                                                         : 1 + random.below(0xffff));
            }
            text[n] = 0;
            const uint32_t pick = random.below(5);
            const int32_t length = pick == 0 ? -1 : pick == 1 ? static_cast<int32_t>(n) : pick == 2 ? static_cast<int32_t>(n + 1)
                                                                                            : static_cast<int32_t>(random.below(n + 2));
            const int32_t out_size = random.below(4) == 0 ? 0 : static_cast<int32_t>(random.below(80));
            static const uint32_t kPages[] = {0, 0xfde9, 0xfde8, 1252, 0xfde9, 0};
            const uint32_t code_page = kPages[random.below(6)];
            const char* const default_char = random.below(10) == 0 ? "?" : nullptr;
            const bool use_flag = random.below(2) != 0;
            const bool no_out = random.below(10) == 0;
            char a[100], b[100];
            std::memset(a, 0xcc, sizeof a);
            std::memset(b, 0xcc, sizeof b);
            uint32_t fa = 7, fb = 7;
            SetLastError(0);
            const int32_t ra = (i & 1) ? orig_utf8(text, length, no_out ? nullptr : a, out_size)
                                       : orig_wc(code_page, 0, text, length, a, out_size, default_char, use_flag ? &fa : nullptr);
            const uint32_t ea = GetLastError();
            SetLastError(0);
            const int32_t rb = (i & 1) ? UnicodeToUTF8(text, length, no_out ? nullptr : b, out_size)
                                       : WideCharToMultiByte(code_page, 0, text, length, b, out_size, default_char,
                                                             use_flag ? &fb : nullptr);
            const uint32_t eb = GetLastError();
            if (ra != rb || ea != eb || fa != fb || std::memcmp(a, b, sizeof a) != 0) {
                if (++differences <= 5)
                    add_line(report, "%s cp %x length %d (%u) out %d: title %d (%u), ours %d (%u)",
                             (i & 1) ? "UnicodeToUTF8" : "WideCharToMultiByte", code_page, length, n, out_size, ra, ea,
                             rb, eb);
            }
        }
        failures += static_cast<int32_t>(differences);
        add_line(report, "UnicodeToUTF8/WideCharToMultiByte: 4000 strings, %u difference(s)", differences);
    }

    // Meta files: the signature check and the value search on files with and without the byte order mark, long
    // lines (the window's skip), keys missing or without '='.
    const char* const scratch = "T:\\svchk";
    {
        CreateDirectoryA(scratch, 0);
        const auto orig_validate = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x001e8c14);
        const auto orig_find_value =
            title_function<uint32_t XB_STDCALL(uint32_t, const uint16_t*, uint16_t*, int32_t)>(0x001e8c5c);
        const auto orig_verify = title_function<uint32_t XB_STDCALL(const char*, const uint16_t*)>(0x001e8ee0);
        std::vector<std::vector<uint8_t>> files;
        files.push_back(meta_bytes(true, u"Name=Profiles\r\n"));
        files.push_back(meta_bytes(true, u"Name=Profiles\r\nNoCopy=1\r\n"));
        files.push_back(meta_bytes(false, u"Name=Profiles\r\n"));
        files.push_back(meta_bytes(true, u"Name=Other\r\n"));
        files.push_back(meta_bytes(true, u"Name=Profiles"));
        files.push_back(meta_bytes(true, u"Nam=Profiles\r\nName =x\r\nName=Profiles\r\n"));
        files.push_back(meta_bytes(true, u"NoCopy=1\r\nName=Profiles\r\n"));
        files.push_back(meta_bytes(true, u""));
        files.push_back({0xff});
        files.push_back({});
        files.push_back(meta_bytes(true, u"Name=\r\n"));
        files.push_back(meta_bytes(true, u"\r\n\r\nName=Profiles\r\n\r\n"));
        // long lines before the name, and a name longer than the value buffers
        for (int i = 0; i < 24; ++i) {
            std::u16string text;
            const uint32_t lines = random.below(5);
            for (uint32_t l = 0; l < lines; ++l) {
                const uint32_t length = random.below(300);
                text += random.below(2) ? u"Junk=" : u"";
                for (uint32_t k = 0; k < length; ++k) text += static_cast<char16_t>('a' + random.below(26));
                text += u"\r\n";
            }
            text += u"Name=";
            const uint32_t length = random.below(i < 20 ? 40 : 200);
            for (uint32_t k = 0; k < length; ++k) text += static_cast<char16_t>('A' + random.below(26));
            if (random.below(4)) text += u"\r\n";
            files.push_back(meta_bytes(random.below(6) != 0, text));
        }
        static const uint16_t kName[] = {'N', 'a', 'm', 'e', 0};
        static const uint16_t kNoCopy[] = {'N', 'o', 'C', 'o', 'p', 'y', 0};
        static const uint16_t kNam[] = {'N', 'a', 'm', 0};
        static const uint16_t kProfiles[] = {'P', 'r', 'o', 'f', 'i', 'l', 'e', 's', 0};
        uint32_t differences = 0, cases = 0;
        const char* const path = "T:\\svchk\\meta.xbx";
        for (size_t f = 0; f < files.size(); ++f) {
            write_file(path, files[f]);
            const uint32_t file = CreateFileA(path, 0x80000000, 1, 0, 3, 0x80, 0);
            if (file == 0xffffffff) {
                ++differences;
                add_line(report, "meta file %u: cannot open", static_cast<uint32_t>(f));
                continue;
            }
            for (const uint16_t* key : {kName, kNoCopy, kNam})
                for (int32_t count : {0x80, 5, 1, 0x200}) {
                    for (uint32_t rewind : {1u, 0u}) {
                        uint32_t pa = 0, pb = 0;
                        const uint32_t start = rewind ? 2 * random.below(3) : 2 * random.below(2);
                        SetFilePointer(file, start, nullptr, 0);
                        scrub_stack();
                        const uint32_t va = orig_validate(file, rewind);
                        uint16_t a[0x210], b[0x210];
                        std::memset(a, 0xcc, sizeof a);
                        std::memset(b, 0xcc, sizeof b);
                        scrub_stack();
                        const uint32_t ra = orig_find_value(file, key, a, count);
                        pa = SetFilePointer(file, 0, nullptr, 1);
                        SetFilePointer(file, start, nullptr, 0);
                        scrub_stack();
                        const uint32_t vb = XapiValidateAndSkipUnicodeSignature(file, rewind);
                        const uint32_t rb = XapiFindValueInMetaFile(file, key, b, count);
                        pb = SetFilePointer(file, 0, nullptr, 1);
                        ++cases;
                        if (va != vb || ra != rb || pa != pb || std::memcmp(a, b, sizeof a) != 0) {
                            if (++differences <= 5)
                                add_line(report, "meta file %u key %s count %d: title %u %u pos %u \"%s\", ours %u %u pos %u \"%s\"",
                                         static_cast<uint32_t>(f), narrow(key).c_str(), count, va, ra, pa,
                                         ra ? narrow(a).c_str() : "", vb, rb, pb, rb ? narrow(b).c_str() : "");
                        }
                    }
                }
            CloseHandle(file);
            for (const uint16_t* name : {kProfiles, kNam}) {
                scrub_stack();
                const uint32_t va = orig_verify(path, name);
                scrub_stack();
                const uint32_t vb = XapiVerifyGameName(path, name);
                ++cases;
                if (va != vb && ++differences <= 5)
                    add_line(report, "XapiVerifyGameName file %u: title %u, ours %u", static_cast<uint32_t>(f), va, vb);
            }
        }
        DeleteFileA(path);
        // A missing meta file: created (empty) by either, 0x491; a missing directory: CreateFileA's error.
        for (const char* missing : {"T:\\svchk\\missing.xbx", "T:\\svchk\\nodir\\SaveMeta.xbx"}) {
            const uint32_t va = orig_verify(missing, kProfiles);
            const bool ca = exists(missing);
            DeleteFileA(missing);
            const uint32_t vb = XapiVerifyGameName(missing, kProfiles);
            const bool cb = exists(missing);
            DeleteFileA(missing);
            ++cases;
            if (va != vb || ca != cb) {
                ++differences;
                add_line(report, "XapiVerifyGameName %s: title %u created %d, ours %u created %d", missing, va, ca, vb, cb);
            }
        }
        failures += static_cast<int32_t>(differences);
        add_line(report, "meta files: %u files, %u cases, %u difference(s)", static_cast<uint32_t>(files.size()), cases,
                 differences);
    }

    // Directories: create, exists, missing parent, remove, remove missing / non-empty
    {
        const auto orig_create = title_function<uint32_t XB_STDCALL(const char*, uint32_t)>(0x001ed5d0);
        const auto orig_remove = title_function<uint32_t XB_STDCALL(const char*)>(0x001ed63c);
        uint32_t differences = 0;
        for (int side = 0; side < 2; ++side) {
            const auto create = side ? &CreateDirectoryA : orig_create;
            const auto remove = side ? &RemoveDirectoryA : orig_remove;
            uint32_t r[7], e[7];
            const auto step = [&](int k, uint32_t v) {
                r[k] = v;
                e[k] = GetLastError();
                SetLastError(0);
            };
            SetLastError(0);
            step(0, create("T:\\svchk\\d1", 0));
            step(1, create("T:\\svchk\\d1", 0));
            step(2, create("T:\\svchk\\nope\\d2", 0));
            write_file("T:\\svchk\\d1\\f", {1, 2, 3});
            step(3, remove("T:\\svchk\\d1"));
            DeleteFileA("T:\\svchk\\d1\\f");
            step(4, remove("T:\\svchk\\d1"));
            step(5, remove("T:\\svchk\\d1"));
            step(6, exists("T:\\svchk\\d1") ? 1 : 0);
            static uint32_t first[14];
            if (!side) {
                std::memcpy(first, r, sizeof r), std::memcpy(first + 7, e, sizeof e);
            } else if (std::memcmp(first, r, sizeof r) != 0 || std::memcmp(first + 7, e, sizeof e) != 0) {
                ++differences;
                for (int k = 0; k < 7; ++k)
                    add_line(report, "directories step %d: title %u (%u), ours %u (%u)", k, first[k], first[7 + k], r[k], e[k]);
            }
        }
        failures += static_cast<int32_t>(differences);
        add_line(report, "Create/RemoveDirectoryA: 6 steps, %u difference(s)", differences);
    }

    // Save games: create (each disposition, NoCopy, short path buffers, a foreign name in the meta file), enumerate
    // (find data bytes), display blocks, delete (with a subdirectory). The title's run first, then ours on the same
    // names; both against what the format prescribes.
    {
        const auto orig_create = title_function<uint32_t XB_STDCALL(const char*, const uint16_t*, uint32_t, uint32_t,
                                                                    char*, uint32_t)>(0x001e8fbc);
        const auto orig_delete = title_function<uint32_t XB_STDCALL(const char*, const uint16_t*)>(0x001e921f);
        const auto orig_first = title_function<uint32_t XB_STDCALL(const char*, uint8_t*)>(0x001e92ef);
        const auto orig_next = title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*)>(0x001e93f9);
        const auto orig_close = title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e9440);
        const auto orig_blocks = title_function<uint32_t XB_STDCALL(const char*)>(0x001ea047);
        const auto orig_fill = title_function<uint32_t XB_STDCALL(uint8_t*, const char*, uint32_t)>(0x001e8ddf);
        const char* const root = "T:\\svchk\\";
        static const std::u16string kNames[] = {u"Profiles", u"Campaign Save 1", u"x", u"\u00e9t\u00e9 \u20ac", u"Long name with many characters 0123456789"};
        struct Op {
            uint32_t name, disposition, flags, path_size;
        };
        static const Op kOps[] = {
            {0, 2, 0, 0x104}, {0, 1, 0, 0x104}, {0, 4, 0, 0x104}, {0, 3, 0, 0x104}, {0, 2, 0, 3},    {1, 1, 1, 0x104},
            {1, 3, 0, 0x14},  {2, 3, 0, 0x104}, {2, 4, 1, 0x1a}, {3, 2, 0, 0x1b},  {4, 2, 1, 0x104}, {4, 2, 0, 0},
        };
        constexpr uint32_t kOpCount = sizeof kOps / sizeof *kOps;
        uint32_t differences = 0;
        std::vector<SaveState> states[2];
        std::vector<std::vector<uint8_t>> finds[2];
        std::vector<uint32_t> blocks[2], deletes[2];
        uint32_t enum_result[2][2] = {};
        for (int side = 0; side < 2; ++side) {
            const auto create = side ? &XCreateSaveGame : orig_create;
            const auto erase = side ? &XDeleteSaveGame : orig_delete;
            const auto first = side ? &XFindFirstSaveGame : orig_first;
            const auto next = side ? &XFindNextSaveGame : orig_next;
            const auto close = side ? &XFindClose : orig_close;
            const auto display_blocks = side ? &XGetDisplayBlocks : orig_blocks;
            for (const Op& op : kOps) {
                SaveState s;
                std::memset(s.path, 0x5a, sizeof s.path);
                s.path[sizeof s.path - 1] = 0;
                const auto* name = reinterpret_cast<const uint16_t*>(kNames[op.name].c_str());
                SetLastError(0);
                s.result = create(root, name, op.disposition, op.flags, op.path_size ? s.path : nullptr, op.path_size);
                s.error = GetLastError();
                char directory[0x104];
                std::strcpy(directory, root);
                XapiSaveGameHashEncode(name, directory + std::strlen(root), 0xf7);
                s.directory = exists(directory);
                std::string meta = std::string(directory) + "\\SaveMeta.xbx";
                s.meta = read_file(meta.c_str(), &s.meta_ok);
                states[side].push_back(s);
            }
            // A foreign name in one save's meta file: create (0x52), open (0x491) and delete (0x491) refuse it.
            {
                const auto* name = reinterpret_cast<const uint16_t*>(kNames[2].c_str());
                char directory[0x104];
                std::strcpy(directory, root);
                XapiSaveGameHashEncode(name, directory + std::strlen(root), 0xf7);
                const std::string meta = std::string(directory) + "\\SaveMeta.xbx";
                bool ok = false;
                const std::vector<uint8_t> saved = read_file(meta.c_str(), &ok);
                write_file(meta.c_str(), meta_bytes(true, u"Name=Somebody else\r\n"));
                SaveState s;
                s.result = create(root, name, 2, 0, nullptr, 0);
                s.error = erase(root, name);
                s.directory = exists(directory);
                states[side].push_back(s);
                write_file(meta.c_str(), saved);
                // a subdirectory with a file, removed with the save
                std::string sub = std::string(directory) + "\\sub";
                CreateDirectoryA(sub.c_str(), 0);
                write_file((sub + "\\data.bin").c_str(), {1, 2, 3, 4, 5});
            }
            // A directory with no meta file and a plain file in the root: the enumeration skips them.
            CreateDirectoryA("T:\\svchk\\NOMETA", 0);
            write_file("T:\\svchk\\NOMETA\\x.bin", {7, 7});
            write_file("T:\\svchk\\plain.bin", {9});
            uint8_t find[0x344];
            std::memset(find, 0xcc, sizeof find);
            uint32_t search = first(root, find);
            enum_result[side][0] = search == 0xffffffff ? 0xffffffff : 1;
            // (the directories were made at other times on each side, and the listing order may differ: times
            // masked, entries sorted, each with its display blocks)
            std::vector<std::vector<uint8_t>> scratch_entries;
            while (search != 0xffffffff) {
                std::vector<uint8_t> entry(find, find + sizeof find);
                std::memset(entry.data() + 4, 0, 0x18);
                const uint32_t n = display_blocks(reinterpret_cast<const char*>(find + 0x140));
                entry.insert(entry.end(), reinterpret_cast<const uint8_t*>(&n), reinterpret_cast<const uint8_t*>(&n) + 4);
                scratch_entries.push_back(entry);
                std::memset(find, 0xcc, sizeof find);
                if (!next(search, find)) {
                    enum_result[side][1] = close(search);
                    break;
                }
            }
            std::sort(scratch_entries.begin(), scratch_entries.end());
            finds[side].insert(finds[side].end(), scratch_entries.begin(), scratch_entries.end());
            // Roots with no save (an empty directory, a missing one): -1 and the last error.
            for (const char* empty_root : {"T:\\svchk\\NOMETA\\", "T:\\svchk\\none\\"}) {
                SetLastError(0);
                const uint32_t r = first(empty_root, find);
                blocks[side].push_back(r);
                blocks[side].push_back(GetLastError());
                if (r != 0xffffffff) close(r);
            }
            for (const char* path : {"T:\\svchk\\plain.bin", "T:\\svchk\\", "T:\\svchk\\NOMETA", "T:\\svchk\\none",
                                     "U:\\", "D:\\nope"}) {
                SetLastError(0);
                blocks[side].push_back(display_blocks(path));
                blocks[side].push_back(GetLastError());
            }
            // The enumeration on the real save folder copy (read only).
            std::memset(find, 0xcc, sizeof find);
            search = first("U:\\", find);
            while (search != 0xffffffff) {
                finds[side].emplace_back(find, find + sizeof find);
                std::memset(find, 0xcc, sizeof find);
                if (!next(search, find)) {
                    close(search);
                    break;
                }
            }
            // Delete every save made, then the leftovers.
            for (const std::u16string& name : kNames) {
                const auto* n = reinterpret_cast<const uint16_t*>(name.c_str());
                deletes[side].push_back(erase(root, n));
                deletes[side].push_back(erase(root, n));
                char directory[0x104];
                std::strcpy(directory, root);
                XapiSaveGameHashEncode(n, directory + std::strlen(root), 0xf7);
                deletes[side].push_back(exists(directory) ? 1 : 0);
            }
            DeleteFileA("T:\\svchk\\NOMETA\\x.bin");
            RemoveDirectoryA("T:\\svchk\\NOMETA");
            DeleteFileA("T:\\svchk\\plain.bin");
        }
        for (uint32_t i = 0; i < states[0].size() && i < states[1].size(); ++i) {
            const SaveState& a = states[0][i];
            const SaveState& b = states[1][i];
            if (a.result != b.result || a.error != b.error || std::memcmp(a.path, b.path, sizeof a.path) != 0 ||
                a.meta_ok != b.meta_ok || a.meta != b.meta || a.directory != b.directory) {
                ++differences;
                add_line(report, "save op %u: title %u (%u) dir %d meta %d/%u \"%s\", ours %u (%u) dir %d meta %d/%u \"%s\"",
                         i, a.result, a.error, a.directory, a.meta_ok, static_cast<uint32_t>(a.meta.size()), a.path,
                         b.result, b.error, b.directory, b.meta_ok, static_cast<uint32_t>(b.meta.size()), b.path);
            }
        }
        // What the format prescribes, for ours (the title's address of a Replaced row is ours too).
        for (uint32_t i = 0; i < kOpCount && i < states[1].size(); ++i) {
            const Op& op = kOps[i];
            const SaveState& s = states[1][i];
            const auto* name = reinterpret_cast<const uint16_t*>(kNames[op.name].c_str());
            char directory[0x104];
            std::strcpy(directory, root);
            XapiSaveGameHashEncode(name, directory + std::strlen(root), 0xf7);
            std::strcat(directory, "\\");
            std::vector<uint8_t> expected;
            // the meta file as the first create of this name wrote it
            for (uint32_t k = 0; k <= i; ++k)
                if (kOps[k].name == op.name && kOps[k].disposition != 3) {
                    expected = meta_bytes(true, u"Name=" + kNames[op.name] + u"\r\n" +
                                                    (kOps[k].flags & 1 ? u"NoCopy=1\r\n" : u""));
                    break;
                }
            bool good = s.directory == !expected.empty() && (expected.empty() || s.meta == expected);
            if (s.result == 0 && op.path_size) {
                char want[0x104];
                std::memset(want, 0x5a, sizeof want);
                want[sizeof want - 1] = 0;
                lstrcpynA(want, directory, op.path_size);
                good = good && std::memcmp(want, s.path, sizeof want) == 0;
            }
            if (!good) {
                ++differences;
                add_line(report, "save op %u: not the prescribed directory / meta file / path (\"%s\")", i, s.path);
            }
        }
        if (finds[0] != finds[1] || blocks[0] != blocks[1] || deletes[0] != deletes[1] ||
            std::memcmp(enum_result[0], enum_result[1], sizeof enum_result[0]) != 0) {
            ++differences;
            add_line(report, "save enumeration/blocks/delete: title %u entries, ours %u; blocks %s; deletes %s",
                     static_cast<uint32_t>(finds[0].size()), static_cast<uint32_t>(finds[1].size()),
                     blocks[0] == blocks[1] ? "equal" : "differ", deletes[0] == deletes[1] ? "equal" : "differ");
            for (size_t k = 0; k < finds[0].size() && k < finds[1].size(); ++k)
                for (size_t j = 0; j < finds[0][k].size() && j < finds[1][k].size(); ++j)
                    if (finds[0][k][j] != finds[1][k][j]) {
                        add_line(report, "  entry %u differs from byte 0x%x", static_cast<uint32_t>(k), static_cast<uint32_t>(j));
                        break;
                    }
        }
        // Each save found once with its name; every save gone after the deletes.
        if (finds[1].size() < 5) {
            ++differences;
            add_line(report, "save enumeration: only %u entries", static_cast<uint32_t>(finds[1].size()));
        }
        for (size_t k = 2; k < deletes[1].size(); k += 3)
            if (deletes[1][k] != 0) {
                ++differences;
                add_line(report, "save %u still there after XDeleteSaveGame", static_cast<uint32_t>(k / 3));
            }
        // XapiFillInSaveGameData on entries of the scratch root (the title's vs ours on the same find data).
        {
            uint8_t find[0x344];
            CreateDirectoryA("T:\\svchk\\FILLA", 0);
            write_file("T:\\svchk\\FILLA\\SaveMeta.xbx", meta_bytes(true, u"Name=Fill\r\n"));
            CreateDirectoryA("T:\\svchk\\FILLB", 0);
            write_file("T:\\svchk\\FILLB\\SaveMeta.xbx", meta_bytes(false, u"Junk\r\nName=Fill B"));
            CreateDirectoryA("T:\\svchk\\FILLC", 0);
            write_file("T:\\svchk\\FILLC.bin", {1});
            uint32_t search = FindFirstFileA("T:\\svchk\\*", find);
            uint32_t entries = 0;
            for (uint32_t more = search != 0xffffffff; more; more = FindNextFileA(search, find)) {
                for (const char* r : {"T:\\svchk\\", "T:\\svchk\\a_much_longer_root_than_needed_for_this\\"}) {
                    const auto name_length = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(find + 0x2c)));
                    for (uint32_t length : {static_cast<uint32_t>(std::strlen(r)), 0xe0u, 0xf5u - name_length,
                                            0xf6u - name_length, 0xf7u - name_length, 0x102u - name_length,
                                            0x103u - name_length}) {
                        uint8_t a[0x344], b[0x344];
                        std::memcpy(a, find, sizeof a);
                        std::memcpy(b, find, sizeof b);
                        scrub_stack();
                        const uint32_t ra = orig_fill(a, r, length);
                        scrub_stack();
                        const uint32_t rb = XapiFillInSaveGameData(b, r, length);
                        ++entries;
                        if (ra != rb || std::memcmp(a, b, sizeof a) != 0) {
                            ++differences;
                            add_line(report, "XapiFillInSaveGameData %s root %u: title %u, ours %u",
                                     reinterpret_cast<const char*>(find + 0x2c), length, ra, rb);
                        }
                    }
                }
            }
            if (search != 0xffffffff) CloseHandle(search);
            DeleteFileA("T:\\svchk\\FILLA\\SaveMeta.xbx");
            DeleteFileA("T:\\svchk\\FILLB\\SaveMeta.xbx");
            DeleteFileA("T:\\svchk\\FILLC.bin");
            RemoveDirectoryA("T:\\svchk\\FILLA");
            RemoveDirectoryA("T:\\svchk\\FILLB");
            RemoveDirectoryA("T:\\svchk\\FILLC");
            add_line(report, "XapiFillInSaveGameData: %u cases", entries);
        }
        failures += static_cast<int32_t>(differences);
        add_line(report, "save games: %u operations, %u + %u entries found, %u difference(s)", kOpCount + 1,
                 static_cast<uint32_t>(finds[0].size()), static_cast<uint32_t>(finds[1].size()), differences);
    }
    RemoveDirectoryA(scratch);
    return failures;
}

}  // namespace game
