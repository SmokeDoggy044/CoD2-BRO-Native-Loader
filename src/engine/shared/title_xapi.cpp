#include "title_xapi.h"

#include <cstring>

#include "civil_time.h"

#include "title_data.h"
#include "title_files.h"
#include "title_host.h"
#include "title_objects.h"

namespace game {

namespace {

TitleClock* g_title_clock = nullptr;
TitleFsMap* g_title_fs_map = nullptr;

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
constexpr uint32_t kKeQuerySystemTime = 0x007b3080;
constexpr uint32_t kRtlTimeFieldsToTime = 0x007b30f0;
constexpr uint32_t kRtlTimeToTimeFields = 0x007b30f4;
constexpr uint32_t kNtQueryVolumeInformationFile = 0x007b3160;
constexpr uint32_t kRtlAnsiStringToUnicodeString = 0x007b31b0;

uint32_t addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// The console settings the title reads (XC_* values), in the 256-byte layout its code parses for the whole
// blocks (0xff the user section with the time zone, 0xffff everything): built from what the player chose
// (TitleHost language, widescreen) - North America, NTSC-M, UTC without daylight time, nothing else set.
struct SettingSlot {
    uint32_t index, type, offset, size;  // type 3 REG_BINARY, 4 REG_DWORD
};
constexpr SettingSlot kSettingSlots[] = {
    {0x00, 4, 0x64, 4}, {0x01, 3, 0x68, 4}, {0x02, 4, 0x78, 4}, {0x03, 4, 0x88, 4}, {0x04, 3, 0x6c, 4},
    {0x05, 4, 0x7c, 4}, {0x06, 4, 0x8c, 4}, {0x07, 4, 0x90, 4}, {0x08, 4, 0x94, 4}, {0x09, 4, 0x98, 4},
    {0x0a, 4, 0x9c, 4}, {0x0b, 4, 0xa0, 4}, {0x0c, 4, 0xa4, 4}, {0x0d, 4, 0xa8, 4}, {0x0e, 4, 0xac, 4},
    {0x0f, 4, 0xb0, 4}, {0x10, 4, 0xb4, 4}, {0x11, 4, 0xb8, 4}, {0x12, 4, 0xbc, 4}, {0xff, 3, 0x60, 0x60},
    {0x100, 3, 0x34, 12}, {0x101, 3, 0x40, 6}, {0x102, 3, 0x48, 16}, {0x103, 4, 0x58, 4}, {0x104, 4, 0x2c, 4},
    {0xffff, 3, 0x00, 0x100},
};

// The console's save-signing key (XboxHDKey): the one the loader always used, so the player's saves keep loading.
constexpr uint8_t kConsoleKey[16] = {'C', 'o', 'D', '2', 'B', 'R', 'O', '-', 'p', 'o', 'r', 't', '-', 'k', 'e', 'y'};

void build_settings(uint8_t* image) {
    std::memset(image, 0, 0x100);
    const auto put = [&](uint32_t offset, uint32_t value) { std::memcpy(image + offset, &value, 4); };
    std::memcpy(image + 0x1c, kConsoleKey, sizeof(kConsoleKey));
    put(0x2c, 1);  // game region North America
    std::memcpy(image + 0x34, "000000000000", 12);
    const uint8_t mac[6] = {0x00, 0x50, 0xf2, 0x00, 0x00, 0x01};
    std::memcpy(image + 0x40, mac, sizeof(mac));
    put(0x58, 0x00400100);  // AV region NTSC-M
    put(0x90, title_host().language);
    put(0x94, title_host().widescreen ? 0x00010000u : 0u);  // XC_VIDEO_FLAGS_WIDESCREEN in the high word
    put(0xbc, 1);  // DVD region 1
}

// A console setting: ExQueryNonVolatileSetting's statuses (0, STATUS_OBJECT_NAME_NOT_FOUND, STATUS_BUFFER_TOO_SMALL).
uint32_t native_setting(uint32_t index, uint32_t* type, void* value, uint32_t size, uint32_t* result_size) {
    for (const SettingSlot& slot : kSettingSlots) {
        if (slot.index != index) continue;
        if (type) *type = slot.type;
        if (result_size) *result_size = slot.size;
        if (size < slot.size) return 0xc0000023;
        uint8_t image[0x100];
        build_settings(image);
        std::memcpy(value, image + slot.offset, slot.size);
        return 0;
    }
    return 0xc0000034;
}

uint32_t query_value(uint32_t index, uint32_t* type, void* value, uint32_t size, uint32_t* result_size) {
    if (native_platform()) return native_setting(index, type, value, size, result_size);
    return kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, void*, uint32_t, uint32_t*)>(kExQueryNonVolatileSetting)(
        index, type, value, size, result_size);
}

// A dword setting; false when it fails.
bool query_setting(uint32_t index, uint32_t* value) {
    uint32_t type = 0;
    return static_cast<int32_t>(query_value(index, &type, value, 4, nullptr)) >= 0;
}

// Time: the host's clock and our own calendar (civil_time.h) under --own-main, the kernel's exports otherwise.
void system_time(uint32_t* time) {
    if (!native_platform()) return kernel<void XB_STDCALL(uint32_t*)>(kKeQuerySystemTime)(time);
    const int64_t now = title_host().system_time();
    time[0] = static_cast<uint32_t>(static_cast<uint64_t>(now));
    time[1] = static_cast<uint32_t>(static_cast<uint64_t>(now) >> 32);
}
void time_to_fields(uint32_t* time, uint16_t* fields) {
    if (!native_platform()) return kernel<void XB_STDCALL(uint32_t*, uint16_t*)>(kRtlTimeToTimeFields)(time, fields);
    const platform::TimeFields result =
        platform::time_to_fields(static_cast<int64_t>(uint64_t{time[1]} << 32 | time[0]));
    std::memcpy(fields, &result, sizeof(result));
}
bool fields_to_time(uint16_t* fields, uint32_t* time) {
    if (!native_platform())
        return kernel<uint8_t XB_STDCALL(uint16_t*, uint32_t*)>(kRtlTimeFieldsToTime)(fields, time) != 0;
    platform::TimeFields in;
    std::memcpy(&in, fields, sizeof(in));
    int64_t result;
    if (!platform::fields_to_time(in, &result)) return false;
    time[0] = static_cast<uint32_t>(static_cast<uint64_t>(result));
    time[1] = static_cast<uint32_t>(static_cast<uint64_t>(result) >> 32);
    return true;
}

// The calling thread's XAPI block (TLS slot *0x009268b0 of the array FS:[4] finds).
uint8_t* xapi_thread_block() {
    const uint32_t index = title_global<uint32_t>(0x009268b0);
    const uint32_t tls = title_fs_word(4);
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

void set_title_fs_map(TitleFsMap* map) { g_title_fs_map = map; }

uint32_t title_fs_word(uint32_t kpcr_offset) {
    return platform_fs_word(g_title_fs_map ? g_title_fs_map(kpcr_offset) : kpcr_offset);
}

// --- Errors -----------------------------------------------------------------------------------------------------

uint32_t XB_STDCALL GetLastError() { return *reinterpret_cast<uint32_t*>(xapi_thread_block() + 8); }

void XB_STDCALL SetLastError(uint32_t error) { *reinterpret_cast<uint32_t*>(xapi_thread_block() + 8) = error; }

namespace {

// NTSTATUS -> Win32 error as NT maps them (RtlNtStatusToDosError), for our native C: the statuses it produces
// itself and the file statuses the title's code passes on.
uint32_t native_dos_error(uint32_t status) {
    struct Mapping {
        uint32_t status, error;
    };
    static constexpr Mapping kMappings[] = {
        {0x00000000, 0},     {0x00000102, 0x102}, {0x00000103, 0x3e5}, {0x80000005, 0xea},  {0x80000006, 0x12},
        {0xc0000002, 1},     {0xc0000008, 6},     {0xc000000d, 0x57},  {0xc000000f, 2},     {0xc0000010, 1},
        {0xc0000011, 0x26},  {0xc0000017, 8},     {0xc0000022, 5},     {0xc0000023, 0x7a},  {0xc0000024, 6},
        {0xc0000033, 0x7b},  {0xc0000034, 2},     {0xc0000035, 0xb7},  {0xc0000039, 0xa1},  {0xc000003a, 3},
        {0xc0000043, 0x20},  {0xc0000046, 0x120}, {0xc000007f, 0x70},  {0xc00000ba, 5},     {0xc0000101, 0x91},
        {0xc0000103, 0x10b},
    };
    for (const Mapping& m : kMappings)
        if (m.status == status) return m.error;
    if ((status & 0x3fff0000) == 0x00070000) return status & 0xffff;  // FACILITY_WIN32
    return 0x13d;  // ERROR_MR_MID_NOT_FOUND
}

// A failed native file call: its Win32 error as the last error.
uint32_t fail_with(uint32_t error, uint32_t result) {
    SetLastError(error);
    return result;
}

}  // namespace

uint32_t title_dos_error(uint32_t status) {
    return native_platform() ? native_dos_error(status)
                             : kernel<uint32_t XB_STDCALL(uint32_t)>(kRtlNtStatusToDosError)(status);
}

const uint8_t* title_console_key() {
    return native_platform() ? kConsoleKey
                             : reinterpret_cast<const uint8_t*>(uintptr_t{title_global<uint32_t>(0x007b302c)});
}

uint32_t XB_STDCALL XapiSetLastNTError(uint32_t status) {
    const uint32_t error = title_dos_error(status);
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

uint32_t XB_STDCALL XGetAVPack() {
    // The AV pack the console booted with: 0 (the loader's zero-filled HalBootSMCVideoMode) on the PC.
    return native_platform() ? 0 : kernel_data<uint32_t>(kHalBootSMCVideoMode);
}

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
    return XPhysicalAllocEx(size, lowest, highest, alignment, protect);
}

uint32_t XB_STDCALL XPhysicalAllocEx(uint32_t size, uint32_t lowest, uint32_t highest, uint32_t alignment,
                                     uint32_t protect) {
    const uint32_t result =
        native_platform()
            ? title_host().allocate_contiguous(size, lowest, highest, alignment, protect)
            : kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
                  kMmAllocateContiguousMemoryEx)(size, lowest, highest, alignment, protect);
    if (!result) SetLastError(8);  // ERROR_NOT_ENOUGH_MEMORY
    return result;
}

void XB_STDCALL XPhysicalFree(uint32_t address) {
    if (native_platform()) return title_host().free_contiguous(address);
    kernel<void XB_STDCALL(uint32_t)>(kMmFreeContiguousMemory)(address);
}

void XB_STDCALL XPhysicalProtect(uint32_t address, uint32_t size, uint32_t protect) {
    if (!size) return;
    if (native_platform()) return title_host().protect(address, size, protect);
    kernel<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(kMmSetAddressProtect)(address, size, protect);
}

uint32_t XB_STDCALL XQueryMemoryProtect(uint32_t address) {
    if (native_platform()) return title_host().protection(address);
    return kernel<uint32_t XB_STDCALL(uint32_t)>(kMmQueryAddressProtect)(address);
}

void XB_STDCALL GlobalMemoryStatus(uint32_t* status) {
    // MEMORYSTATUS from MM_STATISTICS {length, total pages, available pages, -, reserved bytes (+0x10), ...}: 2 GB
    // of virtual address space less the reserved bytes; memory load and page file 0.
    uint32_t stats[9];
    stats[0] = 0x24;
    if (native_platform())
        title_host().memory_statistics(&stats[1], &stats[2], &stats[4]);
    else
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
    if (native_platform()) {
        // Our own sleep: WAIT_IO_COMPLETION when queued completion routines ran.
        const uint32_t status = objects::wait(nullptr, 0, false, ms == 0xffffffff ? nullptr : &ms, alertable & 0xff);
        return status == 0xc0 ? 0xc0 : 0;
    }
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
    const uint32_t status =
        objects::is_ours(handle) ? objects::close(handle) : kernel<uint32_t XB_STDCALL(uint32_t)>(kNtClose)(handle);
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

namespace {

// A native search entry as WIN32_FIND_DATAA (as XapiDirectoryInformationToFindData fills it).
void native_find_data(const files::Entry& entry, uint8_t* find) {
    const auto put = [&](uint32_t offset, uint32_t w) { std::memcpy(find + offset, &w, 4); };
    put(0x00, entry.attributes);
    for (uint32_t time = 0x04; time < 0x1c; time += 8) {
        put(time, static_cast<uint32_t>(entry.write_time));
        put(time + 4, static_cast<uint32_t>(entry.write_time >> 32));
    }
    put(0x1c, static_cast<uint32_t>(entry.size >> 32));
    put(0x20, static_cast<uint32_t>(entry.size));
    const size_t length = std::min<size_t>(entry.name.size(), 0x103);
    std::memcpy(find + 0x2c, entry.name.data(), length);
    find[0x2c + length] = 0;
    find[0x130] = 0;
}

}  // namespace

uint32_t XB_STDCALL FindFirstFileA(const char* path, uint8_t* find) {
    if (native_platform()) {
        uint32_t handle;
        files::Entry entry;
        if (const uint32_t error = files::find_first(path, &handle, &entry)) return fail_with(error, 0xffffffff);
        native_find_data(entry, find);
        return handle;
    }
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
    if (objects::is_ours(handle)) {
        files::Entry entry;
        if (const uint32_t error = files::find_next(handle, &entry)) return fail_with(error, 0);
        native_find_data(entry, find);
        return 1;
    }
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
    if (objects::is_ours(handle)) {
        uint64_t bytes;
        if (const uint32_t error = files::size(handle, &bytes)) return fail_with(error, 0);
        size[0] = static_cast<uint32_t>(bytes);
        size[1] = static_cast<uint32_t>(bytes >> 32);
        return 1;
    }
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

namespace {

// The status block's status for a native transfer: END_OF_FILE, or a FACILITY_WIN32 status carrying the error.
uint32_t native_io_status(uint32_t error) {
    if (error == files::kErrorHandleEof) return 0xc0000011;
    return error ? 0xc0070000 | error : 0;
}

uint64_t overlapped_offset(const uint32_t* overlapped) {
    return (uint64_t{overlapped[3]} << 32) | overlapped[2];
}

// ReadFileEx/WriteFileEx on our files: done inside the call; the OVERLAPPED's first two words get the status and
// the byte count, and the completion routine (error, bytes, OVERLAPPED) runs at the thread's next alertable wait,
// also after a read at the end of the file (then FALSE, ERROR_HANDLE_EOF), as the kernel path did.
uint32_t native_transfer_ex(bool writing, uint32_t handle, void* buffer, uint32_t size, uint32_t* overlapped,
                            uint32_t completion) {
    const uint64_t offset = overlapped_offset(overlapped);
    uint32_t done = 0;
    const uint32_t error = writing ? files::write(handle, buffer, size, &offset, &done)
                                   : files::read(handle, buffer, size, &offset, &done);
    if (error == files::kErrorInvalidHandle || error == 1) return fail_with(error, 0);
    overlapped[0] = native_io_status(error);
    overlapped[1] = done;
    objects::queue_apc(completion, error, done, addr(overlapped));
    return error ? fail_with(error, 0) : 1;
}

// ReadFile/WriteFile on our files. With an OVERLAPPED: done inside the call, its status block filled and its event
// set, but reported as pending (FALSE, ERROR_IO_PENDING) like the console's drivers - the save reader treats TRUE
// on an overlapped read as the end of the file. A read at the end of the file: FALSE, ERROR_HANDLE_EOF (overlapped)
// or TRUE with 0 bytes.
uint32_t native_transfer(bool writing, uint32_t handle, void* buffer, uint32_t size, uint32_t* done,
                         uint32_t* overlapped) {
    if (done) *done = 0;
    uint32_t count = 0;
    if (overlapped) {
        const uint64_t offset = overlapped_offset(overlapped);
        overlapped[0] = 0x103;
        const uint32_t error = writing ? files::write(handle, buffer, size, &offset, &count)
                                       : files::read(handle, buffer, size, &offset, &count);
        if (error == files::kErrorInvalidHandle || error == 1) return fail_with(error, 0);
        overlapped[0] = native_io_status(error);
        overlapped[1] = count;
        if (const uint32_t event = overlapped[4] & ~1u) objects::set_event(event, true);
        return fail_with(error ? error : 0x3e5, 0);
    }
    const uint32_t error = writing ? files::write(handle, buffer, size, nullptr, &count)
                                   : files::read(handle, buffer, size, nullptr, &count);
    if (!error || (!writing && error == files::kErrorHandleEof)) {
        *done = count;
        return 1;
    }
    return fail_with(error, 0);
}

}  // namespace

uint32_t XB_STDCALL ReadFileEx(uint32_t handle, void* buffer, uint32_t size, uint32_t* overlapped,
                               uint32_t completion) {
    // NtReadFile at the OVERLAPPED's offset (+8), its first two words the status block; the completion routine runs
    // as an APC through the kernel's NtUserIoApcDispatcher.
    if (objects::is_ours(handle)) return native_transfer_ex(false, handle, buffer, size, overlapped, completion);
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
    if (objects::is_ours(handle))
        return native_transfer_ex(true, handle, const_cast<void*>(buffer), size, overlapped, completion);
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
    if (objects::is_ours(handle)) {
        if (const uint32_t error = files::sort_key(handle, &key)) return fail_with(error, 0xffffffff);
        return key;
    }
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
    if (native_platform()) {
        files::OpenRequest request;
        request.write = access & 0x40000000;
        request.disposition = disposition;
        request.directory = flags & 0x2000000;
        request.overlapped = flags & 0x40000000;
        request.delete_on_close = flags & 0x4000000;
        uint32_t handle;
        bool existed;
        if (const uint32_t error = files::open(path, request, &handle, &existed))
            return fail_with(error == files::kErrorAlreadyExists ? files::kErrorFileExists : error, 0xffffffff);
        SetLastError(existed && (disposition == 2 || disposition == 4) ? files::kErrorAlreadyExists : 0);
        return handle;
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
    if (objects::is_ours(handle)) return native_transfer(false, handle, buffer, size, read, overlapped);
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
    if (objects::is_ours(handle))
        return native_transfer(true, handle, const_cast<void*>(buffer), size, written, overlapped);
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
    if (objects::is_ours(handle)) {
        const uint32_t error = method == 0 ? 0 : method == 2 ? files::size(handle, &base) : files::position(handle, &base);
        if (error) {
            SetLastError(error);
            return false;
        }
    } else if (method == 0) {
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

// Our file's position as an NTSTATUS for the seek functions' error path (a FACILITY_WIN32 status carries the
// Win32 error through XapiSetLastNTError).
uint32_t native_set_position(uint32_t handle, const uint32_t* position) {
    const uint32_t error = files::set_position(handle, (uint64_t{position[1]} << 32) | position[0]);
    return error ? 0xc0070000 | error : 0;
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
    const uint32_t status =
        objects::is_ours(handle)
            ? native_set_position(handle, position)
            : kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t)>(kNtSetInformationFile)(
                  handle, io_status, position, 8, 0xe);
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
    const uint32_t status =
        objects::is_ours(handle)
            ? native_set_position(handle, target)
            : kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t)>(kNtSetInformationFile)(
                  handle, io_status, target, 8, 0xe);
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
    if (objects::is_ours(handle)) {
        if (const uint32_t error = files::set_end(handle)) return fail_with(error, 0);
        return 1;
    }
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
    if (objects::is_ours(handle)) {
        const uint32_t status = objects::wait(&handle, 1, false, ms == 0xffffffff ? nullptr : &ms, alertable & 0xff);
        if (static_cast<int32_t>(status) < 0) {
            XapiSetLastNTError(status);
            return 0xffffffff;  // WAIT_FAILED
        }
        return status;
    }
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
    const uint32_t status = objects::is_ours(handle)
                                ? objects::set_event(handle, true)
                                : kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(kNtSetEvent)(handle, nullptr);
    if (static_cast<int32_t>(status) >= 0) return 1;
    XapiSetLastNTError(status);
    return 0;
}

uint32_t XB_STDCALL ReleaseMutex(uint32_t handle) {
    const uint32_t status = objects::is_ours(handle)
                                ? objects::release_mutex(handle)
                                : kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*)>(kNtReleaseMutant)(handle, nullptr);
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
    if (native_platform()) {
        // Our own event; the game names none (a name would only matter between processes).
        SetLastError(0);
        return objects::create_event(manual_reset != 0, initial_state != 0);
    }
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
    if (native_platform()) {
        SetLastError(0);
        return objects::create_mutex((initial_owner & 0xff) != 0);
    }
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

// XapiThreadStartup's work around the routine (TLS block, notify routines); its result.
uint32_t xapi_thread_body(uint32_t start, uint32_t parameter);

uint32_t XB_STDCALL CreateThread(uint32_t attributes, uint32_t stack_size, uint32_t start, uint32_t parameter,
                                 uint32_t flags, uint32_t* thread_id) {
    // A kernel thread that starts in XapiThreadStartup (TLS, CRT notifications) with the XBE's default stack
    // (header +0x130) when none is given and the title's TLS size (0x0098f668); CREATE_SUSPENDED is flag 4. The
    // handle goes to the stack size argument's slot, as in the original (which returns that slot even when the
    // thread was not created on a thread without XAPI TLS).
    (void)attributes;
    const uint32_t stack = stack_size ? stack_size : title_global<uint32_t>(0x00010130);
    if (native_platform() && !(flags & 4)) {
        // Our own thread (the game starts none suspended); its handle is signaled when the routine returns.
        const uint32_t handle =
            objects::create_thread(stack, title_global<uint32_t>(0x0098f668), &xapi_thread_body, start, parameter,
                                   thread_id);
        if (!handle) SetLastError(8);  // ERROR_NOT_ENOUGH_MEMORY
        return handle;
    }
    uint32_t* const handle = &stack_size;
    const uint32_t status =
        kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t)>(kPsCreateSystemThreadEx)(
            handle, 0, stack, title_global<uint32_t>(0x0098f668), thread_id, start, parameter,
            (flags >> 2) & 0xffffff01u, 0, own_address(&XapiThreadStartup));
    if (static_cast<int32_t>(status) < 0) {
        const uint32_t irql = title_fs_word(0x24) & 0xff;
        const uint32_t kthread = title_fs_word(0x28);
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
    if (native_platform())
        objects::enter_critical_section(kLock);
    else
        kernel<void XB_STDCALL(uint32_t)>(kRtlEnterCriticalSection)(kLock);
    uint32_t entry = title_global<uint32_t>(kHead);
    while (entry != kHead) {
        const uint32_t* const node = reinterpret_cast<const uint32_t*>(uintptr_t{entry});
        entry = node[0];
        title_function<void XB_STDCALL(uint32_t)>(node[2])(create);
    }
    if (native_platform())
        objects::leave_critical_section(kLock);
    else
        kernel<void XB_STDCALL(uint32_t)>(kRtlLeaveCriticalSection)(kLock);
}

uint32_t xapi_thread_body(uint32_t start, uint32_t parameter) {
    // The thread's TLS block (KTHREAD +0x28): its first word points past itself to the copy of the title's TLS
    // template (directory 0x0082740c: raw data begin, end, ..., zero fill +0x10). Then the notify routines (the
    // CRT's per-thread data), the start routine, the routines again, and the thread ends with its result. The
    // original wraps this in an SEH frame whose filter is UnhandledExceptionFilter; the loader's crash handler takes
    // that part (no title thread raises exceptions it does not handle itself).
    const uint32_t kthread = title_fs_word(0x28);
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
    return result;
}

void XB_STDCALL XapiThreadStartup(uint32_t start, uint32_t parameter) {
    kernel<void XB_STDCALL(uint32_t)>(kPsTerminateSystemThread)(xapi_thread_body(start, parameter));
}

// --- Threads, debugger -------------------------------------------------------------------------------------------

uint32_t XB_STDCALL GetCurrentThreadId() {
    // The current ETHREAD's UniqueThread (+0x12c); FS:[0x28] is the KPCR's current thread.
    return *reinterpret_cast<const uint32_t*>(uintptr_t{title_fs_word(0x28) + 0x12c});
}

void XB_STDCALL OutputDebugStringA(const char*) {
    // The original raises the debug-print service (int 0x2d, BREAKPOINT_PRINT) with the string's ANSI_STRING; a
    // retail kernel without a debugger drops it, and under the loader that interrupt is a fault. Nothing to print to.
}

// --- Time --------------------------------------------------------------------------------------------------------

void XB_STDCALL GetSystemTimeAsFileTime(uint32_t* time) {
    // The original is a jump straight into KeQuerySystemTime.
    system_time(time);
}

uint32_t XB_STDCALL FileTimeToSystemTime(const uint32_t* file_time, uint16_t* system_time) {
    // TIME_FIELDS (Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday) into SYSTEMTIME (Year, Month,
    // DayOfWeek, Day, Hour, Minute, Second, Milliseconds).
    uint32_t time[2] = {file_time[0], file_time[1]};
    uint16_t fields[8];
    time_to_fields(time, fields);
    system_time[0] = fields[0];
    system_time[1] = fields[1];
    system_time[3] = fields[2];
    system_time[2] = fields[7];
    system_time[4] = fields[3];
    system_time[5] = fields[4];
    system_time[6] = fields[5];
    system_time[7] = fields[6];
    return 1;
}

uint32_t XB_STDCALL SystemTimeToFileTime(const uint16_t* system_time, uint32_t* file_time) {
    // SYSTEMTIME into TIME_FIELDS (the weekday is not copied: RtlTimeFieldsToTime ignores it); a time the kernel
    // refuses sets STATUS_INVALID_PARAMETER.
    uint16_t fields[8];
    fields[0] = system_time[0];
    fields[1] = system_time[1];
    fields[2] = system_time[3];
    fields[3] = system_time[4];
    fields[4] = system_time[5];
    fields[5] = system_time[6];
    fields[6] = system_time[7];
    uint32_t time[2];
    if (!fields_to_time(fields, time)) {
        XapiSetLastNTError(0xc000000d);
        return 0;
    }
    file_time[0] = time[0];
    file_time[1] = time[1];
    return 1;
}

uint32_t XB_STDCALL XQueryValue(uint32_t index, uint32_t* type, void* value, uint32_t size, uint32_t* result_size) {
    // ExQueryNonVolatileSetting through the title's jump stub, its status as a DOS error (0 = success).
    return title_dos_error(query_value(index, type, value, size, result_size));
}

namespace {

int64_t file_time64(const uint32_t* time) {
    return static_cast<int64_t>((static_cast<uint64_t>(time[1]) << 32) | time[0]);
}

// Minutes (a 32-bit product, as the original's imul) to 100 ns units.
int64_t minutes_to_time(int32_t minutes) {
    return static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(minutes) * 60u)) * 10000000;
}

}  // namespace

uint32_t XB_STDCALL XapipQueryTimeZoneInformation(uint8_t* info, uint32_t* daylight_on) {
    // All the user settings (index 0xff, 0x100 bytes); TIME_ZONE_INFORMATION from them: bias +4, standard/daylight
    // names +8/+0xc (4 characters each), dates +0x18/+0x1c (month, day, day of week, hour), biases +0x28/+0x2c;
    // the misc flags +0x58 bit 1 = daylight saving off (daylight_on gets the inverse).
    uint32_t type;
    uint8_t settings[0x100];
    const uint32_t error = XQueryValue(0xff, &type, settings, 0x100, nullptr);
    if (error != 0) return error;
    std::memcpy(info, settings + 4, 4);
    const auto name = [](const uint8_t* text, uint8_t* out) {
        uint16_t* const wide = reinterpret_cast<uint16_t*>(out);
        for (int i = 0; i < 4; ++i) wide[i] = text[i];
        wide[4] = 0;
    };
    const auto date = [](const uint8_t* packed, uint8_t* out) {
        uint16_t* const st = reinterpret_cast<uint16_t*>(out);
        std::memset(st, 0, 16);
        st[1] = packed[0];
        st[3] = packed[1];
        st[2] = packed[2];
        st[4] = packed[3];
    };
    name(settings + 8, info + 0x4);
    name(settings + 0xc, info + 0x58);
    date(settings + 0x18, info + 0x44);
    date(settings + 0x1c, info + 0x98);
    std::memcpy(info + 0x54, settings + 0x28, 4);
    std::memcpy(info + 0xa8, settings + 0x2c, 4);
    if (daylight_on) {
        uint32_t flags;
        std::memcpy(&flags, settings + 0x58, 4);
        *daylight_on = ~(flags >> 1) & 1;
    }
    return 0;
}

uint8_t XB_STDCALL RtlpCutoverTimeToSystemTime(const uint16_t* cutover, uint32_t* out, const uint32_t* now,
                                               uint8_t this_year) {
    // A time-zone change date: absolute (year set) or "the Nth <weekday> of <month>" (year 0, day = week 1..5,
    // 5 = the last one). For the relative form the year is this one, or (this_year false) the next occurrence
    // after now.
    uint16_t current[8];
    FileTimeToSystemTime(now, current);
    if (cutover[0] != 0) {
        if (!SystemTimeToFileTime(cutover, out)) return 0;
        return file_time64(out) >= file_time64(now) ? 1 : 0;
    }
    const uint16_t week = cutover[3];
    if (static_cast<int16_t>(week) > 5 || week == 0) return 0;
    const uint16_t weekday = cutover[2];
    const uint16_t month = cutover[1];
    bool same_month = false;
    uint16_t year = current[0];
    if (!this_year) {
        if (static_cast<int16_t>(month) < static_cast<int32_t>(current[1])) {
            year = static_cast<uint16_t>(current[0] + 1);
        } else if (static_cast<int16_t>(month) == static_cast<int32_t>(current[1])) {
            same_month = true;
        }
    }
    const auto rtl_to_time = [](uint16_t* f, uint32_t* t) -> uint8_t { return fields_to_time(f, t) ? 1 : 0; };
    const auto rtl_to_fields = [](uint32_t* t, uint16_t* f) { time_to_fields(t, f); };
    uint16_t fields[8];
    const auto first_of_month = [&] {
        fields[0] = year;
        fields[1] = month;
        fields[2] = 1;
        fields[3] = cutover[4];
        fields[4] = cutover[5];
        fields[5] = cutover[6];
        fields[6] = cutover[7];
        fields[7] = 0;
    };
    first_of_month();
    uint32_t time[2];
    if (!rtl_to_time(fields, time)) return 0;
    for (;;) {
        // The first <weekday> of the month, then week - 1 more weeks while the day stays in the month.
        uint16_t found[8];
        rtl_to_fields(time, found);
        if (static_cast<int16_t>(found[7]) > static_cast<int16_t>(weekday)) {
            fields[2] = static_cast<uint16_t>(fields[2] + weekday - found[7] + 7);
        } else if (static_cast<int16_t>(found[7]) < static_cast<int16_t>(weekday)) {
            fields[2] = static_cast<uint16_t>(fields[2] + weekday - found[7]);
        }
        uint16_t day = fields[2];
        for (uint16_t n = 1; static_cast<int16_t>(n) < static_cast<int16_t>(week); ++n) {
            fields[2] = static_cast<uint16_t>(fields[2] + 7);
            if (!rtl_to_time(fields, time)) break;
            rtl_to_fields(time, found);
            day = found[2];
        }
        fields[2] = day;
        if (!rtl_to_time(fields, time)) return 0;
        if (same_month) {
            const int32_t this_day = static_cast<int16_t>(fields[2]);
            const bool passed = this_day < current[3] ||
                                (this_day == current[3] && file_time64(time) < file_time64(now));
            if (passed) {
                ++year;
                first_of_month();
                same_month = false;
                if (!rtl_to_time(fields, time)) return 0;
                continue;
            }
        }
        out[0] = time[0];
        out[1] = time[1];
        return 1;
    }
}

uint32_t XB_STDCALL GetTimeZoneInformation(uint8_t* info) {
    // 0 unknown (no daylight dates), 1 standard, 2 daylight time now, -1 when the settings cannot be read.
    uint32_t daylight_on = 0;
    const uint32_t error = XapipQueryTimeZoneInformation(info, &daylight_on);
    if (error != 0) {
        SetLastError(error);
        return 0xffffffff;
    }
    if (daylight_on == 0) {
        std::memset(info + 0x54, 0, 4);
        std::memset(info + 0x44, 0, 16);
        std::memset(info + 0xa8, 0, 4);
        std::memset(info + 0x98, 0, 16);
        std::memset(info + 0x58, 0, 0x40);
    }
    uint32_t now[2];
    system_time(now);
    const auto word = [&](uint32_t offset) { return *reinterpret_cast<const uint16_t*>(info + offset); };
    const auto dword = [&](uint32_t offset) { return *reinterpret_cast<const int32_t*>(info + offset); };
    if (word(0x46) == 0 || word(0x9a) == 0) return 0;
    uint32_t standard[2], daylight[2];
    if (!RtlpCutoverTimeToSystemTime(reinterpret_cast<const uint16_t*>(info + 0x44), standard, now, 1)) return 0;
    if (!RtlpCutoverTimeToSystemTime(reinterpret_cast<const uint16_t*>(info + 0x98), daylight, now, 1)) return 0;
    const int64_t bias = minutes_to_time(dword(0));
    const int64_t daylight_utc = static_cast<int64_t>(static_cast<uint64_t>(bias) +
                                                      static_cast<uint64_t>(minutes_to_time(dword(0x54))) +
                                                      static_cast<uint64_t>(file_time64(daylight)));
    const int64_t standard_utc = static_cast<int64_t>(static_cast<uint64_t>(bias) +
                                                      static_cast<uint64_t>(minutes_to_time(dword(0xa8))) +
                                                      static_cast<uint64_t>(file_time64(standard)));
    const int64_t t = file_time64(now);
    if (daylight_utc < standard_utc) return (t >= daylight_utc && t < standard_utc) ? 2 : 1;
    return (t < standard_utc || t >= daylight_utc) ? 2 : 1;
}

void XB_STDCALL XapipGetTimeZoneBias(uint32_t* bias) {
    // The bias in effect now (with the standard or daylight bias added), in 100 ns units; 0 when unreadable.
    uint8_t info[0xac];
    int32_t minutes = 0;
    switch (GetTimeZoneInformation(info)) {
        case 0: std::memcpy(&minutes, info, 4); break;
        case 1: minutes = *reinterpret_cast<const int32_t*>(info) + *reinterpret_cast<const int32_t*>(info + 0x54); break;
        case 2: minutes = *reinterpret_cast<const int32_t*>(info) + *reinterpret_cast<const int32_t*>(info + 0xa8); break;
        default: break;
    }
    const int64_t value = minutes_to_time(minutes);
    bias[0] = static_cast<uint32_t>(value);
    bias[1] = static_cast<uint32_t>(static_cast<uint64_t>(value) >> 32);
}

uint32_t XB_STDCALL FileTimeToLocalFileTime(const uint32_t* file_time, uint32_t* local_time) {
    uint32_t bias[2];
    XapipGetTimeZoneBias(bias);
    const uint64_t value = static_cast<uint64_t>(file_time64(file_time)) - static_cast<uint64_t>(file_time64(bias));
    local_time[0] = static_cast<uint32_t>(value);
    local_time[1] = static_cast<uint32_t>(value >> 32);
    return 1;
}

// --- Files and disks ----------------------------------------------------------------------------------------------

uint32_t XB_STDCALL DeleteFileA(const char* path) {
    // Open for DELETE (sharing read/write/delete, non-directory, synchronous) and set FileDispositionInformation.
    if (native_platform()) {
        if (const uint32_t error = files::remove_file(path)) return fail_with(error, 0);
        return 1;
    }
    AnsiString name;
    kernel<void XB_STDCALL(AnsiString*, const char*)>(kRtlInitAnsiString)(&name, path);
    ObjectAttributes attributes{0xfffffffd, addr(&name), 0x40};
    uint32_t handle;
    uint32_t io_status[2];
    uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, ObjectAttributes*, uint32_t*, uint32_t, uint32_t)>(
        kNtOpenFile)(&handle, 0x10000, &attributes, io_status, 7, 0x4040);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    uint8_t delete_file = 1;
    status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, void*, uint32_t, uint32_t)>(kNtSetInformationFile)(
        handle, io_status, &delete_file, 1, 0xd);
    kernel<uint32_t XB_STDCALL(uint32_t)>(kNtClose)(handle);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    return 1;
}

