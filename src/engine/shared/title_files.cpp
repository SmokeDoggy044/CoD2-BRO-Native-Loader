#include "title_files.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string_view>
#include <system_error>

#include "file.h"
#include "title_objects.h"

namespace game::files {

namespace fs = std::filesystem;

namespace {

// A drive letter's folder. `device` names it in the sort keys (the console's device path, lowercased and hashed
// with the file's path, as the loader's kernel did, so request order stays as in recordings made on it).
struct Drive {
    char letter;
    fs::path root;
    bool read_only;
    const char* device;
    uint64_t total_bytes;  // the size the console reports (retail DVD-9 / hard disk partitions)
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    bool disc;
};

std::vector<Drive>& drives() {
    static auto* list = new std::vector<Drive>;
    return *list;
}

constexpr uint64_t kFileTimeUnixEpoch = 116444736000000000ull;

std::string lowercase(std::string_view text) {
    std::string result(text);
    for (char& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return result;
}

uint32_t error_from(const std::error_code& error) {
    if (error == std::errc::no_such_file_or_directory) return kErrorFileNotFound;
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted) return kErrorAccessDenied;
    if (error == std::errc::no_space_on_device) return kErrorDiskFull;
    if (error == std::errc::file_exists) return kErrorAlreadyExists;
    if (error == std::errc::directory_not_empty) return kErrorDirNotEmpty;
    if (error == std::errc::is_a_directory) return kErrorAccessDenied;
    if (error == std::errc::not_a_directory) return kErrorDirectory;
    if (error == std::errc::read_only_file_system) return kErrorWriteProtect;
    std::fprintf(stderr, "[files] host error %d (%s)\n", error.value(), error.message().c_str());
    return kErrorGenFailure;
}

uint64_t file_time(fs::file_time_type time) {
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(system.time_since_epoch()).count();
    return kFileTimeUnixEpoch + static_cast<uint64_t>(ns / 100);
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

struct Resolved {
    uint32_t error = 0;
    const Drive* drive = nullptr;
    fs::path host;
    std::string key_path;  // device path + components, for the sort key
    bool exists = false;
    bool directory = false;
    bool trailing_backslash = false;
};

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

// "X:\a\b" (also "\??\X:\a\b", forward slashes, repeated separators) to its host file. A missing last component is
// no error (exists = false); a missing or non-directory earlier one is kErrorPathNotFound.
Resolved resolve(std::string_view title_path) {
    Resolved result;
    std::string path(title_path);
    std::replace(path.begin(), path.end(), '/', '\\');
    if (path.size() >= 4 && path.compare(0, 4, "\\??\\") == 0) path.erase(0, 4);
    if (path.size() < 2 || path[1] != ':') {
        result.error = kErrorPathNotFound;
        return result;
    }
    const char letter = static_cast<char>(path[0] >= 'a' && path[0] <= 'z' ? path[0] - 'a' + 'A' : path[0]);
    for (const Drive& drive : drives())
        if (drive.letter == letter) result.drive = &drive;
    if (!result.drive) {
        result.error = kErrorPathNotFound;
        return result;
    }
    result.trailing_backslash = path.size() > 2 && path.back() == '\\';
    std::vector<std::string> components;
    for (size_t start = 2; start < path.size();) {
        size_t end = path.find('\\', start);
        if (end == std::string::npos) end = path.size();
        if (end > start) components.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    result.host = result.drive->root;
    result.key_path = result.drive->device;
    std::error_code error;
    for (size_t i = 0; i < components.size(); ++i) {
        const std::string& component = components[i];
        if (component == "." || component == "..") {
            result.error = kErrorInvalidName;
            return result;
        }
        const fs::path found = find_entry(result.host, component);
        const bool last = i + 1 == components.size();
        if (found.empty()) {
            if (!last) {
                result.error = kErrorPathNotFound;
                return result;
            }
            result.host /= component;
            result.key_path += "\\" + component;
            return result;
        }
        if (!last && !fs::is_directory(found, error)) {
            result.error = kErrorPathNotFound;
            return result;
        }
        result.host = found;
        result.key_path += "\\" + found.filename().string();
    }
    result.exists = true;
    result.directory = fs::is_directory(result.host, error);
    return result;
}

uint32_t attributes_of(const Drive& drive, bool directory) {
    uint32_t attributes = directory ? 0x10 : 0;
    if (drive.read_only) attributes |= 0x01;
    return attributes ? attributes : 0x80;
}

Entry entry_of(const fs::path& path, const Drive& drive, bool directory) {
    Entry entry;
    entry.name = path.filename().string();
    entry.attributes = attributes_of(drive, directory);
    std::error_code error;
    const auto write = fs::last_write_time(path, error);
    if (!error) entry.write_time = file_time(write);
    if (!directory) {
        const auto size = fs::file_size(path, error);
        if (!error) entry.size = size;
    }
    return entry;
}

// The entries of an existing directory matching `mask` (empty = all), sorted by name ignoring case.
std::vector<Entry> listing(const fs::path& dir, const Drive& drive, std::string_view mask) {
    std::vector<Entry> entries;
    std::error_code error;
    for (const auto& item : fs::directory_iterator(dir, error)) {
        const std::string name = item.path().filename().string();
        if (!mask.empty() && !matches(name, mask)) continue;
        std::error_code type_error;
        entries.push_back(entry_of(item.path(), drive, item.is_directory(type_error)));
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return lowercase(a.name) < lowercase(b.name); });
    return entries;
}

// An existing directory, or the error the title saw for it (missing: file not found; a file: kErrorDirectory).
uint32_t existing_directory(const Resolved& r) {
    if (r.error) return r.error;
    if (!r.exists) return kErrorFileNotFound;
    if (!r.directory) return kErrorDirectory;
    return 0;
}

struct OpenFile : objects::Payload {
    ~OpenFile() override {
        if (!delete_on_close) return;
        file.reset();
        std::error_code error;
        fs::remove(host, error);
    }

    std::mutex mutex;
    const Drive* drive = nullptr;
    fs::path host;
    bool directory = false;
    bool overlapped = false;
    bool delete_on_close = false;
    uint32_t key = 0;
    uint64_t position = 0;
    std::shared_ptr<platform::File> file;
};

struct Search : objects::Payload {
    std::mutex mutex;
    std::vector<Entry> entries;
    size_t next = 0;
};

uint32_t transfer(bool writing, uint32_t handle, void* buffer, uint32_t size, const uint64_t* offset, uint32_t* done) {
    *done = 0;
    const auto object = objects::payload_as<OpenFile>(handle);
    if (!object) return kErrorInvalidHandle;
    if (object->directory || !object->file) return 1;  // ERROR_INVALID_FUNCTION
    if (writing && object->drive->read_only) return kErrorWriteProtect;
    std::lock_guard guard(object->mutex);
    if (!offset && object->overlapped) return kErrorInvalidParameter;
    const uint64_t at = offset ? *offset : object->position;
    std::error_code error;
    const bool ok = writing ? object->file->write_at(at, buffer, size, *done, error)
                            : object->file->read_at(at, buffer, size, *done, error);
    if (!object->overlapped) object->position = at + *done;
    if (!ok) return error_from(error);
    if (!writing && *done == 0 && size > 0) return kErrorHandleEof;
    return 0;
}

}  // namespace

void set_folders(const fs::path& data, const fs::path& user) {
    const fs::path e = user / "E";
    const fs::path title = e / "TDATA" / "41560051";
    const fs::path saves = e / "UDATA" / "41560051";
    std::error_code error;
    for (const fs::path& folder : {title, saves, user / "Z"}) fs::create_directories(folder, error);
    if (error) std::fprintf(stderr, "[files] cannot create the user folders in %s\n", user.string().c_str());
    // Sizes as on a retail console: DVD-9, hard disk partition 1 (T:, U:) 4.7 GB, cache partition 750 MB. No E: - the
    // title never had one (XAPI links only D:, T:, U: and the utility drive Z:).
    drives() = {
        {'D', data, true, "\\device\\cdrom0", 0x1A2DB0000ull, 2048, 1, true},
        {'T', title, false, "\\device\\harddisk0\\partition1\\tdata\\41560051", 0x1312D6000ull, 512, 32, false},
        {'U', saves, false, "\\device\\harddisk0\\partition1\\udata\\41560051", 0x1312D6000ull, 512, 32, false},
        {'Z', user / "Z", false, "\\device\\harddisk0\\partition3", 0x2EE00000ull, 512, 32, false},
    };
}

uint32_t open(const char* path, const OpenRequest& request, uint32_t* handle, bool* existed) {
    *handle = 0xffffffff;
    *existed = false;
    const Resolved r = resolve(path);
    if (r.error) return r.error;
    *existed = r.exists;
    const uint32_t d = request.disposition;
    if (r.exists && r.directory && !request.directory)
        return r.trailing_backslash ? kErrorPathNotFound : kErrorAccessDenied;
    if (r.exists && !r.directory && request.only_directory) return kErrorDirectory;
    if (r.exists && d == 1) return kErrorAlreadyExists;
    if (!r.exists && (d == 3 || d == 5)) return kErrorFileNotFound;
    const bool creates = d != 3;
    if (r.drive->read_only &&
        ((!r.directory && request.write) || (!r.exists && creates) || (r.exists && !r.directory && d != 3 && d != 4)))
        return kErrorWriteProtect;

    auto object = std::make_shared<OpenFile>();
    object->drive = r.drive;
    object->host = r.host;
    object->overlapped = request.overlapped;
    object->delete_on_close = request.delete_on_close;
    std::error_code error;
    if (r.exists ? r.directory : request.only_directory) {
        object->directory = true;
        if (!r.exists && !fs::create_directory(r.host, error)) return error ? error_from(error) : kErrorAlreadyExists;
    } else {
        // Creating or truncating needs a writable handle even when only reading follows.
        const bool truncates = r.exists && (d == 2 || d == 5);
        const bool writable = request.write || !r.exists || truncates;
        auto file = platform::File::open(r.host, writable ? platform::File::Access::ReadWrite
                                                          : platform::File::Access::Read,
                                         !r.exists, error);
        if (!file) return error_from(error);
        if (truncates && !file->resize(0, error)) return error_from(error);
        object->file = std::move(file);
    }
    const uint32_t key = static_cast<uint32_t>(std::hash<std::string>{}(lowercase(r.key_path)) & 0x7fffffff);
    object->key = r.drive->disc || !object->directory ? key : 0;
    *handle = objects::create_payload(std::move(object));
    return 0;
}

bool is_file(uint32_t handle) { return objects::payload_as<OpenFile>(handle) != nullptr; }

uint32_t read(uint32_t handle, void* buffer, uint32_t size, const uint64_t* offset, uint32_t* done) {
    return transfer(false, handle, buffer, size, offset, done);
}

uint32_t write(uint32_t handle, const void* buffer, uint32_t size, const uint64_t* offset, uint32_t* done) {
    return transfer(true, handle, const_cast<void*>(buffer), size, offset, done);
}

uint32_t size(uint32_t handle, uint64_t* size) {
    const auto object = objects::payload_as<OpenFile>(handle);
    if (!object) return kErrorInvalidHandle;
    *size = object->file ? object->file->size().value_or(0) : 0;
    return 0;
}

uint32_t position(uint32_t handle, uint64_t* position) {
    const auto object = objects::payload_as<OpenFile>(handle);
    if (!object) return kErrorInvalidHandle;
    std::lock_guard guard(object->mutex);
    *position = object->position;
    return 0;
}

uint32_t set_position(uint32_t handle, uint64_t position) {
    const auto object = objects::payload_as<OpenFile>(handle);
    if (!object) return kErrorInvalidHandle;
    std::lock_guard guard(object->mutex);
    object->position = position;
    return 0;
}

uint32_t set_end(uint32_t handle) {
    const auto object = objects::payload_as<OpenFile>(handle);
    if (!object) return kErrorInvalidHandle;
    if (object->drive->read_only) return kErrorWriteProtect;
    if (!object->file) return kErrorInvalidParameter;
    std::lock_guard guard(object->mutex);
    std::error_code error;
    return object->file->resize(object->position, error) ? 0 : error_from(error);
}

uint32_t touch(uint32_t handle) {
    const auto object = objects::payload_as<OpenFile>(handle);
    if (!object) return kErrorInvalidHandle;
    if (object->drive->read_only) return kErrorWriteProtect;
    std::error_code error;
    fs::last_write_time(object->host, fs::file_time_type::clock::now(), error);
    return error ? error_from(error) : 0;
}

uint32_t sort_key(uint32_t handle, uint32_t* key) {
    const auto object = objects::payload_as<OpenFile>(handle);
    if (!object) return kErrorInvalidHandle;
    *key = object->key;
    return 0;
}

std::shared_ptr<platform::File> host_file(uint32_t handle) {
    const auto object = objects::payload_as<OpenFile>(handle);
    return object ? object->file : nullptr;
}

uint32_t find_first(const char* pattern, uint32_t* handle, Entry* entry) {
    *handle = 0xffffffff;
    const std::string_view text(pattern);
    const size_t split = text.rfind('\\');
    if (split == std::string_view::npos || split + 1 == text.size()) return kErrorInvalidParameter;
    std::string_view mask = text.substr(split + 1);
    if (mask == "*.*") mask = {};
    const Resolved r = resolve(text.substr(0, split + 1));
    if (const uint32_t error = existing_directory(r)) return error;
    auto search = std::make_shared<Search>();
    search->entries = listing(r.host, *r.drive, mask);
    if (search->entries.empty()) return kErrorFileNotFound;
    *entry = search->entries[0];
    search->next = 1;
    *handle = objects::create_payload(std::move(search));
    return 0;
}

uint32_t find_next(uint32_t handle, Entry* entry) {
    const auto search = objects::payload_as<Search>(handle);
    if (!search) return kErrorInvalidHandle;
    std::lock_guard guard(search->mutex);
    if (search->next >= search->entries.size()) return kErrorNoMoreFiles;
    *entry = search->entries[search->next++];
    return 0;
}

uint32_t stat(const char* path, Entry* entry) {
    const Resolved r = resolve(path);
    if (r.error) return r.error;
    if (!r.exists) return kErrorFileNotFound;
    *entry = entry_of(r.host, *r.drive, r.directory);
    return 0;
}

uint32_t list(const char* directory, std::vector<Entry>* entries) {
    const Resolved r = resolve(directory);
    if (const uint32_t error = existing_directory(r)) return error;
    *entries = listing(r.host, *r.drive, {});
    return 0;
}

uint32_t make_directory(const char* path) {
    OpenRequest request;
    request.directory = request.only_directory = true;
    request.disposition = 1;
    uint32_t handle;
    bool existed;
    const uint32_t error = open(path, request, &handle, &existed);
    if (!error) objects::close(handle);
    return error;
}

uint32_t remove_file(const char* path) {
    const Resolved r = resolve(path);
    if (r.error) return r.error;
    if (!r.exists) return kErrorFileNotFound;
    if (r.directory) return r.trailing_backslash ? kErrorPathNotFound : kErrorAccessDenied;
    if (r.drive->read_only) return kErrorWriteProtect;
    std::error_code error;
    fs::remove(r.host, error);
    return error ? error_from(error) : 0;
}

uint32_t remove_directory(const char* path) {
    const Resolved r = resolve(path);
    if (const uint32_t error = existing_directory(r)) return error;
    if (r.drive->read_only) return kErrorWriteProtect;
    std::error_code error;
    if (!fs::is_empty(r.host, error)) return error ? error_from(error) : kErrorDirNotEmpty;
    fs::remove(r.host, error);
    return error ? error_from(error) : 0;
}

uint32_t remove_tree(const char* path) {
    const Resolved r = resolve(path);
    if (const uint32_t error = existing_directory(r)) return error;
    if (r.drive->read_only) return kErrorWriteProtect;
    std::error_code error;
    fs::remove_all(r.host, error);
    return error ? error_from(error) : 0;
}

uint32_t volume(const char* path, Volume* volume) {
    const Resolved r = resolve(path);
    if (const uint32_t error = existing_directory(r)) return error;
    const Drive& drive = *r.drive;
    const uint64_t cluster = uint64_t{drive.bytes_per_sector} * drive.sectors_per_cluster;
    std::error_code error;
    const fs::space_info space = fs::space(drive.root, error);
    volume->total_clusters = drive.total_bytes / cluster;
    volume->free_clusters =
        drive.read_only || error ? 0 : std::min<uint64_t>(space.available, drive.total_bytes) / cluster;
    volume->sectors_per_cluster = drive.sectors_per_cluster;
    volume->bytes_per_sector = drive.bytes_per_sector;
    return 0;
}

}  // namespace game::files
