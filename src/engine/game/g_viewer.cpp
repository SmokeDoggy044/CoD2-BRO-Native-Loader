#include "g_viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "dobj.h"
#include "g_scr_builtins2.h"
#include "g_utils.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t g_handle = 0;  // the viewer's script_model (entity handle), 0 = none
// Where the camera was when the viewer started (the model stands in front of it), and the shown model's box.
float g_view[2] = {}, g_dir[2] = {1, 0}, g_ground_from = 0, g_feet = 0, g_base_yaw = 0;
float g_mins[3] = {}, g_maxs[3] = {};
float g_distance = 0;
int32_t g_zone_set = 0;
constexpr int32_t kZoneSets = 32;  // tried in turn (unknown count; a lookup in an unused set finds nothing)

uint8_t* viewer_entity() {
    if (!g_handle) return nullptr;
    const uint32_t index = g_handle & 0xfff;
    if (index >= 0x540 || (g_handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
}

uint8_t* player_entity() {
    uint8_t* const level = title_global<uint8_t*>(0x0095f534);
    return level ? *reinterpret_cast<uint8_t**>(level + 4) : nullptr;
}

}  // namespace

bool viewer_show(const char* model, float distance, std::string& why) {
    uint8_t* ent = viewer_entity();
    if (!ent) {
        const uint8_t* const player = player_entity();
        if (!player) {
            why = "no player (start a mission first)";
            return false;
        }
        // The camera (cgame refdef: origin 0x00972bb0, forward axis 0x00972bc0), level; the player's feet height.
        const float* const view = &title_global<float>(0x00972bb0);
        const float* const forward = &title_global<float>(0x00972bc0);
        const float flat = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
        g_dir[0] = flat > 0.001f ? forward[0] / flat : 1.0f;
        g_dir[1] = flat > 0.001f ? forward[1] / flat : 0.0f;
        g_view[0] = view[0];
        g_view[1] = view[1];
        g_ground_from = reinterpret_cast<const float*>(player + 0x140)[2];
        g_base_yaw = std::atan2(g_dir[1], g_dir[0]) * 180.0f / 3.14159265f + 180.0f;  // facing the camera
        ScriptString classname{};
        ScriptString_FromCString(&classname, "script_model");
        const float start[3] = {g_view[0] + g_dir[0] * distance, g_view[1] + g_dir[1] * distance, g_ground_from};
        uint32_t place[3];
        std::memcpy(place, start, sizeof(place));
        g_handle = GScr_Spawn(&classname, place, nullptr);
        ScriptString_Release(&classname);
        ent = viewer_entity();
        if (!ent) {
            g_handle = 0;
            why = "the game could not spawn a script_model";
            return false;
        }
    }
    // The level's zone set first (as a level script's setmodel), then each other: zone props and vehicles are
    // registered in the zone set their pack loaded into.
    const uint32_t before = *reinterpret_cast<const uint32_t*>(ent + 0x258);
    const int32_t level_set = *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0095f638) + 0x34);
    int32_t found = -2;
    for (int32_t set = -1; set < kZoneSets && found == -2; ++set) {
        const int32_t context = set < 0 ? level_set : set;
        if (set >= 0 && set == level_set) continue;
        *reinterpret_cast<uint32_t*>(ent + 0x258) = 0;
        G_SetModel(context, ent, model);
        if (*reinterpret_cast<const uint32_t*>(ent + 0x258)) found = context;
    }
    if (found == -2) {
        *reinterpret_cast<uint32_t*>(ent + 0x258) = before;
        why = std::string("no model \"") + model + "\" in the loaded zones";
        return false;
    }
    G_DObjUpdate(ent);
    g_zone_set = found;
    // Far enough to see all of it (its box's half diagonal, a 65 degree view), at least `distance`.
    const auto* obj = *reinterpret_cast<const DObj* const*>(ent + 0x22c);
    g_mins[0] = g_mins[1] = g_mins[2] = -16;
    g_maxs[0] = g_maxs[1] = g_maxs[2] = 16;
    if (obj) DObj_GetBounds(0, g_mins, g_maxs, obj);
    float radius = 0;
    for (int i = 0; i < 3; ++i) radius += (g_maxs[i] - g_mins[i]) * (g_maxs[i] - g_mins[i]);
    radius = std::sqrt(radius) * 0.5f;
    g_distance = std::max(distance * 0.4f, radius / std::tan(32.5f * 3.14159265f / 180.0f) + radius);
    // Stood on the ground under its middle (a point trace down from above the player's feet; else at their height).
    {
        const float x = g_view[0] + g_dir[0] * g_distance, y = g_view[1] + g_dir[1] * g_distance;
        alignas(16) float start[4] = {x, y, g_ground_from + 64, 0}, end[4] = {x, y, g_ground_from - 2048, 0};
        alignas(16) float zero[4] = {};
        alignas(16) uint8_t trace[0x80] = {};
        auto address = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
        G_Trace(address(trace), address(start), address(zero), address(zero), address(end), 0, 1);  // solid
        const float fraction = *reinterpret_cast<const float*>(trace + 0x20);
        g_feet = fraction < 1.0f ? reinterpret_cast<const float*>(trace)[2] : g_ground_from;
    }
    viewer_turn(0, 0);
    char where[192];
    std::snprintf(where, sizeof(where), "zone set %d, box %.0f x %.0f x %.0f (bottom %.0f), %.0f units away, ground %.0f "
                  "(player %.0f)", g_zone_set, g_maxs[0] - g_mins[0], g_maxs[1] - g_mins[1], g_maxs[2] - g_mins[2],
                  g_mins[2], g_distance, g_feet, g_ground_from);
    why = where;
    return true;
}

void viewer_turn(float yaw, float pitch) {
    uint8_t* const ent = viewer_entity();
    if (!ent) return;
    // Turned about the middle of its box, which stays on the camera's line; its bottom at the player's feet.
    const float angles[3] = {pitch, g_base_yaw + yaw, 0};
    const float a = angles[1] * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    const float mid_x = (g_mins[0] + g_maxs[0]) * 0.5f, mid_y = (g_mins[1] + g_maxs[1]) * 0.5f;
    const float origin[3] = {g_view[0] + g_dir[0] * g_distance - (c * mid_x - s * mid_y),
                             g_view[1] + g_dir[1] * g_distance - (s * mid_x + c * mid_y), g_feet - g_mins[2]};
    G_SetAngle(ent, 0, angles);
    G_SetOrigin(ent, 0, origin);
    G_DObjUpdate(ent);
    SV_LinkEntity(ent);
}

void viewer_hide() {
    if (uint8_t* const ent = viewer_entity()) G_FreeEntity(ent);
    g_handle = 0;
}

}  // namespace game
