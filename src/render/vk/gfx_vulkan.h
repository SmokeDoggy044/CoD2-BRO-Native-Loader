#pragma once

// GfxDevice on Vulkan (own engine 0.5; docs/developers/own-engine-host.md, "0.5 plan"): render targets,
// textures, buffers, the title's shaders (SPIR-V, spirv_layout.h) and draws, with one pipeline layout of push
// descriptors and pipelines keyed by the state Vulkan cannot set dynamically.

#include <cstdint>
#include <functional>
#include <memory>

#include "backend.h"
#include "gfx_device.h"
#include "vk_context.h"

namespace render::vk {

class VulkanGfxDevice : public GfxDevice {
public:
    virtual ~VulkanGfxDevice() = default;
    // The back buffer's image (what present shows).
    virtual Image* back_buffer_image() = 0;
    // A colour target's image by the title memory it stands for (Backend's Xbox-path calls); null if none.
    virtual Image* target_image(uint32_t xbox_data) = 0;
    // Backend::set_render_target / clear on the Xbox path, by title memory.
    virtual void set_target_by_data(uint32_t color_data, uint32_t depth_data) = 0;
    // No draw writing depth this frame (no 3D scene: menus, loading screens, movies), and how many frames
    // in a row before this one were so.
    virtual bool frame_is_flat() const = 0;
    virtual uint32_t flat_frames_before() const = 0;
    // Draws this frame that were not drawn.
    virtual uint32_t unsupported_draws() const = 0;
    // Called once a frame was presented.
    virtual void end_frame() = 0;

    // --- The frame's split into world and HUD (render graph, 0.5b) ---
    // With the renderer's scene marks (GfxDevice::begin_scene), `world_end` is called once a frame where its
    // world (3D view, gun) ends and its HUD begins, with the back buffer the target again. `scene_format` set
    // (not VK_FORMAT_UNDEFINED): the world is drawn into an image of that format standing in for the back
    // buffer (the HDR scene), handed to `world_end` to be resolved into the back buffer; otherwise null.
    virtual void set_world_end(std::function<void(Image* scene)> world_end) = 0;
    virtual void set_scene_format(VkFormat scene_format) = 0;
    // Ends the frame's world now if it began and has not ended (no HUD scene came after it); true when the
    // world ended this frame (here or at a HUD scene).
    virtual bool end_world() = 0;
    // The pipeline cache to its file when it has new pipelines.
    virtual void save_caches() = 0;
    // An in-process title restart: the back buffer and depth buffer stand for this memory now.
    virtual void set_frame_buffers(const SurfaceDesc& back, const SurfaceDesc& depth) = 0;
    // Everything made from title memory goes (targets other than the frame buffers included).
    virtual void forget_title_resources() = 0;
};

// `config`: the back buffer, the device's depth buffer and the render scale (resolved, > 0).
std::unique_ptr<VulkanGfxDevice> create_vulkan_gfx_device(Context& context, const BackendConfig& config);

}  // namespace render::vk
