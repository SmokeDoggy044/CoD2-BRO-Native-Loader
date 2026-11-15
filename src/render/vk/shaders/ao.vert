#version 450
// A full-screen triangle (no vertex buffer) for the AO passes; uv 0..1 with the top row at 0.
layout(location = 0) out vec2 uv;
void main() {
    const vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
