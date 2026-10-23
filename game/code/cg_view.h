#pragma once

// cgame view: the first-person gun's sway offsets and kick, shell shock (its cvars,
// screen blur parameters and view kick), the tilted view axis, camera shake slots, the
// cube-map capture views, screen projection of a view direction and bullet tracers.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00275040 (out in ESI): the gun's angular sway by movement speed (0x00973a84 above
// the stance's threshold - weapon +0x56c/570/574 plus 0x0094c088 - scaled into 0..1 up
// to 0x009725a0) times the stance's weights (+0x4f4/518/53c) plus the cvars 0x0094b8a8,
// 0x0094acd0, 0x009497d0; less while aiming (0x009725d8). The current offset
// (0x00972ac0) moves towards it at the weapon's rate (+0x560, +0x564 when 0x009725fc is
// the frame time 0x00972970) plus 0x00948930, at least 0.0001 per ms; added to *out
// (scaled by 1 - 2 * aim while aiming under half).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_CalcGunRotOffset(float* out);
extern "C" void CG_CalcGunRotOffset_entry(void);
// 0x00275380 (out in ESI): the same for the gun's position (0x00972ab4): stances also
// prone-crouched (flag 0x10000; thresholds +0x550..55c, weights +0x4d0/4e8/50c/530,
// cvars 0x0094b428, 0x0094b278, 0x0094c560, threshold 0x00948e40), plus a fixed offset
// (+0x500 or +0x524, and the cvars 0x0094c950, 0x0094af10, 0x00948ed0) while
// 0x009725f8 is 0x00972974 or 0x00972970; rates +0x548/+0x54c plus 0x0094a2a0.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_CalcGunMoveOffset(float* out);
extern "C" void CG_CalcGunMoveOffset_entry(void);
// 0x00276dd0: the gun kick springs (angles 0x00972f88, velocities 0x00972f94) stepped
// in 5 ms pieces of the frame time: pushed back (2400, or the weapon's +0x7ac/+0x7f4
// by aim while 0x009725d0), damped, settled at 0 on a sign change and clamped to 10.
void XB_CDECL CG_UpdateGunKick();
// 0x00276f70 (duration in EAX, shell shock parameters in EDX, elapsed time in EDI): the
// view kick (0x00973a5c/60) from the 128-point noise table 0x0082f4a0 (pairs, cubic
// interpolation at elapsed * +4, offset by duration * 61), times +8 and a smoothstep of
// the remaining fraction of the fade (+0); after the end the scene callback 2 cleared
// and the kick zeroed.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_ShellShockViewKick(int32_t duration, const uint8_t* params,
                                                               int32_t elapsed);
extern "C" void CG_ShellShockViewKick_entry(void);
// 0x002770f0 (out in ESI): the shell shock parameters from their cvars: times in ms
// (rounded, at least 1), the blend factors clamped to 0..1, the sound alias name (14
// characters).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_ReadShellShockCvars(uint8_t* out);
extern "C" void CG_ReadShellShockCvars_entry(void);
// 0x00278ed0 (elapsed in EDX, parameters in ECX): the screen blend (0x00973a58) and the
// two blur values (0x00961778/7c) of a shell shock (+0x68 on): full until the last
// +0x6c ms of `duration`, then faded; off (1, 0, 0) without one or after it.
void XB_REGS3 XB_CDECL CG_ShellShockScreenParams(uint32_t unused, int32_t elapsed, const uint8_t* params,
                                                 int32_t duration);
// 0x00278fa0: the view axis (0x00972bc0) tilted by the shell shock kick (0x00973a5c/60):
// a basis from the normalized {1, kick x, kick y}, its cross with up and their cross
// (__CIsqrt lengths), multiplied into the axis.
void XB_CDECL CG_TiltViewAxis();
// 0x0027d2f0 (slot in EAX): a camera shake slot (start +0, scale +4, duration +8,
// radius +0xc, origin +0x10) while shakes are on (0x009496b4) and it runs: +0x1c the
// strength (scale by remaining time, times the falloff over the radius from the view
// origin), +0x20 the time part. Returns 0 when it does not run.
int32_t XB_REGS1 XB_CDECL CG_CalcCameraShakeSlot(uint8_t* slot);
// 0x0027b3c0: the cube-map capture view: a (size + 2) square refdef (size 0x00971a90),
// the field of view from atan2(size, size + 2) (the title's series), the view origin
// from 0x00972550 (z + 0x009725fc), and the axis of face 0x00971a8c (1..6). Both
// globals are CG_DrawActiveFrame's arguments: reached only by a cube-map capture (not
// tested in --level runs).
void XB_CDECL CG_CalcCubemapViewValues();
// 0x0027ff20 (out x in EDI, out y in ESI): the screen position (-320/-240 scaled by the
// field of view) of the direction at pitch/yaw 0x00972fa0/a4 relative to the view axis;
// 0, 0 when it is behind the view or a field of view is not positive.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_ProjectViewDirToScreen(float* out_x, float* out_y);
extern "C" void CG_ProjectViewDirToScreen_entry(void);
// 0x0027d850 (start in EDX, end in ECX): a tracer quad from start to end facing the view
// (width times 4 with 0x00947df4) through the renderer's polygon call
// (0x00928518, shader 0x00975c4c); nothing when the line points at the view.
void XB_REGS3 XB_CDECL CG_Tracer(uint32_t unused, const float* start, const float* end, float width);

