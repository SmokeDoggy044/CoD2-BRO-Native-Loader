# Renderer: graphics library inventory

Step 1 of our own renderer: everything the decompiled renderer still asks the title's
Xbox graphics libraries to do (D3D8, D3DX, XGraphics). This is what our graphics interface
has to cover before the renderer can run without those libraries.

Regenerate with `tools/d3d_inventory.py`:
- default: every library address in `game/code/*.cpp` -> `tools/data/d3d_calls.csv`
  (file, line, C function, module, address, symbol, call/data), summary per library function;
- `--by-func`: summary per C function;
- `--xbe`: call sites in the original XBE whose function has no C yet (catches what the C
  scan cannot see).

## Totals (2026-10-28)

348 accesses from 90 C functions: 75 distinct library functions called, 46 library data words
written directly. All of it is in the renderer (shaders.cpp, ngl.cpp, r_init.cpp, fx*.cpp,
tr_main.cpp). No other module's C calls the graphics libraries. 243 distinct NV2A method words
(`0x0004xxxx`) appear as constants in the C.

## What the C uses, grouped by what the interface needs

**Device and presentation:** `Direct3D_CreateDevice`, `D3D_SetPushBufferSize`, `D3DDevice_Reset`,
`GetDeviceCaps`, `Swap` (nglEndFrame, nglInit), `PersistDisplay`, `SetGammaRamp`,
`SetVerticalBlankCallback`, `InsertCallback` (nglVBlankCallback), `InsertFence`, block until idle
(0x003e6200, 4 sites), wait on the last fence (0x003e6210). Frame pacing and the vblank
callback belong to stage 3 (native frame rate).

**Render targets and surfaces:** `SetRenderTarget`, `SetRenderTargetFast`, `GetBackBuffer2`,
`GetDepthStencilSurface2`, `D3DSurface_GetDesc`, `D3DSurface_LockRect`, `GetSurfaceLevel2`,
`GetCubeMapSurface2`, `SetViewport`, `Clear`. Render-to-texture with mip generation
(nglGenerateRenderTargetMips), cube faces, and the front buffer read as a texture
(slot 0x00902d0c).

**Resources:** Xbox resources are headers in title memory: `XGSetTextureHeader`,
`XGSetCubeTextureHeader`, `XGSetSurfaceHeader`, `XGSetVertexBufferHeader`, `XGSetPaletteHeader`,
`D3DResource_Register`, `D3DResource_Release`, `BlockUntilNotBusy`, `D3DVertexBuffer_Lock2`
(NOOVERWRITE), `D3DPalette_Lock2`, `D3DTexture_LockRect`, `GetLevelCount`. Texture data is
swizzled by the title (`XGSwizzleRect`, `Swizzler_Init/SwizzleU/SwizzleV`,
`XGBytesPerPixelFromFormat`). `D3DXCreateTextureFromFileInMemory` (nglLoadTextureFile) is the
only D3DX call. Paletted textures (P8) take `SetPalette`.

**Texture binding:** `SetTexture`, `SwitchTexture`, `SetPalette`, `SetTextureState_BorderColor`.
Texture-stage state is written straight into D3D's stage shadows: 0x003f5c68 + stage * 0x80,
about 50 sites.

**Render state:** `SetRenderState_Simple` takes raw NV2A methods (17 sites, 12 functions).
It is joined by 13 named Xbox setters: CullMode, ZEnable, StencilEnable, StencilFail, FillMode,
BackFillMode, FogColor, EdgeAntiAlias, MultiSampleAntiAlias/Mask, LogicOp, YuvEnable,
RopZCmpAlwaysRead. D3D's render-state shadow (0x003f840c-0x003f8514) and dirty flags
(0x003f8320, 41 sites) are written directly by inlined setters.

