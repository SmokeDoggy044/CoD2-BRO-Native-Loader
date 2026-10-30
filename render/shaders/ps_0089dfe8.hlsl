// Pixel shader 0089dfe8 (3 combiner stages), translated from its register combiner
// setup (D3DPIXELSHADERDEF) by tools/shader_hlsl.py; generated, do not edit.
// Used by: cdOcean_InitPixelShaders[2].

#define PIXEL_SHADER
#include "nv2a.hlsli"

sampler2D s0 : register(s0);
sampler2D s2 : register(s2);

float4 main(float4 v0 : COLOR0, float4 v1 : COLOR1, float4 t0 : TEXCOORD0, float4 t1 : TEXCOORD1,
            float4 t2 : TEXCOORD2, float4 t3 : TEXCOORD3, float fog : FOG) : COLOR0 {
    float4 V0 = v0, V1 = v1;
    float4 FOG = float4(fog_color.rgb, saturate(fog));
    float4 T0 = 0, T1 = 0, T2 = 0, T3 = 0;
    T0 = tex2Dproj(s0, t0 * texcoord_scale[0]);
    if (alpha_kill.x != 0) clip(T0.w - 1.0 / 512);
    if (alpha_kill.y != 0) clip(T1.w - 1.0 / 512);
    T2 = tex2Dproj(s2, t2 * texcoord_scale[2]);
    if (alpha_kill.z != 0) clip(T2.w - 1.0 / 512);
    if (alpha_kill.w != 0) clip(T3.w - 1.0 / 512);
    // R0 starts as (0, 0, 0, T0.alpha), R1 as zero.
    float4 R0 = float4(0, 0, 0, T0.w), R1 = 0;
    float4 ab = 0, cd = 0, sum = 0;
    // Combiner stage 0.
    ab.xyz = clamp((T0.xyz * V0.xyz), -1, 1);
    R0.xyz = ab.xyz;
    // Combiner stage 1.
    ab.w = clamp((T2.w * V0.w), -1, 1);
    T2.w = ab.w;
    // Combiner stage 2.
    sum.xyz = clamp((T2.xyz * T2.www + R0.xyz), -1, 1);
    ab.w = clamp((c[2].w), -1, 1);
    R0.xyz = sum.xyz;
    R0.w = ab.w;
    // Final combiner: rgb = A*B + (1-A)*C + D, alpha = G; E*F and V1+R0 are inputs too.
    return saturate(float4(lerp(unsigned_identity(FOG.xyz), unsigned_identity(R0.xyz), unsigned_identity(FOG.www)), unsigned_identity(R0.w)));
}
