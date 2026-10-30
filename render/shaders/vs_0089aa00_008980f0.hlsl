// Vertex program 0089aa00 (7 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdSimpleAlpha_InitVertexShaders[0].

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v2 : TEXCOORD2) {
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
    R12.w = ((float4)(dot(v0, c[11]))).w;
    // 1: DP4
    R12.x = ((float4)(dot(v0, c[8]))).x;
    // 2: DP4 + RCC
    mac = dot(v0, c[9]);
    ilu = nv_rcc((R12.wwww).x);
    R12.y = mac.y;
    R1.x = ilu.x;
    // 3: DP4
    R12.z = ((float4)(dot(v0, c[10]))).z;
    // 4: MOV
    oT0.xy = ((float4)(v2.xyyy)).xy;
    // 5: MOV
    oD0.xyzw = ((float4)(c[20])).xyzw;
    // 6: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
