// Vertex program 0089fc58 (24 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdWorld_InitVertexShaders2[0].

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v3 : TEXCOORD3, float4 v4 : TEXCOORD4, float4 v5 : TEXCOORD5) {
    float4 R1;
    float4 R2;
    float4 R3;
    float4 R4;
    float4 R5;
    float4 R6;
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
    oFog.x = 0;
    float4 mac;
    float4 ilu;

    // 0: ADD
    R2.xyz = ((float4)(c[18].xyzz + -v0.xyzz)).xyz;
    // 1: MUL + RCP
    mac = c[18].wwww * c[18].wwww;
    ilu = 1 / (c[18].wwww).x;
    R4.x = mac.x;
    R1.w = ilu.w;
    // 2: DP3
    R3.x = ((float4)(dot((R2.xyzz).xyz, (v3.xyzz).xyz))).x;
    // 3: DP4
    R12.w = ((float4)(dot(v0, c[10]))).w;
    // 4: SGE
    R4.y = ((float4)(float4(R3.xxxx >= c[1].xxxx))).y;
    // 5: ADD
    R4.y = ((float4)(R4.yyyy + -c[6].xxxx)).y;
    // 6: DP4 + RCC
    mac = dot(v0, c[7]);
    ilu = nv_rcc((R12.wwww).x);
    R12.x = mac.x;
    R1.x = ilu.x;
    // 7: MUL
    R4.y = ((float4)(R4.yyyy * c[6].yyyy)).y;
    // 8: MUL
    R3.x = ((float4)(R3.xxxx * R4.yyyy)).x;
    // 9: MAD
    R11.xyz = ((float4)(-R3.xxxx * v3.xyzz + c[18].xyzz)).xyz;
    // 10: ADD
    R4.yzw = ((float4)(v0.yyzx + -R11.yyzx)).yzw;
    // 11: MUL
    R11.y = ((float4)(R3.xxxx * R3.xxxx)).y;
    // 12: ADD
    R4.x = ((float4)(R4.xxxx + -R11.yyyy)).x;
    // 13: MAD
    R6.x = ((float4)(-R3.xxxx * R1.wwww + c[1].zzzz)).x;
    // 14: DP3
    R5.x = ((float4)(dot((v4.xyzz).xyz, (R4.wyzz).xyz))).x;
    // 15: DP3
    R5.y = ((float4)(dot((v5.xyzz).xyz, (R4.wyzz).xyz))).y;
    // 16: MIN + RSQ
    mac = min(R6.xxxx, c[1].zzzz);
    ilu = rsqrt(abs((R4.xxxx).x));
    R6.x = mac.x;
    R1.y = ilu.y;
    // 17: DP4
    R12.y = ((float4)(dot(v0, c[8]))).y;
    // 18: MUL
    R5.xy = ((float4)(R5.xyyy * R1.yyyy)).xy;
    // 19: MAX
    R6.x = ((float4)(max(R6.xxxx, c[1].xxxx))).x;
    // 20: DP4
    R12.z = ((float4)(dot(v0, c[9]))).z;
    // 21: MAD
    oT0.xy = ((float4)(R5.xyyy * c[6].xxxx + c[6].xxxx)).xy;
    // 22: MOV
    oD0.xyzw = ((float4)(R6.xxxx)).xyzw;
    // 23: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
