// Vertex program 008a3a80 (25 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: nglInitShaderConstantTable.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1) {
    float4 R0;
    float4 R1;
    float4 R11;
    float4 R12;
    float4 oD0;
    float4 oD1;
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1;
    oT1.zw = float2(0, 1);
    float4 oT2;
    oT2.zw = float2(0, 1);
    float4 oT3;
    oT3.zw = float2(0, 1);
    float4 oFog;
    oFog.x = 0;
    float4 mac;
    float4 ilu;
    int a0 = 0;

    // 0: ARL
    a0 = (int)floor((c[5].xxxx).x);
    // 1: NOP
    // 2: DP4
    R12.w = ((float4)(dot(v0, c[a0 + 99]))).w;
    // 3: DP4
    R12.x = ((float4)(dot(v0, c[a0 + 96]))).x;
    // 4: DP4 + RCC
    mac = dot(v0, c[a0 + 97]);
    ilu = nv_rcc((R12.wwww).x);
    R12.y = mac.y;
    R1.x = ilu.x;
    // 5: DP4
    R12.z = ((float4)(dot(v0, c[a0 + 98]))).z;
    // 6: ARL
    a0 = (int)floor((c[5].yyyy).x);
    // 7: MUL
    R12.xyz = ((float4)(R12.xyzz * R1.xxxx)).xyz;
    // 8: DP3
    R0.x = ((float4)(dot((c[a0 + 98].xyzz).xyz, (-v1.xyzz).xyz))).x;
    // 9: DP3
    R0.y = ((float4)(dot((c[a0 + 101].xyzz).xyz, (-v1.xyzz).xyz))).y;
    // 10: DP3
    R0.z = ((float4)(dot((c[a0 + 104].xyzz).xyz, (-v1.xyzz).xyz))).z;
    // 11: DP3
    R0.w = ((float4)(dot((c[a0 + 107].xyzz).xyz, (-v1.xyzz).xyz))).w;
    // 12: DP4
    R11.x = ((float4)(dot(v0, c[a0 + 96]))).x;
    // 13: MAX
    R0.xyzw = ((float4)(max(R0.xzwy, c[1].xxxx))).xyzw;
    // 14: DP4 + MOV
    mac = dot(v0, c[a0 + 97]);
    ilu = R0.xxyx;
    R11.y = mac.y;
    oD0.xyzw = ilu.xyzw;
    // 15: DP4 + MOV
    mac = dot(v0, c[a0 + 99]);
    ilu = R0.xxzw;
    R0.x = mac.x;
    oD1.xyzw = ilu.xyzw;
    // 16: DP4
    R0.y = ((float4)(dot(v0, c[a0 + 100]))).y;
    // 17: DP4
    R0.z = ((float4)(dot(v0, c[a0 + 102]))).z;
    // 18: DP4
    R0.w = ((float4)(dot(v0, c[a0 + 103]))).w;
    // 19: DP4
    R11.z = ((float4)(dot(v0, c[a0 + 105]))).z;
    // 20: DP4
    R11.w = ((float4)(dot(v0, c[a0 + 106]))).w;
    // 21: MUL
    oT0.xy = ((float4)(R11.xyyy * c[a0 + 98].wwww)).xy;
    // 22: MUL
    oT1.xy = ((float4)(R0.xyyy * c[a0 + 101].wwww)).xy;
    // 23: MUL
    oT2.xy = ((float4)(R0.zwww * c[a0 + 104].wwww)).xy;
    // 24: MUL
    oT3.xy = ((float4)(R11.zwww * c[a0 + 107].wwww)).xy;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
