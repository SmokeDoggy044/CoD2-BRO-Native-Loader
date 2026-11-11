#include "title_savegame.h"

#include <cstring>
#include <string>
#include <vector>

#include "title_crt.h"
#include "title_data.h"
#include "title_files.h"
#include "title_host.h"
#include "title_objects.h"
#include "title_heap.h"
#include "title_xapi.h"

namespace game {

namespace {

// A kernel export through the title's kernel thunk table (as title_xapi.cpp): the slot holds the export's address,
// or the exported variable's.
template <typename F>
F* kernel(uint32_t slot) {
    return title_function<F>(title_global<uint32_t>(slot));
}

constexpr uint32_t kRtlNtStatusToDosError = 0x007b2ffc;
constexpr uint32_t kNtCreateFile = 0x007b3004;
constexpr uint32_t kRtlInitAnsiString = 0x007b3008;
constexpr uint32_t kNtClose = 0x007b3014;
constexpr uint32_t kNtOpenFile = 0x007b3018;
constexpr uint32_t kXboxHDKey = 0x007b302c;
constexpr uint32_t kXcSHAFinal = 0x007b3048;
constexpr uint32_t kXcSHAUpdate = 0x007b304c;
constexpr uint32_t kXcSHAInit = 0x007b3050;
constexpr uint32_t kNtSetInformationFile = 0x007b3054;
constexpr uint32_t kKeQuerySystemTime = 0x007b3080;
constexpr uint32_t kNtQueryDirectoryFile = 0x007b3144;
constexpr uint32_t kXboxSignatureKey = 0x007b319c;
constexpr uint32_t kXboxAlternateSignatureKeys = 0x007b31a0;
constexpr uint32_t kRtlUnicodeStringToAnsiString = 0x007b31b4;
constexpr uint32_t kNtQueryFullAttributesFile = 0x007b31c4;

constexpr uint32_t kXMemAttributes = 0x24830000;  // XMemAlloc/XMemFree: XAPI's allocator id
constexpr uint32_t kMaxBlocks = 0xc351;           // display blocks shown as "50,000+"
constexpr uint32_t kNoMoreFiles = 0x80000006;
constexpr uint32_t kNoSuchFile = 0xc000000f;

constexpr char kMetaFile[] = "\\SaveMeta.xbx";
constexpr uint16_t kNameKey[] = {'N', 'a', 'm', 'e', 0};  // 0x00827034
constexpr uint16_t kCrLf[] = {'\r', '\n', 0};             // 0x00827084
constexpr uint16_t kNoCopy[] = {'N', 'o', 'C', 'o', 'p', 'y', '=', '1', '\r', '\n', 0};  // 0x0082708c
constexpr uint16_t kMetaFormat[] = {'%', 'l', 'c', '%', 'l', 's', '%', 'l', 'c', '%', 'l', 's', '%', 'l', 's', 0};
constexpr uint16_t kMetaFormatNoCopy[] = {'%', 'l', 'c', '%', 'l', 's', '%', 'l', 'c', '%', 'l', 's',
                                          '%', 'l', 's', '%', 'l', 's', 0};

uint32_t addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}
template <typename T>
T* ptr(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t dword(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// OBJECT_ATTRIBUTES {RootDirectory, ObjectName, Attributes} and ANSI_STRING / UNICODE_STRING.
struct ObjectAttributes {
    uint32_t root;
    uint32_t name;
    uint32_t attributes;
};
struct CountedString {
    uint16_t length;
    uint16_t maximum;
    uint32_t buffer;
};

void init_ansi(CountedString* string, const char* text) {
    kernel<void XB_STDCALL(CountedString*, const char*)>(kRtlInitAnsiString)(string, text);
}
uint32_t nt_open(uint32_t* handle, uint32_t access, ObjectAttributes* attributes, uint32_t* io_status,
                 uint32_t share, uint32_t options) {
    return kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, ObjectAttributes*, uint32_t*, uint32_t, uint32_t)>(
        kNtOpenFile)(handle, access, attributes, io_status, share, options);
}
uint32_t nt_close(uint32_t handle) { return kernel<uint32_t XB_STDCALL(uint32_t)>(kNtClose)(handle); }
uint32_t nt_set_information(uint32_t handle, uint32_t* io_status, void* info, uint32_t length, uint32_t info_class) {
    return kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, void*, uint32_t, uint32_t)>(kNtSetInformationFile)(
        handle, io_status, info, length, info_class);
}
// NtQueryDirectoryFile(handle, no event/APC, io, buffer, 0x146, FileDirectoryInformation, no mask, restart): the
// originals push the restart flag as a byte in a dword; the kernel reads the byte.
uint32_t query_directory(uint32_t handle, uint32_t* io_status, uint8_t* buffer, uint32_t restart) {
    return kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint8_t*, uint32_t, uint32_t,
                                      CountedString*, uint32_t)>(kNtQueryDirectoryFile)(handle, 0, 0, 0, io_status,
                                                                                         buffer, 0x146, 1, nullptr,
                                                                                         restart);
}
uint32_t dos_error(uint32_t status) {
    return kernel<uint32_t XB_STDCALL(uint32_t)>(kRtlNtStatusToDosError)(status);
}
bool failed(uint32_t status) { return static_cast<int32_t>(status) < 0; }