// 0x0027fe10 (origin in EAX, scale in XMM0, cdecl (duration ms, radius)): a camera shake
// into the first of the four slots (0x0097300c, 0x24 bytes) that has not started or has
// ended, else over the weakest one (+0x1c) if the new one is stronger. Needs scale > 0 and
// shakes on (0x009496b4).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_StartShakeCamera(const float* origin, float scale, int32_t duration, float radius);
extern "C" void CG_StartShakeCamera_entry(void);

// 0x0027a7b0 (result in XMM0): the view's field of view: cg_fov (0x0094c3e8) clamped to 1..160,
// narrowed while zooming towards the weapon's zoomed fov (+0x624); with the zoom flash cvar
// (0x009481e4) a short screen fade on zoom state changes; the current weapon's own fov
// (+0x624 fully zoomed, else +0x848) wins when set.
extern "C" PLATFORM_HIDDEN float XB_CDECL CG_GetViewFov();
extern "C" void CG_GetViewFov_entry(void);
// 0x0027fc40: the camera shake: the strongest active shake slot's scale (capped at 1) moves
// the view origin by random offsets and the view angles by three sine waves of cg.time;
// without a shake only a new random phase (0x0097309c).
void XB_CDECL CG_ShakeCamera();

// 0x0027a2b0: the view weapon's sway angles (0x00972c10/0x00972c14) and lag
// (0x00972c24/0x00972c28) following the view's turn since the last frame.
void XB_CDECL CG_CalcWeaponSway();

// 0x0029a230 (src EAX, dst ESI): Q3's PerpendicularVector.
extern "C" PLATFORM_HIDDEN void XB_CDECL PerpendicularVector(const float* src, float* dst);
extern "C" void PerpendicularVector_entry(void);

// 0x002843e0: Q3's CG_CalcFov (refdef fov 0x00972ba0/0x00972ba4, under-water wobble and
// rdflags 0x20, zoom sensitivity 0x00972c38); returns whether the view is under water.
uint32_t XB_CDECL CG_CalcFov();

// 0x00282340: the damage direction indicators.
void XB_CDECL CG_DrawDamageDirectionIcons();

// 0x0027b860: the shell shock sound stopped (mix restored, both looping channels).
void XB_CDECL CG_StopShellShockSound();
// 0x0027e540 (start EAX, duration ESI, cdecl params): runs or ends the shell shock effects.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_UpdateShellShock(int32_t start, int32_t duration, const uint8_t* params);
extern "C" void CG_UpdateShellShock_entry(void);

// 0x0027afd0: Q3's CG_DrawSkyBoxPortal.
void XB_CDECL CG_DrawSkyBoxPortal();

// 0x002873e0: Q3's CG_OffsetThirdPersonView; the C version gets the entry ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_OffsetThirdPersonView(uint32_t entry_esp);
extern "C" void CG_OffsetThirdPersonView_entry(void);

// 0x00286da0: the tank camera (third person behind the turret or down the barrel); the C
// version gets the entry ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_OffsetAltTankCamView(uint32_t entry_esp);
extern "C" void CG_OffsetAltTankCamView_entry(void);

// 0x00013ce0 (angles EDI, right ESI): the right vector of yaw and roll only.
extern "C" PLATFORM_HIDDEN void XB_CDECL AnglesToRightNoPitch(const float* angles, float* right);
extern "C" void AnglesToRightNoPitch_entry(void);
// 0x00281ad0: Q3's CG_OffsetFirstPersonView.
void XB_CDECL CG_OffsetFirstPersonView();

// 0x0027e210 (vector in EBX): rotated by the gun angles 0x00972fcc.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_RotateByGunAngles(float* v);
extern "C" void CG_RotateByGunAngles_entry(void);
// 0x002807a0 (vector in EBX): from view space to the world (view angles and origin).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_ViewToWorld(float* v);
extern "C" void CG_ViewToWorld_entry(void);
// 0x00278350 (origin in ECX): the zoom offset of the gun from the view (0x00972fa8).
void XB_REGS3 XB_CDECL CG_StoreGunOffsetFromView(uint32_t unused_eax, uint32_t unused_edx, const float* origin);
// 0x00282d10 (origin in EAX): the view weapon's origin.
void XB_REGS1 XB_CDECL CG_CalculateWeaponPosition(float* origin);

}  // namespace game
