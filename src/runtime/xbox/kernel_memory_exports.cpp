// The kernel's memory exports (Mm*, Nt*VirtualMemory): stdcall entries for the title over memory_impl
// (kernel_memory.cpp). Kernel library only: cod_bro, our own engine, has no kernel exports.
#include "kernel_internal.h"
#include "kernel_memory_impl.h"
#include "types.h"

namespace xbox {

namespace {

uint32_t XB_STDCALL NtAllocateVirtualMemory(XbPtr* base_address, uint32_t zero_bits, uint32_t* region_size,
                                         uint32_t allocation_type, uint32_t protect) {
    return memory_impl::NtAllocateVirtualMemory(base_address, zero_bits, region_size, allocation_type, protect);
}
uint32_t XB_STDCALL NtFreeVirtualMemory(XbPtr* base_address, uint32_t* region_size, uint32_t free_type) {
    return memory_impl::NtFreeVirtualMemory(base_address, region_size, free_type);
}
uint32_t XB_STDCALL NtQueryVirtualMemory(XbPtr base_address, XbMemoryBasicInformation* info) {
    return memory_impl::NtQueryVirtualMemory(base_address, info);
}
XbPtr XB_STDCALL MmCreateKernelStack(uint32_t number_of_bytes, uint32_t debugger_thread) {
    return memory_impl::MmCreateKernelStack(number_of_bytes, debugger_thread);
}
void XB_STDCALL MmDeleteKernelStack(XbPtr stack_base, XbPtr stack_limit) {
    memory_impl::MmDeleteKernelStack(stack_base, stack_limit);
}
XbPtr XB_STDCALL MmAllocateContiguousMemoryEx(uint32_t number_of_bytes, uint32_t lowest_acceptable_address,
                                               uint32_t highest_acceptable_address, uint32_t alignment, uint32_t protect) {
    return memory_impl::MmAllocateContiguousMemoryEx(number_of_bytes, lowest_acceptable_address,
                                                     highest_acceptable_address, alignment, protect);
}
XbPtr XB_STDCALL MmAllocateContiguousMemory(uint32_t number_of_bytes) {
    return memory_impl::MmAllocateContiguousMemory(number_of_bytes);
}
void XB_STDCALL MmFreeContiguousMemory(XbPtr base_address) { memory_impl::MmFreeContiguousMemory(base_address); }
void XB_STDCALL MmPersistContiguousMemory(XbPtr base_address, uint32_t number_of_bytes, uint32_t persist) {
    memory_impl::MmPersistContiguousMemory(base_address, number_of_bytes, persist);
}
uint32_t XB_STDCALL MmQueryAllocationSize(XbPtr base_address) {
    return memory_impl::MmQueryAllocationSize(base_address);
}
XbPtr XB_STDCALL MmGetPhysicalAddress(XbPtr base_address) { return memory_impl::MmGetPhysicalAddress(base_address); }
uint32_t XB_STDCALL MmQueryAddressProtect(XbPtr address) { return memory_impl::MmQueryAddressProtect(address); }
void XB_STDCALL MmSetAddressProtect(XbPtr base_address, uint32_t number_of_bytes, uint32_t protect) {
    memory_impl::MmSetAddressProtect(base_address, number_of_bytes, protect);
}
uint32_t XB_STDCALL MmQueryStatistics(XbMmStatistics* statistics) { return memory_impl::MmQueryStatistics(statistics); }

const KernelImpl kMemoryImpls[] = {
    KIMPL(MmAllocateContiguousMemory),
    KIMPL(MmAllocateContiguousMemoryEx),
    KIMPL(MmCreateKernelStack),
    KIMPL(MmDeleteKernelStack),
    KIMPL(MmFreeContiguousMemory),
    KIMPL(MmGetPhysicalAddress),
    KIMPL(MmPersistContiguousMemory),
    KIMPL(MmQueryAddressProtect),
    KIMPL(MmQueryAllocationSize),
    KIMPL(MmQueryStatistics),
    KIMPL(MmSetAddressProtect),
    KIMPL(NtAllocateVirtualMemory),
    KIMPL(NtFreeVirtualMemory),
    KIMPL(NtQueryVirtualMemory),
};

}  // namespace

std::span<const KernelImpl> memory_kernel_impls() { return kMemoryImpls; }

}  // namespace xbox
