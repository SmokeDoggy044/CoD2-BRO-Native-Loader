// Vulkan back end (own engine 0.5): the shared SDL3 window (game_window.h), a Vulkan 1.3 device
// (vk_context.h), a swap chain sized to the window, and the present pass: the title's back buffer placed
// in the window through the gamma ramp, the diagnostics overlay and notice, and the PC menu (Dear ImGui).
// Our GfxDevice (gfx_vulkan.h) owns the render targets. Draws on the Xbox D3D path are not supported
// (the own engine's renderer C draws through GfxDevice only).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "backend.h"
#include "game_window.h"
#include "gfx_vulkan.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "log.h"
#include "vk_context.h"
#include "render_graph.h"
#include "vk_post.h"
#include "vk_shaders.h"

namespace render {

namespace {

using vk::Image;

struct PushConstants {
    float dst[4];  // clip space x0, y0, x1, y1
    float src[4];  // texture coordinates u0, v0, u1, v1
    int32_t mode;  // blit.frag: 0 opaque, 1 gamma, 2 keeps alpha
};

class VulkanBackend final : public Backend {
public:
    ~VulkanBackend() override {
        if (!vk_.device()) return;
        vk_.wait_idle();
        window_.close_menu();  // its renderer needs the device
        post_.reset();
        graph_.reset();
        gfx_.reset();
        destroy_swapchain();
        for (Image* image : {&gamma_table_, &overlay_image_, &notice_image_, &upload_}) vk_.destroy_image(*image);
        VkDevice device = vk_.device();
        for (VkPipeline pipeline : {opaque_pipeline_, blend_pipeline_, resolve_pipeline_})
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout_) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
        if (set_layout_) vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
        for (VkSampler sampler : {linear_, nearest_})
            if (sampler) vkDestroySampler(device, sampler, nullptr);
    }

    bool init(const BackendConfig& config) {
        if (!window_.open(config, SDL_WINDOW_VULKAN)) return false;
        const char* validation_env = std::getenv("COD_BRO_VK_VALIDATION");
        const bool validation = config.vulkan_validation || (validation_env && *validation_env == '1');
        if (!vk_.create(window_.sdl(), validation)) return false;
        width_ = config.width;
        height_ = config.height;
        aspect_ = config.display_aspect > 0 ? config.display_aspect : static_cast<float>(width_) / height_;
        pillarbox_ = config.pillarbox_flat_frames && config.display_aspect > 0;
        wait_for_display_ = config.wait_for_display;
        vsync_ = config.vsync;
        int pixel_width = 0, pixel_height = 0;
        window_.pixel_size(pixel_width, pixel_height);
        BackendConfig draw_config = config;
        if (draw_config.render_scale <= 0)  // the window's (or the screen's) height
            draw_config.render_scale = static_cast<float>(pixel_height) / height_;
        if (!create_swapchain() || !create_present_pass()) return false;
        gfx_ = vk::create_vulkan_gfx_device(vk_, draw_config);
        post_ = std::make_unique<vk::Post>(vk_);
        graph_ = std::make_unique<vk::RenderGraph>(vk_);
        if (!gfx_->back_buffer_image() || !*gfx_->back_buffer_image()) return false;
        gfx_->set_world_end([this](Image* scene) { scene_passes(scene); });
        gfx_->set_scene_format(scene_format());
        window_.set_menu_renderer([this] { return init_menu_renderer(); }, [] { ImGui_ImplVulkan_Shutdown(); });
        LOG_INFO("render", "Vulkan device: %ux%u, rendering at %.2fx%.2f, window %dx%d%s, swap chain %ux%u %s",
                 config.width, config.height, static_cast<double>(draw_config.render_scale_x()),
                 static_cast<double>(draw_config.render_scale), pixel_width, pixel_height,
                 config.fullscreen ? " (fullscreen)" : "", extent_.width, extent_.height, present_mode_name());
        return true;
    }

    // --- Backend ---

    void set_render_target(const SurfaceDesc& color, const SurfaceDesc* depth) override {
        std::lock_guard lock(mutex_);
        gfx_->set_target_by_data(color.data, depth ? depth->data : 0);
    }
    void forget_render_target(uint32_t /*data*/) override {}  // V3: textures made from that memory
    void forget_title_resources() override {
        std::lock_guard lock(mutex_);
        gfx_->forget_title_resources();
    }
    void set_frame_buffers(const SurfaceDesc& back, const SurfaceDesc& depth) override {
        std::lock_guard lock(mutex_);
        gfx_->set_frame_buffers(back, depth);
    }
    void clear(uint32_t flags, uint32_t color, float depth, uint32_t stencil) override {
        std::lock_guard lock(mutex_);
        gfx_->clear(flags, color, depth, stencil);
    }

