# Rendering

## Design (agreed 2026-10-13: HLE to D3D9/DXVK)

`render/` replaces the D3D8 functions; the title's own D3D data stays authoritative
where inlined code uses it:

- Simple render states arrive as NV2A methods (`SetRenderState_Simple`), as do shader
  constants/programs and BeginPush/RunPushBuffer data: all go through `nv2a.cpp`
  (register file, 136 program slots, 192 constants, inline draws).
- Deferred/complex render states and texture stage states live in the title's
  `D3D__RenderState` / `D3D_g_DeferredTextureState` arrays (XDK 5849 indexes in
  `xbox_d3d.h`); texture offset/format per stage in NV2A registers 0x1B00+.
- Shaders: vertex programs are NV2A microcode (assembled at runtime by the game with
  XGAssembleShader), pixel shaders are D3DPIXELSHADERDEF register combiners.
- Back end (`backend.h`): D3D9; dxvk-native has no HLSL compiler, so shaders must be
  emitted as D3D9 bytecode.

## Draw path

Inline primitives (BeginPush) go through `render/draw.cpp` (attributes -> float4,
quads -> triangles) to `render/d3d9_draw.cpp`: NV2A vertex microcode -> vs_3_0 and
register combiners -> ps_3_0 as hand-assembled bytecode (`d3d9_shader.cpp`), textures
decoded by `texture_decode.cpp` (unswizzle, DXT, P8) and cached by address + content
hash, raster state from the NV2A registers. The pixel shader is read from the NV2A
combiner registers (SetPixelShader writes them, like D3D).

Done: fog (deferred fog states -> NV2A fog registers as D3D does), polygon offset,
render targets (D3D9 render target textures keyed by the surface's memory address;
the back buffer is one, copied to the window at present; textures at that address
sample it), texture modes as D3D picks them (PSTextureModes state,
PS_GLOBALFLAGS_TEXMODE_ADJUST) and dependent reads (modes 15/16). The intro movies
play (the video player writes into the back buffer).

Not yet: cube and volume textures (no game file uses them, see
[status.md](status.md)), other texture modes (bump env, dot products),
`RunPushBuffer`, fixed-function vertex (FVF) and texture stages (approximated as
texture0 * diffuse), CPU reads of render targets, Clear with rectangles. Unverified:
half-pixel offset, viewport-as-scissor, EXP/LOG precision.

## Render scale and filtering

`--render-scale X` renders every target at X times its size (default: the window
scale, 2 = 1280x960; above it supersamples, e.g. 4). The title still sees 640x480:
D3D9Draw scales viewport, scissor and the clip offset (pixel centres), depth buffers
are its own, CPU back-buffer writes (movies) are stretched up, screenshots are at the
render size. `--anisotropy N` (default 16) for mipmapped textures. Checked at
1x/2x/4x in missions 1-2 (HUD, glow, cutscenes): same picture, sharper.

## Widescreen

`--aspect 16:9` (the default; `4:3` for the original). The game has its own 16:9
mode, chosen from the console's video settings (XGetVideoFlags bit 0 -> "480P
(640x480) 16x9", `cg_widescreen 1`, `FUN_000649e0`), so the EEPROM says widescreen
and the game renders anamorphically into its 640x480 back buffer: wider view (Hor+)
and a 16:9 HUD. The window is 16:9 and render targets are widened (2.67x2 at scale 2),
so nothing is stretched.

The game only lays out the HUD for 16:9: each HUD component is pulled toward an
anchor (x = 50, 320 or 590 of 640) by hand on `cg_widescreen` (`FUN_00311800`).
Everything else is a 4:3 layout:

- Frames without a 3D scene (menus, loading and briefing screens, movies; no draw
  writes depth, their 2D draws may leave the depth test on) are shown pillarboxed at
  4:3 after 8 such frames in a row, screenshots too.
- In-game menus over the scene (pause, options) are narrowed to 4:3 around the centre
  per draw: `loader/patches.cpp` wraps the in-game UI's screen/menu render calls and
  marks the NGL scenes they draw into; full-width quads (dimming, bars) stay wide.
