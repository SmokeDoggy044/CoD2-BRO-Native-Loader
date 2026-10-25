#pragma once

// The D3D9 back end's draw translation: Xbox vertex programs, register combiners,
// textures and raster state -> D3D9 objects and states, cached across draws.

#include <cstdint>
#include <memory>

#include "backend.h"
#include "d3d9_api.h"
#include "shader_state.h"

namespace render {

class D3D9Draw {
public:
    // `config`: the back buffer and the device's depth buffer (the D3D9 device's own).
    D3D9Draw(IDirect3DDevice9* device, const BackendConfig& config);
    ~D3D9Draw();
    D3D9Draw(const D3D9Draw&) = delete;
    D3D9Draw& operator=(const D3D9Draw&) = delete;

    void draw(const DrawInput& input);
    void end_frame();  // textures are checked for CPU changes once per frame

    // Render targets: D3D9 render target textures by Xbox memory address. False if the
    // target cannot be created (draws then go nowhere).
    bool set_render_target(const SurfaceDesc& color, const SurfaceDesc* depth);
    void forget_render_target(uint32_t data);
    // The surface drawn into for `data` (null if none yet).
    IDirect3DSurface9* target_surface(uint32_t data);
    IDirect3DTexture9* target_texture(uint32_t data);
    bool has_depth() const { return has_depth_; }
    void dump_next_frame() {
        dump_frame_ = frame_ + 1;
        dump_index_ = 0;
    }
    // Diagnostics: draws that need something not translated yet (a texture mode, cube
    // or volume texture, texture format) are drawn solid magenta while this is on.
    void set_highlight(bool on) { highlight_ = on; }
    // Such draws in the current frame so far (counted with highlighting off too).
    uint32_t unsupported_draws() const { return unsupported_draws_; }
    // Flat frames: no draw writes depth, so no 3D scene (menus, loading screens,
    // movies; their 2D draws may leave the depth test on). Whether the current frame
    // is one so far, and how many came before it.
    bool frame_is_flat() const { return !frame_writes_depth_; }
    uint32_t flat_frames_before() const { return flat_frames_; }

private:
    struct Caches;
    struct CachedTexture;
    struct Target;
    // The texture bound to a stage as a D3D9 texture (created or updated), or null.
    CachedTexture* texture_for_stage(uint32_t stage);
    // Makes `entry` hold `replacement` (BackendConfig::replace_texture), with a full
    // mipmap chain if `mipmaps`. False if the texture cannot be created.
    bool upload_replacement(CachedTexture& entry, const TextureReplacement& replacement, bool mipmaps);
    bool magenta_shader();  // creates magenta_ once; false if that failed
    // Draws `input` from vertex_buffer_/index_buffer_ (filled front to back, NOOVERWRITE,
    // DISCARD on wrap). False (nothing drawn) if they cannot be made or locked.
    bool stream_draw(const DrawInput& input, D3DPRIMITIVETYPE type, uint32_t primitives, uint32_t stride);
    // How the draw, UI laid out for a narrower shape, is narrowed, or anchor -1 if it is
    // not (BackendConfig::narrow_ui_draw).
    UiNarrowing narrow_ui(const DrawInput& input, uint32_t width, uint32_t height) const;
    void dump_draw(const DrawInput& input, const XbPixelShaderDef& def, uint64_t vs_key, uint32_t primitives);

    IDirect3DDevice9* device_;
    uint32_t depth_data_;  // the device's depth buffer's Xbox address
    uint32_t anisotropy_;  // BackendConfig::anisotropy, within the device's limit
    float render_scale_x_, render_scale_y_;  // BackendConfig::render_scale_x(), render_scale
    uint32_t back_width_, back_height_;      // the title's back buffer size
    UiNarrowing (*narrow_ui_draw_)();         // BackendConfig::narrow_ui_draw
    decltype(BackendConfig::replace_texture) replace_texture_;
    decltype(BackendConfig::replacement_version) replacement_version_;
    // The current target: its size as the title sees it and the D3D9 surface's.
    uint32_t target_data_ = 0, target_width_ = 0, target_height_ = 0;
    uint32_t target_surface_width_ = 0, target_surface_height_ = 0;
    bool has_depth_ = false;
    bool target_ok_ = false;
    uint32_t frame_ = 1;
    uint32_t dump_frame_ = 0;  // XB_DUMP_FRAME / XB_DUMP_STOP, see dump_draw
    uint32_t dump_stop_ = UINT32_MAX;
    uint32_t dump_index_ = 0;
    bool highlight_ = false;
    uint32_t unsupported_draws_ = 0;
    bool frame_writes_depth_ = false;
    uint32_t flat_frames_ = 0;
    IDirect3DPixelShader9* magenta_ = nullptr;
    // DrawPrimitiveUP copied every draw's vertices into the runtime's own buffer, with
    // a lock and often a flush each; on Windows' D3D9 with an Intel HD 3000 that was a
    // quarter of the frame (issue #8). The draws fill these instead.
    IDirect3DVertexBuffer9* vertex_buffer_ = nullptr;
    IDirect3DIndexBuffer9* index_buffer_ = nullptr;
    uint32_t vertex_buffer_size_ = 0, vertex_offset_ = 0;
    uint32_t index_buffer_size_ = 0, index_offset_ = 0;
    bool streams_failed_ = false;  // creating or locking them failed: DrawPrimitiveUP
    std::unique_ptr<Caches> caches_;
};

}  // namespace render