    // Uploaded at the title's size, then stretched onto the back buffer at the render scale.
    void write_back_buffer(const void* pixels, uint32_t pitch) override {
        std::lock_guard lock(mutex_);
        if (!upload_ && !vk_.create_image(upload_, VK_FORMAT_B8G8R8A8_UNORM, width_, height_,
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            return;
        vk_.upload(upload_, pixels, pitch, 4);
        blit(upload_, *gfx_->back_buffer_image(), VK_FILTER_LINEAR);
    }

    // At the render scale, pillarboxed frames at their own shape.
    bool save_back_buffer(const char* path) override {
        std::lock_guard lock(mutex_);
        Image* back = gfx_->back_buffer_image();
        Image narrow;
        Image* source = back;
        if (pillarboxed_) {
            const uint32_t width = static_cast<uint32_t>(std::lround(static_cast<double>(back->height) * width_ / height_));
            if (vk_.create_image(narrow, VK_FORMAT_B8G8R8A8_UNORM, width, back->height,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
                blit(*back, narrow, VK_FILTER_LINEAR);
                source = &narrow;
            }
        }
        std::vector<uint32_t> pixels;
        const bool ok = vk_.read(*source, pixels) &&
                        write_bmp(path, reinterpret_cast<const uint8_t*>(pixels.data()),
                                  static_cast<int32_t>(source->width * 4), source->width, source->height);
        vk_.destroy_image(narrow);
        if (!ok) LOG_WARN("render", "cannot save the back buffer to %s", path);
        return ok;
    }

    void save_caches() override {
        std::lock_guard lock(mutex_);
        if (gfx_) gfx_->save_caches();
    }

    void finish_picture() override {
        // The scene passes ran where the world ended (the renderer's scene marks), or run now on frames with a
        // 3D scene (without marks: the HUD gets the post passes too); menus stay untouched.
        std::lock_guard lock(mutex_);
        if (!gfx_->end_world() && !gfx_->frame_is_flat()) scene_passes(nullptr);
    }

    void present() override {
        std::lock_guard lock(mutex_);
        show(overlay_lines_);
        gfx_->end_frame();
        gfx_->set_scene_format(scene_format());
        xbox_draws_ = 0;
    }

    double refresh_rate() override { return window_.refresh_rate(); }

    void draw(const DrawInput& /*input*/) override {
        if (!xbox_draw_logged_) {
            LOG_WARN("render", "Vulkan: a draw on the Xbox D3D path (not supported; the renderer C draws through "
                               "GfxDevice)");
            xbox_draw_logged_ = true;
        }
        ++xbox_draws_;
    }

    bool pump_events() override { return window_.pump_events(); }
    void set_overlay(std::vector<std::string> lines) override { overlay_lines_ = std::move(lines); }
    void set_notice(std::vector<std::string> lines) override { notice_lines_ = std::move(lines); }
    std::optional<std::string> prompt(const std::vector<std::string>& heading) override {
        return window_.prompt(heading, [this](const std::vector<std::string>& lines) {
            std::lock_guard lock(mutex_);
            show(lines);
        });
    }
    bool take_mark_request() override { return window_.take_mark_request(); }
    bool take_highlight_toggle() override { return window_.take_highlight_toggle(); }
    void set_highlight(bool /*on*/) override {}  // V3
    uint32_t unsupported_draws() override { return gfx_->unsupported_draws() + xbox_draws_; }
    void dump_next_frame() override { LOG_INFO("render", "Vulkan: draw dumps come with V3"); }
    void set_gamma_ramp(const uint8_t (&ramp)[3][256]) override { window_.set_gamma_ramp(ramp); }
    bool window_to_screen(float x, float y, float& screen_x, float& screen_y) override {
        return window_.window_to_screen(x, y, screen_x, screen_y);
    }
    GfxDevice* gfx_device() override { return gfx_.get(); }

private:
    // --- Swap chain ---

    bool create_swapchain() {
        VkPhysicalDevice physical = vk_.physical();
        VkSurfaceKHR surface = vk_.surface();
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        int pixel_width = 0, pixel_height = 0;
        window_.pixel_size(pixel_width, pixel_height);
        VkExtent2D extent = caps.currentExtent;
        if (extent.width == 0xFFFFFFFFu) {
            extent.width = std::clamp(static_cast<uint32_t>(pixel_width), caps.minImageExtent.width,
                                      caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(pixel_height), caps.minImageExtent.height,
                                       caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0) return false;  // minimized

        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
        if (formats.empty()) return false;
        VkSurfaceFormatKHR format = formats[0];
        for (VkFormat wanted : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
            auto it = std::find_if(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) {
                return f.format == wanted && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
            if (it != formats.end()) {
                format = *it;
                break;
            }
        }

        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());
        auto has = [&](VkPresentModeKHR mode) { return std::find(modes.begin(), modes.end(), mode) != modes.end(); };
        // As the D3D9 back end: the display paces (FIFO); else no tearing without waiting (mailbox); else
        // neither (immediate).
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!wait_for_display_) {
            if (vsync_ && has(VK_PRESENT_MODE_MAILBOX_KHR))
                mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else if (!vsync_ && has(VK_PRESENT_MODE_IMMEDIATE_KHR))
                mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            else if (has(VK_PRESENT_MODE_MAILBOX_KHR))
                mode = VK_PRESENT_MODE_MAILBOX_KHR;
        }
        uint32_t images = std::max(caps.minImageCount, 3u);
        if (caps.maxImageCount) images = std::min(images, caps.maxImageCount);

        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface;
        info.minImageCount = images;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                  ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                  : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha &
                                                                             -caps.supportedCompositeAlpha);
        info.presentMode = mode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = swapchain_;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        if (VkResult r = vkCreateSwapchainKHR(vk_.device(), &info, nullptr, &swapchain); r != VK_SUCCESS) {
            LOG_ERROR("render", "Vulkan: no swap chain (%s)", vk::result_name(r));
            return false;
        }
        destroy_swapchain();
        swapchain_ = swapchain;
        extent_ = extent;
        present_mode_ = mode;
        if (swap_format_ != VK_FORMAT_UNDEFINED && swap_format_ != format.format)
            LOG_WARN("render", "Vulkan: the swap chain's format changed");  // pipelines keep the first
        swap_format_ = format.format;

        vkGetSwapchainImagesKHR(vk_.device(), swapchain_, &count, nullptr);
        std::vector<VkImage> handles(count);
        vkGetSwapchainImagesKHR(vk_.device(), swapchain_, &count, handles.data());
        for (VkImage handle : handles) {
            Image image;
            image.image = handle;
            image.format = format.format;
            image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            image.width = extent.width;
            image.height = extent.height;
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = handle;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(vk_.device(), &view, nullptr, &image.view);
            swap_images_.push_back(image);
            VkSemaphore semaphore = VK_NULL_HANDLE;
            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vkCreateSemaphore(vk_.device(), &semaphore_info, nullptr, &semaphore);
            rendered_.push_back(semaphore);
        }
        recreate_ = false;
        return true;
    }

    void destroy_swapchain() {
        if (!swapchain_) return;
        vk_.wait_idle();
        for (Image& image : swap_images_) vkDestroyImageView(vk_.device(), image.view, nullptr);
        for (VkSemaphore semaphore : rendered_) vkDestroySemaphore(vk_.device(), semaphore, nullptr);
        swap_images_.clear();
        rendered_.clear();
        vkDestroySwapchainKHR(vk_.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    const char* present_mode_name() const {
        switch (present_mode_) {
            case VK_PRESENT_MODE_FIFO_KHR: return "(waits for the display)";
            case VK_PRESENT_MODE_MAILBOX_KHR: return "(mailbox)";
            case VK_PRESENT_MODE_IMMEDIATE_KHR: return "(immediate)";
            default: return "";
        }
    }

    // --- Present pass ---

    VkShaderModule shader_module(const char* name) {
        const vk::SpirvShader* shader = vk::spirv_shader(name);
        if (!shader) return VK_NULL_HANDLE;
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = shader->bytes;
        info.pCode = shader->code;
        VkShaderModule module = VK_NULL_HANDLE;
        vkCreateShaderModule(vk_.device(), &info, nullptr, &module);
        return module;
    }

    bool create_present_pass() {
        VkDevice device = vk_.device();
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 0.0f;
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        vkCreateSampler(device, &sampler, nullptr, &linear_);
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        vkCreateSampler(device, &sampler, nullptr, &nearest_);

        VkDescriptorSetLayoutBinding bindings[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.bindingCount = 2;
        set.pBindings = bindings;
        vkCreateDescriptorSetLayout(device, &set, nullptr, &set_layout_);
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &set_layout_;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout_);

        VkShaderModule vertex = shader_module("blit.vert"), fragment = shader_module("blit.frag");
        if (!vertex || !fragment) {
            LOG_ERROR("render", "Vulkan: the present shaders are missing");
            return false;
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                     vertex, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                     fragment, "main", nullptr};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
        rendering.pColorAttachmentFormats = &swap_format_;
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
        VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &opaque_pipeline_);
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        if (r == VK_SUCCESS)
            r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &blend_pipeline_);
        // The HDR scene's resolve into the 8-bit picture (scene_passes).
        VkShaderModule resolve = shader_module("scene_resolve.frag");
        if (r == VK_SUCCESS && resolve) {
            blend.blendEnable = VK_FALSE;
            stages[1].module = resolve;
            const VkFormat picture_format = VK_FORMAT_B8G8R8A8_UNORM;
            rendering.pColorAttachmentFormats = &picture_format;
            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &resolve_pipeline_) != VK_SUCCESS)
                resolve_pipeline_ = VK_NULL_HANDLE;
        }
        if (resolve) vkDestroyShaderModule(device, resolve, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);
        if (r != VK_SUCCESS) {
            LOG_ERROR("render", "Vulkan: cannot create the present pass (%s)", vk::result_name(r));
            return false;
        }

        // The gamma table (identity until the title sets a ramp) and the overlay texts.
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!vk_.create_image(gamma_table_, VK_FORMAT_B8G8R8A8_UNORM, 256, 1, usage) ||
            !vk_.create_image(overlay_image_, VK_FORMAT_B8G8R8A8_UNORM, TextImage::kWidth, TextImage::kHeight, usage) ||
            !vk_.create_image(notice_image_, VK_FORMAT_B8G8R8A8_UNORM, TextImage::kWidth, TextImage::kHeight, usage))
            return false;
        uint32_t identity[256];
        for (uint32_t i = 0; i < 256; ++i) identity[i] = 0xFF000000u | i << 16 | i << 8 | i;
        vk_.upload(gamma_table_, identity, sizeof(identity), 4);
        std::vector<uint32_t> blank(TextImage::kWidth * TextImage::kHeight, 0);
        vk_.upload(overlay_image_, blank.data(), TextImage::kWidth * 4, 4);
        vk_.upload(notice_image_, blank.data(), TextImage::kWidth * 4, 4);
        return true;
    }

