// The lit model classes on our own shaders (engine improvements 2.1): included by the GLSL that
// tools/shader_glsl.py generates from the title's programs (model_vs_*.vert, model_ps_*.frag).
// Layout as the title's (vk/spirv_layout.h): binding 0 = vertex constants (c0..c191, screen_to_clip c192 c193,
// fog c194, then ours from kGfxVsOwn = c195: gfx_device.h ModelOwn), binding 1 = pixel constants, t<n> at
// binding 2 + n, s<n> at 10 + n; interstage COLOR0 0, COLOR1 1, TEXCOORD<n> 2 + n, FOG 10, ours 11 (world
// position) and 12 (world normal).
// Our constants (own[i], see gfx_device.h kModelOwn*):
//   0: (pixel lighting 0/1, the sun's light index or -1, sun shadow 0/1, debug view)
//   1..3: object -> world rows        4..7: world -> projector rows (u, v, z, w)
//   8: shadow depth (scale, offset, bias, strength) as sun_world.glsl's c0      9, 10: sun mask rows (c1, c2)
//   11: (light count, normal offset in world units, specular, surface maps bound 0/1)
//   12..19: light i (xyz, radius): radius 0 = directional, xyz its direction of travel; else the position
//   20..27: light i (r, g, b, inner radius)
// Textures: t4 = the sun mask (L8), t5 = the sun's shadow map (packed depth, sun_common.glsl), t6 / t7 = the
// surface maps of t0's texture (normal; roughness + height), sampled at t0's coordinates.

#define OWN_COUNT 32

#ifdef OWN_VERTEX
layout(set = 0, binding = 0) uniform VertexConstants {
    vec4 c[192];
    vec4 screen_to_clip[2];
    vec4 fog_params;
    vec4 own[OWN_COUNT];
};
layout(location = 0) out vec4 o_d0;
layout(location = 1) out vec4 o_d1;
layout(location = 2) out vec4 o_t0;
layout(location = 3) out vec4 o_t1;
layout(location = 4) out vec4 o_t2;
layout(location = 5) out vec4 o_t3;
layout(location = 10) out float o_fog;
layout(location = 11) out vec3 o_world_pos;
layout(location = 12) out vec3 o_world_normal;

vec4 nv_exp(float s) { return vec4(exp2(floor(s)), fract(s), exp2(s), 1.0); }
vec4 nv_log(float s) {
    const float l = log2(abs(s));
    const float e = floor(l);
    return vec4(e, abs(s) * exp2(-e), l, 1.0);
}
vec4 nv_rcc(float s) { return vec4(clamp(1.0 / s, -1.884467e19, 1.884467e19)); }
vec4 nv_dst(vec4 a, vec4 b) { return vec4(1.0, a.y * b.y, a.z, b.w); }
vec4 nv_normpacked3(vec4 b) {
    const float x = b.x + (b.y - 8.0 * floor(b.y / 8.0)) * 256.0;
    const float y = floor(b.y / 8.0) + (b.z - 64.0 * floor(b.z / 64.0)) * 32.0;
    const float z = floor(b.z / 64.0) + b.w * 4.0;
    return vec4(max((x >= 1024.0 ? x - 2048.0 : x) / 1023.0, -1.0),
                max((y >= 1024.0 ? y - 2048.0 : y) / 1023.0, -1.0),
                max((z >= 512.0 ? z - 1024.0 : z) / 511.0, -1.0), 1.0);
}

void own_model_world(vec3 p, vec3 n) {
    const vec4 q = vec4(p, 1.0);
    o_world_pos = vec3(dot(own[1], q), dot(own[2], q), dot(own[3], q));
    o_world_normal = vec3(dot(own[1].xyz, n), dot(own[2].xyz, n), dot(own[3].xyz, n));
}

// As nv2a.hlsli's nv_epilogue.
void nv_epilogue(vec4 pos, vec4 d0, vec4 d1, vec4 t0, vec4 t1, vec4 t2, vec4 t3, float fog) {
    const float w = pos.w == 0.0 ? 1.0 : pos.w;
    gl_Position = vec4((pos.xyz * screen_to_clip[0].xyz + screen_to_clip[1].xyz) * w, w);
    o_d0 = d0;
    o_d1 = d1;
    o_t0 = t0;
    o_t1 = t1;
    o_t2 = t2;
    o_t3 = t3;
    const float d = fog * fog_params.y;
    if (fog_params.z == 1.0)
        o_fog = fog_params.x + d - 1.0;
    else if (fog_params.z == 2.0)
        o_fog = fog_params.x + exp2(16.0 * d) - 1.5;
    else if (fog_params.z == 3.0)
        o_fog = fog_params.x + exp2(-32.0 * d * d) - 1.5;
    else
        o_fog = 1.0;
}
#endif

