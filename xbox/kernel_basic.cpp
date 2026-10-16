// Kernel exports that need nothing from the loader: time, strings, debug output and
// simple variables. Signatures follow third_party/xbox-includes/include/xboxkrnl.h.
//
// 64-bit values are returned as int64_t rather than LARGE_INTEGER: GCC returns
// structs through a hidden pointer on i386, MSVC (and the Xbox) in EDX:EAX.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include "compiler.h"
#include "filesystem.h"
#include "kernel_internal.h"
#include "kernel_memory.h"
#include "log.h"
#include "types.h"

namespace xbox {

namespace {

std::atomic<bool> g_trace{false};
KernelConfig g_config{};

const auto g_start = std::chrono::steady_clock::now();

// Rate of the Xbox ACPI timer, as used by Cxbx-Reloaded. Any value works as long as
// counter and frequency agree.
constexpr int64_t kPerformanceFrequency = 3375000;

// 100ns intervals between 1601-01-01 and 1970-01-01.
constexpr int64_t kUnixEpochAsFileTime = 116444736000000000LL;

// --- Variables -------------------------------------------------------------------

volatile uint32_t KeTickCount = 0;
XbKrnlVersion XboxKrnlVersion = {1, 0, 5838, 1};
XbAnsiString XeImageFileName = {};
char g_image_file_name[128] = {};
uint32_t LaunchDataPage = 0;  // PLAUNCH_DATA_PAGE; null means "launched from the dashboard"

void tick_thread() {
    for (;;) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_start);
        KeTickCount = static_cast<uint32_t>(ms.count());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::mutex g_shutdown_mutex;
std::vector<XbShutdownRegistration*> g_shutdown_registrations;

// --- Functions -------------------------------------------------------------------

uint32_t XB_CDECL DbgPrint(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    size_t length = std::strlen(buffer);
    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
        buffer[--length] = '\0';
    LOG_INFO("dbgprint", "%s", buffer);
    return 0;  // STATUS_SUCCESS
}

// Routines: 0 halt, 1 reboot, 2 quick reboot, 3 reboot into the debugger, 4 fatal
// error. A quick reboot with a LaunchDataPage (XLaunchNewImage) starts the XBE named
// there through KernelConfig::reboot and hands it the page; there is no dashboard
// to return to otherwise, so the process ends. Shutdown notifications are not run:
// the process is replaced or ends either way.
[[noreturn]] void XB_STDCALL HalReturnToFirmware(uint32_t routine) {
    constexpr uint32_t kQuickReboot = 2;
    if (routine == kQuickReboot && LaunchDataPage) {
        const auto* page = from_xbox_ptr<const uint8_t>(LaunchDataPage);
        // LAUNCH_DATA_HEADER: type, title id, then the launch path at +8 as
        // "<device path>;<file>" (e.g. "\Device\CdRom0;default.xbe").
        char path[520 + 1] = {};
        std::memcpy(path, page + 8, sizeof(path) - 1);
        std::string xbox_path(path);
        if (const size_t separator = xbox_path.find(';'); separator != std::string::npos) xbox_path[separator] = '\\';
        const std::filesystem::path xbe = host_file_path(xbox_path);
        uint32_t type;
        std::memcpy(&type, page, 4);
        if (!xbe.empty() && g_config.reboot) {
            LOG_INFO("kernel", "HalReturnToFirmware: rebooting into %s (launch data type %d)", path,
                     static_cast<int32_t>(type));
            g_config.reboot(xbe, std::span(page, kLaunchDataPageSize));
        }
        LOG_ERROR("kernel", "HalReturnToFirmware: cannot launch %s", path);
        std::exit(1);
    }
    LOG_INFO("kernel", "HalReturnToFirmware(%u): the title quit to the dashboard", routine);
    std::exit(0);
}

// Only recorded; the list links inside the struct are left alone.
void XB_STDCALL HalRegisterShutdownNotification(XbShutdownRegistration* registration, uint32_t register_it) {
    std::lock_guard lock(g_shutdown_mutex);
    auto& list = g_shutdown_registrations;
    list.erase(std::remove(list.begin(), list.end(), registration), list.end());
    if (register_it & 0xFF) list.push_back(registration);
    KTRACE("HalRegisterShutdownNotification(%08x routine=%08x priority=%d, %s)", to_xbox_ptr(registration),
           registration->notification_routine, registration->priority, (register_it & 0xFF) ? "register" : "unregister");
}

int64_t XB_STDCALL KeQueryPerformanceCounter() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - g_start)
                        .count();
    return static_cast<int64_t>(static_cast<long double>(ns) * kPerformanceFrequency / 1e9L);
}

