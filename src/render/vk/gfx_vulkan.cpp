// GfxDevice on Vulkan. See gfx_vulkan.h. V1: targets, clears, the back buffer and read_target; the rest
// is bookkeeping until V3 draws.

#include "gfx_vulkan.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "d3d_internal.h"
#include "log.h"
#include "title_shaders.h"

namespace render::vk {

namespace {

bool is_depth(Format format) { return format == Format::D16 || format == Format::D24S8; }

class Device final : public VulkanGfxDevice {
public:
    Device(Context& context, const BackendConfig& config)
        : vk_(context),
          scale_x_(config.render_scale > 0 ? config.render_scale_x() : 1.0f),
          scale_y_(config.render_scale > 0 ? config.render_scale : 1.0f) {
        back_buffer_ = add_target(Format::BGRX8, config.back_buffer.width ? config.back_buffer.width : config.width,
                                  config.back_buffer.height ? config.back_buffer.height : config.height,
                                  config.back_buffer.data);
        if (config.depth_stencil.data)
            depth_buffer_ = add_target(Format::D24S8, config.width, config.height, config.depth_stencil.data);
        set_target(back_buffer_, depth_buffer_);
        // A new back buffer is black, not undefined.
        clear(kGfxClearColor | kGfxClearDepth | kGfxClearStencil, 0xFF000000u, 1.0f, 0);
    }

    ~Device() override {
        vk_.wait_idle();
        for (auto& [id, target] : targets_) vk_.destroy_image(target.image);
    }

    // --- GfxDevice ---

    bool open() override { return true; }
    void present() override { show_frame(); }

    TextureId create_texture(const TextureDesc& desc) override {
        const TextureId id = next_id_++;
        textures_[id] = {desc, 0};
        return id;
    }
    bool update_texture(TextureId texture, uint32_t, uint32_t, const void*, uint32_t, uint32_t) override {
        return textures_.count(texture) != 0;
    }
    void destroy_texture(TextureId texture) override { textures_.erase(texture); }

    BufferId create_buffer(const BufferDesc& desc, const void* initial) override {
        const BufferId id = next_id_++;
        std::vector<uint8_t>& bytes = buffers_[id];
        bytes.resize(desc.bytes);
        if (initial) std::memcpy(bytes.data(), initial, desc.bytes);
        return id;
    }
    void* map_buffer(BufferId buffer, uint32_t offset, uint32_t bytes, MapMode) override {
        auto it = buffers_.find(buffer);
        if (it == buffers_.end() || static_cast<uint64_t>(offset) + bytes > it->second.size()) return nullptr;
        return it->second.data() + offset;
    }
    void unmap_buffer(BufferId) override {}
    void destroy_buffer(BufferId buffer) override { buffers_.erase(buffer); }

    ShaderId create_shader(ShaderStage, const ShaderCode&) override { return next_id_++; }
    ShaderId create_title_shader(ShaderStage stage, uint32_t key, uint32_t variant) override {
        const char kind = stage == ShaderStage::Vertex ? 'v' : 'p';
        for (unsigned i = 0; i < kTitleShaderCount; ++i)
            if (kTitleShaders[i].stage == kind && kTitleShaders[i].key == key && kTitleShaders[i].variant == variant)
                return next_id_++;
        LOG_ERROR("render", "gfx: no title %s shader %08x (variant %08x)", kind == 'v' ? "vertex" : "pixel", key,
                  variant);
        return 0;
    }
    void destroy_shader(ShaderId) override {}
    LayoutId create_layout(const VertexLayout&) override { return next_id_++; }

    TargetId create_target(const TargetDesc& desc) override {
        if (!desc.width || !desc.height || desc.format == Format::Unknown) return 0;
        return add_target(desc.format, desc.width, desc.height, desc.xbox_data);
    }

    void destroy_target(TargetId id) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || id == back_buffer_ || id == depth_buffer_) return;
        if (color_ == id || depth_ == id) set_target(back_buffer_, depth_buffer_);
        for (TextureId texture : it->second.texture) textures_.erase(texture);
        vk_.destroy_image(it->second.image);
        targets_.erase(it);
    }

