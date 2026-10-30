// Vertex program 008a3918 (22 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: nglInitShaderConstantTable.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1) {
    float4 R0;
    float4 R1;
    float4 R10;
    float4 R11;
    float4 R12;
    float4 oD0;
    float4 oD1;
    oD1.xyz = float3(0, 0, 0);
    float4 oT0;
    oT0.zw = float2(0, 1);
    float4 oT1;
    oT1.zw = float2(0, 1);
    float4 oT2;
    oT2.zw = float2(0, 1);
    float4 oT3 = float4(0, 0, 0, 1);
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
    // 11: DP4
    R11.w = ((float4)(dot(v0, c[a0 + 96]))).w;
    // 12: DP4
    R11.y = ((float4)(dot(v0, c[a0 + 97]))).y;
    // 13: DP4
    R11.z = ((float4)(dot(v0, c[a0 + 99]))).z;
    // 14: DP4
    R11.x = ((float4)(dot(v0, c[a0 + 100]))).x;
    // 15: DP4
    R10.w = ((float4)(dot(v0, c[a0 + 102]))).w;
    // 16: DP4
    R10.y = ((float4)(dot(v0, c[a0 + 103]))).y;
    // 17: MAX
    mac = max(R0.xzzy, c[1].xxxx);
    R0.xy = mac.xy;
    oD1.w = mac.w;
    // 18: MUL
    oT0.xy = ((float4)(R11.wyyy * c[a0 + 98].wwww)).xy;
    // 19: MUL
    oT1.xy = ((float4)(R11.zxxx * c[a0 + 101].wwww)).xy;
    // 20: MUL
    oT2.xy = ((float4)(R10.wyyy * c[a0 + 104].wwww)).xy;
    // 21: MOV
    oD0.xyzw = ((float4)(R0.xxyx)).xyzw;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