int64_t XB_STDCALL KeQueryPerformanceFrequency() { return kPerformanceFrequency; }

void XB_STDCALL KeQuerySystemTime(int64_t* current_time) {
    *current_time = current_system_time();
    KTRACE("KeQuerySystemTime() -> %lld", static_cast<long long>(*current_time));
}

uint32_t XB_STDCALL RtlEqualString(const XbAnsiString* string1, const XbAnsiString* string2,
                                   uint32_t case_insensitive) {
    if (string1->length != string2->length) return 0;
    const char* a = from_xbox_ptr<const char>(string1->buffer);
    const char* b = from_xbox_ptr<const char>(string2->buffer);
    auto upper = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; };
    for (uint16_t i = 0; i < string1->length; ++i)
        if (case_insensitive & 0xFF ? upper(a[i]) != upper(b[i]) : a[i] != b[i]) return 0;
    return 1;
}

// NTSTATUS -> Win32 error, as NT maps them. Only statuses this kernel returns or
// file/IO code is likely to see; extend as new ones show up (they are logged).
uint32_t XB_STDCALL RtlNtStatusToDosError(uint32_t status) {
    struct Mapping {
        uint32_t status;
        uint32_t error;
    };
    static constexpr Mapping kMappings[] = {
        {0x00000000, 0},    // STATUS_SUCCESS
        {0x00000102, 258},  // STATUS_TIMEOUT -> WAIT_TIMEOUT
        {0x00000103, 997},  // STATUS_PENDING -> ERROR_IO_PENDING
        {0x80000005, 234},  // STATUS_BUFFER_OVERFLOW -> ERROR_MORE_DATA
        {0x80000006, 18},   // STATUS_NO_MORE_FILES
        {0xC0000002, 1},    // STATUS_NOT_IMPLEMENTED -> ERROR_INVALID_FUNCTION
        {0xC0000008, 6},    // STATUS_INVALID_HANDLE
        {0xC000000D, 87},   // STATUS_INVALID_PARAMETER
        {0xC000000F, 2},    // STATUS_NO_SUCH_FILE -> ERROR_FILE_NOT_FOUND
        {0xC0000010, 1},    // STATUS_INVALID_DEVICE_REQUEST -> ERROR_INVALID_FUNCTION
        {0xC0000011, 38},   // STATUS_END_OF_FILE -> ERROR_HANDLE_EOF
        {0xC0000017, 8},    // STATUS_NO_MEMORY -> ERROR_NOT_ENOUGH_MEMORY
        {0xC0000022, 5},    // STATUS_ACCESS_DENIED
        {0xC0000023, 122},  // STATUS_BUFFER_TOO_SMALL -> ERROR_INSUFFICIENT_BUFFER
        {0xC0000024, 6},    // STATUS_OBJECT_TYPE_MISMATCH -> ERROR_INVALID_HANDLE
        {0xC0000033, 123},  // STATUS_OBJECT_NAME_INVALID -> ERROR_INVALID_NAME
        {0xC0000034, 2},    // STATUS_OBJECT_NAME_NOT_FOUND -> ERROR_FILE_NOT_FOUND
        {0xC0000035, 183},  // STATUS_OBJECT_NAME_COLLISION -> ERROR_ALREADY_EXISTS
        {0xC0000039, 161},  // STATUS_OBJECT_PATH_INVALID -> ERROR_BAD_PATHNAME
        {0xC000003A, 3},    // STATUS_OBJECT_PATH_NOT_FOUND -> ERROR_PATH_NOT_FOUND
        {0xC0000043, 32},   // STATUS_SHARING_VIOLATION
        {0xC0000046, 288},  // STATUS_MUTANT_NOT_OWNED -> ERROR_NOT_OWNER
        {0xC000007F, 112},  // STATUS_DISK_FULL
        {0xC00000BA, 5},    // STATUS_FILE_IS_A_DIRECTORY -> ERROR_ACCESS_DENIED
        {0xC0000101, 145},  // STATUS_DIRECTORY_NOT_EMPTY -> ERROR_DIR_NOT_EMPTY
        {0xC0000103, 267},  // STATUS_NOT_A_DIRECTORY -> ERROR_DIRECTORY
    };
    for (const Mapping& m : kMappings)
        if (m.status == status) return m.error;
    if ((status & 0x3FFF0000) == 0x00070000) return status & 0xFFFF;  // FACILITY_WIN32
    LOG_DEBUG("kernel", "RtlNtStatusToDosError: no mapping for %08x", status);
    return 317;  // ERROR_MR_MID_NOT_FOUND, NT's answer for unknown statuses
}

