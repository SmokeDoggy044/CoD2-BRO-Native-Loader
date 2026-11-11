#include "title_xapi.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

TitleClock* g_title_clock = nullptr;

// A kernel export through the title's kernel thunk table: the slot holds the export's address (a function) or the
// address of the exported variable (data exports).
template <typename F>
F* kernel(uint32_t slot) {
    return title_function<F>(title_global<uint32_t>(slot));
}
template <typename T>
T& kernel_data(uint32_t slot) {
    return *reinterpret_cast<T*>(uintptr_t{title_global<uint32_t>(slot)});
}

constexpr uint32_t kExQueryNonVolatileSetting = 0x007b2fe0;
constexpr uint32_t kRtlNtStatusToDosError = 0x007b2ffc;
constexpr uint32_t kNtCreateFile = 0x007b3004;
constexpr uint32_t kRtlInitAnsiString = 0x007b3008;
constexpr uint32_t kNtClose = 0x007b3014;
constexpr uint32_t kNtOpenFile = 0x007b3018;
constexpr uint32_t kNtSetInformationFile = 0x007b3054;
constexpr uint32_t kNtReadFile = 0x007b3058;
constexpr uint32_t kNtWriteFile = 0x007b305c;
constexpr uint32_t kObfDereferenceObject = 0x007b3064;
constexpr uint32_t kObReferenceObjectByHandle = 0x007b306c;
constexpr uint32_t kMmAllocateContiguousMemoryEx = 0x007b30b4;
constexpr uint32_t kMmFreeContiguousMemory = 0x007b30b8;
constexpr uint32_t kMmQueryStatistics = 0x007b30ec;
constexpr uint32_t kNtSetEvent = 0x007b3110;
constexpr uint32_t kLaunchDataPage = 0x007b311c;
constexpr uint32_t kMmSetAddressProtect = 0x007b3124;
constexpr uint32_t kMmQueryAddressProtect = 0x007b3128;
constexpr uint32_t kRtlLeaveCriticalSection = 0x007b312c;
constexpr uint32_t kRtlEnterCriticalSection = 0x007b3130;
constexpr uint32_t kPsTerminateSystemThread = 0x007b313c;
constexpr uint32_t kPsCreateSystemThreadEx = 0x007b3140;
constexpr uint32_t kNtQueryDirectoryFile = 0x007b3144;
constexpr uint32_t kNtQueryInformationFile = 0x007b3158;
constexpr uint32_t kNtWaitForSingleObject = 0x007b315c;
constexpr uint32_t kNtUserIoApcDispatcher = 0x007b3168;
constexpr uint32_t kIoQueryVolumeInformation = 0x007b3170;
constexpr uint32_t kIoFileObjectType = 0x007b3174;
constexpr uint32_t kNtCreateEvent = 0x007b3178;
constexpr uint32_t kNtCreateMutant = 0x007b317c;
constexpr uint32_t kNtReleaseMutant = 0x007b3180;
constexpr uint32_t kNtWaitForSingleObjectEx = 0x007b3184;
constexpr uint32_t kKeDelayExecutionThread = 0x007b3188;
constexpr uint32_t kHalBootSMCVideoMode = 0x007b31d4;

uint32_t addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// ExQueryNonVolatileSetting(index, &type, &value, 4, null) into a dword; false when it fails.
bool query_setting(uint32_t index, uint32_t* value) {
    uint32_t type = 0;
    return static_cast<int32_t>(
               kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t*)>(
                   kExQueryNonVolatileSetting)(index, &type, value, 4, nullptr)) >= 0;
}

// The calling thread's XAPI block (TLS slot *0x009268b0 of the array FS:[4] finds).
uint8_t* xapi_thread_block() {
    const uint32_t index = title_global<uint32_t>(0x009268b0);
    const uint32_t tls = platform_fs_word(4);
    return reinterpret_cast<uint8_t*>(uintptr_t{reinterpret_cast<const uint32_t*>(uintptr_t{tls})[index]});
}

// OBJECT_ATTRIBUTES {RootDirectory, ObjectName, Attributes}: names relative to the object root (-3), case
// insensitive (0x40).
struct ObjectAttributes {
    uint32_t root;
    uint32_t name;
    uint32_t attributes;
};

// ANSI_STRING
struct AnsiString {
    uint16_t length;
    uint16_t maximum;
    uint32_t buffer;
};

}  // namespace

void set_title_clock(TitleClock* clock) { g_title_clock = clock; }

// --- Errors -----------------------------------------------------------------------------------------------------

uint32_t XB_STDCALL GetLastError() { return *reinterpret_cast<uint32_t*>(xapi_thread_block() + 8); }

void XB_STDCALL SetLastError(uint32_t error) { *reinterpret_cast<uint32_t*>(xapi_thread_block() + 8) = error; }

uint32_t XB_STDCALL XapiSetLastNTError(uint32_t status) {
    const uint32_t error = kernel<uint32_t XB_STDCALL(uint32_t)>(kRtlNtStatusToDosError)(status);
    SetLastError(error);
    return error;
}

