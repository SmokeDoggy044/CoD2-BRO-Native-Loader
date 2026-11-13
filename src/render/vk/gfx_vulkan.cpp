// GfxDevice on Vulkan. See gfx_vulkan.h. V1: targets, clears, the back buffer and read_target; the rest
// is bookkeeping until V3 draws.

#include "gfx_vulkan.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "d3d_internal.h"
#include "log.h"
#include "spirv_layout.h"
#include "title_shaders.h"

namespace render::vk {

namespace {

bool is_depth(Format format) { return format == Format::D16 || format == Format::D24S8; }
bool is_block(Format format) { return format == Format::DXT1 || format == Format::DXT3 || format == Format::DXT5; }

// Bytes per row of `width` pixels (per row of 4x4 blocks for DXT), as the title's memory holds them.
uint32_t row_bytes(Format format, uint32_t width) {
    switch (format) {
        case Format::BGRA8: case Format::BGRX8: return width * 4;
        case Format::B5G6R5: case Format::BGR5A1: case Format::BGRX5: case Format::BGRA4: case Format::A8L8:
            return width * 2;
        case Format::L8: case Format::A8: return width;
        case Format::DXT1: return (width + 3) / 4 * 8;
        case Format::DXT3: case Format::DXT5: return (width + 3) / 4 * 16;
        default: return 0;
    }
}

// Our formats as Vulkan formats every Vulkan 1.3 device samples (BC needs textureCompressionBC), with the
// view's component mapping giving D3D9's channels: bit layouts match D3D9's (R5G6B5, A1R5G5B5); A4R4G4B4
// is B4G4R4A4 read with its channels moved; L8 / A8 / A8L8 are R8 / R8G8 spread out.
VkFormat texture_format(Format format, VkComponentMapping& c) {
    using S = VkComponentSwizzle;
    const S r = VK_COMPONENT_SWIZZLE_R, g = VK_COMPONENT_SWIZZLE_G, b = VK_COMPONENT_SWIZZLE_B,
            a = VK_COMPONENT_SWIZZLE_A, one = VK_COMPONENT_SWIZZLE_ONE, zero = VK_COMPONENT_SWIZZLE_ZERO;
    c = {r, g, b, a};
    switch (format) {
        case Format::BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRX8: c.a = one; return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::B5G6R5: return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case Format::BGR5A1: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        case Format::BGRX5: c.a = one; return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        // A4R4G4B4 (A 15-12, R 11-8, G 7-4, B 3-0) read as B4G4R4A4 (B 15-12, G 11-8, R 7-4, A 3-0).
        case Format::BGRA4: c = {g, r, a, b}; return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
        case Format::L8: c = {r, r, r, one}; return VK_FORMAT_R8_UNORM;
        case Format::A8: c = {zero, zero, zero, r}; return VK_FORMAT_R8_UNORM;
        case Format::A8L8: c = {r, r, r, g}; return VK_FORMAT_R8G8_UNORM;  // byte 0 L, byte 1 A
        case Format::DXT1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case Format::DXT3: return VK_FORMAT_BC2_UNORM_BLOCK;
        case Format::DXT5: return VK_FORMAT_BC3_UNORM_BLOCK;
        default: return VK_FORMAT_UNDEFINED;
    }
}

uint32_t full_chain(uint32_t width, uint32_t height, uint32_t depth) {
    uint32_t size = std::max({width, height, depth}), levels = 1;
    while (size > 1) size >>= 1, ++levels;
    return levels;
}

// A replacement picture's levels: each a 2x2 box filter of the one before (as the D3D9 back end).
std::vector<uint32_t> half_level(const std::vector<uint32_t>& level, uint32_t width, uint32_t height) {
    const uint32_t w = std::max(width / 2, 1u), h = std::max(height / 2, 1u);
    std::vector<uint32_t> next(static_cast<size_t>(w) * h, 0);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
            const uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
            const uint32_t texels[4] = {level[y0 * width + x0], level[y0 * width + x1], level[y1 * width + x0],
                                        level[y1 * width + x1]};
            uint32_t out = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                uint32_t sum = 2;
                for (uint32_t t : texels) sum += (t >> shift) & 0xFF;
                out |= (sum / 4) << shift;
            }
            next[static_cast<size_t>(y) * w + x] = out;
        }
    return next;
}

class Device final : public VulkanGfxDevice {
public:
    Device(Context& context, const BackendConfig& config)
        : vk_(context),
          scale_x_(config.render_scale > 0 ? config.render_scale_x() : 1.0f),
          scale_y_(config.render_scale > 0 ? config.render_scale : 1.0f),
          replace_texture_(config.replace_texture),
          replacement_version_(config.replacement_version) {
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
        for (auto& [id, texture] : textures_) vk_.destroy_image(texture.image);
        for (auto& [id, buffer] : buffers_) release_buffer(buffer);
        for (auto& [id, shader] : shaders_) vkDestroyShaderModule(vk_.device(), shader.module, nullptr);
    }

