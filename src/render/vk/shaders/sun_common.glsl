// Shared by our sun-shadow shaders (engine improvements 2.0 step 2), drawn in place of title pixel shaders: the
// title's layout (vk/spirv_layout.h) - pixel constants at binding 1 (gfx_device.h's reserved registers), texture
// t<n> at binding 2 + n, sampler s<n> at 10 + n, inputs COLOR0 0, COLOR1 1, TEXCOORD<n> 2 + n, FOG 10.
layout(set = 0, binding = 1) uniform PixelConstants {
    vec4 c[18];  // the shader's own: ours put their parameters in c0, c1
    vec4 texcoord_scale[8];
    vec4 fog_color;
    vec4 alpha_kill;
    vec4 alpha_test;  // x: compare (0 never .. 7 always), y: reference
} ps;
layout(set = 0, binding = 2) uniform texture2D tex0;
layout(set = 0, binding = 3) uniform texture2D tex1;
layout(set = 0, binding = 4) uniform texture2D tex2;
layout(set = 0, binding = 5) uniform texture2D tex3;
layout(set = 0, binding = 10) uniform sampler smp0;
layout(set = 0, binding = 11) uniform sampler smp1;
layout(set = 0, binding = 12) uniform sampler smp2;
layout(set = 0, binding = 13) uniform sampler smp3;
layout(location = 0) in vec4 v0;
layout(location = 2) in vec4 t0;
layout(location = 3) in vec4 t1;
layout(location = 4) in vec4 t2;
layout(location = 5) in vec4 t3;
layout(location = 10) in float fog;
layout(location = 0) out vec4 color;

// The fixed alpha test, as nv2a.hlsli's nv_alpha_test.
void alpha_test(float alpha) {
    const float f = ps.alpha_test.x;
    if (f == 7.0) return;
    const float a = round(alpha * 255.0), r = ps.alpha_test.y;
    const bool lt = a < r, eq = a == r, gt = a > r;
    const bool ok = (f == 1.0 && lt) || (f == 2.0 && eq) || (f == 3.0 && (lt || eq)) || (f == 4.0 && gt) ||
                    (f == 5.0 && !eq) || (f == 6.0 && (gt || eq));
    if (!ok) discard;
}

// 1 - the caster's depth (0 = no caster: the texture's clear colour and border) in 24 bits of RGB.
vec3 pack_depth(float e) {
    vec3 enc = fract(clamp(e, 0.0, 0.99999994) * vec3(1.0, 255.0, 65025.0));
    enc.xy -= enc.yz / 255.0;
    return enc;
}
float unpack_depth(vec3 rgb) { return dot(round(rgb * 255.0) / 255.0, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)); }
