#include "scene_view.h"

#include <mutex>

namespace render {
namespace {
std::mutex g_mutex;
SceneView g_view;
}  // namespace

void set_scene_view(const SceneView& view) {
    std::lock_guard lock(g_mutex);
    g_view = view;
}

SceneView scene_view() {
    std::lock_guard lock(g_mutex);
    return g_view;
}

}  // namespace render