void sha_init(uint8_t* sha) { kernel<void XB_STDCALL(uint8_t*)>(kXcSHAInit)(sha); }
void sha_update(uint8_t* sha, const uint8_t* data, uint32_t size) {
    kernel<void XB_STDCALL(uint8_t*, const uint8_t*, uint32_t)>(kXcSHAUpdate)(sha, data, size);
}
void sha_final(uint8_t* sha, uint8_t* digest) { kernel<void XB_STDCALL(uint8_t*, uint8_t*)>(kXcSHAFinal)(sha, digest); }

// The XBE certificate's title id (certificate address at 0x00010118, +8).
uint32_t certificate() { return title_global<uint32_t>(0x00010118); }

// Size in 16 KB blocks, rounded up.
uint32_t blocks_of(uint32_t size) { return (size >> 14) + ((size & 0x3fff) != 0 ? 1 : 0); }

}  // namespace

// --- Signatures ---------------------------------------------------------------------------------------------------

void XB_STDCALL XShaHmacInitialize(const uint8_t* key, uint32_t key_length, uint8_t* sha) {
    // Inner pad: the key (at most 64 bytes) zero-filled to 64, xor 0x36.
    if (key_length > 0x40) key_length = 0x40;
    uint8_t pad[0x40] = {};
    std::memcpy(pad, key, key_length);
    for (uint8_t& b : pad) b ^= 0x36;
    sha_init(sha);
    sha_update(sha, pad, 0x40);
}

void XB_STDCALL XShaHmacComputeFinal(uint8_t* sha, const uint8_t* key, uint32_t key_length, uint8_t* digest) {
    // Outer pad (key xor 0x5c) followed by the inner hash, hashed again.
    if (key_length > 0x40) key_length = 0x40;
    uint8_t block[0x54] = {};
    std::memcpy(block, key, key_length);
    for (uint32_t i = 0; i < 0x40; ++i) block[i] ^= 0x5c;
    sha_final(sha, block + 0x40);
    sha_init(sha);
    sha_update(sha, block, 0x54);
    sha_final(sha, digest);
}

uint32_t XB_STDCALL XapiConvertSignatureToNonRoamable(uint8_t* context, const uint8_t* signature, uint8_t* out) {
    // HMAC of the signature keyed with the console's hard disk key (XboxHDKey; when it is all zero, bytes 0x36..
    // of the encrypted EEPROM section, XQueryValue 0xffff).
    const uint8_t* key = ptr<uint8_t>(title_global<uint32_t>(kXboxHDKey));
    uint8_t any = 0;
    for (int32_t i = 0; i < 0x10; ++i) any |= key[i];
    uint8_t eeprom[0x100];
    if (!any) {
        uint32_t type, size;
        XQueryValue(0xffff, &type, eeprom, 0x100, &size);
        key = eeprom + 0x36;
    }
    uint8_t* const sha = context + 8;
    XShaHmacInitialize(key, 0x10, sha);
    sha_update(sha, signature, 0x14);
    XShaHmacComputeFinal(sha, key, 0x10, out);
    return 1;
}

uint32_t XB_STDCALL XapipCalculateSaveGameSignatureBegin(uint32_t flags, uint32_t title_id) {
    // The key: XboxSignatureKey for this title, XboxAlternateSignatureKeys[i] for the certificate's alternate
    // title id i. The search stops at the first zero id or after 16 and then tests i < 64, so a title id not among
    // 16 nonzero ones takes key 16.
    const uint32_t cert = certificate();
    auto* const context = ptr<uint32_t>(XMemAlloc(0x7c, kXMemAttributes));
    if (!context) {
        SetLastError(8);
        return 0xffffffff;
    }
    context[0] = flags;
    uint32_t key;
    if (title_global<uint32_t>(cert + 8) == title_id) {
        key = title_global<uint32_t>(kXboxSignatureKey);
    } else {
        uint32_t i = 0;
        for (;;) {
            const uint32_t id = title_global<uint32_t>(cert + 0x5c + i * 4);
            if (!id) {
                SetLastError(5);  // the context stays allocated
                return 0xffffffff;
            }
            if (id == title_id || ++i >= 0x10) break;
        }
        if (i >= 0x40) {
            SetLastError(5);
            return 0xffffffff;
        }
        key = title_global<uint32_t>(kXboxAlternateSignatureKeys) + i * 0x10;
    }
    context[1] = key;
    XShaHmacInitialize(ptr<uint8_t>(key), 0x10, reinterpret_cast<uint8_t*>(context + 2));
    return addr(context);
}

uint32_t XB_STDCALL XapipCalculateSaveGameSignatureEnd(uint32_t context, uint8_t* signature) {
    // Type bit 0: non-roamable (the HMAC signed again with the console's key).
    auto* const c = ptr<uint32_t>(context);
    const uint32_t non_roamable = c[0] & 1;
    uint32_t ok = 1;
    if (signature) {
        uint8_t digest[0x14];
        XShaHmacComputeFinal(reinterpret_cast<uint8_t*>(c + 2), ptr<uint8_t>(c[1]), 0x10,
                             non_roamable ? digest : signature);
        if (non_roamable) ok = XapiConvertSignatureToNonRoamable(reinterpret_cast<uint8_t*>(c), digest, signature);
    }
    XMemFree(context, kXMemAttributes);
    return ok ? 0 : GetLastError();
}

