// Vertex program 0089ca40 (20 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdSimple_Register.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2) {
    float4 R0;
    float4 R1;
    float4 R2;
    float4 R11;
    float4 R12;
    float4 oD0;
    oD0.w = 1;
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
    R11.xyz = ((float4)(v0.xyzz + -c[19].xyzz)).xyz;
    // 1: DP3
    R0.x = ((float4)(dot((v1.xyzz).xyz, (c[10].xyzz).xyz))).x;
    // 2: DP3 + MOV
    mac = dot((R11.xyzz).xyz, (R11.xyzz).xyz);
    ilu = v2.xyyy;
    R11.x = mac.x;
    oT0.xy = ilu.xy;
    // 3: DP3
    R0.y = ((float4)(dot((v1.xyzz).xyz, (c[11].xyzz).xyz))).y;
    // 4: DP3 + RSQ
    mac = dot((v1.xyzz).xyz, (c[12].xyzz).xyz);
    ilu = rsqrt(abs((R11.xxxx).x));
    R0.z = mac.z;
    R1.x = ilu.x;
    // 5: DP3
    R0.w = ((float4)(dot((v1.xyzz).xyz, (c[13].xyzz).xyz))).w;
    // 6: MAD
    R11.x = ((float4)(R1.xxxx * R11.xxxx + -c[18].yyyy)).x;
    // 7: MAX
    R0.xyzw = ((float4)(max(R0, c[1].xxxx))).xyzw;
    // 8: DP4
    R12.w = ((float4)(dot(v0, c[9]))).w;
    // 9: MUL
    R11.x = ((float4)(R11.xxxx * c[18].zzzz)).x;
    // 10: DP4 + RCC
    mac = dot(v0, c[6]);
    ilu = nv_rcc((R12.wwww).x);
    R12.x = mac.x;
    R1.x = ilu.x;
    // 11: DP4
    R12.y = ((float4)(dot(v0, c[7]))).y;
    // 12: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 13: MAD
    R11.x = ((float4)(R11.xxxx * c[18].wwww + c[18].xxxx)).x;
    // 14: DP4
    R2.x = ((float4)(dot(R0, c[14]))).x;
    // 15: DP4
    R2.y = ((float4)(dot(R0, c[15]))).y;
    // 16: DP4
    R2.z = ((float4)(dot(R0, c[16]))).z;
    // 17: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R11.xxxx)).xyzw;
    // 18: ADD
    oD0.xyz = ((float4)(R2.xyzz + c[17].xyzz)).xyz;
    // 19: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
