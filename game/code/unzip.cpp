#include "unzip.h"

#include "title_data.h"

namespace game {

int32_t XB_CDECL inflate_trees_fixed(uint32_t* bl, uint32_t* bd, void** tl, void** td, void* /*stream*/) {
    *bl = title_global<uint32_t>(0x008a0c78);  // fixed_bl
    *bd = title_global<uint32_t>(0x008a0c7c);  // fixed_bd
    *tl = &title_global<uint8_t>(0x008a0c80);  // fixed_tl
    *td = &title_global<uint8_t>(0x008a1c80);  // fixed_td
    return 0;
}

}  // namespace game
