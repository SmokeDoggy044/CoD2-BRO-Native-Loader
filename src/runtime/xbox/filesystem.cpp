// Files, directories, volumes and symbolic links. See filesystem.h.
//
// Paths: the title names files like "\??\D:\media\foo.ff" (or relative to the DOS
// device root, handle -3, as "d:\media\foo.ff"). Symbolic links turn the drive into
// a device path ("\Device\CdRom0\media\foo.ff"), and the device maps to a host
// folder. The Xbox's file systems are case-insensitive, so each path component is
// matched case-insensitively against the host folder.
//
// All I/O completes before the call returns: the status block is filled in, the
// event (if any) signaled, and an APC completion routine (ReadFileEx) is queued to the
// calling thread, to run at its next alertable wait.

#include "filesystem.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "compiler.h"
#include "dispatcher.h"
#include "file.h"
#include "kernel_internal.h"
#include "log.h"
#include "objects.h"
#include "types.h"

namespace fs = std::filesystem;

namespace xbox {

namespace {

// --- Constants ------------------------------------------------------------------------

constexpr uint32_t kStatusUnsuccessful = 0xC0000001;
constexpr uint32_t kStatusInvalidInfoClass = 0xC0000003;
constexpr uint32_t kStatusInfoLengthMismatch = 0xC0000004;
constexpr uint32_t kStatusNoSuchFile = 0xC000000F;
constexpr uint32_t kStatusInvalidDeviceRequest = 0xC0000010;
constexpr uint32_t kStatusEndOfFile = 0xC0000011;
constexpr uint32_t kStatusAccessDenied = 0xC0000022;
constexpr uint32_t kStatusBufferTooSmall = 0xC0000023;
constexpr uint32_t kStatusObjectNameInvalid = 0xC0000033;
constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;
constexpr uint32_t kStatusObjectNameCollision = 0xC0000035;
constexpr uint32_t kStatusObjectPathNotFound = 0xC000003A;
constexpr uint32_t kStatusDiskFull = 0xC000007F;
constexpr uint32_t kStatusMediaWriteProtected = 0xC00000A2;
constexpr uint32_t kStatusFileIsADirectory = 0xC00000BA;
constexpr uint32_t kStatusDirectoryNotEmpty = 0xC0000101;
constexpr uint32_t kStatusNotADirectory = 0xC0000103;
constexpr uint32_t kStatusBufferOverflow = 0x80000005;
constexpr uint32_t kStatusNoMoreFiles = 0x80000006;

// Create dispositions and what the status block reports.
constexpr uint32_t kFileSupersede = 0, kFileOpen = 1, kFileCreate = 2, kFileOpenIf = 3, kFileOverwrite = 4,
                   kFileOverwriteIf = 5;
constexpr uint32_t kFileSuperseded = 0, kFileOpened = 1, kFileCreated = 2, kFileOverwritten = 3;

// Create options.
constexpr uint32_t kFileDirectoryFile = 0x1;
constexpr uint32_t kFileSynchronousIoAlert = 0x10;
constexpr uint32_t kFileSynchronousIoNonalert = 0x20;
constexpr uint32_t kFileNonDirectoryFile = 0x40;
constexpr uint32_t kFileDeleteOnClose = 0x1000;

// Access rights that need write access to the file's data.
constexpr uint32_t kWriteDataAccess = 0x40000000 /* GENERIC_WRITE */ | 0x10000000 /* GENERIC_ALL */ |
                                      0x2 /* FILE_WRITE_DATA */ | 0x4 /* FILE_APPEND_DATA */;

constexpr uint32_t kAttributeReadOnly = 0x01, kAttributeDirectory = 0x10, kAttributeNormal = 0x80;

constexpr Handle kDosDevicesRoot = 0xFFFFFFFD;  // RootDirectory meaning "\??\"
constexpr const char* kDosDevicesPrefix = "\\??\\";

// ByteOffset values meaning "current position" / "end of file".
constexpr int64_t kUseFilePointerPosition = -2;
constexpr int64_t kWriteToEndOfFile = -1;

// --- Devices ----------------------------------------------------------------------------

struct Device {
    std::string name;  // lowercase, e.g. "\device\cdrom0"
    fs::path root;
    bool read_only;
    uint32_t device_type;  // FILE_DEVICE_CD_ROM 2, FILE_DEVICE_DISK 7
    uint64_t total_bytes;  // reported size; free space is capped to it
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    const char* file_system;
};

struct FileSystemState {
    std::mutex mutex;  // guards links
    std::vector<Device> devices;
    std::map<std::string, std::string> links;  // lowercase link name -> target
};

FileSystemState& g_fs = immortal<FileSystemState>();

std::string lowercase(std::string_view text) {
    std::string result(text);
    for (char& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return result;
}

bool starts_with_component(std::string_view path, std::string_view prefix) {
    return path.size() >= prefix.size() && path.substr(0, prefix.size()) == prefix &&
           (path.size() == prefix.size() || path[prefix.size()] == '\\' || prefix.back() == '\\');
}

std::string_view view(const XbAnsiString* string) {
    if (!string || !string->buffer) return {};
    return {from_xbox_ptr<const char>(string->buffer), string->length};
}

uint32_t status_from(const std::error_code& error) {
    if (error == std::errc::no_such_file_or_directory) return kStatusObjectNameNotFound;
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted) return kStatusAccessDenied;
    if (error == std::errc::no_space_on_device) return kStatusDiskFull;
    if (error == std::errc::file_exists) return kStatusObjectNameCollision;
    if (error == std::errc::directory_not_empty) return kStatusDirectoryNotEmpty;
    if (error == std::errc::is_a_directory) return kStatusFileIsADirectory;
    if (error == std::errc::not_a_directory) return kStatusNotADirectory;
    if (error == std::errc::read_only_file_system) return kStatusMediaWriteProtected;
    LOG_WARN("fs", "host error %d (%s)", error.value(), error.message().c_str());
    return kStatusUnsuccessful;
}

int64_t to_xbox_time(fs::file_time_type time) {
    return to_system_time(std::chrono::clock_cast<std::chrono::system_clock>(time));
}

// --- Objects ----------------------------------------------------------------------------

// A file or directory handle. Its body is a FILE_OBJECT followed by its FsContext.
// XAPI reads FsContext to get a file's physical position (a sort key for reads):
// on GDFX (the disc) the first dword, on FATX the dword at +0x1C unless bit 0 of the
// first byte marks a directory. Any stable per-file value works, so it is a hash of
// the path. Waits on the handle wait for the FILE_OBJECT's event, signaled when an
// I/O operation completes.
class FileObject : public BodyObject {
public:
    static constexpr size_t kContextSize = 0x20;

