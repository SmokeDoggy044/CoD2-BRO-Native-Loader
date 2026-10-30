// Shared by the translated shaders (tools/shader_hlsl.py; generated, do not edit).
// Constant registers: see render/gfx_device.h. Every reserved register is declared, so the
// compiler's own literal constants land above them.

#ifdef VERTEX_SHADER
float4 c[192] : register(c0);              // the NV2A's constant slots
float4 screen_to_clip[2] : register(c192);  // clip.xyz = screen.xyz * [0].xyz + [1].xyz
float4 fog_params : register(c194);         // bias, scale, mode (0 off, 1 linear, 2 exp, 3 exp2)

struct VsOut {
    float4 pos : POSITION;
    float4 d0 : COLOR0;
    float4 d1 : COLOR1;
    float4 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
    float4 t2 : TEXCOORD2;
    float4 t3 : TEXCOORD3;
    float fog : FOG;
};

// ILU EXP and LOG, like vs_1_1's expp/logp.
float4 nv_exp(float s) { return float4(exp2(floor(s)), frac(s), exp2(s), 1); }
float4 nv_log(float s) {
    const float l = log2(abs(s));
    const float e = floor(l);
    return float4(e, abs(s) * exp2(-e), l, 1);
}
// RCC: the reciprocal clamped to +-1.884467e19.
float4 nv_rcc(float s) { return clamp(1 / s, -1.884467e19, 1.884467e19); }
float4 nv_dst(float4 a, float4 b) { return float4(1, a.y * b.y, a.z, b.w); }

// NORMPACKED3 (11:11:10 signed normalized x, y, z), given as its four bytes (UBYTE4): (x, y, z, 1).
float4 nv_normpacked3(float4 b) {
    const float x = b.x + (b.y - 8 * floor(b.y / 8)) * 256;
    const float y = floor(b.y / 8) + (b.z - 64 * floor(b.z / 64)) * 32;
    const float z = floor(b.z / 64) + b.w * 4;
    return float4(max((x >= 1024 ? x - 2048 : x) / 1023, -1), max((y >= 1024 ? y - 2048 : y) / 1023, -1),
                  max((z >= 512 ? z - 1024 : z) / 511, -1), 1);
}

// Screen space (title pixels, depth buffer units) -> clip space: w' = w, or 1 where w is 0.
VsOut nv_epilogue(float4 pos, float4 d0, float4 d1, float4 t0, float4 t1, float4 t2, float4 t3, float fog) {
    VsOut o;
    const float w = pos.w == 0 ? 1 : pos.w;
    o.pos = float4((pos.xyz * screen_to_clip[0].xyz + screen_to_clip[1].xyz) * w, w);
    o.d0 = d0;
    o.d1 = d1;
    o.t0 = t0;
    o.t1 = t1;
    o.t2 = t2;
    o.t3 = t3;
    // The fog factor from the distance (xemu's formulas; D3D picks the parameters).
    const float d = fog * fog_params.y;
    if (fog_params.z == 1)
        o.fog = fog_params.x + d - 1;
    else if (fog_params.z == 2)
        o.fog = fog_params.x + exp2(16 * d) - 1.5;
    else if (fog_params.z == 3)
        o.fog = fog_params.x + exp2(-32 * d * d) - 1.5;
    else
        o.fog = 1;
    return o;
}
#endif

#ifdef PIXEL_SHADER
float4 c[18] : register(c0);              // c0-c7 each stage's C0, c8-c15 C1, c16 c17 the final C0 C1
float4 texcoord_scale[8] : register(c18);  // per texture slot: texel -> normalized (linear textures)
float4 fog_color : register(c26);
float4 alpha_kill : register(c27);          // per stage (x-w): 1 = discard where its texture's alpha is 0

// Combiner input mappings (PS_INPUTMAPPING).
#define unsigned_identity(x) max(x, 0)
#define unsigned_invert(x) (1 - saturate(x))
#define expand_normal(x) (2 * max(x, 0) - 1)
#define expand_negate(x) (-2 * max(x, 0) + 1)
#define halfbias_normal(x) (max(x, 0) - 0.5)
#define halfbias_negate(x) (-max(x, 0) + 0.5)
#endif
