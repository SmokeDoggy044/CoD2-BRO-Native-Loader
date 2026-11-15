// The lit model classes on our own shaders (engine improvements 2.1): included by the GLSL that
// tools/shader_glsl.py generates from the title's programs (model_vs_*.vert, model_ps_*.frag).
// Layout as the title's (vk/spirv_layout.h): binding 0 = vertex constants (c0..c191, screen_to_clip c192 c193,
// fog c194, then ours from kGfxVsOwn = c195: gfx_device.h ModelOwn), binding 1 = pixel constants, t<n> at
// binding 2 + n, s<n> at 10 + n; interstage COLOR0 0, COLOR1 1, TEXCOORD<n> 2 + n, FOG 10, ours 11 (world
// position) and 12 (world normal).
// Our constants (own[i], see gfx_device.h kModelOwn*):
//   0: (pixel lighting 0/1, the sun's light index or -1, sun shadow 0/1, debug view)
//   1..3: object -> world rows        4..7: world -> projector rows (u, v, z, w)
//   8: shadow depth (scale, offset, bias, strength)  9, 10: sun mask rows (c1, c2)
//   11: (light count, normal offset in world units, specular, surface maps bound 0/1)
//   12..19: light i (xyz, radius): radius 0 = directional, xyz its direction of travel; else the position
//   20..27: light i (r, g, b, inner radius)
//   28: (normal map strength, specular strength, cavity strength, 0): 0 = off (per pixel with surface maps only)
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
#ifdef OWN_DLIGHT
layout(location = 13) out vec4 o_base_uv;  // the surface's own texture coordinates (layers 0, 1) - 2.5
#endif

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
layout(set = 0, binding = 4) uniform texture2D tex2;
layout(set = 0, binding = 5) uniform texture2D tex3;
layout(set = 0, binding = 6) uniform texture2D tex4;
layout(set = 0, binding = 7) uniform texture2D tex5;
layout(set = 0, binding = 10) uniform sampler smp0;
layout(set = 0, binding = 11) uniform sampler smp1;
layout(set = 0, binding = 12) uniform sampler smp2;
layout(set = 0, binding = 13) uniform sampler smp3;
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
layout(location = 4) in vec4 t2;
layout(location = 5) in vec4 t3;
layout(location = 10) in float fog;
layout(location = 11) in vec3 world_pos;
layout(location = 12) in vec3 world_normal;
#ifdef OWN_DLIGHT
layout(location = 13) in vec4 base_uv;
#endif
layout(location = 0) out vec4 color;

#define OWN(i) vsc.own[i]

vec4 own_tex2Dproj_0(vec4 coord) { return textureProj(sampler2D(tex0, smp0), coord); }
vec4 own_tex2Dproj_1(vec4 coord) { return textureProj(sampler2D(tex1, smp1), coord); }
vec4 own_tex2Dproj_2(vec4 coord) { return textureProj(sampler2D(tex2, smp2), coord); }
vec4 own_tex2Dproj_3(vec4 coord) { return textureProj(sampler2D(tex3, smp3), coord); }

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

// Whether the baked lighting has the sun at projector point p (0..1).
float own_baked_sun(vec3 p) {
    if (OWN(9) == vec4(0.0)) return 1.0;
    const vec2 g = vec2(dot(OWN(9), vec4(p, 1.0)), dot(OWN(10), vec4(p, 1.0)));
    if (g.x < 0.0 || g.y < 0.0 || g.x > 1.0 || g.y > 1.0) return 1.0;
    return smoothstep(0.15, 0.6, texture(sampler2D(tex4, smp4), g).r);
}

// The share of the sun projector point p loses (0..1): the shadow map (packed depth) in `map`, a tent PCF over 4x4
// texels (soft edges), times the baked-sun mask and the strength (own[8].w).
float g_mask = 1.0;
vec3 g_spec = vec3(0.0);
vec3 g_light = vec3(1.0);
float g_lost = 0.0;
int g_lights = 0;
vec3 g_normal = vec3(0.0, 0.0, 1.0);
float own_shadow_pcf(texture2D map, sampler smp, vec3 p) {
    if (p.x <= 0.0 || p.y <= 0.0 || p.x >= 1.0 || p.y >= 1.0) return 0.0;
    g_mask = own_baked_sun(p);
    if (g_mask <= 0.0) return 0.0;
    const float receiver = (p.z + OWN(8).y) * OWN(8).x - OWN(8).z;
    const ivec2 size = textureSize(sampler2D(map, smp), 0);
    const vec2 pos = p.xy * vec2(size) - 0.5;
    const ivec2 base = ivec2(floor(pos));
    const vec2 f = pos - floor(pos);
    const vec4 wx = vec4(1.0 - f.x, 1.0, 1.0, f.x) / 3.0, wy = vec4(1.0 - f.y, 1.0, 1.0, f.y) / 3.0;
    float sum = 0.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const ivec2 at = clamp(base + ivec2(i - 1, j - 1), ivec2(0), size - 1);
            const float e = own_unpack_depth(texelFetch(sampler2D(map, smp), at, 0).rgb);
            if (e > 0.0 && 1.0 - e < receiver) sum += wx[i] * wy[j];
        }
    return sum * g_mask * OWN(8).w;
}

