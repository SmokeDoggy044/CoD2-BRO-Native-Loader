// The implementations behind the kernel's memory exports (kernel_memory.cpp, xbox_host). The stdcall exports
// (kernel_memory_exports.cpp) live in the kernel library; our own C reaches the same allocator through the title_*
// functions in kernel_memory.h.
#pragma once

#include <cstdint>

#include "types.h"

namespace xbox::memory_impl {

uint32_t NtAllocateVirtualMemory(XbPtr* base_address, uint32_t zero_bits, uint32_t* region_size,
                                 uint32_t allocation_type, uint32_t protect);
uint32_t NtFreeVirtualMemory(XbPtr* base_address, uint32_t* region_size, uint32_t free_type);
uint32_t NtQueryVirtualMemory(XbPtr base_address, XbMemoryBasicInformation* info);
XbPtr MmCreateKernelStack(uint32_t number_of_bytes, uint32_t debugger_thread);
void MmDeleteKernelStack(XbPtr stack_base, XbPtr stack_limit);
XbPtr MmAllocateContiguousMemoryEx(uint32_t number_of_bytes, uint32_t lowest_acceptable_address,
                                   uint32_t highest_acceptable_address, uint32_t alignment, uint32_t protect);
XbPtr MmAllocateContiguousMemory(uint32_t number_of_bytes);
void MmFreeContiguousMemory(XbPtr base_address);
void MmPersistContiguousMemory(XbPtr base_address, uint32_t number_of_bytes, uint32_t persist);
uint32_t MmQueryAllocationSize(XbPtr base_address);
XbPtr MmGetPhysicalAddress(XbPtr base_address);
uint32_t MmQueryAddressProtect(XbPtr address);
void MmSetAddressProtect(XbPtr base_address, uint32_t number_of_bytes, uint32_t protect);
uint32_t MmQueryStatistics(XbMmStatistics* statistics);

}  // namespace xbox::memory_impl
