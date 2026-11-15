#version 450
// The AO onto the HDR scene (multiply blend; the debug view writes it alone): strength p1.w.
#extension GL_GOOGLE_include_directive : require
#include "ao_common.glsl"

void main() {
    const float ao = textureLod(ao_map, uv, 0.0).r;
    const float k = mix(1.0, ao, params.p1.w);
    color = vec4(k, k, k, 1.0);
}
