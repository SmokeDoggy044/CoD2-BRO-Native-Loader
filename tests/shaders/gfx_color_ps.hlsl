// gfx_device_test: texcoord0 as the colour, through the title shaders' alpha test (nv2a.hlsli).
#define PIXEL_SHADER
#include "nv2a.hlsli"

float4 main(float4 t0 : TEXCOORD0) : COLOR { return nv_alpha_test(t0); }
