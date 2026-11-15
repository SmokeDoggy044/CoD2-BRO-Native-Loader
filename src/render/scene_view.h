#pragma once

// What the game tells the renderer about its main 3D view (engine improvements 2.6): enough to turn the world's
// depth into distances for screen-space passes (ambient occlusion). Set by the game's renderer each frame
// (tr_main R_SetupProjection, draw_world), read by the Vulkan back end at the world's end.

namespace render {

struct SceneView {
    bool valid = false;
    // The scene projection's depth for view distance d (nglDepthToZValue): depth buffer value
    // z = (a d + b) / (c d + e), in units of 16777215 (the back ends store z / 16777215).
    float a = 1.0f, b = 0.0f, c = 1.0f, e = 0.0f;
    float tan_across = 1.0f, tan_down = 0.75f;  // tan of the half field of view, across and down the picture
    float fog_start = 0.0f, fog_end = 0.0f;     // the world's fog (0 0: none)
};

// From the level draws (draw_world: the main scene's projection and fog).
void set_scene_view(const SceneView& view);
SceneView scene_view();

}  // namespace render
