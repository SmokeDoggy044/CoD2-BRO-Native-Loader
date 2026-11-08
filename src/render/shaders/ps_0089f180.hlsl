// Pixel shader 0089f180 (3 combiner stages), translated from its register combiner
// setup (D3DPIXELSHADERDEF) by tools/shader_hlsl.py; generated, do not edit.
// Used by: cdWorldBlend_InitPixelShaders[1].

#define PIXEL_SHADER
#include "nv2a.hlsli"

sampler2D s0 : register(s0);
sampler2D s1 : register(s1);
sampler2D s3 : register(s3);

float4 main(float4 v0 : COLOR0, float4 v1 : COLOR1, float4 t0 : TEXCOORD0, float4 t1 : TEXCOORD1,
            float4 t2 : TEXCOORD2, float4 t3 : TEXCOORD3, float fog : FOG) : COLOR0 {
    float4 V0 = v0, V1 = v1;
    float4 FOG = float4(fog_color.rgb, saturate(fog));
    float4 T0 = 0, T1 = 0, T2 = 0, T3 = 0;
    T0 = tex2Dproj(s0, t0 * texcoord_scale[0]);
    if (alpha_kill.x != 0) clip(T0.w - 1.0 / 512);
    T1 = tex2Dproj(s1, t1 * texcoord_scale[1]);
    if (alpha_kill.y != 0) clip(T1.w - 1.0 / 512);
    if (alpha_kill.z != 0) clip(T2.w - 1.0 / 512);
    T3 = tex2Dproj(s3, t3 * texcoord_scale[3]);
    if (alpha_kill.w != 0) clip(T3.w - 1.0 / 512);
    // R0 starts as (0, 0, 0, T0.alpha), R1 as zero.
    float4 R0 = float4(0, 0, 0, T0.w), R1 = 0;
    float4 ab = 0, cd = 0, sum = 0;
    // Combiner stage 0.
    sum.xyz = clamp((unsigned_identity(V0.www) * T1.xyz + T0.xyz * unsigned_invert(V0.www)), -1, 1);
    R0.xyz = sum.xyz;
    // Combiner stage 1.
    ab.w = clamp((T3.w * V0.z), -1, 1);
    R1.w = ab.w;
    // Combiner stage 2.
    ab.xyz = clamp((R0.xyz * unsigned_invert(R1.www)), -1, 1);
    ab.w = clamp((T0.w), -1, 1);
    R0.xyz = ab.xyz;
    R0.w = ab.w;
    return saturate(R0);
}