- The objectives panel (shown while back is held, `FUN_0027e630`) draws into the
  HUD's scene, so it gets a child scene of its own and is narrowed around x = 50 of
  640, the HUD's left anchor.

Checked: missions 1-2, pause menu and its Options, objectives (mission 4, against
`--aspect 4:3`). Not checked: death/mission failed screens, dialogs.

### Wider than 16:9 (experimental)

`--aspect W:H` wider than 16:9 (launcher: 21:9, 32:9, the screen's shape), campaign
only (multiplayer has no HUD manager and stays 16:9). The 3D view takes the picture's
aspect: R_RenderView and nglSetDefaultSceneParams load it with `movss xmm0,[0x0083f254]`
(16/9), pointed at our value instead; the vertical field of view stays (Hor+). Culling
needs nothing: in widescreen CG_CalcFov sets fov_x to 2 atan(16/9) = 121 degrees, wider
than a 32:9 view at the game's fov.

The HUD stays a 16:9 layout narrowed into the middle, which in Hor+ is exactly the
16:9 picture, so 3D-anchored HUD items stay on their targets. Every NGL scene is a
perspective one (`+0x300` = 1, 2D included), so 2D scenes are told apart by who
makes them, like the menus': the HUD manager's draw (vtable 0x0082a5d8 slot 0x14 =
0x00311940, called from the in-game UI's FUN_003154b0; compass, health) and CG_Draw2D
(CG_DrawActive's tail jump at 0x00286c0c; RE_StretchPic adds quads to the current
scene: crosshair, centre prints). The renderer's narrowing takes an anchor and a factor
per draw (`UiNarrowing`): menus 4:3 around the centre, the objectives panel 4:3 around
x = 50 of 640 inside the 16:9 layout, the HUD 16:9; only into the back buffer.

Checked: mission 2 at 21:9 (HUD, objectives, pause menu), mission 1 cutscene; 16:9
unchanged. Not checked: scopes and binoculars (side bars drawn as quads would only
cover the 16:9 part), death/mission failed screens, missions 3+, 32:9 in play.

## Debugging a wrong picture

`XB_DUMP_FRAME=n` logs every draw of frame n (shader keys, combiners, texture
modes/formats, raster states, first vertex) plus each vertex program's microcode
(decode with `tools/nv2a_vsdis.py`); `XB_DUMP_STOP=k` also drops that frame's draws
after the k-th - with `--screenshots` on frame n-1, bisect for the draw that breaks
it. In a `--diag` session, F12 dumps the next frame's draws and F11 shows draws that
need untranslated features in magenta ([playtesting.md](playtesting.md)).

## Field of view and the PC menu

The key below Escape (`` ` ``/`~`) opens the PC menu: Dear ImGui (render's own copy, Direct3D 9
and SDL3 back ends) drawn over the presented frame while the title keeps running; the
title's input is blocked meanwhile (`input::set_game_input_blocked`), and after it closes
port 0 stays neutral until Escape/`` ` ``/Return/Space and the mouse buttons are let go.
Contents: `loader/pc_menu.cpp`; what changed is saved to settings.ini (`fov = N`) on close.

Field of view (campaign; `--fov DEG`, 50-120): cg_fov (default 70, degrees across 4:3,
Hor+ in widescreen), set with Cvar_SetValue at the frame end. Three more places:
- Weapon defs with a fov of their own (`+0x848`: tanks 55, mounted guns), which
  CG_GetViewFov returns instead of cg_fov, are widened by the same factor in
  tan(fov / 2) while cgame draws (CG_Draw2D hook), keeping the original.
- The gun's scene in R_RenderView takes the view's fov_y (`mov ecx,[ebx+0x128]` at
  0x002475de); off the default it reads a value narrowed back by that factor at the
  hip and the view's own when zoomed, so the gun looks as at 70 (at 120 the front hand
  showed under the rifle). One frame late; the original load is back at 70.
- The widescreen culling fov_x (2 atan(16/9), constants loaded at 0x00284727/0x00284735)
  grows when a wide picture at a high fov needs more.

Checked: mission 2 (tank) and mission 4 (rifle, hip and sights) at 50-120, user-tested.
