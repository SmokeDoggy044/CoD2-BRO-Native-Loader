// gfx_device_test's vertex shader: v0 = screen-space position (w 1), v1 passed on as texcoord0.
float4 screen_to_clip[2] : register(c192);

struct VsOut {
    float4 pos : POSITION;
    float4 t0 : TEXCOORD0;
};

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1) {
    VsOut o;
    o.pos = float4(v0.xyz * screen_to_clip[0].xyz + screen_to_clip[1].xyz, v0.w);
    o.t0 = v1;
    return o;
}
