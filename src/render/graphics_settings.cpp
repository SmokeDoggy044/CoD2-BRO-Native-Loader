#include <mutex>

#include "enhanced_graphics.h"

namespace render {
namespace {

std::mutex g_mutex;
bool g_enhanced = true;
GraphicsFeatures g_features;

}  // namespace

void set_graphics(bool enhanced, const GraphicsFeatures& features) {
    std::lock_guard lock(g_mutex);
    g_enhanced = enhanced;
    g_features = features;
}

bool graphics_enhanced() {
    std::lock_guard lock(g_mutex);
    return g_enhanced;
}

GraphicsFeatures graphics_features() {
    std::lock_guard lock(g_mutex);
    return g_features;
}

GraphicsFeatures active_graphics_features() {
    std::lock_guard lock(g_mutex);
    if (g_enhanced) return g_features;
    GraphicsFeatures off;
    for (const auto& s : GraphicsFeatures::kSwitches) off.*s.field = false;
    return off;
}

}  // namespace render