// --- Console settings, launch data -----------------------------------------------------------------------------

uint32_t XB_STDCALL XGetVideoStandard() {
    // XC_FACTORY_AV_REGION (0x103): the standard is its second byte (1 NTSC-M, 2 NTSC-J, 3 PAL-I).
    uint32_t value = 0;
    if (!query_setting(0x103, &value)) return 0;
    return (value >> 8) & 0xff;
}

uint32_t XB_STDCALL XGetAVPack() { return kernel_data<uint32_t>(kHalBootSMCVideoMode); }

uint32_t XB_STDCALL XGetVideoFlags() {
    // XC_VIDEO_FLAGS (8): the flags in bits 16..22 (0x5f: widescreen, letterbox, 480p/720p/1080i, PAL60). The HD
    // modes (0x0e) only with the HDTV pack (AV pack 1) outside PAL.
    uint32_t flags = 0;
    if (query_setting(8, &flags))
        flags = (flags >> 16) & 0x5f;
    else
        flags = 0;
    if (XGetAVPack() != 1 || XGetVideoStandard() == 3) flags &= 0xfffffff1;
    return flags;
}

uint32_t XB_STDCALL XGetLanguage() {
    // XC_LANGUAGE (7); an unknown language reads as English (1), a failure as 0.
    uint32_t value = 0;
    if (!query_setting(7, &value)) return 0;
    return value > 9 ? 1 : value;
}

uint32_t XB_STDCALL XGetLaunchInfo(uint32_t* type, uint8_t* data) {
    // The LaunchDataPage {type, title id, ..., +0x400: 3 KB of data} the previous title left: only its own title's
    // (or a dashboard/debugger launch, types 2 and 3). Copied out and freed; ERROR_NOT_FOUND (0x490) without one.
    uint32_t& page_word = kernel_data<uint32_t>(kLaunchDataPage);
    const uint32_t* page = reinterpret_cast<const uint32_t*>(uintptr_t{page_word});
    if (!page) return 0x490;
    const uint32_t kind = page[0];
    if (kind != 2 && kind != 3) {
        const uint32_t certificate = title_global<uint32_t>(0x00010118);
        if (page[1] != *reinterpret_cast<const uint32_t*>(uintptr_t{certificate + 8})) return 0x490;
    }
    *type = kind;
    std::memcpy(data, reinterpret_cast<const uint8_t*>(page) + 0x400, 0xc00);
    const uint32_t freed = page_word;
    page_word = 0;
    kernel<void XB_STDCALL(uint32_t)>(kMmFreeContiguousMemory)(freed);
    return 0;
}

// --- Memory -------------------------------------------------------------------------------------------------------

uint32_t XB_STDCALL XPhysicalAlloc(uint32_t size, uint32_t address, uint32_t alignment, uint32_t protect) {
    // A fixed physical address (not -1) asks for exactly [address, address + size) and no alignment.
    uint32_t lowest = 0, highest = 0xffffffff;
    if (address != 0xffffffff) {
        alignment = 0;
        lowest = address;
        highest = size + address - 1;
    }
    const uint32_t result =
        kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(kMmAllocateContiguousMemoryEx)(
            size, lowest, highest, alignment, protect);
    if (!result) SetLastError(8);  // ERROR_NOT_ENOUGH_MEMORY
    return result;
}

uint32_t XB_STDCALL XPhysicalAllocEx(uint32_t size, uint32_t lowest, uint32_t highest, uint32_t alignment,
                                     uint32_t protect) {
    const uint32_t result =
        kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(kMmAllocateContiguousMemoryEx)(
            size, lowest, highest, alignment, protect);
    if (!result) SetLastError(8);  // ERROR_NOT_ENOUGH_MEMORY
    return result;
}

void XB_STDCALL XPhysicalFree(uint32_t address) {
    kernel<void XB_STDCALL(uint32_t)>(kMmFreeContiguousMemory)(address);
}

void XB_STDCALL XPhysicalProtect(uint32_t address, uint32_t size, uint32_t protect) {
    if (size) kernel<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(kMmSetAddressProtect)(address, size, protect);
}

uint32_t XB_STDCALL XQueryMemoryProtect(uint32_t address) {
    return kernel<uint32_t XB_STDCALL(uint32_t)>(kMmQueryAddressProtect)(address);
}

void XB_STDCALL GlobalMemoryStatus(uint32_t* status) {
    // MEMORYSTATUS from MM_STATISTICS {length, total pages, available pages, -, reserved bytes (+0x10), ...}: 2 GB
    // of virtual address space less the reserved bytes; memory load and page file 0.
    uint32_t stats[9];
    stats[0] = 0x24;
    kernel<uint32_t XB_STDCALL(uint32_t*)>(kMmQueryStatistics)(stats);
    status[1] = 0;
    status[4] = 0;
    status[5] = 0;
    status[2] = stats[1] << 12;
    status[3] = stats[2] << 12;
    status[6] = 0x7ffe0000;
    status[0] = 0x20;
    status[7] = 0x7ffe0000 - stats[4];
}