    FileObject() : BodyObject(sizeof(XbFileObject) + kContextSize, object_types::file) {
        XbFileObject& object = *as<XbFileObject>();
        object.type = 5;  // IO_TYPE_FILE
        object.fs_context = body() + sizeof(XbFileObject);
        for (XbKEvent* event : {&object.lock, &object.event}) {
            event->header.type = event == &object.lock ? kXbEventSynchronization : kXbEventNotification;
            event->header.size = sizeof(XbKEvent) / 4;
            event->header.wait_list_flink = event->header.wait_list_blink = to_xbox_ptr(&event->header.wait_list_flink);
        }
    }
    ~FileObject() override {
        if (!delete_on_close) return;
        file.reset();
        std::error_code error;
        fs::remove(host, error);
    }

    XbPtr wait_target() const override { return body() + offsetof(XbFileObject, event); }
    XbDispatcherHeader& done_event() { return as<XbFileObject>()->event.header; }

    void set_sort_key(bool gdfx) {
        auto* context = from_xbox_ptr<uint8_t>(as<XbFileObject>()->fs_context);
        const auto key = static_cast<uint32_t>(std::hash<std::string>{}(lowercase(xbox_path)) & 0x7FFFFFFF);
        std::memset(context, 0, kContextSize);
        if (gdfx) {
            std::memcpy(context, &key, 4);
        } else {
            context[0] = directory ? 1 : 0;
            std::memcpy(context + 0x1C, &key, 4);
        }
    }

