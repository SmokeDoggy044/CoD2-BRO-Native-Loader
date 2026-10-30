// Vertex program 0089a4a8 (35 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdGlass_InitVertexShaders[3].
// With vertex declaration 00897fb0: v3, v4 packed normals (NORMPACKED3), given as their bytes.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v3 : TEXCOORD3, float4 v4 : TEXCOORD4) {
    v3 = nv_normpacked3(v3);
    v4 = nv_normpacked3(v4);
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
    R4.xyz = ((float4)(c[15].xyzz + -v0.xyzz)).xyz;
    // 1: ADD
    R11.xyz = ((float4)(v0.xyzz + -c[26].xyzz)).xyz;
    // 2: DP3 + MOV
    mac = dot((R4.xyzz).xyz, (R4.xyzz).xyz);
    ilu = v1.xyyy;
    R5.w = mac.w;
    oT0.xy = ilu.xy;
    // 3: DP3 + MOV
    mac = dot((R11.xyzz).xyz, (R11.xyzz).xyz);
    ilu = c[1].zzzz;
    R11.x = mac.x;
    oD0.w = ilu.w;
    // 4: DP3 + RSQ
    mac = dot((v3.xyzz).xyz, (c[17].xyzz).xyz);
    ilu = rsqrt(abs((R5.wwww).x));
    R0.x = mac.x;
    R1.w = ilu.w;
    // 5: DP3 + RSQ
    mac = dot((v3.xyzz).xyz, (c[18].xyzz).xyz);
    ilu = rsqrt(abs((R11.xxxx).x));
    R0.y = mac.y;
    R1.x = ilu.x;
    // 6: MUL
    R6.xyz = ((float4)(R4.xyzz * R1.wwww)).xyz;
    // 7: ADD
    R7.xyz = ((float4)(R6.xyzz + -c[14].xyzz)).xyz;
    // 8: DP3
    R0.z = ((float4)(dot((v3.xyzz).xyz, (c[19].xyzz).xyz))).z;
    // 9: DP3
    R0.w = ((float4)(dot((v3.xyzz).xyz, (c[20].xyzz).xyz))).w;
    // 10: DP3
    R8.w = ((float4)(dot((R7.xyzz).xyz, (R7.xyzz).xyz))).w;
    // 11: MAX
    R0.xyzw = ((float4)(max(R0, c[1].xxxx))).xyzw;
    // 12: MOV + RSQ
    mac = v4.xyzz;
    ilu = rsqrt(abs((R8.wwww).x));
    R10.xyz = mac.xyz;
    R1.w = ilu.w;
    // 13: MAD
    R11.x = ((float4)(R1.xxxx * R11.xxxx + -c[25].yyyy)).x;
    // 14: MUL
    R9.xyz = ((float4)(R7.xyzz * R1.wwww)).xyz;
    // 15: MUL
    R11.yzw = ((float4)(v3.zzxy * R10.xxyz)).yzw;
    // 16: MUL
    R11.x = ((float4)(R11.xxxx * c[25].zzzz)).x;
    // 17: DP4
    R12.w = ((float4)(dot(v0, c[9]))).w;
    // 18: DP3
    R3.x = ((float4)(dot((v3.xyzz).xyz, (-c[14].xyzz).xyz))).x;
    // 19: MAD
    R11.yzw = ((float4)(-R10.zzxy * v3.xxyz + R11.yyzw)).yzw;
    // 20: MAD
    R11.x = ((float4)(R11.xxxx * c[25].wwww + c[25].xxxx)).x;
    // 21: DP4 + RCC
    mac = dot(v0, c[6]);
    ilu = nv_rcc((R12.wwww).x);
    R12.x = mac.x;
    R1.x = ilu.x;
    // 22: DP4
    R12.y = ((float4)(dot(v0, c[7]))).y;
    // 23: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 24: DP4
    R2.x = ((float4)(dot(R0, c[21]))).x;
    // 25: DP4
    R2.y = ((float4)(dot(R0, c[22]))).y;
    // 26: DP4
    R2.z = ((float4)(dot(R0, c[23]))).z;
    // 27: SGE
    R3.x = ((float4)(float4(R3.xxxx >= c[1].xxxx))).x;
    // 28: DP3
    R0.x = ((float4)(dot((R9.xyzz).xyz, (v4.xyzz).xyz))).x;
    // 29: DP3
    R0.y = ((float4)(dot((R9.xyzz).xyz, (R11.wyzz).xyz))).y;
    // 30: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R11.xxxx)).xyzw;
    // 31: ADD
    oD0.xyz = ((float4)(R2.xyzz + c[24].xyzz)).xyz;
    // 32: MUL
    oD1.xyzw = ((float4)(R3.xxxx * c[16].xxxx)).xyzw;
    // 33: MAD
    oT1.xy = ((float4)(R0.xyyy * c[1].yyyy + c[1].yyyy)).xy;
    // 34: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
