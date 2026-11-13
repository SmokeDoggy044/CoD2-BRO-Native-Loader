#include "vk_post.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "log.h"
#include "post_shaders.h"
#include "spirv_layout.h"

namespace render::vk {
namespace {

// The quad as post_vs.hlsl takes it: clip position (TEXCOORD0) and texture coordinate (TEXCOORD1).
struct Vertex {
    float x, y, z, w, u, v;
};

VkShaderModule post_module(VkDevice device, const char* name) {
    const NamedShader* shader = post_shader(name);
    if (!shader) return VK_NULL_HANDLE;
    std::string error;
    const std::vector<uint32_t> words =
        spirv_fixed_layout(shader->d3d9, shader->d3d9_bytes, shader->spirv, shader->spirv_bytes, &error);
    if (words.empty()) {
        LOG_ERROR("render", "Vulkan: post shader %s: %s", name, error.c_str());
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size() * 4;
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

}  // namespace

Post::~Post() {
    if (!vk_.device()) return;
    vk_.destroy_image(scratch_);
    destroy_pipelines();
    VkDevice device = vk_.device();
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
}

void Post::destroy_pipelines() {
    for (VkPipeline* pipeline : {&fxaa_, &sharpen_}) {
        if (*pipeline) vk_.defer_destroy(*pipeline);
        *pipeline = VK_NULL_HANDLE;
    }
}

bool Post::create(VkFormat format) {
    if (failed_) return false;
    if (fxaa_ && format == format_) return true;
    VkDevice device = vk_.device();
    if (!set_layout_) {
        // The fixed layout's bindings the post shaders use: both constant blocks, t0 and s0.
        const uint32_t used[] = {kVkVertexConstantsBinding, kVkPixelConstantsBinding, kVkFirstImageBinding,
                                 kVkFirstSamplerBinding};
        VkDescriptorSetLayoutBinding bindings[4]{};
        for (uint32_t i = 0; i < 4; ++i) {
            bindings[i].binding = used[i];
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                         : i == 2 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                  : VK_DESCRIPTOR_TYPE_SAMPLER;
        }
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        set.bindingCount = 4;
        set.pBindings = bindings;
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &set_layout_;
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateDescriptorSetLayout(device, &set, nullptr, &set_layout_) != VK_SUCCESS ||
            vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout_) != VK_SUCCESS ||
            vkCreateSampler(device, &sampler, nullptr, &sampler_) != VK_SUCCESS) {
            LOG_WARN("render", "enhanced graphics: cannot create the Vulkan post passes; the picture stays as it is");
            failed_ = true;
            return false;
        }
    }
    destroy_pipelines();

    VkShaderModule vertex = post_module(device, "post_vs"), fxaa = post_module(device, "fxaa"),
                   sharpen = post_module(device, "sharpen");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                 vertex, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                 VK_NULL_HANDLE, "main", nullptr};
    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[2] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                                                             {1, 0, VK_FORMAT_R32G32_SFLOAT, 16}};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
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
    if (vertex && fxaa && sharpen) {
        stages[1].module = fxaa;
        r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &fxaa_);
        stages[1].module = sharpen;
        if (r == VK_SUCCESS) r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &sharpen_);
    }
    for (VkShaderModule module : {vertex, fxaa, sharpen})
        if (module) vkDestroyShaderModule(device, module, nullptr);
    if (r != VK_SUCCESS) {
        LOG_WARN("render", "enhanced graphics: cannot create the Vulkan post passes (%s); the picture stays as it is",
                 result_name(r));
        destroy_pipelines();
        failed_ = true;
        return false;
    }
    format_ = format;
    return true;
}

