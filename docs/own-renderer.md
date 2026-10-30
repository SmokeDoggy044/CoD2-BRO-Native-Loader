# Our own renderer

Goal (user, 2026-10-28): the native loader gets a modern renderer that does not rely on
translating Xbox graphics code. The decompiled renderer C stops calling the Xbox graphics
libraries (D3D8, D3DX, XGraphics: 348 accesses, [renderer-inventory.md](renderer-inventory.md))
and calls a small interface of our own; back ends: D3D9 first, then D3D11, Vulkan, D3D12.
The 60 fps patch is not carried over: native frame pacing (stage 3) replaces it.

## Steps

**R1 - the renderer module runs as C (Replaced), 2026-10-30.** 1086 renderer functions run
instead of the originals. Kept original: the 8 live ones without a conclusive test
(nglSkipList_RemoveTexture, nglSkipList_RemoveIndirect, nglFindFontAddRef, FxPool_SetSlotTag,
XModel_GetBoneCount, XModel_CalcBoneChildren, FxComponentB_IsOwnerGone, FXCmd_Type2AtTransform).
- The loader's patches into renderer code go through `game::renderer_pc_hooks()` (ngl.h)
  instead: wider-than-16:9 scene aspect, the gun scene's fov, the frame end's wait (no spin;
  applies the field of view). All unset in --compare runs.
- 30 fps until stage 3 (user, 2026-10-30): the frame rate patch is not installed while the
  renderer is C; `--fps 60` logs that 60 fps comes back with our own frame loop.
- `scene:` forced calls are ignored while R_RenderView is C (`XBCMP_ORIGINALS=R_RenderView`).
- Found by the first live run: GAME_GAP_ESP's scratch landed on a caller's live C frame when an
  entry was reached from a callee at the original's position (R_AddEntityModel_entry under
  R_RenderView). The floor's limit is now kept in FS:[0x14] and the scratch stays below it
  (title_data.h). `tools/bisect_replaced.py` finds the Replaced function behind a failure.

**R2 - the interface and a D3D9 back end, 2026-10-30.** `render::GfxDevice` (render/gfx_device.h,
declarations only): textures (2D, cube, volume; linear data in our own formats), vertex/index
buffers (static, dynamic with Discard/NoOverwrite), shaders (bytecode per back end, `ShaderCode`),
vertex layouts (inputs 0-15 = HLSL TEXCOORDn, as the NV2A's v0-v15), render targets, state as
whole blocks (blend + alpha test, depth/stencil, raster + polygon offset, sampler per slot, fog,
viewport, scissor), draws (`draw`, `draw_indexed`, `draw_user` from caller memory; quads drawn as
triangles) and `read_target` for screenshot diffs. Sizes are the title's pixels; render scale,
display shape and anisotropic filtering are the back end's business. Reserved constants the back
end fills each draw: vertex c192-c193 screen-to-clip of the current viewport (the NV2A programs
output screen space; pixel centres and render scale included), c194 fog; pixel c18-c25 texel
scale per slot (the title's linear textures), c26 fog colour.
- Back end: render/gfx_d3d9.cpp on the D3D9Draw of the current path: same device, same targets
  (by Xbox address; ours get keys above title memory), same texture cache. Every draw sets its
  whole state, as the Xbox path does, so the two mix in one frame. Draws that write depth count
  for the flat-frame test (pillarboxing).
- Transition: `xbox_texture(header, palette)` gives an id for a title texture, decoded and kept
  current by the Xbox path's cache (sharp-text replacements and PC filtering included);
  `TargetDesc::xbox_data` makes our target the title's surface at that address.
- Presentation stays with `Backend::present` (device.cpp's Swap) until R4's last part.
- The game library gets the device through `game::gfx()` (ngl.h; the loader sets
  `game::set_gfx_device(&render::gfx_device)`); null before the title created its device and
  in test processes.
- Test: tests/gfx_device_test (hidden window, real device; skipped without one): exact pixel
  coverage at render scales 1 and 2, viewport, scissor, a texture through one of our targets,
  blending, indexed buffers, depth.
- Not done yet: what R4 needs as it goes (UI narrowing for converted UI draws, frame dump of
  GfxDevice draws, freeing our depth targets before shutdown).

**R3 - shaders.** A tool translates every vertex microcode program and register-combiner setup
the shader classes load (their Init tables; the boot-assembled ones from their source text) once
into readable HLSL files, committed; compiled to D3D9 bytecode at build time with vkd3d-shader
(user, 2026-10-30).

**R4 - the C onto the interface, one draw path at a time**, with screenshot diffs against the
current path: text and quads, meshes (incl. recorded push buffers), fog, render-target mips, FX,
debug lines; then device setup and presentation. Done when no renderer C touches NV2A or D3D8.