#ifdef OWN_PIXEL
layout(set = 0, binding = 0) uniform VertexConstants {
    vec4 c[192];
    vec4 screen_to_clip[2];
    vec4 fog_params;
    vec4 own[OWN_COUNT];
} vsc;
layout(set = 0, binding = 1) uniform PixelConstants {
    vec4 c[18];
    vec4 texcoord_scale[8];
    vec4 fog_color;
    vec4 alpha_kill;
    vec4 alpha_test;
};
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D tex1;
layout(set = 0, binding = 6) uniform texture2D tex4;
layout(set = 0, binding = 7) uniform texture2D tex5;
layout(set = 0, binding = 10) uniform sampler smp0;
layout(set = 0, binding = 11) uniform sampler smp1;
layout(set = 0, binding = 14) uniform sampler smp4;
layout(set = 0, binding = 15) uniform sampler smp5;
layout(set = 0, binding = 8) uniform texture2D tex6;  // surface maps (2.2): normal X Y (DirectX green)
layout(set = 0, binding = 9) uniform texture2D tex7;  // R roughness, G height
layout(set = 0, binding = 16) uniform sampler smp6;
layout(set = 0, binding = 17) uniform sampler smp7;
layout(location = 0) in vec4 v0;
layout(location = 1) in vec4 v1;
layout(location = 2) in vec4 t0;
layout(location = 3) in vec4 t1;
layout(location = 10) in float fog;
layout(location = 11) in vec3 world_pos;
layout(location = 12) in vec3 world_normal;
layout(location = 0) out vec4 color;

#define OWN(i) vsc.own[i]

vec4 own_tex2Dproj_0(vec4 coord) { return textureProj(sampler2D(tex0, smp0), coord); }
vec4 own_tex2Dproj_1(vec4 coord) { return textureProj(sampler2D(tex1, smp1), coord); }

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }
vec4 saturate(vec4 x) { return clamp(x, 0.0, 1.0); }
void clip(vec4 x) {
    if (any(lessThan(x, vec4(0.0)))) discard;
}
#define unsigned_identity(x) max(x, 0.0)
#define unsigned_invert(x) (1.0 - saturate(x))
#define expand_normal(x) (2.0 * max(x, 0.0) - 1.0)
#define expand_negate(x) (-2.0 * max(x, 0.0) + 1.0)
#define halfbias_normal(x) (max(x, 0.0) - 0.5)
#define halfbias_negate(x) (-max(x, 0.0) + 0.5)

vec4 nv_alpha_test(vec4 result) {
    const float f = alpha_test.x;
    if (f != 7.0) {
        const float a = round(result.w * 255.0), r = alpha_test.y;
        const bool lt = a < r, eq = a == r, gt = a > r;
        const bool ok = (f == 1.0 && lt) || (f == 2.0 && eq) || (f == 3.0 && (lt || eq)) || (f == 4.0 && gt) ||
                        (f == 5.0 && !eq) || (f == 6.0 && (gt || eq));
        if (!ok) discard;
    }
    return result;
}

float own_unpack_depth(vec3 rgb) { return dot(round(rgb * 255.0) / 255.0, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)); }

// Whether the baked lighting has the sun at projector point p (0..1), as sun_world.glsl's baked_sun.
float own_baked_sun(vec3 p) {
    if (OWN(9) == vec4(0.0)) return 1.0;
    const vec2 g = vec2(dot(OWN(9), vec4(p, 1.0)), dot(OWN(10), vec4(p, 1.0)));
    if (g.x < 0.0 || g.y < 0.0 || g.x > 1.0 || g.y > 1.0) return 1.0;
    return smoothstep(0.15, 0.6, texture(sampler2D(tex4, smp4), g).r);
}

