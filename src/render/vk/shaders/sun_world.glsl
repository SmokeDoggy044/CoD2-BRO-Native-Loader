// cdWorld / cdWorldBlend with the sun's shadow map (engine improvements 2.0 step 2): the title's projected
// variants (src/render/shaders ps_008a0190, ps_008a0370, ps_0089f180, ps_0089f360) with the projector's
// silhouette replaced by a depth compare, filtered over 4x4 texels (soft edges).
// Defines: BLEND (cdWorldBlend), LIGHTMAP, PROJ_TEX / PROJ_SMP / PROJ_COORD (the projector's slot and its
// coordinates: the vertex program writes them to TEXCOORD2 for cdWorld, TEXCOORD3 for cdWorldBlend).
// c0: (depth scale, depth offset, bias, strength): the receiver's caster-space depth = (z + offset) * scale.
// c3.x: the debug view (render/dev_mode.h DebugView): lighting, shadow and sun-mask are ours here.
// c1, c2: the projector coordinates (u, v, z, 1) to the sun mask's (t4: the light grid's sun weight around the
// view, see tr_main.cpp grid_sun_mask); both zero: no mask. Where the baked lighting has no sun, no shadow.
#extension GL_GOOGLE_include_directive : require
#include "sun_common.glsl"

vec4 sample_stage(texture2D t, sampler s, vec4 coord, int n, float kill) {
    vec4 v = textureProj(sampler2D(t, s), coord * ps.texcoord_scale[n]);
    if (kill != 0.0 && v.w < 1.0 / 512.0) discard;
    return v;
}

layout(set = 0, binding = 6) uniform texture2D tex4;
layout(set = 0, binding = 14) uniform sampler smp4;

// Whether the baked lighting has the sun here (0..1).
float baked_sun(vec3 p) {
    if (ps.c[1] == vec4(0.0)) return 1.0;
    const vec2 g = vec2(dot(ps.c[1], vec4(p, 1.0)), dot(ps.c[2], vec4(p, 1.0)));
    if (g.x < 0.0 || g.y < 0.0 || g.x > 1.0 || g.y > 1.0) return 1.0;
    return smoothstep(0.15, 0.6, texture(sampler2D(tex4, smp4), g).r);
}

// How much of the sun this point loses (0..1).
float sun_shadow(vec4 coord) {
    const vec3 p = coord.xyz / coord.w;
    if (p.x <= 0.0 || p.y <= 0.0 || p.x >= 1.0 || p.y >= 1.0) return 0.0;
    const float sun = baked_sun(p);
    if (sun <= 0.0) return 0.0;
    const float receiver = (p.z + ps.c[0].y) * ps.c[0].x - ps.c[0].z;
    const ivec2 size = textureSize(sampler2D(PROJ_TEX, PROJ_SMP), 0);
    const vec2 pos = p.xy * vec2(size) - 0.5;
    const ivec2 base = ivec2(floor(pos));
    const vec2 f = pos - floor(pos);
    // A tent over 3x3 texels as 4x4 taps: weights (1 - f, 1, 1, f) / 3 per axis.
    const vec4 wx = vec4(1.0 - f.x, 1.0, 1.0, f.x) / 3.0, wy = vec4(1.0 - f.y, 1.0, 1.0, f.y) / 3.0;
    float sum = 0.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const ivec2 at = clamp(base + ivec2(i - 1, j - 1), ivec2(0), size - 1);
            const float e = unpack_depth(texelFetch(sampler2D(PROJ_TEX, PROJ_SMP), at, 0).rgb);
            // e = 1 - the caster's depth; 0 = none.
            if (e > 0.0 && 1.0 - e < receiver) sum += wx[i] * wy[j];
        }
    return sum * sun;
}

void main() {
    const float fog_factor = clamp(fog, 0.0, 1.0);
    const vec4 T0 = sample_stage(tex0, smp0, t0, 0, ps.alpha_kill.x);
    const vec4 T1 = sample_stage(tex1, smp1, t1, 1, ps.alpha_kill.y);
#if defined(BLEND) && defined(LIGHTMAP)
    const vec4 T2 = sample_stage(tex2, smp2, t2, 2, ps.alpha_kill.z);
#endif
    const float lost = sun_shadow(PROJ_COORD) * ps.c[0].w;
#ifdef BLEND
    vec3 rgb = clamp(max(v0.www, 0.0) * T1.rgb + T0.rgb * (1.0 - clamp(v0.www, 0.0, 1.0)), -1.0, 1.0);
#ifdef LIGHTMAP
    rgb = clamp(rgb * T2.rgb, -1.0, 1.0);
#endif
    const float strength = clamp(lost * v0.z, -1.0, 1.0);
    rgb = clamp(rgb * (1.0 - clamp(strength, 0.0, 1.0)), -1.0, 1.0);
    const vec4 result = clamp(vec4(rgb, T0.w), 0.0, 1.0);
#else
#ifdef LIGHTMAP
    vec3 rgb = clamp(T0.rgb * T1.rgb, -1.0, 1.0);
#else
    vec3 rgb = T0.rgb;
#endif
    const float strength = clamp(lost * v0.w, -1.0, 1.0);
    rgb = clamp(rgb * (1.0 - clamp(strength, 0.0, 1.0)), -1.0, 1.0);
    const vec3 fogged = mix(max(ps.fog_color.rgb, 0.0), max(rgb, 0.0), fog_factor);
    const vec4 result = clamp(vec4(fogged, max(T0.w, 0.0)), 0.0, 1.0);
#endif
    alpha_test(result.w);
    color = result;
    // Debug views (phase 2 tools), unfogged: 1 lighting = the light only (white textures), 2 shadow = the
    // shadow's factor (white full sun; dark red: no sun in the baked lighting), 3 sun-mask = the baked-sun weight.
    const int view = int(ps.c[3].x + 0.5);
    if (view >= 1 && view <= 3) {
        const vec3 q = PROJ_COORD.xyz / PROJ_COORD.w;
        const bool inside = q.x > 0.0 && q.y > 0.0 && q.x < 1.0 && q.y < 1.0;
        const float weight = inside ? baked_sun(q) : 1.0;
#ifdef BLEND
        const float lit = 1.0 - clamp(lost * v0.z, 0.0, 1.0);
#ifdef LIGHTMAP
        const vec3 light = T2.rgb;
#else
        const vec3 light = vec3(1.0);
#endif
#else
        const float lit = 1.0 - clamp(lost * v0.w, 0.0, 1.0);
#ifdef LIGHTMAP
        const vec3 light = T1.rgb;
#else
        const vec3 light = vec3(1.0);
#endif
#endif
        vec3 shown;
        if (view == 1) shown = light * lit;
        else if (view == 2) shown = weight < 0.01 ? vec3(0.45, 0.05, 0.05) : vec3(lit);
        else shown = vec3(weight);
        color = vec4(clamp(shown, 0.0, 1.0), result.w);
    }
}
