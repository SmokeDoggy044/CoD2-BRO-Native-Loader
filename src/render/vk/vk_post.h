#pragma once

// Enhanced graphics' post passes on Vulkan (0.5 V4): FXAA, then sharpening and vibrance, on the finished
// picture before the present pass (which applies brightness through the gamma table). The same HLSL as
// the D3D9 back end (src/render/post/*.hlsl, d3d9_post.h), as SPIR-V with the fixed layout (spirv_layout.h).

#include <cstdint>

#include "enhanced_graphics.h"
#include "vk_context.h"

namespace render::vk {

class Post {
public:
    explicit Post(Context& context) : vk_(context) {}
    ~Post();
    Post(const Post&) = delete;
    Post& operator=(const Post&) = delete;

    // Applies the features that are on to `picture` (a colour target with sampled + transfer usage) in
    // place, recorded into this frame. Ends any rendering; the context's target stays as it was.
    void apply(Image& picture, const GraphicsFeatures& features);

private:
    bool create(VkFormat format);  // layout, sampler and both pipelines for `format`; false if they cannot be made
    void destroy_pipelines();
    void pass(VkPipeline pipeline, Image& source, Image& target, const float (&params)[4]);

    Context& vk_;
    bool failed_ = false;
    VkFormat format_ = VK_FORMAT_UNDEFINED;  // the pipelines' target format
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipeline fxaa_ = VK_NULL_HANDLE, sharpen_ = VK_NULL_HANDLE;
    Image scratch_;  // the picture's size and format
};

}  // namespace render::vk
