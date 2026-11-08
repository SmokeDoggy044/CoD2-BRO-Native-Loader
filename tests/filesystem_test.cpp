// Files, directories, volumes and symbolic links (src/runtime/xbox/filesystem.cpp), called through
// the export table the way XAPI calls them, against a fake disc and data folder in a
// temporary directory.

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include "filesystem.h"
#include "objects.h"
#include "test_support.h"
#include "types.h"

using namespace xbox;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kGenericRead = 0x80000000, kGenericWrite = 0x40000000, kSynchronize = 0x100000;
constexpr uint32_t kFileOpen = 1, kFileCreate = 2, kFileOpenIf = 3, kFileOverwriteIf = 5;
constexpr uint32_t kDirectoryFile = 0x1, kSynchronousIo = 0x20, kNonDirectoryFile = 0x40;
constexpr uint32_t kFileOpened = 1, kFileCreated = 2, kFileOverwritten = 3;
constexpr uint32_t kEndOfFile = 0xC0000011, kNameNotFound = 0xC0000034, kPathNotFound = 0xC000003A;
constexpr uint32_t kNameCollision = 0xC0000035, kNameInvalid = 0xC0000033, kWriteProtected = 0xC00000A2;
constexpr uint32_t kNoMoreFiles = 0x80000006, kNoSuchFile = 0xC000000F, kNotADirectory = 0xC0000103;
constexpr Handle kDosDevices = 0xFFFFFFFD;

using NtCreateFileFn = uint32_t(XB_STDCALL*)(Handle*, uint32_t, XbObjectAttributes*, XbIoStatusBlock*, int64_t*,
                                             uint32_t, uint32_t, uint32_t, uint32_t);
using NtReadFileFn = uint32_t(XB_STDCALL*)(Handle, Handle, uint32_t, uint32_t, XbIoStatusBlock*, void*, uint32_t,
                                           int64_t*);
using NtWriteFileFn = uint32_t(XB_STDCALL*)(Handle, Handle, uint32_t, uint32_t, XbIoStatusBlock*, const void*,
                                            uint32_t, int64_t*);
using NtInformationFn = uint32_t(XB_STDCALL*)(Handle, XbIoStatusBlock*, void*, uint32_t, uint32_t);
using NtQueryDirectoryFn = uint32_t(XB_STDCALL*)(Handle, Handle, uint32_t, uint32_t, XbIoStatusBlock*, void*,
                                                 uint32_t, uint32_t, XbAnsiString*, uint32_t);
using NtFullAttributesFn = uint32_t(XB_STDCALL*)(XbObjectAttributes*, XbFileNetworkOpenInformation*);
using NtOpenLinkFn = uint32_t(XB_STDCALL*)(Handle*, XbObjectAttributes*);
using NtQueryLinkFn = uint32_t(XB_STDCALL*)(Handle, XbAnsiString*, uint32_t*);
using NtCloseFn = uint32_t(XB_STDCALL*)(Handle);
using NtCreateEventFn = uint32_t(XB_STDCALL*)(Handle*, XbObjectAttributes*, uint32_t, uint32_t);
using NtWaitFn = uint32_t(XB_STDCALL*)(Handle, uint32_t, int64_t*);

using KeDelayFn = uint32_t(XB_STDCALL*)(uint32_t, uint32_t, int64_t*);
using ApcDispatcherFn = void(XB_STDCALL*)(XbPtr, XbIoStatusBlock*, uint32_t);
using ObReferenceFn = uint32_t(XB_STDCALL*)(Handle, const XbObjectType*, XbPtr*);
using ObDereferenceFn = void(XB_FASTCALL*)(XbPtr);
using IoQueryVolumeFn = uint32_t(XB_STDCALL*)(XbFileObject*, uint32_t, uint32_t, void*, uint32_t*);

NtCreateFileFn NtCreateFile;
NtCloseFn NtClose;

