#include "vk_ao.h"

#include <algorithm>
#include <cmath>

#include "log.h"
#include "vk_shaders.h"

namespace render::vk {
namespace {

VkShaderModule module_for(VkDevice device, const char* name) {
    const SpirvShader* code = spirv_shader(name);
    if (!code) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code->bytes;
    info.pCode = code->code;
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

}  // namespace

Ao::~Ao() {
    if (!vk_.device()) return;
    VkDevice device = vk_.device();
    for (Image& image : half_) vk_.destroy_image(image);
    for (VkPipeline* pipeline : {&ao_, &blur_, &multiply_, &show_})
        if (*pipeline) vkDestroyPipeline(device, *pipeline, nullptr);
    for (VkSampler sampler : {nearest_, linear_})
        if (sampler) vkDestroySampler(device, sampler, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
}

bool Ao::create(VkFormat scene_format) {
    if (failed_) return false;
    if (ao_ && scene_format == scene_format_) return true;
    VkDevice device = vk_.device();
    if (!set_layout_) {
        VkDescriptorSetLayoutBinding bindings[3]{};
        for (uint32_t i = 0; i < 3; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        set.bindingCount = 3;
        set.pBindings = bindings;
        const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &set_layout_;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        VkSamplerCreateInfo linear = sampler;
        linear.magFilter = linear.minFilter = VK_FILTER_LINEAR;
        if (vkCreateDescriptorSetLayout(device, &set, nullptr, &set_layout_) != VK_SUCCESS ||
            vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout_) != VK_SUCCESS ||
            vkCreateSampler(device, &sampler, nullptr, &nearest_) != VK_SUCCESS ||
            vkCreateSampler(device, &linear, nullptr, &linear_) != VK_SUCCESS) {
            LOG_WARN("render", "ambient occlusion: cannot create its Vulkan objects; left out");
            failed_ = true;
            return false;
        }
    }
    for (VkPipeline* pipeline : {&ao_, &blur_, &multiply_, &show_}) {
        if (*pipeline) vk_.defer_destroy(*pipeline);
        *pipeline = VK_NULL_HANDLE;
    }
    VkShaderModule vertex = module_for(device, "ao.vert"), ao = module_for(device, "ao.frag"),
                   blur = module_for(device, "ao_blur.frag"), apply = module_for(device, "ao_apply.frag");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                 vertex, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                 VK_NULL_HANDLE, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkFormat format = VK_FORMAT_R8_UNORM;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = pipeline_layout_;
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    if (vertex && ao && blur && apply) {
        stages[1].module = ao;
        r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &ao_);
        stages[1].module = blur;
        if (r == VK_SUCCESS) r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &blur_);
        // Onto the scene: multiplied (dst x src), or (debug) written alone.
        format = scene_format;
        stages[1].module = apply;
        if (r == VK_SUCCESS) r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &show_);
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        if (r == VK_SUCCESS) r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &multiply_);
    }
    for (VkShaderModule module : {vertex, ao, blur, apply})
        if (module) vkDestroyShaderModule(device, module, nullptr);
    if (r != VK_SUCCESS) {
        LOG_WARN("render", "ambient occlusion: cannot create its Vulkan pipelines (%s); left out", result_name(r));
        failed_ = true;
        return false;
    }
    scene_format_ = scene_format;
    return true;
}

void Ao::pass(VkPipeline pipeline, Image& target, Image& depth, Image* ao, Image* overlay, const float (&params)[16],
              bool load) {
    vk_.transition(depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (overlay) vk_.transition(*overlay, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (ao) vk_.transition(*ao, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_.transition(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cmd = vk_.cmd();
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = target.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    const VkExtent2D extent{target.width, target.height};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDescriptorImageInfo images[3] = {
        {nearest_, depth.sample_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {linear_, ao ? ao->view : depth.sample_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {nearest_, overlay ? overlay->sample_view : depth.sample_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 3, writes);
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), params);
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

void Ao::apply(Image& scene, Image& depth, Image* overlay, const SceneView& view, const Settings& settings) {
    if (!scene || !depth || !depth.sample_view || !view.valid || !create(scene.format)) return;
    const uint32_t w = std::max(1u, scene.width / 2), h = std::max(1u, scene.height / 2);
    for (Image& image : half_) {
        if (image && (image.width != w || image.height != h)) vk_.destroy_image(image);
        if (!image && !vk_.create_image(image, VK_FORMAT_R8_UNORM, w, h,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
            return;
    }
    vk_.end_rendering();
    const float fade_end = view.fog_end > view.fog_start && view.fog_end > 0.0f ? view.fog_end : 4000.0f;
    const float fade_start = view.fog_end > view.fog_start && view.fog_end > 0.0f ? view.fog_start : 2000.0f;
    float p[16] = {view.a, view.b, view.c, view.e,
                   view.tan_across, view.tan_down, settings.radius, settings.strength,
                   1.0f / w, 1.0f / h, std::min(fade_start, fade_end * 0.5f), fade_end,
                   0.0f, 0.0f, 0.0f, 0.0f};  // static noise: no temporal filter in the engine, a moving one shimmers
    pass(ao_, half_[0], depth, nullptr, nullptr, p, false);
    p[13] = 1.0f;  // across
    pass(blur_, half_[1], depth, &half_[0], nullptr, p, false);
    p[13] = 0.0f;  // down
    p[14] = 1.0f;
    pass(blur_, half_[0], depth, &half_[1], nullptr, p, false);
    p[12] = overlay ? 1.0f : 0.0f;  // the apply pass leaves out where a later scene drew
    p[8] = 1.0f / scene.width;
    p[9] = 1.0f / scene.height;
    if (settings.debug) p[7] = 1.0f;
    pass(settings.debug ? show_ : multiply_, scene, depth, &half_[0], overlay, p, !settings.debug);
}

}  // namespace render::vk
