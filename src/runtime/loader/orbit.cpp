#include "orbit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "dev_mode.h"
#include "log.h"
#include "ngl.h"

namespace loader {

namespace {

// File format (lines; '#' starts a comment):
//   start N      presented frame at which the game pauses (the poses begin kSettle frames later)
//   hold N       frames per pose (the last one is the pose's picture; default 30)
//   FORWARD LEFT UP YAW PITCH   a pose: offsets in game units in the view's level frame, yaw in degrees added
//                               to the view's, pitch in degrees (positive looks down) or "keep"
struct Pose {
    float offset[3];
    float yaw, pitch;
};
struct Orbit {
    uint32_t start = 0, hold = 30;
    std::vector<Pose> poses;
} g_orbit;
constexpr uint32_t kSettle = 30;  // frames between the pause and the first pose (src/runtime/loader/viewer.cpp)
uint32_t g_frames = 0;
int g_shown = -1;

}  // namespace

bool load_orbit(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "--orbit: cannot read %s\n", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (const size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream words(line);
        std::string first;
        if (!(words >> first)) continue;
        if (first == "start") {
            words >> g_orbit.start;
        } else if (first == "hold") {
            words >> g_orbit.hold;
        } else {
            Pose pose{};
            std::string pitch;
            std::istringstream all(line);
            if (!(all >> pose.offset[0] >> pose.offset[1] >> pose.offset[2] >> pose.yaw >> pitch)) {
                std::fprintf(stderr, "--orbit: bad line \"%s\"\n", line.c_str());
                return false;
            }
            pose.pitch = pitch == "keep" ? NAN : std::strtof(pitch.c_str(), nullptr);
            g_orbit.poses.push_back(pose);
        }
    }
    if (g_orbit.poses.empty() || g_orbit.start < 1 || g_orbit.hold < 2) {
        std::fprintf(stderr, "--orbit: %s needs start >= 1, hold >= 2 and at least one pose\n", path.c_str());
        return false;
    }
    return true;
}

std::vector<std::string> orbit_frame(uint32_t) {
    ++g_frames;
    if (g_frames == g_orbit.start) render::dev::set_paused(true);
    game::RendererPcHooks& hooks = game::renderer_pc_hooks();
    const uint32_t first = g_orbit.start + kSettle;
    if (g_frames >= first) {
        const uint32_t index = (g_frames - first) / g_orbit.hold;
        if (index > 0 && (g_frames - first) % g_orbit.hold == 0 && index <= g_orbit.poses.size())
            LOG_INFO("dev", "orbit casters pose=%u count=%u", index - 1, hooks.casters_last_frame);
        if (index >= g_orbit.poses.size()) {
            if (index == g_orbit.poses.size() && (g_frames - first) % g_orbit.hold == 2) {
                LOG_INFO("dev", "orbit done");
                std::exit(0);  // the lab's run is over (its pictures are written as the frames are shown)
            }
        } else if (static_cast<int>(index) != g_shown) {
            g_shown = static_cast<int>(index);
            const Pose& p = g_orbit.poses[index];
            hooks.camera_override = true;
            std::copy(p.offset, p.offset + 3, hooks.camera_offset);
            hooks.camera_yaw = p.yaw;
            hooks.camera_pitch = p.pitch;
            LOG_INFO("dev", "orbit pose %u: %.2f %.2f %.2f yaw %+.2f pitch %.2f (frame %u)", index, p.offset[0],
                     p.offset[1], p.offset[2], p.yaw, p.pitch, g_frames);
        }
    }
    std::vector<std::string> lines;
    if (g_shown >= 0)
        lines.push_back("ORBIT pose " + std::to_string(g_shown + 1) + "/" + std::to_string(g_orbit.poses.size()));
    return lines;
}

}  // namespace loader
