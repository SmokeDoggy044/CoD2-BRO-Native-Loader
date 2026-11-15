#pragma once

// Ambient occlusion on Vulkan (engine improvements 2.6, docs/developers/own-engine-host.md): from the world's depth
// after its draws, at half resolution (vk/shaders/ao.frag: horizon-based, 4 directions x 6 steps), blurred with
// depth weights, multiplied into the HDR scene before its resolve (the HUD comes after and stays as it is).

#include <cstdint>

#include "scene_view.h"
#include "vk_context.h"

namespace render::vk {

class Ao {
public:
    explicit Ao(Context& context) : vk_(context) {}
    ~Ao();
    Ao(const Ao&) = delete;
    Ao& operator=(const Ao&) = delete;

    struct Settings {
        float radius = 48.0f;    // world units
        float strength = 0.6f;   // 0..1: how much of the occlusion darkens
        bool debug = false;      // the AO alone instead of the scene (debug view "ao")
    };
    // Darkens `scene` (an R16G16B16A16 colour target) by the occlusion `depth` (the world's depth target, with a
    // sampled depth view in sample_view) shows. Ends any rendering.
    void apply(Image& scene, Image& depth, const SceneView& view, const Settings& settings);

private:
    bool create(VkFormat scene_format);
    void pass(VkPipeline pipeline, Image& target, Image& depth, Image* ao, const float (&params)[16], bool load);

    Context& vk_;
    bool failed_ = false;
    VkFormat scene_format_ = VK_FORMAT_UNDEFINED;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkSampler nearest_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE;
    VkPipeline ao_ = VK_NULL_HANDLE, blur_ = VK_NULL_HANDLE, multiply_ = VK_NULL_HANDLE, show_ = VK_NULL_HANDLE;
    Image half_[2];  // the AO and its blur, half the scene's size, R8
};

}  // namespace render::vk