// --- Time -----------------------------------------------------------------------------------------------------------

uint32_t XB_STDCALL QueryPerformanceCounter(uint32_t* counter) {
    // RDTSC (733 MHz, the rate QueryPerformanceFrequency reports): the loader's clock, as at the patched site.
    const uint64_t ticks = static_cast<uint64_t>(g_title_clock());
    counter[0] = static_cast<uint32_t>(ticks);
    counter[1] = static_cast<uint32_t>(ticks >> 32);
    return 1;
}

int32_t* XB_STDCALL XapiFormatTimeOut(int32_t* timeout, uint32_t ms) {
    // A relative kernel timeout (negative 100 ns units); null (wait forever) for INFINITE.
    if (ms == 0xffffffff) return nullptr;
    const uint64_t units = uint64_t{ms} * 10000;
    const uint64_t relative = 0 - units;
    timeout[0] = static_cast<int32_t>(static_cast<uint32_t>(relative));
    timeout[1] = static_cast<int32_t>(static_cast<uint32_t>(relative >> 32));
    return timeout;
}

uint32_t XB_STDCALL SleepEx(uint32_t ms, uint32_t alertable) {
    // KeDelayExecutionThread(UserMode, alertable, timeout), repeated while alerted; WAIT_IO_COMPLETION (0xc0, the
    // same number as STATUS_USER_APC) when an APC ran, else 0. INFINITE waits the longest relative time.
    int32_t interval[2];
    int32_t* timeout = XapiFormatTimeOut(interval, ms);
    if (!timeout) {
        interval[0] = 0;
        interval[1] = static_cast<int32_t>(0x80000000u);
        timeout = interval;
    }
    uint32_t status;
    do {
        status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, int32_t*)>(kKeDelayExecutionThread)(1, alertable,
                                                                                                    timeout);
    } while (alertable && status == 0x101);
    return status == 0xc0 ? 0xc0 : 0;
}

// --- Handles, files, directories ----------------------------------------------------------------------------------

uint32_t XB_STDCALL CloseHandle(uint32_t handle) {
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t)>(kNtClose)(handle);
    if (static_cast<int32_t>(status) >= 0) return 1;
    XapiSetLastNTError(status);
    return 0;
}

void XB_STDCALL XapiDirectoryInformationToFindData(const uint8_t* info, uint8_t* find) {
    // FILE_DIRECTORY_INFORMATION (attributes +0x38, times +8.., end of file +0x28, name length +0x3c, name +0x40)
    // to WIN32_FIND_DATAA (attributes, 3 file times, size high/low, name +0x2c, alternate name +0x130 empty).
    const auto word = [](const uint8_t* base, uint32_t offset) {
        uint32_t w;
        std::memcpy(&w, base + offset, 4);
        return w;
    };
    const auto put = [](uint8_t* base, uint32_t offset, uint32_t w) { std::memcpy(base + offset, &w, 4); };
    put(find, 0x00, word(info, 0x38));
    put(find, 0x04, word(info, 0x08));
    put(find, 0x08, word(info, 0x0c));
    put(find, 0x0c, word(info, 0x10));
    put(find, 0x10, word(info, 0x14));
    put(find, 0x14, word(info, 0x18));
    put(find, 0x18, word(info, 0x1c));
    put(find, 0x1c, word(info, 0x2c));
    put(find, 0x20, word(info, 0x28));
    const uint32_t length = word(info, 0x3c);
    std::memcpy(find + 0x2c, info + 0x40, length);
    find[word(info, 0x3c) + 0x2c] = 0;
    find[0x130] = 0;
}

uint32_t XB_STDCALL FindFirstFileA(const char* path, uint8_t* find) {
    // Splits "dir\mask" at the last backslash, opens the directory (relative to the object root) and asks for the
    // first entry matching the mask ("*.*" = all). The handle goes to the path argument's slot, as in the original.
    AnsiString name;
    kernel<void XB_STDCALL(AnsiString*, const char*)>(kRtlInitAnsiString)(&name, path);
    const char* const buffer = reinterpret_cast<const char*>(uintptr_t{name.buffer});
    ObjectAttributes attributes;
    attributes.name = addr(&name);
    attributes.root = 0xfffffffd;
    attributes.attributes = 0x40;
    uint32_t split = name.length;
    while (split > 0 && buffer[split - 1] != '\\') --split;
    AnsiString mask;
    mask.length = mask.maximum = static_cast<uint16_t>(name.length - split);
    mask.buffer = addr(buffer + split);
    name.length = name.maximum = static_cast<uint16_t>(split);
    if (split == 0 || mask.length == 0) {
        SetLastError(0x57);  // ERROR_INVALID_PARAMETER
        return 0xffffffff;
    }
    if (mask.length == 3 && buffer[split] == '*' && buffer[split + 1] == '.' && buffer[split + 2] == '*')
        mask.length = 0;
    uint32_t* const handle = reinterpret_cast<uint32_t*>(&path);
    uint32_t io_status[2];
    uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, ObjectAttributes*, uint32_t*, uint32_t, uint32_t)>(
        kNtOpenFile)(handle, 0x100001, &attributes, io_status, 3, 0x4021);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0xffffffff;
    }
    uint8_t info[0x148];
    status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint8_t*, uint32_t, uint32_t,
                                        AnsiString*, uint32_t)>(kNtQueryDirectoryFile)(*handle, 0, 0, 0, io_status, info,
                                                                                       0x148, 1, &mask, 0);
    if (static_cast<int32_t>(status) < 0) {
        kernel<uint32_t XB_STDCALL(uint32_t)>(kNtClose)(*handle);
        XapiSetLastNTError(status);
        return 0xffffffff;
    }
    XapiDirectoryInformationToFindData(info, find);
    return *handle;
}

