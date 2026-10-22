#pragma once

// Game module: the script math builtins the compiled level scripts call through the script
// interface (cdecl, floats in ST0). Most are x87 code that leaves the result unrounded in
// ST0, so they return long double; the ones that store and reload a float return float.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00011000 / 0x00011020 / 0x00011030: FCOS / FSIN / FPTAN of x (radians).
long double XB_CDECL Scr_Cos(float x);
long double XB_CDECL Scr_Sin(float x);
long double XB_CDECL Scr_Tan(float x);
// 0x00011010: FABS.
long double XB_CDECL Scr_Abs(float x);
// 0x002f62c0: FSQRT.
long double XB_CDECL Scr_Sqrt(float x);
// 0x00013a00: FPATAN of x / 1.
long double XB_CDECL Scr_Atan(float x);
// 0x00014470: the natural logarithm (FYL2X with ln 2).
long double XB_CDECL Scr_Log(float x);
// 0x00013ed0: (a.z * b.z + a.y * b.y) + a.x * b.x on the x87.
long double XB_CDECL Scr_VectorDot(const float* a, const float* b);
// 0x00013ea0 / 0x00013e70: (x * x + y * y) + z * z, and its FSQRT.
long double XB_CDECL Scr_LengthSquared(const float* v);
long double XB_CDECL Scr_Length(const float* v);
// 0x00013ef0: |b - a| (differences stored as floats, __CIsqrt, stored as float).
float XB_CDECL Scr_Distance(const float* a, const float* b);
// 0x00013f50: |b - a|^2 (differences stored as floats, the sum unrounded).
long double XB_CDECL Scr_DistanceSquared(const float* a, const float* b);
// 0x00013fa0: |a - c|^2 > |a - b|^2 (SSE; false when unordered).
uint32_t XB_CDECL Scr_Closer(const float* a, const float* b, const float* c);
// 0x00014030: out = v / |v| (copied first; 1 / FSQRT in SSE). The original uses v's argument
// slot as scratch.
void XB_CDECL Scr_VectorNormalize(float* out, const float* v);
// 0x000140b0: vectoangles(v) into angles.
void XB_CDECL Scr_VectorToAngles(float* angles, const float* v);
// 0x00014440: FastSinCos of degrees * pi / 180 (SSE products).
void XB_CDECL Scr_SinCosDegrees(float degrees, float* sine, float* cosine);
// 0x000142f0: the shared LCG (0x008d5a70) step, (seed >> 17) * n >> 15.
int32_t XB_CDECL Scr_RandomInt(int32_t n);
// 0x00014370: a random int between a and b (either order).
int32_t XB_CDECL Scr_RandomIntRange(int32_t a, int32_t b);
// 0x00014320: (max - 0) * r / 32768 + 0 on the x87, stored as float.
float XB_CDECL Scr_RandomFloat(float max);
// 0x000143b0: a random float between a and b (either order; SSE compare, x87 math).
float XB_CDECL Scr_RandomFloatRange(float a, float b);

// 0x0011a750 (out in ECX, callee pops 4): out = (int)value (truncated), or the script's
// undefined int (0x00922ec0) for the undefined float (0x00922eac). Returns out's value.
int32_t XB_REGS3 XB_STDCALL Scr_FloatToInt(uint32_t, uint32_t, int32_t* out, float value);
// 0x0011b470 (out in ECX, callee pops 4): out = (float)value, or the undefined float for the
// undefined int; also returned in ST0.
float XB_REGS3 XB_STDCALL Scr_IntToFloat(uint32_t, uint32_t, float* out, int32_t value);
// 0x000140d0 / 0x000141e0 / 0x00014150: the up, forward and right vectors of angles (degrees;
// FastSinCos, SSE products). Up and right ignore one angle each: up = (cr sp, -sr, cr cp),
// right = (cr sy, -cr cy, -sr). The original keeps the second cosine in angles' argument slot.
void XB_CDECL Scr_AnglesToUp(float* out, const float* angles);
void XB_CDECL Scr_AnglesToForward(float* out, const float* angles);
void XB_CDECL Scr_AnglesToRight(float* out, const float* angles);

}  // namespace game
