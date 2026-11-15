#version 450
// Horizon-based ambient occlusion at half resolution (2.6): 3 directions (both ways) x 5 steps within `radius` world
// units,
// the horizon's sine above the surface weighted by distance
// falloff. 1 = open. The normal comes from neighbouring depths (the smaller difference per axis). Rotation per
// pixel from a 4x4 pattern (no temporal filter in the engine: the pattern stays put).
#extension GL_GOOGLE_include_directive : require
#include "ao_common.glsl"

void main() {
    const float d = distance_at(uv);
    if (d > 1e8 || d < kNearCut || d > params.p2.w) {
        color = vec4(1.0);
        return;
    }
    const vec3 p = view_at(uv, d);
    const vec2 texel = params.p2.xy;
    const vec3 right = view_at(uv + vec2(texel.x, 0.0), distance_at(uv + vec2(texel.x, 0.0))) - p;
    const vec3 left = p - view_at(uv - vec2(texel.x, 0.0), distance_at(uv - vec2(texel.x, 0.0)));
    const vec3 down = view_at(uv + vec2(0.0, texel.y), distance_at(uv + vec2(0.0, texel.y))) - p;
    const vec3 up = p - view_at(uv - vec2(0.0, texel.y), distance_at(uv - vec2(0.0, texel.y)));
    const vec3 dx = dot(right, right) < dot(left, left) ? right : left;
    const vec3 dy = dot(down, down) < dot(up, up) ? down : up;
    vec3 n = normalize(cross(dy, dx));
    if (dot(n, p) > 0.0) n = -n;  // toward the camera

    const float radius = params.p1.z;
    // The radius in this image's pixels: radius / d over the half-height's tangent.
    const float radius_px = radius / d * 0.5 / (params.p1.y * texel.y);
    if (radius_px < 1.0) {
        color = vec4(1.0);
        return;
    }
    const float step_px = min(radius_px, 48.0) / 5.0;
    // A 4x4 ordered rotation (period 4 pixels): the blur's 9 taps per axis average it out completely.
    const ivec2 cell = ivec2(gl_FragCoord.xy) & 3;
    const float noise = (float((cell.x * 4 + cell.y * 9) & 15) + 0.5) / 16.0;
    float occlusion = 0.0;
    for (int i = 0; i < 3; ++i) {
        const float angle = (float(i) + noise) * 1.047197551;  // 3 directions over 180 degrees, both ways below
        const vec2 dir = vec2(cos(angle), sin(angle));
        for (int side = 0; side < 2; ++side) {
            const vec2 sdir = side == 0 ? dir : -dir;
            float horizon = 0.0;
            for (int s = 1; s <= 5; ++s) {
                const vec2 at = uv + sdir * (float(s) - 0.5 + noise * 0.5) * step_px * texel;
                if (at.x < 0.0 || at.y < 0.0 || at.x > 1.0 || at.y > 1.0) break;
                const vec3 v = view_at(at, distance_at(at)) - p;
                const float l2 = dot(v, v);
                if (l2 > radius * radius || l2 < 1e-4) continue;
                const float h = (dot(v, n) * inversesqrt(l2) - 0.1) * (1.0 - l2 / (radius * radius));
                horizon = max(horizon, h);
            }
            occlusion += horizon;
        }
    }
    occlusion /= 6.0;
    // Fade out toward the far end (fog) and right at the near cut-off (the view model's neighbourhood).
    const float fade = 1.0 - smoothstep(params.p2.z, params.p2.w, d);
    color = vec4(1.0 - clamp(occlusion * 1.6, 0.0, 1.0) * fade);
}