// A model point's lost share: its projector coordinates from our world -> projector rows (own[4..7]), map on t5.
float own_sun_lost(vec3 world) {
    const vec4 q = vec4(world, 1.0);
    const vec4 coord = vec4(dot(OWN(4), q), dot(OWN(5), q), dot(OWN(6), q), dot(OWN(7), q));
    return own_shadow_pcf(tex5, smp5, coord.xyz / coord.w);
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

bool own_has_maps() { return OWN(11).w != 0.0; }
vec4 own_surface() {
    return own_has_maps() ? textureProj(sampler2D(tex7, smp7), t0 * texcoord_scale[0]) : vec4(0.0);
}

// The normal map's normal in world space (2.3): tangent frame per pixel from the position and texture coordinate
// derivatives ("cotangent frame", no vertex tangents); the maps' X follows +u, Y +v (D3D rows; checked against
// their height maps). Called in uniform control flow (the derivatives).
vec3 own_mapped_normal_at(vec3 n, texture2D map, sampler smp, vec4 coord, vec4 coord_scale) {
    const vec2 uv = coord.xy / coord.w * coord_scale.xy;
    // Less the map's average tilt (its last mip level): PBRify's maps lean slightly as a whole, which darkened the
    // sunlit level by ~4% (lab m2 18640, median ratio 0.96); the map should add detail, not tilt the surface.
    const vec2 mean = textureLod(sampler2D(map, smp), uv, 16.0).rg * 2.0 - 1.0;
    const vec2 xy = textureProj(sampler2D(map, smp), coord * coord_scale).rg * 2.0 - 1.0 - mean;
    const vec3 ts = vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy))));
    const vec3 dp1 = dFdx(world_pos), dp2 = dFdy(world_pos);
    const vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    const vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    const vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    const vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    const float scale = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (!(scale > 1e-20)) return n;  // no texture gradient (a flat-mapped or degenerate triangle)
    const float inv = inversesqrt(scale);
    return normalize(mat3(tangent * inv, bitangent * inv, n) * ts);
}
// Cavity (2.6): the height map below its neighbourhood (a level 2.5 mips coarser) shades grooves and seams;
// own[28].z = strength. 1 = no change.
float own_cavity(texture2D map, sampler smp, vec2 uv) {
    if (OWN(28).z <= 0.0) return 1.0;
    const float lod = textureQueryLod(sampler2D(map, smp), uv).x;
    const float h = textureLod(sampler2D(map, smp), uv, lod).g;
    const float around = textureLod(sampler2D(map, smp), uv, lod + 2.5).g;
    return clamp(1.0 - OWN(28).z * max(around - h, 0.0) * 3.0, 0.6, 1.0);
}

vec3 own_mapped_normal(vec3 n) {
    return normalize(mix(n, own_mapped_normal_at(n, tex6, smp6, t0, texcoord_scale[0]), OWN(28).x));
}