// The share of the sun this point loses (0..1): sun_world.glsl's tent PCF over the shadow map.
float g_mask = 1.0;
float own_sun_lost(vec3 world) {
    const vec4 q = vec4(world, 1.0);
    const vec4 coord = vec4(dot(OWN(4), q), dot(OWN(5), q), dot(OWN(6), q), dot(OWN(7), q));
    const vec3 p = coord.xyz / coord.w;
    if (p.x <= 0.0 || p.y <= 0.0 || p.x >= 1.0 || p.y >= 1.0) return 0.0;
    g_mask = own_baked_sun(p);
    if (g_mask <= 0.0) return 0.0;
    const float receiver = (p.z + OWN(8).y) * OWN(8).x - OWN(8).z;
    const ivec2 size = textureSize(sampler2D(tex5, smp5), 0);
    const vec2 pos = p.xy * vec2(size) - 0.5;
    const ivec2 base = ivec2(floor(pos));
    const vec2 f = pos - floor(pos);
    const vec4 wx = vec4(1.0 - f.x, 1.0, 1.0, f.x) / 3.0, wy = vec4(1.0 - f.y, 1.0, 1.0, f.y) / 3.0;
    float sum = 0.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const ivec2 at = clamp(base + ivec2(i - 1, j - 1), ivec2(0), size - 1);
            const float e = own_unpack_depth(texelFetch(sampler2D(tex5, smp5), at, 0).rgb);
            if (e > 0.0 && 1.0 - e < receiver) sum += wx[i] * wy[j];
        }
    return sum * g_mask * OWN(8).w;
}

// Light i's colour and direction toward it at world point p.
vec3 own_light(int i, vec3 p, out vec3 toward) {
    const vec4 a = OWN(12 + i), b = OWN(20 + i);
    if (a.w == 0.0) {
        toward = -a.xyz;
        return b.rgb;
    }
    // nglEvalPointLight per pixel: linear falloff from the inner radius to the radius.
    const vec3 d = p - a.xyz;
    const float dist = length(d);
    if (dist < 1e-3) {
        toward = vec3(0.0, -1.0, 0.0);
        return b.rgb;
    }
    toward = -d / dist;
    return b.rgb * clamp(1.0 - (dist - b.w) / (a.w - b.w), 0.0, 1.0);
}

vec3 g_light = vec3(1.0);
float g_lost = 0.0;
int g_lights = 0;
vec3 g_normal = vec3(0.0, 0.0, 1.0);

// The diffuse colour the title's combiners take as V0.
vec4 own_model_v0(vec4 vertex_color) {
    g_light = vertex_color.rgb;
    g_normal = normalize(world_normal);
    const bool pixel = OWN(0).x != 0.0, shadow = OWN(0).z != 0.0;
    const int sun = int(OWN(0).y);
    g_lights = int(OWN(11).x);
    if (!pixel && !shadow) return vertex_color;
    vec3 n = g_normal;
    if (pixel) {
        vec3 sum = vsc.c[17].rgb;  // the ambient
        for (int i = 0; i < g_lights && i < 8; ++i) {
            vec3 toward;
            const vec3 light = own_light(i, world_pos, toward);
            sum += light * max(dot(n, toward), 0.0);
        }
        g_light = sum;
    }
    if (shadow && sun >= 0) {
        vec3 toward;
        const vec3 sun_color = own_light(sun, world_pos, toward);
        const float facing = dot(n, toward);
        // Normal offset: the receiver moved off its surface (more where the sun grazes it), so the model's own
        // coarse caster copy does not shadow its lit side.
        const float offset = OWN(11).y * (1.0 + 2.0 * (1.0 - clamp(facing, 0.0, 1.0)));
        g_lost = facing > 0.0 ? own_sun_lost(world_pos + n * offset) : 0.0;
        g_light = max(g_light - sun_color * max(facing, 0.0) * g_lost, vec3(0.0));
    }
    return vec4(g_light, vertex_color.w);
}

bool own_has_maps() { return OWN(11).w != 0.0; }
vec4 own_surface() {
    return own_has_maps() ? textureProj(sampler2D(tex7, smp7), t0 * texcoord_scale[0]) : vec4(0.0);
}

void own_model_out(vec4 result) {
    color = result;
    const int view = int(OWN(0).w + 0.5);
    if (view == 5 || view == 8) {  // roughness, height: grey; magenta where the texture has no maps
        const vec4 s = own_surface();
        color = vec4(own_has_maps() ? vec3(view == 5 ? s.r : s.g) : vec3(1.0, 0.0, 1.0), result.w);
        return;
    }
    if (view == 1) color = vec4(clamp(g_light, 0.0, 1.0), result.w);
    else if (view == 2) color = vec4(g_mask < 0.01 ? vec3(0.45, 0.05, 0.05) : vec3(1.0 - g_lost), result.w);
    else if (view == 3) color = vec4(vec3(g_mask), result.w);
    else if (view == 4) color = vec4(0.5 + 0.5 * g_normal, result.w);
    else if (view == 6) {
        const vec3 k[5] = vec3[](vec3(0.0), vec3(0.1, 0.2, 1.0), vec3(0.1, 0.9, 0.2), vec3(1.0, 0.9, 0.1),
                                 vec3(1.0, 0.1, 0.1));
        color = vec4(k[clamp(g_lights, 0, 4)], result.w);
    }
}
#endif
