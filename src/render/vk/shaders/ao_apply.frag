#version 450
// The AO onto the HDR scene (multiply blend; the debug view writes it alone): strength p1.w.
#extension GL_GOOGLE_include_directive : require
#include "ao_common.glsl"

void main() {
    // The view model (drawn after the level's depth was kept) gets no AO: it would show the level's behind it.
    if (params.p3.x > 0.5 && textureLod(overlay_depth, uv, 0.0).r < kSkyDepth) {
        color = vec4(1.0);
        return;
    }
    const float ao = textureLod(ao_map, uv, 0.0).r;
    const float k = mix(1.0, ao, params.p1.w);
    color = vec4(k, k, k, 1.0);
}
