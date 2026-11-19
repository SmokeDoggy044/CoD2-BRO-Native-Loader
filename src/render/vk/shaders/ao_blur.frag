#version 450
// Depth-aware blur of the AO image along one axis (p3.yz), 9 taps; samples across a depth edge are left out. The
// edge test follows the surface's slope along the axis: on a floor seen at a low angle the distance changes fast up
// the screen, and a test against the centre's distance alone left out nearly every tap there, so the vertical pass
// did nothing and the AO's 4x4 pattern stayed as horizontal streaks (#29).
#extension GL_GOOGLE_include_directive : require
#include "ao_common.glsl"

void main() {
    const float d = distance_at(uv);
    const vec2 axis = params.p3.yz * params.p2.xy;
    // The slope: the smaller of the two one-sided differences (a step to one side is an edge, not the surface).
    const float ahead = distance_at(uv + axis) - d, behind = d - distance_at(uv - axis);
    const float slope = abs(ahead) < abs(behind) ? ahead : behind;
    float sum = 0.0, weight = 0.0;
    for (int i = -4; i <= 4; ++i) {
        const vec2 at = uv + axis * float(i);
        const float di = distance_at(at);
        const float expected = d + slope * float(i);
        const float w = exp(-float(i * i) / 18.0) * (abs(di - expected) < 0.05 * d + 4.0 ? 1.0 : 0.0);
        sum += textureLod(ao_map, at, 0.0).r * w;
        weight += w;
    }
    color = vec4(weight > 0.0 ? sum / weight : 1.0);
}
