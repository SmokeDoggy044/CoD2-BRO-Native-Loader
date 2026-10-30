// The post passes' vertex shader: a quad given in clip space with its texture coordinates; c0.xy
// moves it by half a target pixel (Direct3D 9's pixel centres).

float4 half_pixel : register(c0);

struct Out {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

Out main(float4 pos : TEXCOORD0, float2 uv : TEXCOORD1) {
    Out o;
    o.pos = float4(pos.xy + half_pixel.xy, 0, 1);
    o.uv = uv;
    return o;
}