uint32_t XB_STDCALL XapipCalculateContentSignatureBegin() {
    // Content signatures (types 2 and 4): a plain SHA-1, type 2.
    auto* const context = ptr<uint32_t>(XMemAlloc(0x7c, kXMemAttributes));
    if (!context) {
        SetLastError(8);
        return 0xffffffff;
    }
    context[1] = 0;
    context[0] = 2;
    sha_init(reinterpret_cast<uint8_t*>(context + 2));
    return addr(context);
}

uint32_t XB_STDCALL FUN_001e8a21(uint32_t context, const uint8_t* data, uint32_t size) {
    sha_update(ptr<uint8_t>(context + 8), data, size);
    return 0;
}

uint32_t XB_STDCALL XapipCalculateContentSignatureEnd(uint32_t context, uint8_t* signature) {
    if (signature) sha_final(ptr<uint8_t>(context + 8), signature);
    XMemFree(context, kXMemAttributes);
    return 0;
}

// Type 8 goes through three function pointers at 0x009267b0/b4/b8 (stdcall; null in this title).
uint32_t XB_STDCALL XCalculateSignatureBeginEx(uint32_t flags, uint32_t title_id) {
    if (flags <= 1) return XapipCalculateSaveGameSignatureBegin(flags, title_id);
    if (flags == 2 || flags == 4) return XapipCalculateContentSignatureBegin();
    if (flags == 8) return title_function<uint32_t XB_STDCALL(uint32_t)>(title_global<uint32_t>(0x009267b0))(flags);
    SetLastError(0x32);
    return 0xffffffff;
}

uint32_t XB_STDCALL XCalculateSignatureBegin(uint32_t flags) {
    return XCalculateSignatureBeginEx(flags, title_global<uint32_t>(certificate() + 8));
}

uint32_t XB_STDCALL XCalculateSignatureUpdate(uint32_t context, const uint8_t* data, uint32_t size) {
    const uint32_t type = *ptr<uint32_t>(context);
    if (type <= 1 || type == 2 || type == 4) return FUN_001e8a21(context, data, size);
    if (type == 8)
        return title_function<uint32_t XB_STDCALL(uint32_t, const uint8_t*, uint32_t)>(
            title_global<uint32_t>(0x009267b4))(context, data, size);
    return 0x32;  // no last error
}

uint32_t XB_STDCALL XCalculateSignatureEnd(uint32_t context, uint8_t* signature) {
    const uint32_t type = *ptr<uint32_t>(context);
    if (type <= 1) return XapipCalculateSaveGameSignatureEnd(context, signature);
    if (type == 2 || type == 4) return XapipCalculateContentSignatureEnd(context, signature);
    if (type == 8)
        return title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*)>(title_global<uint32_t>(0x009267b8))(context,
                                                                                                         signature);
    return 0x32;
}

// --- Save games ---------------------------------------------------------------------------------------------------

uint32_t XB_STDCALL HexDigitToChar(int32_t digit) {
    return static_cast<uint32_t>(digit > 9 ? digit + 0x37 : digit + 0x30);
}

void XB_STDCALL XapiSaveGameHashEncode(const uint16_t* name, char* out, uint32_t) {
    // hash = (hash * 0x10000 + c) mod 0xffffffffffc5 over the name; 12 upper-case hex digits, most significant
    // first.
    uint64_t hash = 0;
    for (; *name; ++name) hash = (hash * 0x10000 + *name) % 0x0000ffffffffffc5ull;
    for (int32_t i = 0; i < 12; ++i)
        out[11 - i] = static_cast<char>(HexDigitToChar(static_cast<int32_t>((hash >> (i * 4)) & 0xf)));
    out[12] = 0;
}

uint32_t XB_STDCALL XapiValidateAndSkipUnicodeSignature(uint32_t file, uint32_t rewind) {
    // Reads 2 bytes into the upper half of its own `rewind` argument: any 2 bytes read pass; a short or failed read
    // passes only if that word holds 0xfeff anyway.
    if (rewind) SetFilePointer(file, 0, nullptr, 0);
    uint32_t word = rewind;
    uint32_t read;
    if (ReadFile(file, reinterpret_cast<uint8_t*>(&word) + 2, 2, &read, nullptr) && read == 2) return 1;
    return (word >> 16) == 0xfeff ? 1 : 0;
}

