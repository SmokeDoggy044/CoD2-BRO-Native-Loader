#pragma once

// The title's files on host folders (own engine host step 5c, used when native_platform() is on): the game names
// files by drive letter ("D:\sp\global.cod", "U:\<save>\SaveMeta.xbx"); this maps them to host folders in one place
// and keeps open files and searches behind our handles (title_objects.h). Our XAPI C (title_xapi.cpp,
// title_savegame.cpp) builds the Win32 behaviour on top.
//
//   D:  the game data (read-only)          T:  <user>/E/TDATA/41560051 (title data)
//   Z:  <user>/Z (cache)                   U:  <user>/E/UDATA/41560051 (save games)
//
// The same folders the loader's kernel used, so existing saves load. Path components match ignoring case.
// Results are Win32 error codes (0 = success), as the title's XAPI reported them.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace platform {
class File;
}

namespace game::files {

// Win32 errors the layer reports.
constexpr uint32_t kErrorFileNotFound = 2;
constexpr uint32_t kErrorPathNotFound = 3;
constexpr uint32_t kErrorAccessDenied = 5;
constexpr uint32_t kErrorInvalidHandle = 6;
constexpr uint32_t kErrorNoMoreFiles = 0x12;
constexpr uint32_t kErrorWriteProtect = 0x13;
constexpr uint32_t kErrorGenFailure = 0x1f;
constexpr uint32_t kErrorHandleEof = 0x26;
constexpr uint32_t kErrorFileExists = 0x50;
constexpr uint32_t kErrorInvalidParameter = 0x57;
constexpr uint32_t kErrorDiskFull = 0x70;
constexpr uint32_t kErrorInvalidName = 0x7b;
constexpr uint32_t kErrorDirNotEmpty = 0x91;
constexpr uint32_t kErrorAlreadyExists = 0xb7;
constexpr uint32_t kErrorDirectory = 0x10b;  // a file where a directory was needed

// The host folders: the game data and the user folder (its E and Z subfolders). Before the title starts.
void set_folders(const std::filesystem::path& data, const std::filesystem::path& user);

// What CreateFileA asks for. `disposition` is Win32's: 1 CREATE_NEW, 2 CREATE_ALWAYS, 3 OPEN_EXISTING,
// 4 OPEN_ALWAYS, 5 TRUNCATE_EXISTING.
struct OpenRequest {
    bool write = false;            // GENERIC_WRITE
    uint32_t disposition = 3;
    bool directory = false;        // a directory may be opened (FILE_FLAG_BACKUP_SEMANTICS)
    bool only_directory = false;   // it must be one (CreateDirectoryA, searches)
    bool overlapped = false;       // I/O at given offsets; without it a file position moves
    bool delete_on_close = false;  // FILE_FLAG_DELETE_ON_CLOSE
};
// A handle in *handle; *existed says whether the file was there before (for ERROR_ALREADY_EXISTS).
uint32_t open(const char* path, const OpenRequest& request, uint32_t* handle, bool* existed);
bool is_file(uint32_t handle);

// Reads or writes at `offset` (null: the file position, synchronous handles only), all done before it returns.
// `*done` = bytes transferred; a read at the end of the file transfers 0 and reports kErrorHandleEof.
uint32_t read(uint32_t handle, void* buffer, uint32_t size, const uint64_t* offset, uint32_t* done);
uint32_t write(uint32_t handle, const void* buffer, uint32_t size, const uint64_t* offset, uint32_t* done);

uint32_t size(uint32_t handle, uint64_t* size);
uint32_t position(uint32_t handle, uint64_t* position);
uint32_t set_position(uint32_t handle, uint64_t position);
uint32_t set_end(uint32_t handle);  // the end of the file at its position
uint32_t touch(uint32_t handle);    // last write time = now
// The file's place for ordering requests (XGetFilePhysicalSortKey): a stable hash of its path; 0 for a directory
// on the hard disk folders.
uint32_t sort_key(uint32_t handle, uint32_t* key);
std::shared_ptr<platform::File> host_file(uint32_t handle);

// A directory entry as WIN32_FIND_DATAA gives it.
struct Entry {
    std::string name;
    uint32_t attributes = 0;  // FILE_ATTRIBUTE_*
    uint64_t write_time = 0;  // FILETIME
    uint64_t size = 0;
};
// FindFirstFileA's search: "dir\mask" ("*.*" = all), entries sorted by name ignoring case. Errors: no directory
// part or mask kErrorInvalidParameter, nothing found kErrorFileNotFound.
uint32_t find_first(const char* pattern, uint32_t* handle, Entry* entry);
uint32_t find_next(uint32_t handle, Entry* entry);  // kErrorNoMoreFiles at the end

// By path.
uint32_t stat(const char* path, Entry* entry);
uint32_t list(const char* directory, std::vector<Entry>* entries);  // sorted as a search
uint32_t make_directory(const char* path);
uint32_t remove_file(const char* path);       // files only (a directory: kErrorAccessDenied)
uint32_t remove_directory(const char* path);  // empty directories only
uint32_t remove_tree(const char* path);       // a directory and everything below it

// The volume holding `path` (which must be a directory), in clusters as FILE_FS_SIZE_INFORMATION has them.
struct Volume {
    uint64_t total_clusters = 0;
    uint64_t free_clusters = 0;
    uint32_t sectors_per_cluster = 0;
    uint32_t bytes_per_sector = 0;
};
uint32_t volume(const char* path, Volume* volume);

}  // namespace game::files
