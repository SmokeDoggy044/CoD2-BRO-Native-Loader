#pragma once

// The draw path's front end: turns the title's vertex data (inline primitives from
// pushbuffer data, vertex streams) into float4 attributes and back end primitives.

#include <cstddef>
#include <cstdint>

#include "types.h"

namespace render {

// Hooks the NV2A interpreter's inline primitives; call once.
void install_draw_handlers();

// Sets the NV2A registers and D3D render states D3D's CreateDevice would have
// initialized.
void reset_draw_state(bool depth_buffer);

// D3DPT_* / NV2A primitive types.
constexpr uint32_t kPrimPoints = 1, kPrimLines = 2, kPrimLineLoop = 3, kPrimLineStrip = 4, kPrimTriangles = 5,
                   kPrimTriangleStrip = 6, kPrimTriangleFan = 7, kPrimQuads = 8, kPrimQuadStrip = 9, kPrimPolygon = 10;

// Draws not drawn at all so far because they need something not translated yet
// (fixed-function vertex processing, an unknown primitive type). Diagnostics.
uint32_t skipped_draws();

// Draws `vertex_count` vertices from the current vertex streams (DrawVertices).
// `indices` (16-bit, may be null) select them (DrawIndexedVertices).
void draw_streams(uint32_t primitive, uint32_t start_vertex, uint32_t vertex_count, const uint16_t* indices);

// Draws vertices from one block of memory with the given stride (DrawVerticesUP).
void draw_user_pointer(uint32_t primitive, uint32_t vertex_count, const void* data, uint32_t stride);

}  // namespace render