uint32_t XB_STDCALL FindNextFileA(uint32_t handle, uint8_t* find) {
    uint32_t io_status[2];
    uint8_t info[0x148];
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint8_t*, uint32_t, uint32_t,
                                   AnsiString*, uint32_t)>(kNtQueryDirectoryFile)(handle, 0, 0, 0, io_status, info, 0x148,
                                                                                  1, nullptr, 0);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    XapiDirectoryInformationToFindData(info, find);
    return 1;
}

uint32_t XB_STDCALL GetFileSizeEx(uint32_t handle, uint32_t* size) {
    // FileNetworkOpenInformation (0x22, 0x38 bytes): end of file at +0x28.
    uint32_t io_status[2];
    uint32_t info[0x38 / 4];
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t)>(
        kNtQueryInformationFile)(handle, io_status, info, 0x38, 0x22);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    size[0] = info[10];
    size[1] = info[11];
    return 1;
}

uint32_t XB_STDCALL GetFileSize(uint32_t handle, uint32_t* high) {
    // INVALID_FILE_SIZE on failure; a real size of 0xffffffff clears the last error so callers can tell.
    uint32_t size[2];
    if (!GetFileSizeEx(handle, size)) {
        size[0] = 0xffffffff;
    } else {
        if (high) *high = size[1];
        if (size[0] == 0xffffffff) SetLastError(0);
    }
    return size[0];
}

uint32_t XB_STDCALL ReadFileEx(uint32_t handle, void* buffer, uint32_t size, uint32_t* overlapped,
                               uint32_t completion) {
    // NtReadFile at the OVERLAPPED's offset (+8), its first two words the status block; the completion routine runs
    // as an APC through the kernel's NtUserIoApcDispatcher.
    uint32_t offset[2] = {overlapped[2], overlapped[3]};
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, void*, uint32_t, uint32_t*)>(
            kNtReadFile)(handle, 0, title_global<uint32_t>(kNtUserIoApcDispatcher), completion, overlapped, buffer, size,
                         offset);
    if ((status & 0xc0000000) == 0xc0000000) {
        XapiSetLastNTError(status);
        return 0;
    }
    return 1;
}

uint32_t XB_STDCALL WriteFileEx(uint32_t handle, const void* buffer, uint32_t size, uint32_t* overlapped,
                                uint32_t completion) {
    uint32_t offset[2] = {overlapped[2], overlapped[3]};
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, const void*, uint32_t, uint32_t*)>(
            kNtWriteFile)(handle, 0, title_global<uint32_t>(kNtUserIoApcDispatcher), completion, overlapped, buffer,
                          size, offset);
    if ((status & 0xc0000000) == 0xc0000000) {
        XapiSetLastNTError(status);
        return 0;
    }
    return 1;
}

uint32_t XB_STDCALL XGetFilePhysicalSortKey(uint32_t handle) {
    // The file's place on its volume for request ordering: FATX the file control block's first cluster (+0x1c; 0
    // for a directory, flag bit 0), GDFX (DVD) its first sector (+0). -1 on failure; other file systems fail with
    // STATUS_INVALID_PARAMETER. The file object takes the handle argument's slot, as in the original.
    uint32_t key = 0xffffffff;
    uint32_t* const object = &handle;
    uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t*)>(kObReferenceObjectByHandle)(
        handle, title_global<uint32_t>(kIoFileObjectType), object);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return key;
    }
    const auto dereference = [&] { kernel<void XB_FASTCALL(uint32_t)>(kObfDereferenceObject)(*object); };
    uint32_t info[8];
    uint32_t returned;
    status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*)>(kIoQueryVolumeInformation)(
        *object, 5, 0x20, info, &returned);
    if (static_cast<int32_t>(status) < 0) {
        dereference();
        XapiSetLastNTError(status);
        return key;
    }
    // FILE_FS_ATTRIBUTE_INFORMATION: name length +8, name +0xc.
    const uint8_t* const control = reinterpret_cast<const uint8_t*>(
        uintptr_t{*reinterpret_cast<const uint32_t*>(uintptr_t{*object + 8})});
    if (info[2] == 4 && info[3] == 0x58544146) {  // "FATX"
        key = (control[0] & 1) ? 0 : *reinterpret_cast<const uint32_t*>(control + 0x1c);
    } else if (info[2] == 4 && info[3] == 0x58464447) {  // "GDFX"
        key = *reinterpret_cast<const uint32_t*>(control);
    } else {
        XapiSetLastNTError(0xc000000d);
    }
    dereference();
    return key;
}

