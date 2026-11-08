// Vertex program 008bc110 (11 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: FxOp1d5_InitShaders.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2) {
    float4 R0;
    float4 R1;
    float4 R11;
    float4 R12;
    float4 oD0;
    float4 oD1 = float4(0, 0, 0, 1);
    float4 oT0;
    float4 oT1 = float4(0, 0, 0, 1);
    float4 oT2 = float4(0, 0, 0, 1);
    float4 oT3 = float4(0, 0, 0, 1);
    float4 oFog;
    float4 mac;
    float4 ilu;

    // 0: DP4
    mac = dot(v0, c[9]);
    R0.w = mac.w;
    R12.w = mac.w;
    // 1: DP4
    R0.x = ((float4)(dot(v0, c[6]))).x;
    // 2: DP4 + RCC
    mac = dot(v0, c[7]);
    ilu = nv_rcc((R12.wwww).x);
    R0.y = mac.y;
    R1.x = ilu.x;
    // 3: DP4
    R0.z = ((float4)(dot(v0, c[8]))).z;
    // 4: MAD
    R11.x = ((float4)(R0.wwww * c[10].xxxx + c[10].yyyy)).x;
    // 5: MOV
    R12.xyz = ((float4)(R0)).xyz;
    // 6: MAX + MOV
    mac = max(R11.xxxx, c[1].xxxx);
    ilu = v1.xyzz;
    R11.x = mac.x;
    oD0.xyz = ilu.xyz;
    // 7: MOV
    oD0.w = ((float4)(v1.wwww)).w;
    // 8: MIN
    oFog.xyzw = ((float4)(min(R11.xxxx, c[1].zzzz))).xyzw;
    // 9: MOV
    oT0.xyzw = ((float4)(v2)).xyzw;
    // 10: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