// "Title" callbacks: an APC routine and a ReadFileEx completion routine.
uint32_t g_apc_args[3], g_completion_args[3];
int g_apc_runs = 0, g_completion_runs = 0;
void apc_routine(const uint32_t* args, uint32_t) {
    std::copy(args, args + 3, g_apc_args);
    ++g_apc_runs;
}
void completion_routine(const uint32_t* args, uint32_t) {
    std::copy(args, args + 3, g_completion_args);
    ++g_completion_runs;
}

// Keeps an ANSI_STRING and its OBJECT_ATTRIBUTES together.
struct Name {
    explicit Name(std::string text, Handle root = 0) : text(std::move(text)) {
        string = {static_cast<uint16_t>(this->text.size()), static_cast<uint16_t>(this->text.size()),
                  to_xbox_ptr(this->text.data())};
        attributes = {root, to_xbox_ptr(&string), 0x40 /* OBJ_CASE_INSENSITIVE */};
    }
    Name(const Name&) = delete;
    std::string text;
    XbAnsiString string;
    XbObjectAttributes attributes;
};

uint32_t create(Handle& handle, const std::string& path, uint32_t access, uint32_t disposition, uint32_t options,
                uint32_t* information = nullptr, Handle root = 0) {
    Name name(path, root);
    XbIoStatusBlock io{};
    const uint32_t status =
        NtCreateFile(&handle, access | kSynchronize, &name.attributes, &io, nullptr, 0, 3, disposition, options);
    if (information) *information = io.information;
    return status;
}

