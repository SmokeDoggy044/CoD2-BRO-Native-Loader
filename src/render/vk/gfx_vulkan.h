#pragma once

// GfxDevice on Vulkan (own engine 0.5; docs/developers/own-engine-host.md, "0.5 plan"): render targets,
// textures, buffers, the title's shaders (SPIR-V, spirv_layout.h) and draws, with one pipeline layout of push
// descriptors and pipelines keyed by the state Vulkan cannot set dynamically.

#include <cstdint>
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
    // An in-process title restart: the back buffer and depth buffer stand for this memory now.
    virtual void set_frame_buffers(const SurfaceDesc& back, const SurfaceDesc& depth) = 0;
    // Everything made from title memory goes (targets other than the frame buffers included).
    virtual void forget_title_resources() = 0;
};

// `config`: the back buffer, the device's depth buffer and the render scale (resolved, > 0).
std::unique_ptr<VulkanGfxDevice> create_vulkan_gfx_device(Context& context, const BackendConfig& config);

}  // namespace render::vk
