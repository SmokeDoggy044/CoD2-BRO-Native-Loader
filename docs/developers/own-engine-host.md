# Own engine host

Phase 0 of the engine improvements: our own executable owns the main loop and the frame;
the route-B loader (XBE mapping, kernel/XAPI imitation, hooks) goes away. Level code stays original x86 from
the player's dump behind the thunk table ([level_boundary.md](../formats/level_boundary.md)).

Steps: 0.1 inventory (this page) · 0.2 untested rows run as C (failures tracked as they appear, targeted
`--compare` runs when needed) · 0.3 host executable, with a record/replay
mode (inputs + seed, compare state and frames between builds) as its check · 0.3b proper widescreen (Hor+ FOV,
edge-anchored HUD incl. the health-bar offset, menus; options in our C - the cgame widescreen/FOV patches died
with the 11-07 switch) · 0.4 native frame pacing together with native keyboard/mouse (input read per rendered
frame, straight into the usercmd) and the mounted-gun drift fix (round instead of truncate in CL_FinishMove's
angle packing, switchable) · 0.5 Vulkan back end with a render graph · 0.6 one settings schema (launcher,
settings.ini, menus) taking over all the switches.

Developer modes for automated checks: record/replay (0.3), fixed capture points per mission (the lab, 1.0a), state
dumps, an object viewer (1.0d).

## Phase 1: graphics assets

Better assets: upscaled textures through a general mods system, model smoothing, normal and height maps.
Texture packs work on D3D9 as well as Vulkan and can be switched off; they use standard PBRify 4x. Mods sit
**outside** the Enhanced master switch (our enhancements without AI-upscaled textures, or the original look with
them, are both possible). `--compare` runs never load mods.

Starting point: `ngl_gfx.cpp` already hashes each title texture's memory (`render::hash_bytes`, texture_hash.h)
and asks `GfxDevice::replacement_texture` (used today for sharp fonts and key prompts; BGRA8 only, box-filtered
mips, swizzled non-palette textures only). The dump has 9,652 texture entries, 3,920 unique, ~274 MB
(`cod_bro/textures/original/manifest.csv`, `tools/extract_textures.py`). The host is a 32-bit process.

- **1.0 developer mode ("the lab": automated tests and comparisons):**
  - 1.0a `tools/lab.py`: fixed capture points per mission (`tools/data/capture_points.csv`, 8 spread frames when a
    mission has none) on the reference recordings, replayed once per variant (NAME="loader options": back end,
    graphics, mods, single switches) with `--replay-no-draw --replay-shots` - only the 120 frames before each picture
    are drawn (`render::g_skip_keeps_uploads`), so a mission takes minutes. Per point vs the baseline: changed
    pixels, PSNR, sharpness ratio, brightness change; `report.html` (side by side + difference),
    `metrics.csv` + a short table for the assistant, `crop` for looking at a detail (pictures judged by numbers
    first: opening one sends it to the API).
  - 1.0b in-game `--dev` (with 1b): overlay (frame time, texture memory, replacements loaded / pending / missing),
    live keys flipping each Enhanced feature and each mod for A/B in place, a texture picker (texture under the
    crosshair: runtime hash, asset path, which mod) written into F12 marks, pause.
  - 1.0c curation sheet (with 1e): original vs upscaled per texture hash as an HTML page.
  - 1.0d object viewer: `--viewer MODEL` loads a mission whose packs hold the
    model (index model -> missions from a dump scan), freezes its level scripts and AI, spawns the model at a quiet
    spot (our C `G_Spawn` + `G_SetModel`; level lighting), orbit camera (mouse, scroll) and a model list in the
    `--dev` overlay. Compare by flipping (key: mods on/off, later smoothing and single switches) at the same view;
    side by side (a per-entity "no mods" mark through to the renderer) only if flipping is not enough.
    `lab.py viewer OUT --model M --variant ...`: 8 fixed turntable angles per variant -> numbers + report (also the
    main check for 1g smoothing).
- **1a hash bridge:** `--texture-log` lists each runtime texture key (content hash, format, size, levels); a script
  computes the same hash from the dump and maps it to manifest rows. Check: every prologue texture matches; the
  Collector's Edition dump compared too (the pack has to fit every copy).
- **1b mods + pack loader:** `<data dir>/mods/<name>/` with `mod.ini` (name, version, author, kind, source dump),
  an index runtime hash -> DDS; pictures load on a worker thread (original shown until ready), VRAM budget with
  eviction of textures not drawn lately, a log of textures no mod covers. Load order: higher mod wins per hash.
  Data only - no code/DLL mods. Check: prologue replay without hitches or memory growth.
- **1c one pack for both back ends:** DXT1/DXT5 at 4x (DXT3 originals -> DXT5) with full mip chains; D3D9 keeps
  them in video memory only (no managed-pool copy). Check: A/B pictures on D3D9 and Vulkan, address space stays
  inside 32-bit limits.
- **1d pack builder** (`tools/texture_pack/`): extract -> headless spandrel with standard PBRify 4x -> masks ->
  DXT + mips -> `mod.ini` + index -> zip for distribution. Runs without opening images in the assistant (nothing
  sent to the API). Normal/roughness maps are generated but ship later as a separate PBR mod, once lighting uses them.
- **1e curation:** `curation.csv` (skip / upscale / conservative + region masks, e.g. the "1" shoulder patch);
  texture problems are handled when noticed in play (F6 picker + F12 -> curation.csv row -> rebuild of those
  hashes). Packs as built: campaign world 0.1 + UI 0.1.
- **1f Data Files page:** the launcher front-page button opens the mod list (checkbox, load order); one
  `mod-<name>=on|off` line per mod in settings.ini.
  As built: one `mod-list = ui, campaign, -oldpack` setting instead of per-mod lines (the settings schema is a
  fixed table): load order, first wins; '-' = off; unnamed mods follow, on, in folder-name order; nothing written
  while all are on in name order. src/platform/mod_list.* (scan_mods, shared by texture_pack::open and the
  launcher), launcher Page::DataFiles (checkbox, mod.ini name/version/author, texture count, up/down, "source"
  as tooltip, Open mods folder, Refresh; `COD_BRO_LAUNCHER_PAGE=mods`), --mod-list on the command line too.
  Checks: settings_test (Linux + Wine), game log honours the order/off, launcher shots. (The replacement hook
  supports swapping while running.)
- **1g model smoothing:** PN-AEN on the CPU when a mesh loads, cached by content hash (creases from normal splits,
  UV seams, angle threshold; UVs, colours, bone weights carried over), model draws only, own switch under Enhanced.
  CPU-side so D3D9 gets it too (no tessellation shaders there). Check: A/B captures.
  Survey 11-14 (temporary probe in nglDrawMeshSection, lab m0+m4, 6,671 sections): NO push-buffer sections - all
  draw a vertex buffer + index list, so nglDrawMeshSection (vertex buffer, stride, primitive, indices) is the one
  swap point. Primitives 5 list, 6 strip, 8 quad list. Formats (Xbox attribute format at 0x00903c0c, 16 inputs x
  {stream, offset, format, tess}): skinned sections (bone_count > 0) all stride 32 = pos FLOAT3, normal NORMPACKED3
  (0x16), uv FLOAT2, 2 x PBYTE4 (0x44: weights, indices). Static: stride 24 (pos, packed normal, uv | pos, uv,
  colour), 36/40/52 (1-3 uv sets, colour 0x40, packed tangent frame 0x16 x3). World cell meshes are never smoothed.
  First version 11-14: src/render/mesh_smooth.* (PN-AEN, 3 segments per edge, canonical edge evaluation so shared
  edges match bit for bit; straight edges at creases > 60 degrees, open section borders and non-manifold edges;
  bones merged to the 4 strongest; tests/mesh_smooth_test). Classes: cdChar (skinned: v3 bone indices, v4
  weights, from its vertex shader), cdGun, cdSimple; not cdGunSight (alignment), prelit classes (no normals),
  draw_world. nglDrawMeshSectionSmoothed -> xb::draw_smoothed (cache per section address, a sampled hash checks
  the data; COD_BRO_SMOOTH_SEGMENTS for developers) -> ngl_gfx_draw_user with a kGfxFixedContent key (back ends
  keep it without hashing). Switch `smooth-models` (Enhanced, default on; --dev F4/F5). Lab m0/m4: 256 sections,
  none refused, 13 MB, 0.6 ms each (worst 5 ms); Vulkan = D3D9. Worker threads: half the CPU threads (1-8), jobs copy the section's data, the section is drawn as it is until its result
  is ready (no hitch on first sight); workers run with the C float defaults. Lab pictures unchanged (results are
  ready long before a capture); Windows build under Wine smooths the same (same pixel shares).
  In play rifles and props looked "bulbous" (low-poly boxes with averaged corner normals). Guards added: folds (faces meet at > ~65 degrees) and normals leaning > ~40 degrees from their face
  make an edge straight (either winding: cdChar draws two-sided), curvature at 75% (Options::strength). m0: 41%
  of edges still curved, changed pixels about halved. Retest (smooth_1114b): tires not round (the rim is a
  straight fold edge), people slightly inflated (PN passes through the corners: convex shapes only grow). Two
  fixes: (A) volume keeping - corners move back by the average midpoint bulge of their curved edges
  (per position id; open-border positions pinned; the patch moves with them, interpolated), (B) round outlines -
  chains of fold/crease edges turning < 50 degrees get Catmull-Rom tangents from their chain neighbours (corners
  of 3+ feature edges stay). Tests: 12-sided tire rims on the circle, icosahedron mean radius kept. m0+m4: brightness
  change ~0 (was -0.03..-0.15 = grown silhouettes), 5,511 rounded outline edges. Retest (smooth_1114c): tires
  round; jeep licence plate sank into a bulging bumper, a sleeve (BRO patch) swelled - flat slabs whose normals
  the artist averaged (lean < 40 degrees). Curvature now follows normals made from the faces (area-weighted per
  position + smoothing group; Options::geometry_normals), shading keeps the model's. Retest (smooth_1114d):
  overlays (licence plate, sleeve patches: thin pieces a few mm above a surface) still buried/swollen - each
  section is smoothed alone. Parked: switch default off, code kept. What is missing: whole-model smoothing that
  carries overlays along with their surface, and a per-model curation key.