uint32_t XB_STDCALL XapiFindValueInMetaFile(uint32_t file, const uint16_t* key, uint16_t* value, int32_t count) {
    // A window of 0x8c characters over the file, a zero word after it. A line "<key>=<value>\r\n" in the window:
    // value copied (at most count - 1 characters, terminated) and the file pointer set back to the next line.
    // Otherwise the window skips to the next line (reading on when it holds no line end).
    // The original's window is uninitialized stack: past a short read it holds what was there before; ours zeros.
    // A remainder of odd length makes the next read reach the terminator's low byte, as in the original.
    alignas(4) uint8_t window[0x120] = {};
    uint8_t* const base = window;
    const auto* const text = reinterpret_cast<const uint16_t*>(window);
    uint8_t* write = base;
    uint32_t room = 0x8c;  // characters to read
    uint32_t read = 0;
    bool again = false;  // search the window once more without reading
    bool skip = false;   // skipping to the end of a line
    const uint32_t key_length = title_wcslen(key);
    for (;;) {
        if (!again) {
            if (!ReadFile(file, write, room * 2, &read, nullptr) || read == 0) return 0;
        }
        again = false;
        const uint32_t end = read + (static_cast<uint32_t>(write - base) >> 1) * 2;  // bytes in the window
        if (skip) {
            const uint16_t* const line_end = title_wcsstr(text, kCrLf);
            write = base;
            room = 0x8c;
            if (!line_end) continue;
            const uint32_t next = static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(line_end) - base) + 4;
            if (next < end) {
                const uint32_t rest = end - next;
                title_memmove(base, base + next, rest);
                write = base + rest;
                room = 0x8c - (rest >> 1);
            }
            skip = false;
            continue;
        }
        const uint16_t* const hit = title_wcsstr(text, key);
        if (!hit || hit[key_length] != '=') {
            skip = true;
            again = true;
            continue;
        }
        const uint16_t* const found = hit + key_length + 1;
        const uint16_t* const line_end = title_wcsstr(found, kCrLf);
        if (!line_end) {
            skip = true;
            write = base;
            room = 0x8c;
            continue;
        }
        const auto length = static_cast<int32_t>(
            (reinterpret_cast<const uint8_t*>(line_end) - reinterpret_cast<const uint8_t*>(found)) >> 1);
        int32_t copy = count - 1;
        if (copy >= length) copy = length;
        title_wcsncpy(value, found, static_cast<uint32_t>(copy));
        value[copy] = 0;
        const uint32_t next = static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(line_end) - base) + 4;
        if (next < end) SetFilePointer(file, next - end, nullptr, 1);
        return 1;
    }
}

uint32_t XB_STDCALL XapiFillInSaveGameData(uint8_t* find, const char* root, uint32_t root_length) {
    // For a directory entry: XGAME_FIND_DATA's directory (+0x140) = root + name + "\", its display name (+0x244)
    // from <directory>SaveMeta.xbx. 0 for files, names that leave no room, or no readable name.
    if (!(dword(find) & 0x10)) return 0;
    const char* const name = reinterpret_cast<const char*>(find + 0x2c);
    char* const directory = reinterpret_cast<char*>(find + 0x140);
    const auto name_length = static_cast<uint32_t>(std::strlen(name));
    title_strcpy(directory, root);
    title_strcpy(directory + root_length, name);
    const uint32_t length = name_length + root_length;
    if (length < 0x103) {
        directory[length] = '\\';
        directory[length + 1] = 0;
    }
    if (static_cast<int32_t>(length) >= 0xf7) return 0;
    title_strcpy(directory + length, kMetaFile);
    uint32_t result = 0;
    const uint32_t file = CreateFileA(directory, 0x80000000, 1, 0, 3, 0x80, 0);
    if (file != 0xffffffff) {
        result = XapiValidateAndSkipUnicodeSignature(file, 1);
        if (result) result = XapiFindValueInMetaFile(file, kNameKey, reinterpret_cast<uint16_t*>(find + 0x244), 0x80);
        CloseHandle(file);
    }
    directory[length + 1] = 0;
    return result;
}

uint32_t XB_STDCALL XapiVerifyGameName(const char* meta_path, const uint16_t* name) {
    // 0 when the meta file's Name is `name`, 0x491 (ERROR_NO_MATCH) when not, else CreateFileA's error. Opened
    // OPEN_ALWAYS: a missing SaveMeta.xbx is created (empty, system attribute) and gives 0x491.
    const uint32_t file = CreateFileA(meta_path, 0x80100000, 0, 0, 4, 4, 0);
    if (file == 0xffffffff) return GetLastError();
    uint32_t error = 0;
    uint16_t found[0x80];
    if (!XapiValidateAndSkipUnicodeSignature(file, 1) || !XapiFindValueInMetaFile(file, kNameKey, found, 0x80) ||
        title_wcscmp(found, name) != 0)
        error = 0x491;
    CloseHandle(file);
    return error;
}

void XB_STDCALL XapiTouchDirectoryTimestamp(const char* path) {
    // The directory's last write time = now (FileBasicInformation, other times and attributes 0 = unchanged).
    const uint32_t file = CreateFileA(path, 0xc0000000, 3, 0, 3, 0x2000000, 0);
    if (file == 0xffffffff) return;
    if (objects::is_ours(file)) {
        files::touch(file);
        CloseHandle(file);
        return;
    }
    uint32_t basic[10] = {};
    kernel<void XB_STDCALL(uint32_t*)>(kKeQuerySystemTime)(basic + 4);
    uint32_t io_status[2];
    nt_set_information(file, io_status, basic, 0x28, 4);
    CloseHandle(file);
}