uint32_t XB_STDCALL CreateFileA(const char* path, uint32_t access, uint32_t share, uint32_t security,
                                uint32_t disposition, uint32_t flags, uint32_t template_file) {
    // NtCreateFile relative to the object root; the Win32 disposition (1..5) to FILE_CREATE, FILE_OVERWRITE_IF,
    // FILE_OPEN, FILE_OPEN_IF, FILE_OVERWRITE (TRUNCATE_EXISTING needs GENERIC_WRITE); the FILE_FLAG_* bits to create
    // options. Last error 0, or ERROR_ALREADY_EXISTS (0xb7) when CREATE_ALWAYS overwrote / OPEN_ALWAYS opened one.
    (void)security;
    (void)template_file;
    uint32_t create;
    switch (disposition) {
    case 1: create = 2; break;
    case 2: create = 5; break;
    case 3: create = 1; break;
    case 4: create = 3; break;
    case 5:
        if (access & 0x40000000) {
            create = 4;
            break;
        }
        [[fallthrough]];
    default:
        XapiSetLastNTError(0xc000000d);  // STATUS_INVALID_PARAMETER
        return 0xffffffff;
    }
    AnsiString name;
    kernel<void XB_STDCALL(AnsiString*, const char*)>(kRtlInitAnsiString)(&name, path);
    const bool trailing_backslash = name.length > 1 && path[name.length - 1] == '\\';
    ObjectAttributes attributes;
    attributes.name = addr(&name);
    // FILE_FLAG_WRITE_THROUGH -> FILE_WRITE_THROUGH (2), NO_BUFFERING -> FILE_NO_INTERMEDIATE_BUFFERING (8),
    // SEQUENTIAL_SCAN -> FILE_SEQUENTIAL_ONLY (4), RANDOM_ACCESS -> FILE_RANDOM_ACCESS (0x800), BACKUP_SEMANTICS ->
    // FILE_OPEN_FOR_BACKUP_INTENT (0x4000); without OVERLAPPED FILE_SYNCHRONOUS_IO_NONALERT (0x20).
    uint32_t options = (flags >> 4) & 0x8000000;
    options = (options | (flags & 0x20000000)) >> 1;
    options = (options | (flags & 0x8000000)) >> 8;
    options = (options | (flags & 0x10000000)) >> 6;
    const uint32_t backup = flags & 0x2000000;
    options = ((options | backup) >> 11) | (~(flags >> 25) & 0x20);
    attributes.root = 0xfffffffd;
    attributes.attributes = 0x40;
    if (flags & 0x4000000) {  // FILE_FLAG_DELETE_ON_CLOSE: FILE_DELETE_ON_CLOSE, DELETE access
        options |= 0x1000;
        access |= 0x10000;
    }
    if (!backup) options |= 0x40;  // FILE_NON_DIRECTORY_FILE
    uint32_t handle;
    uint32_t io_status[2];
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, ObjectAttributes*, uint32_t*, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t)>(kNtCreateFile)(&handle, access | 0x100080, &attributes,
                                                                       io_status, 0, flags & 0x7fa7, share, create,
                                                                       options);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        if (status == 0xc0000035)  // STATUS_OBJECT_NAME_COLLISION
            SetLastError(0x50);    // ERROR_FILE_EXISTS
        else if (status == 0xc00000ba)  // STATUS_FILE_IS_A_DIRECTORY
            SetLastError(trailing_backslash ? 3 : 5);
        return 0xffffffff;
    }
    if ((disposition == 2 && io_status[1] == 3) || (disposition == 4 && io_status[1] == 1))
        SetLastError(0xb7);
    else
        SetLastError(0);
    return handle;
}

uint32_t XB_STDCALL ReadFile(uint32_t handle, void* buffer, uint32_t size, uint32_t* read, uint32_t* overlapped) {
    // With an OVERLAPPED {status block, offset +8, event +0x10}: started at its offset, the status block marked
    // pending; completion through its event, or an APC with the OVERLAPPED unless the event's low bit is set.
    // Pending reports ERROR_IO_PENDING. Synchronous: waits on the handle; the end of the file reads 0 bytes.
    if (read) *read = 0;
    if (overlapped) {
        uint32_t offset[2] = {overlapped[2], overlapped[3]};
        const uint32_t event = overlapped[4];
        overlapped[0] = 0x103;
        const uint32_t context = (event & 1) ? 0 : addr(overlapped);
        const uint32_t status =
            kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, void*, uint32_t, uint32_t*)>(
                kNtReadFile)(handle, event, 0, context, overlapped, buffer, size, offset);
        if (static_cast<int32_t>(status) >= 0 && status != 0x103) {
            if (read) *read = overlapped[1];
            return 1;
        }
        if (status == 0xc0000011) {  // STATUS_END_OF_FILE
            if (read) *read = 0;
        }
        XapiSetLastNTError(status);
        return 0;
    }
    uint32_t io_status[2];
    uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, void*, uint32_t, uint32_t*)>(
            kNtReadFile)(handle, 0, 0, 0, io_status, buffer, size, nullptr);
    if (status == 0x103) {
        status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t)>(kNtWaitForSingleObject)(handle, 0, 0);
        if (static_cast<int32_t>(status) >= 0) status = io_status[0];
    }
    if (static_cast<int32_t>(status) >= 0) {
        *read = io_status[1];
        return 1;
    }
    if (status == 0xc0000011) {
        *read = 0;
        return 1;
    }
    if ((status & 0xc0000000) == 0x80000000) *read = io_status[1];
    XapiSetLastNTError(status);
    return 0;
}

