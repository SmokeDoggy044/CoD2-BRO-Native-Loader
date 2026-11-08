// Vertex program 008bbdc0 (15 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: FxOp31_InitShaders.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2) {
    float4 R0;
    float4 R1;
    float4 R2;
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

    // 0: DP3
    R0.x = ((float4)(dot((v1.xyzz).xyz, (c[10].xyzz).xyz))).x;
    // 1: DP3
    R0.y = ((float4)(dot((v1.xyzz).xyz, (c[11].xyzz).xyz))).y;
    // 2: DP3
    R0.z = ((float4)(dot((v1.xyzz).xyz, (c[12].xyzz).xyz))).z;
    // 3: DP3
    R0.w = ((float4)(dot((v1.xyzz).xyz, (c[13].xyzz).xyz))).w;
    // 4: DP4
    R12.w = ((float4)(dot(v0, c[9]))).w;
    // 5: MAX + MOV
    mac = max(R0, c[1].xxxx);
    ilu = v2.xyyy;
    R0.xyzw = mac.xyzw;
    oT0.xy = ilu.xy;
    // 6: DP4 + RCC
    mac = dot(v0, c[6]);
    ilu = nv_rcc((R12.wwww).x);
    R12.x = mac.x;
    R1.x = ilu.x;
    // 7: DP4
    R12.y = ((float4)(dot(v0, c[7]))).y;
    // 8: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 9: DP4
    R2.x = ((float4)(dot(R0, c[14]))).x;
    // 10: DP4
    R2.y = ((float4)(dot(R0, c[15]))).y;
    // 11: DP4
    R2.z = ((float4)(dot(R0, c[16]))).z;
    // 12: MOV
    oD0.w = ((float4)(c[18].wwww)).w;
    // 13: ADD
    oD0.xyz = ((float4)(R2.xyzz + c[17].xyzz)).xyz;
    // 14: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