uint32_t XB_STDCALL XCreateSaveGame(const char* root, const uint16_t* name, uint32_t disposition, uint32_t flags,
                                    char* path, uint32_t path_size) {
    // disposition 3 (OPEN_EXISTING): the save must exist with this name; otherwise the directory is made, an
    // existing one must carry this name (else 0x52) and gets a new timestamp, and with disposition 1 (CREATE_NEW)
    // it is the error 0xb7. A new directory gets SaveMeta.xbx; when that fails the directory goes again.
    char directory[0x104];
    title_strcpy(directory, root);
    uint32_t length = static_cast<uint32_t>(std::strlen(directory));
    XapiSaveGameHashEncode(name, directory + length, 0xf7 - length);
    length = static_cast<uint32_t>(std::strlen(directory));
    if (disposition == 3) {
        title_strcpy(directory + length, kMetaFile);
        const uint32_t error = XapiVerifyGameName(directory, name);
        directory[length] = 0;
        if (error) return error;
    } else if (!CreateDirectoryA(directory, 0)) {
        const uint32_t error = GetLastError();
        uint32_t verify = 0x491;
        if (error == 0xb7) {
            title_strcpy(directory + length, kMetaFile);
            verify = XapiVerifyGameName(directory, name);
            directory[length] = 0;
            if (verify) return 0x52;
            XapiTouchDirectoryTimestamp(directory);
        }
        if (disposition == 1 || verify) return error;
    } else {
        title_strcpy(directory + length, kMetaFile);
        uint32_t handle;
        if (native_platform()) {
            // The same request without the kernel: read/write, overwrite or create; the error is not the last error.
            files::OpenRequest request;
            request.write = true;
            request.disposition = 2;
            bool existed;
            const uint32_t error = files::open(directory, request, &handle, &existed);
            directory[length] = 0;
            if (error) {
                RemoveDirectoryA(directory);
                return error;
            }
        } else {
        CountedString meta_name;
        init_ansi(&meta_name, directory);
        ObjectAttributes attributes{0xfffffffd, addr(&meta_name), 0x40};
        uint32_t allocation[2] = {1, 0};
        uint32_t io_status[2];
        // GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, system attribute, no sharing, FILE_OVERWRITE_IF,
        // non-directory + synchronous I/O.
        const uint32_t status =
            kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, ObjectAttributes*, uint32_t*, uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t)>(kNtCreateFile)(&handle, 0xc0100000, &attributes, io_status,
                                                                           addr(allocation), 4, 0, 3, 0x22);
        directory[length] = 0;
        if (failed(status)) {
            const uint32_t error = dos_error(status);  // not set as the last error
            RemoveDirectoryA(directory);
            return error;
        }
        }
        uint16_t text[0x98];
        title_snwprintf(text, 0x98, (flags & 1) ? kMetaFormatNoCopy : kMetaFormat, 0xfeffu, addr(kNameKey),
                        uint32_t{'='}, addr(name), addr(kCrLf), addr(kNoCopy));
        for (uint32_t remaining = title_wcslen(text) * 2; remaining;) {
            // Each pass writes from the start of the text again, as the original does.
            uint32_t written;
            if (!WriteFile(handle, text, remaining, &written, nullptr)) {
                const uint32_t error = GetLastError();
                CloseHandle(handle);
                RemoveDirectoryA(directory);
                return error;
            }
            remaining -= written;
        }
        CloseHandle(handle);
    }
    if (path) {
        lstrcpynA(path, directory, path_size);
        if (length < path_size - 1) {
            path[length] = '\\';
            path[length + 1] = 0;
        }
    }
    return 0;
}

uint32_t XB_STDCALL XDeleteSaveGame(const char* root, const uint16_t* name) {
    // The directory goes only when its SaveMeta.xbx names this game; NukeDirectory's status as a DOS error.
    char path[0x144];
    title_strcpy(path, "\\??\\");
    char* const dos_path = path + std::strlen(path);
    title_strcpy(dos_path, root);
    const auto length = static_cast<uint32_t>(std::strlen(path));
    XapiSaveGameHashEncode(name, path + length, 0x137 - length);
    const auto end = static_cast<uint32_t>(std::strlen(path));
    title_strcpy(path + end, kMetaFile);
    const uint32_t error = XapiVerifyGameName(dos_path, name);
    path[end] = 0;
    if (error) return error;
    path[end + 1] = 0;
    path[end] = '\\';
    if (native_platform()) return files::remove_tree(path);
    return dos_error(XapiNukeDirectory(path, 0));
}