void XB_STDCALL RtlInitAnsiString(XbAnsiString* destination, const char* source) {
    const size_t length = source ? std::strlen(source) : 0;
    destination->length = static_cast<uint16_t>(length);
    destination->maximum_length = static_cast<uint16_t>(source ? length + 1 : 0);
    destination->buffer = to_xbox_ptr(source);
    KTRACE("RtlInitAnsiString(\"%s\")", source ? source : "(null)");
}

// Xbox ANSI strings are Latin-1: code points above 0xFF become '?'.
constexpr uint32_t kStatusBufferOverflow = 0x80000005;
constexpr uint32_t kStatusNotImplemented = 0xC0000002;

uint32_t XB_STDCALL RtlUnicodeStringToAnsiString(XbAnsiString* destination, const XbUnicodeString* source,
                                                 uint32_t allocate) {
    if (allocate & 0xFF) {
        LOG_WARN("kernel", "RtlUnicodeStringToAnsiString: allocating the result is not supported");
        return kStatusNotImplemented;
    }
    const auto* in = from_xbox_ptr<uint16_t>(source->buffer);
    auto* out = from_xbox_ptr<char>(destination->buffer);
    const uint32_t chars = source->length / 2u;
    uint32_t status = 0;
    uint32_t count = chars;
    if (count + 1 > destination->maximum_length) {
        count = destination->maximum_length ? destination->maximum_length - 1u : 0;
        status = kStatusBufferOverflow;
    }
    for (uint32_t i = 0; i < count; ++i) out[i] = in[i] > 0xFF ? '?' : static_cast<char>(in[i]);
    if (count < destination->maximum_length) out[count] = 0;
    destination->length = static_cast<uint16_t>(count);
    return status;
}

uint32_t XB_STDCALL RtlAnsiStringToUnicodeString(XbUnicodeString* destination, const XbAnsiString* source,
                                                 uint32_t allocate) {
    if (allocate & 0xFF) {
        LOG_WARN("kernel", "RtlAnsiStringToUnicodeString: allocating the result is not supported");
        return kStatusNotImplemented;
    }
    const auto* in = from_xbox_ptr<uint8_t>(source->buffer);
    auto* out = from_xbox_ptr<uint16_t>(destination->buffer);
    uint32_t status = 0;
    uint32_t count = source->length;
    const uint32_t capacity = destination->maximum_length / 2u;
    if (count + 1 > capacity) {
        count = capacity ? capacity - 1 : 0;
        status = kStatusBufferOverflow;
    }
    for (uint32_t i = 0; i < count; ++i) out[i] = in[i];
    if (count < capacity) out[count] = 0;
    destination->length = static_cast<uint16_t>(count * 2);
    return status;
}

// --- Time conversion --------------------------------------------------------------------
//
// NT time: 100 ns units since 1601-01-01 (a Monday). Dates use the proleptic Gregorian
// calendar (H. Hinnant's days-from-civil algorithms, shifted to the 1601 epoch).

struct XbTimeFields {  // TIME_FIELDS
    int16_t year, month, day, hour, minute, second, millisecond, weekday;
};
static_assert(sizeof(XbTimeFields) == 16);

constexpr int64_t kTicksPerMillisecond = 10'000;
constexpr int64_t kTicksPerDay = 864'000'000'000;
constexpr int64_t kDaysFrom1601To1970 = 134'774;

// Days since 1970-01-01.
int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = yoe + era * 400 + (m <= 2);
}

void XB_STDCALL RtlTimeToTimeFields(const int64_t* time, XbTimeFields* fields) {
    const int64_t days = *time / kTicksPerDay;
    int64_t rest = *time % kTicksPerDay;
    int64_t y, m, d;
    civil_from_days(days - kDaysFrom1601To1970, y, m, d);
    fields->year = static_cast<int16_t>(y);
    fields->month = static_cast<int16_t>(m);
    fields->day = static_cast<int16_t>(d);
    fields->weekday = static_cast<int16_t>((days + 1) % 7);  // 1601-01-01 was a Monday
    fields->millisecond = static_cast<int16_t>(rest / kTicksPerMillisecond % 1000);
    rest /= kTicksPerMillisecond * 1000;
    fields->second = static_cast<int16_t>(rest % 60);
    fields->minute = static_cast<int16_t>(rest / 60 % 60);
    fields->hour = static_cast<int16_t>(rest / 3600);
}