namespace {

// FILE_FS_SIZE_INFORMATION of the volume holding path (opened as a directory); false with the error set (a missing
// file becomes "path not found", as in the originals).
bool volume_size(const char* path, uint32_t* info) {
    if (native_platform()) {
        files::Volume volume;
        if (uint32_t error = files::volume(path, &volume)) {
            SetLastError(error == files::kErrorFileNotFound ? files::kErrorPathNotFound : error);
            return false;
        }
        info[0] = static_cast<uint32_t>(volume.total_clusters);
        info[1] = static_cast<uint32_t>(volume.total_clusters >> 32);
        info[2] = static_cast<uint32_t>(volume.free_clusters);
        info[3] = static_cast<uint32_t>(volume.free_clusters >> 32);
        info[4] = volume.sectors_per_cluster;
        info[5] = volume.bytes_per_sector;
        return true;
    }
    AnsiString name;
    kernel<void XB_STDCALL(AnsiString*, const char*)>(kRtlInitAnsiString)(&name, path);
    ObjectAttributes attributes{0xfffffffd, addr(&name), 0x40};
    uint32_t handle;
    uint32_t io_status[2];
    uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t*, uint32_t, ObjectAttributes*, uint32_t*, uint32_t, uint32_t)>(
        kNtOpenFile)(&handle, 0x100001, &attributes, io_status, 3, 0x800021);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        if (GetLastError() == 2) SetLastError(3);
        return false;
    }
    status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t*, uint32_t*, uint32_t, uint32_t)>(
        kNtQueryVolumeInformationFile)(handle, io_status, info, 0x18, 3);
    kernel<uint32_t XB_STDCALL(uint32_t)>(kNtClose)(handle);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return false;
    }
    return true;
}

}  // namespace