#ifdef OWN_WORLD
// The level (2.4): the lightmap is the light. Where the baked lighting has the sun (the level's sun mask at the
// point, own[29] own[30]: world -> mask rows), the normal map only re-weights the sun's share by how much more or
// less the bumped normal faces the sun than the face does:
//   light x (1 + sun_weight x (max(N_map.L, 0) / (max(N_face.L, 0.2) max(N_map.N_face, 0.5)) - 1)), within 0.6 .. 1.4,
// so a flat map (N_map = N_face) leaves the picture exactly as it was. own[31] = (sun direction of travel, valid);
// own[12] = the camera (world) to orient the face normal. Indoors / baked shade: unchanged (2.6's cavity there).
float g_world_factor = 1.0;
float g_cavity = 1.0;
float own_level_sun(vec3 p) {
    if (OWN(29) == vec4(0.0)) return 0.0;
    const vec2 g = vec2(dot(OWN(29), vec4(p, 1.0)), dot(OWN(30), vec4(p, 1.0)));
    if (g.x < 0.0 || g.y < 0.0 || g.x > 1.0 || g.y > 1.0) return 0.0;
    return smoothstep(0.15, 0.6, texture(sampler2D(tex4, smp4), g).r);
}
void own_world_begin() {
    // Face normal from the position derivatives, turned toward the camera.
    vec3 face = cross(dFdx(world_pos), dFdy(world_pos));
    face = dot(face, face) > 1e-20 ? normalize(face) : vec3(0.0, 0.0, 1.0);
    if (dot(face, OWN(12).xyz - world_pos) < 0.0) face = -face;
    g_normal = face;
    if (OWN(28).x <= 0.0 || !own_has_maps()) return;
    vec3 n = own_mapped_normal_at(face, tex6, smp6, t0, texcoord_scale[0]);
#ifdef OWN_WORLD_BLEND
    // The two layers' normals blended as their colours (v0.w); t7 = the second layer's normal map.
    const vec3 n1 = own_mapped_normal_at(face, tex7, smp7, t1, texcoord_scale[1]);
    n = normalize(mix(n, n1, clamp(v0.w, 0.0, 1.0)));
#endif
    n = normalize(mix(face, n, OWN(28).x));
    g_normal = n;
#ifndef OWN_WORLD_BLEND
    g_cavity = own_cavity(tex7, smp7, t0.xy / t0.w * texcoord_scale[0].xy);  // t7 = this layer's surface map
#endif
    if (OWN(31).w == 0.0) return;
    const vec3 toward = -OWN(31).xyz;
    // Only faces the sun reaches: the mask is a map over x y, so walls turned away from the sun carry the ground's
    // weight too, and their lightmap has no sun to re-weight (lab m0 8512: -6.7 levels before this).
    const float weight = own_level_sun(world_pos) * smoothstep(0.0, 0.17, dot(face, toward));
    if (weight <= 0.0) return;
    // Over N_map.N_face too: a bump tilted away from the face loses no light when the sun stands along the face
    // normal, so the map re-distributes the light instead of darkening on average (lab m0 8512: -6.7 levels without).
    const float ratio = max(dot(n, toward), 0.0) / (max(dot(face, toward), 0.2) * max(dot(n, face), 0.5));
    g_world_factor = clamp(1.0 + weight * (ratio - 1.0), 0.6, 1.4);
}
vec3 g_world_light = vec3(1.0);
vec3 own_world_light(vec3 lightmap) {
    g_world_light = lightmap * g_world_factor * g_cavity;
    return g_world_light;
}
#ifdef OWN_DLIGHT
// The level's dynamic-light passes (2.5): the original lights each surface by its plane's distance to the light
// (V0 / V1 = 1 - distance / radius, the same in all four channels; the falloff texture draws the circle) with no
// N.L. Ours scales each light's colour (not its alpha: the pass adds rgb x alpha) per pixel by how much more or less
// the bumped normal faces the light than the face does - the 2.4 ratio, so flat surfaces keep the original's
// light. own[11].w: the base texture's normal map bound on t6 (the main pass's stage 0 texture); own[28].x strength.
float own_dlight_factor(vec3 face, vec3 n, vec4 light) {
    const vec4 q = vec4(light.xyz, 1.0);
    const vec3 position = vec3(dot(OWN(1), q), dot(OWN(2), q), dot(OWN(3), q));
    const vec3 toward = normalize(position - world_pos);
    const float ratio = max(dot(n, toward), 0.0) / (max(dot(face, toward), 0.2) * max(dot(n, face), 0.5));
    return clamp(mix(1.0, ratio, smoothstep(0.0, 0.17, dot(face, toward))), 0.5, 1.6);
}
void own_dlight_begin(inout vec4 V0, inout vec4 V1) {
    vec3 face = cross(dFdx(world_pos), dFdy(world_pos));
    face = dot(face, face) > 1e-20 ? normalize(face) : vec3(0.0, 0.0, 1.0);
    if (dot(face, OWN(12).xyz - world_pos) < 0.0) face = -face;
    g_normal = face;
    if (OWN(28).x <= 0.0 || !own_has_maps()) return;
    const vec3 n = normalize(mix(face, own_mapped_normal_at(face, tex6, smp6, vec4(base_uv.xy, 0.0, 1.0), vec4(1.0)),
                                 OWN(28).x));
    g_normal = n;
    V0.rgb *= own_dlight_factor(face, n, vsc.c[18]);
#ifdef OWN_DLIGHT2
    V1.rgb *= own_dlight_factor(face, n, vsc.c[19]);
#endif
}
#endif
// The projector stage: our depth compare's lost share (the title's combiners darken by it times v0's strength).
float own_world_shadow(vec4 coord) {
    if (OWN(0).z == 0.0) return 0.0;
    g_lost = own_shadow_pcf(tex5, smp5, coord.xyz / coord.w);
    return g_lost;
}
#endif