void write_host_file(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << contents;
}

}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("cod_bro_fs_test_" + std::to_string(std::time(nullptr)));
    const fs::path disc = root / "disc";
    const fs::path data = root / "data";
    write_host_file(disc / "Media" / "Foo.ff", "0123456789");
    write_host_file(disc / "Media" / "bar.ff", "bar");
    write_host_file(disc / "Media" / "readme.txt", "text");
    write_host_file(disc / "default.xbe", "XBEH");
    test::init_kernel(disc, data);

    NtCreateFile = test::find_export<NtCreateFileFn>("NtCreateFile");
    NtClose = test::find_export<NtCloseFn>("NtClose");
    const auto NtReadFile = test::find_export<NtReadFileFn>("NtReadFile");
    const auto NtWriteFile = test::find_export<NtWriteFileFn>("NtWriteFile");
    const auto NtQueryInformationFile = test::find_export<NtInformationFn>("NtQueryInformationFile");
    const auto NtSetInformationFile = test::find_export<NtInformationFn>("NtSetInformationFile");
    const auto NtQueryVolumeInformationFile = test::find_export<NtInformationFn>("NtQueryVolumeInformationFile");
    const auto NtQueryDirectoryFile = test::find_export<NtQueryDirectoryFn>("NtQueryDirectoryFile");
    const auto NtQueryFullAttributesFile = test::find_export<NtFullAttributesFn>("NtQueryFullAttributesFile");
    const auto NtOpenSymbolicLinkObject = test::find_export<NtOpenLinkFn>("NtOpenSymbolicLinkObject");
    const auto NtQuerySymbolicLinkObject = test::find_export<NtQueryLinkFn>("NtQuerySymbolicLinkObject");
    const auto NtCreateEvent = test::find_export<NtCreateEventFn>("NtCreateEvent");
    const auto NtWaitForSingleObject = test::find_export<NtWaitFn>("NtWaitForSingleObject");

    const auto KeDelayExecutionThread = test::find_export<KeDelayFn>("KeDelayExecutionThread");
    const auto NtUserIoApcDispatcher = test::find_export<ApcDispatcherFn>("NtUserIoApcDispatcher");
    const auto ObReferenceObjectByHandle = test::find_export<ObReferenceFn>("ObReferenceObjectByHandle");
    const auto ObfDereferenceObject = test::find_export<ObDereferenceFn>("ObfDereferenceObject");
    const auto IoQueryVolumeInformation = test::find_export<IoQueryVolumeFn>("IoQueryVolumeInformation");
    const auto file_type = test::find_export<const XbObjectType*>("IoFileObjectType");
    const auto event_type = test::find_export<const XbObjectType*>("ExEventObjectType");
    const XbPtr apc_id = test::register_guest(2, apc_routine);
    const XbPtr completion_id = test::register_guest(3, completion_routine);

    CHECK(create_symbolic_link("\\??\\D:", "\\Device\\CdRom0") == 0);
    CHECK(create_symbolic_link("\\??\\d:", "\\Device\\CdRom0") == 0xC0000035, "links are case-insensitive");
    CHECK(create_symbolic_link("\\??\\E:", "\\Device\\Harddisk0\\Partition1") == 0);

    // Reading from the disc: case-insensitive names, sequential and positioned reads.
    Handle file = 0;
    uint32_t information = 0;
    CHECK(create(file, "\\??\\D:\\MEDIA\\foo.FF", kGenericRead, kFileOpen, kSynchronousIo | kNonDirectoryFile,
                 &information) == 0);
    CHECK(information == kFileOpened);
    char buffer[16] = {};
    XbIoStatusBlock io{};
    CHECK(NtReadFile(file, 0, 0, 0, &io, buffer, 4, nullptr) == 0 && io.information == 4);
    CHECK(std::string(buffer, 4) == "0123");
    CHECK(NtReadFile(file, 0, 0, 0, &io, buffer, 16, nullptr) == 0 && io.information == 6, "rest: %u", io.information);
    CHECK(NtReadFile(file, 0, 0, 0, &io, buffer, 4, nullptr) == kEndOfFile && io.information == 0);
    int64_t offset = 8;
    CHECK(NtReadFile(file, 0, 0, 0, &io, buffer, 4, &offset) == 0 && io.information == 2 && buffer[0] == '8');
    int64_t position = -1;
    CHECK(NtQueryInformationFile(file, &io, &position, 8, 14) == 0 && position == 10, "position %lld",
          static_cast<long long>(position));
    XbFileStandardInformation standard{};
    CHECK(NtQueryInformationFile(file, &io, &standard, sizeof(standard), 5) == 0);
    CHECK(standard.end_of_file == 10 && !standard.directory && standard.allocation_size == 2048);

    // An event passed to NtReadFile is signaled when the read completes.
    Handle event = 0;
    NtCreateEvent(&event, nullptr, 0, 0);
    int64_t zero = 0;
    CHECK(NtReadFile(file, event, 0, 0, &io, buffer, 1, &zero) == 0);
    CHECK(NtWaitForSingleObject(event, 0, &zero) == 0, "event signaled");
    CHECK(NtWaitForSingleObject(file, 0, &zero) == 0, "file handle signaled");
    NtClose(file);

    // Relative to the DOS device root (handle -3), as FindFirstFile opens directories.
    CHECK(create(file, "d:\\media", kGenericRead, kFileOpen, kDirectoryFile | kSynchronousIo, nullptr, kDosDevices) ==
          0);

    // Directory listing with a mask; one entry per call, sorted, then NO_MORE_FILES.
    alignas(8) uint8_t entry_buffer[0x148];
    auto* entry = reinterpret_cast<XbFileDirectoryInformation*>(entry_buffer);
    auto entry_name = [&] { return std::string(reinterpret_cast<char*>(entry_buffer + 0x40), entry->file_name_length); };
    std::string mask_text = "*.FF";
    XbAnsiString mask{4, 4, to_xbox_ptr(mask_text.data())};
    CHECK(NtQueryDirectoryFile(file, 0, 0, 0, &io, entry_buffer, sizeof(entry_buffer), 1, &mask, 0) == 0);
    CHECK(entry_name() == "bar.ff" && entry->end_of_file == 3, "first %s", entry_name().c_str());
    CHECK(NtQueryDirectoryFile(file, 0, 0, 0, &io, entry_buffer, sizeof(entry_buffer), 1, nullptr, 0) == 0);
    CHECK(entry_name() == "Foo.ff", "second %s", entry_name().c_str());
    CHECK(NtQueryDirectoryFile(file, 0, 0, 0, &io, entry_buffer, sizeof(entry_buffer), 1, nullptr, 0) == kNoMoreFiles);
    CHECK(NtQueryDirectoryFile(file, 0, 0, 0, &io, entry_buffer, sizeof(entry_buffer), 1, nullptr, 1) == 0,
          "restart scan, no mask");
    CHECK(entry_name() == "bar.ff");
    std::string none_text = "*.dds";
    XbAnsiString none{5, 5, to_xbox_ptr(none_text.data())};
    CHECK(NtQueryDirectoryFile(file, 0, 0, 0, &io, entry_buffer, sizeof(entry_buffer), 1, &none, 1) == kNoSuchFile);

    // Open relative to a directory handle.
    Handle relative = 0;
    CHECK(create(relative, "README.TXT", kGenericRead, kFileOpen, kSynchronousIo, nullptr, file) == 0);
    NtClose(relative);
    NtClose(file);

    // Errors: missing names, missing folders, escaping the device, wrong kind.
    CHECK(create(file, "\\??\\D:\\media\\missing.ff", kGenericRead, kFileOpen, 0) == kNameNotFound);
    CHECK(create(file, "\\??\\D:\\nowhere\\x.ff", kGenericRead, kFileOpen, 0) == kPathNotFound);
    CHECK(create(file, "\\??\\D:\\..\\data", kGenericRead, kFileOpen, 0) == kNameInvalid);
    CHECK(create(file, "\\??\\Q:\\x", kGenericRead, kFileOpen, 0) == kPathNotFound, "unknown drive");
    CHECK(create(file, "\\??\\D:\\default.xbe", kGenericRead, kFileOpen, kDirectoryFile) == kNotADirectory);

    // The disc is read-only.
    CHECK(create(file, "\\??\\D:\\default.xbe", kGenericWrite, kFileOpen, 0) == kWriteProtected);
    CHECK(create(file, "\\??\\D:\\new.txt", kGenericWrite, kFileCreate, 0) == kWriteProtected);

    // The hard disk: create folders and files, overwrite, resize, rename, delete.
    CHECK(create(file, "\\??\\E:\\TDATA", kGenericRead, kFileOpenIf, kDirectoryFile, &information) == 0);
    CHECK(information == kFileCreated && fs::is_directory(data / "E" / "TDATA"));
    NtClose(file);
    CHECK(create(file, "\\??\\E:\\tdata", kGenericRead, kFileOpenIf, kDirectoryFile, &information) == 0);
    CHECK(information == kFileOpened);
    NtClose(file);

    CHECK(create(file, "\\??\\E:\\TDATA\\save.dat", kGenericWrite, kFileCreate, kSynchronousIo, &information) == 0);
    CHECK(information == kFileCreated);
    CHECK(NtWriteFile(file, 0, 0, 0, &io, "hello world", 11, nullptr) == 0 && io.information == 11);
    NtClose(file);
    CHECK(create(file, "\\??\\E:\\TDATA\\SAVE.DAT", kGenericWrite, kFileCreate, 0) == kNameCollision);
    CHECK(fs::file_size(data / "E" / "TDATA" / "save.dat") == 11);

    CHECK(create(file, "\\??\\E:\\TDATA\\save.dat", kGenericRead | kGenericWrite, kFileOverwriteIf, kSynchronousIo,
                 &information) == 0);
    CHECK(information == kFileOverwritten && fs::file_size(data / "E" / "TDATA" / "save.dat") == 0);
    int64_t end_of_file = 100;
    CHECK(NtSetInformationFile(file, &io, &end_of_file, 8, 20) == 0);
    CHECK(fs::file_size(data / "E" / "TDATA" / "save.dat") == 100);

    std::string new_name_text = "\\??\\E:\\TDATA\\renamed.dat";
    XbFileRenameInformation rename{};
    rename.file_name = {static_cast<uint16_t>(new_name_text.size()), static_cast<uint16_t>(new_name_text.size()),
                        to_xbox_ptr(new_name_text.data())};
    CHECK(NtSetInformationFile(file, &io, &rename, sizeof(rename), 10) == 0);
    CHECK(fs::exists(data / "E" / "TDATA" / "renamed.dat") && !fs::exists(data / "E" / "TDATA" / "save.dat"));
    uint8_t delete_file = 1;
    CHECK(NtSetInformationFile(file, &io, &delete_file, 1, 13) == 0);
    CHECK(fs::exists(data / "E" / "TDATA" / "renamed.dat"), "deleted only on close");
    NtClose(file);
    CHECK(!fs::exists(data / "E" / "TDATA" / "renamed.dat"));

    // Volume information: XAPI requires 16 KB clusters on the hard disk.
    CHECK(create(file, "\\Device\\Harddisk0\\partition1\\", kGenericRead, kFileOpen, kDirectoryFile) == 0);
    XbFileFsSizeInformation size{};
    CHECK(NtQueryVolumeInformationFile(file, &io, &size, sizeof(size), 3) == 0);
    CHECK(size.sectors_per_allocation_unit * size.bytes_per_sector == 0x4000);
    CHECK(size.total_allocation_units > 0 && size.available_allocation_units <= size.total_allocation_units);
    NtClose(file);
    CHECK(create(file, "\\??\\D:\\", kGenericRead, kFileOpen, kDirectoryFile) == 0);
    CHECK(NtQueryVolumeInformationFile(file, &io, &size, sizeof(size), 3) == 0 && size.bytes_per_sector == 2048 &&
          size.available_allocation_units == 0);
    NtClose(file);

    // Attributes without opening.
    {
        Name name("\\??\\D:\\media\\FOO.ff");
        XbFileNetworkOpenInformation attributes{};
        CHECK(NtQueryFullAttributesFile(&name.attributes, &attributes) == 0);
        CHECK(attributes.end_of_file == 10 && (attributes.file_attributes & 0x01), "read-only on the disc");
        Name folder("\\??\\D:\\media");
        CHECK(NtQueryFullAttributesFile(&folder.attributes, &attributes) == 0 && (attributes.file_attributes & 0x10));
    }

    // Symbolic link objects.
    {
        Name name("\\??\\D:");
        Handle link = 0;
        CHECK(NtOpenSymbolicLinkObject(&link, &name.attributes) == 0);
        char target_buffer[64];
        XbAnsiString target{0, sizeof(target_buffer), to_xbox_ptr(target_buffer)};
        uint32_t length = 0;
        CHECK(NtQuerySymbolicLinkObject(link, &target, &length) == 0);
        CHECK(std::string(target_buffer, target.length) == "\\Device\\CdRom0" && length == 14);
        NtClose(link);
    }

    // ReadFileEx: the read completes at once (the status block is final) but reports
    // STATUS_PENDING, as the Xbox's disk drivers do; its APC runs at the next alertable
    // wait. Asynchronous handles need an explicit offset.
    CHECK(create(file, "\\??\\D:\\media\\foo.ff", kGenericRead, kFileOpen, kNonDirectoryFile) == 0);
    XbIoStatusBlock overlapped{};
    int64_t two = 2;
    CHECK(NtReadFile(file, 0, 0, 0, &overlapped, buffer, 4, nullptr) == 0xC000000D, "async read without offset");
    CHECK(NtReadFile(file, 0, apc_id, 0x55, &overlapped, buffer, 4, &two) == 0x103 && overlapped.status == 0 &&
          overlapped.information == 4);
    CHECK(std::string(buffer, 4) == "2345" && g_apc_runs == 0, "APC waits for an alertable wait");
    int64_t sleep = -10000 * 10;
    CHECK(KeDelayExecutionThread(1, 1, &sleep) == 0xC0);
    CHECK(g_apc_runs == 1 && g_apc_args[0] == 0x55 && g_apc_args[1] == to_xbox_ptr(&overlapped) && g_apc_args[2] == 0);

    // NtUserIoApcDispatcher turns the APC into completion(error, bytes, OVERLAPPED*).
    NtUserIoApcDispatcher(completion_id, &overlapped, 0);
    CHECK(g_completion_runs == 1 && g_completion_args[0] == 0 && g_completion_args[1] == 4 &&
          g_completion_args[2] == to_xbox_ptr(&overlapped));
    XbIoStatusBlock at_end{kEndOfFile, 0};
    NtUserIoApcDispatcher(completion_id, &at_end, 0);
    CHECK(g_completion_args[0] == 38 /* ERROR_HANDLE_EOF */ && g_completion_args[1] == 0);

    // ObReferenceObjectByHandle gives the FILE_OBJECT; XAPI reads a sort key from its
    // FsContext and the file system name through IoQueryVolumeInformation.
    XbPtr body = 0;
    CHECK(ObReferenceObjectByHandle(file, event_type, &body) == 0xC0000024, "type mismatch");
    CHECK(ObReferenceObjectByHandle(file, file_type, &body) == 0 && body != 0);
    auto* file_object = from_xbox_ptr<XbFileObject>(body);
    const uint32_t disc_key = *from_xbox_ptr<uint32_t>(file_object->fs_context);
    CHECK(file_object->type == 5 && disc_key != 0);
    struct {
        uint32_t attributes, max_component, name_length;
        char name[20];
    } volume{};
    uint32_t returned = 0;
    CHECK(IoQueryVolumeInformation(file_object, 5, sizeof(volume), &volume, &returned) == 0);
    CHECK(std::string(volume.name, volume.name_length) == "GDFX" && returned == 16);
    NtClose(file);
    CHECK(IoQueryVolumeInformation(file_object, 5, sizeof(volume), &volume, &returned) == 0,
          "the reference keeps the object alive after NtClose");
    ObfDereferenceObject(body);
    CHECK(IoQueryVolumeInformation(file_object, 5, sizeof(volume), &volume, &returned) == 0xC000000D, "released");

    // The same file gives the same key; another file a different one.
    CHECK(create(file, "\\??\\D:\\MEDIA\\FOO.FF", kGenericRead, kFileOpen, 0) == 0);
    CHECK(ObReferenceObjectByHandle(file, nullptr, &body) == 0);
    CHECK(*from_xbox_ptr<uint32_t>(from_xbox_ptr<XbFileObject>(body)->fs_context) == disc_key);
    ObfDereferenceObject(body);
    NtClose(file);
    CHECK(create(file, "\\??\\D:\\media\\bar.ff", kGenericRead, kFileOpen, 0) == 0);
    CHECK(ObReferenceObjectByHandle(file, nullptr, &body) == 0);
    CHECK(*from_xbox_ptr<uint32_t>(from_xbox_ptr<XbFileObject>(body)->fs_context) != disc_key);
    ObfDereferenceObject(body);
    NtClose(file);

    // On FATX the key is at +0x1C and bit 0 of the first byte marks directories.
    CHECK(create(file, "\\??\\E:\\TDATA", kGenericRead, kFileOpen, kDirectoryFile) == 0);
    CHECK(ObReferenceObjectByHandle(file, file_type, &body) == 0);
    file_object = from_xbox_ptr<XbFileObject>(body);
    const uint8_t* context = from_xbox_ptr<uint8_t>(file_object->fs_context);
    CHECK((context[0] & 1) == 1 && *reinterpret_cast<const uint32_t*>(context + 0x1C) != 0);
    CHECK(IoQueryVolumeInformation(file_object, 5, sizeof(volume), &volume, &returned) == 0);
    CHECK(std::string(volume.name, volume.name_length) == "FATX");
    ObfDereferenceObject(body);
    NtClose(file);

    // The utility drive: Z: links to the cache folder; formatting empties it.
    write_host_file(data / "Z" / "old" / "cache.bin", "stale");
    CHECK(mount_utility_drive(false) == 0 && fs::exists(data / "Z" / "old" / "cache.bin"));
    CHECK(create(file, "\\??\\Z:\\old\\cache.bin", kGenericRead, kFileOpen, 0) == 0);
    NtClose(file);
    CHECK(mount_utility_drive(true) == 0 && fs::is_empty(data / "Z"));

    std::error_code error;
    fs::remove_all(root, error);
    return test::finish("filesystem_test");
}
