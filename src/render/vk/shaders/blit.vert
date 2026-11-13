#version 450
// A rectangle (triangle strip of 4) from push constants: dst in clip space, src in texture coordinates.
layout(push_constant) uniform Push {
    vec4 dst;  // x0, y0, x1, y1
    vec4 src;  // u0, v0, u1, v1
    int mode;  // blit.frag
} pc;
layout(location = 0) out vec2 uv;
void main() {
    vec2 corner = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
    uv = mix(pc.src.xy, pc.src.zw, corner);
    gl_Position = vec4(mix(pc.dst.xy, pc.dst.zw, corner), 0.0, 1.0);
}