    std::mutex mutex;
    std::string xbox_path;  // resolved device path, e.g. "\Device\CdRom0\media\foo.ff"
    fs::path host;
    const Device* device = nullptr;
    bool directory = false;
    bool synchronous = false;
    bool delete_on_close = false;
    uint64_t position = 0;
    std::shared_ptr<platform::File> file;  // shared with media objects (host_file)
    // NtQueryDirectoryFile state.
    bool listing_started = false;
    std::vector<fs::directory_entry> listing;
    size_t next_entry = 0;
};

class SymbolicLinkObject : public Object {
public:
    explicit SymbolicLinkObject(std::string target) : target(std::move(target)) {}
    const XbObjectType* type() const override { return &object_types::symbolic_link; }
    std::string target;
};

// --- Path resolution --------------------------------------------------------------------

struct Resolved {
    uint32_t status = kStatusSuccess;
    const Device* device = nullptr;
    std::string xbox_path;  // device path with the original spelling of found components
    fs::path host;
    bool exists = false;
    bool directory = false;
};

// Full object name from OBJECT_ATTRIBUTES: applies RootDirectory.
uint32_t full_name(const XbObjectAttributes* attributes, std::string& name) {
    if (!attributes) return kStatusInvalidParameter;
    name = std::string(view(from_xbox_ptr<const XbAnsiString>(attributes->object_name)));
    if (attributes->root_directory == kDosDevicesRoot) {
        name = kDosDevicesPrefix + name;
    } else if (attributes->root_directory) {
        const auto root = lookup_handle_as<FileObject>(attributes->root_directory);
        if (!root) return kStatusInvalidHandle;
        name = root->xbox_path + (name.empty() || name.front() == '\\' ? "" : "\\") + name;
    }
    return kStatusSuccess;
}

// Applies symbolic links until the path starts with a device.
std::string follow_links(std::string path) {
    std::lock_guard lock(g_fs.mutex);
    for (int depth = 0; depth < 8; ++depth) {
        const std::string lower = lowercase(path);
        bool replaced = false;
        for (const auto& [link, target] : g_fs.links) {
            if (starts_with_component(lower, link)) {
                path = target + path.substr(link.size());
                replaced = true;
                break;
            }
        }
        if (!replaced) break;
    }
    return path;
}

// Finds `name` in `dir`, exactly or ignoring case. Empty if there is none.
fs::path find_entry(const fs::path& dir, const std::string& name) {
    std::error_code error;
    const fs::path exact = dir / name;
    if (fs::exists(fs::symlink_status(exact, error))) return exact;
    const std::string lower = lowercase(name);
    for (const auto& entry : fs::directory_iterator(dir, error))
        if (lowercase(entry.path().filename().string()) == lower) return entry.path();
    return {};
}

Resolved resolve(const std::string& name) {
    Resolved result;
    std::string path = follow_links(name);
    // Normalize: forward slashes and repeated separators.
    std::replace(path.begin(), path.end(), '/', '\\');
    path.erase(std::unique(path.begin(), path.end(), [](char a, char b) { return a == '\\' && b == '\\'; }),
               path.end());

    const std::string lower = lowercase(path);
    for (const Device& device : g_fs.devices)
        if (starts_with_component(lower, device.name)) result.device = &device;
    if (!result.device) {
        result.status = kStatusObjectPathNotFound;
        return result;
    }

    // Split the rest into components.
    std::vector<std::string> components;
    for (size_t start = result.device->name.size(); start < path.size();) {
        size_t end = path.find('\\', start);
        if (end == std::string::npos) end = path.size();
        if (end > start) components.push_back(path.substr(start, end - start));
        start = end + 1;
    }

    result.xbox_path = path.substr(0, result.device->name.size());
    result.host = result.device->root;
    std::error_code error;
    for (size_t i = 0; i < components.size(); ++i) {
        const std::string& component = components[i];
        if (component == "." || component == "..") {
            result.status = kStatusObjectNameInvalid;
            return result;
        }
        const fs::path found = find_entry(result.host, component);
        const bool last = i + 1 == components.size();
        if (found.empty()) {
            if (!last) {
                result.status = kStatusObjectPathNotFound;
                return result;
            }
            result.host /= component;
            result.xbox_path += "\\" + component;
            return result;  // does not exist (yet)
        }
        if (!last && !fs::is_directory(found, error)) {
            result.status = kStatusObjectPathNotFound;
            return result;
        }
        result.host = found;
        result.xbox_path += "\\" + found.filename().string();
    }
    result.exists = true;
    result.directory = fs::is_directory(result.host, error);
    return result;
}

// --- Helpers ----------------------------------------------------------------------------

void complete(XbIoStatusBlock* io_status, uint32_t status, uint32_t information) {
    if (!io_status) return;
    io_status->status = status;
    io_status->information = information;
}


uint32_t attributes_of(const Device& device, bool directory) {
    uint32_t attributes = directory ? kAttributeDirectory : 0;
    if (device.read_only) attributes |= kAttributeReadOnly;
    return attributes ? attributes : kAttributeNormal;
}

struct FileTimes {
    int64_t write_time = 0;
    uint64_t size = 0;
};

FileTimes times_of(const fs::path& path, bool directory) {
    FileTimes times;
    std::error_code error;
    const auto write = fs::last_write_time(path, error);
    if (!error) times.write_time = to_xbox_time(write);
    if (!directory) {
        const auto size = fs::file_size(path, error);
        if (!error) times.size = size;
    }
    return times;
}

uint64_t cluster_bytes(const Device& device) {
    return uint64_t{device.bytes_per_sector} * device.sectors_per_cluster;
}

uint64_t round_to_cluster(uint64_t size, const Device& device) {
    const uint64_t cluster = cluster_bytes(device);
    return (size + cluster - 1) / cluster * cluster;
}

void fill_network_open(XbFileNetworkOpenInformation& info, const fs::path& path, const Device& device, bool directory) {
    const FileTimes times = times_of(path, directory);
    info = {};
    info.creation_time = info.last_access_time = info.last_write_time = info.change_time = times.write_time;
    info.end_of_file = static_cast<int64_t>(times.size);
    info.allocation_size = static_cast<int64_t>(round_to_cluster(times.size, device));
    info.file_attributes = attributes_of(device, directory);
}

// DOS wildcard match (* and ?), ignoring case.
bool matches(std::string_view name, std::string_view pattern) {
    size_t n = 0, p = 0, star = std::string_view::npos, mark = 0;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(name[n]))) {
            ++n, ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// --- Exports: symbolic links --------------------------------------------------------------

uint32_t XB_STDCALL IoCreateSymbolicLink(XbAnsiString* link_name, XbAnsiString* device_name) {
    return create_symbolic_link(view(link_name), view(device_name));
}

uint32_t XB_STDCALL IoDeleteSymbolicLink(XbAnsiString* link_name) {
    std::lock_guard lock(g_fs.mutex);
    const bool erased = g_fs.links.erase(lowercase(view(link_name))) > 0;
    KTRACE("IoDeleteSymbolicLink(%.*s)%s", int(link_name->length), from_xbox_ptr<const char>(link_name->buffer),
           erased ? "" : " -> not found");
    return erased ? kStatusSuccess : kStatusObjectNameNotFound;
}

uint32_t XB_STDCALL NtOpenSymbolicLinkObject(Handle* link_handle, XbObjectAttributes* attributes) {
    std::string name;
    if (const uint32_t status = full_name(attributes, name); status != kStatusSuccess) return status;
    std::string target;
    {
        std::lock_guard lock(g_fs.mutex);
        const auto it = g_fs.links.find(lowercase(name));
        if (it == g_fs.links.end()) return kStatusObjectNameNotFound;
        target = it->second;
    }
    *link_handle = insert_handle(std::make_shared<SymbolicLinkObject>(target));
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtQuerySymbolicLinkObject(Handle link_handle, XbAnsiString* link_target,
                                              uint32_t* returned_length) {
    const auto link = lookup_handle_as<SymbolicLinkObject>(link_handle);
    if (!link) return kStatusInvalidHandle;
    const auto length = static_cast<uint32_t>(link->target.size());
    if (returned_length) *returned_length = length;
    if (link_target->maximum_length < length) return kStatusBufferTooSmall;
    std::memcpy(from_xbox_ptr<char>(link_target->buffer), link->target.data(), length);
    link_target->length = static_cast<uint16_t>(length);
    return kStatusSuccess;
}

// --- Exports: opening ----------------------------------------------------------------------

uint32_t XB_STDCALL NtCreateFile(Handle* file_handle, uint32_t desired_access, XbObjectAttributes* object_attributes,
                                 XbIoStatusBlock* io_status, int64_t* allocation_size, uint32_t file_attributes,
                                 uint32_t share_access, uint32_t create_disposition, uint32_t create_options) {
    (void)allocation_size, (void)file_attributes, (void)share_access;
    std::string name;
    uint32_t status = full_name(object_attributes, name);
    if (status != kStatusSuccess) return status;
    const Resolved r = resolve(name);
    auto fail = [&](uint32_t failure) {
        KTRACE("NtCreateFile(%s, access=%x, disposition=%u, options=%x) -> %08x", name.c_str(), desired_access,
               create_disposition, create_options, failure);
        complete(io_status, failure, 0);
        return failure;
    };
    if (r.status != kStatusSuccess) return fail(r.status);

    const bool want_directory = create_options & kFileDirectoryFile;
    const bool want_write = desired_access & kWriteDataAccess;
    const bool creates = create_disposition != kFileOpen;
    if (r.exists && r.directory && (create_options & kFileNonDirectoryFile)) return fail(kStatusFileIsADirectory);
    if (r.exists && !r.directory && want_directory) return fail(kStatusNotADirectory);
    if (r.exists && create_disposition == kFileCreate) return fail(kStatusObjectNameCollision);
    if (!r.exists && (create_disposition == kFileOpen || create_disposition == kFileOverwrite))
        return fail(kStatusObjectNameNotFound);
    if (r.device->read_only && ((!r.directory && want_write) || (!r.exists && creates) ||
                                (r.exists && !r.directory && create_disposition != kFileOpen &&
                                 create_disposition != kFileOpenIf)))
        return fail(kStatusMediaWriteProtected);

    auto object = std::make_shared<FileObject>();
    object->xbox_path = r.xbox_path;
    object->host = r.host;
    object->device = r.device;
    object->synchronous = create_options & (kFileSynchronousIoAlert | kFileSynchronousIoNonalert);
    object->delete_on_close = create_options & kFileDeleteOnClose;

    uint32_t information = kFileOpened;
    std::error_code error;
    if (r.exists ? r.directory : want_directory) {
        object->directory = true;
        if (!r.exists) {
            if (!fs::create_directory(r.host, error)) return fail(error ? status_from(error) : kStatusObjectNameCollision);
            information = kFileCreated;
        }
    } else {
        const auto access = r.device->read_only || !want_write ? platform::File::Access::Read
                                                                : platform::File::Access::ReadWrite;
        // Creating or truncating needs a writable handle even when only reading follows.
        const bool needs_write = !r.exists || (create_disposition != kFileOpen && create_disposition != kFileOpenIf);
        object->file = platform::File::open(r.host, needs_write ? platform::File::Access::ReadWrite : access,
                                            !r.exists, error);
        if (!object->file) return fail(status_from(error));
        if (!r.exists) {
            information = kFileCreated;
        } else if (create_disposition == kFileSupersede || create_disposition == kFileOverwrite ||
                   create_disposition == kFileOverwriteIf) {
            if (!object->file->resize(0, error)) return fail(status_from(error));
            information = create_disposition == kFileSupersede ? kFileSuperseded : kFileOverwritten;
        }
    }
    object->set_sort_key(std::strcmp(r.device->file_system, "GDFX") == 0);
    *file_handle = insert_handle(object);
    complete(io_status, kStatusSuccess, information);
    KTRACE("NtCreateFile(%s, access=%x, disposition=%u, options=%x) -> %x (%s)", name.c_str(), desired_access,
           create_disposition, create_options, *file_handle, r.host.string().c_str());
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtOpenFile(Handle* file_handle, uint32_t desired_access, XbObjectAttributes* object_attributes,
                               XbIoStatusBlock* io_status, uint32_t share_access, uint32_t open_options) {
    return NtCreateFile(file_handle, desired_access, object_attributes, io_status, nullptr, 0, share_access,
                        kFileOpen, open_options);
}

// --- Exports: reading and writing -----------------------------------------------------------

uint32_t do_io(bool write, Handle file_handle, Handle event, XbPtr apc_routine, XbPtr apc_context,
               XbIoStatusBlock* io_status, void* buffer, uint32_t length, const int64_t* byte_offset) {
    const auto object = lookup_handle_as<FileObject>(file_handle);
    if (!object) return kStatusInvalidHandle;
    if (object->directory || !object->file) return kStatusInvalidDeviceRequest;
    if (write && object->device->read_only) return kStatusMediaWriteProtected;

    set_event_handle(event, 0);
    XbDispatcherHeader& done_event = object->done_event();
    signal_object(done_event, 0);

    uint32_t status = kStatusSuccess;
    uint32_t transferred = 0;
    {
        std::lock_guard lock(object->mutex);
        uint64_t offset = object->position;
        if (byte_offset && *byte_offset == kWriteToEndOfFile && write)
            offset = object->file->size().value_or(0);
        else if (byte_offset && *byte_offset >= 0)
            offset = static_cast<uint64_t>(*byte_offset);
        else if (!object->synchronous && !(byte_offset && *byte_offset == kUseFilePointerPosition))
            return kStatusInvalidParameter;

        std::error_code error;
        const bool ok = write ? object->file->write_at(offset, buffer, length, transferred, error)
                              : object->file->read_at(offset, buffer, length, transferred, error);
        if (!ok)
            status = status_from(error);
        else if (!write && transferred == 0 && length > 0)
            status = kStatusEndOfFile;
        if (object->synchronous) object->position = offset + transferred;
    }
    complete(io_status, status, transferred);
    signal_object(done_event, 1);
    set_event_handle(event, 1);
    if (apc_routine) queue_user_apc(current_kthread(), apc_routine, apc_context, to_xbox_ptr(io_status), 0);
    // Asynchronous handles: the I/O is already complete (status block set, event
    // signalled), but report it as pending like the Xbox's disk drivers do. Titles rely
    // on it: CoD2: BRO's save reader treats a ReadFile that returns TRUE on an
    // overlapped handle as the end of the file.
    if (!object->synchronous && status == kStatusSuccess) return kStatusPending;
    return status;
}

uint32_t XB_STDCALL NtReadFile(Handle file_handle, Handle event, XbPtr apc_routine, XbPtr apc_context,
                               XbIoStatusBlock* io_status, void* buffer, uint32_t length, int64_t* byte_offset) {
    const uint32_t status =
        do_io(false, file_handle, event, apc_routine, apc_context, io_status, buffer, length, byte_offset);
    KTRACE("NtReadFile(%x, %u bytes at %lld) -> %08x, %u", file_handle, length,
           byte_offset ? static_cast<long long>(*byte_offset) : -1LL, status, io_status ? io_status->information : 0);
    return status;
}

uint32_t XB_STDCALL NtWriteFile(Handle file_handle, Handle event, XbPtr apc_routine, XbPtr apc_context,
                                XbIoStatusBlock* io_status, const void* buffer, uint32_t length,
                                int64_t* byte_offset) {
    const uint32_t status = do_io(true, file_handle, event, apc_routine, apc_context, io_status,
                                  const_cast<void*>(buffer), length, byte_offset);
    KTRACE("NtWriteFile(%x, %u bytes at %lld) -> %08x", file_handle, length,
           byte_offset ? static_cast<long long>(*byte_offset) : -1LL, status);
    return status;
}

uint32_t XB_STDCALL NtFlushBuffersFile(Handle file_handle, XbIoStatusBlock* io_status) {
    const auto object = lookup_handle_as<FileObject>(file_handle);
    if (!object) return kStatusInvalidHandle;
    if (object->file) object->file->flush();
    complete(io_status, kStatusSuccess, 0);
    return kStatusSuccess;
}

// --- Exports: information -------------------------------------------------------------------

template <typename T>
bool fits(uint32_t length) {
    return length >= sizeof(T);
}

uint32_t XB_STDCALL NtQueryInformationFile(Handle file_handle, XbIoStatusBlock* io_status, void* information,
                                           uint32_t length, uint32_t information_class) {
    const auto object = lookup_handle_as<FileObject>(file_handle);
    if (!object) return kStatusInvalidHandle;
    std::lock_guard lock(object->mutex);
    const FileTimes times = times_of(object->host, object->directory);
    uint32_t written = 0;
    switch (information_class) {
        case 4: {  // FileBasicInformation
            if (!fits<XbFileBasicInformation>(length)) return kStatusInfoLengthMismatch;
            auto& info = *static_cast<XbFileBasicInformation*>(information);
            info = {};
            info.creation_time = info.last_access_time = info.last_write_time = info.change_time = times.write_time;
            info.file_attributes = attributes_of(*object->device, object->directory);
            written = sizeof(info);
            break;
        }
        case 5: {  // FileStandardInformation
            if (!fits<XbFileStandardInformation>(length)) return kStatusInfoLengthMismatch;
            auto& info = *static_cast<XbFileStandardInformation*>(information);
            info = {};
            const uint64_t size = object->file ? object->file->size().value_or(0) : 0;
            info.end_of_file = static_cast<int64_t>(size);
            info.allocation_size = static_cast<int64_t>(round_to_cluster(size, *object->device));
            info.number_of_links = 1;
            info.delete_pending = object->delete_on_close;
            info.directory = object->directory;
            written = sizeof(info);
            break;
        }
        case 9: {  // FileNameInformation: the path below the device
            const std::string name = object->xbox_path.substr(object->device->name.size());
            if (length < 4) return kStatusInfoLengthMismatch;
            auto& info = *static_cast<XbFileNameInformation*>(information);
            info.file_name_length = static_cast<uint32_t>(name.size());
            const uint32_t room = length - 4;
            std::memcpy(info.file_name, name.data(), std::min<size_t>(room, name.size()));
            complete(io_status, room < name.size() ? kStatusBufferOverflow : kStatusSuccess,
                     4 + std::min<uint32_t>(room, static_cast<uint32_t>(name.size())));
            return room < name.size() ? kStatusBufferOverflow : kStatusSuccess;
        }
        case 14: {  // FilePositionInformation
            if (length < 8) return kStatusInfoLengthMismatch;
            *static_cast<int64_t*>(information) = static_cast<int64_t>(object->position);
            written = 8;
            break;
        }
        case 34: {  // FileNetworkOpenInformation
            if (!fits<XbFileNetworkOpenInformation>(length)) return kStatusInfoLengthMismatch;
            fill_network_open(*static_cast<XbFileNetworkOpenInformation*>(information), object->host,
                              *object->device, object->directory);
            written = sizeof(XbFileNetworkOpenInformation);
            break;
        }
        default:
            LOG_WARN("fs", "NtQueryInformationFile: class %u not supported", information_class);
            return kStatusInvalidInfoClass;
    }
    complete(io_status, kStatusSuccess, written);
    return kStatusSuccess;
}

uint32_t rename_file(FileObject& object, const XbFileRenameInformation& info) {
    std::string name(view(&info.file_name));
    if (info.root_directory == kDosDevicesRoot) {
        name = kDosDevicesPrefix + name;
    } else if (info.root_directory) {
        const auto root = lookup_handle_as<FileObject>(info.root_directory);
        if (!root) return kStatusInvalidHandle;
        name = root->xbox_path + "\\" + name;
    }
    const Resolved target = resolve(name);
    if (target.status != kStatusSuccess) return target.status;
    if (target.device != object.device) return kStatusAccessDenied;  // NOT_SAME_DEVICE in NT
    if (target.exists && !info.replace_if_exists) return kStatusObjectNameCollision;
    std::error_code error;
    fs::rename(object.host, target.host, error);
    if (error) return status_from(error);
    object.host = target.host;
    object.xbox_path = target.xbox_path;
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtSetInformationFile(Handle file_handle, XbIoStatusBlock* io_status, void* information,
                                         uint32_t length, uint32_t information_class) {
    const auto object = lookup_handle_as<FileObject>(file_handle);
    if (!object) return kStatusInvalidHandle;
    std::lock_guard lock(object->mutex);
    if (object->device->read_only && information_class != 14) return kStatusMediaWriteProtected;
    uint32_t status = kStatusSuccess;
    std::error_code error;
    switch (information_class) {
        case 4:  // FileBasicInformation: times and attributes are not stored
            if (!fits<XbFileBasicInformation>(length)) return kStatusInfoLengthMismatch;
            break;
        case 10:  // FileRenameInformation
            if (!fits<XbFileRenameInformation>(length)) return kStatusInfoLengthMismatch;
            status = rename_file(*object, *static_cast<const XbFileRenameInformation*>(information));
            break;
        case 13: {  // FileDispositionInformation
            if (length < 1) return kStatusInfoLengthMismatch;
            const bool remove = *static_cast<const uint8_t*>(information);
            if (remove && object->directory && !fs::is_empty(object->host, error)) return kStatusDirectoryNotEmpty;
            object->delete_on_close = remove;
            break;
        }
        case 14:  // FilePositionInformation
            if (length < 8) return kStatusInfoLengthMismatch;
            object->position = static_cast<uint64_t>(*static_cast<const int64_t*>(information));
            break;
        case 19:    // FileAllocationInformation: only shrinking changes the file
        case 20: {  // FileEndOfFileInformation
            if (length < 8) return kStatusInfoLengthMismatch;
            if (!object->file) return kStatusInvalidParameter;
            const auto size = static_cast<uint64_t>(*static_cast<const int64_t*>(information));
            if (information_class == 20 || size < object->file->size().value_or(0))
                if (!object->file->resize(size, error)) status = status_from(error);
            break;
        }
        default:
            LOG_WARN("fs", "NtSetInformationFile: class %u not supported", information_class);
            return kStatusInvalidInfoClass;
    }
    complete(io_status, status, 0);
    return status;
}

uint32_t XB_STDCALL NtQueryFullAttributesFile(XbObjectAttributes* object_attributes,
                                              XbFileNetworkOpenInformation* attributes) {
    std::string name;
    if (const uint32_t status = full_name(object_attributes, name); status != kStatusSuccess) return status;
    const Resolved r = resolve(name);
    if (r.status != kStatusSuccess) return r.status;
    if (!r.exists) return kStatusObjectNameNotFound;
    fill_network_open(*attributes, r.host, *r.device, r.directory);
    return kStatusSuccess;
}

// One entry per call, as the Xbox does. FileDirectoryInformation (class 1) only.
uint32_t XB_STDCALL NtQueryDirectoryFile(Handle file_handle, Handle event, XbPtr apc_routine, XbPtr apc_context,
                                         XbIoStatusBlock* io_status, void* information, uint32_t length,
                                         uint32_t information_class, XbAnsiString* file_mask, uint32_t restart_scan) {
    const auto object = lookup_handle_as<FileObject>(file_handle);
    if (!object) return kStatusInvalidHandle;
    if (!object->directory) return kStatusInvalidParameter;
    if (information_class != 1) {
        LOG_WARN("fs", "NtQueryDirectoryFile: class %u not supported", information_class);
        return kStatusInvalidInfoClass;
    }
    if (length < sizeof(XbFileDirectoryInformation)) return kStatusInfoLengthMismatch;

    std::lock_guard lock(object->mutex);
    const bool first = !object->listing_started || (restart_scan & 0xFF);
    if (first) {
        // The mask given with the first call applies to the whole scan.
        const std::string mask(view(file_mask));
        object->listing.clear();
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(object->host, error))
            if (mask.empty() || matches(entry.path().filename().string(), mask)) object->listing.push_back(entry);
        std::sort(object->listing.begin(), object->listing.end(), [](const auto& a, const auto& b) {
            return lowercase(a.path().filename().string()) < lowercase(b.path().filename().string());
        });
        object->listing_started = true;
        object->next_entry = 0;
    }
    uint32_t status;
    if (object->next_entry >= object->listing.size()) {
        status = first ? kStatusNoSuchFile : kStatusNoMoreFiles;
        complete(io_status, status, 0);
    } else {
        const fs::directory_entry& entry = object->listing[object->next_entry++];
        std::error_code error;
        const bool directory = entry.is_directory(error);
        XbFileNetworkOpenInformation info;
        fill_network_open(info, entry.path(), *object->device, directory);
        auto& out = *static_cast<XbFileDirectoryInformation*>(information);
        out = {};
        out.file_index = static_cast<uint32_t>(object->next_entry);
        out.creation_time = info.creation_time;
        out.last_access_time = info.last_access_time;
        out.last_write_time = info.last_write_time;
        out.change_time = info.change_time;
        out.end_of_file = info.end_of_file;
        out.allocation_size = info.allocation_size;
        out.file_attributes = info.file_attributes;
        const std::string name = entry.path().filename().string();
        const uint32_t room = length - sizeof(XbFileDirectoryInformation);
        const uint32_t copied = std::min<uint32_t>(room, static_cast<uint32_t>(name.size()));
        out.file_name_length = static_cast<uint32_t>(name.size());
        std::memcpy(static_cast<char*>(information) + sizeof(XbFileDirectoryInformation), name.data(), copied);
        status = copied < name.size() ? kStatusBufferOverflow : kStatusSuccess;
        complete(io_status, status, sizeof(XbFileDirectoryInformation) + copied);
    }
    set_event_handle(event, 1);
    if (apc_routine) queue_user_apc(current_kthread(), apc_routine, apc_context, to_xbox_ptr(io_status), 0);
    return status;
}

uint32_t query_volume(const FileObject& object, XbIoStatusBlock* io_status, void* information, uint32_t length,
                      uint32_t information_class) {
    const Device& device = *object.device;
    const uint64_t cluster = cluster_bytes(device);
    std::error_code error;
    const fs::space_info space = fs::space(device.root, error);
    const uint64_t total = device.total_bytes / cluster;
    const uint64_t available = device.read_only || error ? 0 : std::min<uint64_t>(space.available, device.total_bytes) / cluster;

    uint32_t written = 0;
    switch (information_class) {
        case 3: {  // FileFsSizeInformation
            if (!fits<XbFileFsSizeInformation>(length)) return kStatusInfoLengthMismatch;
            auto& info = *static_cast<XbFileFsSizeInformation*>(information);
            info = {static_cast<int64_t>(total), static_cast<int64_t>(available), device.sectors_per_cluster,
                    device.bytes_per_sector};
            written = sizeof(info);
            break;
        }
        case 4: {  // FileFsDeviceInformation
            if (!fits<XbFileFsDeviceInformation>(length)) return kStatusInfoLengthMismatch;
            auto& info = *static_cast<XbFileFsDeviceInformation*>(information);
            info.device_type = device.device_type;
            info.characteristics = device.read_only ? 0x3 /* removable, read-only */ : 0;
            written = sizeof(info);
            break;
        }
        case 5: {  // FileFsAttributeInformation
            const auto name_length = static_cast<uint32_t>(std::strlen(device.file_system));
            if (length < sizeof(XbFileFsAttributeInformation) + name_length) return kStatusBufferOverflow;
            auto& info = *static_cast<XbFileFsAttributeInformation*>(information);
            info.file_system_attributes = 0;
            info.maximum_component_name_length = 42;  // FATX limit
            info.file_system_name_length = name_length;
            std::memcpy(static_cast<char*>(information) + sizeof(info), device.file_system, name_length);
            written = sizeof(info) + name_length;
            break;
        }
        case 7: {  // FileFsFullSizeInformation
            if (!fits<XbFileFsFullSizeInformation>(length)) return kStatusInfoLengthMismatch;
            auto& info = *static_cast<XbFileFsFullSizeInformation*>(information);
            info = {static_cast<int64_t>(total), static_cast<int64_t>(available), static_cast<int64_t>(available),
                    device.sectors_per_cluster, device.bytes_per_sector};
            written = sizeof(info);
            break;
        }
        default:
            LOG_WARN("fs", "NtQueryVolumeInformationFile: class %u not supported", information_class);
            return kStatusInvalidInfoClass;
    }
    complete(io_status, kStatusSuccess, written);
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtQueryVolumeInformationFile(Handle file_handle, XbIoStatusBlock* io_status, void* information,
                                                 uint32_t length, uint32_t information_class) {
    const auto object = lookup_handle_as<FileObject>(file_handle);
    if (!object) return kStatusInvalidHandle;
    return query_volume(*object, io_status, information, length, information_class);
}

// The same for a FILE_OBJECT (from ObReferenceObjectByHandle).
uint32_t XB_STDCALL IoQueryVolumeInformation(XbFileObject* file_object, uint32_t information_class, uint32_t length,
                                             void* information, uint32_t* returned_length) {
    const auto object = std::dynamic_pointer_cast<FileObject>(lookup_referenced_body(to_xbox_ptr(file_object)));
    if (!object) return kStatusInvalidParameter;
    XbIoStatusBlock io{};
    const uint32_t status = query_volume(*object, &io, information, length, information_class);
    if (returned_length) *returned_length = io.information;
    return status;
}

const KernelImpl kFileSystemImpls[] = {
    KIMPL(IoCreateSymbolicLink),
    KIMPL(IoDeleteSymbolicLink),
    KIMPL(IoQueryVolumeInformation),
    KIMPL(NtCreateFile),
    KIMPL(NtFlushBuffersFile),
    KIMPL(NtOpenFile),
    KIMPL(NtOpenSymbolicLinkObject),
    KIMPL(NtQueryDirectoryFile),
    KIMPL(NtQueryFullAttributesFile),
    KIMPL(NtQueryInformationFile),
    KIMPL(NtQuerySymbolicLinkObject),
    KIMPL(NtQueryVolumeInformationFile),
    KIMPL(NtReadFile),
    KIMPL(NtSetInformationFile),
    KIMPL(NtWriteFile),
};

const char* kCachePartition = "\\Device\\Harddisk0\\Partition3";

}  // namespace

bool filesystem_init(const KernelConfig& config) {
    std::lock_guard lock(g_fs.mutex);
    g_fs.devices.clear();
    g_fs.links.clear();
    if (!config.cdrom_root.empty())
        g_fs.devices.push_back({"\\device\\cdrom0", config.cdrom_root, true, 2, 0x1A2DB0000ull /* DVD-9 */, 2048, 1,
                                "GDFX"});  // the Xbox DVD file system; XAPI checks the name
    if (!config.data_root.empty()) {
        // Sizes as on a retail hard disk: partition 1 (E:) 4.7 GB, cache partitions 750 MB.
        const fs::path e = config.data_root / "E";
        const fs::path z = config.data_root / "Z";
        std::error_code error;
        fs::create_directories(e, error);
        fs::create_directories(z, error);
        if (error) {
            LOG_ERROR("fs", "cannot create the data folders in %s: %s", config.data_root.string().c_str(),
                      error.message().c_str());
            return false;
        }
        g_fs.devices.push_back({"\\device\\harddisk0\\partition1", e, false, 7, 0x1312D6000ull, 512, 32, "FATX"});
        g_fs.devices.push_back({lowercase(kCachePartition), z, false, 7, 0x2EE00000ull, 512, 32, "FATX"});
    }
    for (const Device& device : g_fs.devices)
        LOG_INFO("fs", "%s -> %s%s", device.name.c_str(), device.root.string().c_str(),
                 device.read_only ? " (read-only)" : "");
    return true;
}

uint32_t create_symbolic_link(std::string_view link_name, std::string_view target) {
    std::lock_guard lock(g_fs.mutex);
    const auto [it, inserted] = g_fs.links.emplace(lowercase(link_name), std::string(target));
    KTRACE("IoCreateSymbolicLink(%.*s -> %.*s)%s", int(link_name.size()), link_name.data(), int(target.size()),
           target.data(), inserted ? "" : " -> exists");
    return inserted ? kStatusSuccess : kStatusObjectNameCollision;
}

fs::path host_file_path(std::string_view xbox_path) {
    const Resolved r = resolve(std::string(xbox_path));
    return r.status == kStatusSuccess && r.exists && !r.directory ? r.host : fs::path();
}

std::shared_ptr<platform::File> host_file(Handle handle) {
    const auto object = lookup_handle_as<FileObject>(handle);
    return object ? object->file : nullptr;
}

uint32_t mount_utility_drive(bool format) {
    const Device* cache = nullptr;
    for (const Device& device : g_fs.devices)
        if (device.name == lowercase(kCachePartition)) cache = &device;
    if (!cache) return kStatusObjectPathNotFound;
    if (format) {
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(cache->root, error)) fs::remove_all(entry.path(), error);
        LOG_INFO("fs", "utility drive formatted (%s emptied)", cache->root.string().c_str());
    }
    const uint32_t status = create_symbolic_link("\\??\\Z:", kCachePartition);
    return status == kStatusObjectNameCollision ? kStatusSuccess : status;
}

std::span<const KernelImpl> filesystem_kernel_impls() { return kFileSystemImpls; }

}  // namespace xbox