uint32_t XB_STDCALL XFindFirstSaveGame(const char* root, uint8_t* find) {
    // FindFirstFileA("<root>*") up to the first save directory; the search handle is an XMemAlloc block
    // {'EVBS', find handle, root length, root (0x104)}.
    char pattern[0x104];
    title_strcpy(pattern, root);
    const auto root_length = static_cast<uint32_t>(std::strlen(pattern));
    title_strcpy(pattern + root_length, "*");
    uint32_t search = FindFirstFileA(pattern, find);
    if (search == 0xffffffff) return search;
    if (!XapiFillInSaveGameData(find, root, root_length)) {
        uint32_t more;
        do {
            more = FindNextFileA(search, find);
        } while (more && !XapiFillInSaveGameData(find, root, root_length));
        if (!more) {
            CloseHandle(search);
            search = 0xffffffff;
            SetLastError(0x12);  // ERROR_NO_MORE_FILES
            return search;
        }
    }
    auto* const handle = ptr<uint32_t>(XMemAlloc(0x110, kXMemAttributes));
    if (!handle) {
        CloseHandle(search);
        SetLastError(8);
        return 0xffffffff;
    }
    handle[0] = 0x53425645;
    handle[1] = search;
    handle[2] = root_length;
    lstrcpynA(reinterpret_cast<char*>(handle + 3), root, 0x104);
    return addr(handle);
}

uint32_t XB_STDCALL XFindNextSaveGame(uint32_t search, uint8_t* find) {
    const auto* const handle = ptr<uint32_t>(search);
    uint32_t more = FindNextFileA(handle[1], find);
    while (more && !XapiFillInSaveGameData(find, reinterpret_cast<const char*>(handle + 3), handle[2]))
        more = FindNextFileA(handle[1], find);
    return more;
}

uint32_t XB_STDCALL XFindClose(uint32_t search) {
    // XAPI's find handles by signature: 'TNOC' / 'EVBS' {+4 handle}, 0xbe4bea00 {+0xc handle}, 'KCIN' (no handle).
    const auto* const handle = ptr<uint32_t>(search);
    uint32_t inner;
    switch (handle[0]) {
    case 0x434f4e54:
    case 0x53425645:
        inner = handle[1];
        break;
    case 0xbe4bea00:
        inner = handle[3];
        break;
    case 0x4e49434b:
        XMemFree(search, kXMemAttributes);
        return 1;
    default:
        return 0;
    }
    XMemFree(search, kXMemAttributes);
    return CloseHandle(inner);
}

uint32_t XB_STDCALL XapiGetDirectoryDisplayBlocks(uint32_t directory, uint32_t* blocks, uint8_t* buffer) {
    // Adds the blocks of every file below the directory (recursing into subdirectories, 64 bytes of directory
    // entry per entry rounded up to a block) to *blocks, capped at kMaxBlocks; files of 4 GB or more give the cap.
    uint32_t restart = 1;
    uint32_t entries = 0;
    uint32_t io_status[2];
    uint32_t status;
    for (;;) {
        status = query_directory(directory, io_status, buffer, restart);
        restart = 0;
        if (failed(status)) break;
        ++entries;
        // FILE_DIRECTORY_INFORMATION: end of file +0x28, attributes +0x38, name length +0x3c, name +0x40.
        if (dword(buffer + 0x38) & 0x10) {
            buffer[0x40 + dword(buffer + 0x3c)] = 0;
            CountedString name;
            init_ansi(&name, reinterpret_cast<const char*>(buffer + 0x40));
            ObjectAttributes attributes{directory, addr(&name), 0x40};
            uint32_t child;
            status = nt_open(&child, 0x100001, &attributes, io_status, 3, 0x4021);
            if (!failed(status)) {
                status = XapiGetDirectoryDisplayBlocks(child, blocks, buffer);
                nt_close(child);
            }
        } else {
            if (dword(buffer + 0x2c)) {
                *blocks = kMaxBlocks;
                break;
            }
            const uint32_t n = blocks_of(dword(buffer + 0x28));
            if (n < kMaxBlocks) *blocks += n;
        }
        if (*blocks >= kMaxBlocks) {
            *blocks = kMaxBlocks;
            break;
        }
        if (failed(status)) break;
    }
    *blocks += blocks_of(entries ? entries << 6 : 1);
    if (*blocks >= kMaxBlocks) *blocks = kMaxBlocks;
    if (status == kNoMoreFiles || status == kNoSuchFile) status = 0;
    return status;
}

namespace {

// XapiGetDirectoryDisplayBlocks over our files (by path): the same sum in the same order. A failure below stops
// the count like the original's.
uint32_t native_directory_blocks(const std::string& directory, uint32_t* blocks) {
    std::vector<files::Entry> entries;
    uint32_t error = files::list(directory.c_str(), &entries);
    if (error) return error;
    uint32_t count = 0;
    for (const files::Entry& entry : entries) {
        ++count;
        if (entry.attributes & 0x10) {
            error = native_directory_blocks(directory + "\\" + entry.name, blocks);
        } else {
            if (entry.size >> 32) {
                *blocks = kMaxBlocks;
                break;
            }
            const uint32_t n = blocks_of(static_cast<uint32_t>(entry.size));
            if (n < kMaxBlocks) *blocks += n;
        }
        if (*blocks >= kMaxBlocks) {
            *blocks = kMaxBlocks;
            break;
        }
        if (error) break;
    }
    *blocks += blocks_of(count ? count << 6 : 1);
    if (*blocks >= kMaxBlocks) *blocks = kMaxBlocks;
    return error;
}

}  // namespace

