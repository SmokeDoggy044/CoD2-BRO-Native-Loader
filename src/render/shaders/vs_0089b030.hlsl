// Vertex program 0089b030 (39 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdPropeller_Register.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2) {
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

    // 0: MOV
    R3.x = ((float4)(c[18].xxxx)).x;
    // 1: ADD
    R11.xyz = ((float4)(v0.xyzz + -c[23].xyzz)).xyz;
    // 2: MAD
    R4.x = ((float4)(R3.xxxx * c[2].wwww + c[2].yyyy)).x;
    // 3: DP3
    R11.x = ((float4)(dot((R11.xyzz).xyz, (R11.xyzz).xyz))).x;
    // 4: DP3 + EXP
    mac = dot((v1.xyzz).xyz, (c[10].xyzz).xyz);
    ilu = nv_exp((R4.xxxx).x);
    R0.x = mac.x;
    R1.y = ilu.y;
    // 5: DP3 + RSQ
    mac = dot((v1.xyzz).xyz, (c[11].xyzz).xyz);
    ilu = rsqrt(abs((R11.xxxx).x));
    R0.y = mac.y;
    R1.x = ilu.x;
    // 6: MAD
    R4.x = ((float4)(R1.yyyy * c[2].zzzz + -c[2].xxxx)).x;
    // 7: DP3
    R0.z = ((float4)(dot((v1.xyzz).xyz, (c[12].xyzz).xyz))).z;
    // 8: DST
    R5.xy = ((float4)(nv_dst(R4.xxxx, R4.xxxx))).xy;
    // 9: MUL
    R5.z = ((float4)(R5.yyyy * R5.yyyy)).z;
    // 10: MUL
    R5.w = ((float4)(R5.yyyy * R5.zzzz)).w;
    // 11: MUL
    R4.xyzw = ((float4)(R5 * R4.xxxx)).xyzw;
    // 12: DP4
    R4.y = ((float4)(dot(R4, c[4]))).y;
    // 13: DP4
    R4.x = ((float4)(dot(R5, c[3]))).x;
    // 14: DP3
    R0.w = ((float4)(dot((v1.xyzz).xyz, (c[13].xyzz).xyz))).w;
    // 15: MAD
    R11.x = ((float4)(R1.xxxx * R11.xxxx + -c[22].yyyy)).x;
    // 16: MAX
    R0.xyzw = ((float4)(max(R0, c[1].xxxx))).xyzw;
    // 17: ADD
    R2.yz = ((float4)(v2.xxyy + -c[1].yyyy)).yz;
    // 18: MOV
    R2.x = ((float4)(c[1].xxxx)).x;
    // 19: DP4
    R12.w = ((float4)(dot(v0, c[9]))).w;
    // 20: MUL
    R11.x = ((float4)(R11.xxxx * c[22].zzzz)).x;
    // 21: MOV + RCC
    mac = c[1].xxxx;
    ilu = nv_rcc((R12.wwww).x);
    R6.x = mac.x;
    R1.x = ilu.x;
    // 22: MOV
    R6.y = ((float4)(R4.yyyy)).y;
    // 23: MOV
    R6.w = ((float4)(-R4.xxxx)).w;
    // 24: MOV
    R7.z = ((float4)(c[1].xxxx)).z;
    // 25: MOV
    R7.yw = ((float4)(R4.yyyx)).yw;
    // 26: DP4
    R12.x = ((float4)(dot(v0, c[6]))).x;
    // 27: DP4
    R12.y = ((float4)(dot(v0, c[7]))).y;
    // 28: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 29: MAD
    R11.x = ((float4)(R11.xxxx * c[22].wwww + c[22].xxxx)).x;
    // 30: DP3
    R8.x = ((float4)(dot((R2.yzxx).xyz, (R6.ywxx).xyz))).x;
    // 31: DP3
    R8.y = ((float4)(dot((R2.yzxx).xyz, (R7.wyzz).xyz))).y;
    // 32: DP4
    R9.x = ((float4)(dot(R0, c[14]))).x;
    // 33: DP4
    R9.y = ((float4)(dot(R0, c[15]))).y;
    // 34: DP4
    R9.z = ((float4)(dot(R0, c[16]))).z;
    // 35: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R11.xxxx)).xyzw;
    // 36: ADD
    oT0.xy = ((float4)(R8.xyyy + c[1].yyyy)).xy;
    // 37: ADD
    oD0.xyz = ((float4)(R9.xyzz + c[17].xyzz)).xyz;
    // 38: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
