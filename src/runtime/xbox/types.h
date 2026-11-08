#pragma once

// Xbox-side structures that live in game memory. They must keep their 32-bit layout
// even in a 64-bit build, so pointers inside them are stored as uint32_t.

#include <cstddef>
#include <cstdint>

namespace xbox {

using XbPtr = uint32_t;

// Converts a host pointer to the 32-bit form stored in Xbox structures. Only
// meaningful while host and Xbox share an address space (the route-B loader).
template <typename T>
inline XbPtr to_xbox_ptr(T* pointer) {
    return static_cast<XbPtr>(reinterpret_cast<uintptr_t>(pointer));
}

template <typename T>
inline T* from_xbox_ptr(XbPtr pointer) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(pointer));
}

struct XbAnsiString {  // ANSI_STRING
    uint16_t length;
    uint16_t maximum_length;
    XbPtr buffer;
};
static_assert(sizeof(XbAnsiString) == 8);

struct XbUnicodeString {  // UNICODE_STRING; lengths in bytes
    uint16_t length;
    uint16_t maximum_length;
    XbPtr buffer;  // uint16_t*
};
static_assert(sizeof(XbUnicodeString) == 8);

struct XbKrnlVersion {  // XBOX_KRNL_VERSION
    uint16_t major;
    uint16_t minor;
    uint16_t build;
    uint16_t qfe;
};
static_assert(sizeof(XbKrnlVersion) == 8);

struct XbDispatcherHeader {  // DISPATCHER_HEADER
    uint8_t type;
    uint8_t absolute;
    uint8_t size;
    uint8_t inserted;
    int32_t signal_state;
    XbPtr wait_list_flink;
    XbPtr wait_list_blink;
};
static_assert(sizeof(XbDispatcherHeader) == 16);

// DISPATCHER_HEADER.type values.
enum XbDispatcherType : uint8_t {
    kXbEventNotification = 0,
    kXbEventSynchronization = 1,
    kXbMutant = 2,
    kXbSemaphore = 5,
    kXbThread = 6,
    kXbTimerNotification = 8,
    kXbTimerSynchronization = 9,
    kXbDpc = 19,
};

struct XbKEvent {  // KEVENT
    XbDispatcherHeader header;
};
static_assert(sizeof(XbKEvent) == 16);

struct XbKMutant {  // KMUTANT
    XbDispatcherHeader header;  // signal_state 1 = free, <= 0 = owned (recursively)
    XbPtr list_flink;
    XbPtr list_blink;
    XbPtr owner_thread;  // KTHREAD
    uint8_t abandoned;
    uint8_t pad[3];
};
static_assert(sizeof(XbKMutant) == 0x20);

struct XbKDpc {  // KDPC
    int16_t type;  // kXbDpc
    uint8_t inserted;
    uint8_t padding;
    XbPtr list_flink;
    XbPtr list_blink;
    XbPtr deferred_routine;  // VOID NTAPI (*)(KDPC*, context, argument1, argument2)
    XbPtr deferred_context;
    XbPtr system_argument1;
    XbPtr system_argument2;
};
static_assert(sizeof(XbKDpc) == 0x1C);

struct XbKTimer {  // KTIMER
    XbDispatcherHeader header;  // header.inserted = timer is set
    uint64_t due_time;
    XbPtr list_flink;
    XbPtr list_blink;
    XbPtr dpc;
    int32_t period;  // milliseconds, 0 = one-shot
};
static_assert(sizeof(XbKTimer) == 0x28);
static_assert(offsetof(XbKTimer, dpc) == 0x20);

struct XbMemoryBasicInformation {  // MEMORY_BASIC_INFORMATION
    XbPtr base_address;
    XbPtr allocation_base;
    uint32_t allocation_protect;
    uint32_t region_size;
    uint32_t state;
    uint32_t protect;
    uint32_t type;
};
static_assert(sizeof(XbMemoryBasicInformation) == 28);

struct XbMmStatistics {  // MM_STATISTICS
    uint32_t length;     // must be sizeof(XbMmStatistics)
    uint32_t total_physical_pages;
    uint32_t available_pages;
    uint32_t virtual_memory_bytes_committed;
    uint32_t virtual_memory_bytes_reserved;
    uint32_t cache_pages_committed;
    uint32_t pool_pages_committed;
    uint32_t stack_pages_committed;
    uint32_t image_pages_committed;
};
static_assert(sizeof(XbMmStatistics) == 0x24);

// --- Files. Times are FILETIMEs (100ns since 1601). Explicit padding keeps the sizes
// the same with 4-byte (GCC i386) and 8-byte (MSVC) int64 alignment.

struct XbIoStatusBlock {  // IO_STATUS_BLOCK
    uint32_t status;
    uint32_t information;
};
static_assert(sizeof(XbIoStatusBlock) == 8);

struct XbFileBasicInformation {  // class 4
    int64_t creation_time;
    int64_t last_access_time;
    int64_t last_write_time;
    int64_t change_time;
    uint32_t file_attributes;
    uint32_t pad;
};
static_assert(sizeof(XbFileBasicInformation) == 40);

