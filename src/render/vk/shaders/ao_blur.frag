#version 450
// Depth-aware blur of the AO image along one axis (p3.yz), 9 taps; samples across a depth edge are left out.
#extension GL_GOOGLE_include_directive : require
#include "ao_common.glsl"

void main() {
    const float d = distance_at(uv);
    const vec2 axis = params.p3.yz * params.p2.xy;
    float sum = 0.0, weight = 0.0;
    for (int i = -4; i <= 4; ++i) {
        const vec2 at = uv + axis * float(i);
        const float di = distance_at(at);
        const float w = exp(-float(i * i) / 18.0) * (abs(di - d) < 0.05 * d + 4.0 ? 1.0 : 0.0);
        sum += textureLod(ao_map, at, 0.0).r * w;
        weight += w;
    }
    color = vec4(weight > 0.0 ? sum / weight : 1.0);
}
