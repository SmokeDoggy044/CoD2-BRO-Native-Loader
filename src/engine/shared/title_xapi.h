#pragma once

// Our own versions of the XAPI functions (XAPILIB, statically linked into default.xbe) that the engine calls, for
// the own engine host (0.3: the host maps no original .text). Each does what the title's does where the game can
// see it, on the same kernel exports: they are called through the title's kernel thunk table (0x007b2fe0..), which
// the loader fills with its kernel imitation (src/runtime/xbox), so the compare harness records them like the
// originals' calls. Registered as Compare rows at the original addresses (src/engine/shared/functions.cpp).

#include <cstdint>
#include <string>

#include "compiler.h"

namespace game {

// The title clock (RDTSC at the Xbox's 733 MHz) as the loader provides it: its RDTSC patches' clock, which
// record/replay records per main-thread read and the compare harness stands in for. Set before the title starts.
using TitleClock = int64_t XB_CDECL();
void set_title_clock(TitleClock* clock);

// A word of the title thread's FS block at a KPCR offset (FS:[4] TLS array, FS:[0x28] current KTHREAD, ...), read
// where the title's own code reads it: the loader maps the offset (on Windows some KPCR fields live in TEB TLS
// slots, see loader::title_fs_offset).
using TitleFsMap = uint32_t(uint32_t);
void set_title_fs_map(TitleFsMap* map);
uint32_t title_fs_word(uint32_t kpcr_offset);

// --- Errors (the thread's XAPI block: TLS slot *0x009268b0, +8 = last error) ---
uint32_t XB_STDCALL GetLastError();                       // 0x001e7c84
void XB_STDCALL SetLastError(uint32_t error);             // 0x001e7cac
uint32_t XB_STDCALL XapiSetLastNTError(uint32_t status);  // 0x001e7cda: the DOS error, also returned
// RtlNtStatusToDosError: our own table under --own-main, the kernel's otherwise.
uint32_t title_dos_error(uint32_t status);
// The console's save-signing key (XboxHDKey, 16 bytes): our constant under --own-main, the kernel's otherwise.
const uint8_t* title_console_key();

// --- Console settings, launch data ---
uint32_t XB_STDCALL XGetVideoStandard();  // 0x001e75df
uint32_t XB_STDCALL XGetVideoFlags();     // 0x001e7ee1
uint32_t XB_STDCALL XGetAVPack();         // 0x001edfde
uint32_t XB_STDCALL XGetLanguage();       // 0x001e6f2a
uint32_t XB_STDCALL XGetLaunchInfo(uint32_t* type, uint8_t* data);  // 0x001e6e7f

// --- Memory ---
uint32_t XB_STDCALL XPhysicalAlloc(uint32_t size, uint32_t address, uint32_t alignment, uint32_t protect);  // 0x001e763a
void XB_STDCALL XPhysicalFree(uint32_t address);                                     // 0x001e76ba
uint32_t XB_STDCALL XPhysicalAllocEx(uint32_t size, uint32_t lowest, uint32_t highest, uint32_t alignment,
                                     uint32_t protect);  // 0x001e767d
void XB_STDCALL XPhysicalProtect(uint32_t address, uint32_t size, uint32_t protect);  // 0x001e76aa
uint32_t XB_STDCALL XQueryMemoryProtect(uint32_t address);                            // 0x001e76c0
void XB_STDCALL GlobalMemoryStatus(uint32_t* status);                                 // 0x001e7c35

// --- Time ---
uint32_t XB_STDCALL QueryPerformanceCounter(uint32_t* counter);         // 0x001ed6ca
int32_t* XB_STDCALL XapiFormatTimeOut(int32_t* timeout, uint32_t ms);   // 0x001edfb0
uint32_t XB_STDCALL SleepEx(uint32_t ms, uint32_t alertable);           // 0x001e8659

// --- Handles, files, directories ---
uint32_t XB_STDCALL CloseHandle(uint32_t handle);                                      // 0x001e7b0b
void XB_STDCALL XapiDirectoryInformationToFindData(const uint8_t* info, uint8_t* find);  // 0x001e7941
uint32_t XB_STDCALL FindFirstFileA(const char* path, uint8_t* find);                   // 0x001e79ad
uint32_t XB_STDCALL FindNextFileA(uint32_t handle, uint8_t* find);                     // 0x001e7aba
uint32_t XB_STDCALL GetFileSizeEx(uint32_t handle, uint32_t* size);                    // 0x001e835a
uint32_t XB_STDCALL GetFileSize(uint32_t handle, uint32_t* high);                      // 0x001e84e1
uint32_t XB_STDCALL ReadFileEx(uint32_t handle, void* buffer, uint32_t size, uint32_t* overlapped,
                               uint32_t completion);  // 0x001e8398
uint32_t XB_STDCALL WriteFileEx(uint32_t handle, const void* buffer, uint32_t size, uint32_t* overlapped,
                                uint32_t completion);  // 0x001e83e9
uint32_t XB_STDCALL XGetFilePhysicalSortKey(uint32_t handle);  // 0x001e843a
uint32_t XB_STDCALL CreateFileA(const char* path, uint32_t access, uint32_t share, uint32_t security,
                                uint32_t disposition, uint32_t flags, uint32_t template_file);  // 0x001e7d56
uint32_t XB_STDCALL ReadFile(uint32_t handle, void* buffer, uint32_t size, uint32_t* read,
                             uint32_t* overlapped);  // 0x001e7f2a
uint32_t XB_STDCALL WriteFile(uint32_t handle, const void* buffer, uint32_t size, uint32_t* written,
                              uint32_t* overlapped);  // 0x001e8017
uint32_t XB_STDCALL SetFilePointer(uint32_t handle, uint32_t distance, uint32_t* distance_high,
                                   uint32_t method);  // 0x001e8171
uint32_t XB_STDCALL SetFilePointerEx(uint32_t handle, uint32_t distance, uint32_t distance_high, uint32_t* position,
                                     uint32_t method);  // 0x001e826b
uint32_t XB_STDCALL SetEndOfFile(uint32_t handle);  // 0x001e80ed
uint32_t XB_STDCALL GetOverlappedResult(uint32_t handle, uint32_t* overlapped, uint32_t* transferred,
                                        uint32_t wait);  // 0x001e7cf1

// --- Synchronization ---
uint32_t XB_STDCALL WaitForSingleObjectEx(uint32_t handle, uint32_t ms, uint32_t alertable);  // 0x001e8615
uint32_t XB_STDCALL WaitForSingleObject(uint32_t handle, uint32_t ms);                        // 0x001e86ab
uint32_t XB_STDCALL SetEvent(uint32_t handle);                                                // 0x001e857d
uint32_t XB_STDCALL ReleaseMutex(uint32_t handle);                                            // 0x001e85f5
uint32_t XB_STDCALL SetUnhandledExceptionFilter(uint32_t filter);                             // 0x001e7767
uint32_t* XB_STDCALL XapiFormatObjectAttributes(uint32_t* attributes, uint32_t* name, const char* text);  // 0x001edf88
uint32_t XB_STDCALL CreateEventA(uint32_t security, uint32_t manual_reset, uint32_t initial_state,
                                 const char* name);  // 0x001e851c
uint32_t XB_STDCALL CreateMutexA(uint32_t security, uint32_t initial_owner, const char* name);  // 0x001e859d

// --- Threads ---
uint32_t XB_STDCALL CreateThread(uint32_t attributes, uint32_t stack_size, uint32_t start, uint32_t parameter,
                                 uint32_t flags, uint32_t* thread_id);  // 0x001e7896
void XB_STDCALL XapiThreadStartup(uint32_t start, uint32_t parameter);  // 0x001e77fe
void XB_STDCALL XapiCallThreadNotifyRoutines(uint32_t create);          // 0x001e76c6

// Threads, debugger, time
uint32_t XB_STDCALL GetCurrentThreadId();                                        // 0x001e7904
void XB_STDCALL OutputDebugStringA(const char* text);                            // 0x001e7608
void XB_STDCALL GetSystemTimeAsFileTime(uint32_t* time);                         // 0x001e7911
uint32_t XB_STDCALL FileTimeToSystemTime(const uint32_t* file_time, uint16_t* system_time);  // 0x001e7062
uint32_t XB_STDCALL SystemTimeToFileTime(const uint16_t* system_time, uint32_t* file_time);  // 0x001e70cd
uint32_t XB_STDCALL XQueryValue(uint32_t index, uint32_t* type, void* value, uint32_t size,
                                uint32_t* result_size);                          // 0x001ed6ef
uint32_t XB_STDCALL XapipQueryTimeZoneInformation(uint8_t* info, uint32_t* daylight_on);  // 0x001e6fb0
uint8_t XB_STDCALL RtlpCutoverTimeToSystemTime(const uint16_t* cutover, uint32_t* out, const uint32_t* now,
                                               uint8_t this_year);               // 0x001e7179
uint32_t XB_STDCALL GetTimeZoneInformation(uint8_t* info);                       // 0x001e7383
void XB_STDCALL XapipGetTimeZoneBias(uint32_t* bias);                            // 0x001e74df
uint32_t XB_STDCALL FileTimeToLocalFileTime(const uint32_t* file_time, uint32_t* local_time);  // 0x001e75b4

// Files, disks, strings
uint32_t XB_STDCALL DeleteFileA(const char* path);                               // 0x001e9e84
uint32_t XB_STDCALL XGetDiskSectorSizeA(const char* path);                       // 0x001e9d67
uint32_t XB_STDCALL XGetDiskClusterSizeA(const char* path);                      // 0x001e9cc8
uint32_t XB_STDCALL GetDiskFreeSpaceExA(const char* path, uint32_t* free_to_caller, uint32_t* total,
                                        uint32_t* free);                         // 0x001e9827
uint32_t XB_STDCALL CancelIo(uint32_t handle);                                   // 0x001e86f2
int32_t XB_STDCALL UTF8ToUnicode(const char* source, int32_t length, uint16_t* out, int32_t out_size);  // 0x001e9496
int32_t XB_STDCALL MultiByteToWideChar(uint32_t code_page, uint32_t flags, const char* source, int32_t length,
                                       uint16_t* out, int32_t out_size);         // 0x001e96a4

// --check-crt: the functions above against the title's (title_xapi_check.cpp); the number of differences.
int32_t check_title_xapi(std::string& report);

}  // namespace game
