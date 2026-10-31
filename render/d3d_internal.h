#pragma once

// Shared between the D3D front end's files (device.cpp, resource.cpp, ...).

#include <cstddef>
#include <iterator>

#include "kernel.h"
#include "types.h"

namespace render {

using xbox::XbPtr;

#define D3D_IMPL(name) {#name, reinterpret_cast<void*>(&name)}

// Each file's replacements; d3d_functions() returns them all.
extern const xbox::KernelImpl kDeviceFunctions[];
extern const size_t kDeviceFunctionCount;
extern const xbox::KernelImpl kResourceFunctions[];
extern const size_t kResourceFunctionCount;

extern const xbox::KernelImpl kStateFunctions[];
extern const size_t kStateFunctionCount;
extern const xbox::KernelImpl kShaderFunctions[];
extern const size_t kShaderFunctionCount;
extern const xbox::KernelImpl kTextureFunctions[];
extern const size_t kTextureFunctionCount;
extern const xbox::KernelImpl kDrawFunctions[];
extern const size_t kDrawFunctionCount;

// D3DRESOURCETYPE of a resource (D3DResource_GetType).
uint32_t d3d_resource_type(XbPtr resource);

// A texture/surface level surface we created (GetSurfaceLevel2 & co): deletes it and
// returns true; false for anything else.
bool delete_level_surface(XbPtr surface);

// The CPU may have written a texture's or palette's memory (it was locked).
void texture_written(XbPtr resource);

// The title's D3D__RenderState array (see xbox_d3d.h for the indexes).
uint32_t* render_state();
void set_render_state_address(XbPtr address);
// The title's texture stage states, [stage * kTssPerStage + state].
uint32_t* texture_state();
void set_texture_state_address(XbPtr address);

// Textures and palettes bound per stage (D3D objects in title memory, or 0).
struct TextureStage {
    XbPtr texture;
    XbPtr palette;
};
TextureStage& texture_stage(uint32_t stage);

// D3DVIEWPORT8
struct XbViewport {
    uint32_t x, y, width, height;
    float min_z, max_z;
};
static_assert(sizeof(XbViewport) == 24);

struct XbRect {
    int32_t left, top, right, bottom;
};

// Viewport, scissors and the screen space offset, for the draw-time translation.
struct ViewState {
    XbViewport viewport{0, 0, 640, 480, 0.0f, 1.0f};
    uint32_t scissor_count = 0;
    bool scissor_exclusive = false;
    XbRect scissors[8]{};
    float offset_x = 0.0f, offset_y = 0.0f;  // SetScreenSpaceOffset
};
ViewState& view_state();

// The device's current render target and depth buffer (title D3DSurface headers; 0 = none).
XbPtr current_render_target();
XbPtr current_depth_stencil();

class Backend;
Backend* backend();
// The frame's end (D3DDevice_Swap, GfxDevice::present): the picture shown, the window's events
// handled; the frames shown so far.
uint32_t show_frame();

// Before each draw: puts CPU writes to the back buffer on screen first. False without
// a device.
bool begin_draw();

// The current depth buffer's largest value (24-bit or 16-bit).
float depth_buffer_max();

// A resource's last reference is gone: drop anything the back end made from it.
void resource_destroyed(XbPtr resource);

}  // namespace render
