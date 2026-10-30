// Vertex program 008a3718 (13 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: nglInitShaderConstantTable.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1) {
    float4 R0;
    float4 R1;
    float4 R12;
    float4 oD0;
    oD0.xyz = float3(0, 0, 0);
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
    // 8: DP4
    R0.x = ((float4)(dot(v0, c[a0 + 96]))).x;
    // 9: DP4
    R0.y = ((float4)(dot(v0, c[a0 + 97]))).y;
    // 10: DP3
    R0.z = ((float4)(dot((c[a0 + 98].xyzz).xyz, (-v1.xyzz).xyz))).z;
    // 11: MUL
    oT0.xy = ((float4)(R0.xyyy * c[a0 + 98].wwww)).xy;
    // 12: MAX
    oD0.w = ((float4)(max(R0.zzzz, c[1].xxxx))).w;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
