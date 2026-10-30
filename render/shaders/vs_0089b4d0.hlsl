// Vertex program 0089b4d0 (12 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdDecal_Register.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2) {
    float4 R0;
    float4 R1;
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
    R11.xyz = ((float4)(v0.xyzz + -c[19].xyzz)).xyz;
    // 1: DP4
    R12.w = ((float4)(dot(v0, c[9]))).w;
    // 2: DP3 + MOV
    mac = dot((R11.xyzz).xyz, (R11.xyzz).xyz);
    ilu = v1.xyyy;
    R11.x = mac.x;
    oT0.xy = ilu.xy;
    // 3: DP4 + RCC
    mac = dot(v0, c[6]);
    ilu = nv_rcc((R12.wwww).x);
    R12.x = mac.x;
    R1.x = ilu.x;
    // 4: DP4 + RSQ
    mac = dot(v0, c[7]);
    ilu = rsqrt(abs((R11.xxxx).x));
    R12.y = mac.y;
    R1.y = ilu.y;
    // 5: DP4
    R12.z = ((float4)(dot(v0, c[8]))).z;
    // 6: MAD
    R0.x = ((float4)(R1.yyyy * R11.xxxx + -c[18].yyyy)).x;
    // 7: MOV
    oD0.xyzw = ((float4)(v2)).xyzw;
    // 8: MUL
    R0.x = ((float4)(R0.xxxx * c[18].zzzz)).x;
    // 9: MAD
    R0.x = ((float4)(R0.xxxx * c[18].wwww + c[18].xxxx)).x;
    // 10: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R0.xxxx)).xyzw;
    // 11: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
