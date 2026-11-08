// Vertex program 0089da40 (60 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdOcean_Register.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1) {
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
    float4 oD1 = float4(0, 0, 0, 1);
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1;
    oT1.zw = float2(0, 1);
    float4 oT2;
    oT2.zw = float2(0, 1);
    float4 oT3;
    oT3.zw = float2(0, 1);
    float4 oFog;
    float4 mac;
    float4 ilu;

    // 0: DP4
    R0.x = ((float4)(dot(v0, c[12]))).x;
    // 1: DP4
    R0.y = ((float4)(dot(v0, c[13]))).y;
    // 2: MOV + MOV
    mac = v0.xyyw;
    ilu = c[1].zzzz;
    R4.xyw = mac.xyw;
    oD0.xyz = ilu.xyz;
    // 3: ADD
    R2.xyzw = ((float4)(R0.xyxy + -c[24])).xyzw;
    // 4: ADD
    R11.xyzw = ((float4)(R0.xyxy + -c[25])).xyzw;
    // 5: MUL + MOV
    mac = R2 * c[26];
    ilu = v1.xyyy;
    R2.xyzw = mac.xyzw;
    oT3.xy = ilu.xy;
    // 6: MUL
    R11.xyzw = ((float4)(R11 * c[27])).xyzw;
    // 7: ADD
    R3.xy = ((float4)(R2.xzzz + R2.ywww)).xy;
    // 8: ADD
    R3.zw = ((float4)(R11.xxxz + R11.yyyw)).zw;
    // 9: MAD
    oT0.xy = ((float4)(R0.xyyy * c[21].xyyy + -c[21].zwww)).xy;
    // 10: MAX
    R3.xyzw = ((float4)(max(R3, -R3))).xyzw;
    // 11: MUL
    R11.xyzw = ((float4)(R3 * c[29])).xyzw;
    // 12: ADD
    R11.xyzw = ((float4)(R11 + c[31])).xyzw;
    // 13: MUL
    R5.xyzw = ((float4)(R3 * c[28])).xyzw;
    // 14: MAD
    R7.xyzw = ((float4)(R11 * c[2].wwww + c[2].yyyy)).xyzw;
    // 15: MIN
    R5.xyzw = ((float4)(min(R5, c[1].zzzz))).xyzw;
    // 16: MAX + EXP
    mac = max(R5, c[1].xxxx);
    ilu = nv_exp((R7.zzzz).x);
    R5.xyzw = mac.xyzw;
    R1.y = ilu.y;
    // 17: EXP
    R8.y = ((float4)(nv_exp((R7.wwww).x))).y;
    // 18: MOV
    R8.x = ((float4)(R1.yyyy)).x;
    // 19: EXP
    R11.y = ((float4)(nv_exp((R7.xxxx).x))).y;
    // 20: EXP
    R7.y = ((float4)(nv_exp((R7.yyyy).x))).y;
    // 21: MOV
    R7.z = ((float4)(R11.yyyy)).z;
    // 22: MOV
    R7.xw = ((float4)(R8.xxxy)).xw;
    // 23: MAD
    R6.xyzw = ((float4)(-R5 * c[30] + c[30])).xyzw;
    // 24: MAD
    R7.xyzw = ((float4)(R7.zyxw * c[2].zzzz + -c[2].xxxx)).xyzw;
    // 25: MAD
    oT1.xy = ((float4)(R0.xyyy * c[22].xyyy + -c[22].zwww)).xy;
    // 26: DST
    R9.xy = ((float4)(nv_dst(R7.xxxx, R7.xxxx))).xy;
    // 27: DST
    R10.xy = ((float4)(nv_dst(R7.yyyy, R7.yyyy))).xy;
    // 28: DST
    R11.xy = ((float4)(nv_dst(R7.zzzz, R7.zzzz))).xy;
    // 29: DST
    R2.y = ((float4)(nv_dst(R7.wwww, R7.wwww))).y;
    // 30: MUL
    R9.z = ((float4)(R9.yyyy * R9.yyyy)).z;
    // 31: MUL
    R10.z = ((float4)(R10.yyyy * R10.yyyy)).z;
    // 32: MUL
    R11.z = ((float4)(R11.yyyy * R11.yyyy)).z;
    // 33: MUL
    R2.z = ((float4)(R2.yyyy * R2.yyyy)).z;
    // 34: MUL
    R9.w = ((float4)(R9.yyyy * R9.zzzz)).w;
    // 35: MUL
    R10.w = ((float4)(R10.yyyy * R10.zzzz)).w;
    // 36: MUL
    R11.w = ((float4)(R11.yyyy * R11.zzzz)).w;
    // 37: MUL
    R2.w = ((float4)(R2.yyyy * R2.zzzz)).w;
    // 38: DP4
    R2.x = ((float4)(dot(R9, c[3]))).x;
    // 39: DP4
    R2.y = ((float4)(dot(R10, c[3]))).y;
    // 40: DP4
    R2.z = ((float4)(dot(R11, c[3]))).z;
    // 41: MAD
    oT2.xy = ((float4)(R0.xyyy * c[23].xyyy + -c[23].zwww)).xy;
    // 42: DP4
    R2.w = ((float4)(dot(R2, c[3]))).w;
    // 43: DP4
    R11.x = ((float4)(dot(R2, R6))).x;
    // 44: MAD
    R3.xyzw = ((float4)(-R5 * R2 + R2)).xyzw;
    // 45: ADD
    R0.z = ((float4)(c[20].xxxx + R11.xxxx)).z;
    // 46: DP4
    oD0.w = ((float4)(dot(R3, c[32]))).w;
    // 47: DP4
    R4.z = ((float4)(dot(R0.xyzy, c[18]))).z;
    // 48: NOP
    // 49: ADD
    R11.xyz = ((float4)(R4.xyzz + -c[7].xyzz)).xyz;
    // 50: DP4
    R12.w = ((float4)(dot(R4, c[11]))).w;
    // 51: DP3
    R11.x = ((float4)(dot((R11.xyzz).xyz, (R11.xyzz).xyz))).x;
    // 52: DP4 + RCC
    mac = dot(R4, c[8]);
    ilu = nv_rcc((R12.wwww).x);
    R12.x = mac.x;
    R1.x = ilu.x;
    // 53: DP4 + RSQ
    mac = dot(R4, c[9]);
    ilu = rsqrt(abs((R11.xxxx).x));
    R12.y = mac.y;
    R1.y = ilu.y;
    // 54: DP4
    R12.z = ((float4)(dot(R4, c[10]))).z;
    // 55: MAD
    R8.x = ((float4)(R1.yyyy * R11.xxxx + -c[6].yyyy)).x;
    // 56: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;
    // 57: MUL
    R8.x = ((float4)(R8.xxxx * c[6].zzzz)).x;
    // 58: MAD
    R8.x = ((float4)(R8.xxxx * c[6].wwww + c[6].xxxx)).x;
    // 59: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R8.xxxx)).xyzw;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