// Returns FALSE (and leaves *time alone) for fields out of range; the weekday is ignored.
uint32_t XB_STDCALL RtlTimeFieldsToTime(const XbTimeFields* f, int64_t* time) {
    if (f->year < 1601 || f->month < 1 || f->month > 12 || f->day < 1 || f->hour < 0 || f->hour > 23 ||
        f->minute < 0 || f->minute > 59 || f->second < 0 || f->second > 59 || f->millisecond < 0 ||
        f->millisecond > 999)
        return 0;
    const int64_t days = days_from_civil(f->year, f->month, f->day) + kDaysFrom1601To1970;
    int64_t y, m, d;  // reject days past the end of the month (Feb 30 -> Mar 2)
    civil_from_days(days - kDaysFrom1601To1970, y, m, d);
    if (m != f->month) return 0;
    *time = days * kTicksPerDay +
            ((f->hour * 3600 + f->minute * 60 + f->second) * 1000LL + f->millisecond) * kTicksPerMillisecond;
    return 1;
}

const KernelImpl kImpls[] = {
    KIMPL(DbgPrint),
    KIMPL(HalRegisterShutdownNotification),
    KIMPL(HalReturnToFirmware),
    KIMPL(KeQueryPerformanceCounter),
    KIMPL(KeQueryPerformanceFrequency),
    KIMPL(KeQuerySystemTime),
    KIMPL(RtlAnsiStringToUnicodeString),
    KIMPL(RtlEqualString),
    KIMPL(RtlInitAnsiString),
    KIMPL(RtlNtStatusToDosError),
    KIMPL(RtlTimeFieldsToTime),
    KIMPL(RtlTimeToTimeFields),
    KIMPL(RtlUnicodeStringToAnsiString),
    KIMPL_DATA(KeTickCount),
    KIMPL_DATA(LaunchDataPage),
    KIMPL_DATA(XboxKrnlVersion),
    KIMPL_DATA(XeImageFileName),
};

}  // namespace

std::span<const KernelImpl> basic_kernel_impls() { return kImpls; }

std::span<const KernelImpl> kernel_impls() {
    static const std::vector<KernelImpl> all = [] {
        std::vector<KernelImpl> impls;
        for (std::span<const KernelImpl> part :
             {basic_kernel_impls(), object_kernel_impls(), sync_kernel_impls(), dispatcher_kernel_impls(),
              settings_kernel_impls(), memory_kernel_impls(), filesystem_kernel_impls(), crypto_kernel_impls()})
            impls.insert(impls.end(), part.begin(), part.end());
        return impls;
    }();
    return all;
}

bool kernel_trace_enabled() { return g_trace.load(std::memory_order_relaxed); }

std::vector<XbShutdownRegistration*> shutdown_notifications() {
    std::lock_guard lock(g_shutdown_mutex);
    auto list = g_shutdown_registrations;
    std::stable_sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->priority > b->priority; });
    return list;
}

uint32_t nt_status_to_dos_error(uint32_t status) { return RtlNtStatusToDosError(status); }

uint32_t current_kthread() { return g_config.current_thread ? g_config.current_thread() : 0; }

const KernelConfig& kernel_config() { return g_config; }

int64_t to_system_time(std::chrono::system_clock::time_point time) {
    const auto since_unix = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
    return kUnixEpochAsFileTime + since_unix / 100;
}

int64_t current_system_time() { return to_system_time(std::chrono::system_clock::now()); }

bool kernel_init(const KernelConfig& config) {
    g_config = config;
    g_trace = config.trace_calls;
    std::snprintf(g_image_file_name, sizeof(g_image_file_name), "%s", config.image_file_name);
    RtlInitAnsiString(&XeImageFileName, g_image_file_name);
    std::thread(tick_thread).detach();
    settings_init(config);
    if (!memory_init(config.memory) || !filesystem_init(config)) return false;
    if (!config.launch_data_page.empty()) {
        // The new title reads it with XGetLaunchInfo, which frees it.
        LaunchDataPage = allocate_contiguous(kLaunchDataPageSize, kLaunchDataPageSize);
        if (!LaunchDataPage) return false;
        std::memcpy(from_xbox_ptr<void>(LaunchDataPage), config.launch_data_page.data(),
                    std::min<size_t>(config.launch_data_page.size(), kLaunchDataPageSize));
    }
    return true;
}

// 733.33 MHz = 11/15 cycles per nanosecond (integer only: title threads run with the
// title's FPU modes).
int64_t cpu_time_stamp_counter() {
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - g_start).count();
    return ns / 15 * 11 + ns % 15 * 11 / 15;
}

}  // namespace xbox
