// Vertex program 008a0a70 (7 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdSky_Register.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2) {
    float4 R1;
    float4 R12;
    float4 oD0;
    float4 oD1 = float4(0, 0, 0, 1);
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1 = float4(0, 0, 0, 1);
    float4 oT2 = float4(0, 0, 0, 1);
    float4 oT3 = float4(0, 0, 0, 1);
    float4 oFog;
    oFog.x = 0;
    float4 mac;
    float4 ilu;

    // 0: DP4
    R12.w = ((float4)(dot(v0, c[9]))).w;
    // 1: DP4
    R12.x = ((float4)(dot(v0, c[6]))).x;
    // 2: DP4 + RCC
    mac = dot(v0, c[7]);
    ilu = nv_rcc((R12.wwww).x);
    R12.y = mac.y;
    R1.x = ilu.x;
    // 3: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 4: ADD
    oT0.xy = ((float4)(v1.xyyy + c[10].xyyy)).xy;
    // 5: MOV
    oD0.xyzw = ((float4)(v2)).xyzw;
    // 6: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
