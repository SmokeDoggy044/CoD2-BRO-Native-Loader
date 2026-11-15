// Ambient occlusion (engine improvements 2.6, vk_ao.cpp): shared by ao.frag, ao_blur.frag, ao_apply.frag.
// Set 0 (push descriptors): binding 0 = the world's depth (depth aspect, nearest), binding 1 = the AO image
// (linear). Push constants:
//   p0 = the projection's (a, b, c, e): depth buffer z = (a d + b) / (c d + e) in units of 16777215 for distance d
//   p1 = (tan of the half field of view across, down, radius in world units, strength)
//   p2 = (texel of the image drawn into: 1 / width, 1 / height, fade start, fade end)
//   p3 = (0, blur axis x, y, frame noise)
layout(set = 0, binding = 0) uniform sampler2D depth_map;
layout(set = 0, binding = 1) uniform sampler2D ao_map;
layout(push_constant) uniform Params {
    vec4 p0, p1, p2, p3;
} params;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

const float kSkyDepth = 0.999999;  // cleared / sky
const float kNearCut = 24.0;       // closer than this: the view model (left out)
float distance_at(vec2 at) {
    const float depth = textureLod(depth_map, at, 0.0).r;
    if (depth >= kSkyDepth) return 1e9;
    const float z = depth * 16777215.0;
    return (params.p0.y - z * params.p0.w) / (z * params.p0.z - params.p0.x);
}
// View space: x right, y down, z forward (distance along the view axis).
vec3 view_at(vec2 at, float d) { return vec3((at * 2.0 - 1.0) * params.p1.xy * d, d); }
