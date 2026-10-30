// Vertex program 0089c0b0 (31 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdAirplaneMetal_Register.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2, float4 v3 : TEXCOORD3, float4 v4 : TEXCOORD4) {
    float4 R0;
    float4 R1;
    float4 R2;
    float4 R3;
    float4 R4;
    float4 R5;
    float4 R6;
    float4 R11;
    float4 R12;
    float4 oD0;
    oD0.w = 1;
    float4 oD1;
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1;
    oT1.zw = float2(0, 1);
    float4 oT2 = float4(0, 0, 0, 1);
    float4 oT3 = float4(0, 0, 0, 1);
    float4 oFog;
    float4 mac;
    float4 ilu;

    // 0: ADD
    R3.xyz = ((float4)(c[11].xyzz + -v0.xyzz)).xyz;
    // 1: ADD
    R11.xyz = ((float4)(v0.xyzz + -c[22].xyzz)).xyz;
    // 2: DP3 + MOV
    mac = dot((R3.xyzz).xyz, (R3.xyzz).xyz);
    ilu = v4.xyyy;
    R4.w = mac.w;
    oT0.xy = ilu.xy;
    // 3: DP3
    R11.x = ((float4)(dot((R11.xyzz).xyz, (R11.xyzz).xyz))).x;
    // 4: DP3 + RSQ
    mac = dot((v1.xyzz).xyz, (c[13].xyzz).xyz);
    ilu = rsqrt(abs((R4.wwww).x));
    R0.x = mac.x;
    R1.w = ilu.w;
    // 5: DP3 + RSQ
    mac = dot((v1.xyzz).xyz, (c[14].xyzz).xyz);
    ilu = rsqrt(abs((R11.xxxx).x));
    R0.y = mac.y;
    R1.x = ilu.x;
    // 6: MUL
    R3.xyz = ((float4)(R3.xyzz * R1.wwww)).xyz;
    // 7: ADD
    R5.xyz = ((float4)(R3.xyzz + -c[10].xyzz)).xyz;
    // 8: DP3
    R0.z = ((float4)(dot((v1.xyzz).xyz, (c[15].xyzz).xyz))).z;
    // 9: DP3
    R0.w = ((float4)(dot((v1.xyzz).xyz, (c[16].xyzz).xyz))).w;
    // 10: DP3
    R6.w = ((float4)(dot((R5.xyzz).xyz, (R5.xyzz).xyz))).w;
    // 11: MAX
    R0.xyzw = ((float4)(max(R0, c[1].xxxx))).xyzw;
    // 12: DP4 + RSQ
    mac = dot(v0, c[9]);
    ilu = rsqrt(abs((R6.wwww).x));
    R12.w = mac.w;
    R1.w = ilu.w;
    // 13: MAD
    R11.x = ((float4)(R1.xxxx * R11.xxxx + -c[21].yyyy)).x;
    // 14: MUL + RCC
    mac = R5.xyzz * R1.wwww;
    ilu = nv_rcc((R12.wwww).x);
    R5.xyz = mac.xyz;
    R1.x = ilu.x;
    // 15: MUL
    R11.x = ((float4)(R11.xxxx * c[21].zzzz)).x;
    // 16: MAD
    R11.x = ((float4)(R11.xxxx * c[21].wwww + c[21].xxxx)).x;
    // 17: DP4
    R12.x = ((float4)(dot(v0, c[6]))).x;
    // 18: DP4
    R12.y = ((float4)(dot(v0, c[7]))).y;
    // 19: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 20: DP4
    R2.x = ((float4)(dot(R0, c[17]))).x;
    // 21: DP4
    R2.y = ((float4)(dot(R0, c[18]))).y;
    // 22: DP4
    R2.z = ((float4)(dot(R0, c[19]))).z;
    // 23: DP3
    R0.x = ((float4)(dot((v1.xyzz).xyz, (-c[10].xyzz).xyz))).x;
    // 24: DP3
    R0.z = ((float4)(dot((R5.xyzz).xyz, (v2.xyzz).xyz))).z;
    // 25: DP3
    R0.y = ((float4)(dot((R5.xyzz).xyz, (v3.xyzz).xyz))).y;
    // 26: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R11.xxxx)).xyzw;
    // 27: ADD
    oD0.xyz = ((float4)(R2.xyzz + c[20].xyzz)).xyz;
    // 28: SGE
    oD1.xyzw = ((float4)(float4(R0.xxxx >= c[1].xxxx))).xyzw;
    // 29: MAD
    oT1.xy = ((float4)(R0.zyyy * c[1].yyyy + c[1].yyyy)).xy;
    // 30: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
