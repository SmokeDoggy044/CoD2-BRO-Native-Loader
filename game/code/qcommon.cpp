#include "qcommon.h"

#include "title_data.h"

namespace game {

void* XB_CDECL Mem_AlignedAlloc(uint32_t size, uint32_t alignment, uint32_t flags) {
    using Alloc = void* XB_CDECL(uint32_t, uint32_t, uint32_t);
    ++title_global<uint32_t>(0x009098c4);
    if (const uint32_t allocator = title_global<uint32_t>(0x009098e8))
        return title_function<Alloc>(allocator)(size, alignment, flags);
    if (flags & 0x30000) {
        const uint32_t protect = (flags & 0x20000) ? 0x404 : 0x4;  // PAGE_READWRITE (| WRITECOMBINE)
        return title_function<void* XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x001e763a)(
            size, 0xffffffff, alignment, protect);  // XPhysicalAlloc
    }
    return title_function<void* XB_CDECL(uint32_t, uint32_t)>(0x001f114f)(size, alignment);  // _aligned_malloc
}

}  // namespace game