uint32_t XB_STDCALL WriteFile(uint32_t handle, const void* buffer, uint32_t size, uint32_t* written,
                              uint32_t* overlapped) {
    if (written) *written = 0;
    if (overlapped) {
        uint32_t offset[2] = {overlapped[2], overlapped[3]};
        const uint32_t event = overlapped[4];
        overlapped[0] = 0x103;
        const uint32_t context = (event & 1) ? 0 : addr(overlapped);
        const uint32_t status =
            kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, const void*, uint32_t,
                                       uint32_t*)>(kNtWriteFile)(handle, event, 0, context, overlapped, buffer, size,
                                                                 offset);
        if ((status & 0xc0000000) != 0xc0000000 && status != 0x103) {
            if (written) *written = overlapped[1];
            return 1;
        }
        XapiSetLastNTError(status);
        return 0;
    }
    uint32_t io_status[2];
    uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, const void*, uint32_t, uint32_t*)>(
            kNtWriteFile)(handle, 0, 0, 0, io_status, buffer, size, nullptr);
    if (status == 0x103) {
        status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t)>(kNtWaitForSingleObject)(handle, 0, 0);
        if (static_cast<int32_t>(status) >= 0) status = io_status[0];
    }
    if (static_cast<int32_t>(status) >= 0) {
        *written = io_status[1];
        return 1;
    }
    if ((status & 0xc0000000) == 0x80000000) *written = io_status[1];
    XapiSetLastNTError(status);
    return 0;
}

namespace {

// The file's position (FilePositionInformation 0xe) or end (FileNetworkOpenInformation 0x22, +0x28) plus
// `distance` for a seek `method` (0 begin, 2 end, anything else current); false (last error set) when the query
// fails.
bool seek_target(uint32_t handle, uint32_t method, uint32_t distance_low, uint32_t distance_high, uint32_t* target) {
    using Query = uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t);
    const uint64_t distance = (uint64_t{distance_high} << 32) | distance_low;
    uint32_t io_status[2];
    uint64_t base = 0;
    if (method == 0) {
        base = 0;
    } else if (method == 2) {
        uint32_t info[0x38 / 4];
        const uint32_t status = kernel<Query>(kNtQueryInformationFile)(handle, io_status, info, 0x38, 0x22);
        if (static_cast<int32_t>(status) < 0) {
            XapiSetLastNTError(status);
            return false;
        }
        base = (uint64_t{info[11]} << 32) | info[10];
    } else {
        uint32_t position[2];
        const uint32_t status = kernel<Query>(kNtQueryInformationFile)(handle, io_status, position, 8, 0xe);
        if (static_cast<int32_t>(status) < 0) {
            XapiSetLastNTError(status);
            return false;
        }
        base = (uint64_t{position[1]} << 32) | position[0];
    }
    const uint64_t sum = base + distance;
    target[0] = static_cast<uint32_t>(sum);
    target[1] = static_cast<uint32_t>(sum >> 32);
    return true;
}

}  // namespace

uint32_t XB_STDCALL SetFilePointer(uint32_t handle, uint32_t distance, uint32_t* distance_high, uint32_t method) {
    // Without distance_high the distance is signed 32-bit and the result must stay below 2 GB
    // (ERROR_INVALID_PARAMETER); a negative position is ERROR_NEGATIVE_SEEK (0x83). Returns the position's low word
    // (its high word to *distance_high), -1 on failure; a real low word of -1 clears the last error.
    const uint32_t high = distance_high ? *distance_high : (static_cast<int32_t>(distance) < 0 ? 0xffffffff : 0);
    uint32_t position[2];
    if (!seek_target(handle, method, distance, high, position)) return 0xffffffff;
    if (static_cast<int32_t>(position[1]) < 0) {
        SetLastError(0x83);
        return 0xffffffff;
    }
    if (!distance_high && (position[1] & 0x7fffffff)) {
        SetLastError(0x57);
        return 0xffffffff;
    }
    uint32_t io_status[2];
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t)>(
        kNtSetInformationFile)(handle, io_status, position, 8, 0xe);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        if (distance_high) *distance_high = 0xffffffff;
        return 0xffffffff;
    }
    if (distance_high) *distance_high = position[1];
    if (position[0] == 0xffffffff) SetLastError(0);
    return position[0];
}