- **1h normal + height maps (assets only)** (the shaders that use them are phase 2's: with the original lights -
  baked lightmaps without direction, 4 object lights, short dynamic lights - the gain would be small).
  tools/texture_pack/build.py stage running Tools/PBRify_Remix/Models/1x-PBRify_NormalV3.pth and
  1x-PBRify_Height.pth (chaiNNer's Python, as upscale.py) on the upscaled textures of class `world` ONLY (never UI,
  menu or HUD pictures - builder --classes world; lightmaps and fonts skipped too) -> files kept with
  the pack work (normal as BC5, height as BC4; keyed by the same content hash), not loaded by the game yet.
  Checked by numbers (no pictures opened). Tools/Outputs/*_normal_dx.png = an earlier manual try (DirectX green
  convention).
  `build.py WORK --stage maps [--source RE]` + tools/texture_pack/maps.py: everything PBRify can make in one GPU
  pass - normal, roughness AND height (PBRify has
  no metalness/AO model), raw outputs kept in WORK/maps/<sha1>_<label>.png (normal unnormalised/unblurred 8 bit,
  roughness 8 bit, height 16 bit); CPU encode to WORK/maps_dds/<key>_<label>.dds at the pack texture's size with
  full mips: normal BC5/ATI2 (X,Y; blur 0.8 + normalise as PBRify's chain, --normal-blur), roughness/height
  BC4/ATI1 (own numpy encoder; Pillow cannot write BC4). Derived maps (cavity/AO from height) belong to phase 2.
  Prologue pilot (88 world textures, 1.8 min): normal R/G correlate -0.78/-0.79 with the height slope per
  column/row = DirectX green confirmed; mean normal Z 0.96; DDS vs raw PSNR normal 35 dB (incl. the blur),
  roughness 49, height 53. ~1.5 s per 4x megapixel on the 2080 SUPER (campaign 1,429 Mpx ~ 40 min).
  Campaign: 1,660 world textures (vehicles 257, props 391, characters 94, weapons 52, decals 99, level
  surfaces) x 3 maps; raw 4.4 GB, DDS 3.6 GB (pack_work/campaign/maps, maps_dds; kept out of the colour pack).
  300-texture sample: normal/height slope correlation -0.77/-0.78, 99% of textures agree on both axes; normal Z
  0.96; height DDS 52 dB median.

## Phase 2: lighting

Lighting on top of the baked lightmaps; Vulkan only; each feature under Enhanced with its
own switch. The normal + height maps from 1h are its first users.

- **2.0 shadows: the original's dynamic shadows switchable, then our own:**
  - Survey every shadow the game draws: the box shadow projectors in draw_world (the first projector reaching a
    mesh: projector texture + the `projected` shader variant; shaders.cpp WorldClass/draw_world), any blob or
    decal shadows under characters/vehicles, shadows in the model classes, and what is baked (lightmaps, vertex
    colours). Result: a table kind -> where drawn -> can it be switched off in our C.
  - Switch `original-shadows` (Enhanced; default on until ours replace them): off = draw_world takes the
    unprojected shader variant and no projector texture; other dynamic kinds the same way. Check: lab A/B shows
    only the projected darkening gone, a --compare run unaffected (switch unset there).
  - Our own: shadow maps from moving objects (characters, vehicles, the player's gun later), an estimated sun
    per mission, darkening only where the lightmap lights the surface.
  - Baked shadows (in the lightmaps with the light itself) cannot be switched off alone: only by dropping the
    lightmaps and relighting everything - not part of 2.0.
  - **Survey** - the game has ONE dynamic shadow kind (no blob/decal shadow textures in the dump, no
    stencil shadows):
    | kind | where drawn | switchable |
    |---|---|---|
    | r_testshadow projector (cvar default "1", always on) | R_RenderView -> R_RenderTestShadow: 512x512 texture, ortho camera looking STRAIGHT DOWN, centred 0.45*700 ahead of the view, 100 above; casters = model entities via R_AddDObjSurfaces (coarsest LOD; static ones only within camera z -100..+40, animated always; spawn key "noshadow" opts out; handle-table objects never cast); one nglAddShadowProjector box {700, -700, 500} | yes: `original-shadows` |
    | receivers | draw_world's `projected` variant: cdWorld + cdWorldBlend only (level surfaces; models never receive) | - |
    | baked | lightmaps (world), light grid / vertex colours (models) | route 2 only |
    Parameters are .data constants (radius 0x008db19c = 700, lift 0x008db1a0 = 100, depth 0x008db1a4 = 500,
    band 0x008db120/124 = 40/100); script import 0x204 (GScr_Set8db19c) could change the radius but no level
    calls it (levelreach.py). SUN: the projector points straight down in every mission - there is no direction to
    read; the "right way" look is the silhouette under the caster.
  - **Switch**: `original-shadows` (Enhanced, default on, always on with enhanced off; schema +
    launcher). Off = R_RenderTestShadow still draws its texture but adds no projector. Skipping the whole pass
    DRIFTS replays (lab m0 exit 3): R_AddDObjSurfaces' shadow copies set/reset the casters' forced LOD
    (DObj_SetForcedLod(4) then forced_lod = -1), which is game state. Lab A/B enhanced vs off: m0 0.2-0.3% of
    pixels brighter, m2 (tank) up to 1.3% brighter, nothing darker, no drift (m4 exit 3 in both = known drift).
  - **Sun - read, not estimated**: every mission's light grid (RLightGrid, R_SampleLightGrid) holds
    exactly ONE directional light (RGridLight.point == 0), always the LAST entry of `lights`, the same in every grid
    of the mission, referenced by 30-60% of the samples (outdoor share). Its vector is the light's travel direction
    (z < 0; as point lights: light -> point), its colour the sun's (warm, ~1.0-1.24 in red; m6 blue 0.89/1.00/1.32).
    Angles are whole degrees (elevation 25-55) = the designers' sun. Table: tools/data/sun_by_mission.csv (m0-m13,
    from a temporary dump in R_SampleLightGrid over tools/smoke_test.sh runs, removed). Our shadow maps take it
    at run time: the grid of the camera's cell (R_GetCellLighting), last light with point == 0 - no table needed.
  - **Step 1: `sun-shadows`** (Enhanced, Vulkan only, default on): R_RenderTestShadow's camera turned
    along the grid's sun (grid_sun: cached per grid; aim_at_sun keeps the original's row order/handedness; box
    centre kept, camera backed up along the sun by the lift / steepness). The projector box follows the camera
    rows, so the same silhouettes now fall along the sun. Lab (a = down, b = sun, c = none; masks = darker than
    c by > 4): replays in sync (m0, m2 exit 0); m0 shadows similar area, offset; m2 (30 degrees) 2-6x the area,
    frame 18640 6.5% of the picture. Limits (step 2): silhouettes have no depth (they
    also fall on surfaces between sun and caster), 512 texels over 1400 units, box depth 500 cuts long shadows.
  - Step 1 fixes (gaps in the m2 tank shadows, lab sun_m2b): casters
    drawn double-sided in the shadow scene (its draws write alpha only; derive() in ngl_gfx.cpp), camera 300
    above the view with 1500 deep casters + projector box from the camera along the sun (the 500 deep box centred
    on the camera cut low-sun shadows), receiver reach 700 / sin(elevation) (draw_world; the slanted square lands
    stretched). Ruled out: cdWorldVertexLit receivers (none in the frame; that class has no projector at all).
    Left for step 2: stair-stepped edges (512 texels).
  - Found in play (m2, build/diag/sun_1114c): (1) shadows on both sides / changing with the
    terrain / on embankments (marks 5, 7, 8) and odd angles, top half sticking out (6) = no depth: the
    silhouette darkens everything along the sun ray in the box; (2) shadows inside baked shadow (3) = no sun
    test; (3) player turret casts nothing (4, 7): rigid parts' shadow copies ignore their bone (fixed in 2c; the
    view-model guess here was wrong); (4)
    shadows pop with the view angle / vanish quickly (1, 2, 9) = 1400-unit square centred 315 ahead of the view.
    All four are step 2 work.
  - **Step 2a: depth shadow map** (Vulkan, part of `sun-shadows`). Casters (the shadow scene's
    alpha-only draws INTO the shadow texture - other passes write alpha only into the frame too, so the test is
    the target's memory, `RendererPcHooks::shadow_target_data`) draw with our `sun_caster.frag`: 1 - depth packed
    in RGB, our own D24S8 buffer (ngl_gfx `sun_depth_for`, cleared at the frame's first caster) keeps the nearest.
    The projected world draws switch to `sun_world*.frag` / `sun_blend*.frag` (the four title projected variants
    with the silhouette replaced by a 4x4-tap tent PCF depth compare; c0 = depth scale/offset/bias/strength from
    R_RenderTestShadow). Own GLSL pixel shaders in the title layout: `render::OwnPixelShader` keys through
    create_title_shader (Vulkan only). Square radius 1500 (was 700; the target is render-scaled, so texels stay
    fine), box 5000 deep along the sun. Lab m2/m0: replays in sync; flat-ground shadows as step 1 but soft, and
    distant tanks now shadowed. Casters are still only models visible in the main view (R_AddDObjSurfaces adds a
    coarse copy of what it draws), so shadows of off-screen models still pop in at the screen edge.
    Left: (d) off-screen casters (own caster list).
  - **Step 2b: no shadow in baked shade.** tr_main `grid_sun_mask`: the view's light-grid area (its
    BSP cell's) as an L8 texture of each sample's sun reference weight (0..255 of the grid's strongest; cached per
    area), bound on slot 4 for the receivers; c1/c2 map the projector's (u, v, z, 1) to its texture coordinates
    (the box point r3 + across R (u - .5) - up R (v - .5) + sun D z). The shadow fades out by
    smoothstep(0.15, 0.6, weight). Debug view (temporary, removed) showed the mask on the ground matching the
    shaded strip along m2's rock wall. Lab m2/m0 in sync; m0 16424's shadow (in shade) gone.
  - **Step 2c: turret shadows.** The missing turret was NOT the view-model scene (its list is empty
    in m2; the step-1 note was wrong): tanks are two-model DObjs and the original adds every RIGID part's shadow
    copy at the entity matrix, bone ignored - the turret's shadow sat inside the hull's (invisible straight down).
    With shadow_depth the copy takes the part's bone x entity matrix (`part_world`, before the far-distance
    shrink) and only the entity's own scale. Lab m2 +0.2-0.4% shadow area, m0/m1 in sync.
  - Shadows near the screen's bottom edge were cut off along mesh borders (build/diag/sun_1115a). draw_world's receiver reach was measured from the
    projector's position, which the deep box puts ~2500 units along the sun: now from the square's centre
    (`shadow_reach_center`) with its stretched far corner (R sqrt(1 + 1/sin^2)). Marks 2-5: distant hill
    banding = the textures themselves (same with Enhanced/mods off; not a bug).
  - A half-track ~2000+ ahead had no shadow (sun_1115b). **Range doubled instead of a second cascade:** the shadow texture's target (and our
    depth buffer) at 2x resolution on top of the render scale (TargetDesc::extra_scale, ngl_gfx
    kShadowTargetScale; Vulkan placement follows the image size), radius 3000, box 9000 deep: the same texel
    density as radius 1500, twice the reach. Lab m2 18640 7.6 -> 8.1% shadow area, m0/m2 in sync. A real far
    cascade only if this still falls short.
  - The player's tank shadow came and went with the orbit camera (sun_1115c). Cause: the 2b mask used the camera's cell's grid only - EVERY BSP cell has its own
    grid with one area (m2: 12), neighbouring rectangles overlap, and a cell's samples outside the cell carry no
    sun. Now `level_sun_mask`: all cells' grids (as R_GetCellLighting) in one mask over their union at the finest
    cell size, max sun weight of any area's nearest sample; rebuilt when the grids' signature changes. Debug view
    as before; lab m2 4208 5.0 -> 5.9% shadow area, m0/m2 in sync.
  - It still came and went with the camera, other tanks too (sun_1115d) - the real cause was the
    original's caster band: static (non-animated) models cast only within -100..+40 of the CAMERA's height
    (0x008db120/124), so the orbit camera's height switched them. Under shadow_depth the band is skipped; for
    rigid parts it would have rejected, the copy goes in without DObj_SetForcedLod (coarsest LOD for model 0,
    as the forced 4 picks; game state untouched). Lab: m0/m2 in sync; palm trees now cast (m2 15032 1.1 -> 3.9%).
  - The own tank's shadow still went with the sun behind the camera: the shadow
    camera sat only kSunLift / sin back from the square's centre (~0.64 R ahead of the view), so casters on the
    sun side of the centre were before the near plane. Now it starts at the square's sun-side edge
    (R / tan(elevation) + kSunLift / sin(elevation) back), box depth = back + R / tan + 1000. The own tank near the
    camera still lost its shadow at some angles - found with the Vulkan mark dumps (below): the projector covers +-radius/2, and the
    original's centre (0.45 radius along both square axes) leaves the view at the square's back corner - the
    own tank sat on or past the map's edge. Now the centre is 0.25 radius straight ahead, radius 4000, shadow
    target 3x (kShadowTargetScale). Lab m2 in sync.
  - Vulkan mark dumps: an F12 / auto mark writes <mark>_draws.txt (every draw of the next frame: target
    memory, vertex/pixel keys, [caster]/[receiver] = our shaders, states, textures; header = sun shadow state and
    receiver constants), <mark>_target_<memory>.bmp per off-screen target, <mark>_shadow_depth.bmp (decoded:
    white = nearest the sun) and <mark>_sun_mask.bmp. Replays: COD_BRO_DUMP_FRAME=N:PREFIX (presented frame N).
  - A shadow that seemed to "stretch" (stationary tank, camera only turned): checks: turning/shifting the
    square alone on a fixed replay frame changes 0.17% / 0.002% of pixels (texel edges); off-screen PARTS of a drawn
    model now cast too (the original copies only parts the main view drew - the own tank's tracks below the screen
    edge); recorded run recordings/m2/m2_shadow_1115.rep: own tank's silhouette in the maps of both marks the same
    size (1681/1683, 1061/1085 texels), the red-painted shadow (temporary debug) the same footprint seen from two
    sides - the far end lies under the other tank's hull from above (models do not receive shadows).
  - Off-screen casters limited to a slight extended angle: every entity
    in draw range reaches R_AddDObjSurfaces (no view cull before it), so an off-screen part now casts only when its
    bounding sphere is inside the main frustum widened by 0.1 x its distance (near_view_caster, ~6 degrees).
    Recording m2_shadow_1115 frame 5000: 29 caster draws on-screen only, 53 with the margin, 56 for all in range.

### The original's lighting (survey, 2026-11-15)

What the title's lighting does (the starting point of 2.1-2.6):
- **Models** (cdChar skinned, cdGun, cdSimple, cdSimpleAlpha, cdPrelit, cdSimplePrelit, cdSimpleUVAnim, cdGunSight):
  lit PER VERTEX by the title's vertex programs from constants 6..17 = matrix + 4 object lights (object-space
  directions, colours) + ambient (shaders.cpp set_object_lights / nglComputeObjectLights). Vertices carry packed
  normals (NORMPACKED3), no tangents. The lights come from the light grid (R_SampleLightGrid -> nglAddDirectionalLight,
  the sun among them: the grid's last point == 0 light) and from effects (FX_FreeFinishedEffects ->
  nglAddPointLight, one yellow flash per effect). Point lights are evaluated ONCE at the model's centre
  (nglEvalLight), so a flash lights a whole soldier evenly. At most 4 lights per model.
- **World** (cdWorld, cdWorldBlend; cdWorldVertexLit never drawn in our frames): texture x lightmap (+ fog); the
  vertex program (vs_0089fa60) takes position, UV and lightmap UV only - NO normals. Dynamic lights DO reach the
  world (corrected 11-15 while building the tools): after the main draw, shaders.cpp adds them in extra passes
  (blend ONE, the dynamic-light falloff texture on every stage, two lights per pass, vertex programs by light
  count: k.light_vertex_shaders) - per vertex, no normals, so a flash lights both sides of a wall alike.
- Our receivers so far: own GLSL pixel shaders behind the title's vertex programs (render::OwnPixelShader,
  Vulkan only). Models need more than that: per-pixel lighting needs the world position and normal in the
  pixel shader, which the title vertex programs do not output.
- Surface maps: normal (BC5, DirectX green), roughness + height (BC4) for 1,660 class-`world` textures (incl.
  characters 94, weapons 52, vehicles 257, props 391) in pack_work/campaign/maps_dds, keyed by the original
  texture's content hash; nothing loads them yet.
- Render graph (render_graph.h): world draws -> scene passes (resolve, FXAA, sharpen) -> HUD -> present;
  lighting passes go between the world and the resolve. Our depth (D24S8) is the title's depth buffer.

Rules for every phase 2 feature: Vulkan only, D3D9 keeps the original; each feature its own switch under
Enhanced (schema + launcher + --dev F-key where cheap); replays stay in sync (rendering-only changes, no game
state); lab A/B picture numbers per step, Vulkan mark dumps for marks.

### 2.1 done (2026-11-15): own model shaders, per-pixel lights, models receive the sun's shadow

- **2.1a** `tools/shader_glsl.py` turns the lit classes' generated title HLSL (cdGunSight, cdGun, cdPropeller, cdChar,
  cdSimpleUVAnim, cdSimple: 12 vertex programs incl. packed-normal variants, 12 pixel shaders) into GLSL
  (vk/shaders/model_*, generated; `--check` finds stale files). Same operations in the same order; the vertex
  programs also output the world position / normal (locations 11, 12) from the point fed to c6 and the normal lit
  with c10. cdAirplaneMetal lights with -c10 (another model) and stays on the title's. Keys: title key |
  render::kOwnModelShader. Check: COD_BRO_MODEL_SHADERS=1 (ours with both switches off) + COD_BRO_SHADER_AB=own on
  m2 frame 5000 (74 draws, 705k pixels) and m0 frame 6000 (94 draws, 592k pixels): **0 pixels differ, max 0**.
- Our constants: vertex c195..c226 (render::kGfxVsOwn, 32 registers; own_model.glsl lists them), the vertex block
  now visible to the pixel stage too (Vulkan; D3D9 ignores the range). nglComputeObjectLights notes the node's
  lights in world space (RendererPcHooks::model: up to 8, directional = travel direction, point = position +
  inner/outer radius) and the object -> world rows; ngl_gfx set_model_constants hands them over per draw.
- **2.1b/d** `pixel-lighting` (Enhanced, Vulkan, default on): ambient + every light per pixel from the
  interpolated normal; point lights with nglEvalPointLight's linear falloff at the pixel, up to 8 lights.
- **2.1c** models receive the sun's shadow (part of `sun-shadows`): the light matching R_RenderTestShadow's sun
  (hooks.sun_direction, dot > 0.9995; m2: 76% of model draws - the rest stand in baked shade) loses the shadow
  map's share (sun_world.glsl's PCF and baked-sun mask; projector matrix captured in nglAddShadowProjector). The
  view model receives the same way. Self-shadowing: normal offset 8 units (x up to 3 where the sun grazes); lab
  orbit m2 speckle per pose with 0 / 8 / 16: turn 135 0.22 / 0.08 / 0.07 (reference before 2.1: 0.07).
- Lab m0/m2 per-vertex vs per-pixel (both with shadows): replays in sync, 0.0-0.3% of pixels changed, slightly
  darker (models in shadow). Debug views on models: lighting, shadow, sun-mask, normals, lights (count).

### 2.2 done (2026-11-15): surface maps in the game

- Own mod: `build.py WORK --stage maps-pack` -> WORK/maps_mod/ (mod.ini kind=surface-maps,
  maps/<key>_normal.dds = BC5 X Y hard-linked from maps_dds, maps/<key>_surface.dds = BC5 R roughness G height:
  the two BC4 files' blocks interleaved, no re-encoding - only texture slots 6 and 7 are free). Campaign: 1,660
  textures, 3.6 GB, 2 min.
- texture_pack scans every mod's maps/ beside textures/ (same loading thread, `surface_map(content, kind)`); keyed by
  the ORIGINAL texture's hash, so they serve with the colour pack off; F2 (mods off) drops them too. Vulkan
  `surface_map_texture` uploads BC5 (kept and idle-freed with the replacements). ngl_gfx remembers each stage's
  content hash (`texture_for(..., &content)`); our model shaders get t6/t7 for stage 0's texture, own[11].w = 1.
- Debug views roughness and height (new, `--debug-view height`): grey, magenta where the texture has no maps.
  Check m0 frames 6000/16424: models 0% magenta (17% / 31% of the picture grey); view off = byte-identical to
  --mods off. World draws get their maps with 2.4 (their shaders become ours there).

### 2.3 done (2026-11-15): normal maps + shine on models

- Switches `normal-maps`, `specular` (Enhanced, Vulkan, default on; do nothing without the surface maps mod). The
  lighting switches reach the game as one hook, RendererPcHooks::lighting_features (kLighting* bits).
- own_model.glsl: tangent frame per pixel from world position / texture coordinate derivatives (cotangent frame);
  the maps' X follows +u and Y +v in D3D rows (checked on 12 maps: correlation with their height slope 0.4-0.9),
  so no flip. The mapped normal lights every light; the shadow lookup keeps the geometric normal (offset).
- Shine: GGX, F0 0.04, sun + point lights only (the grid's other directions are bounce light), shadowed with the
  sun; roughness from the map floored at 0.4, x 0.35, soft cap 0.3 (`0.3 s / (0.3 + s)`), added after the
  combiners, faded with fog. Lab nmap vs spec: m2 18640 p95 rise 37 levels (163 before floor + cap), m0 <= 23.
- Lab m0/m2 flat vs normal maps: 0-0.5% of pixels changed, brightness +0.00..+0.06; replays in sync.

### 2.4 done (2026-11-15): normal maps on the level

- The level's main draws are generated ports too (shader_glsl.py WORLD_VS / WORLD_PS: cdWorld, cdWorldBlend, plain
  and projected, 8 pixel shaders) with two hooks: the lightmap sample x our normal-map factor, the projector
  sample = our depth compare's lost share (the title's combiners darken by it). The hand-written sun_world*.frag
  (2.0 step 2) and RendererPcHooks::pixel_override are gone. draw_world hands its object -> world rows and the
  camera to ngl_gfx (hooks.model.to_world, world_eye). Checks: A/B forced, everything off, m2 5000: 218 draws,
  2.8M pixels, max 0; lab neutral vs the 2.3 build (build/lab_reference/p23), shadows on, no maps: 16/16 PASS.
- Factor (lightmap stays the light): light x (1 + w x (max(Nm.L, 0) / (max(Nf.L, 0.2) max(Nm.Nf, 0.5)) - 1)),
  within 0.6..1.4; Nf = face normal from position derivatives (toward the camera), L = the view grid's sun
  (hooks.sun_direction, now set whenever shadows or normal maps are on), w = the level sun mask at the point
  (hooks.sun_mask_world: world x y -> mask) x smoothstep(0, 0.17, Nf.L). cdWorldBlend mixes both layers' normals
  by v0.w (t6, t7 = their normal maps).
- Two fixes found by measuring the factor per pixel (temporary debug output, world pixels marked): walls turned
  away from the sun carried the ground's mask weight (-6.7 levels on m0 8512) -> the Nf.L fade; PBRify maps lean
  slightly as a whole (median ratio 0.96 on m2 18640) -> each map's average tilt (its 1x1 mip) is subtracted
  (models too). Lab flat vs normal maps: m0 0-6% of pixels, m2 4-15%, brightness -0.8..+0.0, sharpness up to
  1.32; replays in sync.

### 2.5 done (2026-11-15): the level's dynamic lights per pixel - and how faint they are

- Survey (vertex programs 0089fc58 / 0089fde0, cdWorldBlend 0089ec48 / 0089edd0; lights in c18 c19 = object-space
  position + radius, set by draw_world from the node's lights): the level vertices carry a normal (v3) and a tangent
  frame (v4 v5). Each pass projects the light's sphere onto the triangle's plane: falloff texture coordinates from
  the in-plane offset over the circle's radius sqrt(r^2 - d^2), intensity V = 1 - d / r (d = plane distance), only
  on the plane's front side. Exact on flat triangles already; no N.L at all.
- Ours: generated ports (shader_glsl.py WORLD_LIGHT_VS / _PS; the vertex programs also pass the surface's own
  texture coordinates v1 v2, location 13) with `own_dlight_begin`: each light's colour (not its alpha - the pass
  adds rgb x alpha) x the 2.4 ratio against the face for that light's direction (0.5..1.6), using the normal map
  of the level draw's first layer (ngl_gfx keeps the main pass's stage contents for the passes that follow it).
  Switch `world-dlights` (Enhanced, Vulkan, needs normal maps on + the maps mod). Flat surfaces keep the
  original's light.
- Checks: A/B forced, all off, m2 frame 8318 (64 light passes among 493 draws): max 0. Effect: 20 flash frames
  (m0, m2): at most 2 levels anywhere - the original's level lights are faint (falloff texture x V in colour and
  again in alpha), so per-pixel N.L has little to work with; colours and radii stay the original's. The "all
  lights in one pass" merge was left out (no visible gain; the passes are few).

### 2.6 done (2026-11-15): ambient occlusion + cavity; a shadow-map copy bug found by the GPU timer

- `ambient-occlusion` (Enhanced, Vulkan): vk_ao.cpp after the world, before the HDR resolve (the HUD stays clean):
  half resolution horizon AO (ao.frag: 3 directions both ways x 5 steps in 48 world units, 4x4 ordered rotation -
  no temporal filter in the engine, so the pattern stays put), two 9-tap depth-aware blurs, multiplied into the
  scene at strength 0.6; faded out between the fog's start and end, sky and anything nearer than 24 units (the view
  model) left out. Debug view `ao` shows it alone. Depth: the scene projection's own mapping (nglDepthToZValue:
  z = (a d + b) / (c d + e) in 16777215ths), handed over by draw_world through RendererPcHooks::set_scene_view
  (the game library does not link the render library). The world draws several scenes with depth clears between
  (sky, level, view model): gfx_vulkan keeps a copy of the depth of the scene with the most depth-writing draws
  (keep_world_depth); without it the AO saw only the gun.
- `cavity` (with the maps mod): the height map below its neighbourhood 2.5 mips coarser darkens the light
  (x 3, >= 0.6) on models and cdWorld (cdWorldBlend's second map slot holds its other layer's normal map).
- Lab m0/m2 (base / cavity / + AO): replays in sync; cavity 0-2% of pixels, -0.2..-0.75 levels, sharpness up to
  1.10; AO 3-11% of pixels, -1.1..-2.5 levels. Orbit m2 micro: shimmer 0.000.
- GPU timer (developers): `COD_BRO_GPU_TIME=1` prints at exit the GPU time of the frames that drew a world
  (timestamps at each frame's command start/end; replays: only the frames --replay-no-draw draws). m2, 5 capture
  points x 120 frames, render 1707x960 (2.67x), maps mod: phase 1 0.90 ms; sun shadows 1.00; everything 2.38 (AO
  0.57, maps 0.26) -> phase 2 = +1.48 ms (budget 2 ms). It found a bug from 2.1: our shaders left the shadow map
  bound on slot 5, and the next frame's casters draw INTO it, so the back end copied the whole 4100 x 4100 map
  before every caster draw: 12.3 ms a frame. apply() now unbinds slots 4-7 on every draw that does not set them.

### 2.7 gate (2026-11-15)

- Lab build/lab/gate27 (m0-m9, 8 capture points each; variants original = `--graphics original`, phase1 = enhanced
  + colour pack with every phase 2 switch off and original shadows, phase2 = enhanced + colour pack + surface maps,
  defaults): every run exit 0, 8/8 pictures. Sync: m0 m1 m2 m5 m6 m7 all frames match in all three variants; m3
  (from 52398), m4 (38347, the known 34 frames), m8 (15379, known unstable) and m9 (59014) drift IDENTICALLY in all
  three - recording drift, not rendering (phase 2 changes no game state).
- Pictures vs original: phase2 changes 0-49% of pixels (sun shadows, AO, maps), brightness -0.05..-4.3 levels,
  sharpness as phase 1 (the colour pack). Report build/lab/gate27/report.html.
- GPU per drawn frame (COD_BRO_GPU_TIME, two runs sharing the GPU, so higher than alone - m2 alone +1.48 ms):
  phase2 - phase1 = m0 +2.2, m1 +2.0, m2 +1.8, m3 +1.4, m4 +0.9, m5 +0.3, m6 +1.0, m7 +0.9, m8 +1.1, m9 +0.7 ms at
  1707x960. m0 is at the 2 ms budget; levers if needed: AO samples / half -> quarter resolution, the
  shadow map 3x -> 2x (measured: 3x map 0.1 ms after the 2.6 fix, so AO first).
- Windows build under Wine: m4_smoke with everything + both mods: all 2620 frames match. Found (older than phase
  2, phase 1b): with ANY mod loaded the Windows process does not exit after the run (one thread left in process
  teardown; winedbg cannot attach, error 299); without mods it exits in 33 s. Detaching the pack's loading thread
  at exit did not change it (reverted). Open: needs a Windows check / deeper look before a release with packs.

### Play runs 2026-11-15 (phase 2)

- m0 (build/diag/p2_m0_1115, 284 s, everything on + both mods): no marks; 59.9 fps, GPU mean 3.7 ms.
- m11 Crucifix Hill, night (build/diag/p2_m11_1115, 1162 s, recorded: recordings/m11/m11_night_1115.rep, 67994
  frames, replays in sync): 59.8 fps, GPU mean 3.8 ms. Mark 3 (the post's edge seen on the gun) = our AO on the
  view model: the kept level depth has no gun, and the gun's own depth is cleared again by a later scene before the
  world ends, so the live depth could not mask it. Now gfx_vulkan also keeps the depth of the scene after the level
  (overlay_depth_copy_) and ao_apply leaves out every pixel it covers; check on the recording: gun pixels identical
  to AO off. Mark 2 (rifle into the wall) = the game's own clipping (same with --graphics original).

### Phase 2 tools (built 2026-11-15)

1. **Per-draw shader A/B** (ngl_gfx.cpp ShaderAb): `COD_BRO_SHADER_AB=own` with a dumped frame (F12 mark or
   `COD_BRO_DUMP_FRAME=N:PREFIX`): each draw whose shaders are ours is drawn twice more before its real draw, into a
   cleared scratch target (BGRA8 + D24S8, the draw target's size): title shaders + title constants, then ours.
   Read back and compared: `PREFIX_shader_ab.csv` (draw index as in `_draws.txt`, vertex / title pixel / our
   pixel key, covered pixels each, pixels differing > 2 levels, max, mean) and the worst draw's two pictures.
   `=self` compares every draw title against title (the harness check: m2_shadow_1115 frame 5000, 302 draws,
   0 differ). The real frame is untouched (shots equal to a plain run). For 2.1a: the vertex shader side is
   already in (ShaderAb.vs / title_vs): own vertex programs only need apply() to choose them.
   First use (11-15, today's sun receivers, frame 5000): 144 draws ours, 11 differ = the ones with shadow in
   them (depth compare vs the old silhouette, as intended).
2. **Debug views** (render/dev_mode.h DebugView): F10 in --dev steps through, `--debug-view NAME` sets one
   (lab variants). Passed to our shaders as c3.x (RendererPcHooks::debug_view, apply()). Today in sun_world.glsl:
   lighting (lightmap x shadow), shadow (factor; dark red = no sun in the baked light), sun-mask. Reserved for
   the coming steps: normals, roughness, lights, ao. Draws not on our shaders stay as they are (today: only the
   world near the player, inside the projector's reach).
3. **Lab camera orbit**: `tools/lab.py orbit OUT [--recording R] [--mission m2] [--at F] [--sets turn orbit micro]`;
   loader `--orbit FILE` (src/runtime/loader/orbit.cpp; implies --dev) pauses on the first live frame after the
   cut recording and draws world scenes from each pose (RendererPcHooks::camera_override in RE_RenderScene:
   rendering only). Table per pose: caster draws, shadow area vs the shadows-off variant, speckle, dips;
   orbit.html side by side. Repeatable: two runs give identical pixels at every pose. m2_shadow_1115 at 4900:
   casters only ahead of the column (turn 135-315: 0 casters -> 0 area, real).
4. **Shimmer / speckle**: the orbit's `micro` set (8 steps of 0.25 units sideways): shimmer = shadow-mask pixels
   flipping between neighbouring steps minus the baseline's own changes; speckle = mask pixels that disagree with
   their 3x3 majority, per mask pixel (acne and noise high, clean edges low). Today: shimmer 0.000, speckle ~0.07.
5. **Neutral check**: `tools/lab.py reference [NAME]` keeps the current build in build/lab_reference/NAME (RPATH
   rewritten to $ORIGIN so it loads its own game library); `tools/lab.py neutral OUT [--reference NAME]
   [--options "..."]` runs both over the capture points and needs identical pixels (PASS/FAIL per picture).
   Same build against itself on m0+m2: 16/16 PASS (5 min). Per step: reference before, neutral with the new
   switches off after.

## Phase 4: in-game menus

The console front end adapted for PC, keeping its look: every player setting in the game's own menus, generated
from the settings schema (settings_schema.cpp) - except the renderer choice (Vulkan / Direct3D 9), which cannot
change while the game runs and stays in the launcher; short sub-pages in the original's style; full mouse support
with keyboard and controller still working; the Dear ImGui overlay developer-only. Groundwork (11-07): Options ->
GRAPHICS = the cut Screen menu, through game::PcSettingsHooks (src/engine/client/fe_pc.h); everything here is off
in --compare runs.

### Phase 4 tools (4.0, built 2026-11-15)

- **Menu walks:** `--menu-walk SCRIPT DIR` (src/runtime/loader/menu_walk.h) runs a script of steps that wait for
  menu states (`screen [front|game|dialog:]N [button]`, `idle`), press pad buttons, move and click a scripted mouse
  (`item N` puts the pointer on a row, `click`, `rclick`, `wheel`; the pointer goes through the same menu-pointer
  code as the real mouse) and take pictures with the menu state beside them. `tools/menu_walk.py OUT [WALK]
  [--level N]` runs one on a copy of the save folder and makes OUT/sheet.png; the stored walks are in
  tools/data/menu_walks/ (front_options: every Options page, Single Player, Chapter Select; pause: `--level 4`, the
  pause menu and its Options). Two walks run side by side in ~1 minute.
- **Menu-state dump:** loader::menu_state() (patches.cpp): per manager (front end, in-game, dialogs) the active
  screen (index, vtable, selection, locked) and every item (shown / hidden / off, text, box in 640x480); written as
  <shot>.txt, all states in walk.txt.
- Found on the way, in the own exe (`cod_bro`, not the loader): the front end's save list needed the kernel's ANSI
  conversion (WideCharToMultiByte has a native branch now) and called the set-aside Live invite check after the
  profile screen (skipped on our own platform); next on that path is the SDK's save signature
  (Save_CalculateSignature) - the own exe cannot reach the menus with saves yet, walks use the loader.

### 4.1 done (2026-11-15): mouse in the menus

The pointer code from 0.6c (patches.cpp "Menus with the mouse", called from UIScreenMgr_HandleInput's C) already
covers the plan: the system cursor shows in menus (the mouse is not captured there), hovering selects, a left click
accepts, a click left or right of a row's value steps it (the arrows), the wheel moves the selection (sideways in
pickers), a right click goes back. Added: several wheel notches in one poll step one after another (they merged
into one press). Checked by `tools/menu_walk.py OUT mouse` (tools/data/menu_walks/mouse.walk): the pointer through
the window -> menu conversion of the pillarboxed screen (`window X Y`), Options opened by a click, Subtitles' right
arrow clicked (off -> on in the state's texts), two wheel notches = two rows, a right click back to Options. The
state dump lists every drawn text too (values such as on / off, titles, prompts).

### 4.2 done (2026-11-16): settings pages in the game's own menus

- **Where:** Options -> CONTROLS (was CONTROLLER), GRAPHICS and SOUND open our pages on the front end's Screen menu
  (the paper); the pause menu's Xbox Live Options row became **Settings** and opens them on the in-game Options
  screen (root page Settings: Graphics, Sound, Controls). The game's own Gameplay and Controller screens stay
  (Controls -> Controller opens screen 5, whose Back / Apply come back to our page).
- **Host side** (src/runtime/loader/menu_pages.cpp): the pages, generated from the schema (labels in title case,
  short choice names where the launcher's are long, presets for free-text/wide settings: aspect, fps, render scale,
  anisotropy, mouse sensitivity, reverb decay; number steps per setting). Graphics: Enhanced Graphics + Display,
  Screen, Image Quality, Lighting, Detail (-> Texture Packs: one on/off row per installed pack, mod-list). Sound:
  the game's own Master Volume, Enhanced Sound + Volumes, Sound Paths, Reverb, Quality, Output. Controls: mouse
  sensitivity, aim assist, button prompts, Controller, Keyboard (3 pages of bindings: accept / right = press a key,
  left = drop the second key; Escape cancels). Live settings change at once; the others show "*" (after a restart).
  Apply and Exit (root) saves what changed (one line per setting, like the launcher); Back on the root undoes the
  live changes; Escape in a level = Apply and Exit + resume. Renderer / Direct3D 9 stay launcher-only.
- **Game side** (src/engine/client/fe_pc_pages.cpp): the host menu runs with our vtable copy (draw, update, input,
  activation, destructor slots), our item array and value texts (base UI texts like the panel's, same font, colour,
  draw order), the panel's background and title kept, its other texts and pictures hidden, its first arrow pair
  around the selected value; leaving puts every one of these back. Contract in fe_pc.h (PcPageView / page_input).
- **Input:** input::begin_key_capture / key_capture (devices.h) for rebinding; the menu pointer accepts the
  multi-line text class too (our rows' clicks, hover, arrows).
- **Checks:** tools/data/menu_walks/pages_front.walk and pages_ingame.walk (`--keep-data`: OUT/data/cod_bro/
  settings.ini holds what Apply saved); the walk tool now runs with XDG_DATA_HOME in OUT (settings.ini and log.txt
  writes stay there; --data alone does not move settings.ini). Replays m4_smoke and m2_shadow_1115:
  all frames matched.
- Pitfalls found: a new multi-line UIText (UIText_Construct) draws nothing this way and UIText_SetText reads line 0
  of a text without line storage (+0x13c (n) first); `+0x78` takes a script string; SetActiveScreen passes the next
  screen's object (not its index) to +0x44.

### 4.3 done (2026-11-16): widescreen menus

- In a level the menus were already right: drawn into the middle of the full-width picture at their own shape
  (narrow_ui_draw), letterbox bands full width - checked at 16:9 and 21:9 with the page walks.
- Front end: pillarboxed frames now fill the sides with the same picture scaled
  to cover the width (top and bottom cut, nothing stretched), blurred and dimmed to 40 %, under the sharp 4:3
  picture. Vulkan: blit.frag modes 3/4 (a 48-tap spiral blur, 6 % of the width; 4 = through the gamma ramp);
  Direct3D 9: the picture made 128x96 -> 32x24, dimmed on the CPU, scaled up (backend_d3d9.cpp fill_sides).
  Screenshots and walk pictures hold only the 4:3 frame, so the fill is checked by eye. The corner texts of
  the front end are console-only and go in 4.4; what stays (the help bar) is centred.
- Walks front_options (updated for the Options routes of 4.2) pass at 21:9 on Vulkan (--vk-validation: no
  messages) and Direct3D 9.

### 4.4 done (2026-11-16): console-only parts

With our PC menus on (pc_pages(); --compare and --pc-patches off keep the original):
- The front end's Xbox Live corner ("Sign-in failed" / "Not signed in", "[Y] Xbox Live") is not drawn
  (FrontEndMenu_Draw, now Replaced - it was a matched gap row still on Compare) and Y no longer opens the Live
  screen (FrontEndMenu_OnButtonDown).
- The pause menu's Xbox Live Options row is Settings (4.2).
- The title screen says "Press ENTER or START" (the original's FEMENU_TITLE_PRESS_START_XBOX names START only;
  Enter = A works there too). The other prompts already show PC keys (0.6 key prompts).
- Memory units: none of the mapped screens shows one on PC (no device choice without units; the "not enough
  blocks" dialogs only show when the disk is full) - left as they are.
- Main menu -> Multiplayer re-executes into the multiplayer executable, whose own menus (Play Xbox Live, Play
  System Link, its own Live corner) belong to the system-link plan; left as they are. A menu walk does not follow
  the switch (its script restarts in the new executable).

### 4.5 done (2026-11-16): the ` overlay is developer-only

The Dear ImGui PC menu (key below Escape) is installed only with `--dev` (title_setup.cpp); its one-time notice in
the front end says "Developer settings" and shows only then. Players set everything in the game's own menus. The
key stays reserved (not bindable) as before.

### 4.6 gate, automated part (2026-11-16)

- `--check-settings` (title_session.cpp): settings.ini read as the launcher and the game read it - every line a
  known setting with a valid value, stable through a load and save, accepted by the option parser; exit 0 = valid.
  tools/menu_walk.py runs it on the copy after every walk (exit 5 when it fails; OUT/settings_check.txt).
- tools/data/menu_walks/sweep_settings.walk: every settings row on every front-end page stepped once, Apply on each
  root: 40 settings saved, all valid. It found the aspect preset 16:10, which the game refuses (only 4:3, 16:9 or
  wider): removed, and a step now skips any value the schema rejects.
- Walks pass: front_options, mouse, pages_front, pages_ingame, sweep_settings (Vulkan; front_options also at 21:9
  with validation and on Direct3D 9); replays m4_smoke and m2_shadow_1115 all frames matched; the Windows build
  compiles. A walk script that does not parse now ends the program (exit 2) instead of leaving the game running.

### Close-range boost and Quit Game (2026-11-16)

- The close-range boost has no switch any more (its rows were dimmed by close-boost = off, a switch on another
  page): its three dB volumes apply whenever enhanced sound is on
  (0 dB = the game's level); old close-boost lines are read and ignored (kHidden).
- **Quit Game** on the main menu: a sixth row of its own (MainMenu_Construct makes
  room only with the PC menus; not the special edition's fifth row, which the Collector's Edition uses). The paper
  has no room below the last row (its checkboxes are art), so it sits right-aligned in the bottom bar level with the
  button prompts, white like them, dimmed unless selected (MainMenu_Draw, now Replaced). It ends the program as
  closing the window does (render::request_quit). The original's hidden DISABLED row, half visible in the top-left
  corner at (0, 0), is no longer drawn.
- Quit Game is left out while recording or replaying: its boot-time allocations shift the title heap, and m4_smoke
  then differed from frame 152 on (pointer drift, found by the replay check). Without it in those modes, m4_smoke
  and m2_shadow_1115 match every frame again.

### Menu map (2026-11-15, from the walks)

Front end (manager 0x0095f8f8), screens by index:

| Screen | Class (vtable) | Rows | Console-only parts |
|---|---|---|---|
| 0 | title / intro (008297f0) | none | "Press START" |
| 15 | profiles (00828ed0) | profile names, NEW x3 | memory-unit dialogs on save errors |
| 1 | main menu (00829b80) | Single Player, Multiplayer, Options, Bonus Materials (+ hidden DISABLED) | Multiplayer starts the MP executable (system link later) |
| 2 | Single Player (00829410) | Continue, New Game, Chapter Select, Save Profile, Switch Profile | |
| 17 | Chapter Select (008299f0) | 14 chapters, a sideways picker | |
| 3 | Options (00829348) | Gameplay, Controller, Graphics (ours), Sound | |
| 4 | Gameplay (00828930) | Subtitles, Crosshair, Friendly Tags, Alternate Tank Control, Enable ADS Toggle, Reset to Defaults, Apply and Exit | |
| 5 | Controller (00829c48) | Invert Aim, Vibration, Horizontal / Vertical Sensitivity, Controller Configuration, Reset, Apply | the Xbox pad picture with the layout |
| 6 | Graphics = the cut Screen menu (00828b88) | Enhanced Graphics, Reset, Apply | |
| 7 | Sound (00829fa0) | Volume (a bar), Reset, Apply | |

Every front-end screen: "Sign-in failed" (top left) and "[Space] Xbox Live" (top right) - console-only.
In-game (manager 0x0095f8fc): 3 = the loading screen ("Press A to start", 0082a1f8); 0 = pause menu (008289f8):
Resume, Options, Xbox Live Options (console-only), Restart, Quit; 1 = in-game Options (0082a388): Invert Aim,
Vibration, Horizontal / Vertical Sensitivity, Crosshair, Friendly Tags, Volume, Subtitles, Alternate Tank Control,
Enable ADS Toggle, Reset Defaults, Apply (12 rows, the whole page). Dialogs (0x0095f900): 0 = message boxes (OK /
two buttons).

## Phase 3: sound propagation

The starting point (the own audio backend of 10-29/10-30; src/engine/sound/snd_env.cpp, src/audio/sound_engine.cpp):
- Reverb: 14 probe rays from the listener (8 level, 5 up, 1 down) through the GAME's CM_BoxTrace every few
  updates -> fitted box -> Sabine decay at mid/high with measured absorption per surface type, reflections
  direction, glides between probes. Per voice: one EFX send.
- Occlusion: per 3D voice, 7 lines to a sphere round the source + one edge detour (over / round the first hit);
  in sight = full direct sound. 8 voices per update on the game thread; result = direct gain x (1 - 0.3 o) and HF
  gain x (1 - 0.5 o), reverb send up. Same muffle for a curtain and a bunker wall.
- Limits: everything on the game thread through CM_BoxTrace (game code: it also counts traces in the collision
  state - not ideal for determinism), so rays are few; no transmission by material or thickness, one bend at most,
  sound never seems to come FROM a doorway, one band of filtering, reverb from one point's 14 rays.

Rules as in phase 2: Enhanced sound + own switch per feature, neutral or subtle defaults, replays stay in sync (the
sound path must not touch game state), heavy work on worker threads, measured before tuned. The game's own falloff
curves stay. Transmission keeps a -18 dB floor (enemies behind walls stay trackable).

### Phase 3 tools (3.0, built 2026-11-15)

1. **Sound capture**: `--audio-capture FILE` (forces the silent loopback device; works at `--replay-speed N`, the
   capture is still in game time): FILE = the mixed output (WAV, stereo 16-bit 48 kHz; header kept current once a
   second, so a run that ends in exit() leaves a valid file), FILE.frames.csv = per presented frame the capture's
   sample position and the listener, FILE.voices.csv = every playing voice every 6th frame (category, position,
   distance, distance gain, volume, occlusion, out gain, direct / direct HF / reverb send as handed to OpenAL, and
   the RMS the voice rendered since the previous row, before gains). Replays stay in sync (m4_smoke 2620/2620,
   m2_playtest full run).
2. **Compare captures**: `tools/audio_compare.py [--out DIR] [--window 60] a=A.wav b=B.wav ...`: windows cut by
   replay frame, per window loudness (RMS dBFS of the mid signal), five bands (<200, 200-800, 800-3k, 3k-8k, 8k+),
   left/right balance; mean and worst change vs the first capture; per voice category out level, occlusion, direct
   HF, send. Noise floor (two identical variants, m4_smoke): <= 0.2 dB per window, 0.00 mean. First use: m4_smoke
   original vs enhanced = -0.01 dB overall, low band up to 4.7 dB in single windows (reverb).
3. **Sound at a mark**: every F12 mark (and `COD_BRO_MARKS=900,1500` = unattended marks at those presented
   frames, no note prompt) writes `<mark>_sound.txt`: the environment in effect, the 14 probe rays (distance +
   surface name), then every playing voice nearest first with its gains and each of the 7 occlusion lines (clear,
   or the surface hit, where, contents, how much it blocks), the detour cost and the occlusion recomputed now.
4. **Sound view**: debug view `sound` (F10 in --dev, `--debug-view sound`; Vulkan): the 6 nearest playing 3D
   voices as rings at the sound (green clear .. red occluded; label slot, distance, occlusion) and their lines
   over the picture: clear green, blocked red up to the hit, glass blue, the rest of a blocked line dim red, a
   detour yellow; 20 times a second, drawn with Dear ImGui's background list (render::dev::set_shapes, set by the
   game through RendererPcHooks::set_overlay_shapes; snd.cpp update_sound_view). In first person the lines start
   at the camera (dots); third person / lab orbit shows them whole. Drawing only (replays in sync with it on).
   Projection: refdef origin/axes, the scene's tan_across/tan_down (fallback refdef fov). Not yet checked by eye.

### 3.1 done (2026-11-15): own acoustic scene + ray worker

- `src/engine/sound/snd_scene.cpp`: the world trees (0x0095f4fc; per tree +0x30/+0x34 meshes of 0x50 bytes, +0x38/+0x3c
  brushes of 0x40) copied on the game thread when their list changes (zones stream in and out: m2 rebuilds ~10 times,
  0.1-0.4 ms copy). Mesh: +0x30/+0x34 vertices (16 bytes), +0x38/+0x3c triangle words (indexes in 6-bit fields),
  +0x40/+0x44 one plane per triangle - the triangles are wound to face their plane (the game's ray trace hits fronts
  only). Brush: +0x10 centre, +0x20 half size, +0x30/+0x34 side planes {normal, dist}: box planes + sides clipped into
  polygons (doubles), wound outwards. Contents solid/glass/foliage/water copied, surface type per object (flags
  bits 20-24). A worker builds the BVH (`acoustic_scene.cpp`, binned SAH, 4 per leaf): m4 44,765 triangles in 36 ms.
  The old scene stays in use until the new one is built.
- Rays as the game's: only front faces hit, so a ray starting inside a brush ignores that brush (Q3 startsolid
  without allsolid - treating it as "inside" was wrong: 449 of 210k lines differed until fixed).
- Check `COD_BRO_SOUND_SCENE_CHECK=1` (both traces per ray, on the game thread): m2 1 of 64k probe rays and 53 of 210k
  occlusion lines differ; m0 0.13 %, m11 0.09 % of lines; m4's far-apart probes are all the listener at (0,0,0)
  before the player exists. Ours 0.8 us per ray, the game's 2.3-3.6 us.
- `RayWorker` (snd_env.cpp): each sound update the game thread posts the listener, the probe (every 6th update) and
  every playing 3D voice when the worker is idle, and applies what finished (environment target, occlusion targets;
  glides stay on the game thread). New voices are still traced at once on the game thread (a shot behind a wall
  starts muffled). Check mode runs jobs inline (CM_BoxTrace is game-thread only). `COD_BRO_SOUND_TIME=1`: m2 game
  thread 6-11 us per update, worker 5-70 us per job (3-35 voices).
- Capture A/B vs 3.0 (`tools/audio_ab.sh`, m2): loudness -0.02 dB mean; per voice (matched by frame + position)
  dialogue/effects/bullets nearly equal, gunfire |change| > 0.1 in 24 % of rows: every voice is now refreshed each
  update instead of 8 in turn, so short voices follow the scene sooner.
- Replays: the sound path no longer bumps the collision checkcount. Recordings made before 3.1 can then differ in
  stale heap words only: m0_playtest frames 11574-12356, entity +0x2ec of entities 451/449/178 = a world-tree
  object's checkcount mark left in zone memory that the entity reuses and the game never writes for it (gdb: written
  by CM_TraceThroughWorldTrees at frame 4987). Nothing else differs to the end (28352 frames).
- `tools/audio_ab.sh OUT REC name="[LOADER::]OPTIONS" ...`: captures in parallel + audio_compare.

### 3.2 done (2026-11-15): transmission

- `SndEnv_Propagation` (snd_env.cpp): per occlusion line the scene's crossings (trimmed as trace_line) give solids
  (enter .. leave; a leave without its enter = started inside, not counted; touching brushes' shared faces cancel).
  Each solid loses `kTransmission[surface]` dB per band (low <250 Hz, mid, high >5 kHz) at a reference thickness,
  6 dB per doubling (mass law) within [half, +24]. Band energies averaged over the 7 lines (clear lines pass all);
  the edge detour is a second path (loss 1/2/4 dB + 6/12/20 x cost), the louder wins. Floor: the middle band
  never below -18 dB; the others keep their difference to it (lows <= mid, highs up to 12 dB more) so a sound behind
  concrete stays dull. A blocked line with no solid between crossings (a one-sided mesh) counts as a 2-unit wall.
- Device: `SoundDevice::set_transmission` (band gains; mid = gain, LF/HF relative) -> EFX band-pass (low-pass when
  LF is 1). EFX cannot make lows louder than mids: lows are clamped to the mid. The occlusion amount still sets the
  reverb send and the sound view. Switch `transmission` (settings, launcher, PC menu; off = the fixed 0.3/0.5 muffle).
- Glides per band on the game thread like the amount. Capture table: `direct_lf` column; marks: each line's solids
  (material, thickness) and the band losses.
- m2 A/B (off vs on): loudness -0.13 dB mean, 6 dB in the window with the most fire behind cover; occluded rows:
  3-6 dB (partial cover, some lines clear) or 15-18 dB (behind walls, highs 10-12 dB more); in sync.

### 3.3 done (2026-11-15): partial cover

- Switch `partial-cover` (on): when neither the sound's centre nor its muzzle point is in sight (in sight = full),
  32 lines - centre, muzzle point, 30 over a 40-unit sphere (Fibonacci lattice) - instead of 7, each
  with its transmission; energy averaged, smoothed by the per-band glide. Every voice refreshed each update (3.1).
- New voices start on the game thread with the 7 lines (32 there cost up to 0.7 ms in gunfire bursts); the worker
  refines them the next update. m2: game thread <= ~0.12 ms per update; worker mostly < 0.1 ms per job, heavy
  fights 0.3 ms average / 2 ms worst (jobs then run every other update).
- m2 A/B (off vs on): mean loudness unchanged, occluded losses spread finer (more at 2 dB and 6-14 dB, fewer at the
  7-line steps); in sync.

### 3.4 done (2026-11-15): sound through openings and round corners

- `snd_paths.cpp`: an air grid round the listener (64-unit cells, +-1280 across, +-384 up/down; 10 neighbours:
  sideways, diagonal, up, down), cells joined where the scene (faces from both sides) leaves the line between
  centres open; Dijkstra from the cells the listener sees, up to 3000 units. Edge tests are cached per scene
  (cleared over 4M). Built on its own thread (PathWorker in snd_env.cpp: a first build is up to ~80 ms) about 4 times
  a second or when the listener moved 64 units; the ray worker uses the latest finished field.
- Per hidden voice (occlusion > 0): the way back from the reached cell next to its centre that sees it, string-
  pulled into waypoints (length, bends, the opening = the first waypoint). Loss per band 2/5/9 dB per bend (up to 4)
  + 2/4/6 dB per doubling of (1 + extra length / 200); its energy is ADDED to what comes through the walls (both
  are heard). The direction: the straight one turned towards the opening by the way's share of the middle band.
  Device: `Transmission::redirect/position` (OpenAL position = listener + direction x real distance; distance gain
  stays the game's); glides on the game thread (0.2 per update), back to straight when no way is heard.
- Switch `openings` (on). Diagnostics: capture table `turn` (degrees between voice and where it is heard); marks print
  each hidden voice's way (length, bends, waypoints; the mark builds its own grid on the game thread).
- Game-thread cost fix: a new voice gets a quick first estimate (centre + muzzle lines, the centre line's
  transmission; `SndEnv_QuickPropagation`) - the full trace on the game thread cost up to 4.7 ms in m0 bursts.
  Now (one run alone): game thread 7-35 us per update average, worst ~0.46 ms; ray worker up to ~0.8 ms per job
  average in m0's busiest part.
- Energies are clamped at 1e-30 (1e-12 flattened the band difference behind hills); lows at most 6 dB better than
  the mid (the device holds them at the mid anyway).
- m0 A/B (off vs on): +0.14 dB mean; marks show 1-2 bends through doorways/round corners, heard 14-17 deg turned;
  sources past the grid (> ~1300 units) keep transmission only. Replays in sync (m2, m4_smoke; m0 = the 3.1 note).

### 3.5 done (2026-11-15): detailed reverb + echoes

- Switch `reverb-detail` (on; with room reverb): `probe_dense` (snd_env.cpp) - 256 Fibonacci directions from the
  listener plus every 4th from 4 points 48 units round it that it sees (weights: listener half, the points half).
  Per ray (an open one = an opening at 1200 units taking everything): volume (4 pi / 3) r^3, surface 4 pi r^2 / cos
  (cos >= 0.25), absorption per band (new low table `kAbsorptionLow`, 125-250 Hz). Sabine per band -> decay, HF and
  LF ratios (EAX reverb DECAY_LFRATIO / GAINLF / LFREFERENCE 250 Hz, new Environment fields room_lf,
  decay_lf_ratio); reflections level/delay/direction from the near hard surfaces (weight (1 - alpha) x nearness,
  delay weighted by its square); late delay from the mean free path 4V/S; room, room_hf, late level, diffusion as
  the 14-ray probe. Off = the 14 rays.
- Switch `echoes` (on): outdoors (sky >= 0.5) the strongest distant hard surface (600-3000 units, near horizontal,
  strength hardness x facing x 600/r) gives an EFX echo on a second send (ALC_MAX_AUXILIARY_SENDS 2): delay (EFX max
  207 ms), a second surface >= 40 ms later as LR delay, no feedback, damping by distance, side from the listener's
  facing, send level <= 0.3. Only gunfire and own gunfire are sent. `SoundDevice::set_echo`; OpenAL is updated only
  on audible changes. Log: "echo: level, ms, second, damping, side".
- m0/m2 A/B (both off vs on): loudness +0.01/+0.03 dB, air band +0.2-0.35 dB; reflections now 9-27 ms from real
  near walls; echoes 90-110 ms (17-19 m), level 0.1-0.26. In sync (m2).
- Timing note: in 4x loopback replays the game thread sees occasional 1-3 ms stalls inside OpenAL calls (filter and
  effect updates while the mixer renders flat out); our rays are not in them. To be checked at normal speed in 3.8.

### 3.6 done (2026-11-15): air absorption

- Switch `air` (on): world voices lose 0.040 dB/m above the 5 kHz shelf and 0.0047 dB/m in the middle (ISO 9613-1,
  20 C, 50 % RH; 100 m: highs -4 dB) on top of the walls' filter (sound_engine.cpp update). The game's falloff stays.
- Ground effect left out: over soft ground it is a dip around 200-600 Hz, which EFX's direct filter (low/high shelves
  round one gain) cannot make - it needs an EQ of our own (later, with the own mixer).
- m2 A/B: air band -0.72 dB mean (6 dB in distant-fire windows), high -0.38, loudness -0.10; gunfire direct HF
  0.85 -> 0.63 average; in sync.

### 3.7 (2026-11-15): the deferred audio findings

- Omaha (m9) / cutscene dialogue panned left (only in cutscenes). Checked on the capture tables (new
  columns: listener front/top, camera forward and origin = cgame refdef 0x00972bc0 / 0x00972bb0) over m6, m8, m9:
  the listener follows the camera in position and facing (no span over 64 units or 20 deg longer than a frame),
  its top stays world-down (no left/right flip), dialogue voices are world voices like any other. m9's recording
  has no cutscene speech before frame 4524 (the boat ride: effects only); the checked dialogue frames were gameplay.
  Cause not found from the recordings. Diagnostic added: the mark dump prints the listener's facing, the camera's,
  and per voice "direction from the listener" (+ right) - one F12 in a cutscene that sounds left gives the answer.
- m7 frame 1664 (line playing twice): the game starts the line on two dialogue voices at once, at x -1232 and -1000
  (heard -42 and +69 deg), same envelope 2:1, also with `--audio original` and more-voices off: not our device.
  Whether the original does it too needs the level/sound script path (our C in this build) - left for the decomp.
- Gela (m6) missed line: no mark or frame was recorded for it; the m6 capture has dialogue throughout. Needs a mark.

### 3.8 gate, automated part (2026-11-15)

Every recording (m0-m9, m11 night) captured as original sound / enhanced before phase 3 (p30 = build of 9de60c77,
build/lab_reference/p30) / phase 3 (`build/p38_gate.sh`, results build/p38/<m>.txt):
- Sync: phase 3 = p30 except stale collision-checkcount words (old recordings, see 3.1): m0 from 11574, m1 from
  39833 (76 words, all count-sized, old/new 1.3-1.8x), m7 from 55790 (2 words). Known drifts identical in both
  (m3 52398, m4 38347, m8 15379, m9 59014). `--audio original` differs early everywhere: the recordings were made
  with enhanced sound, and the voice setup feeds back into the game.
- Loudness, phase 3 vs p30 (mean dB): m0 -0.80, m1 -0.07, m2 -0.22, m3 -0.14, m4 -0.22, m5 -0.30, m6 -0.37,
  m7 -0.17, m8 -0.27, m9 -0.25, m11 -0.53; highs -0.1..-0.7 (walls, air). Enhanced stays louder than original by the
  earlier passes (close boost, voices).
- Cost at normal speed (m0, 6.5 min, silent): game thread 47 us per sound update on average, worst 2.0 ms (5 of 78
  ten-second spans had an update over 1 ms: OpenAL filter/effect calls); ray worker 1.7 ms per job on average, worst
  26 ms (jobs run every other update then; the air grid builds on its own thread).
- Builds: Linux release + debug, Windows (MinGW) clean; settings/audio/category unit tests pass. Full ctest not run.

### Omaha cutscene panning, found (2026-11-15, build/diag/p3_m9_1115, recording m9_cutscene_1115)

F12 marks while looking at each speaker in the boat cutscene: Brooklyn mostly right ear, Hawkins mostly left, balanced
only looking straight ahead. Mark dumps: the speech is two "effects" voices (volume 0.92, distance gain 1.0) fixed at
(-4004, 29397, 54) and (-4156, 29214, 50) - where the boat was when the scene started - while the listener rides from
x -3762 to -688. gdb: started by FX_AddSoundComponents -> FxSoundComponent_Update -> S_StartAliasVoice2 on an entity
(handle 0x21fd); S_ReapplyChannelParams re-sends the entity's origin (+0x140) every frame, and that entity is static
in every position field (+0x20 trajectory, +0x100..+0x140) and not linked (+0x3cc 0). The drawn speakers move, so
their movement lives elsewhere (client/drone side). Cause (found the same day): the entity is no speaker. Omaha's level code (x86, 0x006b32b0) spawns two
`script_origin`s (dump entities 553/554) at a level entity's origin and plays a stereo mix split into the aliases
`OMA_E01_L` / `OMA_E01_R` on them, about 2 frames apart, through FX sound components: fake stereo from two world
spots that stay where they were spawned while the boat moves. The game code has 44 such `_L`/`_R` pairs, nearly all
cutscene events of every mission (`Gel_E00`, `PIA_E04_SceneA`, `TRO_E14`, ...; plus a few effects: a train
explosion, a convoy, barn doors). Fix (enhanced, switch `stereo-scenes`, on): start_alias_voice notes each voice's
alias (`Snd_GetAliasName`; SndVoice_Create returns a pool handle = the record's +0xcc, not its index); when both
halves of one name start within 250 ms the device plays them at the listener, 30 deg left/right, no distance,
walls, reverb, air or echo. Original sound keeps the game's placement (whether the Xbox did the same is not known).
Mark dumps show "(stereo left/right)". The cutscene recording m9_cutscene_1115 drifts from frame 4295 in every build
(made in dev mode with nine F12 pauses).
Re-check (build/diag/p3_m9_1115b): the halves play at the listener as intended; the mix itself puts Hawkins mostly
left (one finished stereo track, not per-speaker voices), so that part stays as recorded.

## 0.6: one settings schema + native keys

Before: ~30 player options written three times (options.cpp parser/help, launcher main.cpp, pc_menu.cpp), several
settings.ini lines that packed many values (`graphics-features`, `volume`, `enhanced`, `keys`), and keys/mouse buttons
reaching the game as an emulated pad that follows the controller style. The engine's Q3 key system is decompiled
(keys[256] at 0x0093425c, CL_KeyEvent, key name tables, +forward/+back/+moveleft/+moveright/+attack kbuttons).

Design: keyboard/mouse have their own PC bindings straight to game commands (the controller style only affects
gamepads; the old `keys` setting converts); settings.ini has one line per setting (old combined lines are still
read and rewritten in the new form); one table of every setting (key, type, default, range/choices, label, help,
group, live or restart, player or developer) gives CLI parsing, --help, settings.ini and the old-format conversion;
developer switches stay CLI-only and hidden; --compare ignores settings.ini. Native keys: SDL key/mouse-button
events -> Q3 key numbers -> the event queue -> CL_KeyEvent and the bindings; the pad path stays for gamepads and
menus; replays record key events.

### 1.0a + 1a done (2026-11-14)

- Lab (b6ea534c): `tools/lab.py run OUT --mission m0` - two variants of the prologue in ~3 min, 8/8 pictures each.
  The automatic points include a loading screen (600) and the end screen; give m0 real capture points in
  `tools/data/capture_points.csv` before the 1e pilot.
- Hash bridge: `COD_BRO_TEXTURE_LOG=FILE` (ngl_gfx `log_texture`: each texture content once, with its layout) and
  `tools/texture_bridge.py UNPACKED MANIFEST LOG... --out bridge.csv`. Prologue: 250 of 258 drawn textures match a
  dump DDS by `hash_bytes` over the DDS data as is (DXT1 180/180, DXT3 41/41, DXT5 28/32, A8R8G8B8 1/4 swizzled);
  the 8 others are made at run time (key-prompt icons 64x64 DXT5, a 640x480 linear frame, font/target pages, a 1x1)
  - none belongs in a pack. Collector's Edition: its oran packs differ as files, but all 440 of their texture
  entries have retail contents, so a pack keyed by content covers both editions.

### 1d done (2026-11-14): pack builder

- `tools/texture_pack/build.py WORK [--source REGEX] [--bridge CSV] [--curation CSV] [--zip] [--pbr]`: stages
  select -> upscale -> encode -> pack, each resumable in WORK (cod_bro/textures/pack_work/, outside the repos).
  `upscale.py` runs under chaiNNer's portable Python (Tools/chaiNNer/python: torch 2.7 cu128 + spandrel; default
  `--torch-python`) with Tools/PBRify_Remix 4x-PBRify-UpscalerV4 (DAT2, fp32 only: bf16 fails in its attention).
  The model never opens a picture in the assistant.
- Per texture: runtime key as texture_bridge.py (swizzled for uncompressed), alpha none/cutout/blend (DXT1 alpha and
  0/255-only alpha = cutout: thresholded again after the model, then DXT5), wrap per axis (edge step <= 2x the inner
  neighbour step -> padded with the other edge, else mirrored: no seams on repeating textures). Alpha goes through
  the model as grey (chaiNNer's split/merge). Skipped: sides < 16 texels, curation `skip`. Mips: each level Lanczos
  from the top one; Pillow's DXT encoder (system Python, Pillow 12).
- Speed on the RTX 2080 SUPER: 128-px tiles + 16 margin, ~1.5 s per 256x256 (x2 with alpha). Prologue pilot
  (`--source '^sp/prologue/|^sp/global\.cod|^sp/frontend\.cod'`): 380 textures (16.7 Mpx), 9.5 min, 246 MB.
  Whole dump (179.6 Mpx): ~1.5-2 h, ~3 GB (most textures 128-256 px -> 512-1024).
- Check: shrunk back to the original size, pack textures match the originals at 36-40 dB, colour shift < 1 level.
  Lab `build/lab/pilot_m0` (Vulkan, mods off vs the pilot): pack loads (380 textures, no warnings), replay ends
  normally, 8/8 pictures; in-level points change 2-17% of pixels, sharpness 1.07-1.78x, brightness +0.25-0.72
  levels; loading screen and end screen unchanged.
- First play run (pilot): brighter surfaces (church tower) and shimmer at a distance. Causes and fixes:
  the model brightens (+0.76 levels average, +3 on walls) -> colour fix in encode (low frequencies matched to the
  original, alpha-weighted so colour under transparent texels does not leak into edges; Pillow resizes RGBA
  premultiplied - shrink per channel); lightmaps were upscaled (model shifts them up to 12 levels) -> skipped
  automatically, fonts too (sharp text covers them); Enhanced lod_bias -2 on 4x pictures shimmered -> both back ends
  skip the Enhanced mipmap bias for replacement pictures; mips now 2x2 boxes (Lanczos levels were crisper).
  Pilot 0.2: 351 textures, 232 MB, visible brightness -0.28 average (max 3.8 on HUD cut-outs); lab
  `build/lab/pilot2_m0`: brightness -0.35..+0.11, sharpness 1.15-1.75x, Vulkan = D3D9.
- Second run (pilot 0.2): no shimmer, brightness fine; paused A/B (marks 3/4): brightness -0.29, distance 0.8x as
  sharp as the Enhanced originals (lod_bias -2). Pack pictures get no Enhanced mipmap bias.
- 1.0c sheet: `tools/texture_pack/sheet.py WORK` -> WORK/sheet/index.html, opened via `--serve PORT` (Flatpak Firefox sees no img/ over file://; never published: game
  textures): original (nearest) vs pack per texture, 1:1 zoom, PSNR sort, upscale/conservative/skip + masks kept in
  localStorage, "Export curation.csv" for build.py --curation. Pilot: 351 textures, 27 MB of WebP previews.
- Campaign build started 11-14 (`--source '^sp/'`, WORK pack_work/campaign): 2,911 textures, 1,866 upscaled
  (97.5 Mpx), 1,018 lightmaps + 3 fonts + 24 small skipped. MP packs left out (MP draws on the original D3D path).
- Campaign pack 0.1 (pack_work/campaign/mod): 1,660 world textures, 1,197 MB, ~55 min GPU. On the pilot sheet
  the world textures all pass; menus/HUD/UI do not -> builder class `world`/`ui` (shell/, hud/,
  textures/global button prompts = 206 ui textures, `--classes`), ui left original until its own workflow.
- UI workflow (models for clean art): candidates 4x-AnimeSharp (CC BY-NC-SA), 4x-IllustrationJaNai V1 DAT2,
  4x-Nomos2 otf ESRGAN (CC BY 4.0); bake-off on the 206 ui textures, model column in the sheet.
- Lab `build/lab/campaign` (m1-m7, mods off vs campaign pack, Vulkan): pack loads everywhere (1,660 textures, no
  warnings), 56/56 pictures per variant; per mission at most 2.7-10.9% of pixels change, brightness -1.01..+0.13,
  median sharpness 1.03-1.34x. m3/m4 exit 3 in BOTH variants = the known plain-replay drift (frames 52398/38347).
- UI bake-off: 5 models in Tools/UI_models (SOURCES.txt: AnimeSharp, Nomos2, IllustrationJaNai V1 DAT2, V3 detail
  DAT2, V3 denoise DAT2; V3 FDAT does not load in chaiNNer's spandrel 0.4.1; upscale.py loads weights as fp32 since
  V3 ships bf16). Each `build.py pack_work/ui_<model> --classes ui --model M` (~3 min ESRGAN, ~8 min DAT2).
  `tools/texture_pack/compare_sheet.py sheet OUT --variant NAME=WORK ... --serve PORT`: original + every model per
  texture, one is picked (or the original kept), "Export picks.csv"; `compare_sheet.py merge OUT picks.csv
  --variant ...` assembles the UI mod.
- UI pack 0.1 (pack_work/ui/mod, 205 textures, 142 MB; picks in ui/picks.csv: AnimeSharp 101,
  JaNai V3 detail 76, V3 denoise 26, V1 2). Both packs together: pack_work/mods_all (symlinks pbrify_world + ui);
  lab `build/lab/ui_m0` (world vs world+ui): both mods load, <= 1.1% of pixels change (HUD), brightness +0.05.
- OPEN BUG (lab m0 pictures; also with mods off = not the pack): the shell-shock blur does not cover
  the whole widescreen picture. m0 shot 16424 (pilot2_m0/off and ui_m0/world alike) is blurred in the middle and
  sharp at the sides (Laplacian per 1/32 column: left ~370 px sharp; boundary not a clean 4:3 split). Traced so far: CG_DrawShellShockBlur's import slot 0x1a (0x00928550) is RE_EmptyStub26
  (draws nothing); CG_ShellShockScreenParams writes 0x00973a58, which CG_DrawActive's caller folds into the refdef
  word 0x00961774; ngl blend modes 5-7 (constant alpha) = the blur's blend.
  11-14 later: the earlier trace was wrong - 0x00961774/78/7c are view/turn limits (cl.cpp), not blur values. The
  shell-shock blur is CG_DrawFrontBufferFade (last frame as a quad at (4,4)..(640+4,480+4), mode 5); in m0 it runs
  at replay frames 10384-10502, 12462-12580, 18199-18317, 26382-26507, 26696-26934 - NOT at 16424 (that shot is
  smoke + distance). Shot 12468 (inside a fade) covers the whole 16:9 width. UI narrowing (draw_placement.h
  ui_narrowing) skips quads not starting at x<=1, but a span-based test changed 0 pixels at 16:9 and 21:9: the fade
  quad is not narrowed - change reverted.
  CAUSE (build/diag/blur_1114b, marks 3+4 at frames 12839/16283: it does not reach the left or top edge): the
  fade quad's (4,4) offset leaves a left and top strip uncovered - hidden by TV overscan on the
  Xbox, visible on a PC (wider at render scale). Same family as the glow pass's left/top dark band. A fix would be
  a PC option (quad to the picture edges, e.g. x0/y0 0 with the UV shifted). Parked.
- OPEN BUG (m0 run build/diag/blur_1114b mark 1, frame 8959): one soldier's mesh torn - legs and
  lower body stretched to the ground in long polygons; gone 16 s later (mark 2). Geometry/skinning, not the texture
  pack. Not chased (no Vulkan draw dump); possibly the uninitialised blend-node read seen in m3 (unverified).

### 1.0d done (2026-11-14): object viewer in game and in the lab

- `--viewer M1,M2` or `--viewer @FILE` (implies --dev; src/engine/game/g_viewer.*, src/runtime/loader/viewer.*): F8
  shows the next model in front of the camera as a level script would (`GScr_Spawn("script_model")` + `G_SetModel`,
  G_DObjUpdate, SV_LinkEntity), hides the gun (`cg_drawgun 0`) and pauses 30 frames later - paused at once, the
  client never takes the new entity up. Paused, it turns (F9 on/off): `G_SetAngle`/`G_SetOrigin` each frame show
  while paused. Freezing scripts and AI = the pause; no level-specific work. Overlay: shown model, next k/N.
- Placement: the middle of the model's box on the camera's level line (in the air for small models - framing over
  ground contact), distance from `DObj_GetBounds` (half diagonal in an 18 degree half-height view, >= 48 units),
  turned about the box middle. The spot is the camera's at the first F8 (press it where it is open).
- Names: the .xbmesh entry name without extension (`tools/model_index.py UNPACKED` -> tools/data/model_index.csv,
  2,255 models; `--level N` prints one mission's list for @FILE). Models sit in different zone sets (prologue:
  characters 6, props 7, vehicles 12, scenery 13; weapons in the level's): the viewer tries the level's set, then
  0-31. Only zones loaded at that moment are found: at m0 frame 6000, 83 of the prologue's 155 (the rest: later
  zones' sbmodel* scenery, the intro cutscene's LOD models).
- Lab turntable: `tools/lab.py viewer OUT --model M.. [--at 6000] [--steps 8] --variant a="--mods off" --variant
  b="--mods-dir DIR"`: the recording cut after --at (tools/lab.py cut_recording; the 'F' records) and replayed with
  --replay-continue - from there live and WITHOUT input: under a full replay the recorded usercmds still move the
  player while cl_paused (msec 0 stops only the server frame). `COD_BRO_VIEWER_TURNTABLE=STEPS,FRAMES` holds fixed
  angles and exits when done; metrics only inside the model's box (what moves between the baseline's angles).
  2 models x 2 variants: 1 min 45 s, 32/32 pictures.
- Pausing the audio stalls a replay (voice queue waits of 10 s): the viewer's pause under a full replay is no use;
  the cut recording avoids it.

### 1.0b done (2026-11-14): `--dev` overlay, live switches, texture picker

- `--dev` (src/render/dev_mode.*, drawn with the --diag overlay; never with --compare): frame time (avg/worst per
  second), GPU memory (Vulkan: VMA budgets of the device-local heaps; D3D9: GetAvailableTextureMem), replacement
  pictures held, pack counts (loaded / pending / failed / title textures asked for that no mod covers).
- Keys: F1 overlay, F2 mods on/off (`texture_pack::set_enabled`: version() moves, devices ask again and fall back to
  the title's picture), F3 Enhanced, F4/F5 one feature, F6 picker, F7 pause (`cl_paused` + audio pause: the world
  stops, frames are still drawn - A/B while paused). With --diag an F12 note gets `[dev: mods.., graphics.., pick..]`.
- Picker (Vulkan only; GfxDevice::pick/take_pick/label_texture): one frame draws only the centre title pixel, each
  draw into the full picture inside an occlusion query; results a frame later. Overlay: depth-tested hits top first;
  the log lists every hit with all four texture slots. `COD_BRO_TEXTURE_NAMES=bridge.csv` adds the dump's names.
- Unattended: `COD_BRO_DEV_KEYS="6@1800,2@2000"` presses Fn at presented frame N. Checked on m0 (test pack): F2 off/on
  on both back ends (tint gone/back), F7 frames pixel-equal while paused, picks name the wall/lightmap/smoke under the
  crosshair, no Vulkan validation messages.

### 1b done (2026-11-14): texture packs load and show on both back ends

- `src/render/texture_pack.*`: scan `<data>/mods` (or `--mods-dir DIR`; `--mods off`; none with --compare),
  `textures/<hash>.dds` index, one loading thread, generation counter as `replacement_version` (the device asks
  again once a picture is ready; handed-out copies dropped). `TextureReplacement` carries DXT1/DXT5 levels; Vulkan
  BC1/BC3 images, D3D9 DEFAULT pool via a SYSTEMMEM copy + UpdateTexture. Replacements not bound for
  `kReplacementIdleFrames` (1800) are freed on both back ends (made again when asked).
- Test pack: `tools/test_mod.py UNPACKED MANIFEST bridge.csv OUT` (tinted 2x, full mip chains; 250 prologue
  textures, 34 MB). Lab `build/lab/packs`: mods on changes 27-99% of pixels at the 8 points, Vulkan = D3D9 pictures
  (0.00% differ), all 28,352 frames match. Real-time m0 replay with --diag: hitches 34 (on) vs 35 (off), worst
  spots the same level loads. Not yet exercised: freeing across a level change (needs a multi-mission run).
- Left for phase 1 around it: 1.0b `--dev` overlay + live mod toggle, 1.0d viewer, 1f `mod-<name>` lines + Data
  Files page. 1.0b, 1.0d and 1d done (sections above); NEXT 1e curation.

### 1b design (2026-11-14)

- **Mod layout:** `<data dir>/mods/<name>/mod.ini` (`name=`, `version=`, `author=`, `kind=textures`, `source=` dump
  it was built from) + `textures/<16 hex runtime hash>.dds` (DXT1/DXT5 with mip chain; the file name is the index).
  Developer `--mods-dir DIR` (lab runs use copied data folders); `--mods off` = none (and always none in --compare).
  Per-mod `mod-<name>=on|off` lines arrive with 1f; until then every mod found is on, load order = name order.
- **Picture type:** `render::TextureReplacement` gains a format (BGRA8 / DXT1 / DXT5) and ready mip levels (bytes
  per level); BGRA8 without levels keeps today's box-filtered chain (sharp text, key prompts). Vulkan:
  `TextureImageDesc.format` BC1/BC3 + `upload_level` per level; D3D9 `upload_replacement`: D3DFMT_DXT1/DXT5,
  DEFAULT pool (no managed copy).
- **Loader (src/platform/mods.*):** scan -> map content -> file; `replace_texture`: loaded -> picture, else queue for
  the worker thread and answer null (the title's own shows); `replacement_version` moves when it is loaded, so the
  device asks again. CPU copy dropped once handed out; device side frees replacements not drawn for a while (today
  they are never freed) under a VRAM budget. Log: mods found, textures each covers, prologue misses.
- **Check:** test mod from the dump (tools/lab: prologue textures tinted, via Pillow DXT), lab variants `--mods off`
  vs on, both back ends; no hitches (frame times), memory flat over the m0 replay.

### 0.6a done (2026-11-13)

- `src/platform/settings_schema.{h,cpp}`: every setting once (key, --help placeholder, type Flag/Switch/Int/Float/
  Choice/Text/Path/Custom, group, flags live/command-line/launcher/hidden, range or choices, default, help, the
  combined setting it is a part of). `check_setting` gives the normal form or what the setting needs;
  `settings_help` is --help; `upgrade_settings` turns the older combined lines (graphics-features, volume, reverb,
  enhanced) into one line per part (defaults left out); `combined_setting` / `set_combined_setting` read and write
  the parts as the "k=v ..." text the structs (GraphicsFeatures, CategoryVolumes, ReverbTuning, EnhancedFeatures)
  parse. New part names: fxaa, anisotropic, sharpen, sharpness, vibrance, vibrance-amount, texture-detail,
  lod-bias; volume-effects/-ambience/-dialogue/-music, close-gunfire, own-gunfire, close-bullets; resampler,
  more-voices, room-reverb, occlusion, close-boost; reverb-amount/-decay/-brightness/-reflections/-clutter.
- options.cpp: parse_args looks every option up in the table, checks it there (errors "--scale needs a whole
  number (1-8)") and `apply_setting` puts the value into Options; only the Custom ones (diag, screenshots,
  replay-reference/-shots/-dump, force, text-trap, launch-data, session-start) keep their own code. Combined
  settings and their parts merge in order (settings.ini, then the command line; a later value wins) - before,
  `--graphics-features X` replaced the whole settings.ini line, the A/B pitfall of 0.5b.
- settings.ini: load_settings upgrades on read, so the next save (launcher, PC menu) writes one line per setting;
  launcher-only keys (check-updates) are not passed to the game. Recordings keep their settings text as recorded
  (combined lines still parse). `keys` stays one line until 0.6c (bindings change there anyway).
- Checks: settings_test (schema checks, defaults pass their own checks, no key twice, upgrade, combined round
  trip, as_args) on Linux and under Wine; m4_smoke replay all 2620 frames (recorded combined lines applied as
  before), m1_playtest all 57333 frames, m4 again with parts on the command line (merged as expected: "fxaa=0 sharpness=0.5
  vibrance_amount=0.01", volumes music=40 close_gunfire=2 ...), the player's settings.ini (copy, XDG_DATA_HOME)
  read as before; Windows MinGW build clean.

### 0.6b done (2026-11-13)

- settings_schema gained the UI table (`SettingUi`: section, label, tooltip, choice labels, slider format, parent
  condition "sharpen" / "graphics=enhanced", checkbox for two-way choices, logarithmic) in drawing order.
- New `src/ui/settings_widgets.{h,cpp}` (library settings_ui): `draw_settings(groups, Settings&, {require_flags,
  custom})` draws the table's rows with their sections, indents and disables children by their parents, and edits
  settings.ini text (defaults removed). A custom callback draws the few rows the standard widgets do not fit.
- Launcher: the `Choices` struct, read_choices/write_choices and the per-setting widget code are gone; the tabs edit
  `settings_` directly (Graphics = Display + Graphics groups, Sound, Controls + the keys table, Multiplayer =
  Network). Custom rows: aspect (screen shape), window size (resolution), render scale, texture filtering, frame
  rate, audio device, net port, d3d9 (Windows only). New in the launcher: field of view. The tab bar stays put and
  each tab scrolls above the buttons. "Reset to defaults" clears every table row (assets and hand-written lines
  stay).
- PC menu: `live_values()` reads what runs now (graphics + parts, audio + parts, volumes, reverb, fov, mouse
  sensitivity) into settings text, the tabs draw the kLive rows from it, `apply_live` pushes what changed, and
  closing saves the changed keys (one line each). New live: mouse sensitivity (patches set_mouse_sensitivity).
  Six hand-written save blocks and the slider helpers are gone.
- Checks: settings_test (UI rows name one setting each, parents exist, choice labels match, put_setting /
  setting_enabled) Linux + Wine; m4_smoke replay all 2620 frames; Windows build clean. Visual check of both UIs by
  eye (desktop capture on a Wayland session gives stale frames).

### 0.6c done (2026-11-14): native keyboard and mouse

- Keys and mouse buttons no longer reach play as an emulated pad. `ClientPcHooks::native_actions` (set by the
  loader's patches when IN_Frame, CL_KeyEvent and Key_ClearStates run as C) returns the held actions as a bit mask
  (src/input/bindings.h's Action order); IN_Frame compares it with the engine's own key table (`keys[key].down`) and
  queues SE_KEY downs/ups for key numbers 0xdf + action (JOY17 on; the pad uses JOY1-16, no config binds above).
  No state of ours: a restart or Key_ClearStates simply lets a still-held key press again.
- The commands come from `native_key_command` (cl.cpp), not the binding table: the default layout's commands
  (+forward ... +attack, +speed, +activate, +reload, +moveup, lowerstance, +sprint, +melee, +grenadeattack,
  weapnext, toggle cl_run) but plain +leanleft/+leanright for Q/E (the default layout's +lean*switchnext switch
  weapons unless aiming; Q/E lean); "+speed" becomes "toggle
  cl_run" while the toggled-aim flag 0x008dce70 is set (as Key_Bind_f does). Why not bind letters in keys[]:
  Key_GetKey / Key_GetKeyNameForCommand scan keys 0-255 and would find "F" before the pad's JOY6, so the game's own
  button prompts would change; the table also needs Z_Malloc'd strings (heap layout). The controller style now
  only affects gamepads.
- Input side (src/input): `set_native_actions(true)` stops the action keys giving pad buttons outside menus;
  `held_actions()` is 0 in menus, while our PC menu blocks input and until the keys that closed it are let go. In
  menus the action keys still steer as pad buttons (WASD, F); the menu keys, Escape and Tab stay pad buttons. The
  look keys (IJKL) are gone (the mouse looks); old `keys` lines with look-* are skipped.
- Replay format 5: one 'Y' value (the mask) per IN_Frame. Format 4 recordings still replay (their keyboard is in
  the pad state; the hook returns 0 for them). `--press` takes action tokens too (`move-forward@76+4,fire@81`).
- IN_Frame is Replaced now (was Compare: its harness tests are inconclusive, it reaches host input; own-main replays
  m0-m7 had run its C all along).
- Checks: m4 recording with scripted actions (walk, two shots 8 -> 6 rounds, turn, crouch, next weapon, aim, jump;
  build/keys06c) replays all 5,855 frames in release/debug/Windows; m4_smoke (format 4) all 2,620 frames in all
  three; m1_playtest all 57,333 frames (release).

### 0.6d done (2026-11-14): rebinding

- PC menu: new **Controls** tab (mouse sensitivity etc. moved there from General) with the keys table; changes
  apply at once (`input::set_bindings` is thread-safe now, `bindings()` returns a copy) and are saved when the menu
  closes. The table is shared with the launcher (`ui::KeyTable`, `draw_key_table` / `key_table_event` in
  settings_widgets): click a cell, press a key/mouse button/wheel, Esc cancels (the game window hands the menu
  every event first, `BackendConfig::pc_menu_event`, so Esc does not close the menu while a key is wanted),
  right-click clears; a key taken from another action says so; an action without keys shows in orange. ` is
  reserved now (it opens the menu).
- Actions: "D-pad down" removed (no command in the default layout); "Right stick click" is **Aim down sights
  (toggle)** (`toggle cl_run` flips the walking bit +speed holds - CL_KeyMove; the game's own toggled-aim option
  binds the same). Older `right-stick-click` / `dpad-down` / `look-*` words are still read.
- settings.ini: one `key-<action>` line each (`key-melee = v mouse5`), parts of the combined `keys` (command line
  `--keys` and recordings keep the combined form). A repeated action in an old line keeps both inputs; parts parse
  onto the bindings so far (a later line for an action replaces it).
- Prompts: the keyboard labels already come from the bindings each poll (update_button_prompts), so a rebind shows
  in the game's hints at once.
- Checks: settings_test (upgrade incl. repeats and the old aim name) + bindings_test, Linux and Wine; the player's
  settings.ini copy reads `melee=v melee=mouse5`, a bad key name is logged; replays: m4_smoke 2,620 and the 0.6c
  keyboard recording 5,374 frames, release/debug/Windows all matched. tools/replay_check.sh reads Wine logs as text
  (grep -a; Wine's log made it report "no replay result").

### Launcher front page (2026-11-14, closes phase 0)

- Bethesda-style front page: the picture over the whole window (16:10 now, 700x440 x UI scale), the game's logo
  top right, a button column Play / Multiplayer (with the MP XBE) / Options (the existing tabs, Back saves) / Tech
  Support (the former Help tab + updates) / Exit; missing game files, the update notice and the version bottom left.
- Picture (src/launcher/front_picture.*, from the player's own files): a random unlocked concept art image -
  bonus\concept<1 for chapters 1-7, else 2>\cp<n>_<i>f.dds, chapter n unlocked when a profile slot's chapters
  reached (profile +0x14; the Profiles/Profiles1-3 records hold the profile from +0x1c after the "9-21-05" header)
  is above n, all with +0x24 (as FE_BonusConceptSetText's unlocked_level_word) - else the main menu's
  Single_Front_bkrnd.dds (sp/frontend.cod). Both are shown at 4:3 with their letterbox bars cut. Own DXT1/3/5
  decoder; KapfPack for the pack.
- Developer: `COD_BRO_LAUNCHER_SHOT=FILE` (+ `COD_BRO_LAUNCHER_PAGE=options|tech`) saves the window after five
  frames and exits - look checks without the desktop. Checked Linux and Wine; concept art with a test save copy
  (chapter 9 reached: 16 images of chapters 1-8).

## 0.5: Vulkan back end

Starting point: every renderer draw goes through `render::GfxDevice` (R4b/R5, [own-renderer.md](own-renderer.md));
D3D9 is the only back end (DXVK on Linux). Vulkan becomes a second `GfxDevice` + `Backend` beside it, written
from scratch. 32-bit Vulkan is available (NVIDIA and Mesa i686 drivers, loader, headers; llvmpipe for headless tests).

**Vulkan is the default, D3D9 the fallback** (launcher choice); render graph + HDR came with 0.5 (as 0.5b);
third-party **volk + VMA** (MIT, FetchContent).

Shader spike 2026-11-13: vkd3d's HLSL front end cannot take our SM3 HLSL straight to SPIR-V, but the d3dbc we
already build converts: `vkd3d-compiler -x d3dbc -b spirv-binary` on all 166 title shaders, no errors (not yet
validated with spirv-val, no bindings chosen yet).

| Step | What | Check |
|---|---|---|
| V1 device + window + present | SDL3 Vulkan surface; Vulkan 1.3 minimum (dynamic rendering, sync2; D3D9 covers older GPUs); 2 frames in flight; FIFO present = `--fps display`; diag overlay, gamma ramp, screenshots / `read_target`, movie frames (`write_back_buffer`); `--renderer d3d9\|vulkan` + launcher choice (the launcher itself never touches Vulkan) | menus and logos show; pacing as D3D9 |
| V2 shaders | build time: our HLSL -> d3dbc (as now) -> SPIR-V with vkd3d; one fixed binding layout (VS c0-c195, PS c0-c27, samplers); screen-to-clip constants c192-c193 per back end (Y flip, pixel centres) | `title_shader_test` Vulkan vs D3D9: all identical |
| V3 GfxDevice on Vulkan | textures (staging uploads, our decoded formats), render targets + `target_texture`, static buffers + ring buffer for dynamic / `draw_user` (keep the content-hash cache), pipelines keyed by our state blocks + pipeline cache on disk, depth bias, clears | `gfx_device_test` on both back ends |
| V4 post passes | FXAA, sharpen, vibrance, brightness (same HLSL); Enhanced switches unchanged | A/B screenshots |
| V5 gate | `--replay-shots` m0-m7, D3D9 vs Vulkan through `shot_compare.py` (bar: about the Wine level, <= 0.01% of pixels); frame time vs DXVK; Windows build under Wine; a play run | default switches to Vulkan |
| V6 (0.5b) render graph + HDR | frame as passes (scene -> HUD -> post -> present), RGBA16F linear scene target, Original mode still matching, FXAA before the HUD | foundation for the lighting work |

Risks: pixel-centre / clip conventions (V2's test), 32-bit address space with a Vulkan driver loaded (watch in V3),
NVIDIA vs Mesa differences (test both). Validation layers needed for development
(`vulkan-validation-layers` + `.i686`).

### 0.5 progress

**V1 done (2026-11-13): window, device and present on Vulkan.** `--renderer d3d9|vulkan` (settings.ini `renderer`,
launcher "Renderer"; default d3d9 until V5; Vulkan falls back to D3D9 without a Vulkan 1.3 device),
`--vk-validation` / `COD_BRO_VK_VALIDATION=1` (Khronos layer messages in the log as `vulkan` warnings/errors).
- Shared by both back ends: src/render/game_window.h (SDL window, events, F11/F12, Alt+Enter, prompt, PC-menu keys and
  Dear ImGui context, picture placement + window_to_screen, gamma ramp with brightness, overlay text pixels,
  write_bmp). The D3D9 back end was moved onto it (mission 4 run and gfx_device_test unchanged).
- src/render/vk/: vk_context (volk, VMA, Vulkan 1.3 dynamic rendering + synchronization2, 2 frames in flight, per-frame
  command buffer, 16 MB staging ring, descriptor pool, deferred destruction, image layout tracking; COD_BRO_VK_DEVICE
  picks a device by name, e.g. llvmpipe), backend_vulkan (swap chain sized to the window, FIFO when the display paces /
  mailbox / immediate as the D3D9 path; present pass = back buffer through the gamma table between black bars,
  overlay, notice, PC menu with ImGui's Vulkan renderer; screenshots, movie frames), gfx_vulkan (V1 GfxDevice:
  targets at the render scale, clears, read_target, flat-frame tracking for pillarboxing; textures, buffers, shaders
  and draws are bookkeeping only, draws counted as unsupported).
- Our own Vulkan shaders: GLSL in src/render/vk/shaders, glslang at build time (cmake/embed_spirv.cmake). The vkd3d
  command line cannot take a binding layout (it crashed on SM5 resources), and Fedora ships no vkd3d headers.
- Checks: boot on Vulkan with validation: intro movies and logos shown, 0 validation messages (one fixed: views on
  copy-only images); the opening videos play. Mission 4 on Vulkan: black as expected (no draws yet),
  pillarboxed frames the same as D3D9's. Windows build under Wine: Vulkan device on the same GPU, movie frames saved.

**V2 done (2026-11-13): title and post shaders as SPIR-V with one fixed layout.**
- Build: every title / post HLSL -> d3dbc (as before) -> SPIR-V with `vkd3d-compiler -x d3dbc -b spirv-binary`;
  both embedded (TitleShader / NamedShader `spirv`, GfxDevice ShaderCode `spirv`).
- vkd3d numbers bindings in order of use and locations by register number (a VS's COLOR0 is o1, the PS reads it as
  v0), so src/render/vk/spirv_layout.cpp rewrites the decorations at shader creation, reading the semantics from the
  d3dbc's `dcl` tokens: set 0 binding 0 = VS constants (195 float4), 1 = PS constants (28), 2-9 images t0-t7,
  10-17 samplers s0-s7; VS inputs location n = TEXCOORDn; VS outputs = PS inputs COLOR0 0, COLOR1 1, TEXCOORDn 2+n,
  FOG 10; PS colour outputs unchanged. The Vulkan GfxDevice makes shader modules from it (draws still V3).
- Checks: `vk_shader_layout_test` (all 74 VS + 92 PS + 3 post: layout applied, bindings in range, every title PS
  input written by every title VS with enough components, post pair likewise) and `vk_shader_layout_spirv_val`
  (spirv-val --target-env vulkan1.3 on all 169: valid). Mission 4 on Vulkan with validation: 0 messages.
- Moved to V3: the picture check (`title_shader_test` on Vulkan vs D3D9) needs draws. Its and gfx_device_test's
  hand-assembled helper shaders have no SPIR-V: give them SPIR-V at build time (convert the tokens with
  vkd3d-compiler in the test build) when V3 runs those tests on Vulkan. Screen-to-clip c192-c193 per back end
  (Vulkan: Y down, no half-pixel offset) also lands with V3's draw path.
- V3 (GfxDevice on Vulkan): reference behaviour = src/render/gfx_d3d9.cpp, interface = gfx_device.h.
  - V3a resources: textures (Format -> VkFormat: BGRA8/BGRX8 B8G8R8A8, 565/1555/4444 as the PACK16 formats or
    converted to BGRA8 where optional, L8/A8/A8L8 as R8/R8G8 + component swizzle in the view, DXT1/3/5 = BC1/2/3;
    2D/cube/volume, mip levels, update_texture through the staging ring), target_texture (target image + view,
    texel_coordinates -> c18-c25 scale), replacement_texture, static buffers (VMA, staged) + a per-frame ring for
    Dynamic maps (Discard = new ring range, NoOverwrite = same range) and draw_user (keep the content-hash cache).
  - **V3a done 2026-11-13:** textures (2D/cube/volume, mip chains, formats above; A4R4G4B4 = B4G4R4A4 with
    moved channels), replacements (BGRA8 + box-filtered chain, rebuilt on a version change), static buffers
    (device memory, staged copies in command order with barriers both sides) and dynamic buffers (host-visible
    versions: Discard / Write-while-queued renames, NoOverwrite in place; `VkBuf::used` must be set by V3b's
    draws). Context: create_texture_image, upload_level, upload_buffer; the staging ring is also vertex/index/
    uniform usable (draw_user and constants can read it directly); textureCompressionBC enabled. m4 on Vulkan
    with validation: ~120-157 textures with images, 4 replacements, 0 messages; the game itself makes no
    GfxDevice buffers (all draw_user) - buffers get their check in V3c's gfx_device_test.
  - V3b draws: one pipeline layout (set 0 = spirv_layout.h bindings; push descriptors or per-draw sets from
    Context::descriptor_set), pipelines keyed by (shaders, layout, strides, primitive class, blend, depth/stencil,
    raster, target formats) in a hash map + VkPipelineCache on disk; dynamic state for viewport, scissor, stencil
    ref, blend constant, depth bias. Quads and fans -> index lists (as D3D9 does for quads); alpha test in the
    pixel shader? (D3D9 has the fixed test: check how gfx_d3d9 does it, Vulkan has none -> spec constant or a
    PS c-register + discard needs the HLSL; decide then). Constants: VS c0-c194 / PS c0-c27 per draw into the ring
    as two UBO ranges. Screen-to-clip c192-c193 for Vulkan (Y down, no half-pixel offset; viewport transform as
    D3D9's at the render scale).
  - **V3b + V3c done 2026-11-13** (draws; tests). Pipeline layout = one push-descriptor set (VK_KHR_push_descriptor
    now required), pipelines keyed by shaders/layout/topology class/blend/fill/formats, everything else dynamic
    (1.3 extended dynamic state incl. vertex stride); constants per draw in the upload space (one piece for both
    blocks); draw_user streams through the upload space or the kept-vertex cache, retried once if staging flushed;
    quads through a shared index buffer; unbound textures read opaque black, missing vertex inputs (0,0,0,1);
    sampling the target drawn into goes through a copy; X-format targets sample alpha 1 and DestAlpha -> One.
    Shared with D3D9: draw_placement.h (viewport/scissor/screen-to-clip incl. widescreen edges and UI narrowing;
    Vulkan = y flipped + half-pixel centre) - D3D9 refactor checked pixel-identical (m4_smoke 4 shots).
    Fixed alpha test moved into the title pixel shaders (nv_alpha_test, c28 = func/ref; D3D9 passes Always and
    keeps its fixed test - checked pixel-identical also with the shader test forced on D3D9).
    Findings: (1) the renderer's target for the back buffer's memory must BE the back buffer (D3D9Draw keys
    targets by memory; Vulkan now shares a target per memory with a user count); (2) vkd3d's SPIR-V `texkill r.x`
    tests the register's other components too (stale values discarded whole draws): every clip() in our HLSL now
    takes a replicated float4 (`.xxxx`; D3D9 unchanged).
    Tests: gfx_device_test runs on both back ends (its shaders are now tests/shaders/*.hlsl, D3D9 + SPIR-V) and
    gained an alpha-test case (8 functions x 3 references). m4_smoke replay D3D9 vs Vulkan: all 2,620 frames
    equal; loading screen identical; in-game shots differ only by the constant-alpha blends below.
    **Glow pass (default off).** ngl blend modes 5-7 use CONSTANT_ALPHA with blend colour
    alpha<<24; D3D9 has no constant-alpha factor (D3DBLEND_BLENDFACTOR = rgb = 0), so on D3D9 every release dropped
    R_DrawFogPass's result: a "fog" pass that some levels enable (m4: 256x256 copy of the picture, 6 blur passes,
    added at ~0.8 over the screen) - a strong haze on Vulkan. Its blur carries a dark band (~20 texels) at the left
    and top: the copy quad's edge lies on the first column/row's pixel centres, they stay black (also at render
    scale 1) and the blur spreads it. `--glow on|off` (default off; RendererPcHooks::skip_glow_pass, set under
    pc_features: compare runs keep the original). Checks: m4_smoke all frames equal; D3D9 identical to before;
    Vulkan vs D3D9 <= 0.01%; --glow on shows it again.
  - V3c tests: gfx_device_test + title_shader_test on Vulkan too (their hand-assembled shaders need SPIR-V: write
    the tokens out and convert them with vkd3d-compiler at test build time, or ship them as small .hlsl);
    COD_BRO_VK_DEVICE=llvmpipe for headless runs; then mission 4 on Vulkan with validation (pictures vs D3D9 by eye
    before V5's shot_compare gate).

**V4 done (2026-11-13): enhanced graphics' post passes on Vulkan.** src/render/vk/vk_post.cpp (`vk::Post`): FXAA,
then sharpen + vibrance, the same HLSL as D3D9 (post_vs/fxaa/sharpen SPIR-V through spirv_fixed_layout; own
push-descriptor layout with only bindings 0, 1, t0, s0); picture <-> scratch image, copied back if it ends in the
scratch; only on frames with a 3D scene (as D3D9). Vulkan differences: vertex c0 = 0 (no half-pixel offset), quad
y flipped. Brightness (gamma table in the present pass, V1), anisotropy and texture detail (sampler, V3) were
already there. Checks (m4_smoke, `--graphics enhanced` given explicitly: the recording's settings say original):
D3D9 enhanced vs Vulkan enhanced <= 0.01% of pixels (the same as with the passes off), enhanced vs original ~4%
(the passes run); all frames match; validation: no messages from the post passes. Known validation WARNINGS from
title pipelines (since V3, harmless): VS writes FOG (location 10) the PS does not read; vertex attributes 5-7 the
VS does not consume.

**V5 gate, replays (2026-11-13):** m0-m7 on D3D9 and Vulkan (`--graphics enhanced`, SPEED=8, a shot every 3,000
frames, build/v5.txt): every mission's frames equal between the back ends (m3 and m4 differ from their recordings
from frames 52398 / 38347 on both alike = the known plain-replay drift, not the renderer); pictures <= 0.01-0.04%.
Three shots off by 8-59% were the **shell-shock blur** (CG_ShellShockScreenParams; ngl blend modes 5-7 =
constant alpha): D3D9's BLENDFACTOR is the whole colour, the title sets only alpha, so D3D9 never showed it (all
releases). gfx_d3d9 now copies the alpha into rgb when a constant-alpha factor is used; the three shots now
<= 0.01%. (The glow pass uses the same modes: still off by default, `--glow on` now shows it on D3D9 too.)
Also found while watching: the soldier health bar in 16:9 (8a61e0d4, XMM0 lost across
UIPicture_ScaleXForAspect). Left for V5: frame time vs DXVK, a play run on Vulkan; then Vulkan as default.

**Pipeline cache on disk (2026-11-13, after an Omaha play run on Vulkan with hitches up to 678 ms):**
`<data folder>/cache/vulkan_pipelines.bin` (BackendConfig::pipeline_cache_path), loaded when the header matches the
GPU/driver (vendor, device, pipelineCacheUUID), saved every 1,800 frames when it grew, on window close and on
Ctrl+C/kill (render::save_caches from the exit hook; not after a crash). New pipelines >= 50 ms are logged.
m4_smoke: 20 pipelines 627 ms (slowest 47 ms) fresh, 8 ms from the file. Omaha (m9) recording: plain replay drifts
from frame 59014 (both back ends alike, not chased yet); D3D9 vs Vulkan pictures equal. A mark (boards popping
in, frame ~36800) is the same on both back ends = game side, object not identified yet.

**Pipeline libraries (2026-11-13, a complete shader cache):** with VK_EXT_graphics_pipeline_library +
extended_dynamic_state3 (blend enable/equation, write mask, polygon mode) + vertex_input_dynamic_state, every title
shader (166) is compiled into a library on 8 worker threads when the device is made (~0.35-0.55 s, during the
logos); a draw's pipeline = fast link of vertex input (per primitive class) + VS + PS + fragment output (per
formats) libraries, its key only shaders / class / formats; vertex input, blending, write mask and fill set per draw
(dynamic_draw_state). No mission runs needed. Without the extensions (or COD_BRO_VK_NO_LIBRARIES=1): whole
pipelines as before. Checks: m4_smoke 19 links in ~1 ms (whole: 627 ms cold); pictures identical to whole pipelines,
D3D9 <= 0.01%; validation clean (only the old FOG-output warning); Wine OK; gfx_device_test passes; m4 playtest at
--replay-speed 50: 228 s vs 222 s whole (noise ~10%) - no optimized relink needed for now.

**V5 closed (2026-11-13): Vulkan is the default renderer.** `--renderer vulkan|d3d9` (default vulkan),
settings.ini `renderer=d3d9` / launcher "Renderer" (Vulkan first) choose Direct3D 9; without a Vulkan 1.3 device the
game still falls back to D3D9 by itself. Recordings without a `renderer` line now replay on Vulkan (the game state
does not depend on the back end). Checks: m4_smoke default (Vulkan) vs `--renderer d3d9` all 2,620 frames equal,
shots <= 0.01%; Windows build under Wine picks Vulkan, frames and shot equal to Linux. Side items left open: Omaha (m9) plain
replay drifts from frame 59014; the mark
"boards popped in" at ~frame 36800 (same on D3D9; object not named yet); second timing pass not repeated.

**V6 / 0.5b done (2026-11-13): render graph + HDR scene (Vulkan).**
- Frame shape: world (the title's 3D view and gun) -> scene passes (HDR resolve, FXAA, sharpen/vibrance) -> HUD (the
  title's draws after the world) -> present (gamma, overlay, PC menu). FXAA and sharpening no longer touch the HUD,
  subtitles and letterbox bars (they did on both back ends before; D3D9 keeps the old order).
- The split: nglRenderSceneTree numbers its scenes (children first) and nglBeginSceneRender calls
  `GfxDevice::begin_scene(world)`; the world is every scene up to the last one that is on the screen (render target
  flag 0x4000), writes depth and has opaque nodes (m4: the 3D view with ~700 nodes, then the gun's own scene with a
  depth clear; the HUD scenes after it have only translucent nodes or no depth). Only with
  `RendererPcHooks::mark_scenes` (set with the PC features; --compare runs keep the original calls). D3D9 ignores it.
- src/render/vk/render_graph.{h,cpp}: passes over named images (imported or transient = owned, kept by name across
  frames), each image moved into the layout its use needs before the pass, passes whose results nothing reads culled.
  It does not reorder (the title's draws are immediate); lighting passes go between the world and the resolve later.
- HDR scene (Vulkan only; on whenever enhanced graphics is on - its own switch `hdr` was dropped 11-13: later
  lighting features become part of enhanced graphics, and the scene alone looks the same):
  at the frame's first world scene the device swaps the back buffer target's image for an RGBA16F one (what the frame
  drew so far blitted over; the sampling copy swaps too), so the world's draws and blends keep 16-bit floats and
  are not clamped between passes; at the split `scene_resolve.frag` writes it into the 8-bit picture (clamped, = what
  an 8-bit target holds) and the HUD is drawn in 8 bits as before. The values stay in the title's gamma space (its
  shaders and blending are written for it; true linear blending would change the look): linear light for later
  passes = pow(value, 2.2), the resolve is where a tone map goes.
- Checks: original graphics m4_smoke Vulkan vs D3D9 identical / <= 0.01% (no split work without enhanced features);
  enhanced HDR on vs off <= 0.01% (m4 shots, m1 4 shots 0.00%), all frames equal (m4 2,620, m1 57,333); enhanced
  Vulkan vs D3D9 differs only where the HUD is no longer anti-aliased (m4 "Objectives updated.", m1 compass, health
  bar, prompts: 0.04-0.6%); validation clean with HDR on (only the old FOG-output warning); Wine identical to Linux;
  gfx_device_test / shader tests pass. Pitfall met on the way: `--graphics-features "hdr=0"` replaces the recording's
  whole feature string (its sharpness=0.3 was lost) - give the recorded amounts with it for A/B runs.
- Not done in 0.5b: GPU timings per pass (quality gate's budget), D3D9 FXAA before the HUD.

## 0.4 progress (2026-11-13)

- **Mounted-gun drift fix** (`--mounted-gun-fix on|off`, default on, off with `--pc-patches off`):
  CL_FinishMove rounds the view angles to the command's 16-bit units (floor(x + 0.5)) instead of truncating
  (`ClientPcHooks::round_view_angles`). Check (m1 truck MG, COD_LOOK_TRACE=1): idle on the gun the original turns
  13.6 deg pitch / 13.7 deg yaw over 2,533 commands, with the fix -0.07 / +0.01 (vehicle motion).
- **Game options in recordings:** options that change the game's state are written as `#game NAME=0|1` lines
  after a recording's command line (title_session.cpp `apply_game_options`); a replay uses the recorded value,
  a recording without the line gets the original behaviour (all recordings before 0.4 replay unchanged: m1 all
  57,333 frames match). Naming the option on the replay's command line overrides it (A/B runs).
- **Pacing decision: variable step at the display's rate**, not a fixed tick with interpolation.
  Reason: the single-player client draws the server's own entities (one table, 0x0097a968; cgame "cents" are the
  gentities) and skeletons come from the server's animation state, so drawing between two ticks would need a
  render-side save/lerp/restore layer for every entity and skeleton. Possibly revisited with the own renderer.
  On 60 Hz displays the gain is steady frame times and input latency.
- **Native frame pacing** (`--fps display|30|N`, default display; launcher "Frame rate"): with the renderer as C
  the title's vertical-blank wait is off (frame interval 0) and the host paces right before the present
  (patches.cpp "Native frame pacing", render::pace_frame/set_frame_limit): `display` = present waits for the
  display (D3DPRESENT_INTERVAL_ONE, DXVK d3d9.maxFrameLatency = 2), N = our own clock (mailbox), 30 = the Xbox
  path unchanged; movies limited to 30. Replays never wait for the display (own clock x replay speed).
  maxFrameLatency 1 dropped mission 4's opening cutscene to 35-50 fps with sound;
  2 holds 60 there. Silent m4 80 s: display 17 hitches vs own clock 32.
  Checks: m4_smoke release/debug/Wine all frames match; HOST=1 gap_check m4/m2/m1 dumps identical to ws_host2.
- **Keyboard/mouse per frame: already so** (checked 11-13). Window events are read right after each present
  (device.cpp present_frame -> pump_events), and the next frame's CL_CreateCmd takes the mouse motion straight into
  cl.viewangles (ClientPcHooks::mouse_look) and the keys from the pad state read in the same frame - with the game
  stepping once per displayed frame, input reaches the usercmd of the very next frame. The mouse also turns tank
  turrets and mounted guns (m2 playtest trace: turret mode, mouse turns). Keys still go through the pad's sticks
  (forward 126 instead of the key path's 127, 0.8% slower; not changed).
- **0.4 closed.** Keys and mouse buttons still reach the game as an emulated pad on port 0 (merged with
  the first gamepad, remapped per controller style). Native keys (SDL key events into the engine's own Q3 key system:
  CL_KeyEvent, binding table, +forward/+attack; the pad kept for gamepads and menus) move to 0.6 with the settings
  schema and in-game rebinding.

## 0.3b widescreen progress (2026-11-13)

The cgame switch (11-07) had left `--aspect` wider than 16:9, `--fov` and the objectives panel's 4:3
narrowing as dead patches into original code. They are now hooks our C calls (`ClientPcHooks` in
src/engine/client/cl.h: draw_2d_begin/end, hud_draw_begin/end, hud_component_scene, objectives_scene,
fixed_fov, cull_ratio, bar_fill_fix), filled by the loader's patches.cpp when the function runs as C.
New: an edge-anchored HUD on pictures wider than 16:9 (each HUD component in a child scene, narrowed toward
its own edge) and the tank health bar's fill fixed (the original's own 16:9 bug). Details:
[rendering.md](rendering.md) "Widescreen". 16:9 runs keep the original paths: HOST=1 gap_check m0-m7
equal to the dllsplit baseline (m0/m3/m6 actor dumps: host-stack words only, as before).
Then: binoculars keep their 16:9 shape with black sides, letterbox bars/fades reach
the picture's top/left edge (a half-pixel gap at render scale, also in 0.2.2), CG_Draw2D's side texts anchored
(9ea38ff4, 9409375c). Left for 0.3b: death/mission-failed screens at 21:9+, 32:9 in play. After 0.3b : a procedural widescreen binocular mask under Enhanced.

## 0.1 inventory: what our C still takes from the loader

Generated by `tools/host_inventory.py` → [host_inventory.md](../formats/host_inventory.md) (tables) and
`host_inventory.csv` (rows). Numbers from 2026-11-07.

**Kernel: nothing from the engine.** 124 of 147 kernel imports are used, every call site sits in library
code: mslib (XAPI/CRT) 333, XNET 227, XPP 205, DSOUND 184, D3D 39, XONLINE 11, crt 2. Engine modules and
level sections call none. The kernel imitation leaves together with the libraries.

**Library entry points our C calls (~270), to be replaced by the host's own layer:**

| from the C | targets | becomes |
|---|---|---|
| mslib CRT and other helpers (math, string, stdio, malloc, UIX) + crt | 62 + 9 | host libc / our own helpers |
| mslib XAPI (files, threads, events, mutexes, time, memory status) | 44 | src/platform (file, thread, time) |
| D3D / XGRPH / D3DX | 108 / 15 / 3 | GfxDevice: textures, device creation, present, YUV movie quads (the open R5 leftovers) |
| XPP (controllers) | 12 | src/input |
| DSOUND | 5 | SoundDevice (movie audio, voice) |
| XONLINE / XNET | 10 / 2 | gone with Live (set aside); system link later on our own UDP layer |

Behind those entries sit 1,636 more mslib functions (the closure over direct calls); they go with them.

**Engine functions without C that the C still calls (~110):** memory (Z_Malloc, Z_Free, Heap_AllocAligned,
Mem_*), FS_FileExists/FileDevice_IssueIo, the script thread-switch family (Scr_Wait*, Scr_Suspend*,
Scr_StartThread_*/Scr_CallThread_* functors, Scr_ThreadEntry), Live_*/LiveMgr (gone), Save_* signature code,
10 unnamed functions, 6 named-only. Level code also calls the Scr_Wait family directly (not through a slot),
so the fiber runtime (scr_fiber.cpp, branch ts_compare) must own those addresses. Level slots without C: 4
(`_sprintf`, `_atol`, `_strncpy`, `G_SetupVehiclePool_Thunk`) - done 2026-11-08, see "Own CRT" under 0.3.

