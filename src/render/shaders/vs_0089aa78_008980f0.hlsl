// Vertex program 0089aa78 (22 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdSimpleAlpha_InitVertexShaders[1].
// With vertex declaration 008980f0: v1 packed normals (NORMPACKED3), given as their bytes.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2) {
    v1 = nv_normpacked3(v1);
    float4 R0;
    float4 R1;
    float4 R2;
    float4 R3;
    float4 R11;
    float4 R12;
    float4 oD0;
    float4 oD1 = float4(0, 0, 0, 1);
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1 = float4(0, 0, 0, 1);
    float4 oT2 = float4(0, 0, 0, 1);
    float4 oT3 = float4(0, 0, 0, 1);
    float4 oFog;
    float4 mac;
    float4 ilu;

    // 0: ADD
    R11.xyz = ((float4)(v0.xyzz + -c[7].xyzz)).xyz;
    // 1: DP3
    R0.x = ((float4)(dot((v1.xyzz).xyz, (c[12].xyzz).xyz))).x;
    // 2: DP3
    R0.y = ((float4)(dot((v1.xyzz).xyz, (c[13].xyzz).xyz))).y;
    // 3: DP3
    R0.z = ((float4)(dot((v1.xyzz).xyz, (c[14].xyzz).xyz))).z;
    // 4: DP3
    R0.w = ((float4)(dot((v1.xyzz).xyz, (c[15].xyzz).xyz))).w;
    // 5: DP3 + MOV
    mac = dot((R11.xyzz).xyz, (R11.xyzz).xyz);
    ilu = v2.xyyy;
    R11.x = mac.x;
    oT0.xy = ilu.xy;
    // 6: MAX
    R0.xyzw = ((float4)(max(R0, c[1].xxxx))).xyzw;
    // 7: DP4 + RSQ
    mac = dot(v0, c[11]);
    ilu = rsqrt(abs((R11.xxxx).x));
    R12.w = mac.w;
    R1.x = ilu.x;
    // 8: DP4
    R2.x = ((float4)(dot(R0, c[16]))).x;
    // 9: MAD
    R11.x = ((float4)(R1.xxxx * R11.xxxx + -c[6].yyyy)).x;
    // 10: DP4 + RCC
    mac = dot(R0, c[17]);
    ilu = nv_rcc((R12.wwww).x);
    R2.y = mac.y;
    R1.x = ilu.x;
    // 11: MUL
    R11.x = ((float4)(R11.xxxx * c[6].zzzz)).x;
    // 12: DP4
    R2.z = ((float4)(dot(R0, c[18]))).z;
    // 13: DP4
    R12.x = ((float4)(dot(v0, c[8]))).x;
    // 14: DP4
    R12.y = ((float4)(dot(v0, c[9]))).y;
    // 15: DP4
    R12.z = ((float4)(dot(v0, c[10]))).z;
    // 16: MAD
    R0.x = ((float4)(R11.xxxx * c[6].wwww + c[6].xxxx)).x;
    // 17: ADD
    R3.xyz = ((float4)(R2.xyzz + c[19].xyzz)).xyz;
    // 18: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R0.xxxx)).xyzw;
    // 19: MUL
    oD0.xyz = ((float4)(R3.xyzz * c[20].xyzz)).xyz;
    // 20: MOV
    oD0.w = ((float4)(c[20].wwww)).w;
    // 21: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