uint32_t XB_STDCALL SetFilePointerEx(uint32_t handle, uint32_t distance, uint32_t distance_high, uint32_t* position,
                                     uint32_t method) {
    uint32_t target[2];
    if (!seek_target(handle, method, distance, distance_high, target)) return 0;
    if (static_cast<int32_t>(target[1]) < 0) {
        SetLastError(0x83);
        return 0;
    }
    uint32_t io_status[2];
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t)>(
        kNtSetInformationFile)(handle, io_status, target, 8, 0xe);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    if (position) {
        position[0] = target[0];
        position[1] = target[1];
    }
    return 1;
}

uint32_t XB_STDCALL SetEndOfFile(uint32_t handle) {
    // End of file (FileEndOfFileInformation 0x14) and allocation (0x13) at the current position.
    using Info = uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t);
    uint32_t io_status[2];
    uint32_t position[2];
    uint32_t status = kernel<Info>(kNtQueryInformationFile)(handle, io_status, position, 8, 0xe);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    uint32_t end[2] = {position[0], position[1]};
    status = kernel<Info>(kNtSetInformationFile)(handle, io_status, end, 8, 0x14);
    if (static_cast<int32_t>(status) >= 0) {
        uint32_t allocation[2] = {position[0], position[1]};
        status = kernel<Info>(kNtSetInformationFile)(handle, io_status, allocation, 8, 0x13);
        if (static_cast<int32_t>(status) >= 0) return 1;
    }
    XapiSetLastNTError(status);
    return 0;
}

uint32_t XB_STDCALL GetOverlappedResult(uint32_t handle, uint32_t* overlapped, uint32_t* transferred, uint32_t wait) {
    // Still pending: waits on the OVERLAPPED's event (or the file) when asked, else ERROR_IO_INCOMPLETE (0x3e4).
    if (overlapped[0] == 0x103) {
        uint32_t result = 0x102;  // WAIT_TIMEOUT
        if (wait) result = WaitForSingleObject(overlapped[4] ? overlapped[4] : handle, 0xffffffff);
        if (result == 0x102) {
            SetLastError(0x3e4);
            return 0;
        }
        if (result != 0) return 0;
    }
    *transferred = overlapped[1];
    const uint32_t status = overlapped[0];
    if (static_cast<int32_t>(status) >= 0) return 1;
    XapiSetLastNTError(status);
    return 0;
}

// --- Synchronization ----------------------------------------------------------------------------------------------

uint32_t XB_STDCALL WaitForSingleObjectEx(uint32_t handle, uint32_t ms, uint32_t alertable) {
    int32_t interval[2];
    int32_t* const timeout = XapiFormatTimeOut(interval, ms);
    uint32_t status;
    for (;;) {
        status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, int32_t*)>(kNtWaitForSingleObjectEx)(
            handle, 1, alertable, timeout);
        if (static_cast<int32_t>(status) < 0) {
            XapiSetLastNTError(status);
            return 0xffffffff;  // WAIT_FAILED
        }
        if (!alertable || status != 0x101) return status;
    }
}

uint32_t XB_STDCALL WaitForSingleObject(uint32_t handle, uint32_t ms) { return WaitForSingleObjectEx(handle, ms, 0); }

uint32_t XB_STDCALL SetEvent(uint32_t handle) {
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(kNtSetEvent)(handle, nullptr);
    if (static_cast<int32_t>(status) >= 0) return 1;
    XapiSetLastNTError(status);
    return 0;
}

uint32_t XB_STDCALL ReleaseMutex(uint32_t handle) {
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(kNtReleaseMutant)(handle, nullptr);
    if (static_cast<int32_t>(status) >= 0) return 1;
    XapiSetLastNTError(status);
    return 0;
}

uint32_t XB_STDCALL SetUnhandledExceptionFilter(uint32_t filter) {
    const uint32_t previous = title_global<uint32_t>(0x0098f664);
    title_global<uint32_t>(0x0098f664) = filter;
    return previous;
}

uint32_t* XB_STDCALL XapiFormatObjectAttributes(uint32_t* attributes, uint32_t* name, const char* text) {
    // A named object under \BaseNamedObjects (root -4), opened if it exists (OBJ_OPENIF 0x80).
    kernel<void XB_STDCALL(uint32_t*, const char*)>(kRtlInitAnsiString)(name, text);
    attributes[1] = addr(name);
    attributes[0] = 0xfffffffc;
    attributes[2] = 0x80;
    return attributes;
}

