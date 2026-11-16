#version 450
// Picture to the window. mode 0: opaque copy; 1: through the title's gamma ramp (each channel's value
// v picks table texel round(v * 255), as d3d9::gamma_ramp_pixel_shader); 2: keeps alpha (overlays); 3 / 4: the
// widescreen front end's side fill - blurred (a 48-tap spiral, 6% of the width) and dimmed, 4 then through the
// gamma ramp.
layout(set = 0, binding = 0) uniform sampler2D source;
layout(set = 0, binding = 1) uniform sampler2D gamma_table;
layout(push_constant) uniform Push {
    vec4 dst;
    vec4 src;
    int mode;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
vec4 blurred(vec2 at) {
    vec3 sum = vec3(0.0);
    const int kTaps = 48;
    for (int i = 0; i < kTaps; ++i) {
        float r = sqrt((float(i) + 0.5) / float(kTaps)) * 0.06;
        float a = float(i) * 2.39996;
        vec2 p = clamp(at + vec2(cos(a), sin(a) * 1.3333) * r, vec2(0.0), pc.src.zw);
        sum += texture(source, p).rgb;
    }
    return vec4(sum / float(kTaps) * 0.4, 1.0);
}

void main() {
    vec4 c = pc.mode >= 3 ? blurred(uv) : texture(source, uv);
    if (pc.mode == 1 || pc.mode == 4) {
        ivec3 i = ivec3(clamp(c.rgb * 255.0 + 0.5, 0.0, 255.0));
        c = vec4(texelFetch(gamma_table, ivec2(i.r, 0), 0).r, texelFetch(gamma_table, ivec2(i.g, 0), 0).g,
                 texelFetch(gamma_table, ivec2(i.b, 0), 0).b, 1.0);
    } else if (pc.mode == 0) {
        c.a = 1.0;
    }
    color = c;
}
