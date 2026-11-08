// Vertex program 0089fde0 (43 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdWorld_InitVertexShaders2[1].

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v3 : TEXCOORD3, float4 v4 : TEXCOORD4, float4 v5 : TEXCOORD5) {
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
    float4 oD1;
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1;
    oT1.zw = float2(0, 1);
    float4 oT2 = float4(0, 0, 0, 1);
    float4 oT3 = float4(0, 0, 0, 1);
    float4 oFog;
    oFog.x = 0;
    float4 mac;
    float4 ilu;

    // 0: ADD
    R2.xyz = ((float4)(c[18].xyzz + -v0.xyzz)).xyz;
    // 1: ADD
    R11.yzw = ((float4)(c[19].yyzx + -v0.yyzx)).yzw;
    // 2: DP3 + RCP
    mac = dot((R2.xyzz).xyz, (v3.xyzz).xyz);
    ilu = 1 / (c[18].wwww).x;
    R3.x = mac.x;
    R1.w = ilu.w;
    // 3: DP3 + RCP
    mac = dot((R11.wyzz).xyz, (v3.xyzz).xyz);
    ilu = 1 / (c[19].wwww).x;
    R3.y = mac.y;
    R1.x = ilu.x;
    // 4: SGE
    R4.x = ((float4)(float4(R3.xxxx >= c[1].xxxx))).x;
    // 5: SGE
    R4.y = ((float4)(float4(R3.yyyy >= c[1].xxxx))).y;
    // 6: ADD
    R4.x = ((float4)(R4.xxxx + -c[6].xxxx)).x;
    // 7: ADD
    R4.y = ((float4)(R4.yyyy + -c[6].xxxx)).y;
    // 8: MUL
    R4.x = ((float4)(R4.xxxx * c[6].yyyy)).x;
    // 9: MUL
    R4.y = ((float4)(R4.yyyy * c[6].yyyy)).y;
    // 10: MUL
    R3.x = ((float4)(R3.xxxx * R4.xxxx)).x;
    // 11: MUL
    R3.y = ((float4)(R3.yyyy * R4.yyyy)).y;
    // 12: MAD
    R11.xyz = ((float4)(-R3.xxxx * v3.xyzz + c[18].xyzz)).xyz;
    // 13: MAD
    R10.yzw = ((float4)(-R3.yyyy * v3.yyzx + c[19].yyzx)).yzw;
    // 14: ADD
    R4.xyz = ((float4)(v0.xyzz + -R11.xyzz)).xyz;
    // 15: MUL
    R4.w = ((float4)(c[18].wwww * c[18].wwww)).w;
    // 16: MUL
    R11.y = ((float4)(R3.xxxx * R3.xxxx)).y;
    // 17: ADD
    R11.xzw = ((float4)(v0.xxzy + -R10.wwzy)).xzw;
    // 18: MUL
    R10.x = ((float4)(c[19].wwww * c[19].wwww)).x;
    // 19: MUL
    R10.y = ((float4)(R3.yyyy * R3.yyyy)).y;
    // 20: ADD
    R4.w = ((float4)(R4.wwww + -R11.yyyy)).w;
    // 21: MAD
    R6.x = ((float4)(-R3.xxxx * R1.wwww + c[1].zzzz)).x;
    // 22: ADD
    R11.y = ((float4)(R10.xxxx + -R10.yyyy)).y;
    // 23: MAD
    R6.y = ((float4)(-R3.yyyy * R1.xxxx + c[1].zzzz)).y;
    // 24: DP3 + RSQ
    mac = dot((v4.xyzz).xyz, (R4.xyzz).xyz);
    ilu = rsqrt(abs((R11.yyyy).x));
    R5.x = mac.x;
    R1.x = ilu.x;
    // 25: DP3
    R5.y = ((float4)(dot((v5.xyzz).xyz, (R4.xyzz).xyz))).y;
    // 26: MIN + RSQ
    mac = min(R6.xxxx, c[1].zzzz);
    ilu = rsqrt(abs((R4.wwww).x));
    R6.x = mac.x;
    R1.y = ilu.y;
    // 27: DP3
    R5.z = ((float4)(dot((v4.xyzz).xyz, (R11.xwzz).xyz))).z;
    // 28: DP3
    R5.w = ((float4)(dot((v5.xyzz).xyz, (R11.xwzz).xyz))).w;
    // 29: MIN
    R6.y = ((float4)(min(R6.yyyy, c[1].zzzz))).y;
    // 30: DP4
    R12.w = ((float4)(dot(v0, c[10]))).w;
    // 31: MUL
    R5.xy = ((float4)(R5.xyyy * R1.yyyy)).xy;
    // 32: MAX + RCC
    mac = max(R6.xxxx, c[1].xxxx);
    ilu = nv_rcc((R12.wwww).x);
    R6.x = mac.x;
    R1.y = ilu.y;
    // 33: MUL
    R5.zw = ((float4)(R5.zzzw * R1.xxxx)).zw;
    // 34: MAX
    R6.y = ((float4)(max(R6.yyyy, c[1].xxxx))).y;
    // 35: DP4
    R12.x = ((float4)(dot(v0, c[7]))).x;
    // 36: DP4
    R12.y = ((float4)(dot(v0, c[8]))).y;
    // 37: DP4
    R12.z = ((float4)(dot(v0, c[9]))).z;
    // 38: MAD
    oT0.xy = ((float4)(R5.xyyy * c[6].xxxx + c[6].xxxx)).xy;
    // 39: MOV
    oD0.xyzw = ((float4)(R6.xxxx)).xyzw;
    // 40: MAD
    oT1.xy = ((float4)(R5.zwww * c[6].xxxx + c[6].xxxx)).xy;
    // 41: MOV
    oD1.xyzw = ((float4)(R6.yyyy)).xyzw;
    // 42: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.yyyy)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
