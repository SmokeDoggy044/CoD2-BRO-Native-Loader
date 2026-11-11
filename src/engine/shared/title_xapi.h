#pragma once

// Our own versions of the XAPI functions (XAPILIB, statically linked into default.xbe) that the engine calls, for
// the own engine host (0.3: the host maps no original .text). Each does what the title's does where the game can
// see it, on the same kernel exports: they are called through the title's kernel thunk table (0x007b2fe0..), which
// the loader fills with its kernel imitation (src/runtime/xbox), so the compare harness records them like the
// originals' calls. Registered as Compare rows at the original addresses (src/engine/shared/functions.cpp).

#include <cstdint>

#include "compiler.h"

namespace game {

// The title clock (RDTSC at the Xbox's 733 MHz) as the loader provides it: its RDTSC patches' clock, which
// record/replay records per main-thread read and the compare harness stands in for. Set before the title starts.
using TitleClock = int64_t XB_CDECL();
void set_title_clock(TitleClock* clock);

// --- Errors (the thread's XAPI block: TLS slot *0x009268b0, +8 = last error) ---
uint32_t XB_STDCALL GetLastError();                       // 0x001e7c84
void XB_STDCALL SetLastError(uint32_t error);             // 0x001e7cac
uint32_t XB_STDCALL XapiSetLastNTError(uint32_t status);  // 0x001e7cda: the DOS error, also returned

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

}  // namespace game
