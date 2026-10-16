/* Windows address-space layout shared by the stub executable (win32_stub.c, C) and
   the loader proper (os_win32.cpp). */
#pragma once

/* The stub executable is linked at the XBE base address and its image reaches up to
   here, so Windows itself keeps the range free for the title's image. */
#define COD_BRO_IMAGE_BASE 0x00010000u
#define COD_BRO_IMAGE_AREA_END 0x04000000u

/* Ranges the stub reserves before any DLL loads, for the kernel's memory
   (xbox::MemoryConfig): NtAllocateVirtualMemory's range, and contiguous memory at
   0x80000000 for up to --memory 1024 (needs the large-address-aware flag). The
   loader frees them right before the kernel reserves what it uses. */
#define COD_BRO_HELD_RANGE_COUNT 2
#define COD_BRO_HELD_RANGES      \
    {{0x10000000u, 0x40000000u}, \
     {0x80000000u, 0x40000000u}}

/* The loader proper and its entry point (void, does not return). */
#define COD_BRO_LOADER_CORE_DLL L"cod_bro_loader_core.dll"
#define COD_BRO_LOADER_CORE_ENTRY "cod_bro_loader_main"