uint32_t XB_STDCALL XGetDisplayBlocks(const char* path) {
    // A file's size or a directory's contents in 16 KB blocks; 0 with the last error on failure.
    if (native_platform()) {
        files::Entry entry;
        uint32_t error = files::stat(path, &entry);
        if (!error) {
            if (!(entry.attributes & 0x10)) {
                if (entry.size >> 32) return kMaxBlocks;
                const uint32_t blocks = blocks_of(static_cast<uint32_t>(entry.size));
                return blocks > kMaxBlocks ? kMaxBlocks : blocks;
            }
            uint32_t blocks = 0;
            std::string directory(path);
            while (directory.size() > 3 && directory.back() == '\\') directory.pop_back();
            error = native_directory_blocks(directory, &blocks);
            if (!error) return blocks;
        }
        SetLastError(error);
        return 0;
    }
    CountedString name;
    init_ansi(&name, path);
    ObjectAttributes attributes{0xfffffffd, addr(&name), 0x40};
    uint8_t info[0x38];  // FILE_NETWORK_OPEN_INFORMATION: end of file +0x28, attributes +0x30
    uint32_t status = kernel<uint32_t XB_STDCALL(ObjectAttributes*, uint8_t*)>(kNtQueryFullAttributesFile)(
        &attributes, info);
    if (!failed(status)) {
        if (dword(info + 0x30) & 0x10) {
            uint32_t blocks = 0;
            uint32_t handle;
            uint32_t io_status[2];
            status = nt_open(&handle, 0x100001, &attributes, io_status, 3, 0x4021);
            if (!failed(status)) {
                uint8_t buffer[0x148];
                status = XapiGetDirectoryDisplayBlocks(handle, &blocks, buffer);
                nt_close(handle);
                if (!failed(status)) return blocks;
            }
        } else {
            if (dword(info + 0x2c)) return kMaxBlocks;
            const uint32_t blocks = blocks_of(dword(info + 0x28));
            return blocks > kMaxBlocks ? kMaxBlocks : blocks;
        }
    }
    XapiSetLastNTError(status);
    return 0;
}

// --- Strings, directories -----------------------------------------------------------------------------------------

int32_t XB_STDCALL UnicodeToUTF8(const uint16_t* source, int32_t length, char* out, int32_t out_size) {
    // 1 to 3 bytes per character (surrogates encoded one by one); every byte counts, a byte is stored while the
    // count stays within out_size. Too small (out_size != 0): 0 and ERROR_INSUFFICIENT_BUFFER.
    int32_t count = 0;
    if (length == -1) length = static_cast<int32_t>(title_wcslen(source) + 1);
    const auto put = [&](uint32_t byte) {
        ++count;
        if (count <= out_size && out) *out++ = static_cast<char>(byte);
    };
    if (length > 0) {
        for (auto n = static_cast<uint32_t>(length); n; --n, ++source) {
            const uint32_t c = *source;
            if (c <= 0x7f) {
                put(c);
            } else if (c <= 0x7ff) {
                put(((c >> 6) & 0x1f) | 0xc0);
                put((c & 0x3f) | 0x80);
            } else {
                put(((c >> 12) & 0xf) | 0xe0);
                put(((c >> 6) & 0x3f) | 0x80);
                put((c & 0x3f) | 0x80);
            }
        }
    }
    if (out_size && out_size < count) {
        SetLastError(0x7a);
        return 0;
    }
    return count;
}

int32_t XB_STDCALL WideCharToMultiByte(uint32_t code_page, uint32_t, const uint16_t* source, int32_t length,
                                       char* out, int32_t out_size, const char* default_char, uint32_t* used_default) {
    // CP_UTF8 through UnicodeToUTF8; CP_UTF7 and a default character are invalid parameters; anything else is
    // the kernel's ANSI conversion. The count is length + 1 (so a length that already holds the terminator
    // converts one character more) in 16 bits.
    if (code_page == 0xfde8 || (code_page != 0xfde9 && default_char)) {
        SetLastError(0x57);
        return 0;
    }
    if (code_page == 0xfde9) return UnicodeToUTF8(source, length, out, out_size);
    if (used_default) *used_default = 0;
    const uint32_t n = (length == -1 ? title_wcslen(source) : static_cast<uint32_t>(length)) + 1;
    const uint32_t count = n & 0xffff;
    if (!out_size) return static_cast<int32_t>(count);
    if (out_size < static_cast<int32_t>(count)) {
        SetLastError(0x7a);
        return 0;
    }
    CountedString ansi{0, static_cast<uint16_t>(out_size), addr(out)};
    CountedString unicode{static_cast<uint16_t>(n * 2 - 2), static_cast<uint16_t>(n * 2), addr(source)};
    const uint32_t status = kernel<uint32_t XB_STDCALL(CountedString*, CountedString*, uint32_t)>(
        kRtlUnicodeStringToAnsiString)(&ansi, &unicode, 0);
    if (!failed(status)) return static_cast<int32_t>(count);
    SetLastError(dos_error(status));
    return 0;
}

