// Vertex program 008a36b0 (6 instructions), translated from NV2A microcode by
// tools/shader_hlsl.py; generated, do not edit.
// Used by: nglInitShaderConstantTable.

#define VERTEX_SHADER
#include "nv2a.hlsli"

VsOut main(float4 v0 : TEXCOORD0, float4 v1 : TEXCOORD1, float4 v2 : TEXCOORD2, float4 v3 : TEXCOORD3, float4 v4 : TEXCOORD4) {
    float4 R12;
    float4 oD0 = float4(0, 0, 0, 1);
    float4 oD1 = float4(0, 0, 0, 1);
    float4 oT0;
    float4 oT1;
    float4 oT2;
    float4 oT3;
    float4 oFog;
    oFog.x = 0;

    // 0: MOV
    R12.zw = ((float4)(v0.zzzw)).zw;
    // 1: MOV
    oT0.xyzw = ((float4)(v1)).xyzw;
    // 2: MOV
    oT1.xyzw = ((float4)(v2)).xyzw;
    // 3: MOV
    oT2.xyzw = ((float4)(v3)).xyzw;
    // 4: MOV
    oT3.xyzw = ((float4)(v4)).xyzw;
    // 5: MUL
    R12.xy = ((float4)(v0.xyyy * c[0].zwww)).xy;

    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);
}
