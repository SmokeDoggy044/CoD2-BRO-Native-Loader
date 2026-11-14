// GfxDevice on Vulkan. See gfx_vulkan.h. Reference behaviour: gfx_d3d9.cpp (the D3D9 back end); shared
// maths in draw_placement.h, the shaders' layout in spirv_layout.h.

#include "gfx_vulkan.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <atomic>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "d3d_internal.h"
#include "draw_placement.h"
#include "enhanced_graphics.h"
#include "log.h"
#include "texture_hash.h"
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

VkCompareOp compare_op(Compare c) { return static_cast<VkCompareOp>(c); }        // same order
VkStencilOp stencil_op(StencilOp o) { return static_cast<VkStencilOp>(o); }     // same order

VkBlendFactor blend_factor(Blend b) {
    switch (b) {
        case Blend::Zero: return VK_BLEND_FACTOR_ZERO;
        case Blend::One: return VK_BLEND_FACTOR_ONE;
        case Blend::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case Blend::InvSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case Blend::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case Blend::InvSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case Blend::DestAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case Blend::InvDestAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case Blend::DestColor: return VK_BLEND_FACTOR_DST_COLOR;
        case Blend::InvDestColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case Blend::SrcAlphaSat: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case Blend::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case Blend::InvConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case Blend::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case Blend::InvConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp blend_op(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return VK_BLEND_OP_ADD;
        case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min: return VK_BLEND_OP_MIN;
        case BlendOp::Max: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkFormat attribute_format(AttributeType t) {
    switch (t) {
        case AttributeType::Float1: return VK_FORMAT_R32_SFLOAT;
        case AttributeType::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case AttributeType::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case AttributeType::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case AttributeType::Color: return VK_FORMAT_B8G8R8A8_UNORM;  // D3DCOLOR: B G R A in memory
        case AttributeType::UByte4: return VK_FORMAT_R8G8B8A8_USCALED;
        case AttributeType::UByte4N: return VK_FORMAT_R8G8B8A8_UNORM;
        case AttributeType::Short2: return VK_FORMAT_R16G16_SSCALED;
        case AttributeType::Short4: return VK_FORMAT_R16G16B16A16_SSCALED;
        case AttributeType::Short2N: return VK_FORMAT_R16G16_SNORM;
        case AttributeType::Short4N: return VK_FORMAT_R16G16B16A16_SNORM;
        case AttributeType::Half2: return VK_FORMAT_R16G16_SFLOAT;
        case AttributeType::Half4: return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

VkPrimitiveTopology topology(Primitive p) {
    switch (p) {
        case Primitive::Points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case Primitive::Lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case Primitive::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case Primitive::Triangles: case Primitive::Quads: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case Primitive::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case Primitive::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

// Vertices a primitive needs at least (as D3D9: incomplete primitives are dropped).
bool enough_vertices(Primitive p, uint32_t n) {
    switch (p) {
        case Primitive::Points: return n >= 1;
        case Primitive::Lines: case Primitive::LineStrip: return n >= 2;
        case Primitive::Quads: return n >= 4;
        default: return n >= 3;
    }
}

void unpack_color(uint32_t color, float* out) {
    out[0] = ((color >> 16) & 0xFF) / 255.0f;
    out[1] = ((color >> 8) & 0xFF) / 255.0f;
    out[2] = (color & 0xFF) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}

void warn_once(bool& done, const char* message) {
    if (done) return;
    done = true;
    LOG_WARN("render", "gfx (Vulkan): %s", message);
}

constexpr uint32_t kQuadLimit = 16384;                   // quads per draw from the shared index buffer
constexpr uint32_t kDefaultBinding = kGfxVertexStreams;  // (0, 0, 0, 1) for shader inputs no stream gives

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
        narrow_ui_draw_ = config.narrow_ui_draw;
        back_width_ = config.width;
        back_height_ = config.height;
        anisotropy_ = vk_.features().samplerAnisotropy
                          ? std::min<uint32_t>(config.anisotropy,
                                               static_cast<uint32_t>(vk_.limits().maxSamplerAnisotropy))
                          : 1;
        pipeline_cache_path_ = config.pipeline_cache_path;
        create_draw_objects();
        if (vk_.pipeline_libraries()) start_prebuild();
        // A new back buffer is black, not undefined.
        clear(kGfxClearColor | kGfxClearDepth | kGfxClearStencil, 0xFF000000u, 1.0f, 0);
    }

    ~Device() override {
        vk_.wait_idle();
        for (auto& [id, target] : targets_) vk_.destroy_image(target.image);
        for (auto& [id, texture] : textures_) vk_.destroy_image(texture.image);
        for (auto& [id, buffer] : buffers_) release_buffer(buffer);
        join_prebuild();
        for (auto& [id, shader] : shaders_) release_shader(shader);
        for (auto& [code, shader] : prebuilt_) {
            if (shader.library) vk_.defer_destroy(shader.library);
            vkDestroyShaderModule(vk_.device(), shader.module, nullptr);
        }
        for (auto& [key, library] : stage_libraries_) vk_.defer_destroy(library);
        for (auto& [id, target] : targets_) vk_.destroy_image(target.copy);
        for (auto& [key, kept] : kept_) vk_.defer_destroy(kept.buffer.buffer, kept.buffer.allocation);
        for (auto& [key, pipeline] : pipelines_) vk_.defer_destroy(pipeline);
        for (auto& [key, sampler] : samplers_) vkDestroySampler(vk_.device(), sampler, nullptr);
        vk_.destroy_image(dummy_texture_);
        vk_.destroy_image(scene_);
        vk_.destroy_image(scene_copy_);
        if (default_vertex_.buffer) vk_.defer_destroy(default_vertex_.buffer, default_vertex_.allocation);
        if (quad_indices_.buffer) vk_.defer_destroy(quad_indices_.buffer, quad_indices_.allocation);
        save_pipeline_cache();
        if (pipeline_cache_) vkDestroyPipelineCache(vk_.device(), pipeline_cache_, nullptr);
        if (pipeline_layout_) vkDestroyPipelineLayout(vk_.device(), pipeline_layout_, nullptr);
        if (set_layout_) vkDestroyDescriptorSetLayout(vk_.device(), set_layout_, nullptr);
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
        if (it == textures_.end() || !it->second.image || !data || (g_skip_gpu_draws && !g_skip_keeps_uploads)) return it != textures_.end();
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
            if (!g_skip_gpu_draws || g_skip_keeps_uploads) vk_.upload_buffer(b.statik.buffer, b.map_offset, b.pending.data(), b.map_bytes);
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
        join_prebuild();
        if (auto found = prebuilt_.find(code.spirv); found != prebuilt_.end() && found->second.stage == stage) {
            const ShaderId id = next_id_++;
            shaders_[id] = found->second;
            shaders_[id].shared = true;
            return id;
        }
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
        shaders_[id] = {stage, module, stage == ShaderStage::Vertex ? spirv_input_mask(words) : 0,
                        vk_.pipeline_libraries() ? shader_library(stage, module) : VK_NULL_HANDLE};
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
        release_shader(it->second);
        shaders_.erase(it);
        // Pipelines made with it go too (a new shader may get the same id's module handle).
        for (auto p = pipelines_.begin(); p != pipelines_.end();) {
            if (p->first.vs == shader || p->first.ps == shader) {
                vk_.defer_destroy(p->second);
                p = pipelines_.erase(p);
            } else {
                ++p;
            }
        }
        if (vertex_shader_ == shader) vertex_shader_ = 0;
        if (pixel_shader_ == shader) pixel_shader_ = 0;
    }
    LayoutId create_layout(const VertexLayout& layout) override {
        if (layout.count > kGfxVertexInputs) return 0;
        std::vector<VertexAttribute> attributes(layout.attributes, layout.attributes + layout.count);
        for (const VertexAttribute& a : attributes)
            if (a.input >= kGfxVertexInputs || a.stream >= kGfxVertexStreams) return 0;
        layouts_.push_back(std::move(attributes));
        return static_cast<LayoutId>(layouts_.size());
    }

    TargetId create_target(const TargetDesc& desc) override {
        if (!desc.width || !desc.height || desc.format == Format::Unknown) return 0;
        // The same title memory is the same target (as on the D3D9 back end, D3D9Draw keys targets by it): the
        // renderer's target for the frame buffers' memory is the back buffer the picture shows.
        if (desc.xbox_data)
            for (auto& [id, target] : targets_)
                if (target.xbox_data == desc.xbox_data && is_depth(target.format) == is_depth(desc.format)) {
                    ++target.users;
                    return id;
                }
        return add_target(desc.format, desc.width, desc.height, desc.xbox_data);
    }

    void destroy_target(TargetId id) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || id == back_buffer_ || id == depth_buffer_) return;
        if (it->second.users > 1) {  // also made for the same memory by another
            --it->second.users;
            return;
        }
        if (color_ == id || depth_ == id) set_target(back_buffer_, depth_buffer_);
        for (TextureId texture : it->second.texture) textures_.erase(texture);
        vk_.destroy_image(it->second.image);
        vk_.destroy_image(it->second.copy);
        targets_.erase(it);
    }

    TextureId target_texture(TargetId id, bool texel_coordinates) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || is_depth(it->second.format)) return 0;
        TextureId& texture = it->second.texture[texel_coordinates ? 1 : 0];
        if (!texture) {
            texture = next_id_++;
            Texture& t = textures_[texture];
            t.desc.texel_coordinates = texel_coordinates;
            t.target = id;
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
        if (texture < replacement_bound_.size()) replacement_bound_[texture] = frames_;
    }
    void set_sampler(uint32_t slot, const SamplerState& state) override {
        if (slot < kGfxTextureSlots) sampler_states_[slot] = state;
    }
    void set_shaders(ShaderId vertex, ShaderId pixel) override {
        vertex_shader_ = vertex;
        pixel_shader_ = pixel;
    }
    void set_constants(ShaderStage stage, uint32_t first, const float* values, uint32_t count) override {
        auto& constants = stage == ShaderStage::Vertex ? vs_constants_ : ps_constants_;
        const uint32_t limit = stage == ShaderStage::Vertex ? kGfxVsScreenToClip : kGfxPsTexcoordScale;
        if (first >= limit) return;
        count = std::min(count, limit - first);
        std::memcpy(constants.data() + first * 4, values, count * 16);
    }
    void set_layout(LayoutId layout) override { layout_ = layout; }
    void set_vertex_buffer(uint32_t stream, BufferId buffer, uint32_t offset, uint32_t stride) override {
        if (stream < kGfxVertexStreams) streams_[stream] = {buffer, offset, stride};
    }
    void set_index_buffer(BufferId buffer) override { index_buffer_ = buffer; }

    void draw(Primitive primitive, uint32_t first_vertex, uint32_t vertex_count) override {
        if (g_skip_gpu_draws || !enough_vertices(primitive, vertex_count)) return;
        if (!bind_streams() || !prepare(primitive)) return;
        if (primitive == Primitive::Quads) {
            draw_quads(first_vertex, vertex_count);
            return;
        }
        vkCmdDraw(vk_.cmd(), vertex_count, 1, first_vertex, 0);
    }

    void draw_indexed(Primitive primitive, uint32_t first_index, uint32_t index_count, int32_t base_vertex) override {
        if (g_skip_gpu_draws || !enough_vertices(primitive, index_count)) return;
        if (primitive == Primitive::Quads) {
            static bool once = false;
            warn_once(once, "indexed quads are not supported (draw them as triangles)");
            return;
        }
        auto ib = buffers_.find(index_buffer_);
        if (ib == buffers_.end() || ib->second.desc.kind == BufferKind::Vertex) return;
        if (!bind_streams() || !prepare(primitive)) return;
        VkBuf& b = buffer_for_draw(ib->second);
        vkCmdBindIndexBuffer(vk_.cmd(), b.buffer, 0,
                             ib->second.desc.kind == BufferKind::Index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(vk_.cmd(), index_count, 1, first_index, base_vertex, 0);
    }

    // Caller vertices go into the upload space (or a kept copy, see kept_vertices). Staging can flush the
    // frame's commands when the space is full: that drops the recorded pipeline and descriptors, so the draw
    // is prepared again (once; the space is empty after a flush).
    void draw_user(Primitive primitive, const void* vertices, uint32_t vertex_count, uint32_t stride,
                   const uint16_t* indices, uint32_t index_count, uint64_t cache_key) override {
        if (g_skip_gpu_draws || !vertices || !stride || !vertex_count) return;
        if (!enough_vertices(primitive, indices ? index_count : vertex_count) || (indices && primitive == Primitive::Quads))
            return;
        float range[4];  // x min, max, y min, max
        const bool has_range = layout_ && layout_ <= layouts_.size() &&
                               position_range(layouts_[layout_ - 1].data(),
                                              static_cast<uint32_t>(layouts_[layout_ - 1].size()), vertices,
                                              vertex_count, stride, range);
        const uint32_t bytes = vertex_count * stride;
        const VkBuf* kept = kept_vertices(cache_key, vertices, bytes);
        Staged vertex_staged, index_staged;
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!prepare(primitive, has_range ? range : nullptr, true, stride)) return;
            const uint64_t epoch = vk_.staging_epoch();
            if (!kept) {
                vertex_staged = vk_.stage(bytes, 4);
                if (!vertex_staged.memory) return;
                std::memcpy(vertex_staged.memory, vertices, bytes);
            }
            if (indices) {
                index_staged = vk_.stage(index_count * 2, 4);
                if (!index_staged.memory) return;
                std::memcpy(index_staged.memory, indices, index_count * 2);
            }
            if (vk_.staging_epoch() == epoch) break;
            if (attempt == 1) return;
        }
        // Stream 0 at offset 0 of its binding: vertex indices start at 0.
        const VkBuffer buffer = kept ? kept->buffer : vertex_staged.buffer;
        const VkDeviceSize offset = kept ? 0 : vertex_staged.offset;
        const VkDeviceSize stride64 = stride;
        vkCmdBindVertexBuffers2(vk_.cmd(), 0, 1, &buffer, &offset, nullptr,
                                vk_.pipeline_libraries() ? nullptr : &stride64);
        if (primitive == Primitive::Quads) {
            draw_quads(0, vertex_count);
            return;
        }
        if (!indices) {
            vkCmdDraw(vk_.cmd(), vertex_count, 1, 0, 0);
            return;
        }
        vkCmdBindIndexBuffer(vk_.cmd(), index_staged.buffer, index_staged.offset, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(vk_.cmd(), index_count, 1, 0, 0, 0);
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
            if (replacement_bound_.size() <= r.id) replacement_bound_.resize(r.id + 1, 0);
            replacement_bound_[r.id] = frames_;
            Texture& t = textures_[r.id];
            t.replacement = content;
            t.mipmaps = mipmaps;
        }
        Texture& t = textures_[r.id];
        const std::shared_ptr<const TextureReplacement> picture = replace_texture_(content, scale_y_);
        if (!picture || !replacement_valid(*picture)) return 0;
        TextureImageDesc image;
        image.width = picture->width;
        image.height = picture->height;
        if (picture->compression != TextureReplacement::Compression::None) {
            const bool dxt1 = picture->compression == TextureReplacement::Compression::Dxt1;
            image.format = dxt1 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
            image.levels = mipmaps ? static_cast<uint32_t>(picture->levels.size()) : 1;
            if (!vk_.create_texture_image(t.image, image)) return 0;
            t.desc.format = dxt1 ? Format::DXT1 : Format::DXT5;
            t.desc.width = image.width;
            t.desc.height = image.height;
            t.desc.levels = image.levels;
            uint32_t width = image.width, height = image.height;
            for (uint32_t i = 0; i < image.levels; ++i) {
                const uint32_t row = replacement_block_row_bytes(picture->compression, width);
                vk_.upload_level(t.image, i, 0, picture->levels[i].data(), row, (height + 3) / 4, row, 0);
                width = std::max(width / 2, 1u);
                height = std::max(height / 2, 1u);
            }
            return r.id;
        }
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

    void begin_scene(bool world) override {
        if (!world_end_ || color_ != back_buffer_ || world_done_) return;
        if (world) {
            if (!world_seen_) {
                world_seen_ = true;
                if (scene_format_ != VK_FORMAT_UNDEFINED) redirect_to_scene();
            }
            return;
        }
        if (world_seen_) end_world();
    }

    void set_world_end(std::function<void(Image* scene)> world_end) override { world_end_ = std::move(world_end); }
    void set_scene_format(VkFormat scene_format) override { scene_format_ = scene_format; }

    bool end_world() override {
        if (world_done_ || !world_seen_) return world_done_;
        world_done_ = true;
        vk_.end_rendering();
        Image* scene = nullptr;
        if (redirected_) {
            Target& back = targets_[back_buffer_];
            std::swap(back.image, scene_);
            std::swap(back.copy, scene_copy_);
            redirected_ = false;
            scene = &scene_;
        }
        if (world_end_) world_end_(scene);
        return true;
    }

    bool frame_is_flat() const override { return !frame_writes_depth_; }
    uint32_t flat_frames_before() const override { return flat_frames_; }
    uint32_t unsupported_draws() const override { return unsupported_draws_; }

    void save_caches() override { save_pipeline_cache(); }

    void end_frame() override {
        flat_frames_ = frame_is_flat() ? flat_frames_ + 1 : 0;
        if (++frames_ % 60 == 0) free_idle_replacements();
        if (redirected_) {  // presented without finish_picture: the back buffer is the 8-bit image again
            Target& back = targets_[back_buffer_];
            std::swap(back.image, scene_);
            std::swap(back.copy, scene_copy_);
            redirected_ = false;
        }
        world_seen_ = world_done_ = false;
        frame_writes_depth_ = false;
        unsupported_draws_ = 0;
        ++frame_;
        // New pipelines are saved every 30 s or so (a crash or a killed process loses little).
        if (frame_ % 1800 == 0 && pipelines_new_) save_pipeline_cache();
        // Kept vertices unused for a while go.
        if (frame_ % 60 == 0)
            for (auto it = kept_.begin(); it != kept_.end();) {
                if (frame_ - it->second.used_frame > 300) {
                    if (it->second.buffer.buffer)
                        vk_.defer_destroy(it->second.buffer.buffer, it->second.buffer.allocation);
                    it = kept_.erase(it);
                } else {
                    ++it;
                }
            }
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
        for (auto& [key, kept] : kept_)
            if (kept.buffer.buffer) vk_.defer_destroy(kept.buffer.buffer, kept.buffer.allocation);
        kept_.clear();
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
    struct Shader {
        ShaderStage stage;
        VkShaderModule module;
        uint32_t inputs;                     // vertex shaders: the input locations read
        VkPipeline library = VK_NULL_HANDLE;  // pipeline libraries: the compiled stage
        bool shared = false;                 // module and library belong to prebuilt_
    };
    struct PipelineKey {
        uint32_t vs, ps, layout, topology, blend_enable, src, dst, op, write_mask, fill, color_format, depth_format,
            bindings;
        bool operator==(const PipelineKey& o) const { return std::memcmp(this, &o, sizeof *this) == 0; }
    };
    struct SamplerKey {
        uint32_t values[5];
        bool operator==(const SamplerKey& o) const { return std::memcmp(values, o.values, sizeof values) == 0; }
    };
    struct WordHash {
        template <typename T>
        size_t operator()(const T& key) const {
            static_assert(sizeof(T) % 4 == 0);
            uint32_t words[sizeof(T) / 4];
            std::memcpy(words, &key, sizeof(T));
            uint64_t h = 1469598103934665603ull;
            for (uint32_t w : words) h = (h ^ w) * 1099511628211ull;
            return static_cast<size_t>(h ^ (h >> 32));
        }
    };
    struct DrawStream {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0, stride = 0;
    };
    struct Target {
        Format format = Format::BGRA8;
        uint32_t xbox_data = 0;
        uint32_t width = 0, height = 0;  // in title pixels
        uint32_t users = 1;              // create_target calls for this memory
        Image image;
        Image copy;  // what was drawn, for sampling it while it is the target drawn into
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
    // Frames shown, and the frame each replacement id was last bound (indexed by id; kReplacementIdleFrames).
    uint32_t frames_ = 0;
    std::vector<uint32_t> replacement_bound_;
    void free_idle_replacements() {
        std::vector<TextureId> idle;
        for (const auto& [key, r] : replacements_)
            if (r.id < replacement_bound_.size() && frames_ - replacement_bound_[r.id] > kReplacementIdleFrames)
                idle.push_back(r.id);
        for (TextureId id : idle) destroy_texture(id);
    }
    struct VkBuf {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        uint8_t* memory = nullptr;  // host-visible versions
        uint64_t used = 0;          // 1 + the frame number of the last draw reading it; 0 none
    };
    struct Kept {
        VkBuf buffer;
        uint64_t content = 0;
        uint32_t bytes = 0;
        uint64_t used_frame = 0;
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
        target.width = width;
        target.height = height;
        const bool depth = is_depth(format);
        // V1: every colour target is 8-bit BGRA (V3 picks per format).
        const VkFormat vk_format = depth ? vk_.depth_format() : VK_FORMAT_B8G8R8A8_UNORM;
        const VkImageUsageFlags usage =
            depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                  : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!vk_.create_image(target.image, vk_format, surface_width, surface_height, usage)) return 0;
        if (!depth && no_alpha(format)) {
            // Sampled, a target without alpha reads alpha 1 (as D3D9's X8R8G8B8).
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = target.image.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = vk_format;
            view.components.a = VK_COMPONENT_SWIZZLE_ONE;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(vk_.device(), &view, nullptr, &target.image.sample_view);
        }
        const TargetId id = next_id_++;
        targets_.emplace(id, std::move(target));
        return id;
    }

    // The world is drawn into scene_ (scene_format_) from here on: it takes the back buffer target's place (its
    // image and sampling copy swap with scene_'s), with what the frame drew so far copied over.
    void redirect_to_scene() {
        Target& back = targets_[back_buffer_];
        if (scene_ && (scene_.format != scene_format_ || scene_.width != back.image.width ||
                       scene_.height != back.image.height)) {
            vk_.destroy_image(scene_);
            vk_.destroy_image(scene_copy_);
        }
        if (!scene_) {
            if (!vk_.create_image(scene_, scene_format_, back.image.width, back.image.height,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
                LOG_WARN("render", "Vulkan: no HDR scene image; the world is drawn in 8 bits");
                scene_format_ = VK_FORMAT_UNDEFINED;
                return;
            }
            if (no_alpha(back.format)) {  // sampled, it reads alpha 1 as the back buffer does
                VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                view.image = scene_.image;
                view.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view.format = scene_format_;
                view.components.a = VK_COMPONENT_SWIZZLE_ONE;
                view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCreateImageView(vk_.device(), &view, nullptr, &scene_.sample_view);
            }
        }
        vk_.transition(back.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);  // ends rendering
        scene_.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk_.transition(scene_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit region{};
        region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = region.dstOffsets[1] = {static_cast<int32_t>(scene_.width),
                                                       static_cast<int32_t>(scene_.height), 1};
        vkCmdBlitImage(vk_.cmd(), back.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scene_.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
        std::swap(back.image, scene_);
        std::swap(back.copy, scene_copy_);
        redirected_ = true;
    }

    static bool no_alpha(Format f) { return f == Format::BGRX8 || f == Format::BGRX5 || f == Format::B5G6R5; }

    // --- Pipeline cache on disk ---

    // The kept pipelines when the file was written for this GPU and driver (Vulkan's cache header: vendor,
    // device, pipeline cache UUID), else an empty cache.
    void create_pipeline_cache() {
        std::vector<char> data;
        if (!pipeline_cache_path_.empty()) {
            if (std::FILE* f = std::fopen(pipeline_cache_path_.c_str(), "rb")) {
                std::fseek(f, 0, SEEK_END);
                const long size = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                if (size > 0) {
                    data.resize(static_cast<size_t>(size));
                    if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
                }
                std::fclose(f);
            }
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vk_.physical(), &properties);
        VkPipelineCacheHeaderVersionOne header{};
        bool usable = data.size() >= sizeof header;
        if (usable) {
            std::memcpy(&header, data.data(), sizeof header);
            usable = header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                     header.vendorID == properties.vendorID && header.deviceID == properties.deviceID &&
                     std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
            if (!usable)
                LOG_INFO("render", "Vulkan: the kept pipelines are for another GPU or driver; starting afresh");
        }
        VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if (usable) {
            cache.initialDataSize = data.size();
            cache.pInitialData = data.data();
        }
        if (vkCreatePipelineCache(vk_.device(), &cache, nullptr, &pipeline_cache_) != VK_SUCCESS && usable) {
            cache.initialDataSize = 0;
            cache.pInitialData = nullptr;
            vkCreatePipelineCache(vk_.device(), &cache, nullptr, &pipeline_cache_);
        } else if (usable) {
            saved_bytes_ = data.size();
            LOG_INFO("render", "Vulkan: kept pipelines loaded (%zu KB)", data.size() / 1024);
        }
    }

    // Written to a temporary file, then renamed over the old one (never a half-written cache).
    void save_pipeline_cache() {
        if (!pipeline_cache_ || pipeline_cache_path_.empty() || !pipelines_new_) return;
        size_t size = 0;
        if (vkGetPipelineCacheData(vk_.device(), pipeline_cache_, &size, nullptr) != VK_SUCCESS || !size) return;
        std::vector<char> data(size);
        if (vkGetPipelineCacheData(vk_.device(), pipeline_cache_, &size, data.data()) != VK_SUCCESS) return;
        data.resize(size);
        if (size == saved_bytes_) {  // only cache hits since the last save
            pipelines_new_ = 0;
            pipelines_ms_ = pipelines_slowest_ms_ = 0;
            return;
        }
        namespace fs = std::filesystem;
        const fs::path path(pipeline_cache_path_);
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path temporary = path.string() + ".new";
        std::FILE* f = std::fopen(temporary.string().c_str(), "wb");
        if (!f) {
            LOG_WARN("render", "Vulkan: cannot write the pipeline cache %s", temporary.string().c_str());
            pipeline_cache_path_.clear();  // once
            return;
        }
        const bool written = std::fwrite(data.data(), 1, data.size(), f) == data.size();
        std::fclose(f);
        if (written) fs::rename(temporary, path, ec);
        if (!written || ec) {
            fs::remove(temporary, ec);
            return;
        }
        saved_bytes_ = size;
        LOG_INFO("render", "Vulkan: pipeline cache saved (%zu KB; %u pipelines made since, %.0f ms in all, slowest %.0f ms)",
                 data.size() / 1024, pipelines_new_, pipelines_ms_, pipelines_slowest_ms_);
        pipelines_new_ = 0;
        pipelines_ms_ = pipelines_slowest_ms_ = 0;
    }

    // --- Draw objects ---

    void create_draw_objects() {
        VkDescriptorSetLayoutBinding bindings[kVkBindingCount]{};
        for (uint32_t i = 0; i < kVkBindingCount; ++i) {
            VkDescriptorSetLayoutBinding& b = bindings[i];
            b.binding = i;
            b.descriptorCount = 1;
            b.stageFlags = i == kVkVertexConstantsBinding ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            b.descriptorType = i < kVkFirstImageBinding     ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                               : i < kVkFirstSamplerBinding ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                            : VK_DESCRIPTOR_TYPE_SAMPLER;
        }
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        set.bindingCount = kVkBindingCount;
        set.pBindings = bindings;
        vkCreateDescriptorSetLayout(vk_.device(), &set, nullptr, &set_layout_);
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &set_layout_;
        vkCreatePipelineLayout(vk_.device(), &layout, nullptr, &pipeline_layout_);
        create_pipeline_cache();

        // An unbound texture reads opaque black.
        TextureImageDesc dummy;
        if (vk_.create_texture_image(dummy_texture_, dummy)) {
            const uint32_t black = 0xFF000000u;
            vk_.upload_level(dummy_texture_, 0, 0, &black, 4, 1, 4, 0);
        }
        // Shader inputs no stream gives read (0, 0, 0, 1).
        const float defaults[4] = {0, 0, 0, 1};
        if (create_vk_buffer(sizeof defaults, false, default_vertex_))
            vk_.upload_buffer(default_vertex_.buffer, 0, defaults, sizeof defaults);
        // Quads as two triangles each: v0 v1 v2, v0 v2 v3.
        std::vector<uint16_t> quads(kQuadLimit * 6);
        for (uint32_t q = 0; q < kQuadLimit; ++q) {
            const auto v = static_cast<uint16_t>(q * 4);
            const uint16_t quad[6] = {v, static_cast<uint16_t>(v + 1), static_cast<uint16_t>(v + 2), v,
                                      static_cast<uint16_t>(v + 2), static_cast<uint16_t>(v + 3)};
            std::memcpy(&quads[q * 6], quad, sizeof quad);
        }
        if (create_vk_buffer(static_cast<uint32_t>(quads.size() * 2), false, quad_indices_))
            vk_.upload_buffer(quad_indices_.buffer, 0, quads.data(), quads.size() * 2);
        // Vertex formats the title's layouts may use that are optional in Vulkan.
        for (VkFormat f : {VK_FORMAT_R8G8B8A8_USCALED, VK_FORMAT_R16G16_SSCALED, VK_FORMAT_R16G16B16A16_SSCALED}) {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(vk_.physical(), f, &properties);
            if (!(properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
                LOG_WARN("render", "Vulkan: vertex format %d not supported (draws using it will fail)", int(f));
        }
    }

    // --- Streams ---

    VkBuf& buffer_for_draw(Buffer& b) {
        VkBuf& v = b.desc.usage == BufferUsage::Static ? b.statik : b.versions[b.current];
        v.used = vk_.frame_number() + 1;
        return v;
    }

    // The bound vertex buffers of a buffer draw; false without stream 0.
    bool bind_streams() {
        for (uint32_t s = 0; s < kGfxVertexStreams; ++s) {
            draw_streams_[s] = {};
            auto it = buffers_.find(streams_[s].buffer);
            if (it == buffers_.end() || it->second.desc.kind != BufferKind::Vertex) {
                if (s == 0) return false;
                continue;
            }
            draw_streams_[s] = {buffer_for_draw(it->second).buffer, streams_[s].offset, streams_[s].stride};
        }
        return true;
    }

    // draw_user's cache: the vertices of `key` from a kept copy while they stay the same (made the second
    // time the same contents come), else null (streamed).
    const VkBuf* kept_vertices(uint64_t key, const void* vertices, uint32_t bytes) {
        if (!key) return nullptr;
        const uint64_t content = hash_bytes(vertices, bytes);
        Kept& k = kept_[key];
        k.used_frame = frame_;
        if (k.buffer.buffer && k.content == content && k.bytes == bytes) return &k.buffer;
        if (k.content != content || k.bytes != bytes) {
            k.content = content;
            k.bytes = bytes;
            if (k.buffer.buffer) vk_.defer_destroy(k.buffer.buffer, k.buffer.allocation);
            k.buffer = {};
            return nullptr;  // changed: streamed until it repeats
        }
        if (!create_vk_buffer(bytes, false, k.buffer)) return nullptr;
        vk_.upload_buffer(k.buffer.buffer, 0, vertices, bytes);
        return &k.buffer;
    }

    void draw_quads(uint32_t first_vertex, uint32_t vertex_count) {
        if (!quad_indices_.buffer) return;
        vkCmdBindIndexBuffer(vk_.cmd(), quad_indices_.buffer, 0, VK_INDEX_TYPE_UINT16);
        for (uint32_t done = 0; done + 4 <= vertex_count; done += kQuadLimit * 4) {
            const uint32_t quads = std::min(kQuadLimit, (vertex_count - done) / 4);
            vkCmdDrawIndexed(vk_.cmd(), quads * 6, 1, 0, static_cast<int32_t>(first_vertex + done), 0);
        }
    }

    // --- Textures and samplers for a draw ---

    struct Resolved {
        VkImageView view = VK_NULL_HANDLE;
        float scale[2] = {1.0f, 1.0f};
        uint32_t levels = 1;
    };

    // The view behind `id` now (layouts made ready: this may end the current rendering), its texel scale.
    Resolved resolve_texture(TextureId id) {
        Resolved r;
        r.view = dummy_texture_.view;
        auto it = textures_.find(id);
        if (it == textures_.end()) return r;
        Texture& t = it->second;
        if (t.target) {
            auto target = targets_.find(t.target);
            if (target == targets_.end()) return r;
            Target& tg = target->second;
            if (t.desc.texel_coordinates) {
                r.scale[0] = 1.0f / static_cast<float>(tg.width);
                r.scale[1] = 1.0f / static_cast<float>(tg.height);
            }
            if (t.target == color_) {
                // Being drawn into: sample a copy of what is there now (as D3D9Draw::sample_target).
                if (!tg.copy) {
                    TextureImageDesc desc;
                    desc.format = tg.image.format;
                    desc.width = tg.image.width;
                    desc.height = tg.image.height;
                    if (no_alpha(tg.format)) desc.components.a = VK_COMPONENT_SWIZZLE_ONE;
                    if (!vk_.create_texture_image(tg.copy, desc)) return r;
                }
                vk_.transition(tg.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                vk_.transition(tg.copy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkImageCopy copy{};
                copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.extent = {tg.image.width, tg.image.height, 1};
                vkCmdCopyImage(vk_.cmd(), tg.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tg.copy.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                vk_.transition(tg.copy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                r.view = tg.copy.view;
                return r;
            }
            vk_.transition(tg.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            r.view = tg.image.sample_view ? tg.image.sample_view : tg.image.view;
            return r;
        }
        if (!t.image) return r;
        if (t.desc.type != TextureType::Tex2D) {
            static bool once = false;
            warn_once(once, "a cube or volume texture bound for a 2D sampler (drawn without it)");
            return r;
        }
        if (t.desc.texel_coordinates) {
            r.scale[0] = 1.0f / static_cast<float>(t.desc.width);
            r.scale[1] = 1.0f / static_cast<float>(t.desc.height);
        }
        if (t.image.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            vk_.transition(t.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        r.view = t.image.view;
        r.levels = t.image.levels;
        return r;
    }

    VkSampler sampler_for(const SamplerState& s, uint32_t levels, const GraphicsFeatures& features) {
        auto address = [](Address a) {
            switch (a) {
                case Address::Wrap: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                case Address::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                case Address::Clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                case Address::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        };
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = s.mag == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.minFilter = s.min == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = s.mip == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = address(s.u);
        info.addressModeV = address(s.v);
        info.addressModeW = address(s.w);
        // D3D9's MAXMIPLEVEL is the most detailed level used; mipmaps off = that level only.
        info.minLod = static_cast<float>(std::min(s.max_level, levels - 1));
        info.maxLod = s.mip == Filter::None ? info.minLod + 0.25f : VK_LOD_CLAMP_NONE;
        float bias = s.lod_bias;
        if (levels > 1 && features.texture_detail) bias += features.lod_bias;
        const float bias_limit = vk_.limits().maxSamplerLodBias;
        info.mipLodBias = std::clamp(bias, -bias_limit, bias_limit);
        if (anisotropy_ > 1 && features.anisotropic && s.min == Filter::Linear && s.mip != Filter::None &&
            levels > 1) {
            info.anisotropyEnable = VK_TRUE;
            info.maxAnisotropy = static_cast<float>(std::max(std::max<uint32_t>(1, s.anisotropy), anisotropy_));
        }
        switch (s.border) {
            case 0x00000000u: info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; break;
            case 0xFF000000u: info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK; break;
            case 0xFFFFFFFFu: info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; break;
            default: {
                static bool once = false;
                warn_once(once, "a border colour other than black or white (the nearest of those used)");
                info.borderColor = s.border >> 24 < 0x80        ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
                                   : (s.border & 0xFF) >= 0x80 ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                                               : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            }
        }
        SamplerKey key{};
        key.values[0] = info.magFilter | info.minFilter << 4 | info.mipmapMode << 8 | info.addressModeU << 12 |
                        info.addressModeV << 16 | info.addressModeW << 20 | info.anisotropyEnable << 24 |
                        static_cast<uint32_t>(info.borderColor) << 25;
        std::memcpy(&key.values[1], &info.minLod, 4);
        std::memcpy(&key.values[2], &info.maxLod, 4);
        std::memcpy(&key.values[3], &info.mipLodBias, 4);
        std::memcpy(&key.values[4], &info.maxAnisotropy, 4);
        auto it = samplers_.find(key);
        if (it != samplers_.end()) return it->second;
        VkSampler sampler = VK_NULL_HANDLE;
        vkCreateSampler(vk_.device(), &info, nullptr, &sampler);
        samplers_[key] = sampler;
        return sampler;
    }

    // --- Pipelines ---

    // --- Pipeline libraries (Context::pipeline_libraries) ---
    // Each shader is compiled once into a library (vertex shader = pre-rasterization state, pixel shader = fragment
    // shader state); the vertex input (per primitive class) and fragment output (per target formats) libraries are
    // small. A draw's pipeline links the four without link-time optimization, which is fast; blend, write mask,
    // polygon mode and vertex input are set per draw (dynamic_draw_state).

    static const VkDynamicState* library_dynamic_states(uint32_t& count) {
        static const VkDynamicState states[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
            VK_DYNAMIC_STATE_VERTEX_INPUT_EXT, VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
            VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT, VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
            VK_DYNAMIC_STATE_POLYGON_MODE_EXT,
        };
        count = static_cast<uint32_t>(std::size(states));
        return states;
    }

    // One library: `part` = which state; `module` for the shader parts; `topology` for the vertex input part;
    // the formats for the fragment output part.
    VkPipeline create_library(VkGraphicsPipelineLibraryFlagsEXT part, VkShaderModule module,
                              VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                              VkFormat color_format = VK_FORMAT_UNDEFINED, VkFormat depth_format = VK_FORMAT_UNDEFINED) {
        VkGraphicsPipelineLibraryCreateInfoEXT library{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT};
        library.flags = part;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        library.pNext = &rendering;
        if (part == VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachmentFormats = &color_format;
            rendering.depthAttachmentFormat = rendering.stencilAttachmentFormat = depth_format;
        }
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = topology;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = kColorAll;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.pDynamicStates = library_dynamic_states(dynamic.dynamicStateCount);
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.module = module;
        stage.pName = "main";

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &library;
        info.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
        info.pDynamicState = &dynamic;
        switch (part) {
        case VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT:
            info.pVertexInputState = &vertex;
            info.pInputAssemblyState = &assembly;
            break;
        case VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT:
            stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            info.stageCount = 1;
            info.pStages = &stage;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.layout = pipeline_layout_;
            break;
        case VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT:
            stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            info.stageCount = 1;
            info.pStages = &stage;
            info.pMultisampleState = &multisample;
            info.pDepthStencilState = &depth;
            info.layout = pipeline_layout_;
            break;
        default:  // fragment output
            info.pMultisampleState = &multisample;
            info.pColorBlendState = &blend;
            break;
        }
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (VkResult r = vkCreateGraphicsPipelines(vk_.device(), pipeline_cache_, 1, &info, nullptr, &pipeline);
            r != VK_SUCCESS) {
            LOG_ERROR("render", "gfx: Vulkan pipeline library: %s", result_name(r));
            return VK_NULL_HANDLE;
        }
        return pipeline;
    }

    VkPipeline shader_library(ShaderStage stage, VkShaderModule module) {
        return create_library(stage == ShaderStage::Vertex ? VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT
                                                           : VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT,
                              module);
    }

    void release_shader(Shader& shader) {
        if (shader.shared) return;
        if (shader.library) vk_.defer_destroy(shader.library);
        vkDestroyShaderModule(vk_.device(), shader.module, nullptr);
    }

    // Every title shader compiled on worker threads from the device's creation on (while the logos and movies
    // play); create_shader waits for them.
    void start_prebuild() {
        prebuild_results_.assign(kTitleShaderCount, Shader{});
        prebuild_start_ = std::chrono::steady_clock::now();
        const unsigned threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
        auto next = std::make_shared<std::atomic<unsigned>>(0);
        for (unsigned t = 0; t < threads; ++t)
            prebuild_threads_.emplace_back([this, next] {
                for (unsigned i = (*next)++; i < kTitleShaderCount; i = (*next)++) {
                    const TitleShader& title = kTitleShaders[i];
                    std::string error;
                    const std::vector<uint32_t> words =
                        spirv_fixed_layout(title.d3d9, title.d3d9_bytes, title.spirv, title.spirv_bytes, &error);
                    if (words.empty()) continue;
                    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                    info.codeSize = words.size() * 4;
                    info.pCode = words.data();
                    VkShaderModule module = VK_NULL_HANDLE;
                    if (vkCreateShaderModule(vk_.device(), &info, nullptr, &module) != VK_SUCCESS) continue;
                    const ShaderStage stage = title.stage == 'v' ? ShaderStage::Vertex : ShaderStage::Pixel;
                    prebuild_results_[i] = {stage, module, stage == ShaderStage::Vertex ? spirv_input_mask(words) : 0,
                                            shader_library(stage, module)};
                }
            });
    }

    void join_prebuild() {
        if (prebuild_threads_.empty()) return;
        for (std::thread& t : prebuild_threads_) t.join();
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prebuild_start_).count();
        uint32_t built = 0;
        for (unsigned i = 0; i < kTitleShaderCount; ++i) {
            Shader& shader = prebuild_results_[i];
            if (!shader.module) continue;
            if (!prebuilt_.emplace(kTitleShaders[i].spirv, shader).second) {  // the same code twice
                if (shader.library) vk_.defer_destroy(shader.library);
                vkDestroyShaderModule(vk_.device(), shader.module, nullptr);
                continue;
            }
            ++built;
        }
        LOG_INFO("render", "Vulkan: %u title shaders compiled in %.0f ms (%zu threads)", built, ms,
                 prebuild_threads_.size());
        prebuild_threads_.clear();
        prebuild_results_.clear();
        ++pipelines_new_;  // save the cache with them
    }

    VkPipeline stage_library(VkGraphicsPipelineLibraryFlagsEXT part, uint32_t a, uint32_t b = 0) {
        const uint64_t key = static_cast<uint64_t>(part) << 56 ^ static_cast<uint64_t>(a) << 28 ^ b;
        auto found = stage_libraries_.find(key);
        if (found != stage_libraries_.end()) return found->second;
        const VkPipeline library =
            part == VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT
                ? create_library(part, VK_NULL_HANDLE, static_cast<VkPrimitiveTopology>(a))
                : create_library(part, VK_NULL_HANDLE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, static_cast<VkFormat>(a),
                                 static_cast<VkFormat>(b));
        stage_libraries_[key] = library;
        return library;
    }

    VkPipeline link_pipeline(const PipelineKey& key, const Shader& vs, const Shader& ps) {
        const VkPipeline libraries[4] = {
            stage_library(VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT, key.topology),
            vs.library, ps.library,
            stage_library(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT, key.color_format,
                          key.depth_format)};
        for (VkPipeline library : libraries)
            if (!library) return pipelines_[key] = VK_NULL_HANDLE;
        VkPipelineLibraryCreateInfoKHR linked{VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
        linked.libraryCount = 4;
        linked.pLibraries = libraries;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &linked;
        info.layout = pipeline_layout_;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const auto start = std::chrono::steady_clock::now();
        if (VkResult r = vkCreateGraphicsPipelines(vk_.device(), pipeline_cache_, 1, &info, nullptr, &pipeline);
            r != VK_SUCCESS) {
            LOG_ERROR("render", "gfx: Vulkan pipeline link: %s", result_name(r));
            pipeline = VK_NULL_HANDLE;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ++pipelines_new_;
        pipelines_ms_ += ms;
        pipelines_slowest_ms_ = std::max(pipelines_slowest_ms_, ms);
        if (ms >= 50.0) LOG_INFO("render", "Vulkan: linking a pipeline took %.0f ms", ms);
        return pipelines_[key] = pipeline;
    }

    // What whole pipelines keep in their key, set per draw: vertex input (bindings with their strides, the layout's
    // attributes, (0, 0, 0, 1) for the rest), blending, write mask and polygon mode.
    void dynamic_draw_state(VkCommandBuffer cmd, uint32_t bindings_used, const VkDeviceSize* strides, const Shader& vs,
                            const std::vector<VertexAttribute>& attributes, VkBlendFactor src, VkBlendFactor dst,
                            VkBlendOp op, bool blend, uint32_t write_mask, VkPolygonMode fill) {
        VkVertexInputBindingDescription2EXT bindings[kGfxVertexStreams + 1];
        VkVertexInputAttributeDescription2EXT inputs[kGfxVertexInputs];
        uint32_t binding_count = 0, input_count = 0, given = 0;
        for (uint32_t s = 0; s <= kGfxVertexStreams; ++s)
            if (bindings_used & (1u << s)) {
                VkVertexInputBindingDescription2EXT& d = bindings[binding_count++];
                d = {VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT};
                d.binding = s;
                d.stride = static_cast<uint32_t>(strides[s]);
                d.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                d.divisor = 1;
            }
        auto add = [&](uint32_t location, uint32_t binding, VkFormat format, uint32_t offset) {
            VkVertexInputAttributeDescription2EXT& d = inputs[input_count++];
            d = {VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT};
            d.location = location;
            d.binding = binding;
            d.format = format;
            d.offset = offset;
        };
        for (const VertexAttribute& a : attributes) {
            if (given & (1u << a.input)) continue;
            given |= 1u << a.input;
            add(a.input, a.stream, attribute_format(a.type), a.offset);
        }
        for (uint32_t i = 0; i < kGfxVertexInputs; ++i)
            if ((vs.inputs & (1u << i)) && !(given & (1u << i)))
                add(i, kDefaultBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
        vkCmdSetVertexInputEXT(cmd, binding_count, bindings, input_count, inputs);
        const VkBool32 enable = blend;
        vkCmdSetColorBlendEnableEXT(cmd, 0, 1, &enable);
        const VkColorBlendEquationEXT equation{src, dst, op, src, dst, op};
        vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &equation);
        const VkColorComponentFlags mask = write_mask;
        vkCmdSetColorWriteMaskEXT(cmd, 0, 1, &mask);
        vkCmdSetPolygonModeEXT(cmd, fill);
    }

    VkPipeline pipeline_for(const PipelineKey& key, const Shader& vs, const Shader& ps,
                            const std::vector<VertexAttribute>& attributes) {
        auto found = pipelines_.find(key);
        if (found != pipelines_.end()) return found->second;
        if (vk_.pipeline_libraries()) return link_pipeline(key, vs, ps);
        VkVertexInputBindingDescription bindings[kGfxVertexStreams + 1];
        VkVertexInputAttributeDescription inputs[kGfxVertexInputs];
        uint32_t binding_count = 0, input_count = 0, given = 0;
        for (uint32_t s = 0; s <= kGfxVertexStreams; ++s)
            if (key.bindings & (1u << s)) bindings[binding_count++] = {s, 0, VK_VERTEX_INPUT_RATE_VERTEX};
        for (const VertexAttribute& a : attributes) {
            if (given & (1u << a.input)) continue;
            given |= 1u << a.input;
            inputs[input_count++] = {a.input, a.stream, attribute_format(a.type), a.offset};
        }
        for (uint32_t i = 0; i < kGfxVertexInputs; ++i)
            if ((vs.inputs & (1u << i)) && !(given & (1u << i)))
                inputs[input_count++] = {i, kDefaultBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = binding_count;
        vertex.pVertexBindingDescriptions = bindings;
        vertex.vertexAttributeDescriptionCount = input_count;
        vertex.pVertexAttributeDescriptions = inputs;
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = static_cast<VkPrimitiveTopology>(key.topology);
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = static_cast<VkPolygonMode>(key.fill);
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = key.blend_enable;
        attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = static_cast<VkBlendFactor>(key.src);
        attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = static_cast<VkBlendFactor>(key.dst);
        attachment.colorBlendOp = attachment.alphaBlendOp = static_cast<VkBlendOp>(key.op);
        attachment.colorWriteMask = key.write_mask;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
        };
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states));
        dynamic.pDynamicStates = dynamic_states;
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs.module;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = ps.module;
        stages[1].pName = "main";
        const VkFormat color_format = static_cast<VkFormat>(key.color_format);
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &color_format;
        rendering.depthAttachmentFormat = static_cast<VkFormat>(key.depth_format);
        rendering.stencilAttachmentFormat = static_cast<VkFormat>(key.depth_format);
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipeline_layout_;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const auto start = std::chrono::steady_clock::now();
        if (VkResult r = vkCreateGraphicsPipelines(vk_.device(), pipeline_cache_, 1, &info, nullptr, &pipeline);
            r != VK_SUCCESS) {
            LOG_ERROR("render", "gfx: Vulkan pipeline: %s", result_name(r));
            pipeline = VK_NULL_HANDLE;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ++pipelines_new_;
        pipelines_ms_ += ms;
        pipelines_slowest_ms_ = std::max(pipelines_slowest_ms_, ms);
        if (ms >= 50.0) LOG_INFO("render", "Vulkan: a new pipeline took %.0f ms (not in the pipeline cache yet)", ms);
        pipelines_[key] = pipeline;
        return pipeline;
    }

    // Everything but stream 0 of draw_user and the index buffers, for a draw now; false if it cannot draw.
    // Records the pipeline, descriptors, dynamic state and vertex buffers into the current rendering (begun
    // here).
    bool prepare(Primitive primitive, const float* range = nullptr, bool user = false, uint32_t user_stride = 0) {
        begin_draw();
        auto vs = shaders_.find(vertex_shader_);
        auto ps = shaders_.find(pixel_shader_);
        auto color = targets_.find(color_);
        if (vs == shaders_.end() || ps == shaders_.end() || layout_ == 0 || layout_ > layouts_.size() ||
            color == targets_.end()) {
            static bool once = false;
            warn_once(once, "draw without shaders, vertex layout or target (skipped)");
            return false;
        }
        const Target& target = color->second;
        const std::vector<VertexAttribute>& attributes = layouts_[layout_ - 1];

        // Viewport, scissor and screen to clip (draw_placement.h).
        PlacementInput in;
        in.width = target.width;
        in.height = target.height;
        in.surface_width = target.image.width;
        in.surface_height = target.image.height;
        in.viewport = viewport_;
        in.scissor = scissor_on_ ? &scissor_ : nullptr;
        const bool full_picture = target.width == back_width_ && target.height == back_height_;
        in.narrow = ui_narrowing(narrow_ui_draw_, range, frame_is_flat(), full_picture, back_width_);
        in.range = range;
        in.full_picture = full_picture;
        in.vulkan = true;
        const Placement place = place_draw(in);
        float* reserved = vs_constants_.data() + kGfxVsScreenToClip * 4;
        std::memcpy(reserved, place.screen_to_clip, sizeof(place.screen_to_clip));
        reserved[8] = fog_.bias;
        reserved[9] = fog_.scale;
        reserved[10] = static_cast<float>(fog_.mode);
        reserved[11] = 0.0f;

        // Textures (may end the current rendering: layouts, copies of the target drawn into).
        const GraphicsFeatures features = active_graphics_features();
        VkDescriptorImageInfo images[kGfxTextureSlots]{}, samplers[kGfxTextureSlots]{};
        for (uint32_t slot = 0; slot < kGfxTextureSlots; ++slot) {
            const Resolved r = resolve_texture(bound_textures_[slot]);
            float* c = ps_constants_.data() + (kGfxPsTexcoordScale + slot) * 4;
            c[0] = r.scale[0];
            c[1] = r.scale[1];
            c[2] = c[3] = 1.0f;
            images[slot] = {VK_NULL_HANDLE, r.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            samplers[slot].sampler = sampler_for(sampler_states_[slot], r.levels, features);
        }
        unpack_color(fog_.color, ps_constants_.data() + kGfxPsFogColor * 4);
        for (uint32_t slot = 0; slot < 4; ++slot)
            ps_constants_[kGfxPsAlphaKill * 4 + slot] = sampler_states_[slot].alpha_kill ? 1.0f : 0.0f;
        const BlendState& b = blend_;
        ps_constants_[kGfxPsAlphaTest * 4] = static_cast<float>(b.alpha_test ? b.alpha_func : Compare::Always);
        ps_constants_[kGfxPsAlphaTest * 4 + 1] = static_cast<float>(b.alpha_ref);

        // Constants: both blocks in one piece of the upload space (one piece cannot lose the other).
        const VkDeviceSize align = std::max<VkDeviceSize>(16, vk_.limits().minUniformBufferOffsetAlignment);
        const VkDeviceSize vs_bytes = kVkVertexConstants * 16, ps_bytes = kVkPixelConstants * 16;
        const VkDeviceSize ps_at = (vs_bytes + align - 1) / align * align;
        const Staged constants = vk_.stage(ps_at + ps_bytes, align);
        if (!constants.memory) return false;
        std::memcpy(constants.memory, vs_constants_.data(), vs_bytes);
        std::memcpy(static_cast<uint8_t*>(constants.memory) + ps_at, ps_constants_.data(), ps_bytes);

        // The pipeline.
        const Target* depth_target = depth_ ? &targets_[depth_] : nullptr;
        const bool x_target = no_alpha(target.format);
        auto dest_alpha = [&](Blend f) {  // a target without alpha has alpha 1 (D3D9)
            if (!x_target) return f;
            return f == Blend::DestAlpha ? Blend::One : f == Blend::InvDestAlpha ? Blend::Zero : f;
        };
        const VkPrimitiveTopology top = topology(primitive);
        PipelineKey key{};
        key.vs = vertex_shader_;
        key.ps = pixel_shader_;
        key.layout = layout_;
        key.topology = top == VK_PRIMITIVE_TOPOLOGY_POINT_LIST    ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                       : top <= VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                 : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        key.blend_enable = b.enable;
        key.src = b.enable ? blend_factor(dest_alpha(b.src)) : VK_BLEND_FACTOR_ONE;
        key.dst = b.enable ? blend_factor(dest_alpha(b.dst)) : VK_BLEND_FACTOR_ZERO;
        key.op = b.enable ? blend_op(b.op) : VK_BLEND_OP_ADD;
        key.write_mask = b.write_mask & kColorAll;  // R G B A bits as Vulkan's
        key.fill = raster_.fill == Fill::Wireframe && vk_.features().fillModeNonSolid ? VK_POLYGON_MODE_LINE
                   : raster_.fill == Fill::Point && vk_.features().fillModeNonSolid   ? VK_POLYGON_MODE_POINT
                                                                                      : VK_POLYGON_MODE_FILL;
        key.color_format = target.image.format;
        key.depth_format = depth_target ? depth_target->image.format : VK_FORMAT_UNDEFINED;
        uint32_t given = 0;
        for (const VertexAttribute& a : attributes) {
            key.bindings |= 1u << a.stream;
            given |= 1u << a.input;
        }
        if (vs->second.inputs & ~given) key.bindings |= 1u << kDefaultBinding;
        if (user) key.bindings |= 1u;  // draw_user binds stream 0
        const uint32_t bindings_used = key.bindings;
        if (vk_.pipeline_libraries()) {
            // Set per draw instead (dynamic_draw_state): the pipeline is the shaders, primitive class and formats.
            key.layout = key.blend_enable = key.src = key.dst = key.op = key.write_mask = key.fill = key.bindings = 0;
        }
        const VkPipeline pipeline = pipeline_for(key, vs->second, ps->second, attributes);
        if (!pipeline) return false;
        if (raster_.flat_shading) {
            static bool once = false;
            warn_once(once, "flat shading is not supported yet (drawn smooth)");
        }

        // Record.
        if (!vk_.begin_rendering()) return false;
        VkCommandBuffer cmd = vk_.cmd();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDescriptorBufferInfo buffers[2] = {{constants.buffer, constants.offset, vs_bytes},
                                             {constants.buffer, constants.offset + ps_at, ps_bytes}};
        VkWriteDescriptorSet writes[kVkBindingCount]{};
        for (uint32_t i = 0; i < kVkBindingCount; ++i) {
            VkWriteDescriptorSet& w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstBinding = i;
            w.descriptorCount = 1;
            if (i < kVkFirstImageBinding) {
                w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &buffers[i];
            } else if (i < kVkFirstSamplerBinding) {
                w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                w.pImageInfo = &images[i - kVkFirstImageBinding];
            } else {
                w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                w.pImageInfo = &samplers[i - kVkFirstSamplerBinding];
            }
        }
        vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, kVkBindingCount, writes);

        const VkViewport viewport{static_cast<float>(place.x),     static_cast<float>(place.y),
                                  static_cast<float>(place.width), static_cast<float>(place.height),
                                  place.min_z,                     place.max_z};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor;
        scissor.offset = {std::max(0, place.left), std::max(0, place.top)};
        scissor.extent = {static_cast<uint32_t>(std::max(0, place.right - scissor.offset.x)),
                          static_cast<uint32_t>(std::max(0, place.bottom - scissor.offset.y))};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        float blend_constants[4];
        unpack_color(b.constant, blend_constants);
        vkCmdSetBlendConstants(cmd, blend_constants);
        vkCmdSetPrimitiveTopology(cmd, top);
        const RasterState& r = raster_;
        vkCmdSetCullMode(cmd, r.cull == Cull::None ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
        // Vulkan's winding is the one seen on screen (y down), as D3D9's: the dropped one is the back.
        vkCmdSetFrontFace(cmd, r.cull == Cull::Clockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
        const bool bias = r.depth_bias != 0.0f || r.slope_bias != 0.0f;
        vkCmdSetDepthBiasEnable(cmd, bias);
        if (bias) vkCmdSetDepthBias(cmd, r.depth_bias, 0.0f, r.slope_bias);
        const DepthStencilState& ds = depth_stencil_;
        const bool has_depth = depth_target != nullptr;
        vkCmdSetDepthTestEnable(cmd, ds.depth_test && has_depth);
        vkCmdSetDepthWriteEnable(cmd, ds.depth_write && has_depth);
        vkCmdSetDepthCompareOp(cmd, compare_op(ds.depth_func));
        if (ds.depth_test && ds.depth_write && has_depth) frame_writes_depth_ = true;
        vkCmdSetStencilTestEnable(cmd, ds.stencil && has_depth);
        vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, stencil_op(ds.stencil_fail), stencil_op(ds.stencil_pass),
                          stencil_op(ds.stencil_depth_fail), compare_op(ds.stencil_func));
        vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ds.stencil_read_mask);
        vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ds.stencil_write_mask);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ds.stencil_ref);

        // Vertex buffers: the streams (draw_user binds its stream 0 itself), the default for the rest.
        VkBuffer vbs[kGfxVertexStreams + 1];
        VkDeviceSize offsets[kGfxVertexStreams + 1], strides[kGfxVertexStreams + 1];
        uint32_t first = UINT32_MAX, last = 0;
        for (uint32_t s = 0; s <= kGfxVertexStreams; ++s) {
            DrawStream stream = s == kDefaultBinding || user ? DrawStream{} : draw_streams_[s];
            if (!stream.buffer) stream = {default_vertex_.buffer, 0, 0};
            vbs[s] = stream.buffer;
            offsets[s] = stream.offset;
            strides[s] = stream.stride;
            if (key.bindings & (1u << s)) first = std::min(first, s), last = s;
        }
        if (vk_.pipeline_libraries()) {
            if (user) strides[0] = user_stride;
            for (uint32_t s = 0; s <= kGfxVertexStreams; ++s)
                if (bindings_used & (1u << s)) first = std::min(first, s), last = std::max(last, s);
            dynamic_draw_state(cmd, bindings_used, strides, vs->second, attributes,
                               b.enable ? blend_factor(dest_alpha(b.src)) : VK_BLEND_FACTOR_ONE,
                               b.enable ? blend_factor(dest_alpha(b.dst)) : VK_BLEND_FACTOR_ZERO,
                               b.enable ? blend_op(b.op) : VK_BLEND_OP_ADD, b.enable, b.write_mask & kColorAll,
                               raster_.fill == Fill::Wireframe && vk_.features().fillModeNonSolid ? VK_POLYGON_MODE_LINE
                               : raster_.fill == Fill::Point && vk_.features().fillModeNonSolid
                                   ? VK_POLYGON_MODE_POINT
                                   : VK_POLYGON_MODE_FILL);
        }
        if (first != UINT32_MAX)
            vkCmdBindVertexBuffers2(cmd, first, last - first + 1, vbs + first, offsets + first, nullptr,
                                    vk_.pipeline_libraries() ? nullptr : strides + first);
        return true;
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
    std::unordered_map<ShaderId, Shader> shaders_;
    std::unordered_map<const void*, Shader> prebuilt_;   // pipeline libraries: every title shader, by its SPIR-V
    std::vector<std::thread> prebuild_threads_;
    std::vector<Shader> prebuild_results_;
    std::chrono::steady_clock::time_point prebuild_start_;
    std::unordered_map<uint64_t, VkPipeline> stage_libraries_;  // vertex input / fragment output libraries
    std::vector<std::vector<VertexAttribute>> layouts_;  // LayoutId - 1
    std::unordered_map<PipelineKey, VkPipeline, WordHash> pipelines_;
    std::unordered_map<SamplerKey, VkSampler, WordHash> samplers_;
    DrawStream draw_streams_[kGfxVertexStreams];
    std::unordered_map<uint64_t, Kept> kept_;  // draw_user's cache_key
    uint64_t frame_ = 0;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::string pipeline_cache_path_;
    uint32_t pipelines_new_ = 0;  // made since the last save (cache hits too)
    size_t saved_bytes_ = 0;      // the cache data's size when it was last loaded or saved
    double pipelines_ms_ = 0, pipelines_slowest_ms_ = 0;
    Image dummy_texture_;
    VkBuf default_vertex_, quad_indices_;
    SamplerState sampler_states_[kGfxTextureSlots];
    ShaderId vertex_shader_ = 0, pixel_shader_ = 0;
    LayoutId layout_ = 0;
    std::vector<float> vs_constants_ = std::vector<float>(kVkVertexConstants * 4);
    std::vector<float> ps_constants_ = std::vector<float>(kVkPixelConstants * 4);
    UiNarrowing (*narrow_ui_draw_)() = nullptr;
    uint32_t back_width_ = 0, back_height_ = 0;
    uint32_t anisotropy_ = 1;
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
    // The world / HUD split (render graph, 0.5b).
    std::function<void(Image*)> world_end_;
    VkFormat scene_format_ = VK_FORMAT_UNDEFINED;
    Image scene_, scene_copy_;  // the HDR scene (and its sampling copy) while not standing in for the back buffer
    bool world_seen_ = false, world_done_ = false, redirected_ = false;
    uint32_t flat_frames_ = 0;
    uint32_t unsupported_draws_ = 0;
};

}  // namespace

std::unique_ptr<VulkanGfxDevice> create_vulkan_gfx_device(Context& context, const BackendConfig& config) {
    return std::make_unique<Device>(context, config);
}

}  // namespace render::vk
