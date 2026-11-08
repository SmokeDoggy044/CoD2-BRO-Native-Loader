// Vertex program 0089a1b8 (26 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdGlass_InitVertexShaders[1].

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
    float4 R7;
    float4 R8;
    float4 R9;
    float4 R10;
    float4 R11;
    float4 R12;
    float4 oD0;
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
    R3.xyz = ((float4)(c[15].xyzz + -v0.xyzz)).xyz;
    // 1: ADD
    R11.xyz = ((float4)(v0.xyzz + -c[26].xyzz)).xyz;
    // 2: DP3 + MOV
    mac = dot((R3.xyzz).xyz, (R3.xyzz).xyz);
    ilu = v1.xyyy;
    R4.w = mac.w;
    oT0.xy = ilu.xy;
    // 3: DP3 + MOV
    mac = dot((R11.xyzz).xyz, (R11.xyzz).xyz);
    ilu = v2.xyzz;
    R11.x = mac.x;
    oD0.xyz = ilu.xyz;
    // 4: MOV + RSQ
    mac = v4.xyzz;
    ilu = rsqrt(abs((R4.wwww).x));
    R9.xyz = mac.xyz;
    R1.w = ilu.w;
    // 5: DP4 + RSQ
    mac = dot(v0, c[9]);
    ilu = rsqrt(abs((R11.xxxx).x));
    R12.w = mac.w;
    R1.x = ilu.x;
    // 6: MUL + MOV
    mac = R3.xyzz * R1.wwww;
    ilu = c[1].zzzz;
    R5.xyz = mac.xyz;
    oD0.w = ilu.w;
    // 7: MAD
    R0.x = ((float4)(R1.xxxx * R11.xxxx + -c[25].yyyy)).x;
    // 8: ADD
    R6.xyz = ((float4)(R5.xyzz + -c[14].xyzz)).xyz;
    // 9: MUL + RCC
    mac = v3.yzxx * R9.zxyy;
    ilu = nv_rcc((R12.wwww).x);
    R10.xyz = mac.xyz;
    R1.x = ilu.x;
    // 10: DP3
    R7.w = ((float4)(dot((R6.xyzz).xyz, (R6.xyzz).xyz))).w;
    // 11: MUL
    R0.x = ((float4)(R0.xxxx * c[25].zzzz)).x;
    // 12: DP3 + RSQ
    mac = dot((v3.xyzz).xyz, (-c[14].xyzz).xyz);
    ilu = rsqrt(abs((R7.wwww).x));
    R2.x = mac.x;
    R1.w = ilu.w;
    // 13: MAD
    R10.xyz = ((float4)(-R9.yzxx * v3.zxyy + R10.xyzz)).xyz;
    // 14: MUL
    R8.xyz = ((float4)(R6.xyzz * R1.wwww)).xyz;
    // 15: MAD
    R0.x = ((float4)(R0.xxxx * c[25].wwww + c[25].xxxx)).x;
    // 16: DP4
    R12.x = ((float4)(dot(v0, c[6]))).x;
    // 17: DP4
    R12.y = ((float4)(dot(v0, c[7]))).y;
    // 18: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 19: SGE
    R2.x = ((float4)(float4(R2.xxxx >= c[1].xxxx))).x;
    // 20: DP3
    R11.x = ((float4)(dot((R8.xyzz).xyz, (v4.xyzz).xyz))).x;
    // 21: DP3
    R11.y = ((float4)(dot((R8.xyzz).xyz, (R10.xyzz).xyz))).y;
    // 22: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R0.xxxx)).xyzw;
    // 23: MUL
    oD1.xyzw = ((float4)(R2.xxxx * c[16].xxxx)).xyzw;
    // 24: MAD
    oT1.xy = ((float4)(R11.xyyy * c[1].yyyy + c[1].yyyy)).xy;
    // 25: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