**Shaders:**
- Vertex: `LoadVertexShaderProgram` (0x003e6ea0), `SelectVertexShaderDirect`, `SetVertexShader`,
  `SetShaderConstantMode`, `SetVertexShaderConstant1Fast`, `SetVertexShaderConstantNotInlineFast`.
- Pixel: `SetPixelShader`, `SetPixelShaderProgram` (register combiners), `SetPixelShaderConstant`.
- Most programs are precompiled microcode and combiner tables in the XBE's data (the shader
  classes' Init tables, nglVertexShader_Init / nglPixelShader_Init). A few screen-space shaders
  are assembled at boot from source text in .rdata (`XGAssembleShader` through
  nglAssemblePixelShader / nglAssembleVertexShader, shaders.cpp).

**Geometry and draws:**
- Streams and draw calls: `SetVertexShaderInputDirect` (stream binding, 12 sites),
  `DrawVertices`, `DrawIndexedVertices`, `DrawVerticesUP`.
- `RunPushBuffer` (nglDrawMeshSection): mesh sections can carry a recorded NV2A push buffer.
- `BeginPush`/`EndPush` with NV2A words written directly: text glyph quads (nglRenderTextNode)
  and nglDrawQuad.

## Not covered by C yet (`--xbe`)

- ~~8 FX draw functions, 0x000a7930-0x000ac3b0~~: done 2026-10-28 (fx_load.cpp `draw_op_quads`, one
  shared body with a per-class layout table; thunks `FxOp*_Draw`, bodies `FxOp*_DrawQuads`). FxOp53, Grid,
  1dd, 9d5 and 15b matched (160-6458 tests each). dd, d5 and 5b are set aside as "no data": their op
  classes' Init never runs in missions 0-9.
- **UI_ShowDiscErrorScreen** (client): the dirty-disc screen draws with D3D surfaces itself.
- **PersistDisplay** before a reboot: Cmd_LaunchMultiplayer_f, Cmd_RelaunchTitle_f,
  FUN_003352d0.
- **Gap function 0x00066b00** (called from 0x0006bf26/32): clears the vertex stream, then
  blocks until idle.
- Inlined `D3DResource_AddRef` copies at 0x00223b72 and 0x00330532. These sit inside the
  Ghidra ranges of Sys_ListFilteredFiles and Profile_ApplySkill, so the function boundaries
  there are probably wrong (gap functions).
- Generic `D3DDevice_SetRenderState` dispatchers: library copies at 0x000453c0 (no direct
  caller) and 0x000a7720. They go away with the library.
- UIX (Xbox Live UI library) calls D3D throughout. Set aside with Live.

Immediate values in the D3D range in other functions are float constants (0x3f800000
misaligned and the like), not references.

## What this means for stage 2

1. **The cut is at NV2A level, not D3D API level.** The C writes NV2A methods
   (`SetRenderState_Simple`, BeginPush) and D3D's shadow tables directly. Our interface should
   take semantic state (blend, depth, stencil, cull, fog, sampler) and draw calls. The C sites
   that write methods or shadows get rewritten to call it. The method knowledge in
   `render/nv2a.*` and `render/state.cpp` is reused as the reference for what each method
   means.
2. **Recorded push buffers** (RunPushBuffer for mesh sections): decode once at load into
   ordinary index buffers. Check first whether they come from level files or are recorded at
   load time.
3. **Shaders:** translate the vertex microcode and combiner programs into HLSL files of our
   own, written once offline. The tools for this are `render/shader.cpp`,
   `render/d3d9_shader.cpp` and `tools/nv2a_vsdis.py`. The shaders assembled at boot have
   source text to work from. The next step is to count the distinct programs.
4. **Resources:** create textures from Xbox format + swizzled memory, reusing
   `render/texture_decode.*`. Vertex buffers stay in title memory, so dynamic buffers
   (Lock2 NOOVERWRITE) need an upload path.
5. **Presentation and pacing:** Swap, vblank callback, InsertCallback and fences become our
   frame loop in stage 3. The 60 fps patch is not carried over.