uint32_t XB_STDCALL XGetDiskSectorSizeA(const char* path) {
    uint32_t info[6];
    return volume_size(path, info) ? info[5] : 0;
}

uint32_t XB_STDCALL XGetDiskClusterSizeA(const char* path) {
    uint32_t info[6];
    return volume_size(path, info) ? info[4] * info[5] : 0;
}

uint32_t XB_STDCALL GetDiskFreeSpaceExA(const char* path, uint32_t* free_to_caller, uint32_t* total, uint32_t* free) {
    uint32_t info[6];
    if (!volume_size(path, info)) return 0;
    const uint64_t cluster = static_cast<uint32_t>(info[4] * info[5]);
    const uint64_t free_bytes = file_time64(info + 2) * cluster;
    const uint64_t total_bytes = file_time64(info) * cluster;
    const auto put = [](uint32_t* out, uint64_t value) {
        if (!out) return;
        out[0] = static_cast<uint32_t>(value);
        out[1] = static_cast<uint32_t>(value >> 32);
    };
    put(free_to_caller, free_bytes);
    put(total, total_bytes);
    put(free, free_bytes);
    return 1;
}

uint32_t XB_STDCALL CancelIo(uint32_t handle) {
    // The original references the file object, then (at DPC level) moves the IDE channel's queued requests for it
    // to a list a DPC cancels. Our kernel completes every request inside the call, so nothing is ever queued (and
    // its IdexChannelObject is a zero block the original's list walk would fault on).
    if (objects::is_ours(handle)) return files::is_file(handle) ? 1 : fail_with(files::kErrorInvalidHandle, 0);
    uint32_t object;
    const uint32_t status = kernel<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t*)>(kObReferenceObjectByHandle)(
        handle, title_global<uint32_t>(kIoFileObjectType), &object);
    if (static_cast<int32_t>(status) < 0) {
        XapiSetLastNTError(status);
        return 0;
    }
    kernel<void XB_FASTCALL(uint32_t)>(kObfDereferenceObject)(object);
    return 1;
}