void Post::pass(VkPipeline pipeline, Image& source, Image& target, const float (&params)[4]) {
    // Constants (vertex c0 = no pixel offset: Vulkan samples at pixel centres) and the quad, staged
    // before rendering begins (staging may flush).
    const VkDeviceSize align = std::max<VkDeviceSize>(16, vk_.limits().minUniformBufferOffsetAlignment);
    const VkDeviceSize vs_bytes = kVkVertexConstants * 16, ps_bytes = kVkPixelConstants * 16;
    const VkDeviceSize ps_at = (vs_bytes + align - 1) / align * align;
    const VkDeviceSize quad_at = (ps_at + ps_bytes + 15) / 16 * 16;
    // Vulkan's clip space has y down: the picture's top row at y = -1 (D3D9's quad has it at +1).
    const Vertex quad[4] = {{-1, -1, 0, 1, 0, 0}, {1, -1, 0, 1, 1, 0}, {-1, 1, 0, 1, 0, 1}, {1, 1, 0, 1, 1, 1}};
    const Staged staged = vk_.stage(quad_at + sizeof(quad), align);
    if (!staged.memory) return;
    auto* bytes = static_cast<uint8_t*>(staged.memory);
    std::memset(bytes, 0, quad_at);
    std::memcpy(bytes + ps_at, params, sizeof(params));
    std::memcpy(bytes + quad_at, quad, sizeof(quad));

    vk_.transition(source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_.transition(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cmd = vk_.cmd();
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = target.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // every pixel is written
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    const VkExtent2D extent{target.width, target.height};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDescriptorBufferInfo buffers[2] = {{staged.buffer, staged.offset, vs_bytes},
                                               {staged.buffer, staged.offset + ps_at, ps_bytes}};
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, source.sample_view ? source.sample_view : source.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo sampler{sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[4]{};
    const uint32_t bindings[4] = {kVkVertexConstantsBinding, kVkPixelConstantsBinding, kVkFirstImageBinding,
                                  kVkFirstSamplerBinding};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = bindings[i];
        writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffers[0];
    writes[1].pBufferInfo = &buffers[1];
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[2].pImageInfo = &image;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[3].pImageInfo = &sampler;
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 4, writes);
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    const VkDeviceSize vertex_offset = staged.offset + quad_at;
    vkCmdBindVertexBuffers(cmd, 0, 1, &staged.buffer, &vertex_offset);
    vkCmdDraw(cmd, 4, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

void Post::apply(Image& picture, const GraphicsFeatures& f) {
    const bool fxaa = f.fxaa;
    const bool sharpen = f.sharpen && f.sharpness > 0.0f;
    const bool vibrance = f.vibrance && f.vibrance_amount != 0.0f;
    if (!picture || (!fxaa && !sharpen && !vibrance) || !create(picture.format)) return;
    if (scratch_ && (scratch_.width != picture.width || scratch_.height != picture.height ||
                     scratch_.format != picture.format))
        vk_.destroy_image(scratch_);
    if (!scratch_ && !vk_.create_image(scratch_, picture.format, picture.width, picture.height,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    vk_.end_rendering();

    // As on D3D9: FXAA into the scratch image; sharpening and vibrance from wherever the picture is now into
    // the other one; the result copied back if it ended in the scratch image.
    const float texel_x = 1.0f / picture.width, texel_y = 1.0f / picture.height;
    bool in_scratch = false;
    if (fxaa) {
        pass(fxaa_, picture, scratch_, {texel_x, texel_y, 0, 0});
        in_scratch = true;
    }
    if (sharpen || vibrance) {
        pass(sharpen_, in_scratch ? scratch_ : picture, in_scratch ? picture : scratch_,
             {texel_x, texel_y, sharpen ? f.sharpness : 0.0f, vibrance ? f.vibrance_amount : 0.0f});
        in_scratch = !in_scratch;
    }
    if (in_scratch) {
        vk_.transition(scratch_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vk_.transition(picture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy region{};
        region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {picture.width, picture.height, 1};
        vkCmdCopyImage(vk_.cmd(), scratch_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, picture.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
}

}  // namespace render::vk