    TextureId target_texture(TargetId id, bool texel_coordinates) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || is_depth(it->second.format)) return 0;
        TextureId& texture = it->second.texture[texel_coordinates ? 1 : 0];
        if (!texture) {
            texture = next_id_++;
            TextureDesc desc;
            desc.texel_coordinates = texel_coordinates;
            textures_[texture] = {desc, id};
        }
        return texture;
    }

    TargetId back_buffer() override { return back_buffer_; }
    TargetId depth_buffer() override { return depth_buffer_; }

    void set_target(TargetId color, TargetId depth) override {
        auto c = targets_.find(color);
        if (c == targets_.end() || is_depth(c->second.format)) return;
        auto d = targets_.find(depth);
        const bool has_depth = d != targets_.end() && is_depth(d->second.format);
        color_ = color;
        depth_ = has_depth ? depth : 0;
        vk_.set_target(&c->second.image, has_depth ? &d->second.image : nullptr);
    }

    void clear(uint32_t flags, uint32_t color, float depth, uint32_t stencil) override {
        if (g_skip_gpu_draws || !color_) return;
        begin_draw();  // the Xbox path's CPU writes to the back buffer first
        VkClearAttachment attachments[2]{};
        uint32_t count = 0;
        if (flags & kGfxClearColor) {
            VkClearAttachment& a = attachments[count++];
            a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            a.colorAttachment = 0;
            a.clearValue.color.float32[0] = ((color >> 16) & 0xFF) / 255.0f;
            a.clearValue.color.float32[1] = ((color >> 8) & 0xFF) / 255.0f;
            a.clearValue.color.float32[2] = (color & 0xFF) / 255.0f;
            a.clearValue.color.float32[3] = (color >> 24) / 255.0f;
        }
        VkImageAspectFlags depth_aspects = 0;
        if (depth_ && (flags & kGfxClearDepth)) depth_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (depth_ && (flags & kGfxClearStencil)) depth_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (depth_aspects) {
            VkClearAttachment& a = attachments[count++];
            a.aspectMask = depth_aspects;
            a.clearValue.depthStencil = {depth, stencil};
        }
        if (!count || !vk_.begin_rendering()) return;
        const Image& target = *vk_.target_color();
        VkClearRect rect{};
        rect.rect = {{0, 0}, {target.width, target.height}};
        if (vk_.target_depth()) {  // the render area may not exceed either attachment
            rect.rect.extent.width = std::min(rect.rect.extent.width, vk_.target_depth()->width);
            rect.rect.extent.height = std::min(rect.rect.extent.height, vk_.target_depth()->height);
        }
        rect.layerCount = 1;
        vkCmdClearAttachments(vk_.cmd(), count, attachments, 1, &rect);
    }

    void set_viewport(const Viewport& viewport) override { viewport_ = viewport; }
    void set_scissor(const Rect* rect) override {
        scissor_on_ = rect != nullptr;
        if (rect) scissor_ = *rect;
    }
    void set_blend(const BlendState& state) override { blend_ = state; }
    void set_depth_stencil(const DepthStencilState& state) override { depth_stencil_ = state; }
    void set_raster(const RasterState& state) override { raster_ = state; }
    void set_fog(const Fog& fog) override { fog_ = fog; }
    void set_texture(uint32_t, TextureId) override {}
    void set_sampler(uint32_t, const SamplerState&) override {}
    void set_shaders(ShaderId, ShaderId) override {}
    void set_constants(ShaderStage, uint32_t, const float*, uint32_t) override {}
    void set_layout(LayoutId) override {}
    void set_vertex_buffer(uint32_t, BufferId, uint32_t, uint32_t) override {}
    void set_index_buffer(BufferId) override {}

    void draw(Primitive, uint32_t, uint32_t) override { count_draw(); }
    void draw_indexed(Primitive, uint32_t, uint32_t, int32_t) override { count_draw(); }
    void draw_user(Primitive, const void*, uint32_t, uint32_t, const uint16_t*, uint32_t, uint64_t) override {
        count_draw();
    }

    // The pixel centres' surface pixels (render scale), as the D3D9 back end.
    bool read_target(TargetId id, uint32_t* pixels, uint32_t width, uint32_t height) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || is_depth(it->second.format) || !pixels || !width || !height) return false;
        Image& image = it->second.image;
        std::vector<uint32_t> surface;
        if (!vk_.read(image, surface)) return false;
        const bool opaque = it->second.format == Format::BGRX8;
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t sy = std::min(image.height - 1, static_cast<uint32_t>((y + 0.5) * image.height / height));
            for (uint32_t x = 0; x < width; ++x) {
                const uint32_t sx = std::min(image.width - 1, static_cast<uint32_t>((x + 0.5) * image.width / width));
                const uint32_t value = surface[static_cast<size_t>(sy) * image.width + sx];
                pixels[y * width + x] = opaque ? value | 0xFF000000u : value;
            }
        }
        return true;
    }

    TextureId replacement_texture(uint64_t, bool) override { return 0; }

    // --- VulkanGfxDevice ---

    Image* back_buffer_image() override { return &targets_[back_buffer_].image; }

    Image* target_image(uint32_t xbox_data) override {
        for (auto& [id, target] : targets_)
            if (target.xbox_data == xbox_data && !is_depth(target.format)) return &target.image;
        return nullptr;
    }

    void set_target_by_data(uint32_t color_data, uint32_t depth_data) override {
        TargetId color = 0, depth = 0;
        for (auto& [id, target] : targets_) {
            if (!target.xbox_data) continue;
            if (target.xbox_data == color_data && !is_depth(target.format)) color = id;
            if (depth_data && target.xbox_data == depth_data && is_depth(target.format)) depth = id;
        }
        if (color) set_target(color, depth);
    }

    bool frame_is_flat() const override { return !frame_writes_depth_; }
    uint32_t flat_frames_before() const override { return flat_frames_; }
    uint32_t unsupported_draws() const override { return unsupported_draws_; }

    void end_frame() override {
        flat_frames_ = frame_is_flat() ? flat_frames_ + 1 : 0;
        frame_writes_depth_ = false;
        unsupported_draws_ = 0;
        // The title draws its next frame into the back buffer from its own state.
        set_target(back_buffer_, depth_buffer_);
    }

    void set_frame_buffers(const SurfaceDesc& back, const SurfaceDesc& depth) override {
        targets_[back_buffer_].xbox_data = back.data;
        if (depth_buffer_) targets_[depth_buffer_].xbox_data = depth.data;
    }

    void forget_title_resources() override {
        std::vector<TargetId> others;
        for (auto& [id, target] : targets_)
            if (id != back_buffer_ && id != depth_buffer_) others.push_back(id);
        for (TargetId id : others) destroy_target(id);
        textures_.clear();
        buffers_.clear();
    }