uint32_t XB_STDCALL CreateEventA(uint32_t security, uint32_t manual_reset, uint32_t initial_state, const char* name) {
    // NotificationEvent (0) for manual reset, else SynchronizationEvent (1); ERROR_ALREADY_EXISTS for an existing
    // named one (STATUS_OBJECT_NAME_EXISTS 0x40000000), else last error 0. 0 on failure.
    (void)security;
    uint32_t attributes[3];
    uint32_t text[2];
    uint32_t* const object = name ? XapiFormatObjectAttributes(attributes, text, name) : nullptr;
    uint32_t handle;
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t*, uint32_t, uint32_t)>(kNtCreateEvent)(
        &handle, object, manual_reset == 0 ? 1 : 0, initial_state);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    SetLastError(status == 0x40000000 ? 0xb7 : 0);
    return handle;
}

uint32_t XB_STDCALL CreateMutexA(uint32_t security, uint32_t initial_owner, const char* name) {
    (void)security;
    uint32_t attributes[3];
    uint32_t text[2];
    uint32_t* const object = name ? XapiFormatObjectAttributes(attributes, text, name) : nullptr;
    uint32_t handle;
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t*, uint32_t)>(kNtCreateMutant)(&handle, object, initial_owner);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    SetLastError(status == 0x40000000 ? 0xb7 : 0);
    return handle;
}

// --- Threads ------------------------------------------------------------------------------------------------------

uint32_t XB_STDCALL CreateThread(uint32_t attributes, uint32_t stack_size, uint32_t start, uint32_t parameter,
                                 uint32_t flags, uint32_t* thread_id) {
    // A kernel thread that starts in XapiThreadStartup (TLS, CRT notifications) with the XBE's default stack
    // (header +0x130) when none is given and the title's TLS size (0x0098f668); CREATE_SUSPENDED is flag 4. The
    // handle goes to the stack size argument's slot, as in the original (which returns that slot even when the
    // thread was not created on a thread without XAPI TLS).
    (void)attributes;
    const uint32_t stack = stack_size ? stack_size : title_global<uint32_t>(0x00010130);
    uint32_t* const handle = &stack_size;
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t)>(kPsCreateSystemThreadEx)(
            handle, 0, stack, title_global<uint32_t>(0x0098f668), thread_id, start, parameter,
            (flags >> 2) & 0xffffff01u, 0, own_address(&XapiThreadStartup));
    if (static_cast<int32_t>(status) < 0) {
        const uint32_t irql = platform_fs_word(0x24) & 0xff;
        const uint32_t kthread = platform_fs_word(0x28);
        if (irql < 2 && *reinterpret_cast<const uint32_t*>(uintptr_t{kthread + 0x28}) != 0) {
            XapiSetLastNTError(status);
            return 0;
        }
    }
    return *handle;
}

void XB_STDCALL XapiCallThreadNotifyRoutines(uint32_t create) {
    // The XRegisterThreadNotifyRoutine list (head 0x008cfd04, entries {next, previous, routine}) under its lock
    // (0x008cfce8); each routine(create) may unlink itself.
    constexpr uint32_t kLock = 0x008cfce8;
    constexpr uint32_t kHead = 0x008cfd04;
    kernel<void XB_STDCALL(uint32_t)>(kRtlEnterCriticalSection)(kLock);
    uint32_t entry = title_global<uint32_t>(kHead);
    while (entry != kHead) {
        const uint32_t* const node = reinterpret_cast<const uint32_t*>(uintptr_t{entry});
        entry = node[0];
        title_function<void XB_STDCALL(uint32_t)>(node[2])(create);
    }
    kernel<void XB_STDCALL(uint32_t)>(kRtlLeaveCriticalSection)(kLock);
}

void XB_STDCALL XapiThreadStartup(uint32_t start, uint32_t parameter) {
    // The thread's TLS block (KTHREAD +0x28): its first word points past itself to the copy of the title's TLS
    // template (directory 0x0082740c: raw data begin, end, ..., zero fill +0x10). Then the notify routines (the
    // CRT's per-thread data), the start routine, the routines again, and the thread ends with its result. The
    // original wraps this in an SEH frame whose filter is UnhandledExceptionFilter; the loader's crash handler takes
    // that part (no title thread raises exceptions it does not handle itself).
    const uint32_t kthread = platform_fs_word(0x28);
    const uint32_t tls = *reinterpret_cast<const uint32_t*>(uintptr_t{kthread + 0x28});
    uint8_t* const data = reinterpret_cast<uint8_t*>(uintptr_t{tls + 4});
    *reinterpret_cast<uint32_t*>(uintptr_t{tls}) = tls + 4;
    const uint32_t raw_begin = title_global<uint32_t>(0x0082740c);
    const uint32_t raw_size = title_global<uint32_t>(0x00827410) - raw_begin;
    std::memcpy(data, reinterpret_cast<const void*>(uintptr_t{raw_begin}), raw_size);
    const uint32_t zero_fill = title_global<uint32_t>(0x0082741c);
    if (zero_fill) std::memset(data + raw_size, 0, zero_fill);
    XapiCallThreadNotifyRoutines(1);
    const uint32_t result = title_function<uint32_t XB_STDCALL(uint32_t)>(start)(parameter);
    XapiCallThreadNotifyRoutines(0);
    kernel<void XB_STDCALL(uint32_t)>(kPsTerminateSystemThread)(result);
}

}  // namespace game