char* XB_STDCALL lstrcpynA(char* dest, const char* source, uint32_t count) {
    // At most count - 1 characters and the terminator (none when count is 0). The original's SEH frame (0 on a
    // fault) is not kept.
    if (count) {
        char* d = dest;
        while (count && *source) {
            *d++ = *source++;
            --count;
        }
        if (!count) --d;
        *d = 0;
    }
    return dest;
}

uint32_t XB_STDCALL CreateDirectoryA(const char* path, uint32_t) {
    if (native_platform()) {
        if (const uint32_t error = files::make_directory(path)) {
            SetLastError(error);
            return 0;
        }
        return 1;
    }
    CountedString name;
    init_ansi(&name, path);
    ObjectAttributes attributes{0xfffffffd, addr(&name), 0x40};
    uint32_t handle;
    uint32_t io_status[2];
    // FILE_LIST_DIRECTORY | SYNCHRONIZE, normal attributes, share read/write, FILE_CREATE,
    // directory + synchronous I/O + open for backup.
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, ObjectAttributes*, uint32_t*, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t)>(kNtCreateFile)(&handle, 0x100001, &attributes, io_status, 0,
                                                                       0x80, 3, 2, 0x4021);
    if (failed(status)) {
        XapiSetLastNTError(status);
        return 0;
    }
    nt_close(handle);
    return 1;
}

uint32_t XB_STDCALL RemoveDirectoryA(const char* path) {
    if (native_platform()) {
        if (const uint32_t error = files::remove_directory(path)) {
            SetLastError(error);
            return 0;
        }
        return 1;
    }
    CountedString name;
    init_ansi(&name, path);
    ObjectAttributes attributes{0xfffffffd, addr(&name), 0x40};
    uint32_t handle;
    uint32_t io_status[2];
    uint32_t status = nt_open(&handle, 0x110000, &attributes, io_status, 7, 0x4021);
    if (failed(status)) {
        XapiSetLastNTError(status);
        return 0;
    }
    uint8_t delete_file = 1;
    status = nt_set_information(handle, io_status, &delete_file, 1, 0xd);
    nt_close(handle);
    if (failed(status)) {
        XapiSetLastNTError(status);
        return 0;
    }
    return 1;
}

uint32_t XB_STDCALL XapiNukeDirectoryFromHandle(uint32_t directory, uint8_t* buffer) {
    // Deletes every entry (read-only files first lose that attribute; subdirectories recursively), then the
    // directory itself. Before that it sets the directory's attributes to those of the last entry the buffer
    // holds, masked (what the original does; the buffer is its caller's).
    uint32_t restart = 1;
    uint32_t io_status[2];
    uint32_t status;
    for (;;) {
        status = query_directory(directory, io_status, buffer, restart);
        restart = 0;
        if (failed(status)) break;
        const uint32_t is_directory = (dword(buffer + 0x38) >> 4) & 1;
        buffer[0x40 + dword(buffer + 0x3c)] = 0;
        CountedString name;
        init_ansi(&name, reinterpret_cast<const char*>(buffer + 0x40));
        ObjectAttributes attributes{directory, addr(&name), 0x40};
        uint32_t entry;
        // DELETE | SYNCHRONIZE (+ FILE_LIST_DIRECTORY), share all, directory or non-directory file,
        // synchronous I/O.
        status = nt_open(&entry, 0x110100 | is_directory, &attributes, io_status, 7,
                         (is_directory ? 0x1 : 0x40) | 0x4000);
        if (failed(status)) break;
        if (is_directory) {
            status = XapiNukeDirectoryFromHandle(entry, buffer);
        } else {
            const uint32_t entry_attributes = dword(buffer + 0x38);
            if (entry_attributes & 1) {
                uint32_t basic[10] = {};
                basic[8] = (entry_attributes & 0x3126) | 0x80;
                status = nt_set_information(entry, io_status, basic, 0x28, 4);
            }
            if (!failed(status)) {
                uint8_t delete_file = 1;
                status = nt_set_information(entry, io_status, &delete_file, 1, 0xd);
            }
        }
        nt_close(entry);
        if (failed(status)) break;
    }
    if (status == kNoMoreFiles || status == kNoSuchFile) {
        uint32_t basic[10] = {};
        basic[8] = (dword(buffer + 0x38) & 0x3126) | 0x80;
        nt_set_information(directory, io_status, basic, 0x28, 4);
        uint8_t delete_file = 1;
        status = nt_set_information(directory, io_status, &delete_file, 1, 0xd);
    }
    return status;
}

uint32_t XB_STDCALL XapiNukeDirectory(const char* path, uint32_t root) {
    // The original's 0x148-byte buffer is uninitialized stack (see XapiNukeDirectoryFromHandle); ours starts zeroed.
    CountedString name;
    init_ansi(&name, path);
    ObjectAttributes attributes{root, addr(&name), 0x40};
    uint32_t handle;
    uint32_t io_status[2];
    uint32_t status = nt_open(&handle, 0x110101, &attributes, io_status, 3, 0x4021);
    if (!failed(status)) {
        uint8_t buffer[0x148] = {};
        status = XapiNukeDirectoryFromHandle(handle, buffer);
        nt_close(handle);
    }
    return status;
}

}  // namespace game