private:
    struct Target {
        Format format = Format::BGRA8;
        uint32_t xbox_data = 0;
        Image image;
        TextureId texture[2] = {0, 0};  // normalized, texel coordinates
    };
    struct Texture {
        TextureDesc desc;
        TargetId target = 0;  // a render target's picture
    };

    TargetId add_target(Format format, uint32_t width, uint32_t height, uint32_t xbox_data) {
        const uint32_t surface_width = std::max(1u, static_cast<uint32_t>(std::lround(width * scale_x_)));
        const uint32_t surface_height = std::max(1u, static_cast<uint32_t>(std::lround(height * scale_y_)));
        Target target;
        target.format = format;
        target.xbox_data = xbox_data;
        const bool depth = is_depth(format);
        // V1: every colour target is 8-bit BGRA (V3 picks per format).
        const VkFormat vk_format = depth ? vk_.depth_format() : VK_FORMAT_B8G8R8A8_UNORM;
        const VkImageUsageFlags usage =
            depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                  : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!vk_.create_image(target.image, vk_format, surface_width, surface_height, usage)) return 0;
        const TargetId id = next_id_++;
        targets_.emplace(id, std::move(target));
        return id;
    }

    void count_draw() {
        ++unsupported_draws_;
        if (depth_ && depth_stencil_.depth_test && depth_stencil_.depth_write) frame_writes_depth_ = true;
    }

    Context& vk_;
    float scale_x_, scale_y_;
    uint32_t next_id_ = 1;
    std::unordered_map<TargetId, Target> targets_;
    std::unordered_map<TextureId, Texture> textures_;
    std::unordered_map<BufferId, std::vector<uint8_t>> buffers_;
    TargetId back_buffer_ = 0, depth_buffer_ = 0;
    TargetId color_ = 0, depth_ = 0;
    Viewport viewport_;
    bool scissor_on_ = false;
    Rect scissor_;
    BlendState blend_;
    DepthStencilState depth_stencil_;
    RasterState raster_;
    Fog fog_;
    bool frame_writes_depth_ = false;
    uint32_t flat_frames_ = 0;
    uint32_t unsupported_draws_ = 0;
};

}  // namespace

std::unique_ptr<VulkanGfxDevice> create_vulkan_gfx_device(Context& context, const BackendConfig& config) {
    return std::make_unique<Device>(context, config);
}

}  // namespace render::vk