// --- Strings ------------------------------------------------------------------------------------------------------

int32_t XB_STDCALL UTF8ToUnicode(const char* source, int32_t length, uint16_t* out, int32_t out_size) {
    // ConvertUTF8toUTF16 with the title's tables (trailing bytes 0x008cfd28, offsets 0x008cfd10, constants from
    // 0x008cfe28): length -1 = through the terminator; out_size 0 only counts. Characters past out_size are counted
    // but not written; a short buffer then fails with ERROR_INSUFFICIENT_BUFFER. A sequence running past the
    // input ends the conversion.
    const uint8_t* p = reinterpret_cast<const uint8_t*>(source);
    if (length == -1) length = static_cast<int32_t>(std::strlen(source)) + 1;
    int32_t written = 0;
    const auto put = [&](uint16_t unit) {
        ++written;
        if (written <= out_size) *out++ = unit;
    };
    for (int32_t index = 0; index < length;) {
        const uint8_t first = *p;
        const uint32_t extra = static_cast<uint16_t>(static_cast<int8_t>(title_global<uint8_t>(0x008cfd28 + first)));
        if (static_cast<int32_t>(extra + (p - reinterpret_cast<const uint8_t*>(source))) > length) break;
        uint32_t ch = 0;
        if (extra <= 5) {
            for (uint32_t i = extra; i > 0; --i) {
                ch += *p++;
                ++index;
                ch <<= 6;
            }
            ch += *p++;
            ++index;
        }
        ch -= title_global<uint32_t>(0x008cfd10 + extra * 4);
        if (ch <= title_global<uint32_t>(0x008cfe2c)) {
            put(static_cast<uint16_t>(ch));
        } else if (ch > title_global<uint32_t>(0x008cfe30)) {
            put(title_global<uint16_t>(0x008cfe28));
        } else {
            ch -= title_global<uint32_t>(0x008cfe38);
            put(static_cast<uint16_t>((ch >> (title_global<uint32_t>(0x008cfe34) & 31)) + title_global<uint32_t>(0x008cfe40)));
            put(static_cast<uint16_t>((ch & title_global<uint32_t>(0x008cfe3c)) + title_global<uint32_t>(0x008cfe44)));
        }
    }
    if (out_size != 0 && out_size < written) {
        SetLastError(0x7a);
        return 0;
    }
    return written;
}

