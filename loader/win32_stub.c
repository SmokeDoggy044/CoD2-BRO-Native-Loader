/* cod_bro_loader.exe on Windows: a tiny executable at the XBE's base address, so the
   title's image range belongs to us before anything else is mapped. Its zero-filled
   array makes the image cover the whole range (win32_layout.h). It also reserves the
   kernel's fixed memory ranges, then loads the loader proper from
   cod_bro_loader_core.dll, which maps the XBE over this image and never returns.

   Built without the C runtime (only kernel32/user32 calls), as the process's first
   code after Windows' own. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "win32_layout.h"

__attribute__((used)) static unsigned char g_image_area[COD_BRO_IMAGE_AREA_END - COD_BRO_IMAGE_BASE];

static void fail(const char* message) {
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, lstrlenA(message), &written, NULL);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), "\r\n", 2, &written, NULL);
    MessageBoxA(NULL, message, "cod_bro_loader", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

void stub_entry(void) {
    static const struct {
        DWORD start, size;
    } held[COD_BRO_HELD_RANGE_COUNT] = COD_BRO_HELD_RANGES;
    for (int i = 0; i < COD_BRO_HELD_RANGE_COUNT; ++i)
        VirtualAlloc((void*)(ULONG_PTR)held[i].start, held[i].size, MEM_RESERVE, PAGE_NOACCESS);

    HMODULE core = LoadLibraryW(COD_BRO_LOADER_CORE_DLL);
    if (!core) fail("Cannot load cod_bro_loader_core.dll (or one of the DLLs it needs) next to this program.");
    void (*entry)(void) = (void (*)(void))(void*)GetProcAddress(core, COD_BRO_LOADER_CORE_ENTRY);
    if (!entry) fail("cod_bro_loader_core.dll has no " COD_BRO_LOADER_CORE_ENTRY ".");
    /* Never returns: the XBE replaces this code and its import table. */
    entry();
}
