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

**R3 - shaders, 2026-10-30.** Every shader program the renderer sets up is HLSL of our own in
render/shaders (126 files: 44 vertex programs, 82 register-combiner setups; manifest.csv says which
C function sets each up), compiled at build time with vkd3d-shader (`vkd3d-compiler`, required;
render/CMakeLists.txt) and embedded; `GfxDevice::create_title_shader(stage, key)` makes one.
- Key: the title data naming the program: the microcode blob or D3DPIXELSHADERDEF address the
  shader class's Init passes, or for the four screen-space shaders assembled at boot the address
  of their source text. All 126 are set up at boot (the same set in missions 4 and 6).
- Made by `tools/shader_hlsl.py DUMP_DIR` from what the loader records with `--dump-shaders DIR`
  (a hook in nglVertexShader_Init / nglPixelShader_Init / the assemblers,
  RendererPcHooks::shader_loaded). Files are generated: change the tool and run it again.
- The translation mirrors render/d3d9_shader.cpp (the Xbox path's, checked in game): one HLSL
  statement per NV2A instruction or combiner term, with the instruction named in a comment.
  What that translation took from the draw-time state are reserved constants now: the fog mode
  (vertex c194.z, `Fog::mode`) and alpha kill per stage (pixel c27, `SamplerState::alpha_kill`;
  as on the Xbox path it also discards on stages without a texture).
- Size: vkd3d's shader model 3 code is unoptimized; the temporaries are only zeroed where a program
  reads them before writing, which keeps the largest vertex program at 404 instructions (512 is
  vs_3_0's minimum). Pixel shaders: at most 185.
- Test: tests/title_shader_test (XB_SHADER_DUMP=DIR; skipped without it) draws every shader and the
  Xbox path's translation of the same program with random inputs on a real device and compares
  the pixels: pixel shaders with alpha kill off and on, vertex programs output by output (colours,
  texture coordinates, fog in all four modes). All 126 identical (2026-10-30).
- Not covered: draws without a pixel shader (the fixed-function combiners D3D9Draw builds from
  texture stage state) and without a vertex program; R4 adds what those draws need.

**R4a - the renderer C's state in our own terms, 2026-10-30.** Every Xbox graphics call of the
renderer C that sets state goes through a wrapper in `game::xb` (game/code/ngl_gfx.cpp, 160 sites
rewritten): it still makes the Xbox call (the Xbox path draws what is not ours yet) and records
what it means in a `render::GfxState` (render/gfx_state.h: blend and alpha test, depth/stencil,
raster, fog, samplers, textures and palettes, shaders with their render/shaders keys, vertex and
pixel constants, render target, viewport). `game::ngl_gfx_state()` returns it.
- `XB_GFX_VERIFY=1` compares it with the Xbox path's state before every one of its draws
  (render/gfx_verify.cpp) and logs each field that differs, plus a coverage summary. Missions 4,
  1 and 6 (100 s each, 0.3-1.75 million draws) and the front-end menus: no difference in any
  field. Not exercised there: stencil, fixed-function draws (every draw had a pixel shader).
- Found on the way: `nglSetTextureAddressMode` writes the stage's mag/min/mip filters (and max
  anisotropy), not its address modes (worth a rename).

**R4b - every draw of the renderer on our interface, 2026-10-30.** Text, 2D quads, the
DrawVerticesUP draws (render-target mips, both fog passes, debug lines), meshes (DrawVertices /
DrawIndexedVertices from the title's vertex buffers), quad batches (particles, FX) and clears go
through `game::ngl_gfx_draw_user` / the `game::xb` draw wrappers onto GfxDevice with the recorded
state. Missions 4, 1, 6 and the front end: no draw reaches the Xbox path any more.
- Vertices stay in title memory for now: `draw_user` takes a cache key (the title buffer range);
  the D3D9 back end hashes the draw's bytes and draws from a kept copy while they stay the same
  (made the second time the same contents come), else streams them.
- Indexed draws pass the vertex range the indices use, the indices rebased; indexed quads become
  triangles.
- Packed normals (NORMPACKED3, 20 declaration inputs) are unpacked by the vertex shader: the
  shader tool makes a variant of a program per declaration that gives it packed normals
  (`vs_KEY_DECLARATION.hlsl`, 22), fed the raw bytes (UByte4). PBYTE4 is UByte4N.
- The screen-space shaders (text, quads, mips, fog: nglInitShaderConstantTable) are in
  render/shaders too: 144 programs + 22 variants, all identical to the Xbox path's translation
  (tests/title_shader_test, variants fed packed bytes against the decoded floats).
- Wide pictures: the D3D9 back end narrows UI drawn over a 3D scene like the Xbox path
  (BackendConfig::narrow_ui_draw, x range of the positions).
- Checks: `XB_DRAWS_ALTERNATE=1` draws on every other frame through each path; paused scenes
  in missions 4, 1 and 6 (world, FX, fog, menu) are pixel-identical between them.
  `XB_XBOX_DRAWS=1` keeps every draw on the Xbox path. Frame time: the same (game thread 46.6%
  busy vs 46.1% at 30 fps; hashing the vertices replaces the Xbox path's conversion).
- Still on the Xbox side: the YUV quads (movie frames; not drawn in these runs), recorded push
  buffers (none seen), the textures (read from title memory through the Xbox path's cache),
  device creation and presentation (device.cpp, Backend::present), and the Xbox state calls the
  wrappers still make (they keep XB_XBOX_DRAWS working; they go with the D3D8 library).

**Enhanced graphics, 2026-10-30 (user: switchable improvements like enhanced sound; for review).**
One master switch, Original (the picture as on the Xbox, at the PC's resolution) or Enhanced,
and each improvement on its own switch; amounts default to neutral (0) so players choose them
(the enhanced-sound rule). `--graphics original|enhanced` (settings.ini `graphics`; original
with --pc-patches off), `--graphics-features "K=V ..."` (settings.ini `graphics-features`,
render/enhanced_graphics.h), the launcher's Graphics tab, the PC menu's Graphics tab (live,
saved on closing).
- Anti-aliasing (`fxaa`, on): render/post/fxaa.hlsl, edges by luma contrast blended along
  their direction (FXAA 3 console idea). Frames with a 3D scene only, so menus stay crisp; the
  in-game HUD gets it too (a later refinement: run it before the HUD).
- Anisotropic filtering (`anisotropic`, on): the existing level (--anisotropy, launcher's Texture
  filtering) now only with enhanced graphics.
- Sharpening (`sharpen` + `sharpness` 0..1, default 0): render/post/sharpen.hlsl, contrast
  adaptive (CAS idea): strong in flat areas, weak at hard edges.
- Colour vibrance (`vibrance` + `vibrance_amount` -1..1, default 0): weak colours raised more
  than strong ones.
- Texture detail (`texture_detail` + `lod_bias` -2..0, default 0): mipmap bias of our draws.
- The post passes (render/d3d9_post.cpp) run on the back buffer in `Backend::finish_picture()`
  (before screenshots and the brightness pass), shaders compiled with vkd3d like the title's
  (render/post, cmake/embed_named_shaders.cmake).

**R4 - the C onto the interface, one draw path at a time**, with screenshot diffs against the
current path: text and quads, meshes (incl. recorded push buffers), fog, render-target mips, FX,
debug lines; then device setup and presentation. Done when no renderer C touches NV2A or D3D8.