struct XbFileStandardInformation {  // class 5
    int64_t allocation_size;
    int64_t end_of_file;
    uint32_t number_of_links;
    uint8_t delete_pending;
    uint8_t directory;
    uint8_t pad[2];
};
static_assert(sizeof(XbFileStandardInformation) == 24);

struct XbFileNameInformation {  // class 9
    uint32_t file_name_length;
    char file_name[1];
};

struct XbFileRenameInformation {  // class 10
    uint8_t replace_if_exists;
    uint8_t pad[3];
    uint32_t root_directory;  // HANDLE
    XbAnsiString file_name;
};
static_assert(sizeof(XbFileRenameInformation) == 16);

struct XbFileNetworkOpenInformation {  // class 34, NtQueryFullAttributesFile
    int64_t creation_time;
    int64_t last_access_time;
    int64_t last_write_time;
    int64_t change_time;
    int64_t allocation_size;
    int64_t end_of_file;
    uint32_t file_attributes;
    uint32_t pad;
};
static_assert(sizeof(XbFileNetworkOpenInformation) == 56);

struct XbFileDirectoryInformation {  // class 1, NtQueryDirectoryFile; the name follows
    uint32_t next_entry_offset;
    uint32_t file_index;
    int64_t creation_time;
    int64_t last_access_time;
    int64_t last_write_time;
    int64_t change_time;
    int64_t end_of_file;
    int64_t allocation_size;
    uint32_t file_attributes;
    uint32_t file_name_length;
};
static_assert(sizeof(XbFileDirectoryInformation) == 0x40);

struct XbFileFsSizeInformation {  // volume class 3
    int64_t total_allocation_units;
    int64_t available_allocation_units;
    uint32_t sectors_per_allocation_unit;
    uint32_t bytes_per_sector;
};
static_assert(sizeof(XbFileFsSizeInformation) == 24);

struct XbFileFsDeviceInformation {  // volume class 4
    uint32_t device_type;
    uint32_t characteristics;
};

struct XbFileFsAttributeInformation {  // volume class 5; the name follows
    uint32_t file_system_attributes;
    uint32_t maximum_component_name_length;
    uint32_t file_system_name_length;
};

struct XbFileFsFullSizeInformation {  // volume class 7
    int64_t total_allocation_units;
    int64_t caller_available_allocation_units;
    int64_t actual_available_allocation_units;
    uint32_t sectors_per_allocation_unit;
    uint32_t bytes_per_sector;
};
static_assert(sizeof(XbFileFsFullSizeInformation) == 32);

// FILE_OBJECT, laid out with MSVC alignment (CurrentByteOffset at 0x18). XAPI reads
// FsContext (+8) directly - confirmed; the offsets after it are unverified.
struct XbFileObject {
    int16_t type;  // 5 = IO_TYPE_FILE
    uint8_t access_bits;
    uint8_t flags;
    XbPtr device_object;
    XbPtr fs_context;
    XbPtr fs_context2;
    uint32_t final_status;
    uint32_t pad;
    int64_t current_byte_offset;
    XbPtr related_file_object;
    XbPtr completion_context;
    int32_t lock_count;
    XbKEvent lock;
    XbKEvent event;  // signaled when I/O completes; what a wait on a file handle waits for
    uint32_t pad2;
};
static_assert(offsetof(XbFileObject, fs_context) == 8);
static_assert(offsetof(XbFileObject, current_byte_offset) == 0x18);
static_assert(offsetof(XbFileObject, event) == 0x3C);
static_assert(sizeof(XbFileObject) == 0x50);

// OBJECT_TYPE: what IoFileObjectType, ExEventObjectType, ... point to.
struct XbObjectType {
    XbPtr allocate_procedure;
    XbPtr free_procedure;
    XbPtr close_procedure;
    XbPtr delete_procedure;
    XbPtr parse_procedure;
    XbPtr default_object;  // offset of the object's dispatcher header for waits
    uint32_t pool_tag;
};
static_assert(sizeof(XbObjectType) == 28);

struct XbObjectAttributes {  // OBJECT_ATTRIBUTES
    uint32_t root_directory;  // HANDLE
    XbPtr object_name;        // ANSI_STRING*
    uint32_t attributes;
};
static_assert(sizeof(XbObjectAttributes) == 12);

struct XbShutdownRegistration {  // HAL_SHUTDOWN_REGISTRATION
    XbPtr notification_routine;     // VOID NTAPI (*)(HAL_SHUTDOWN_REGISTRATION*)
    int32_t priority;               // higher runs earlier
    XbPtr list_flink;
    XbPtr list_blink;
};
static_assert(sizeof(XbShutdownRegistration) == 16);

// RTL_CRITICAL_SECTION. Titles also define these statically in their data
// (event type 1, LockCount -1), so all state must live in these fields.
struct XbCriticalSection {
    XbDispatcherHeader synchronization;  // a synchronization (auto-reset) event
    int32_t lock_count;                  // -1 = free
    int32_t recursion_count;
    XbPtr owning_thread;                 // KTHREAD of the owner
};
static_assert(sizeof(XbCriticalSection) == 0x1C);
static_assert(offsetof(XbCriticalSection, lock_count) == 0x10);
static_assert(offsetof(XbCriticalSection, owning_thread) == 0x18);

}  // namespace xbox