    // --- GfxDevice ---

    bool open() override { return true; }
    void present() override { show_frame(); }

    // --- Textures ---

    TextureId create_texture(const TextureDesc& desc) override {
        TextureImageDesc image;
        image.format = texture_format(desc.format, image.components);
        if (image.format == VK_FORMAT_UNDEFINED || !desc.width || !desc.height) {
            LOG_ERROR("render", "gfx: texture format %u: no Vulkan format", static_cast<unsigned>(desc.format));
            return 0;
        }
        image.view_type = desc.type == TextureType::Cube     ? VK_IMAGE_VIEW_TYPE_CUBE
                          : desc.type == TextureType::Volume ? VK_IMAGE_VIEW_TYPE_3D
                                                             : VK_IMAGE_VIEW_TYPE_2D;
        image.width = desc.width;
        image.height = desc.type == TextureType::Cube ? desc.width : desc.height;
        image.depth = desc.type == TextureType::Volume ? std::max(1u, desc.depth) : 1;
        const uint32_t chain = full_chain(image.width, image.height, image.depth);
        image.levels = desc.levels ? std::min(desc.levels, chain) : chain;
        Texture t;
        t.desc = desc;
        t.desc.levels = image.levels;
        if (!vk_.create_texture_image(t.image, image)) return 0;
        const TextureId id = next_id_++;
        textures_.emplace(id, std::move(t));
        return id;
    }

    bool update_texture(TextureId id, uint32_t level, uint32_t face, const void* data, uint32_t pitch,
                        uint32_t slice_pitch) override {
        auto it = textures_.find(id);
        if (it == textures_.end() || !it->second.image || !data || g_skip_gpu_draws) return it != textures_.end();
        Texture& t = it->second;
        const uint32_t width = std::max(1u, t.image.width >> level);
        const uint32_t height = std::max(1u, t.image.height >> level);
        const uint32_t rows = is_block(t.desc.format) ? (height + 3) / 4 : height;
        return vk_.upload_level(t.image, level, t.desc.type == TextureType::Cube ? face : 0, data,
                                row_bytes(t.desc.format, width), rows, pitch, slice_pitch);
    }

