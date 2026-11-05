#include "bro_scr_world.h"

#include "bro_scr_common.h"
#include "qcommon_fs.h"
#include "title_data.h"

#include <cstring>

namespace game {

uint32_t* XB_CDECL BroScr_GetLevelVecOrZero(uint32_t* out, ScriptString name) {
    if (name.rep == nullptr) {
        out[0] = 0;  // 0.0f
        out[1] = 0;
        out[2] = 0;
    } else {
        uint32_t hash;
        const uint32_t* key = title_function<uint32_t* XB_CDECL(uint32_t*, const ScriptString*)>(0x0011d8b0)(&hash, &name);  // Scr_HashScriptString
        HashMap* map = reinterpret_cast<HashMap*>(title_global<uint32_t>(0x00922ee0) + 0x3b4);
        const uint32_t* v = reinterpret_cast<const uint32_t*>(HashMap_Index(map, key));
        const uint32_t w0 = v[0], w1 = v[1], w2 = v[2];
        const float u = title_global<float>(0x00922eb8);  // g_scrUndefinedVecLane
        float f[3];
        std::memcpy(f, v, sizeof(f));
        if (!(f[0] == u) || !(f[1] == u) || !(f[2] == u)) {
            out[0] = w0;
            out[1] = w1;
            out[2] = w2;
        } else {
            out[0] = 0;
            out[1] = 0;
            out[2] = 0;
        }
    }
    scr_release(&name);
    return out;
}

}  // namespace game
