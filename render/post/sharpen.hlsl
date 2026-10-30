// Sharpening and colour vibrance (enhanced graphics' "sharpen", "vibrance"). Sharpening adapts to
// the local contrast like AMD's FidelityFX CAS (written here from its description): strong where
// the neighbourhood is flat, weak at hard edges, so it does not ring. Vibrance raises the
// saturation of weakly coloured pixels more than of strong ones (negative: greyer).
// c0: one texel (xy), sharpness 0..1 (z), vibrance -1..1 (w).

sampler2D picture : register(s0);
float4 params : register(c0);

float3 at(float2 uv) { return tex2Dlod(picture, float4(uv, 0, 0)).rgb; }

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    const float2 t = params.xy;
    float3 c = at(uv);
    if (params.z > 0) {
        const float3 n = at(uv + float2(0, -t.y)), s = at(uv + float2(0, t.y));
        const float3 w = at(uv + float2(-t.x, 0)), e = at(uv + float2(t.x, 0));
        const float3 lo = min(c, min(min(n, s), min(w, e)));
        const float3 hi = max(c, max(max(n, s), max(w, e)));
        // How far the neighbourhood is from clipping, as a weight (CAS's amplitude).
        const float3 amplitude = sqrt(saturate(min(lo, 1 - hi) / max(hi, 1.0 / 256)));
        const float3 weight = -amplitude * lerp(1.0 / 8, 1.0 / 5, params.z);
        c = saturate((c + (n + s + w + e) * weight) / (1 + 4 * weight));
    }
    if (params.w != 0) {
        const float grey = dot(c, float3(0.299, 0.587, 0.114));
        const float saturation = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
        c = saturate(lerp(grey.xxx, c, 1 + params.w * (1 - saturation)));
    }
    return float4(c, 1);
}
