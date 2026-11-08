#pragma once

// GfxDevice on Direct3D 9: the device, render targets and texture cache of the Xbox D3D path's
// back end (D3D9Draw), so both draw into the same frame. Draws set their whole state each time
// (the Xbox path does too), so neither depends on what the other left set.

#include <memory>

#include "backend.h"
#include "d3d9_api.h"
#include "gfx_device.h"

namespace render {

class D3D9Draw;

class D3D9GfxDevice : public GfxDevice {
public:
    virtual ~D3D9GfxDevice() = default;
    // Called once a frame was presented.
    virtual void end_frame() = 0;
};

// `config`: the back buffer and the device's depth buffer.
std::unique_ptr<D3D9GfxDevice> create_d3d9_gfx_device(IDirect3DDevice9* device, D3D9Draw& draw,
                                                      const BackendConfig& config);

}  // namespace render
