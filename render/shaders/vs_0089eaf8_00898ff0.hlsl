// Vertex program 0089eaf8 (20 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: cdWorldBlend_InitVertexShaders[1].
// With vertex declaration 00898ff0: v5 packed normals (NORMPACKED3), given as their bytes.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2, float4 v3 : TEXCOORD3, float4 v4 : TEXCOORD4, float4 v5 : TEXCOORD5) {
    v5 = nv_normpacked3(v5);
    float4 R0;
    float4 R1;
    float4 R2;
    float4 R3;
    float4 R11;
    float4 R12;
    float4 oD0;
    oD0.xy = float2(0, 0);
    float4 oD1 = float4(0, 0, 0, 1);
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1;
    oT1.zw = float2(0, 1);
    float4 oT2;
    oT2.zw = float2(0, 1);
    float4 oT3;
    float4 oFog;
    float4 mac;
    float4 ilu;

    // 0: ADD
    R11.xyz = ((float4)(v0.xyzz + -c[17].xyzz)).xyz;
    // 1: MOV
    R3.xyz = ((float4)(v0.xyzz)).xyz;
    // 2: DP3 + MOV
    mac = dot((R11.xyzz).xyz, (R11.xyzz).xyz);
    ilu = v1.xyyy;
    R11.x = mac.x;
    oT0.xy = ilu.xy;
    // 3: DP4
    R12.w = ((float4)(dot(v0, c[10]))).w;
    // 4: DP4 + RSQ
    mac = dot(v0, c[7]);
    ilu = rsqrt(abs((R11.xxxx).x));
    R12.x = mac.x;
    R1.x = ilu.x;
    // 5: DP4 + RCC
    mac = dot(v0, c[8]);
    ilu = nv_rcc((R12.wwww).x);
    R12.y = mac.y;
    R1.y = ilu.y;
    // 6: MAD
    R0.x = ((float4)(R1.xxxx * R11.xxxx + -c[16].yyyy)).x;
    // 7: DP4
    R12.z = ((float4)(dot(v0, c[9]))).z;
    // 8: MUL + MOV
    mac = R0.xxxx * c[16].zzzz;
    ilu = v2.xyyy;
    R0.x = mac.x;
    oT1.xy = ilu.xy;
    // 9: MUL
    R2.x = ((float4)(v5.zzzz * c[15].xxxx)).x;
    // 10: MAD
    R0.x = ((float4)(R0.xxxx * c[16].wwww + c[16].xxxx)).x;
    // 11: ADD
    oFog.xyzw = ((float4)(c[1].zzzz + -R0.xxxx)).xyzw;
    // 12: MOV
    oT2.xy = ((float4)(v3.xyyy)).xy;
    // 13: MOV
    oD0.w = ((float4)(v4.wwww)).w;
    // 14: MAX
    oD0.z = ((float4)(max(R2.xxxx, c[1].xxxx))).z;
    // 15: DPH
    oT3.x = ((float4)(dot((R3).xyz, (c[11]).xyz) + (c[11]).w)).x;
    // 16: DPH
    oT3.y = ((float4)(dot((R3).xyz, (c[12]).xyz) + (c[12]).w)).y;
    // 17: DPH
    oT3.z = ((float4)(dot((R3).xyz, (c[13]).xyz) + (c[13]).w)).z;
    // 18: DPH
    oT3.w = ((float4)(dot((R3).xyz, (c[14]).xyz) + (c[14]).w)).w;
    // 19: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.yyyy)).xyz;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
