// gfx_device_test: texture 0 at texcoord0.
sampler2D s0 : register(s0);

float4 main(float4 t0 : TEXCOORD0) : COLOR { return tex2D(s0, t0.xy); }