int32_t XB_STDCALL MultiByteToWideChar(uint32_t code_page, uint32_t, const char* source, int32_t length,
                                       uint16_t* out, int32_t out_size) {
    // UTF-7 refused, UTF-8 by UTF8ToUnicode, anything else as ANSI through the kernel. The ANSI count is the length
    // plus one even when the length is given (the terminator's slot), cut to 16 bits.
    if (code_page == 0xfde8) {
        SetLastError(0x57);
        return 0;
    }
    if (code_page == 0xfde9) return UTF8ToUnicode(source, length, out, out_size);
    uint32_t count = length == -1 ? static_cast<uint32_t>(std::strlen(source)) : static_cast<uint32_t>(length);
    ++count;
    const uint16_t needed = static_cast<uint16_t>(count);
    if (out_size == 0) return needed;
    if (out_size < needed) {
        SetLastError(0x7a);
        return 0;
    }
    AnsiString ansi;
    ansi.length = static_cast<uint16_t>(count - 1);
    ansi.maximum = static_cast<uint16_t>(count);
    ansi.buffer = addr(source);
    struct {
        uint16_t length;
        uint16_t maximum;
        uint32_t buffer;
    } unicode{static_cast<uint16_t>((count - 1) * 2), static_cast<uint16_t>(out_size * 2), addr(out)};
    if (native_platform()) {
        // RtlAnsiStringToUnicodeString: bytes widened (single-byte code page); without room for them and a
        // terminator it copies what fits and fails with STATUS_BUFFER_OVERFLOW (ERROR_MORE_DATA).
        uint32_t chars = static_cast<uint16_t>(count - 1);
        const uint32_t capacity = static_cast<uint16_t>(out_size * 2) / 2u;
        const bool overflow = chars + 1 > capacity;
        if (overflow) chars = capacity ? capacity - 1 : 0;
        for (uint32_t i = 0; i < chars; ++i) out[i] = static_cast<uint8_t>(source[i]);
        if (chars < capacity) out[chars] = 0;
        if (!overflow) return needed;
        SetLastError(0xea);
        return 0;
    }
    const uint32_t status = kernel<uint32_t XB_STDCALL(void*, AnsiString*, uint8_t)>(kRtlAnsiStringToUnicodeString)(
        &unicode, &ansi, 0);
    if (static_cast<int32_t>(status) >= 0) return needed;
    SetLastError(kernel<uint32_t XB_STDCALL(uint32_t)>(kRtlNtStatusToDosError)(status));
    return 0;
}

}  // namespace game