**C reaching C through original addresses: 3,281 targets.** `title_function<...>(0x...)` calls into
functions that already have C go through the hook jump today. In the host either every original address
keeps a jump into C, or a script rewrites those calls into direct C calls. Recommended: rewrite (scripted),
and keep jumps only where something outside the C holds the address (below).

**Code addresses stored in the image's data (vtables, callback/cmd tables): 1,180 targets, 958 with C.**
The data sections come from the dump unchanged, so these pointers still name original addresses: each needs
a jump at the original address (the thunk table grows from 345 slots to ~1,500 entries) or the tables get
rebuilt in C. The thunk route is cheaper and keeps the dump data as it is.

**Title data: 9,419 distinct addresses** (.data/.bss 4,888, .rdata 4,442). The host keeps mapping the dump's
data sections at their original addresses (level code needs 532 of them anyway); moving engine globals into
C-owned memory is later and optional.

### What the host has to provide (0.3)

1. Map from the dump: level sections, .rdata, .data/.bss (+ the small XTL/DOLBY sections if still read) at
   the original addresses. No .text, no SDK sections.
2. A thunk at every original address something outside our C can reach: level slots (345), direct level
   calls, code pointers in data (~1,180), the fiber runtime's Scr_Wait family.
3. Our own CRT/XAPI/D3D/XPP/DSOUND entry points for the ~270 library calls (and the 4 CRT level slots).
4. C for the ~110 engine functions without it (memory, file, thread switch, save signature, unnamed rows).
5. A scripted rewrite of `title_function` calls into C that exists (3,281 targets).