// GGX specular (dielectric F0 0.04) for light `toward` with colour `light` at roughness r: D G F / (4 N.V), i.e.
// the BRDF times N.L (Smith G carries N.L).
vec3 own_specular(vec3 n, vec3 v, vec3 toward, vec3 light, float r) {
    const vec3 h = normalize(toward + v);
    const float nl = max(dot(n, toward), 0.0), nv = max(dot(n, v), 1e-3), nh = max(dot(n, h), 0.0);
    const float a = max(r * r, 0.02), a2 = a * a;
    const float d = nh * nh * (a2 - 1.0) + 1.0;
    const float D = a2 / (3.14159265 * d * d);
    const float k = a * 0.5;
    const float G = nl / (nl * (1.0 - k) + k) * nv / (nv * (1.0 - k) + k);
    const float F = 0.04 + 0.96 * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    return light * (D * G * F / (4.0 * nv));
}


// The diffuse colour the title's combiners take as V0.
vec4 own_model_v0(vec4 vertex_color) {
    g_light = vertex_color.rgb;
    const vec3 geometric = normalize(world_normal);
    g_normal = geometric;
    const bool pixel = OWN(0).x != 0.0, shadow = OWN(0).z != 0.0;
    const int sun = int(OWN(0).y);
    g_lights = int(OWN(11).x);
    if (!pixel && !shadow) return vertex_color;
    // The shading normal: the normal map's where there is one (2.3).
    vec3 n = geometric;
    if (pixel && OWN(28).x > 0.0) n = own_mapped_normal(geometric);
    g_normal = n;
    // Roughness floor 0.4: the old textures' maps get no mirror-sharp highlights (lab m2 11-15: p95 rise 163 levels
    // without it).
    const float roughness = OWN(28).y > 0.0 ? clamp(own_surface().r, 0.4, 1.0) : 1.0;
    const vec4 eye_object = vec4(vsc.c[19].xyz, 1.0);  // the camera in object space (the fog's)
    const vec3 eye = vec3(dot(OWN(1), eye_object), dot(OWN(2), eye_object), dot(OWN(3), eye_object));
    const vec3 view = normalize(eye - world_pos);
    // The sun's share lost to the shadow map (receiver off its surface along the GEOMETRIC normal, more where the sun
    // grazes it, so the model's own coarse caster copy does not shadow its lit side).
    g_lost = 0.0;
    if (shadow && sun >= 0) {
        vec3 toward;
        own_light(sun, world_pos, toward);
        const float facing = dot(geometric, toward);
        const float offset = OWN(11).y * (1.0 + 2.0 * (1.0 - clamp(facing, 0.0, 1.0)));
        g_lost = facing > 0.0 ? own_sun_lost(world_pos + geometric * offset) : 0.0;
    }
    if (pixel) {
        vec3 sum = vsc.c[17].rgb;  // the ambient
        g_spec = vec3(0.0);
        for (int i = 0; i < g_lights && i < 8; ++i) {
            vec3 toward;
            vec3 light = own_light(i, world_pos, toward);
            if (i == sun) light *= 1.0 - g_lost;
            sum += light * max(dot(n, toward), 0.0);
            // Shine from the sun and point lights only (the grid's other directions are bounce light).
            if (OWN(28).y > 0.0 && (i == sun || OWN(12 + i).w != 0.0))
                g_spec += own_specular(n, view, toward, light, roughness);  // includes N.L
        }
        // "Low strength" (user 11-15): scaled, then softly capped at 0.3 per channel.
        g_spec *= OWN(28).y;
        g_spec = 0.3 * g_spec / (0.3 + g_spec);
        if (own_has_maps()) sum *= own_cavity(tex7, smp7, t0.xy / t0.w * texcoord_scale[0].xy);
        g_light = sum;
    } else if (shadow && sun >= 0) {
        vec3 toward;
        const vec3 sun_color = own_light(sun, world_pos, toward);
        g_light = max(g_light - sun_color * max(dot(geometric, toward), 0.0) * g_lost, vec3(0.0));
    }
    return vec4(g_light, vertex_color.w);
}

void own_model_out(vec4 result) {
    // Shine on top of the combiners' result, faded with the fog as the surface colour is.
    color = vec4(min(result.rgb + g_spec * clamp(fog, 0.0, 1.0), vec3(1.0)), result.w);
    const int view = int(OWN(0).w + 0.5);
    if (view == 5 || view == 8) {  // roughness, height: grey; magenta where the texture has no maps
#ifdef OWN_WORLD_BLEND
        color = vec4(1.0, 0.0, 1.0, result.w);  // t7 holds the second layer's normal map here
#else
        const vec4 s = own_surface();
        color = vec4(own_has_maps() ? vec3(view == 5 ? s.r : s.g) : vec3(1.0, 0.0, 1.0), result.w);
#endif
        return;
    }
#ifdef OWN_WORLD
    if (view == 1) {  // the light only: lightmap x normal-map factor x shadow
        color = vec4(clamp(g_world_light * (1.0 - g_lost), 0.0, 1.0), result.w);
        return;
    }
#endif
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