    bool init_menu_renderer() {
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_3;
        info.Instance = vk_.instance();
        info.PhysicalDevice = vk_.physical();
        info.Device = vk_.device();
        info.QueueFamily = vk_.queue_family();
        info.Queue = vk_.queue();
        info.MinImageCount = 2;
        info.ImageCount = static_cast<uint32_t>(std::max<size_t>(swap_images_.size(), 2));
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.DescriptorPoolSize = 8;
        info.UseDynamicRendering = true;
        info.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
        info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swap_format_;
        return ImGui_ImplVulkan_Init(&info);
    }

    // Source to destination, whole images (transfer layouts; recorded in this frame).
    void blit(Image& source, Image& dest, VkFilter filter) {
        vk_.transition(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vk_.transition(dest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {static_cast<int32_t>(source.width), static_cast<int32_t>(source.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[1] = {static_cast<int32_t>(dest.width), static_cast<int32_t>(dest.height), 1};
        vkCmdBlitImage(vk_.cmd(), source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);
    }

    // --- Render graph (0.5b) ---

    // The world's format for the next frame: 16-bit float with enhanced graphics' HDR (and its resolve pass).
    VkFormat scene_format() const {
        return resolve_pipeline_ && active_graphics_features().hdr ? VK_FORMAT_R16G16B16A16_SFLOAT
                                                                   : VK_FORMAT_UNDEFINED;
    }

    // Where the world ends: the HDR scene (when it was drawn into one) resolved into the picture, then enhanced
    // graphics' post passes on the picture, before the HUD is drawn over it.
    void scene_passes(Image* scene) {
        std::lock_guard lock(mutex_);
        using Access = vk::RenderGraph::Access;
        Image* back = gfx_->back_buffer_image();
        const auto picture = graph_->import("picture", *back);
        if (scene) {
            const auto hdr = graph_->import("scene", *scene);
            graph_->add("resolve", {{hdr, Access::Sampled}, {picture, Access::Color}},
                        [this, scene, back] { resolve(*scene, *back); });
        }
        const GraphicsFeatures features = active_graphics_features();
        graph_->add("post", {{picture, Access::Color}}, [this, back, features] { post_->apply(*back, features); });
        graph_->run();
    }

    // The whole HDR scene into the picture through scene_resolve.frag.
    void resolve(Image& scene, Image& picture) {
        VkDescriptorSet set = vk_.descriptor_set(set_layout_);
        if (!set) return;
        const VkDescriptorImageInfo images[2]{
            {nearest_, scene.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {nearest_, gamma_table_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(vk_.device(), 2, writes, 0, nullptr);
        VkCommandBuffer cmd = vk_.cmd();
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = picture.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // every pixel is written
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        const VkExtent2D extent{picture.width, picture.height};
        rendering.renderArea = {{0, 0}, extent};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &rendering);
        const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, resolve_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &set, 0, nullptr);
        const PushConstants push{{-1, -1, 1, 1}, {0, 0, 1, 1}, 0};
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDraw(cmd, 4, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    // One textured rectangle at window pixels (x, y, width, height) of the swap chain image.
    void draw_rect(Image& texture, VkSampler sampler, float x, float y, float width, float height, float u1, float v1,
                   int32_t mode) {
        VkDescriptorSet set = vk_.descriptor_set(set_layout_);
        if (!set) return;
        VkDescriptorImageInfo images[2]{{sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {nearest_, gamma_table_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(vk_.device(), 2, writes, 0, nullptr);
        VkCommandBuffer cmd = vk_.cmd();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mode == 2 ? blend_pipeline_ : opaque_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &set, 0, nullptr);
        const float w = static_cast<float>(extent_.width), h = static_cast<float>(extent_.height);
        PushConstants push{{2 * x / w - 1, 2 * y / h - 1, 2 * (x + width) / w - 1, 2 * (y + height) / h - 1},
                           {0, 0, u1, v1},
                           mode};
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }

    // Text into its image when it changed; the box's size.
    void update_text(TextImage& text, Image& image, const std::vector<std::string>& lines, uint32_t& width,
                     uint32_t& height) {
        if (lines.empty() || !text.ok()) {
            width = height = 0;
            return;
        }
        if (text.render(lines, width, height)) vk_.upload(image, text.pixels(), text.pitch(), 4);
    }

    // The title's back buffer into the window (at its shape between black bars, through the gamma ramp),
    // the overlay, the notice and the menu on top; then present.
    void show(const std::vector<std::string>& overlay) {
        uint32_t table[256];
        bool identity = true;
        if (window_.take_gamma_table(table, identity)) {
            gamma_identity_ = identity;
            if (!identity) vk_.upload(gamma_table_, table, sizeof(table), 4);
        }
        uint32_t overlay_width = 0, overlay_height = 0, notice_width = 0, notice_height = 0;
        update_text(overlay_text_, overlay_image_, overlay, overlay_width, overlay_height);
        update_text(notice_text_, notice_image_, notice_lines_, notice_width, notice_height);

        int window_width = 0, window_height = 0;
        window_.pixel_size(window_width, window_height);
        if (recreate_ || static_cast<uint32_t>(window_width) != extent_.width ||
            static_cast<uint32_t>(window_height) != extent_.height) {
            if (!create_swapchain()) {  // minimized: the frame's work still runs
                vk_.submit_frame(VK_NULL_HANDLE, VK_NULL_HANDLE);
                return;
            }
        }
        uint32_t index = 0;
        VkSemaphore acquired = vk_.acquire_semaphore();
        VkResult r = vkAcquireNextImageKHR(vk_.device(), swapchain_, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_ = true;
            vk_.submit_frame(VK_NULL_HANDLE, VK_NULL_HANDLE);
            return;
        }
        if (r == VK_SUBOPTIMAL_KHR) recreate_ = true;

        Image* back = gfx_->back_buffer_image();
        vk_.transition(*back, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        for (Image* image : {&gamma_table_, &overlay_image_, &notice_image_})
            vk_.transition(*image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Image& target = swap_images_[index];
        target.layout = VK_IMAGE_LAYOUT_UNDEFINED;  // its old contents are not needed
        vk_.transition(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkCommandBuffer cmd = vk_.cmd();
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = target.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // black bars
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, extent_};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &rendering);
        const VkViewport viewport{0, 0, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0, 1};
        const VkRect2D scissor{{0, 0}, extent_};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Pillarbox after a few flat frames in a row, so that a moment without the scene (a fade) does not jump.
        constexpr uint32_t kPillarboxAfter = 8;
        pillarboxed_ = pillarbox_ && gfx_->frame_is_flat() && gfx_->flat_frames_before() >= kPillarboxAfter;
        const double aspect = pillarboxed_ ? static_cast<double>(width_) / height_ : aspect_;
        const WindowRect area{0, 0, static_cast<int32_t>(extent_.width), static_cast<int32_t>(extent_.height)};
        const WindowRect dest = window_.place_picture(area, aspect);
        draw_rect(*back, linear_, static_cast<float>(dest.left), static_cast<float>(dest.top),
                  static_cast<float>(dest.width()), static_cast<float>(dest.height()), 1.0f, 1.0f,
                  gamma_identity_ ? 0 : 1);
        constexpr float kTextU = 1.0f / TextImage::kWidth, kTextV = 1.0f / TextImage::kHeight;
        if (overlay_width)
            draw_rect(overlay_image_, nearest_, 4, 4, static_cast<float>(overlay_width),
                      static_cast<float>(overlay_height), overlay_width * kTextU, overlay_height * kTextV, 2);
        if (notice_width) {
            // Above the menus' button legend, the font 3 times larger at 960 lines.
            const int scale = std::max(1, area.height() / 320);
            const float width = static_cast<float>(notice_width * scale), height = static_cast<float>(notice_height * scale);
            draw_rect(notice_image_, nearest_, std::floor((area.width() - width) / 2),
                      static_cast<float>(area.height() * 7 / 10), width, height, notice_width * kTextU,
                      notice_height * kTextV, 2);
        }
        if (window_.menu_open()) {
            ImGui_ImplVulkan_NewFrame();
            window_.menu_frame();
            if (ImDrawData* data = ImGui::GetDrawData()) ImGui_ImplVulkan_RenderDrawData(data, cmd);
        }
        vkCmdEndRendering(cmd);
        vk_.transition(target, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VkSemaphore rendered = rendered_[index];
        if (!vk_.submit_frame(acquired, rendered)) return;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &rendered;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &index;
        r = vkQueuePresentKHR(vk_.queue(), &present);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            recreate_ = true;
        else if (r != VK_SUCCESS)
            LOG_ERROR("render", "Vulkan: present failed (%s)", vk::result_name(r));
    }

    std::recursive_mutex mutex_;  // the title draws from more than one thread (loading screens)
    GameWindow window_;           // first in, last out: the device's surface needs the window
    vk::Context vk_;
    std::unique_ptr<vk::VulkanGfxDevice> gfx_;
    std::unique_ptr<vk::Post> post_;  // enhanced graphics' post passes
    std::unique_ptr<vk::RenderGraph> graph_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat swap_format_ = VK_FORMAT_UNDEFINED;
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<Image> swap_images_;
    std::vector<VkSemaphore> rendered_;  // per swap chain image
    bool recreate_ = false;

    VkSampler linear_ = VK_NULL_HANDLE, nearest_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline opaque_pipeline_ = VK_NULL_HANDLE, blend_pipeline_ = VK_NULL_HANDLE;
    VkPipeline resolve_pipeline_ = VK_NULL_HANDLE;  // HDR scene -> picture (render graph)
    Image gamma_table_;
    bool gamma_identity_ = true;
    TextImage overlay_text_, notice_text_;
    Image overlay_image_, notice_image_;
    std::vector<std::string> overlay_lines_, notice_lines_;
    Image upload_;  // CPU-written back buffer (movies)

    uint32_t width_ = 640, height_ = 480;  // the title's back buffer
    float aspect_ = 4.0f / 3.0f;
    bool pillarbox_ = false, pillarboxed_ = false;
    bool wait_for_display_ = false, vsync_ = true;
    uint32_t xbox_draws_ = 0;
    bool xbox_draw_logged_ = false;
};

}  // namespace

std::unique_ptr<Backend> create_vulkan_backend(const BackendConfig& config) {
    auto backend = std::make_unique<VulkanBackend>();
    if (!backend->init(config)) return nullptr;
    return backend;
}

}  // namespace render