Open questions for 0.3: the 40+ mslib call sites into PROLOGUE/BUCHHOLZ/LIBEATOR code (probably misfiled
helpers, check in Ghidra), the 222 data-stored pointers to code without C (mostly mslib CRT/D3D tables),
and SEH via fs:[0] in level code (the host sets up an FS segment like the loader does today).

## 0.2 progress (2026-11-07, branch `own_host`)

`tools/own_calls.py` rewrites the C so it reaches our own C by symbol, not through the hook jump at the
original address (each pass is idempotent; rerun after new C lands):

| pass | what | sites |
|---|---|---|
| default | `title_function<F>(0xADDR)` / `(kConst)` → `own_function<F>(&Name)` (direct call, same convention) | 1,787 |
| `--helpers` | address-taking call helpers (`call_at`, `cg_call_regs`, `title_call_at_regs`, ...) → `own_address(&Name)` | 2,322 |
| `--values` | addresses used as values: level import slots, command tables, thread bodies, callbacks | 1,912 |
| `--consts` | `constexpr` address constants inlined at their uses, `uintptr_t{0x...}` asm operands | 54 constants (153 uses), 9 |
| `--generated` | the default and `--values` passes over generator output; the generators run it last | 9 + 354 |

Compare-status callees reached this way now run as C (0.2). Done by hand 2026-11-08 (with the passes above): the
constexpr struct tables of callees (fe_bonus, fe_menus, the HashMap instances, tank specs) became `const` with
`own_address`; `deleting<>` / `key_table_*<>` take `auto` (an address or our symbol, `target_function<F, T>()`);
asm `movl $ADDR, %reg; call *%reg` became `PLATFORM_ASM_CALL(Name_entry)`, `pushl $ADDR; ret` a `jmp`; two asm
stubs (MemContextScope_Enter, DebugDraw_AddLine) became plain C calls. Still on original addresses, on purpose:
comparisons/identities (xanim's `base == 0x002e4650`), `sample_root` and `Heap_CreateFromZone` arguments
(KEEP_CALLEES), the generated scr_field template arguments, and a few generated sites whose declaring header
the including .cpp lacks (4).

Fixed 2026-11-08: **SV_Map_f** now runs as C too (`KEEP_NAMES` is empty). The live crash under SV_SpawnServer's
C was UIScreenMgr_SetActiveScreen_entry (and UIDialog_SetText_entry, same shape) pushing EBP 0x20000 below its
entry ESP *before* GAME_GAP_ESP's floor check: called at the original's position, that word landed on the
SV_SpawnServer body's return address. EBP is now saved after the macro. Rule: an entry writes nothing below its
ESP until GAME_GAP_ESP has placed the body. Smoke runs m4/1/6 (debug) and m4/1 (release) clean.

## 0.3 progress (2026-11-08, branch `own_host`)

**Record/replay (the check for the host), done.** `--record FILE` writes every value the title's main thread
(thread 1) reads from outside, in order: clock (`cpu_time_stamp_counter` = the RDTSC sites + XAPI
QueryPerformanceCounter, and KeQueryPerformanceCounter), pad state, connected ports, mouse motion, menu pointer.
After each presented frame it writes a state record: level.time, entity count and hashes of the level locals
(0x00985370, 0x2708 bytes), the clients (level +0, 0x640 each) and every entity (table 0x0097a8c0: count +0x2aac,
pointers +0x2ab0, 0x460 bytes each). `--replay FILE` feeds the values back frame by frame and compares the state
record; it exits at the end of the file (0 = all frames matched, 3 = some differed) and logs the first differing
frame. Each frame also records one hash per entity (format 3), so a differing frame logs which entity indices
differ; the file is flushed every frame, so a crash keeps every frame before it (~110 KB per second of play).
`--replay-dump N DIR` writes frame N's raw state in either mode (`N-M` every frame of a range, `N-M:S` every S-th);
`tools/replay_diff.py A B N` lists the differing words. `--replay-no-draw` sends no draws or clears to the GPU
(resources, state and our renderer C still run; the window stays black): the same state at half the time (m1 frame
24420: 71 s vs 143 s). Use it for every state check; drop it only for pictures (`--replay-shots`). Code: `src/runtime/xbox/replay.cpp`; the hooks sit at the boundary the host has to provide
anyway (clock, XPP, SDL mouse/pointer), so the same mode works in the host.

Replay is per frame, not per read: the main thread sometimes reads the clock once more or less than in the
recording (waits on the loading thread, frames ~150 of a level start). Extra clock reads continue from the last
recorded value at the real clock's pace, extra other reads repeat the last value, unread values are dropped at
the frame's end. Other threads (file, sound) read real values. A recording covers one boot (dropped on reboot).

Checked 2026-11-08 on mission 4 (`tools/smoke_test.sh` presses, `--silent`, 45 s = 2,400 frames): replay in the
release build and in the -O0 debug build: all 2,400 frames match. Sensitivity: the clock stretched 1.2x from frame
1500 differs from frame 1502 (level.time 16163/16166); pad input zeroed from frame 600 differs from frame 809
(client 0 button words +0x528/+0x594/+0x59c, the only 3 differing words). With sound on (record and replay
without `--silent`): all 2,400 frames match as well. A 90 s play run (mission 4, keyboard/mouse) replayed
matching up to frame 3719, then entity-only differences on 317 of the remaining frames (cause open; the
recording had no per-entity hashes yet). That run crashed at 99 s in FXCmd_Alloc (entity pointer 3, called
through fx_cmd.cpp's Builder) - not reproduced by the replay because the old format lost the last unflushed
frames.

**Cross-build check:** `tools/replay_check.sh RECORDING SAVES MISSION OUT [NAME=LOADER ...]` replays one recording
in several builds at once (default: Linux release, Linux debug, Windows release under Wine), each on a fresh copy
of the save folder the recording started from, and prints one line per build (matched / first differing frame /
crash). 2026-11-08, mission 4, 2,670 frames: all three match. On the way it found three bugs: CG_FireWeaponEmpty's
missing EAX (crash in a play run), Vehicle_GatherCollisionCandidates' lane 3 (host garbage in the entity), and
G_SetModel's 5-word name-hash buffer (0x20 bytes needed): on Windows the overrun zeroed the caller's EBX/ESI and
crashed G_SpawnItem at every mission start.

**Fixed capture points:** `--replay-shots 600,1800,... DIR` (with `--record` or `--replay`) saves those presented
frames as `DIR/shot_<n>.bmp`; the frame numbers are the replay's (as `--replay-dump`), so one recording gives the same
pictures in every build or back end. `SHOTS=600,1800,2600 tools/replay_check.sh ...` does it for each build and runs
`tools/shot_compare.py` (first build vs each other; share of pixels off by more than 8/255). 2026-11-08, mission 4,
2,620 frames: release and debug pictures identical; Windows (Wine, other back end) at most 0.01% of pixels differ
(largest difference 115). The quality gate for 0.3b widescreen and 0.5 Vulkan uses this.

**Open (2026-11-08): run-to-run differences after ~90 s of a 13-minute playtest recording** (mission 4, after
the opening cutscene). Two Linux replays part at frame ~5800, Windows at ~5600 (and Windows then drifts
visibly: the player ends up elsewhere). First differing word: entity 402 +0x3cc, the tag link's address
(G_EntLinkTo, SmallHeap 0x0095f598, main-thread only) - so the main thread linked/unlinked in a different
order earlier, in state the hash does not cover. Suspect: sound and file completions, which the main thread
sees as written by the audio/file threads in real time (script waits on sound ends, zone load completion);
the replay records neither yet. Plan: record, per main-thread poll, which completions it saw, and on replay
hold them back / wait for them to match (fits 0.3: the host owns sound and file completion). Tools for it:
`--replay-hashes FILE` (a run's own per-frame state), `--replay-reference FILE DIR` (dumps the first frame that
differs from another run), `tools/replay_compare.py A B` (first differing frame and entities of two runs).
Fixed on the way: the cached trace box lane 3 in six more writers (Path_TraceGatherCandidates and others).

Format 4 (2026-11-08) closes the two suspects: a wait with a timeout on the main thread records its result (a
replay returns the timeout at once, or waits for exactly the object the recording saw signaled); voice status reads
are recorded ('S'); packets the mixer finishes are held until the main thread's DirectSoundDoWork, which writes their
status words, and the recording lists which ones ('k'/'K', a replay writes exactly those, waiting for the mixer when
it is behind). The recording also starts with the settings.ini text and command line it ran with ('O'/'A'): a replay
uses that text instead of the player's settings.ini (which may have changed, and under Wine is another file). Check:
mission 4, 45 s with sound, 2,963 frames match; a 12.5-minute play run (mission 4, keyboard/mouse, 45,498 frames) replays to the end in all three builds: Linux release matches every frame, Linux debug and Windows (Wine) each differ on 34 frames only (one entity for a moment - debug 676 from frame 38,723, Windows 582 from frame 11,417 - then back in step). Cause (hardware watchpoint): G_MissileImpact copies the trace's end point as 4 words into the entity (+0x60..+0x6c); the 4th is a lane the trace copies from its own stack, so it differed per build. G_RunMissile now sets it to 0 before the call. Source (found 11-08): no writer - CM_BoxTrace and SV_Trace compute the end point in all four lanes (start + (end - start) * fraction), and G_RunMissile's `end` gets only three lanes from BG_EvaluateTrajectory, so lane 3 carries a stale stack word (always the same in the original, left by our C here). The trace normal's lane 3 (G_MissileTrace's start - end over four lanes) has the same source when a trace starts solid. Since then release, debug and Windows agree on every frame (all three differ from that older recording on the same 34 frames).

**Own CRT (2026-11-08):** `src/engine/shared/title_crt.h` - our `_sprintf`/`_vsprintf`, `_atol`, `_strncpy`, written
to behave like the title's VC7.1 CRT, not the host's: floats from 17 significant digits rounded half up on the digit
string (`%.0f` of 2.5 = 3, 1e30 = 1 and 30 zeros), 3-digit exponents, `1.#INF00`/`1.#QNAN`/`1.#IND`, `-0` printed
without its sign, `%p` = 8 upper-case hex digits, 16-bit wide strings; atol wraps on overflow. `--check-crt` runs
ours and the title's side by side on a table (ints, flags, widths, `*`, `I64`, wide, rounding edges, denormals,
specials) at the first frame (the title's CRT needs its own init) and exits 3 on any difference: 0 differences in
Linux release/debug and Windows (Wine). The 4 level slots without C now point at our C (`G_SetupVehiclePool_Thunk`
is one `jmp G_SetupVehiclePool`), and ~130 `title_function` calls of the three in our C use ours; 11 calls through
stack-exact frame helpers / address constants still reach the originals (for the scripted 0.3 rewrite). Checked:
m4_smoke replay matches in all three builds; smoke runs of missions 0, 3, 5, 9, 10 (the slots' users) clean.

**Memory functions (2026-11-08):** of the 8 memory functions our C called without C, 6 were 5-byte `jmp` thunks to
functions that have C (`Z_Malloc` 0x00011040/0x00042c10, `Z_Free` 0x00011050, `Heap_AllocAligned` 0x00042c50/0x00042c60,
`Mem_AlignedAlloc` 0x00066af0, `Mem_AlignedFree` 0x000c3830). `tools/own_calls.py` now resolves such thunks (23 in
all, chains included: also G_SetupVehiclePool_Thunk, BroScr_WaitFrame x5, Phys_CreateWorld/DestroyWorld, ...) and
rewrites calls through them to the target's C (57 calls, 11 helper arguments, 4 values). The other two now have C,
untested by design (XPhysicalAlloc / debugger output cannot run twice): `Mem_AllocPhysicalPools` (the 0x232b000-byte
physical and 0x3ac000-byte write-combined pools; no debug pool) and `Mem_DebugPrintf` (never runs: its one caller is
behind the debug pool, which is always off). host_inventory.py: no memory function left; level slots without C: 0.
Checked: m4_smoke replay matches in all three builds; smoke runs of missions 0 and 5 clean.

**File device layer (2026-11-08):** `FS_FileExists` already had C; `FileDevice_IssueIo` and its helper
`FileDevice_AlignTransfer` (both set aside before) now have C, untested by design (they start real asynchronous I/O
through the driver's function pointer, so they cannot run twice): reads are rounded to the device's sector / offset
alignment and largest transfer and clamped at the file's end; device mode 1 takes only aligned transfers, 3 always uses
the bounce buffer, 2 and 4 when unaligned. Checked: m4_smoke replay matches in all three builds (every level load goes
through them); smoke runs of missions 0, 5 and 9 clean. The write path (op 1) is not reached by those runs.

**Engine functions without C (2026-11-08, partly):** 8 more now have C (NullCallback, ListVectorValue_Dtor,
Record54_Copy, ThreadHandleList_EndAll - 71/71 matched -, G_EntityWeaponRecordFlag, GScr_ConstStrings_Dtor,
BroScr_JoinHangarChain, FollowEnt_Worker; all on Compare, the others not reached yet: exit path, ORAN late, BUCHHOLZ
event 3). Left: Scr_WaitFrames / Scr_WaitRecord15-17 (thread switch, with ts_compare), the CRT ones below, mslib-filed
FUN_0002c442 / FUN_001fa100 / LoadScreen_SetProgress, and ScrEnt_Set/GetInt_370, LocStrings_Ctor/Dtor,
BrushModel_CopyBounds, FUN_0030f190, FUN_000b8990, FUN_001c2aa0.

**More own CRT (2026-11-10):** title_crt.h gained `tolower`, `atof`, `sscanf` (the `%f` lists the title's callers
use; also `%lf`, `%d`), `memset` (the level import table's memset2 slot now points at it) and the eh vector
constructor/destructor iterators; ~60 call sites in our C use ours. VC7.1's decimal conversion is not the host's: it
rounds to nearest but breaks an exact tie toward zero (sscanf `%f` of 9150587.5 = 9150587), and float denormals
round up only past 0.75 of a step (rounded to one more bit, then truncated). Ours converts with exact integer
arithmetic (no host strtod: msvcrt's ignores rounding modes and is not correctly rounded); VC's sscanf also takes a
bare exponent letter (`"1e 2 3"` reads 1, 2, 3). `--check-crt` adds tolower -1..255, an atof/sscanf table and 20,000
seeded random decimals: 0 differences in Linux release/debug and Windows (Wine). Known limit, logged but not counted:
of the 10,000 long ones (up to 20 digits, exponents to +-300) one near-tie at e-291 (0.5003 of a step) differs,
VC's 64-bit intermediate with inexact powers of ten (not ported; no game data comes near). Still on the originals:
`srand` and `_errno` (they write the title CRT's per-thread data, which `rand` and the CRT read - they move with the
rest of that data), `_ftol` (one x87 asm chain with `__CIacos` in ragdoll.cpp) and the 11 calls through
stack-exact frame helpers (scripted 0.3 rewrite). Checked: m4_smoke replay matches in all three builds.

**False compare mismatches since 0.2: fixed 2026-11-10.** A compare run on 2026-11-08 reported level-script bodies
(BroScr_*, AeThreadFunctor*) as mismatched where a memory word held our C's address of a function (own_address(&X),
0xf7... host) and the original's X's title address - the 0.2 `--values` rewrite, not a bug. The harness
(compare_linux.cpp, `g_own_to_title`) now treats the pair (C entry, original address) of every kFunctions row as the
same word; pairs, since several functions share one C body (the BroScr_EmptyFunc_* rows all use BroScr_Stub_Ret). Rerun
(missions 1,4,5,6): no code-address differences left. What remains in those rows is the older level-script family
(host heap pointers stored on one side only, +INF vs 0, the .data byte 0x00929b40); such rows still stay out of
commits.

Next for 0.3: the host executable itself (our exe maps the dump and owns the main loop, list above), checked by
replaying recordings made with the loader.

## 0.3 host executable: coverage (2026-11-10)

Two loader developer options measure what the host still needs:

- `--own-main`: mainXapiStartup's call to Sys_Main lands in our C (`install_jump`), so the main loop (IN_Frame,
  Live_Frame, Com_Frame) is ours, and **every** kFunctions row with C runs it, Compare rows too
  (`compare::Config::all_c`; rows that return within 5 bytes keep the original: a jump would overwrite the next
  function). The XAPI/CRT start-up before Sys_Main is still the title's.
- `--text-trap LIST OUT` (Linux): an INT3 on the first byte of every original non-level function
  (`tools/text_trap_list.py`, 14,124 entries from the Ghidra export) that is not hooked; the first run of each is
  logged with its caller, the byte restored. `tools/text_trap_report.py OUT [--rows]` groups them by module.

m4_smoke with both: 894 original functions still run (2026-11-10, after all_c): mslib 526 (XAPILIB 75, LIBCMT +
plain CRT ~150, UIX 44, MSVC EH), SDK 302 (XGRAPHICS incl. XGAssembleShader and its D3DX/XGRPH helpers), set-aside
engine functions ~45 (the script thread-switch family Scr_Wait*/Scr_Suspend*/Scr_CallThread_*/Scr_ThreadEntry,
Live_*/LiveMgr*, FileXapi_*, Sys_ReadLaunchInfo, Sys_GetStackBounds, Save_FirstPort, SoundManager_Ctor), the _cinit
static initializers (StaticInit_*, mostly with C) and ~15 unnamed helpers. The frontier (first caller is our C or
level code) is ~110 of them; the rest is library closure behind it.

**Replays with all C:** m4_smoke diverges from frame 604 on, but only in heap pointers: script thread bodies in C
(found by `tools/bisect_own.py`: BroScr_AnimNoteTracksThread, one more in the other half) have other frame sizes than
the originals, so the thread switch saves a stack of another size and the pool hands out other slots. At frame 2600
the dumps (`--replay-dump`, all-C vs Compare-rows-original) differ in 32 words, all pool pointers (entity +0x234
callback blocks, +0x264/+0x284/+0x3dc/+0x428 script objects); positions, health, level and client state are equal.
So the host's check is "every non-pointer word equal", not frame-exact hashes.

**Finding a drift (2026-11-11, m0/m1 recordings).** A replay on our C that later waits 10 s per sound packet ("the
recording finished packet ... the mixer did not") has drifted for real. Steps: `tools/np_scan.sh MISSION N-M:S TAG` (FILTER=lc) dumps of
a frame range (`--replay-dump N-M:S`, plain vs `--own-main`) and replay_diff with pool pointers (0x80-0x83xxxxxx,
pool ids in the high 16 bits, entity addresses) filtered out give the first frame with a real difference. Count only
level, client and entity origin words: the 0x460-byte entity dump reads past small heap objects (type 0xe), whose
neighbours differ (m1 frame 9110 was such noise). Then `tools/bisect_own.py REC OUT --speed 8 --dump FRAME
--reference PLAIN_DUMP_DIR --keep REGEX --fail 'Segmentation fault|crash  ' -- --replay-no-draw` (a step stops once
its dump is written). Keeping the first 710 Compare rows original crashes into the disc-error screen (original D3D
reached), so `--always-c` keeps those as C when they are clean. Found: BroScr_ShuffleForwardAnim (m0 frame 8447,
condition 0x36b1a5 || 0x77 read as "0x77 unless 0x36b1a5"), BroScr_ActorRunToEntIgnoringAll_2 (m1 frame 24420,
the twin difference is the third AI flag, not the node lookup flag). After both: m0 and m1 run to the end with no
waits and no non-pointer differences.

**Live dropped, static initializers in C (2026-11-10, 5a9fcf96).** Our Sys_Main (only reached with `--own-main`)
builds the Live manager's fields in C (LiveMgr_Construct/LiveMgrBase_Construct/LiveRecord_Construct: plain field
fills, g_liveMgr) and leaves out SetUnhandledExceptionFilter, the UIX layout stream, Live_Init, the launch-mode
invite branches, Live_Frame and Live_Render: m4_smoke state at frame 2600 equal except heap pointers. `_cinit`
(0x001eddde) is hooked to `game::title_cinit`: FP init hook + the CRT's 6 .CRT$XI entries still by address, then the
414 .CRT$XC initializers from `static_init_generated.cpp` (`tools/gen_static_init.py`: 32 with C, 376 translated
from their x86 - constant copies, atexit registrations kept in a host list, constructor calls by address, fill
loops -, 6 MANUAL by address: 0x003787b0, 0x007abead, 0x007abedd, 0x00372de0, 0x003768a0, 0x00376c70). State at
frame 2600 identical to the original _cinit's (0 words). Trap count m4_smoke: 894 -> 628.

Note: `--text-trap` only sees Ghidra's function entries; code Ghidra has no function for (most of these
initializer stubs were such) runs unseen. The host's own check is .text filled with INT3 except the jumps.

### Direction: a native PC game, not a loader in C

Two layers, two rules:
- **Game logic stays exact** (AI, physics, scripts, weapons, menus, the CRT behaviour it depends on): replays and
  `--check-crt` keep checking it word for word. That is what keeps the game playing like the original.
- **The platform layer is native.** Our own `main()`, files through normal host paths (data folder, user folder,
  cache), saves written straight to the user folder, threads/time/memory from `src/platform`. No kernel imitation,
  no drive letters or symbolic links, no LaunchDataPage, no XBE header reads, no Xbox TLS bookkeeping in our C.
  Xbox-only start-up work (partitions, title updates, auto power-down, kernel patches, TitleMeta.xbx/title images)
  is not ported at all.
- Check for the platform layer: replays (non-pointer state equal), save compatibility (the player's existing saves
  load and saves we write load in the original), not instruction-level equality with XAPI.
- One real constraint stays: levels remain original x86 from the dump, and they read globals at fixed
  addresses, so .rdata/.data/.bss stay mapped at their original addresses. That is a memory layout, not an
  Xbox imitation.

Where our C still leans on the imitation (11-11): `title_xapi.cpp` (69 kernel calls: files, time, events, threads,
settings) and `title_savegame.cpp` (14: files, signatures), plus the loader-side SDK redirects (D3D/XGRAPHICS, XPP,
DSOUND) that move into the host as they are.

### 0.3 work list (2026-11-11)
State 2026-11-11 (own_host): m4_smoke with --own-main --text-trap: 81 original functions left (139 on 11-10), m2
93 (153); both replays reach their end, m4 state at frame 2600 unchanged, m2 at 25900 equal to the plain loader's
except 300 heap-pointer words. Plain release replays still match every frame.

**Done 11-11 - 1. Script thread switch in C** (scr_fiber.cpp): our Scr_SuspendWaitTime/Frames/Till leave the wait's
frame as the original did (16-bit FLAGS, POPA block, FS:[0]) and call scr_fiber_wait (note the fiber's ESP, build the
wait record, switch back); Scr_WaitRecord15/16/17 and Scr_RestoreContext (kept instruction for instruction: the
harness lands a suspended test through it) are C/asm and fill import slots 15-17; Scr_ThreadEntry is our asm copy of
the original frame with our own handler stub, FuncInfo and catch(...) funclet (the library is position independent:
label addresses via call/pop; `.pushsection` does not exist on MinGW's gas, so the data goes in `.data` then back to
`.text`). The Scr_Wait family (11 rows) and the 28 Scr_StartThread*/Scr_CallThread*/ThreadFunctor1 wrappers (their C
existed; the harness cannot hook them: a call inside the first 5 bytes, or the thread switch) are Replaced rows now,
not compared - replays check them. **Also done (part of 4):** LocStrings_Ctor, the static-object constructors
FUN_00023ad0/26fc0/270e0/27cf0/001e4700 (StaticObj_*_Ctor, ScrUndefinedInts21_Ctor), ScrStringHolder_Dtor
(0x00014c80), ScrCall_GScr_GetEntScriptObject(_2), NAL_SkipActiveChannelBlocks_VFunc (0x00022ba0),
BroScr_SetPoseMovement/SetDirectionalMoveAnims, Scr_NotifyAfterDeath, SmallHeap_AllPoolsExhausted (0x000445d0) and
the gap function 0x002fd6e0 (SmallHeap_DrawExhaustedWarning, "Broc Pool empty"; Ghidra's Vehicle_CountActive wrongly
spans 0x002fd680 too - a separate function there, no Ghidra entry; both still to fix in Ghidra). Names file-only.

Left, in order:
2. CRT leftovers. **Done 11-11: x87 intrinsics __CIsqrt/sin/cos/tan/acos/fmod** (title_crt.cpp asm, Compare rows,
   `--check-crt`: 440 arguments x 5 control words + fmod pairs, results / control word / errno bit-exact in Linux and
   Wine; m4 trap 100 -> 81). What they do, from the title's LIBCMT: sqrt/acos run under (cw & 0x300) | 0x7f (the
   caller's precision kept), sin/cos/tan under 0x027f with fprem1-by-2-pi reduction when C2 is set; the exit
   (__math_exit) stores and reloads the result as a double when the caller's word is not 0x027f, masks PM and the PE
   flag is set (the CRT's "error" path for inexact); domain errors (negative sqrt, infinite angle, |x| > 1 acos, any
   NaN) give the indefinite / quiet NaN through a double and errno EDOM. fmod (__cintrindisp2 + its table at
   0x008ff42c): word 0x133f, operand classes by fxam (normal; zero AND denormal; NaN/other; infinite): normal/normal
   fprem, zero/normal +0 (so a denormal dividend gives +0), NaN: faddp with the quiet bits picking the code, all other
   pairs the indefinite + EDOM; a plain result is NOT rounded to double (that exit tests PM the other way round).
   **Done 11-11: __CIpow** (0x001f1a6c, Compare row; cl.cpp and fx.cpp call ours): the x87 part (core 0x001f1a90,
   __fload_withFB, __twoToTOS, __check_range_exit, __startTwoArgErrorHandling) is the title's instruction sequence
   in title_crt.cpp asm; its C helpers (__powhlp, _87except, __handle_exc's masked responses incl. the
   overflow-by-rounding-mode and bit-cut denormal underflow, _decomp, __d_inttype) are C++ on the doubles' bits, so
   only the title's own x87 instructions touch the status word; __set_statfp is copied as asm. `--check-crt`: 41
   edge values squared + 400 random pairs x 6 control words, result / control word / errno / status flags + stack
   top bit-exact (Linux release+debug, Wine); a mutation (no odd-power fchs) shows up. Not ported: _raise_exc (an
   inexact result with PM unmasked; the title masks all). Note the title leaves y on the x87 stack for x = +-0,
   y != 0 (ours too).
   **Done 11-11: our own heap** (`title_heap.cpp`, instead of porting the title's Rtl heap): RtlAllocateHeap /
   RtlFreeHeap / RtlReAllocateHeap / RtlSizeHeap / RtlCreateHeap / RtlDestroyHeap, the XAPI wrappers (HeapAlloc's
   GetProcessHeap, HeapFree, HeapCreate/Destroy, LocalAlloc/Free, XMemAlloc/Free, which keeps its physical-memory
   path through XPhysicalAlloc(Ex)) and LIBCMT's malloc / __nh_malloc / __heap_alloc / free / calloc / realloc /
   _msize / _aligned_* are Replaced rows on one boundary-tag allocator (16-byte headers and alignment, exact bins
   below 2 KB, log bins above, first fit, coalescing) over 1 MB segments from NtAllocateVirtualMemory (where the
   Rtl heap's memory came from: with segments from the contiguous allocator instead, every engine pointer moved by
   1 MB), so addresses are the same in every run and build, and plain replays still match the recordings' hashes
   frame for frame (the CRT blocks' own addresses are not in the hashed state). Heap handles are ignored; the small-block
   heap (__active_heap 3) is not ported; _callnewh is still called by address on exhaustion. `--check-crt` runs
   30,000 random alloc/free/realloc on it. The game holds only ~6 CRT blocks at the first frame.
   **Done 11-11: the CRT our C called by address** (Compare rows, call sites rewritten by own_calls): _toupper,
   _strncat, _strrchr, _memchr, _memmove, _strcpy, _wcslen, _strtoul (strtoxl unsigned: overflow ULONG_MAX, a '-'
   negates unchecked), _strtod (atof's converter; overflow +-HUGE_VAL, and a nonzero number below the smallest
   normal - denormals too - gives +0, even "-1e-400", both ERANGE), __ceil_default (x87 asm: frndint under the
   word at 0x008d11f0; NaN EDOM) and __CIasin (acos's frame; +-1 gives the 80-bit pi/2 at 0x008d19b6).
   **The stdio write side** in title_stdio.cpp: fwrite/_fwrite_lk/_flsbuf/_write/_write_lk (text mode CR LF through
   a 1 KB staging buffer that can reach 0x401 bytes)/_lseeki64_lk/_isatty/__doserrno/_dosmaperr (the CRT's 45-entry
   table at 0x008d1208) as Compare rows; **the lock table** (_mtinitlocks, _mtinitlocknum, _lock, _unlock,
   __crtInitCritSecAndSpinCount/NoSpinCount) as Replaced rows (comparing would enter each section twice).
   `--check-crt` adds tables for all of them and an fwrite run (the title's into T:\crtcheck_a.bin, ours into _b,
   binary and text, 13 write shapes; return values, stream fields and bytes; files deleted after). 0 differences
   Linux release/debug and Wine. Replays: m4 all frames in release/debug/Windows, m2 release; own-main dumps only
   heap pointers (m4 6 words, m2 300). Traps m4 61 -> 52, m2 67 -> 58. Replays write no save files (checked with
   the new `KEEP_DATA=1` of replay_check.sh), so fwrite is checked by --check-crt only.
   Left from item 2: printf/fprintf (__output, _stbuf/_ftbuf; only Nfl_Print*, CL_Shutdown, Mem_DebugPrintf use
   them), _flsall, _chsize_lk, _commit, _amsg_exit, _callnewh; title_crt.cpp's XAPI calls by address
   (GetCurrentThreadId, GetSystemTimeAsFileTime) -> title_xapi C (title_crt.cpp is in own_calls KEEP_FILES).
3. XAPI leftovers our C calls but m4 never reaches. **Done 11-11 (batch A, title_xapi.cpp):** GetCurrentThreadId,
   GetSystemTimeAsFileTime, FileTimeToSystemTime/SystemTimeToFileTime, XQueryValue, the time-zone chain
   (XapipQueryTimeZoneInformation, RtlpCutoverTimeToSystemTime, GetTimeZoneInformation, XapipGetTimeZoneBias,
   FileTimeToLocalFileTime), XGetDiskSectorSizeA/ClusterSizeA, GetDiskFreeSpaceExA, UTF8ToUnicode, MultiByteToWideChar
   as Compare rows; DeleteFileA, OutputDebugStringA (the original's int 0x2d faults under the loader: ours drops the
   text, as a retail kernel does), CancelIo (the original walks IdexChannelObject's queue, a zero block in our kernel
   that would fault; ours only checks the handle - our kernel completes I/O inside the call) as Replaced rows.
   `--check-crt` now also runs `check_title_xapi` (title_xapi_check.cpp): 400 file/system times both ways, 36
   setting queries, 3000 cutover dates (relative + absolute, this year or next), 8 disk paths, 4000 UTF-8/ANSI
   strings - 0 differences in Linux release/debug and Wine; a mutation in the cutover weekday step and one in the
   low surrogate both show up. Found on the way: on Windows FS:[0x20/0x24/0x28/0x58] are TEB TLS slots (the loader
   patches the title's reads), so our C's `platform_fs_word` read the TEB there - now `game::title_fs_word` maps the
   offset through `loader::title_fs_offset` (XapiThreadStartup and the CRT's per-thread data had the same bug,
   unseen because those Compare rows are not installed in Windows runs). Traps m4 52 -> 50, m2 58 -> 56; plain
   replays match every frame, own-main dumps only heap pointers. `tools/fnpack.py` names kernel import slots
   (`krnl:Name`).
   **Done 11-11 (batch B, title_savegame.cpp):** the save games - 29 Compare rows (signatures: XShaHmac*,
   XapipCalculate*Signature*, XCalculateSignature*, XapiConvertSignatureToNonRoamable, FUN_001e8a21; hash encode,
   HexDigitToChar, meta-file parsing, XapiFillInSaveGameData, XFindFirst/NextSaveGame, XFindClose, display blocks,
   UnicodeToUTF8, WideCharToMultiByte, lstrcpynA; CRT _wcsncpy/_wcsstr/_wcscmp/_snwprintf in title_crt.cpp) and 8
   Replaced rows that write files (XCreateSaveGame, XDeleteSaveGame, Create/RemoveDirectoryA, XapiNukeDirectory(+
   FromHandle), XapiTouchDirectoryTimestamp, XapiVerifyGameName - opens SaveMeta.xbx OPEN_ALWAYS), each first
   checked as Compare against the title's, then flipped; `check_title_savegame` (title_savegame_check.cpp) checks
   them against the save format afterwards. 0 differences Linux release/debug and Wine; 13 mutations caught. Traps
   m4 49, m2 55. Notes: our loader has no XboxSignatureKey/XboxAlternateSignatureKeys export (zero blocks), so
   signatures use an all-zero key - same as the title's code under the loader; FindValueInMetaFile's and
   XapiNukeDirectory's buffers are uninitialized stack in the originals (ours start zeroed); quirks kept
   (WideCharToMultiByte converts length + 1, XCreateSaveGame's write loop restarts at the buffer start,
   ValidateAndSkipUnicodeSignature accepts any 2 bytes); lstrcpynA's SEH wrapper not ported.
4. Leftover engine rows. **Done 11-11 (item 4 batch):** SoundManager_Ctor (Replaced; the default I3DL2 listener it
   built went to SndSys_EmptyStub, a bare RET, so ours skips that call), BrushModel_CopyBounds, ScrIface_HashKeyNoCase
   (Replaced, the level import slot 0x8a4 now holds ours), Vec3A_Perpendicular (was FUN_000b8990),
   Stream_FindMap98Loaded (was FUN_0028e610). Its caller showed a family Ghidra has no functions for: slot 1
   (ClassName, "cdTextureDirectory".., 0x0028be30..90) and slot 2 (Find, 0x00025b10..0x00025ed0) of the seven
   Zone*Manager vtables - all 14 in C (stream.cpp), names file-only (manual_names.csv says "NO Ghidra function yet";
   create them in Ghidra). Traps m4 49 -> 44, m2 55 -> 49; m4 state at 2600 equal to the
   previous batch. FUN_00396f5f is a .CRT$XI entry (item 5).
   Left: too short for a jump (keep the original until the host's data pointers are rewritten): NullCallback,
   EmptyVFunc_Ret8, SndSys_EmptyStub, BroScr_Nop, BroScr_EmptyFunc_3/_36, Actor_Noncombat_Suspend,
   Actor_DefaultPain/Finish/Slot5; the 6 MANUAL static initializers; the UIX feature ctors from
   StaticInit_FxParticleBounds (drop with Live); LoadScreen_SetProgress (0x0033ac80, filed under mslib).
5. **Done 11-11 (title_start.cpp):** `--own-main` jumps the XBE entry to `title_start` (sets the TLS size/index,
   starts `title_main_thread` through our CreateThread); the thread makes the heap handle, the CRT state below as
   plain C (_mtinit with our CRT notify routine, _ioinit, _fpmath, onexit, inittime, _initstdio, _setmbcp(-3) =
   SBCS), then run_static_initializers and Sys_Main. Not run any more: entry, mainXapiStartup, XapiInitProcess,
   _rtinit, _cinit, kernel patches, auto power-down, updates, parental check, partitions, Live init 0x00396f5f,
   the unhandled-exception filter. **Interim until 5c batch 2 (gone 11-11):** map_drive_letters() still makes D: (kernel link), T:/U:
   (the title's XapiSetupPerTitleDriveLetters) and Z: (loader) - without them the game hangs after frame 3 (no
   data). Traps m4 44 -> 16, m2 49 -> 21; dumps equal to the previous batch. Original plan text:
   **Own start** (replaces the XAPI start-up, nothing of it ported): our start function maps nothing new yet (the
   loader still maps the dump while replays need it) but runs our own sequence: platform init -> the CRT state the
   game reads, as plain C -> title_cinit -> Sys_Main. The CRT state (from the 11-11 reading of the originals):
   per-thread data (calloc 0x88; tid, handle -1, NO mbcinfo/seed - the original's _initptd stores were folded onto
   the null path), the lowio table (malloc 0x500 -> 0x0098f8e0, 0x0098f8c4 = 0x20, 32 records of 0x28: +0 -1, +4 0
   then 0xc1 for the first three, +5 0x0a, +8 0), stdio table (0x0098f9f4 = 0x200, calloc -> 0x0098f9f0, first 20 =
   _iob 0x008d13a0 + i*0x20, _iob 0..2 +0x10 = -1 when their lowio handle is -1/0), onexit table (malloc 0x80 ->
   0x0098f9e8 = 0x0098f9e4, word 0), start time 0x00926a58 (FILETIME), float conversion pointers 0x008d1988..9c +
   _controlfp(0x10000, 0x30000) + fnclex, code page (_setSBCS: 0x0098f680 0x101 bytes and 0x0098f7a0 6 dwords zeroed,
   0x0098f784/678/670 = 0; mbcinfo 0x220 block -> 0x0098f674; 0x0098f9ec = 1), heap handle 0x0098f660. Dropped:
   XONLINE pointers (0x00396f5f, Live), unhandled-exception filter, drive letters, partitions, updates, parental
   check, power-down, kernel patches, LaunchDataPage handling (5b passes the level choice natively). For now
   `--own-main` jumps the XBE entry (0x001e7bc3) to it; a replay check per step as before.
5c. **Native platform layer under our C**: files (CreateFileA/ReadFile/WriteFile/Find*/... in title_xapi.cpp on
   `src/platform` files; title paths D:\ -> data folder, T:\/U:\ -> user folder, Z:\ -> cache, mapped in one
   place), save games (title_savegame.cpp on the same, same folder layout and signatures so existing saves load),
   time (host clock), events/mutexes/threads (platform), memory (heap segments from the host). Then nothing in our
   C calls the kernel thunk table. Check: replays + a save round trip both ways with the original.
   **How (11-11):** `title_host.h` - a `TitleHost` of function pointers the loader fills today and our own exe
   later (start a thread with the title's FS/TLS block, the record/replay hooks); `native_platform()` is on under
   `--own-main` only, so the kernel path stays for the compare harness and plain runs (the host exe drops it).
   Handles of our own start at 0x7e000000 (the kernel counts up from 4), so every XAPI function routes by handle
   while both exist; `xbox::set_foreign_objects` lets the loader's kernel I/O and SDK redirects (sound packets,
   input) signal our events and queue completion routines on our threads.
   **Batch 1 (title_objects.cpp):** events, mutexes, threads (no kernel handle: `loader::start_title_thread`),
   waits with the kernel's 'W' replay rule, sleeps and each thread's completion-routine queue; CreateEventA,
   CreateMutexA, CreateThread, SetEvent, ReleaseMutex, WaitForSingleObject(Ex), SleepEx, CloseHandle native.
   **Batch 2 done 11-11 (title_files.cpp):** title paths map to host folders in one place (D: the game data,
   read-only; T:/U: <user>/E/TDATA|UDATA/41560051; Z: <user>/Z; no E: - the title never had one; "\??\" prefixes
   accepted; components matched ignoring case). Open files and searches are payloads in title_objects' handle
   table. Native branches (native_platform() or one of our handles) in CreateFileA, ReadFile(Ex), WriteFile(Ex)
   (overlapped: done inside the call, event set, still reported pending), GetFileSize(Ex), SetFilePointer(Ex),
   SetEndOfFile, Find*, XGetFilePhysicalSortKey (same path hash as the kernel's), DeleteFileA, CancelIo, disk
   sizes, and in title_savegame.cpp CreateDirectoryA, RemoveDirectoryA, XCreateSaveGame's meta file,
   XDeleteSaveGame (remove_tree), XGetDisplayBlocks (by path), XapiTouchDirectoryTimestamp (now sets the time; the
   kernel ignored it). XapiSetLastNTError maps our own statuses without the kernel. ForeignObjects.host_file serves
   XFileCreateMediaObjectAsync. map_drive_letters() is gone (XapiSetupPerTitleDriveLetters, TitleMeta copies no
   longer run). Checks: m4/m2 dumps byte-identical to batch 1, traps m4 16 -> 12, m2 21 -> 17; `--check-crt` with
   XB_CHECK_NATIVE_FILES=1 runs our side of the directory, disk-size and save-game checks on the native files and
   the title's on the kernel (same folders, so each reads the other's saves and the real save folder): 0
   differences on Linux and Wine; Wine --own-main m4 replay runs through.
   **Batch 3 done 11-11:** time - `TitleHost::system_time` (host UTC clock) and our calendar
   (`src/platform/civil_time.h`, now also behind the kernel's RtlTime* exports) for GetSystemTimeAsFileTime,
   File/SystemTime conversions, the time zone and cutover dates; console settings - our own table
   (title_xapi.cpp `native_setting`, the 256-byte layout the title parses for blocks 0xff/0xffff) from
   `TitleHost::language`/`widescreen`, North America/NTSC-M/UTC as before, XGetAVPack 0; critical sections - pure C
   on the title's RTL_CRITICAL_SECTION (title_objects.cpp) with the kernel's protocol and the same futex word, so
   the title's x86 and our C share sections (CRT lock table, stdio file locks, the thread-notify lock); memory -
   XPhysicalAlloc/Free/Protect, XQueryMemoryProtect, GlobalMemoryStatus and our heap's segments through
   `TitleHost` (allocate_contiguous, protect, memory_statistics, reserve - the loader's allocator, so addresses
   stay); save signatures - SHA-1 from `src/platform/sha1.h` (kernel's XcSHA* use it too), the console key
   "CoD2BRO-port-key" and zero signature keys as constants (`title_console_key`), so existing saves keep loading;
   status -> DOS error our own full table (`title_dos_error`); MultiByteToWideChar's ANSI widening in C. Checks:
   m4/m2 state dumps byte-identical to batch 2, traps m4 12 / m2 17 (unchanged); `--check-crt` 0 differences on
   Linux and Wine with and without XB_CHECK_NATIVE_FILES=1 (now covers the whole native layer: times, settings,
   time zone, string conversion, disk sizes, saves); Wine `--own-main` m4 replay runs through
   (build/trap/wine_own.sh). Kernel calls left in our C under --own-main (XGetLaunchInfo native since 5b):
   RaiseException/RtlUnwind in title_eh.cpp (SEH), the kernel paths kept for plain runs/compare.
5b. Level switches without a restart: the title moves between levels by rebooting itself
   (XLaunchNewImageA + launch data page); the loader re-execs, so the window closes and reopens. Ours: keep the
   window, GfxDevice, audio device and input; map .data/.bss fresh from the dump, reset our heap and CRT state,
   stop the title's threads and close its handles, hand over the launch data, run the start-up again (static
   initializers, Sys_Main). Check: a replay across an in-process switch matches one across a real restart.
   **Done 11-11:** the switch is the console command `reboot`; `Cmd_RelaunchTitle_f` (0x002cfa40, was set aside)
   and `XLaunchNewImageA` (0x001e6d5c) are C. Natively (same title, "d:\default.xbe") XLaunchNewImageA builds the
   page XWriteTitleInfoNoReboot writes (type, title id, "\Device\Cdrom0;default.xbe", the data at +0x400) and calls
   `TitleHost::relaunch`; other launches keep the original path. Under --own-main the title has ONE thread of its
   own (the main thread), so the loader (`request_title_restart`) ends that thread and its parked start thread does
   the reset: `render::restart_title` (vertical blank stops calling the title, back end forgets everything made
   from title memory, device closed; the next CreateDevice reuses window and back end and moves the frame buffers
   to their new memory - `Backend::set_frame_buffers`), `audio::forget_title_voices`, `game::title_restart_reset`
   (handles, heap, script fibers, sound additions, ngl_gfx device objects + recorded state), the image's header and
   writable sections back to a snapshot taken before the entry ran (incl. the per-level sections' reference counts),
   `xbox::restore_memory` (allocator bookkeeping + committed pages as at that point), thread ids from 1 again,
   `xbox::place_launch_data_page`, the entry again. Window, devices, input, settings stay.
   **Check:** `--replay-at-switch` starts a recording/replay at the first in-process switch. A recording of a fresh
   boot (`--launch-data 3 3<page`, the page --diag saves at the switch) replays from `--level 4 --exec reboot`
   with every frame matching (2350 frames, Linux and Wine) and the frame-900 state dump byte-identical to the
   fresh boot's. The page differs from the original only past the 0xe9 words of data (the original's stack bytes,
   ours zeros).
   **leaf_test 11-11 (after 5b):** full ctest fails in leaf_test only - 23 mismatches (destructors, FreeAligned,
   CG_SizeUp/Down_f, CG_CopySaveState, Phys_DestroyWorld, ...) and 2 functions whose workers die (host malloc
   corruption); the same at 395ea703, so older than 5b, new since the 11-07 run. Likely the harness on random
   inputs with all-C call chains and our heap (both versions crash, in different places); to look into.
   Order: 5, 5c, 5b (in-process switches are simplest once threads and files are ours), then the host
   executable itself (our own exe, no loader).
**Host executable, step 1 (11-11): `--text-clear`.** What the host really needs from the original code: under
`--own-main` the loader turns every original byte of the non-level code sections (.text, XONLINE, XNET, D3DX,
DSOUND, D3D, XGRPH, XPP; 3.98 MB) into INT3, except the bytes our hooks wrote and the data kept from
`tools/data/code_section_data.txt`. Code that still runs traps, is logged (`address caller`) and gets its one
function back (up to the next 16-byte boundary after INT3 padding that follows a ret/jmp; a single 0xCC may be an
operand byte such as `[ebp-0x34]`), so one run lists everything. `tools/text_clear_report.py LOG...` names the spots.
- **Data inside code sections:** LTCG merged constants into them (XGRPH holds strings such as "a" and "v" that our
  C reads by address, bro_scr_ai_anim_threads.cpp / bro_scr_level_threads_2.cpp, and TUNISIA/LIBEATOR/GELA/PIANLUPO
  code reads that area too). Cleared, the voice alias name went wrong and m2 replays stalled 10 s per sound packet.
  `tools/text_clear_keep.py` builds the keep list from `tools/ghidra/ExportCodeSectionData.java` (instruction runs,
  defined data, data references): Ghidra's defined data plus every non-instruction address an instruction reads, up
  to the next instruction (341 ranges, 270 KB; 141 KB of it inside the cleared sections). Addresses stored in data
  sections (vtables, .CRT$XC tables) count as code. The host maps these ranges like .rdata.
- Debug switches: `XB_CLEAR_SKIP=SECTION,...` leaves sections alone (bisecting), `XB_CLEAR_NOACCESS=SECTION` also
  takes the section's pages away after the restart snapshot and logs the first access to each as `R address eip`.
- **Result:** m4_smoke and m2_playtest replays reach their end, state dumps byte-identical to the previous batch
  (dump_lsw2, m2dump_lsw2_own). **315 spots** of original code still run (m4 264, m2 311; build/trap/clear_spots.txt),
  where the Ghidra-entry trap saw 12/17: nearly all are **gap functions** (no Ghidra function; 54 not even
  disassembled) that our C reaches through vtables, callbacks or title addresses - UI text/picture/menu methods,
  HUD component setup, the cd* material Draw/AddToList methods, NAL/ngl helpers, EH unwind funclets
  (0x00343xxx/0x0034exxx, entered by the loader's SEH dispatch), plus the known short stubs. Sizes: 120 up to 16
  bytes, 81 up to 48, 78 up to 256, 36 larger.
- **Ghidra functions created (11-11):** `tools/ghidra/CreateFunctionsAt.java` over
  `tools/data/host_gap_functions.txt` (the spots minus the 18 EH unwind funclets 0x0033df40-0x0034e008, plus the 14
  zone manager methods and 0x002fd680/0x002fd6e0): 297 created (6 cut back where a new body ran into a neighbour
  through shared tails), 16 existed; manual_names.csv applied (18 names), symbols + naming export refreshed. Backup
  of the project before: cod_bro/ghidra_backups/20261111-before-gap-functions. The funclets still run (entered by
  our SEH dispatch, loader 0x080a3c7d, and game C 0xf7b81711): find which frames they unwind before the host.
- `tools/gap_list.py LIST MAX [MIN]` prints size + disassembly per address. Batch 1 = the 121 of 16 bytes or less:
  getters/setters on `ecx`, bare `ret`/`ret n`, `this+4` adjustor thunks (`add ecx,4; jmp X`), vtable forwarders
  (`mov ecx,[ecx+0x10]; mov eax,[ecx]; jmp [eax+N]`), constant returns (FOURCC tags 0x000a48xx, zone class names
  0x0028be30..90), destructor-style vtable stores (cd* material classes, 0x00047d70..). A function shorter than a
  jump still takes one: the INT3 padding to the next function is free (compare's all_c "returns within 5 bytes"
  rule is too strict there - check the next function's start instead).
- **m0/m1 recordings (11-11):** `--text-clear` replays of both (build/trap/clear_m0.txt, clear_m1.txt; 340 spots) add
  35 spots m4/m2 never reached: `tools/data/host_gap_functions_m01.txt` (15 of them up to 16 bytes). Created in
  Ghidra 11-11: 29 new (FUN_00025850 cut back at 0x0008e270), 6 existed; symbols refreshed. Backup before:
  cod_bro/ghidra_backups/20261111-before-m01-gap-functions. Left out: EH unwind funclets 0x0033f509,
  0x0033fc8b and 0x0033b272/0x0033b4c0 (also entered by the loader's SEH dispatch). The entry trap (`--text-trap`)
  adds FUN_0001ce00 and FUN_001a4540 (both in that file); all four recordings reach 19 trapped functions.
- **Gap batches (11-12):** check = `tools/gap_check.sh LOADER OUT [BASELINE]` (m4/m2/m0/m1 replays with
  `--own-main --text-clear`, speed 8, no drawing; state dumps byte-identical to the baseline's; spots = original
  code still run; serialized on cod_bro/wt/compare.lock; ~6 min alone). Baseline before the batches: build/gap/base
  (354 spots incl. level sections). `tools/gap_context.py RTTI_TSV ADDR...` = vtable/slot/class + callers per
  address (RTTI table from the skill's msvc_rtti_vtables.py, copy in cod_bro/wt/rtti_vtables.tsv).
  Batch 1 DONE 3836dd8b: the 124 of 16 bytes or less in C (src/engine/shared/gap_methods.cpp), and Compare rows
  that return within 5 bytes are now hooked when INT3 padding follows (5 stub rows had still run): 354 -> 225 spots.
  Then RaiseException (title_xapi.cpp, Replaced), the 2 XPP _cinit arrays and the 6 UIX constructors the static
  initializers call (gap_methods). **Spot counts understate:** the clear logs only the first hit per page, so a
  spot that goes away can reveal a neighbour in the same page (0x000d5e3c appeared after 0x000d440b got C) - look
  for raw `call_regs(0x...)` / `title_function(0x...)` left in our C as well.
  Agents 11-12 (brief cod_bro/wt/gap_brief.md) DONE and merged (164b0390; gap_a missed 0x003159a0, added after): gap_a 87 rows (17-48 bytes), gap_b 86
  (49-256), gap_c 26 (263-1759); every list address has C except the ones below.
  **Finished 11-12:** the 329 gap names + 14 C names Ghidra still showed as FUN_ applied in Ghidra
  (backup cod_bro/ghidra_backups/20261112-before-gap-names-fxop; 13 kept an older Ghidra name, 0x007abedd has no
  Ghidra function); UIText_SetText has its row, its host TLS byte (0b vs 01) in accepted.csv ;
  0x00379080 translated by gen_static_init.py (it now follows a short jmp over alignment filler, translates cmp, and
  runs the own_calls passes on its output); the FxOp slot-2 getters 0x0009e7c0/0x0009e900/0x0009ee90 (+0x0009e650)
  created in Ghidra (they were free, no splits) and given C. Vehicle_CountActive's boundary is right (ends 0x002fd67f).
  **m3/m5 recordings (11-12):** `gap_check.sh` replays them too (dumps at 69500 / 42400). Their 13 new spots
  (tools/data/host_gap_functions_m35.txt, 9 created in Ghidra + 4 existing) have C: HudCompMeter (the Liberator
  meter: Draw, Update, DrawTargetRow/Grid with ESI/ECX/EDI arguments), HudCompBomberMap_Setup, UIPicture_SetQuadScale
  (+Uniform), FxParticleComponent_Prewarm, cdPrelitVertexIter_Ctor, apsTypeTag_MFxV, SV_MapRestartCheck_f,
  ScrCall_GScr_ZoneSetField34AndWait (Replaced: thread switch), BroScr_MapSlot_h4fcdc42e. 14 more EH funclets.
  Left: 0x002227f0 (Live, being dropped); 0x0007b980 (no references, looks dead, unhooked); MemHook_AlignedAlloc
  (the original INT3s on a failed allocation, ours returns null).
  **Result 11-12:** six recordings (m0-m5) reach their end under `--own-main --text-clear`, dumps identical to the
  baselines; the only spots left are the 36 EH unwind funclets and 0x002227f0 (Live) - build/gap/m35b.
  The 5 UIText localize rows with the same host TLS byte (LocalizeWords, SetWrappedString, SetLocalizedWrappedText,
  SetLocalizedText, UITextBase_SetLocalizedText) accepted too.
  **RESOLVED 11-12: m3 own-main drift = an original-game read of uninitialised heap memory, not a C bug.** Frame
  47618, entity 449 (AI soldier in cover, actor 0x813c5d40, DObj 0x81b6eee0, owner 0x81b7f0b0): its yaw turns 1.5985
  deg in plain, 0.6152 in own-main. Chain: DObj_GetRootAnimDelta reads the clip state's root record (clip+0xd8),
  copied (NAL_PosOri_CopyState) from the pose AnimTree_CalcPose_r built. Per-node logging at level.time 581787
  (gdb, see below) shows the first differing output at tree node 281: a blend node whose only child (293) has
  weight 0 that frame, so it evaluates nothing and returns its dst buffer untouched. That dst is node 2's blend
  temp from NAL_AnimState_Init -> Mem_AlignedAlloc (the scratch arena range 0x009098f0..f4 is empty), never
  initialised by channel slot 12 for the root record. Plain gets a different freed heap block (0x82277cd0, left
  over rotation z=0xbcd282d3, ~3 deg); our own heap (title_heap.cpp, own bins) returns the block of the frame before
  (near identity). Inputs were identical: anim infos 0x00965d00 (times/weights), DObj, every track's last_time and
  cached root, x87 CW 0xc7f / MXCSR 0x7fbf. The earlier 0x00/0xff fill test only filled fresh blocks, not reused
  ones, so it could not see this. Effect is transient: at frame 69000 plain vs own-main non-pointer state is equal
  (only the known stale path-record high halves), both replays reach the recording's end (69529).
  Also seen: the PLAIN replay itself differs from the recording's hashes from frame 52398 (entities 322+) - same
  class likely (heap reuse depends on allocation order across threads at record time); not chased.
  Options if frame-exact matters later: give title_heap.cpp the Rtl heap's reuse order, or (enhancement, switchable)
  initialise the dst when a node has no active children. Per replay-success-bar: mission completes = pass.
  gdb method (fast): gate expensive breakpoints behind `xbox::replay::frame()` with
  `*(unsigned*)&'xbox::replay::(anonymous namespace)::g_frame' >= N`, C++ symbols need the mangled-signature form
  (break *'game::DObj_UpdateSkeletons(game::DObj*)'); a gated run is a normal replay's time (~3 min for m3 at 8x).
  Merged tree builds on Linux and MinGW (11-12). Then the host exe proper (map data sections + level sections + kept data, jumps at the remaining
  entry points, no .text). Left on purpose: EH unwind funclets (entered by our SEH dispatch).

**Host exe proper, step H1 (11-13): no kernel, level EH code kept.**
- What `--own-main` still takes from the kernel table: `XB_KERNEL_COUNT=FILE` (developer env) puts a counting stub
  in front of every function import (FILE: `name calls last_caller`, rewritten every 2 s). m4_smoke and m2_playtest:
  only RtlRaiseException/RtlUnwind (C++ throws from our title_eh.cpp). Now our C: `src/engine/shared/title_seh.cpp`
  (the loader's seh.cpp moved; the chain end and the fatal report come from `TitleHost::seh_chain_end` / `fatal`),
  and the loader's kernel exports point at it, so plain runs use the same code. Under `--own-main` nothing calls the
  kernel table any more.
- The 36 "EH funclet" spots are **level code the linker put in .text**: level functions push handler stubs
  (`mov eax, FuncInfo; jmp ___CxxFrameHandler`) that live in 0x0033b011..0x00372dc0 (no Ghidra functions there),
  and their FuncInfo unwind actions / catch blocks are funclets next to them (`lea ecx,[ebp-N]; jmp Dtor`,
  conditional "destroy if constructed" forms). `tools/level_eh_code.py default.xbe tools/data/level_eh_code.txt`
  finds them statically (any 32-bit value in a level section pointing at a stub, then FuncInfo -> unwind map + try
  map handlers, every path decoded to its jmp/ret, plus the `.text` thunks `jmp LEVEL_FUNCTION` that funclets jump
  to): 3,679 stubs, 18,077 funclets, 332 ranges, 190 KB. `--text-clear` keeps them like code_section_data.txt (the
  host maps them like a level section). The 7 engine functions they jump to all have C
  (tools/data/level_eh_code_targets.txt; new: StdString_Tidy 0x00014750).
- 0x002227f0 = LiveMgr_PadInput (LiveMgr vtable slot 6, called by IN_PadUpdate): C, returns 0 while the Live UI is
  down (+8 = 0, always in the own host).
- Result: `gap_check.sh` m0-m5 reach their end, **0 spots** of original code run, state dumps equal to the
  previous batch (build/gap/h1). The actors dumps differ in a few words whose high halves are host stack addresses
  (0xf4xx..../0xf7xx....: the known stale path-record high halves; they moved with the old trap signal frames).

**Step H2 (11-13): `--host`.** `--own-main` plus: right after the XBE is mapped, every byte of the non-level code
sections is INT3 except the two keep lists (`src/runtime/loader/host_image.cpp`), before any hook is written; the
kernel thunk table holds only fatal stubs (functions) and zero blocks (data); original code that runs = INT3 = the
crash handler's backtrace. So no setup step may read original engine code. Found: CL_Frame read its SCR_UpdateScreen
call target from the original call's bytes (that is how the loader's --level/--exec/--force thunk reached it);
now `ClientPcHooks::screen_update_call` (the loader sets it when CL_Frame is C, else patches the call as before).
The hook placement needs no code bytes either: under all-C no row is kept original by `ends_within_jump`. The
loader's patches into original engine code log "not ..." warnings under --host (they patch code that no longer
runs). Check: `HOST=1 tools/gap_check.sh LOADER OUT BASELINE`.
Next (H3): our own executable - the --host path without the XBE kernel/loader parts: map data + level sections +
keep lists from the dump, the hook table, TitleHost, render/audio/input/replay setup shared with the loader
(split main.cpp's option parsing and device setup into a library both use).

**Step H3 (11-12): our own executable `cod_bro`** (`src/runtime/loader/host_main.cpp`). Linux: the loader's code is a
static library (`cod_bro_runtime`) linked into `cod_bro_loader` (main.cpp) and `cod_bro` (host_main.cpp); Windows: the
core DLL exports `cod_bro_loader_main` and `cod_bro_host_main`, and `cod_bro.exe` is the same stub as
`cod_bro_loader.exe` with the other entry (`COD_BRO_LOADER_CORE_ENTRY`). Shared by both mains: `options.h/.cpp`
(Options, parse_args), `title_session.h/.cpp` (settings.ini + command line, log/--diag/fatal hooks, assets, run_title
with the in-process restart loop), `title_setup.h/.cpp` (sound/graphics/input/TitleHost set-up). `cod_bro` = --host
without the kernel set-up (only `xbox::memory_init` + the launch data page), without release maps (another release:
"start cod_bro_loader"), compare harness (hooks only), --text-trap/--text-clear and exec reboots; campaign only.
Found: the D3D vertical-blank/swap callbacks and the sound engine's packet callbacks called title code through
`xbox::kernel_config().call_guest`, which only the kernel set-up filled - now `xbox::TitleCalls`
(`src/runtime/xbox/title_calls.h`), set by both mains. Check (`HOST=1 tools/gap_check.sh .../bin/cod_bro OUT
build/gap/host`): m0-m5 reach their end, 0 spots, dumps identical except m0 actors (3 host-stack high halves, the known
stale path-record words); `cod_bro.exe` under Wine: m4 dump identical to the Linux baseline.
m6/m7 (first time under our C, loader --host too): stop at load on gap functions - `--own-main --text-clear` lists 15
spots (build/h3/clear_m6, clear_m7: 0x000508a0, 0x00335b80, 0x0004f240/60/80/90, 0x0001d0e0, 0x00011ef0/f00/f50/f60,
0x0030f000/f010, 0x0030ec40, 0x00314aa0). Done 11-12: all 15 + the unreached sibling 0x0004f250 in C (gap_methods.cpp:
UIMenu slots 20-23 forwarders to 44-47 and the two empty slots, UIMenu_ClearHeldDirection, UIScreenMgr_ActiveScreenSlot33,
ZeroWord_Ctor, cdOcean_StateSlot2/4/StateEntry/ConstSlot5/6, cdOceanVertexIter_Ctor; fe_menus.cpp LoadSicilyMenu_OnContinue);
m6 + m7 --text-clear: 0 spots, both reach their end.

**Unreached functions without C (11-12): `tools/host_unreached.py [--out FILE]`.** Replays only find what their missions
reach; this lists statically every function start in .text (Ghidra function, or 16-aligned after INT3/ret padding = gap)
with no row in functions.cpp, referenced by an aligned word in a non-code section or an immediate/branch in level code.
First run: 1,234 (733 from .rdata words, 432 .data, 34 level words, 29 .XTLID - noise, 5 level call/jmp). Many are Live UI /
voice classes (UIX::, XVOICE::, LiveMgr: unreachable with Live off). Real examples: KASSRINE calls Scr_BeginThreadEndOnFunctor1
0x0011a960 (C exists, no row), PROLOGUE jumps to a second BroScr_ReturnFalse 0x00124bc0, MONS1/BUCHHOLZ call FUN_0011d040 /
FUN_0011d2d0; menu vtables 0x00828e04.. point at never-reached UI gap functions (0x000112c0 ...).
**Classified 2026-11-12: `tools/host_unreached_classify.py`** (writes build/unreached/classes.txt; class rules in its
docstring). Of the 1,234: **758 need nothing** - static_init 361 (only in .CRT$XC: static_init_generated.cpp), live 112
(Live/UIX/XVOICE/XONLINE and vtables only Live code installs: Live is not part of the own engine; multiplayer
= system link only, later), mslib 96 (XFONT TrueType rasteriser - no C row and m0-m7 never trap it, so unused - and
XAPI/LIBCMT tables our platform layer and CRT replace), eh_dead 76 (unwind/catch funclets of functions that run as our C,
or of FuncInfos no handler stub loads), eh_sdk 45 (the same for XGRPH), noise 38 (.XTLID/DOLBY/XON_RD words), set_aside
13, command 17 (cheat/debug console commands: nglstats, wireframe, memreport, ... - debug/dev only, as 10-27; open:
cameraInterrupt). **476 are work**: virtual 345 (slots of vtables our C installs - mostly UI menu/text/dialog classes,
NGL managers, Fx domains/ops, CM shapes), codec_slot 60 (NAL anim codec class methods, run only if an anim uses that
codec; missions m8-m13 have no recordings), level 21 (words/calls in level sections: FUN_0011d040 MONS1,
FUN_0011d2d0 BUCHHOLZ, 0x0011da70/90 in every level ...), thunk 19 + thunk_c 10 (jmp thunks; thunk_c only need alias
rows), table 9 (EH copy constructors/catch blocks at 0x00895axx, ...), sdk_class 6 + vt_noc 4 + data_table 1 + eh 1.
Live check (11-12): the only Live code our C still calls is LiveMgr slot 7 (0x00223ae0, voice port pause around
Live_Frame) in Com_Frame's controller-reconnect resume - skipped under native_platform() now. The other Live reads in
our C are fields, or branches for Live state 2 (signed in), which never happens.
**Done 2026-11-12 night (3 agents, branches unr_a/b/c merged):** 480 rows - UI slots 172 (ui/fe_* files, ui_menu.h
tail-jump macros UI_MENU_TAIL_*), engine slots 184 (new shared/vslot_methods.cpp: cd*/ngl materials, zone/ngl/anim
managers, Fx/aps, allocators, CM shapes; the 6 "sdk_class" rows were AdvancedOptionsMenu slots - level data held the
vtable address), level/thunks/codecs 124 (shared/unr_methods.cpp, renderer/ngl_file_load.cpp = ngl managers' Load/
Release slots, nal_codec.cpp = all NAL codec slots), + std_exception_Dtor and two Sys_DebugBreak thunks. None is
reached by a replay or the compare harness: checked by reading against the disassembly only. The 20 checked list
entries that need no C are in tools/data/unreached_reviewed.csv (class `reviewed`). Rerun: 778 unreached, all in
need-nothing classes. Ghidra function 0x0030fe50 (Movie_DrawFrameQuiet, MainMenu_Draw tail-jumps to
it) created, names of tools/data/gap_names_unr_{a,b,c}.csv applied (backup cod_bro/ghidra_backups/
20261112-before-unr-names); 0x003367a0 renamed FrontEndMenu_Update (FrontEndMenu slot 4), 0x00339240 = MainMenu_Update.

**Kernel out of cod_bro's link (2026-11-12 night).** src/runtime/xbox is two libraries now: `xbox_host` (kernel_memory =
the title's memory + LaunchDataPage word, replay, title_calls, kernel_shared = clock start/RDTSC/--trace flag,
kernel_exports = the ordinal name table for the fatal stubs) and `xbox` (the kernel: dispatcher, objects, sync,
filesystem, settings, crypto, kernel_basic; links xbox_host). Everything but cod_bro_loader and the unit tests links
`xbox_host` only, so a kernel reference from shared code is a link error for cod_bro. What moved: title threads +
in-process restart -> loader/title_threads.cpp (no ThreadObject; kernel threads take ids from it), fatal stubs ->
loader/kernel_stubs.cpp (cod_bro: install_fatal_kernel_thunks), image page counts -> host_image.cpp. Hooks the loader's
main.cpp sets (cod_bro sets the native ones in title_setup.cpp under own_main, or leaves them unset): foreign objects,
xbox::set_event_signal (sound packet + input completion events), xbox::set_irql_calls (vertical-blank callback at
DISPATCH_LEVEL), audio::set_title_file_lookup (XFileCreateMediaObjectAsync), loader::set_kernel_links (patches:
XMountUtilityDrive replacement, compare's recorded kernel calls). Unit tests set the kernel versions (test_support.h,
audio_test). Linux cod_bro 3.39 -> 3.17 MB.
**Done 2026-11-13:** the memory exports' stdcall entries and their table moved to kernel_memory_exports.cpp (`xbox`
library); kernel_memory.cpp keeps the implementations as `xbox::memory_impl::*` (kernel_memory_impl.h) for the title_*
functions. Windows: the runtime is a static library (cod_bro_runtime) as on Linux, linked into two DLLs -
cod_bro_loader_core.dll (main.cpp + the kernel, entry cod_bro_loader_main) and cod_bro_core.dll (host_main.cpp,
xbox_host only, entry cod_bro_host_main); the stub takes the DLL name from COD_BRO_LOADER_CORE_DLL
(entry_loader_win32.cpp / entry_host_win32.cpp, loader::os::run_main). Releases still ship only the loader.

6. Recordings of other missions through the trap. Done for mission 2 (tank,
   recordings/m2/m2_playtest.rep, 7.2 min, 2026-11-10): two C bugs our Compare rows hid, fixed (the thread starts or
   the variable write happen after the harness compares): Bro_aim__keep_aiming passed BroScr_TurnToEnemy_2 its
   callback and end-on hash swapped (crash at 171 s), BroScr_SpawnerSetupAI set the tank-squish var on the actor
   instead of the spawner (player script object missing). Now: replay reaches its end, non-pointer state at frame
   25900 equal to the plain loader's; 153 trapped functions, 17 not in m4's list (Scr_CallThread_* x7,
   Scr_WaitTillArgsNoFlag, Scr_StartThread_ent_ent_f_i_i_i_f, Actor_DefaultPain/Finish/Slot5, __CItan + helper,
   BroScr_EmptyFunc_3/_36, FUN_000b8990). Then ctest/leaf_test after the merges
   (LEAF_JOBS=8); then the host executable: .text/SDK sections as INT3 + jumps, start at title_cinit +
   Sys_Main; check = replays reach their end with non-pointer state equal to the loader's.
