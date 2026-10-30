// Vertex program 0089b7c0 (46 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdChar_Register.

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
    float4 R10;
    float4 R11;
    float4 R12;
    float4 oD0;
    oD0.w = 1;
    float4 oD1 = float4(0, 0, 0, 1);
    float4 oT0;
    float4 oT1 = float4(0, 0, 0, 1);
    float4 oT2 = float4(0, 0, 0, 1);
    float4 oT3 = float4(0, 0, 0, 1);
    float4 oFog;
    float4 mac;
    float4 ilu;
    int a0 = 0;

    // 0: MAD
    R0.xyzw = ((float4)(v3 * c[0].xxxx + c[0].yyyy)).xyzw;
    // 1: ADD
    R11.xyz = ((float4)(v0.xyzz + -c[19].xyzz)).xyz;
    // 2: ARL + MOV
    a0 = (int)floor((R0.xxxx).x);
    oT0.xyzw = ((float4)(v2)).xyzw;
    // 3: MOV
    R4.z = ((float4)(c[1].zzzz)).z;
    // 4: MUL
    R10.xyzw = ((float4)(v4.xxxx * c[a0 + 96])).xyzw;
    // 5: MUL
    R2.xyzw = ((float4)(v4.xxxx * c[a0 + 97])).xyzw;
    // 6: MUL
    R3.xyzw = ((float4)(v4.xxxx * c[a0 + 98])).xyzw;
    // 7: ARL
    a0 = (int)floor((R0.yyyy).x);
    // 8: DP3
    R11.x = ((float4)(dot((R11.xyzz).xyz, (R11.xyzz).xyz))).x;
    // 9: MAD
    R10.xyzw = ((float4)(v4.yyyy * c[a0 + 96] + R10)).xyzw;
    // 10: MAD
    R2.xyzw = ((float4)(v4.yyyy * c[a0 + 97] + R2)).xyzw;
    // 11: MAD
    R3.xyzw = ((float4)(v4.yyyy * c[a0 + 98] + R3)).xyzw;
    // 12: ARL + RSQ
    a0 = (int)floor((R0.zzzz).x);
    R1.x = ((float4)(rsqrt(abs((R11.xxxx).x)))).x;
    // 13: MAD
    R10.xyzw = ((float4)(v4.zzzz * c[a0 + 96] + R10)).xyzw;
    // 14: MAD
    R2.xyzw = ((float4)(v4.zzzz * c[a0 + 97] + R2)).xyzw;
    // 15: MAD
    R3.xyzw = ((float4)(v4.zzzz * c[a0 + 98] + R3)).xyzw;
    // 16: ARL
    a0 = (int)floor((R0.wwww).x);
    // 17: MAD
    R0.x = ((float4)(R1.xxxx * R11.xxxx + -c[18].yyyy)).x;
    // 18: MAD
    R11.xyzw = ((float4)(v4.wwww * c[a0 + 96] + R10)).xyzw;
    // 19: MAD
    R2.xyzw = ((float4)(v4.wwww * c[a0 + 97] + R2)).xyzw;
    // 20: MAD
    R3.xyzw = ((float4)(v4.wwww * c[a0 + 98] + R3)).xyzw;
    // 21: DP3
    R10.x = ((float4)(dot((v1.xyzz).xyz, (R11.xyzz).xyz))).x;
    // 22: DP3
    R10.y = ((float4)(dot((v1.xyzz).xyz, (R2.xyzz).xyz))).y;
    // 23: DP3
    R10.z = ((float4)(dot((v1.xyzz).xyz, (R3.xyzz).xyz))).z;
    // 24: DP4
    R4.x = ((float4)(dot(v0, R11))).x;
    // 25: DP3
    R5.w = ((float4)(dot((R10.xyzz).xyz, (R10.xyzz).xyz))).w;
    // 26: DP4
    R4.y = ((float4)(dot(v0, R2))).y;
    // 27: DP4 + RSQ
    mac = dot(v0, R3);
    ilu = rsqrt(abs((R5.wwww).x));
    R4.w = mac.w;
    R1.w = ilu.w;
    // 28: MUL
    R0.x = ((float4)(R0.xxxx * c[18].zzzz)).x;
    // 29: MUL
    R10.xyz = ((float4)(R10.xyzz * R1.wwww)).xyz;
    // 30: DP3
    R11.x = ((float4)(dot((R10.xyzz).xyz, (c[10].xyzz).xyz))).x;
    // 31: DP3
    R11.y = ((float4)(dot((R10.xyzz).xyz, (c[11].xyzz).xyz))).y;
    // 32: DP3
    R11.z = ((float4)(dot((R10.xyzz).xyz, (c[12].xyzz).xyz))).z;
    // 33: DP3
    R11.w = ((float4)(dot((R10.xyzz).xyz, (c[13].xyzz).xyz))).w;
    // 34: DP4
    R12.w = ((float4)(dot(R4.xywz, c[9]))).w;
    // 35: MAX
    R11.xyzw = ((float4)(max(R11, c[1].xxxx))).xyzw;
    // 36: MAD
    R0.x = ((float4)(R0.xxxx * c[18].wwww + c[18].xxxx)).x;
    // 37: DP4 + RCC
    mac = dot(R11, c[14]);
    ilu = nv_rcc((R12.wwww).x);
    R6.x = mac.x;
    R1.x = ilu.x;
    // 38: DP4
    R6.y = ((float4)(dot(R11, c[15]))).y;
    // 39: DP4
    R6.z = ((float4)(dot(R11, c[16]))).z;
    // 40: DP4
    R12.x = ((float4)(dot(R4.xywz, c[6]))).x;
    // 41: DP4
    R12.y = ((float4)(dot(R4.xywz, c[7]))).y;
    // 42: DP4
    R12.z = ((float4)(dot(R4.xywz, c[8]))).z;
    // 43: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R0.xxxx)).xyzw;
    // 44: ADD
    oD0.xyz = ((float4)(R6.xyzz + c[17].xyzz)).xyz;
    // 45: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