    void destroy_texture(TextureId id) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) return;
        for (TextureId& bound : bound_textures_)
            if (bound == id) bound = 0;
        vk_.destroy_image(it->second.image);
        if (it->second.replacement)
            replacements_.erase(replacement_key(it->second.replacement, it->second.mipmaps));
        textures_.erase(it);
    }

    // --- Buffers ---
    // Static: device memory, written through the upload space in command order (draws recorded before a
    // write read the old bytes, as D3D9). Dynamic: versions in host-visible memory; Discard (or Write while
    // the GPU may still read the current one) moves to a version no queued frame reads, NoOverwrite writes
    // into the current one.

    BufferId create_buffer(const BufferDesc& desc, const void* initial) override {
        if (!desc.bytes) return 0;
        Buffer b;
        b.desc = desc;
        if (desc.usage == BufferUsage::Static) {
            if (!create_vk_buffer(desc.bytes, false, b.statik)) return 0;
        } else {
            b.versions.emplace_back();
            if (!create_vk_buffer(desc.bytes, true, b.versions.back())) return 0;
        }
        const BufferId id = next_id_++;
        buffers_.emplace(id, std::move(b));
        if (initial) {
            if (void* memory = map_buffer(id, 0, desc.bytes, MapMode::Write)) {
                std::memcpy(memory, initial, desc.bytes);
                unmap_buffer(id);
            }
        }
        return id;
    }

    void* map_buffer(BufferId id, uint32_t offset, uint32_t bytes, MapMode mode) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end() || static_cast<uint64_t>(offset) + bytes > it->second.desc.bytes ||
            it->second.mapped)
            return nullptr;
        Buffer& b = it->second;
        b.mapped = true;
        b.map_offset = offset;
        b.map_bytes = bytes;
        if (b.desc.usage == BufferUsage::Static) {
            b.pending.resize(bytes);
            return b.pending.data();
        }
        if (mode != MapMode::NoOverwrite && in_use(b.versions[b.current])) {
            const uint32_t old = b.current;
            b.current = free_version(b);
            // Write keeps the bytes outside the mapped range; Discard drops them.
            if (mode == MapMode::Write)
                std::memcpy(b.versions[b.current].memory, b.versions[old].memory, b.desc.bytes);
        }
        return b.versions[b.current].memory + offset;
    }

    void unmap_buffer(BufferId id) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end() || !it->second.mapped) return;
        Buffer& b = it->second;
        b.mapped = false;
        if (b.desc.usage == BufferUsage::Static) {
            if (!g_skip_gpu_draws) vk_.upload_buffer(b.statik.buffer, b.map_offset, b.pending.data(), b.map_bytes);
            b.pending.clear();
            b.pending.shrink_to_fit();
        } else {
            vmaFlushAllocation(vk_.allocator(), b.versions[b.current].allocation, b.map_offset, b.map_bytes);
        }
    }

    void destroy_buffer(BufferId id) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end()) return;
        for (Stream& s : streams_)
            if (s.buffer == id) s = {};
        if (index_buffer_ == id) index_buffer_ = 0;
        release_buffer(it->second);
        buffers_.erase(it);
    }

    // The SPIR-V with our fixed layout (spirv_layout.h) as a shader module; no SPIR-V (hand-made test
    // tokens) = none.
    ShaderId create_shader(ShaderStage stage, const ShaderCode& code) override {
        std::string error;
        const std::vector<uint32_t> words =
            spirv_fixed_layout(code.d3d9, code.d3d9_bytes, code.spirv, code.spirv_bytes, &error);
        if (words.empty()) {
            LOG_ERROR("render", "gfx: %s shader without usable SPIR-V: %s",
                      stage == ShaderStage::Vertex ? "vertex" : "pixel", error.c_str());
            return 0;
        }
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = words.size() * 4;
        info.pCode = words.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (VkResult r = vkCreateShaderModule(vk_.device(), &info, nullptr, &module); r != VK_SUCCESS) {
            LOG_ERROR("render", "gfx: vkCreateShaderModule failed (%d)", int(r));
            return 0;
        }
        const ShaderId id = next_id_++;
        shaders_[id] = {stage, module};
        return id;
    }
    ShaderId create_title_shader(ShaderStage stage, uint32_t key, uint32_t variant) override {
        const char kind = stage == ShaderStage::Vertex ? 'v' : 'p';
        for (unsigned i = 0; i < kTitleShaderCount; ++i)
            if (kTitleShaders[i].stage == kind && kTitleShaders[i].key == key && kTitleShaders[i].variant == variant)
                return create_shader(stage, {kTitleShaders[i].d3d9, kTitleShaders[i].d3d9_bytes,
                                             kTitleShaders[i].spirv, kTitleShaders[i].spirv_bytes});
        LOG_ERROR("render", "gfx: no title %s shader %08x (variant %08x)", kind == 'v' ? "vertex" : "pixel", key,
                  variant);
        return 0;
    }
    // Pipelines keep no reference to their modules: a module can go at once.
    void destroy_shader(ShaderId shader) override {
        auto it = shaders_.find(shader);
        if (it == shaders_.end()) return;
        vkDestroyShaderModule(vk_.device(), it->second.module, nullptr);
        shaders_.erase(it);
    }
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
    void set_texture(uint32_t slot, TextureId texture) override {
        if (slot < kGfxTextureSlots) bound_textures_[slot] = texture;
    }
    void set_sampler(uint32_t, const SamplerState&) override {}
    void set_shaders(ShaderId, ShaderId) override {}
    void set_constants(ShaderStage, uint32_t, const float*, uint32_t) override {}
    void set_layout(LayoutId) override {}
    void set_vertex_buffer(uint32_t stream, BufferId buffer, uint32_t offset, uint32_t stride) override {
        if (stream < kGfxVertexStreams) streams_[stream] = {buffer, offset, stride};
    }
    void set_index_buffer(BufferId buffer) override { index_buffer_ = buffer; }

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

    // The PC's picture for title texture contents (BackendConfig::replace_texture): BGRA8 with a full chain of
    // box-filtered levels when the title's has mipmaps. Same id while it stays; rebuilt when its version moves.
    TextureId replacement_texture(uint64_t content, bool mipmaps) override {
        if (!replace_texture_) return 0;
        const uint64_t key = replacement_key(content, mipmaps);
        const uint32_t version = replacement_version_ ? replacement_version_(content) : 0;
        auto known = replacements_.find(key);
        if (known != replacements_.end()) {
            Texture& t = textures_[known->second.id];
            if (known->second.version == version) return t.image ? known->second.id : 0;
            vk_.destroy_image(t.image);
        }
        Replacement& r = replacements_[key];
        r.version = version;
        if (!r.id) {
            r.id = next_id_++;
            Texture& t = textures_[r.id];
            t.replacement = content;
            t.mipmaps = mipmaps;
        }
        Texture& t = textures_[r.id];
        const std::shared_ptr<const TextureReplacement> picture = replace_texture_(content, scale_y_);
        if (!picture || !picture->width || !picture->height ||
            picture->pixels.size() < static_cast<size_t>(picture->width) * picture->height)
            return 0;
        TextureImageDesc image;
        image.width = picture->width;
        image.height = picture->height;
        image.levels = mipmaps ? full_chain(image.width, image.height, 1) : 1;
        if (!vk_.create_texture_image(t.image, image)) return 0;
        t.desc.width = image.width;
        t.desc.height = image.height;
        t.desc.levels = image.levels;
        std::vector<uint32_t> level = picture->pixels;
        uint32_t width = image.width, height = image.height;
        for (uint32_t i = 0; i < image.levels; ++i) {
            if (i > 0) {
                level = half_level(level, width, height);
                width = std::max(width / 2, 1u);
                height = std::max(height / 2, 1u);
            }
            vk_.upload_level(t.image, i, 0, level.data(), width * 4, height, width * 4, 0);
        }
        return r.id;
    }

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
        for (auto& [id, texture] : textures_) vk_.destroy_image(texture.image);
        for (auto& [id, buffer] : buffers_) release_buffer(buffer);
        textures_.clear();
        buffers_.clear();
        replacements_.clear();
        for (TextureId& bound : bound_textures_) bound = 0;
        for (Stream& s : streams_) s = {};
        index_buffer_ = 0;
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
        Image image;                // ours / a replacement's
        TargetId target = 0;        // a render target's picture
        uint64_t replacement = 0;   // a replacement picture, by content
        bool mipmaps = false;
    };
    struct Replacement {
        TextureId id = 0;
        uint32_t version = 0;
    };
    struct VkBuf {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        uint8_t* memory = nullptr;  // host-visible versions
        uint64_t used = 0;          // 1 + the frame number of the last draw reading it; 0 none
    };
    struct Buffer {
        BufferDesc desc;
        VkBuf statik;                 // Static
        std::vector<VkBuf> versions;  // Dynamic
        uint32_t current = 0;
        bool mapped = false;
        uint32_t map_offset = 0, map_bytes = 0;
        std::vector<uint8_t> pending;  // Static: the mapped bytes until unmap
    };
    struct Stream {
        BufferId buffer = 0;
        uint32_t offset = 0, stride = 0;
    };

    static uint64_t replacement_key(uint64_t content, bool mipmaps) { return content << 1 | (mipmaps ? 1 : 0); }

    bool create_vk_buffer(uint32_t bytes, bool host, VkBuf& out) {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bytes;
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     (host ? 0 : VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VmaAllocationCreateInfo memory{};
        memory.usage = VMA_MEMORY_USAGE_AUTO;
        if (host)
            memory.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mapped{};
        if (vmaCreateBuffer(vk_.allocator(), &info, &memory, &out.buffer, &out.allocation, &mapped) != VK_SUCCESS) {
            LOG_ERROR("render", "gfx: buffer of %u bytes: cannot create", bytes);
            out = {};
            return false;
        }
        out.memory = static_cast<uint8_t*>(mapped.pMappedData);
        return true;
    }

    // A queued (or the current) frame may read it.
    bool in_use(const VkBuf& v) const { return v.used && v.used - 1 + kFramesInFlight > vk_.frame_number(); }

    uint32_t free_version(Buffer& b) {
        for (uint32_t i = 0; i < b.versions.size(); ++i)
            if (!in_use(b.versions[i])) return i;
        b.versions.emplace_back();
        if (!create_vk_buffer(b.desc.bytes, true, b.versions.back())) {
            b.versions.pop_back();
            return b.current;  // nothing better: overwrite (may show in a queued frame)
        }
        if (b.versions.size() == 16)
            LOG_WARN("render", "gfx: a dynamic buffer of %u bytes has 16 versions (discarded per draw?)",
                     b.desc.bytes);
        return static_cast<uint32_t>(b.versions.size() - 1);
    }

    void release_buffer(Buffer& b) {
        if (b.statik.buffer) vk_.defer_destroy(b.statik.buffer, b.statik.allocation);
        for (VkBuf& v : b.versions) vk_.defer_destroy(v.buffer, v.allocation);
        b.statik = {};
        b.versions.clear();
    }

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
    std::unordered_map<BufferId, Buffer> buffers_;
    std::unordered_map<uint64_t, Replacement> replacements_;
    decltype(BackendConfig::replace_texture) replace_texture_;
    decltype(BackendConfig::replacement_version) replacement_version_;
    TextureId bound_textures_[kGfxTextureSlots] = {};
    Stream streams_[kGfxVertexStreams];
    BufferId index_buffer_ = 0;
    struct Shader {
        ShaderStage stage;
        VkShaderModule module;
    };
    std::unordered_map<ShaderId, Shader> shaders_;
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
