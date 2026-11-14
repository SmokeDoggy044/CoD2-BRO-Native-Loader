#version 450
// The HDR scene into the 8-bit picture (render graph, 0.5b). The scene holds the title's own values (its
// shaders and blending work in the picture's gamma space, as on the Xbox) in 16-bit floats: no 8-bit rounding
// between passes and no clamping of what adds up past white. Linear light for later passes is
// pow(value, 2.2) of these values. mode 0: clamped to 0..1 as an 8-bit target would have it.
layout(set = 0, binding = 0) uniform sampler2D source;
layout(push_constant) uniform Push {
    vec4 dst;
    vec4 src;
    int mode;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
    color = vec4(clamp(texture(source, uv).rgb, 0.0, 1.0), 1.0);
}
