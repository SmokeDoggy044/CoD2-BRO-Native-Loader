// Anti-aliasing of the picture (enhanced graphics' "fxaa"): edges found by the luma contrast of a
// pixel's neighbours are blended along their direction (the idea of NVIDIA's FXAA 3 console
// version, written here from its description). c0.xy: one texel.

sampler2D picture : register(s0);
float4 texel : register(c0);

float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    const float2 t = texel.xy;
    const float3 centre = tex2Dlod(picture, float4(uv, 0, 0)).rgb;
    const float nw = luma(tex2Dlod(picture, float4(uv + float2(-0.5, -0.5) * t, 0, 0)).rgb);
    const float ne = luma(tex2Dlod(picture, float4(uv + float2(0.5, -0.5) * t, 0, 0)).rgb);
    const float sw = luma(tex2Dlod(picture, float4(uv + float2(-0.5, 0.5) * t, 0, 0)).rgb);
    const float se = luma(tex2Dlod(picture, float4(uv + float2(0.5, 0.5) * t, 0, 0)).rgb);
    const float m = luma(centre);
    const float lo = min(m, min(min(nw, ne), min(sw, se)));
    const float hi = max(m, max(max(nw, ne), max(sw, se)));
    // Flat areas and faint edges stay as they are.
    if (hi - lo < max(0.0312, hi * 0.125)) return float4(centre, 1);

    // The edge's direction: across the strongest luma change.
    float2 dir = float2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    const float reduce = max((nw + ne + sw + se) * 0.03125, 1.0 / 128);
    const float scale = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * scale, -8, 8) * t;

    const float3 a = 0.5 * (tex2Dlod(picture, float4(uv - dir / 6, 0, 0)).rgb +
                            tex2Dlod(picture, float4(uv + dir / 6, 0, 0)).rgb);
    const float3 b = 0.5 * a + 0.25 * (tex2Dlod(picture, float4(uv - dir / 2, 0, 0)).rgb +
                                       tex2Dlod(picture, float4(uv + dir / 2, 0, 0)).rgb);
    // The wider blend unless it reaches past the local luma range (then across another edge).
    const float lb = luma(b);
    return float4(lb < lo || lb > hi ? a : b, 1);
}
