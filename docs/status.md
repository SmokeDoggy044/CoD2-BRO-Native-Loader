# Status and next steps

**Decompilation fork (2026-10-17):** this repository (CoD2-BRO-Decomp, local
`cod_bro/decomp/`) is the native loader's fork for the decompilation; the loader
repo (`upstream`) stays as it is. Merge loader fixes with `git pull upstream main`.
Done: the [comparison harness](comparison-harness.md) (`--compare`,
`tools/data/functions.csv`), the `game/code/` library (libcod_bro_game.so in Linux
loader builds) and the level-script string class (game/code/script_string.cpp,
0x0011d540-0x0011db70): 11 functions, 8 matched in every test over missions 1 and 4
(57 to 9,669 tests each); Find, FindLast and SetChar are not reached from `--level`
runs (only a front-end text dialog calls them). Next:
1. Name engine functions after their Quake 3 / RTCW counterparts (Ghidra): in
   progress, 1085 functions + 560 globals so far - workflow, findings and next steps in
   [naming.md](naming.md). Decision (user, 2026-10-17): keep naming before step 2,
   including CoD-specific code (AI, script, anim trees, vehicles) named from its
   bodies, so the decompilation and --compare work covers both CoD-specific and
   Q3-derived functions. Order (user, 2026-10-17): module by module - the game
   module is done except the AI; then the cgame (CG_DrawActiveFrame), then client/UI.
2. Decompile in batches by subsystem (leaf functions first), `--compare` runs per
   batch. Decision (user, 2026-10-18): matched functions stay `Compare` until their
   whole module is finished, then the module becomes `Replaced` at once; naming
   stays the main work until then. Decision (user, 2026-10-23): a module is finished
   only when no function is left unnamed and every function is sorted into a module (modules.csv / progress.py);
   until then an unnamed or unsorted function may still belong to it, so no module is Replaced before that.
3. Stand-ins for common pure kernel calls in test processes (critical sections), so
   more functions become testable.
4. The level sections (~5,000 compiler-generated script functions): a translator,
   not function-by-function decompilation.

**Own audio backend, 2026-10-29 (user): audio first, then the renderer, both staged on the loader's `beta` branch.**
Goal: better than the original, with one setting that keeps the original sound or turns on our improvements
(per-level reverb, surround/HRTF, occlusion, more voices, better resampling). Per-category volume and output
device are plain settings. Found: the retail game has no working reverb (details under step 3b). Step 1: the DirectSound boundary layer in C
(set aside on 10-28, now reversed). Done: SndHw_Update, SndVoice_SetMixBins, SndVoice_SetupBuffer,
SndStream_SetFormat/Update/ReadPacket (snd.cpp "Hardware voices"), 967 tests, all matched. The startup path
(SndSys_Init, SndSys_InitDirectSound, SndSys_CreateVoiceBuffers, SndSys_InitDevice) is on branch snd_boot.
Fixed the same day: SndHw_Update mismatched 5 of 5723 every-call tests on one host byte; it was the padding
byte after audio::Format's 8-bit codec, copied from a Format built on the stack (stale bytes differ between our
frame and the original's). Codec is 16 bits now, Format has no padding: 4474/4474 every-call tests clean. Found by
bisecting (C calling the original children: SndStream_SetFormat) and dumping the words around the host address.
Steered sound-menu runs (build/compare/soundmenu_user, soundmenu_user2; release, copy of the saves): SoundMenu_OnLeft/
OnRight/UpdateSliders/Construct/RestoreSelection matched; SoundMenu_Draw mismatched on the help-bar stack word
0x100006a8 like the other options Draws, fixed with DRAW_GAP_ENTRY (now in fe_options.h with set_help_bar_at, which
takes the row limit: 1 for the sound menu, 5 for gameplay/controller): 460/460. Third run (soundmenu_user3: menu, then a mission
start, which frees the front-end menus): SoundMenu_Dtor/DeletingDtor matched, SndHw_Update 4137 more every-call
tests, all matched. The sound module is complete: everything tested except S_WaitForWaveBanks (unhookable).
**First Replaced module (user, 2026-10-29): sound.** 126 functions run instead of the originals (Status::Replaced);
kept original: the three 1-byte stubs (SndArena_EmptyStub, SndSys_EmptyStub, Snd_Printf: a 5-byte jump would
overwrite the next function) and S_WaitForWaveBanks (C never tested). Replaced installs only on the supported build
(release map identity, compare_linux/win32.cpp): the C uses its addresses throughout, so another release keeps its
own code. Smoke runs (tools/smoke_test.sh, missions 4/1/6, 120 s): no warnings or errors, gameplay reached.
Steered listening run (release, the user's settings): crash in mission 0 when a squad mate starts talking,
G_EntityRunNotifyCallbacks reading ECX = faca5d86 (reproduced). Cause: our Scr_Notify and Scr_QueueNotify
(g_scr_runtime.cpp) called it as stdcall without its callback block in ECX ([ent+0x234]); the -O0 build happened
to leave it there (386 matched tests), -O2 did not. Now thiscall with the block. They first ran live because the
Replaced sound C calls them directly. Scan of all 426 title functions the C calls without register arguments:
no other real case (the rest were rdtsc writes and jump-table bytes the linear scan reads as code).
XBCMP_ORIGINALS=1 or =name,name keeps originals in place of Replaced functions (finding which one breaks).
platform_x87_log10_mul_ftol (compiler.h) now holds the volume conversion that was inline asm in movie.cpp.
Step 2a (2026-10-29): our own sound interface, audio/sound_device.h (`audio::SoundDevice`: voices, spatial
placement, listener, reverb send, Apply::Deferred + commit(), Start::WithSynch + start_synched()), one back end on
OpenAL Soft (audio/sound_engine.cpp, the voice/listener/distance logic that was in xbox_dsound.cpp). The sound C
(snd.cpp: SndHw_Update, SndStream_*, SndVoice_*, SndSys_InitDirectSound/CreateVoiceBuffers/InitDevice) calls it
directly; the loader hands it over (game::set_sound_device). xbox_dsound.cpp is now a translation onto it, kept for
the code that still calls DirectSound: the movie player (client, Compare), voice chat (XHV), other releases and
XBCMP_ORIGINALS. A voice id is the address of a DirectSound object, so either side can be given the other's voices
(the movie buffer: made by the C, played by the original Movie_StartAudio). Found: the second half of every mix bin
table adds DSMIXBIN_I3DL2 (10), so voice request 0x20 is the voice's reverb send (set_reverb_send, stored; nothing
plays it until reverb is restored). Kept as title calls: the file media objects (XFileCreateMediaObjectAsync and
its methods, XAPI), XPhysicalAlloc, XGetVideoStandard. Smoke runs missions 4 and 1: no warnings, gameplay reached.
Step 2b (2026-10-29): the SoundDevice methods are pass-through calls in test processes (kRecordedDeviceCalls,
audio::device_methods(); results `ret:al`/`ret:void`), so Compare functions that reach SndSys_Update are testable
again: mission 4, CL_Frame 79 matched (0 mismatched), CL_ClearState/CL_MapLoading/FE_FadeOut matched.
The movie C (Movie_StartAudio, Movie_Dtor, Movie_DecodeFrame's stop) stays on DirectSound: it is Compare (client
module), and onto the device its tests would log device calls where the original logs DirectSound calls. It
moves to the device when the client module becomes Replaced, as the sound module did.
Step 3a (2026-10-29): the settings. `--audio original|enhanced` (default enhanced; original with --pc-patches
off, so --compare too), `--speakers auto|stereo|headphones|quad|5.1|7.1` (ALC_OUTPUT_MODE_SOFT; auto keeps the
game's speaker-mode request: snd_speakerMode 0x008bd650, 2 = stereo at boot from SoundManager_Ctor, the profile
may change it in CL_ApplyProfileSettings; any other layout ignores it), `--audio-device NAME` (missing: warning,
the system's). Enhanced so far: the "23rd order Sinc" resampler on every source (OpenAL's default: cubic).
Launcher General tab: Sound (Original/Enhanced), Speakers, Output device (audio/output_devices.h, OpenAL only).
Step 3b (2026-10-29): reverb, enhanced only, from the place around the listener.
- Retail has none at all: GScr_SetReverb (0x002fc9d0, script slot 452, sends `reverb "%s" %g %g`) is called by no
  level code (no `call/mov [reg+0x710]` user); FUN_002ad9d0 holds five I3DL2 presets (0 Generic, 3 Hangar, 4 Room,
  1/2 unidentified; preset in EAX) and nothing calls it; the only live preset is Generic at boot (SoundManager_Ctor)
  and after shell shock (CG_StopShellShockSound), into the empty SndSys_EmptyStub 0x000c72f0; no voice asks for the
  reverb send (mix-bin request 0x20 never seen in a mission 1 run). No SetI3DL2Listener is linked.
- Ours: game/code/snd_env.cpp probes from SndHw_Update (every 6th update): 14 CM_BoxTrace world rays (8 level,
  5 up, 1 down; results: endpos +0, plane +0x10, fraction +0x20, surface flags +0x24, contents +0x28). Maps have
  no sky brushes: an upward ray hitting nothing = open sky. Surface type = flags bits 20-24, named by the game's
  surface parameter table 0x008d4e68 (16-byte rows {name, clears solid, surface flags, contents}): 1 bark, 2 brick,
  3 carpet, 4 cloth, 5 concrete, 6 dirt, 7 flesh, 8 foliage, 9 glass (and opaqueglass), 10 grass, 11 gravel, 12 ice,
  13 metal, 14 mud, 15 paper, 16 plaster, 17 rock, 18 sand, 19 snow, 20 water, 21 wood, 22 asphalt; foliage, glass
  and water are not solid (contents 0x2 / 0x10 / 0x20): the probe traces solid | glass. The same table has the
  Q3-style keywords (nodraw, playerclip, sky, ...) and the languages. Reverb from room acoustics:
  Sabine decay from a box fitted to the rays and absorption by material (open sky / missing walls absorb all;
  measured mid/high octave-band coefficients per material where published, estimates marked "est." in snd_env.cpp,
  + 0.07 for what the rays do not see; brightness = high-band Sabine incl. air over mid),
  reflections after the nearest wall's round trip, late delay from the mean free path, level from enclosure,
  brightness from hardness, reflections panned towards near hard walls (turned with the listener each update;
  the game's listener "top" is world down, its handedness fix). The game's presets blended by size were tried
  first and were not heard apart (playtests); the dynamic model was.
- audio::SoundDevice: Environment (I3DL2 fields + pans), set_environment, set_category, enhanced(); Output: EFX EAX
  reverb slot, per-source send. Categories from the sound entry's group (+0x10, name +0x108): the game's groups
  are NSL_MASTER, NSL_LISTENER, VOICE, GUI, MUSIC, SFX (the same in the front end and all 14 missions, checked
  10-30; no other group name in the 26 bank-carrying packs; an entry without a group name has none, treated as
  SFX); enhanced sends SFX + VOICE (the player's gun is two
  stereo SFX voices + one mono), MUSIC/GUI stay dry. OpenAL Soft 1.24: a fully focused reverb pan folds back to
  the middle in stereo (capped at 0.6); HRTF pans inconsistently (not handled).
- Advanced reverb (user 10-30: the real test is the public; let players tune it): audio/reverb_tuning.h (header
  only) amount dB, decay x, brightness x, early reflections dB, room clutter (absorption the rays miss, 0.07);
  settings.ini "reverb = amount=-3 decay=1.2" / --reverb, launcher Sound > Advanced reverb, PC menu live sliders;
  game::SndEnv_SetTuning. tests/reverb_tuning_test.
- PC menu (` key): "Enhanced sound" switches live (audio::set_enhanced; saved to settings.ini on close). Reverb
  changes are logged ("reverb: level ..., decay ...", at most once a second).
- OpenAL errors are logged per call site with the values (output.cpp al_failed). Reason: an original-mode
  prologue run (10-29) logged "cannot set up a callback buffer" 125 times from 262 s (a dialogue stream's
  format change) and the squad/scripts stalled afterwards; the old check could not tell a real failure from a
  left-over error. Not seen again in 25+ min of play; cause open.
- Open: the sergeant in the prologue cutscene sounds left of centre (also before our sound module; the listener
  is probably the player, not the cutscene camera).
Step 3c (2026-10-30): volume per kind of sound, a plain setting (original and enhanced alike), on top of the game's
one volume slider (0x008dce88): effects, ambience, dialogue, music in percent (gain = percent squared, 50% = -12 dB;
GUI goes with effects). audio/category_volumes.h (header only), settings.ini `volume = "music=50 ambience=60"`,
--volume, launcher Sound section, PC menu live sliders (saved on close); audio::set_category_volumes, applied in
Engine::update. tests/category_volumes_test.
- Found in a steered run (user's F12: "background effects are categorized as music"): the game plays its level
  background beds through Music_Play in the MUSIC group. Dumped the prologue's two MUSIC streams: music_mainmenu is
  tonal, BG_PROLOGUE_BRIEFING only distant battle. The aliases tell them apart: music = music_mainmenu, BG_MX_*,
  BG_<name>MX; every other BG_* (BG_Battle_Med, BG_Buch_Snow_int, ...) is a bed. Music_Play remembers bed sound ids
  (snd.cpp is_ambience_alias / set_ambience_sound; the alias id is the voice's sound id +0xc8), S_AmbientPlay's
  sounds count as ambience too; SndHw_Update gives them Category::Ambience (dry, like music). Sorted by name only,
  not listened to: BG_Drag_Hot_Action, BG_Mons_4_Battle, BG_Kpass_Rail_Intro, BG_PIANO_INTRO, BG_PIANO_STUKAS,
  BG_Mons_3b_Shellshock (ambience). User: "Works".
- Not categorised yet: the movie sound and voice chat (DirectSound translation, no category -> effects) until the
  client module is Replaced.
Step 3d (2026-10-30): more voices, enhanced only. The original made 90 hardware slots at boot (SndSys_CreateVoiceBuffers
counts 1,1,1,1,7,36,7,36: 18 stream slots, 36 mono 3D buffer slots (kind 5), 36 stereo (kind 6); SndHw_AllocSlot takes
the first free slot of the sound's kind and never the last slot; no stealing - a refused voice retries for 10 updates,
then is freed unless looping). FX_AddSoundComponents (its only caller of Snd_CountBusyVoices, checked in the XBE)
drops an effect's sounds while more than 15 slots are busy. Measured (temporary counters, 200 s unattended):
prologue original: 3D slots full (36), 2099 voices refused, 86% of effect sounds dropped; Tunisia 36 full, 35-67%
dropped; stereo slots: at most 3 in use. Ours: enhanced appends mono 3D slots after the original ones up to 192 3D
voices (246 slots) the first time a slot is wanted (so after a live switch too); original uses only the original
slots; the effects' budget becomes 64 busy (Snd_CountBusyVoices reports busy * 16 / 65). OpenAL context asks for
480 mono + 32 stereo sources (default 256). Prologue enhanced: 59 3D voices at the peak, 0 refused, 0.1% of effect
sounds dropped. The slot tables (0x00911548, 0x0090a940) hold 256: going further means moving them.
Step 3e (2026-10-30): occlusion, enhanced only. snd.cpp update_occlusion (after SndHw_Update's slot loop): each playing
3D slot traced when it starts (skipped until placed), then 8 per update in turn, gliding; within 48 units of the
listener never. game/code/snd_env.cpp SndEnv_Occlusion: volumetric as Steam Audio - 7 points on a 40-unit sphere at
shoulder height (origin + 48: sound origins are mostly a soldier's feet), one leaning towards the listener (the
muzzle). In view (centre or listener-side point clear): 0, the direct sound in full (user: bounces only add).
Hidden: share of the 7 lines blocked, at most the cost of the shortest way over/round the centre line's hit (points
24-160 above/beside it with both legs clear: 0.2 + 0.8 * extra length / 200 units). Hits within 32 units of the
listener / 12 of the point, and lines starting inside a wall, do not count; glass 0.35. Device: set_occlusion ->
low-pass on the direct sound (full: -3 dB, a further -6 dB at 5 kHz) and the reverb send up to +3 dB (source gain up,
filter down; AL_MAX_GAIN 4). Playtests 10-30 (user's F12 + new voice log): one line, clearest-of-three, five points
and the first sphere all muffled gunfire in plain sight - the hits were real detail brushes (contents 0x8000001:
parapets, sills, rubble), not invisible blockers. User: "Sounded much better", directly exposed sounds now equal to
original. No frame-rate cost seen (unattended prologue 60 fps).
- Diagnostics: an F12 mark logs every playing world voice (audio::log_voices: distance, distance gain, volume,
  occlusion) and, for occluded ones, each line's hit (game::SndEnv_DescribeOcclusion: position, contents, flags).
- Reverb: enclosed level lowered to -1000 mB (I3DL2 presets; was -300) and the late level max to 0 (was +200):
  "small stony rooms feel too echoey"; then + up to 2 dB in enclosed hard places only (by enclosure x hardness,
  mid absorption 0.03 all .. 0.15 none; user: "Reverb sounded better").
- Crash once (10-30, prologue 204 s, not reproduced): NAL_AnimTrack_SampleFrame+0x1c (original code, negative index)
  a second after a sound got a NaN position - an entity gone NaN; cause unknown, not seen before. Guards: the engine
  mutes a voice with a non-finite position/level (logged once), occlusion skips non-finite positions.
- Open: the game's own mix is quiet for distant gunfire (also in original; linear rolloff min..max, low volumes);
  option discussed: an enhanced softer falloff setting - not started (user: the problem was occlusion, not distance).
Step 3f (2026-10-30, user): close-range boosts and per-improvement switches.
- Boosts (enhanced, CategoryVolumes, 0 dB by default - "let players experiment"; user liked +12 for others' fire):
  close_gunfire (others' shots: alias "*_FIRE_3RD*", "*_GUN_SHOT_3P", "*_GUN_RINGOFF_3P" -> Category::Gunfire),
  own_gunfire (the player's "*_FIRE_1ST", not placed: always full), close_bullets (impacts "IMP_Bullet_*", flybys
  "WhizBySound_*"); full within 600 units, fading to none at 1200. Measured: the game mixes others' shots at
  0.04-0.23 (F12 voice log), also quiet in original.
- audio/enhanced_features.h: resampler, voices, reverb, occlusion, close_boost each on/off (settings.ini "enhanced",
  --enhanced); SoundDevice::has(Feature) = enhanced && on; Output::set_enhanced(resampler, reverb).
- PC menu: tabs General (FOV) and Sound (volumes; Enhanced sound master with each improvement's switch and its own
  options nested). Launcher: Sound tab (Output, Volume, Enhanced sound). User toggled each live: works.
- Full ctest 10-30 (35 min, build/ctest_3f.txt): 18/19 pass; leaf_test: Save_PortToDevice 11 (the known port-8 case),
  NAL_EntropyMode0B 1 mismatch + 1 inconclusive, NAL_EntropyMode10 1 inconclusive - random garbage arena input,
  untouched code, nal_entropy_test passes; user 10-30: ignore (nonsense input).
Next: the renderer (own-renderer plan); open: the one NaN crash (above).

**Own renderer, 2026-10-28 (user):** the renderer is replaced by our own instead of switching to Replaced with a 60 fps
patch fix (the patch disappears with native pacing). Stages: 1 inventory of the graphics library use (done:
[renderer-inventory.md](renderer-inventory.md), tools/d3d_inventory.py); 2 our graphics interface + a D3D9 back end with
the same picture (screenshot diffs); 3 native 60 fps and beyond; then D3D11, Vulkan, D3D12 (Vulkan/D3D12 nearer the native
build); extras as they come easily; texture pack stays on hold. Found by step 1: 8 unnamed FX draw functions
0x000a7930-0x000ac3b0 (vtable slots, "unassigned") still called D3D - decompiled the same day (FxOp*_Draw thunks +
FxOp*_DrawQuads, one shared body in fx_load.cpp): 10 of 16 matched, 6 (dd/d5/5b) set aside, no shipped data.
The 60 fps fix notes below (day 25) are obsolete.
**R1 done 2026-10-30: the renderer module runs as C (Replaced, 1086 functions), 30 fps until stage 3 (user).** Steps,
findings and what is next: [own-renderer.md](own-renderer.md). Found live: the asm entries' scratch could land on a caller's
live C frame (fixed in title_data.h GAME_GAP_ESP, floor limit in FS:[0x14]); `tools/bisect_replaced.py` finds a broken
Replaced function. nglRenderSceneTree's word 27 constant-lane mismatch added to accepted.csv (same dead-stack lane).

**R2 done 2026-10-30: our graphics interface `render::GfxDevice` and its D3D9 back end** (render/gfx_device.h,
gfx_d3d9.cpp), on the current path's device and targets so converted and unconverted draws mix; details in
[own-renderer.md](own-renderer.md). tests/gfx_device_test passes (real device, render scales 1 and 2); smoke run mission 4
clean. Also fixed: the Windows build's link clash of the decompiled MatrixMultiply with SDL3's (the C is now
MatrixMultiply3x3). Full ctest not run on it.

**R3 done 2026-10-30: the title's shader programs are HLSL of our own** (render/shaders, tools/shader_hlsl.py from a
`--dump-shaders` run). Since 0.2.2 the 166 SM3 bytecode files are prebuilt in render/shaders/prebuilt/ (plus three in
render/post/prebuilt/), so a release needs no HLSL compiler; [shader-prebuilt.md](shader-prebuilt.md) has the regeneration
recipe. tests/title_shader_test compared the original 126 against the Xbox path's translation. Details in
[own-renderer.md](own-renderer.md). Next: R4, the renderer C onto GfxDevice one draw path at a time (text and quads first),
screenshot diffs against the current path.

**R4a done 2026-10-30: the renderer C records its state in our own terms** (game/code/ngl_gfx.cpp, `game::xb` wrappers
around the 160 state-setting Xbox calls); `XB_GFX_VERIFY=1` finds no difference from the Xbox path's state in missions
4, 1, 6 and the menus. Next: R4b, the first draws onto GfxDevice (text, quads).

**R4b done 2026-10-30: every renderer draw goes through our interface** (text, quads, meshes with packed normals unpacked
by shader variants, particles, fog, mips, clears); paused scenes identical to the Xbox path in missions 4, 1, 6; same frame
time. Details in [own-renderer.md](own-renderer.md). Next (user, 2026-10-30): switchable graphics improvements under an
"Enhanced" master switch, like enhanced sound.

**Enhanced graphics 2026-10-30 (for the user's review):** master switch Original/Enhanced plus switches for FXAA,
anisotropic filtering, sharpening, colour vibrance and texture detail (amounts default 0); launcher, PC menu, settings.ini,
--graphics / --graphics-features. Details in [own-renderer.md](own-renderer.md). User review 2026-10-30: approved.

**R5 2026-10-30 (user: the renderer fully on ours, then 60 fps, the full test, a steered test, release 0.2.0; after it
the native loader only gets bug fixes):** textures, YUV quads, opening the display and presentation on our interface; no
draw reaches the Xbox path. Paused A/B scenes as before (a few pixels off by at most 3/255 in missions 1 and 6, already in
de44ed7: not from R5; not looked into yet). 60 fps back through the C's hooks (mission 4: 59.7 average, movies 30).
Full ctest 10-30 (build/ctest_r5.txt, 36 min): 21/22 pass (title_shader_test skipped without a
dump); leaf_test: Save_PortToDevice (known port-8 case), copy_block 1 (random input whose output buffer overlaps the
deflate state; passes on rerun), and NAL_Packed16EntropyIKSpin_Dequantize/_Blend crashing on every case: R1's
GAME_GAP_ESP reads FS:[0x14] and leaf_test had no FS block - it now gets a zeroed one (both 100% matched again).
User's steered test 10-30: Operation Husky 24 min at 60 fps, plays and sounds good. Next: release 0.2.0.

**qcommon push 4 (2026-10-29, day 27): 94 functions, 0 mismatches, matched 3273 -> 3352/16531, qcommon 498 -> 420.**
- Low: 11 manager *_DeletingDtor (forced on stack objects, only the "not registered" path), all ResourceManager_Create*,
  DynArray/PtrVector/std_list helpers, ShortString/FixedStr64_Printf. High: Com_GetEvent, MatrixToAngles/AxisToAngles, Pak
  zones, MemContext_*_r, Quat/Mat44, Loc_GetString/TranslateString, Profile_Copy, Com_Shutdown (forced).
- Ghidra: DynArray_AllocIntsZeroed(_2) extended past their inline int3 to ret 8 / ret 4; created AlignedAllocator_DeletingDtor
  0x00094100, Pak_HasZoneNamed 0x0028e0b0, FxComponentB_IsOwnerGone 0x0029d620 (renderer, live vtable slot, NO C yet -
  renderer not complete until it has C), Vec3_RotateByAngles 0x002a2560 (dead). Duplicate names fixed: 0x0029ea20 ->
  Bounds_AddPointSplit (0x00029380 keeps Bounds_AddPointSimd), 0x00094120 -> AlignedAllocator_Alloc (0x0009be80 keeps apsAlloc).
- Set aside: Com_Freeze_f (debug: spins on Com_Milliseconds), Sys_GetStackBounds (boot), HandleTable512/EntHandleTable_
  ReportFull (debug), 7 dead. FUN_0001ec40 = level-manager vtable 0x00832614 slot 0 (unassigned).
- Fields: manager table 0x009719fc (count 0x009719f8, max 20); std::string 0x1c B (+4 buffer, +0x14 size, +0x18 capacity);
  DynArray ints from SmallHeap 0x0095f4a4 else heap 0x0095f4a0; memory context heaps +0x90 (count +0x8c), children +0xc4.
- Left low: File_ init/pumps, std_string/map/set/tree rows, FUN_ 20b80/20fd0/21670/21a00/21b30/21ee0/22460/28bd0/28ed0/d19e0,
  Save_*, HashMap/HashStr inserts, handle-table allocs. Left high: FS_FOpenFile*/ReadFile/Seek/Startup/AddGameDirectory,
  FS_Printf, Cmd_/Cbuf_, Com_Frame/Init/EventLoop, RotatePointAroundVector, Matrix_Orthonormalize, Loc_ parsers,
  ProfileSave callbacks, templates, FUN_002a9c10/0031ac60/002c5780/002c5ac0.

**qcommon push 3 (2026-10-28, day 26 late): 87 functions, 0 mismatches, matched 3202 -> 3273/16527, qcommon 560 -> 498.**
- Low: dlmalloc heap (Heap_EnsureInit = malloc_consolidate, Heap_FreeBlock = free), SmallHeap, File_ requests. High:
  vectoangles family, Pak zones, MemContext, event journal, Netchan_Process/Transmit, Loc_, profiles.
- Ghidra boundaries fixed (user: "we want our work to be correct"): FUN_00222f70 merged into Sys_GetEvent; Heap_InitBins
  split out of Heap_EnsureInit; FUN_00297bdf replaced by FS_FilesAreEqual 0x00297b90; 12 functions created (Heap_Alloc16_2,
  Heap_Mallopt/Valloc/Pvalloc/IndependentCalloc/Comalloc, File_OpenMode7/GetSizeByName/QueueRequestMaybeAlt/ReadAtAlt,
  Msg_TransmitWithEOF split from Mat43_UntransformPoint). Total 16516 -> 16527. Heap_Free/Z_Malloc/Heap_AllocAligned were
  already right (the inline int3 sits inside their bodies). default_xbe_signatures/matchdata lack the new functions.
- Fields: dlmalloc state 0x494 B x 8 heaps from 0x008ff638 (top +0x2c, bins +0x34, binmap +0x434); SmallHeap pools 0x28 B;
  Pak 0x0095f638; Netchan sock +0, remote +8, in seq +0x20, out seq +0x24; loopback 0x00928590 (0x93c per side).
- Left low: File_ init/pumps (FUN_000c9530/9880/ca150/ca250), STL rows + ~15 FUN_, Save_/ResourceManager_/DeletingDtor rows.
  Left high: FS_FOpenFile*/ReadFile/Seek/Startup/AddGameDirectory, Cmd_/Cbuf_, Com_Frame/Init/EventLoop/Filter,
  RotatePointAroundVector, Quat/Mat44 helpers, Loc_ parsers, Noise_Init, Profile_Copy, templates, FUN_ rows.

**qcommon push 2 (2026-10-28, day 26 night): 93 functions + 5 forced console commands, 0 open mismatches, matched 3120 -> 3202, qcommon 641 -> 560.**
- Low (qcommon_core.cpp): math, Save_, File_/HandlePool. High (qcommon_fs.cpp): q_math, Loc_ text classifiers, FS file handles,
  Cvar_SetA/SetS/Restart_f, Cmd_Vstr_f, Cvar_Toggle_f forced with cmd_argc/argv poked.
- Save_PortToDevice: 9 offline mismatches, all port 8 (original reads past its 6-entry table into its return address;
  both callers handle port 8 first) - accept at the switch. Quat_RotationBetween: NaN-payload only (as Quat_Slerp); it
  builds a 4x4 matrix, rename to Mat44_RotationBetweenVectors to consider.
- Set aside: XAPI device layer + Save_CalculateSignature (host), Cvar_Reset_f (kind `crash`: original strlen(NULL)),
  dead Vec3_Scale, Vec3Distance, Q_strncmp, gap FS_FilesAreEqual 0x00297b90.
- Ghidra: FUN_00297a69 merged into FS_Write (body 0x00297a60-0x00297ae2, done 2026-10-28).
- Fields: SaveRequest names[8][64] +0, cursor +0x240, count +0x244; HandlePool data +0, free +4, used +0x10, stride +0x1c,
  count +0x20, mask +0x24 (handle bit 31 = free); file records 36 B at 0x920820; requests 0x44 B at 0x920810.
- Left low: Heap_Free/Heap_*, File_Open/QueueRead/Pump, std_string/map/list, ~15 FUN_ (STL static init, tree erase/insert,
  FileSystem init 0xc9530/0xc9880, device pumps). Left high: FS_FOpenFile*/ReadFile/Seek/Startup, Cmd_/Cbuf_, Com_Frame/Init/
  EventLoop, Netchan_Process/Transmit, Pak_*, MemContext_*, Profile*, vectoangles family, Loc_ parsers, templates, 9 FUN_.

**qcommon push 1 (2026-10-28, day 26 evening): 111 functions, 0 mismatches, matched 3036 -> 3120/16517, qcommon 727 -> 641.**
- Split by address over two worktrees: < 0x170000 -> game/code/qcommon_core.cpp, >= 0x170000 -> qcommon_fs.cpp
  (qcommon.cpp untouched). 18 names applied in Ghidra; jmp thunks, sub-5-byte stubs (kind `unhookable`), Sys_DebugBreak,
  Sys_ReadTimeStamp (dead), Mem_InitLargeBlockPools (boot) in set_aside.csv.
- Open: Cvar_Restart_f/Reset_f/SetA_f/SetS_f, Cmd_Vstr_f need argv for --force; ResourceManager_Destruct;
  Cvar_WriteNamedValues (writes a file); 12 DynArray/std_list/Pak/Angle*Quantized never reached (callees hit the XAPI heap).
- Ghidra to check: FUN_00222f70 = empty-queue tail of Sys_GetEvent (merge into it); FUN_00297a69/00297bdf after FS_Write
  (no callers, boundaries?); Bounds_AddPointSplit 0x0029ea20 dead candidate (pointer search not run).
- Fields: DynArray {data, capacity, size}; HashMap head +0x08, buckets +0x14, mask +0x20, max_index +0x24, index
  (key ^ 0xdeadbeef) & mask; event queue 0x0095e800 (64 x 24), head 0x0095e7f4, tail 0x0095ee04; cheats 0x00954f34.
- Renderer: apsOrientToVelocity (fx.cpp) still mismatches offline (438/1002, rest drift; suspect Mat44_FromDirection, 11
  offline mismatches on record) - renderer not fully clean.
- Next low half starts at Vec3A_Normalize (float math) through the File_/HandlePool/Save_ group at 0xd0c40; Heap_Free
  (tail-jumps 0x43260).

**Resume here (decomp, 2026-10-28, day 26): module pushes, matched 2564 -> 3031/16517.**
- Complete (every function rewritten, tested or set aside): collision, streaming, xanim, cgame, zlib (+ renderer
  from day 25). Coordinated pushes: one worktree + fresh agent per module push (briefs in cod_bro/wt/).
- Sound: all C; left 4 vtable-only SoundMenu functions (need a live sound-options menu), S_FreeLevelSounds (not
  reached), S_WaitForWaveBanks (unhookable: loops into its prologue).
- Server: SV_ClientCommand (not reached, indirect via VM_Call) + SV_SpawnServer, SV_Map_f (wait on the title's file
  request thread, then D3D/input host code) + 6 shutdown/restart/save functions no run reaches.
- Harness today: pass-through file calls and sound calls in test processes (docs/comparison-harness.md "File calls",
  "Sound calls"), resume slot zeroed, hook refuses branches into its own prologue (6 functions now unhookable),
  --force: 32 words, nested reads, E*N, ?A=B, XBCMP_POKE, XBCMP_ORIGINAL_TWICE determinism runs. Gap entries
  whose callees read files need gaps of at least 0x30000.
- Still inconclusive by host code: D3D getters (nglTextureGetSurfaces), XPP input (IN_Frame, rumble), the file
  request thread. A renderer/input stand-in would be next if wanted.
- zlib: zlib 1.1.4 + unzip 0.15 (Q3 copy) in zlib.cpp/unzip.cpp; manual e2e test tests/zlib_e2e (not in CMake).
- Names applied in Ghidra today: ~50 (manual_names.csv rows dated 2026-10-28); S_StopAllChannels -> S_UpdateChannels,
  MSG_ReadString (0x002aa480) -> MSG_ReadStringLine. Gap functions stay uncreated; their names live in functions.csv.
- Unnamed callees seen: FUN_003134e0/00313240/00313790 (front-end loading-bar steps, chapters 1-5/6-8/9+),
  FUN_0024bce0 (CG_EntityEvent 0xc5), FUN_002e2e40 (prone yaw limit?).
- Gap 0x00376670: static initializer (table 0x008976dc) setting 0x009638b0 = 42.5f, not created.
- Next (user to pick): qcommon (727 left), client leftovers (288), game (1348), script; or D3D/input stand-ins.

- Quat_Slerp NaN payloads (user, 2026-10-28: leave until the switch to Replaced): with NaN in both terms of the final
  add(mul(la,w0), mul(lb,w1)) GCC's swapped commutative SSE operands return b's payload, the original a's; fix = a
  fixed-destination addps/mulps helper in lanes.h. Settled 647/647 in game.

**Resume here (decomp, 2026-10-27, day 25): renderer complete, matched 2536 -> 2564/16517.**
- Done today: leftovers sorted (mslib ranges, module_hints.csv via tools/sort_unnamed.py, 74 static initializers named);
  renderer finished (last 23, compare-all clean, full leaf_test: 4 overlapping-argument fixes); set-aside state
  (tools/data/set_aside.csv: 48 renderer + 6 Live screens); steered client menu run (14 first-time matched).
- Renderer: stays Compare. Switch to Replaced = 60 fps patch fix (below, user: at switch time) + smoke runs (debug and
  release). 7 live functions still lack a conclusive test (listed under "Set aside" below).
- Client open (128 untested, 71 inconclusive): the inconclusive ones call XAPI file code - user to choose stand-ins vs
  set aside as host file code; menu destructors + console commands -> --force / exec_sweep; memory-unit error dialogs ->
  prepared save folders; Collector's-edition screens -> not testable with this XBE (user to decide).
- Other modules by size left: collision (2 unnamed, 38 named), streaming, xanim, server (8 on-record mismatches).
- Untouched suggestions (user: "nothing now"): renderer struct typing, D3D call inventory, CPU-overhead cuts.
- Online module set aside (user, 2026-10-27): all 27 (21 named, 6 unnamed by module hint inside the Live block 0x00220de0-
  0x00224a50 + 0x0002ba90-0x0002bce0) in set_aside.csv kind `live`, not decompiled; set-aside now also applies to
  functions without C, and FUN_ names never count as named. Our own engine has no Live code at all.
- README progress table: one row per module (user, 2026-10-27), so each push maps onto one row.
- Plan for the next sessions (user, 2026-10-27): name and decompile module by module in big concentrated pushes - one
  module to complete (named, sorted, decompiled, tested or set aside), then the next.

**Steered client menu run, 2026-10-27 (build/compare/menu_user6, 508 s, release, copy of the user's saves, XBCMP_ONLY = the
152 untested client functions + SV_SetSkillEasy/Hard, every call):** route: bonus movies/concepts/characters/vehicles,
options/advanced/controller, profiles, chapter select, Easy + Hard, pause Restart/Quit, mission failed. 14 first-time
matched (UI_DialogLayoutButtons, ChapterMenu_Dtor, Bonus*Menu dtors, SV_SetSkillEasy/Hard, FE_BonusCharacterLabel,
LoadingMenu_SetMissionRange, AdvancedOptionsMenu_ResetDefaults, InGameScreens_Construct, PauseMenu_ShowRestart/QuitDialog),
0 mismatches, 4 inconclusive (XAPI file / SndHw host code), 134 not called (all hooked; 16 calls lost to "all busy"):
- ~35 menu destructors: the screens stay allocated (reboots re-exec the loader) - shutdown paths, forced calls.
- memory-unit error paths (AutosaveMenu/ProfileMenu dialogs, MemDlg_*, FE_CheckStartupStorage, SaveSlot_InitAll): need a
  full / unformatted / corrupt save device - prepared data folders, not steering.
- Collector's-edition screens (SpecialConcept/MaterialsMenu, FE_BonusCE*, FE_SetupSpecial*): the CE XBE is another binary
  ("Collectors editions/", hash differs) - not testable with this build's addresses.
- Xbox Live screens (InGameLiveOptionsMenu, XboxLiveMenu): set aside (user, 2026-10-27: Live is defunct, no sign-in), kind
  `live` in tools/data/set_aside.csv.
- console commands / CL_Shutdown: exec_sweep / --force. MovieVid_Predict{FieldMotion,DualPrime}: MPEG-2 modes the
  shipped movies may not use - offline test. The rest: UIText/UIMenu/UITextTable helpers of screens not visited.

**Renderer complete, 2026-10-27 (day 25):** every function named, sorted and decompiled (1084; 1025 rewritten, the
rest set aside by decision). It stays Compare until the switch to Replaced (needs the 60 fps patch fix below).
- Part 5 (dec-rfin): the last 23. FxSystemPool_Refresh matched 4, DObj_Ctor settled 222 (DObj +0x8c = handle, its
  slot in the handle table 0x00981370), FxComponent_OwnerGone offline 3820 at 100% (slot 6 of the component base
  vtable 0x00837624); 4 pure-store static initializers offline 4000 at 100% (game/code/r_static_init.cpp).
- Dead (no calls, no stored pointer): FxSystem_UpdateBounds, FxSystemPool_Destroy (only caller gap 0x00095640 <- gap
  0x0009c190, unreferenced), R_DrawBackBufferOverlay.
- Untestable by design (atexit, the title never exits): the 11 renderer AtExit_* destructors.
- Sorted out of the renderer: ClientActive_Ctor (client; static init gap 0x00376450), HashStrMap_Ctor (qcommon; static
  init gaps 0x003784c0/0x003784e0/0x00378750/0x00378770). Gap 0x0029d620 = slot 6 of vtable 0x008376fc.
- Release build rechecked: no drift (below).
- Final checks (user, 2026-10-27): `--compare-all` missions 1/4/5/6 (build/compare/rall, 40k tests): renderer 0 mismatches
  outside accepted.csv. Full leaf_test (build/leaf_full_1027.txt, first since the chain-mutation generator): 570 of 574 clean;
  4 real differences on overlapping arguments, fixed: FxOp53_Init/FxOpGrid_Init (16-byte copies now load before storing,
  copy16; columns/rows re-read per store like the original) and NAL dequantize3 (all inputs read before the first
  store); FxOp*_Init and both dequantizers clean over seeds 0-2. Renderer matched 1025 -> 1028.
- Set aside (user, 2026-10-27): the 48 renderer functions not tested by decision are listed in tools/data/set_aside.csv
  (kind: dead/boot/no data/debug/host/exit) and get their own progress state and README column; not counted as
  rewritten. Still waiting for a conclusive test (live, no mismatch on record): nglSkipList_RemoveTexture (offline 68%),
  nglSkipList_RemoveIndirect, nglFindFontAddRef, FxPool_SetSlotTag, XModel_GetBoneCount (offline 87%),
  XModel_CalcBoneChildren, FXCmd_Type2AtTransform.

**Sorting the leftovers, 2026-10-27 (user: option b + provisional sorting):** unsorted .text 2,633 -> 63.
- Microsoft library blocks the signature matcher missed are now sorted by address range into `mslib`
  (modules.csv): XGRAPHICS TrueType rasteriser 0x2bfa1-0x423ad (328 unnamed), XVOICE/UIX 0xd27b9-0x1199d0,
  XAPILIB/LIBCMT 0x1e6d14-0x1fa3d5, LIBCMT __CI* 0x33ac80, UIX atexit destructors 0x379dfb. Library-named functions
  nothing else sorts go to `mslib` too. They stay unnamed (Microsoft code, replaced, not rewritten).
- 74 C++ static initializers and atexit destructors (0x372ef0-0x379e70, no direct callers: the CRT init table and
  _atexit) named `StaticInit_*` / `AtExit_*` / `CrtInitStub_*` from the globals they fill or free, module from those
  globals' readers (manual rows in tools/data/module_hints.csv).
- Provisional modules for 423 unnamed game functions: `tools/sort_unnamed.py` writes auto rows of module_hints.csv from
  the neighbours in address order and the sorted .text callers (prefix rules still win once a function is named). They
  get named as each module's decompile reaches them; the 63 left without a hint are mostly caller-less (vtable/table
  slots or dead) and sit between modules - sort them when read.
- Rerun after renames: `progress.py`, `sort_unnamed.py`, `progress.py --readme`.
- At the renderer's switch to Replaced (user, 2026-10-27: fix then, not before; the native build replaces the patches
  later): the 60 fps patch breaks - its nglSetVBlankMode entry jump is overwritten by the Replaced hook (installed
  after the patches, main.cpp), and its movie-wait thunk sits on tail jumps inside nglVBlankCallback +0x2d and
  nglDrawFpsCounter +0x258 that the C versions don't run. Fix: install it after the Replaced hooks, move the movie check
  to nglWaitFrameInterval's entry. Also `--force "scene:"` (hook inside R_RenderView +0x21a) stops firing once R_RenderView is C.

**Renderer finish, 2026-10-27 (day 25), in progress:** renderer left = 32 untested + 7 boot-inconclusive (live only).
- Forced calls (build/compare/force_r1-r3): FX_FindEvictableEffect 30/30 (`FX_FindEvictableEffect([[[[0x95f630]+0x5c]+0xd4]+4])?[0x98880c]`),
  FX_PruneComponents settled 120/120 (`esi=[[0x95f5b4]+4]` / `+8`, `?[[0x95f5b4]]`: the active-effect list FX_UpdateEffects
  walks, count +0, effects from +4), DObj_DebugDrawAnimTrees settled 108 (`edi=[[0x97a968+8k]+0x22c]?` same, k 0-127:
  entity handle table). Draw-list calls at the client-frame site crash in both versions (nglListAddMesh/AddText read the
  null current scene 0x0090259c): scene-time functions need another call site.
- Dead (static, checked with xbefind call/imm): nglDebugDrawLine/BoxMesh/PyramidMesh (only self-recursion, the dead
  nglDebugDrawAxes and uncalled gap functions 0x0006d200/0x0006d220), RE_DrawTextWithIcons/RE_TextWidthWithIcons/
  R_ParseTextIcon (refexport slots +0x94/+0x98 of re 0x009284e8; the only reader is the uncalled gap function 0x002b2b40),
  nglParseDDSPalette/nglCreateDDSPalette (self-recursion). levelreach --dead no longer counts self-recursion as a caller;
  it still counts a stored code address as live, and forced tests as "ran".
- Content: cdWater_Draw / cdWorldVertexLit_Draw only in Omaha Beach (mission 9: zones omaha06/07/10/12) and Mons (10:
  mons01b) - later zones, not reached by 110 s unattended runs: a user-steered run through those levels. No pack uses cddebug.
- User-steered Omaha Beach run (build/compare/omaha_user, 97 min, release, XBCMP_ONLY = all 307 untested, every call):
  cdWater_Draw 13857/13857 and cdWorldVertexLit_Draw 45636/45636 settled, 22 more first-time matches (HUD timers,
  LoadingMenu_Draw settled, Sound/Screen menu setup, menu dtors, CL_InitRef, nglMaterial_ReleaseTwoTexturesClear), no
  mismatches; 4 save/zone functions inconclusive (XAPI file code). Matched 2504 -> 2524.
- `--force "scene:..."` (new): call site inside R_RenderView with the main scene open (docs/comparison-harness.md).
  First use found a real bug: nglColorToVec4's `1.0f / 255.0f` is no longer folded under -frounding-math and rounds
  down with the title's MXCSR (0x3b808080, so 255 -> 0.99999994); now the original's constant as a hex float. Same fix for
  1/3 (cgame text scale, 0x3eaaaaab), 1/640 and 1/480 (nglInit). nglColorToVec4 offline 3817/3817 (was 961 mismatched),
  nglDebugDrawLine 30/30 at the scene site (pre-fix drift row dropped). R_RenderView's mismatch is the on-record one.
- Not tested by decision (user, 2026-10-27): the boot/device functions (nglInit, nglSelectVideoMode, nglInitFrameBuffers,
  nglInitMultisample, nglResetDevice, nglInitShaders, Anim_InitManagers) - the native engine replaces them with its own
  back ends - and paths no shipped data reaches (FxRenderer_NewOp5b / FxOp5b_AddToList 'CBlb', nglMeshFile_RelocateSection3,
  cdDebug_AddToList/Draw: no pack uses cddebug). Their C stays.
- Renderer left to test (live, 2026-10-27): debug/dev only - nglVertexWriter_Init, nglMeshBuilder_AddPosSection
  (CM_DrawWorldTreeDebug), nglDebugLineBatch_Draw, nglDebugShader_Draw, nglDumpPrintf, nglGetFreeMemory (fps display);
  host file/screenshot - nglSaveScreenshot, nglSaveTextureTGA, nglWriteTGA, nglReadFile, nglFreeFileBuffer; vtable slots -
  nglTextureManager/MeshManager/ResourceManager_ForEachPass, nglMaterial_LoadTextureRef; FX - FxAction_Destroy/
  DeletingDtor (shutdown), FxOpdd_AddToList (mission 11 data); other - nglGenerateRenderTargetMips, nglRestoreRenderStates,
  nglCreateNormalCubeMap, nglLockPalette, nglSkipList_RemoveIndirect (offline 68-84%), RE_ParseButtonToken (offline 46%).
- The 13 (user, 2026-10-27; the 11 debug/dev and host file/screenshot functions above are not tested by decision, like
  the boot ones). Matched with forced calls (build/compare/force_r4-r6): nglTexture/Resource/MeshManager_ForEachPass
  (ecx = [0x009025f0] / [0x00903d10] / [0x009028d0] / [0x00902a80/84/88]: those globals hold manager pointers),
  FxAction_Destroy / FxAction_DeletingDtor (an effect definition's first stage action: `[[[def+0x50]+0x14]]`, the stage's
  +0x14 is an apsPtrVector), nglRestoreRenderStates (ecx=0x00902f08), nglGenerateRenderTargetMips (scene:, the scene's
  render target [[0x0090259c]+0x2f8]), RE_ParseButtonToken and nglMaterial_LoadTextureRef (`&N`, new). Matched 2524 -> 2534.
  Left: nglSkipList_RemoveIndirect (offline 1821 matched, 84%: the unlink loop needs a consistent list; the mesh
  managers (0x14 bytes, list embedded at +4, Ghidra nglInitMeshManagers) have live first nodes whose key 0x8383ecb8 is
  unreadable even in the main process - find which list is indirect in practice, or build consistent skip lists in leaf_test), FxOpdd_AddToList
  (op dd exists in mission 11 data; needs an op object at the scene site), nglCreateNormalCubeMap / nglLockPalette (D3D
  resource creation / lock: host code - not tested by decision, user 2026-10-27, like the boot functions).
- Focused pass (user, 2026-10-27): nglSkipList_RemoveIndirect offline 1805/1805 at 94% (leaf_test's new chain mutation:
  an argument 1-3 pointer steps from another, so the removed object is in the list; RemoveTexture/AnimManager_SkipListRemove
  also reach 94-95% on some seeds). FxOpdd_AddToList settled 108/108 at the scene site on live emitters:
  `scene:FxOpdd_AddToList(ecx=[E],[E+0xbc],[E+0xc0],E+0x84,E+0x60,0,0)?[E+0xc0]`, E = `[[[[[0x988ce8]+4j]+4]+0x54]+4k]`
  (effect list 0x00988ce8 / count 0x00988cf0, effect +4 = system, system +0x54 = emitters; FxEmitter_Draw's argument
  recipe; the op's type is not checked, so any live renderer serves). `?W` guards that cannot be read yet now wait.
  Renderer: every live function tested except those set aside by decision. Not run since the generator change: the
  full leaf_test (ask first).
- Offline coverage (tests/leaf_test LEAF_REACHED=DIR, new): nglFreeFileBuffer 86% = everything but the two tail jumps
  through file-hook pointers 0x009098dc/0x009098ec (set only by FUN_000938f0 from Sys_Main); RE_ParseButtonToken 46% =
  no random string holds both '{' and '}'. Coverage per LEAF_SEED is not reproducible across LEAF_ONLY sets.

**Steered menu run 2026-10-26 (build/compare/menu_user3, 197 s, optimized build, copy of the user's saves):** 55
functions tested for the first time, matched 2418 -> 2462; no new mismatches in already-tested functions. Covered:
profiles (load + create keyboard), chapter select, Options/Gameplay/Controller config, loading screen. To fix: UI_ShowDialog
(ui_menu.cpp), GameplayMenu_Draw and ControllerConfigMenu_Draw (fe_options.cpp) mismatch on heap word 0x100006a8 (the
text-setter stack address: their setter calls need the original's ESP, FE_GAP_ENTRY). Not reached yet: bonus/special
galleries, pause menu dialogs, autosave slots, Sound/Screen/Advanced options. Inconclusive ones call XAPI file code.
Quit: the retail game has no quit option; CL_Shutdown only via the console "quit" (Com_Quit_f) - test with --force.

**Cheat-unlocked menu runs 2026-10-26 (menu_user4/5):** the chapter-select cheat (hold both triggers, Up Up Down
Down Left Left Right Right X Right X Right X) opens every chapter and bonus; the unlock lives in the profile block
(file 0x30 / 0x40) but these runs did not write it to disk. 44 functions first tested, matched 2462 -> 2469. Fixed the same day (fix_user1, all matched, pre-fix rows dropped): the
text-setter stack word 0x100006a8 in UI_ShowDialog (UI_ShowDialogAt: strings in the original's arg / push slots, every
call at the body ESP), GameplayMenu_Draw / ControllerConfigMenu_Draw (DRAW_GAP_ENTRY) and FE_BonusCharacter/Vehicle/
WeaponSetText (FE_FRAME_ENTRY). The user's real profile was unlocked with tools/profile_edit.py (confirmed in game). Bonus gallery handlers / image loaders are inconclusive only (they call XAPI file code).

**Resume here (decomp, 2026-10-26, day 24, autonomous 4 h session): client module, matched 2152 -> 2414/16517.**
Two subagents (worktrees wt/fx, wt/rest; brief cod_bro/wt/decomp_brief.md, now with the offline leaf-test step),
one fresh agent per batch, coordinator merges (`tools/merge_functions_csv.py` resolves functions.csv conflicts by
adding both sides' counts; functions.cpp conflicts: keep both, address order).
- UI primitives in fe.cpp: UIElement/UIQuad/Color_Modulate (19), UIPicture/UIMeshList/UIWorldPicture (23). All
  matched (in game or offline) or unreached; UIElement_GetDepth unhookable (4-byte stub).
- Movie: the video decoder (37 MovieVid_*, movie_vid.cpp: MMX results reloaded through asm helpers, the harness
  compares ST/MM registers) and the player (20 Movie/MovieBase/Subtitle, movie.cpp; file/DirectSound ones
  inconclusive). Movies play in --level runs of missions 1 and 6 only.
- Fixed: the -O2-only nglRenderSceneTree drift = GCC folding 1.0f/320.0f (nglMatrixScreenToClip) at build time while
  the title's MXCSR does not round to nearest -> game_code builds with -frounding-math (found by bisecting ngl.cpp
  with per-range `#pragma GCC optimize("O0")`). Release compare runs are now as good as -O0 ones.
- Fixed: GAME_GAP_ESP (day 23) made 17 entry epilogues wrong: they undid the gap with `addl $gap, %esp`, but the body
  ESP is aligned down or moved below the host floor -> wrong EDI/ESI/return address (release: segfault in our
  R_AddEntitySurfaces under R_RenderView tests). The macro now pushes the entry's ESP; epilogues `popl %esp`.
  game_body_esp keeps the body clear of the macro's scratch (the pushed word clobbered it in the nested case).
- Proposed names (applied in Ghidra and the code 2026-10-26; FUN_003173e0 = UIQuad_Fill, FUN_00322b30 = UI_FindLocalizedTexture, FUN_00316480 = Panel_ReadVec3): UIQuad_GetMaxCorner -> UIQuad_GetMaxUV, UIPictureBlock_SetVertexUVs ->
  UIQuad_SetVertexUVs; FUN_000cfb20 MovieVid_Idct, FUN_000cea00 MovieVid_ReadMotionDelta, FUN_000d0860
  MovieVid_ConvertYuv8; FUN_003173e0 (fills a UI quad), FUN_00322b30 (texture by name, language prefix first),
  FUN_00316480 (reads 3 LE floats). Struct notes are in the batch commit messages / headers (movie.h, fe.h).
- Before Replaced: UIPicture_GetBoundsMin/Max need entries saving XMM6/XMM7 (SetQuadCorners keeps values there).
- Later batches (same session): UIText family (40), menu framework ui_menu.cpp (47), panels/dialogs ui_panel.cpp
  (44), CL/SCR core (33), pad input + movie gap functions (26), every front-end menu class (fe_menus/fe_options/
  fe_profile/fe_bonus/fe_special.cpp, ~210), FE_* helpers (27), 10 of 33 FE_Setup* (fe_setup.cpp: register-argument
  helpers FE_SetupPanel calls at level start, so the other 23 are testable in level runs - next batch). Client left: the 23,
  named functions undecompiled except what the last batch lists; ~250 decompiled client functions are untested
  because unattended --level runs never show the front end. NEXT: a user-steered `--compare` menu walk
  (memory "user steers coverage runs") over every main-menu screen, bonus/special/chapter/profile/save menus, then
  the client gap functions the batches found (bonus 0x00012ff0/0x00013000/0x00013040/0x0031ba90/0x0031bb50/
  0x0031bc10/0x00335640, 0x0031c430-0x0031c5d0, 0x0031d110-0x0031d250, 0x0031d752-0x0031d820; options 0x0031ec90,
  0x00012c60/70, 0x000127a0/b0; profile 0x00338a40; chapter 0x00337e00).
- Gap entries for set-text callers: UIText stores a stack address in heap word 0x100006a8, so every function that
  reaches a text setter runs its title calls at the original's ESP (FE_GAP_ENTRY, FE_FRAME_ENTRY in fe.cpp).
- Tools: fnpack no longer flags a relative CALL/JMP in the first 5 bytes (the trampoline re-encodes it);
  merge_functions_csv.py merges functions.csv counts and unions kFunctions/#include/CMake-source conflicts.
- FE_Setup*: all 33 done (2026-10-26; fe_setup.cpp, fe_setup_bonus.cpp, fe_setup_special.cpp, helpers in
  fe_setup_internal.h). FE_SetupPanel calls every helper at the original's ESP (131/131, settled). Not reached yet:
  FE_SetupSpecialConceptMenu / FE_SetupSpecialMaterialsMenu (collector's-edition panels; may need the CE build).
- Open (ctest 10-26, leaf_test, new random inputs; unrelated to the batch): nglColorToVec4 961/3857 mismatches, 1 ulp
  plus MXCSR PE (7f80 vs 7fa0) - check the -frounding-math change against its float->int path; NAL_Packed16Entropy-
  IKSpin_BeginChannel 1/1007 (arena word 00000700 vs ffffff90). offline.csv had both at 100%.
- Final missions 1/4 regression (build/compare/day24_final): only on-record mismatches, plus CL_Init's first test:
  refimport slots 0x12/0x17 the original never sets (stale pushed Cmd_AddCommand names vs our zeros) - accepted.csv row.
- Loader patch to update before Replaced: 0x0031fc10 (UIScreenMgr_HandleInput entry, loader/patches.cpp).
- More proposed names (applied 2026-10-26: CL_ResumeFromPause -> CL_UpdatePauseOverlay, FE_DrawLoadingQuote -> ChapterMenu_Draw (ChapterMenu slot 10), 0x003307a0 = MissionSuccessFailMenu_ReloadCheckpoint (both callers are that menu), 0x0031a390 SpecialConceptMenu_NextItem): 0x0030ea00 UI_StickDirectionHeld; 0x0032de50 CL_PauseIntoFrontEnd;
  CL_ResumeFromPause/0x0032df90 = pause overlay update; FE_BonusStopMovie (0x00333060) = SpecialMaterialsMenu_PlayMovie;
  FE_BonusMovieFileName (0x003330f0) = SpecialMaterialsMenu_HandleSelect; FE_MainMenu_UpdateSignIn = MainMenu_Update;
  FE_DrawLoadingQuote = loading-screen Draw; 0x00313610/0x00313360/0x00313120 Loading/LoadAfrica/LoadSicilyMenu_
  ShowMissions; 0x0030cfb0 MissionSuccessFailMenu_ClearTexts; 0x0030e390/0x0030e4c0 Gameplay/AdvancedOptionsMenu_
  UpdateLabelColors; 0x0031ee10 AdvancedOptionsMenu_SetSliderBar; 0x0031e9d0/0x0031e810 ControllerConfigMenu_
  UpdateSliderA/B; 0x0030f950 UI_GetTextWidth; 0x00315cd0 UIText_FadeColour; 0x00012480 UITextLine_VectorDeletingDtor;
  0x0031a390 SpecialConceptMenu next item; 0x003307a0 is MissionSuccessFailMenu_ReloadCheckpoint in the symbols but
  PauseMenu_ReloadCheckpoint in the code (reconcile). Vtables: UITextBase 0x008298b8, UIText 0x0082a450, UIPicture
  0x0082a2c0, UIWorldPicture 0x00828df8, UIRootMenu 0x00829280, InGameScreens 0x008286cc, FrontEndMenu 0x0082905c,
  Movie 0x007cf390, MovieBase 0x007cf600. Struct layouts are in the module headers (fe.h, ui_menu.h, ui_panel.h,
  movie.h, fe_*.h).

**Resume here (decomp, 2026-10-25, day 22c): user-steered menu walk + one full level, matched 2035 -> 2049.**
Two `--compare --diag` runs, optimized build, on copies of the user's saves: build/compare/menu_user2 (81 s, every
main-menu screen plus the multiplayer XBE and back) and build/compare/level_user (662 s, one whole mission to its end
reboot). First-time matched: FE_FormatPlayTime, FE_FormatPlayTime2, FE_CommandDisplayName, FE_SetDifficultyPicture,
UI_PlayTypeSound, FxOp31_Draw (29; not reached in missions 0-7 before), FX_EffectComponentsFinished,
FXCmd_PlayWeaponSoundLoop, NAL_Skeleton_Release, Actor_Negotiation_Start, Path_GetNegotiationEndNode,
Actor_GrenadeTargetClear, Use_trigger_use, CG_HasWeapon, CG_BobCycleVertical, CG_DrawHudElemClock,
HudElem_SetShaderTimer. Every other mismatch was an on-record one (SV_ trace family, stale-stack anim/renderer words).
Open:
- FE_SaveSlotsUpdateText: fixed (user chose the frame rebuild). Its title set-text methods store a stack address in
  heap word 0x100006a8, and our frame was 0x30 deeper. The asm entry now keeps nothing above a 64 KB gap (the calls'
  arguments go at entry-4) and every call runs at the original's ESP via title_call_at_regs, leaving the original's
  return address; 6/6 matched (-O0 and -O2, build/compare/fess3/fess4), the pre-fix row was reset.
- Optimized-build-only drift: in the linux-loader-release build nglRenderSceneTree gets "float drift" at frame memory
  0x80054290/0x800542a4 (bytes 1-3: ffff7f orig vs 000080 ours, i.e. ~0x7fffffxx vs 0x800000xx - looks like a
  float->int/saturation or NaN case compiled differently at -O2); the -O0 linux-loader build shows none (same
  mission-1 run, build/compare/rst_*). Find the writer in nglRenderSceneTree's subtree; matters before Replaced in
  release builds. Keep compare runs on the linux-loader preset until then (steered runs used release for frame rate).
  Rechecked 2026-10-27 (build/compare/rst2_release, release, mission 1, --only --every 1): 28205 tests, 0 drift - gone
  since the -frounding-math constant fixes; the 5632 mismatches are all the accepted constant lane (word 21).

**Renderer testing, 2026-10-25 (day 23), in progress:**
- Group 1 sweep, missions 0,2,3,7,8 (build/compare/r146a, --only the 146, --every 1): only cdOcean_SetWaveOrigin and
  nglCreateSysFont new; the 7 boot functions stay inconclusive (call host code). Missions 9-13 (r146b/c; there is
  no mission 14): FxRenderer_NewOpd5/dd, RE_RegisterModel, FxOpd5_AddToList (m11, settled). The sweep is exhausted:
  the rest is never reached by an unattended level run.
- leaf_test coverage bug fixed (e64366e): tracing stopped for good when the original called out of the image
  (stand-ins' `push target; ret` thunks, log stubs), so everything after a Z_Malloc call counted as unreached - the
  82 leaf-eligible renderer functions had matched for days at 22-86% coverage. Now a resume trampoline re-arms TF on
  return; 1 traced input in 16 makes the stand-in allocators return null (the clones' out-of-memory branch).
  Counter 2051 -> 2117 -> 2141 (second run, with the null-alloc inputs); renderer untested 138 -> 65. Still low: switch/compare-constant branches (FxRenderer_NewOp5b/d5 ~40%, RE_ParseButtonToken,
  XModel_CalcBoneChildren, nglSkipList_Remove*, FxConst*_Within equal floats): compare-constant input seeding is
  being built on branch leaf-const (worktree cod_bro/wt/leafconst).
- Forced calls (3a278e4, group 4 mechanism): `--force "NAME(ARGS)@S*N?W"` works (docs/comparison-harness.md);
  each target needs its own argument recipe (a live object with work to do - check the `force:` log values).
  Dead code is not tested (user, 2026-10-25): no CALL/JMP, no pointer in data, no immediate in code (xbefind imm;
  fnpack's "callers (0)" alone misses `mov [x], offset` callbacks): FXCmd_Type2AtOrigin, FxEffectStage_Release,
  nglDebugDrawAxes, nglDebugDrawSphereMesh, nglDrawSingleQuadFrame, nglDumpCamera, nglDumpSceneMeshes,
  R_EntityLodDistance, R_FindEffectByName, XModel_FreeAnimStates. Renderer left after them: 57 (50 untested, 7 boot).
- Fixed: nested gap entries (an asm entry of ours reached through title_call_at* at the original's ESP, e.g. our
  FX_FlushSpawnQueue -> our FX_SpawnQueuedEffect) put their C frame on the caller's (both 0x20000 below nearly the
  same ESP). Now title_call_at/_regs keep a per-thread host-stack floor and the 46 gap entries use GAME_GAP_ESP
  (title_data.h), which stays below it; first version saved registers below the gap and clobbered the outer saved
  EBP (found with --force; reports now name host code with dladdr). FX_FlushSpawnQueue 30/30 with
  --force "FX_FlushSpawnQueue(eax=[0x95f5b4])@5*30?[[0x95f5b4]+0x910]" (the FX command queue, count +0x910); missions
  1/4 regression run clean. tools/levelreach.py: which level scripts reach a function (LoadZoneSync in PROLOGUE/
  LIBEATOR reaches 6 untested zone-load functions - a longer or steered run there is next).
- Dead code, all modules (`tools/levelreach.py --dead`, 2026-10-25): 2,121 functions nothing in the XBE reaches (no
  CALL/JMP, data pointer or code address, or only from dead ones): script 559 + levelscript 457 (compiler-generated
  field accessors and script-library functions no level uses), ~830 in the 14 level sections, game 81, qcommon 62,
  renderer 40, client 24, ... Static only: pointers computed at run time are invisible (nglDrawQuad, FxMem_Alloc/Free
  ran in-game with no reference) - functions the harness saw called are excluded and listed as a warning. Not
  removed from the progress counter (ask the user first).
- User-steered Prologue run, whole mission (build/compare/prologue_user, 54 live renderer functions): only
  FXCmd_Type6AtTransform new (22/22). The "LoadZoneSync reaches 6 zone-load functions" lead was wrong: GScr_LoadZoneSync
  only loops on Stream_Update (the per-frame streamer) - levelreach paths through such hubs over-approximate. The 6
  (nglReadFile, nglFreeFileBuffer, FxEffectDef_Release, FxRenderer_NewOp5b, nglMeshFile_RelocateSection3,
  nglMeshBuilder_AddPosSection) depend on content: next is a data scan of the .cod packs (effect op 0x5b, mesh files with
  section 3, zones whose unload releases effects), then a run of the level that has it.
  User-steered Liberator run (15 min, build/compare/liberator_user): nothing new either - confirms it.
  Data scan of every .cod pack (sp/mp/bonus): no .xbmesh of 3,919 has a type-3 section (only 1 materials, 2
  meshes) -> nglMeshFile_RelocateSection3 unreachable with shipped data; no .bpe of 966 uses renderer code 'CBlb'
  (ColorBillboardRenderer = FxRenderer_NewOp5b; renderer type = Stream_ReadUInt FourCC, LE: CUVA 863 files, UVAR
  397, cUVr 123, UVRe 87, SMsh 31, Shmp 28, Blbo 9, Rect 1, CRct 1) -> unreachable. nglMeshBuilder_AddPosSection
  only from CM_DrawWorldTreeDebug (debug draw); nglReadFile/nglFreeFileBuffer = loose-file loading (host file API,
  always inconclusive in tests). FxEffectDef_Release settled with --force on loaded effect definitions:
  `FxEffectDef_Release(ecx=[[[[0x95f630]+0x5c]+0xd4]+4+j*8])` (zone 0 of the stream table 0x0095f630: +0xd0
  count, +0xd4 {?, def} pairs), 205/205.
- Merged leaf-const (compare-constant input seeding, subagent; LEAF_SEED=N). Its post-merge full run caught a
  GAME_GAP_ESP regression: the macro's scratch just below the entry is where some C rebuilds the original's frame
  (NAL_Packed16EntropyIKSpin_Dequantize copied the saved registers out: 0/3482). Scratch now 64 KB below the gap.
  Full leaf run 531/531, 0 mismatches; missions 1/4 regression clean (SV_Trace family rates within their history:
  SV_ClipMoveToEntities_r 36%, SV_CapsuleTrace 54% before today); FX_FlushSpawnQueue 91/91 forced.
- Rare pre-existing flake: "worker crashed outside a call" (NAL_Quat_BeginBlock, nglSkipList_RemoveTexture once
  each); the named function passes alone, the committed binary shows the same. Second run: two workers died of
  SIGALRM outside a call (after apsActionPlaneBounce_Apply, CM_FloodAreaConnections); retries passed - watch for it.

**Next (user, 2026-10-25): test the renderer's 146 remaining functions** (138 untested, 8 inconclusive; no new naming
passes - leftovers get named as the decompile reaches them). Groups and plan, cheapest first:
1. Rare gameplay / other levels / UI (~25: RE_DrawTextWithIcons, R_ParseTextIcon, RE_ParseButtonToken, cdWater_Draw,
   cdWorldVertexLit_Draw, FX_FindEvictableEffect, FXCmd_Type2/6AtTransform, RE_RegisterModel,
   nglGenerateRenderTargetMips, nglMeshFile_RelocateSection3, ...): unattended `--level` sweep over ALL missions with
   `--only` the 146 and XBCMP_EVERY=1 (compare_batch.py --missions 0-14 --only --every 1, linux-loader preset);
   button-icon text may need a short user-steered run with hint prompts.
2. Debug/dev (~25: nglDebugDraw*, DebugDraw_Queue*, cdDebug_*, nglDump*, R_DrawSkeletonBones_r, nglSaveScreenshot/
   nglWriteTGA, nglGetFreeMemory, CM_DrawWorldTreeDebug's nglVertexWriter_Init/nglMeshBuilder_AddPosSection):
   tools/exec_sweep.py with the g_devCommands groups.
3. Shader vertex iterators (~45: cd*_NewVertexIter, *VertexIter_Clone, nglVertexWriter_*, *_ForEachPass) and FX data
   no level has (~20: FxAction*_Init x11, FxRenderer_NewOp5b/d5/dd, FxOp5b/d5/dd_AddToList, FxConst_Within,
   FxAction_Compact): offline leaf tests; leafscan.py must accept the allocator vtable call (stand-ins exist).
4. Shutdown/free/destructors (~20: FxSystemList_DestroyAll, FxEmitters_Destroy, FxPools_Destroy, FxEffectDef_Release,
   XModel_FreeAnimStates, *_DeletingDtor, nglFreePalette, nglSkipList_Remove*): forced-call harness feature (fork at a
   quiet point, call e.g. CG_Shutdown on live state) - the campaign reboots instead of shutting down.
5. Boot-time inconclusive (8: nglInit, nglInitShaders, nglSelectVideoMode, nglInitFrameBuffers, nglInitMultisample,
   nglResetDevice, nglCreateSysFont, Anim_InitManagers): kernel/D3D stand-ins in test processes (step 3 at the top).
Also open: the -O2-only nglRenderSceneTree drift (day 22c below).

**Earlier (decomp, 2026-10-25, day 22b): every named renderer function has C, matched 1959 -> 2035.**
Two subagents (worktrees cod_bro/wt/fx, wt/rest; shared brief cod_bro/wt/decomp_brief.md, fresh agent per batch)
decompiled the 97 named renderer functions in 6 batches: FX system/components/domains/memory/command queue/draw ops
(fx*.cpp), ngl vertex writers, pass walkers, debug-draw queue/flush, ocean wave sets, XModel_TraceParts,
R_SampleLightGridStatic, XModel_CalcBoneChildrenOrtho, DObj_GetRootAnimDelta and others. Harness-found fixes:
FxSystem_AllocArray (a counted-down cdecl argument wrote its compared slot at -O0), R_AddSimpleInstancesToScene
(rebuilds the original's frame for cdSimpleInstance_AddListToScene's partly written block). Offline leaf list +42,
all matched. Renderer left: 141 untested (debug paths, unload, script-set ocean values, FxOp31_Draw: menu/played run),
8 inconclusive (boot-time host calls), 6 bare-ret stubs that cannot be hooked. Ghidra applied: renames
nglMeshSection_CreateVertexWriter -> nglPosMaterial_CreateVertexWriter, FxSystem_ResetView -> FxView_Reset,
FxOp_SetTextures -> FxOp_SetInstances, XModel_GetTransformedBounds -> DObj_GetTransformedBounds; DebugDraw_MakeTriangle
and 12 globals/vtables (manual_names.csv). 0x0024f840 (SV_ClearConfigstrings10_1F, uncalled) is a gap function:
named in manual_names.csv only, not created (memory "gap functions deferred"). Next: client module (cl.cpp, 590 left)
or a user-steered run for the untested renderer functions.

**Earlier (decomp, 2026-10-25, day 22): renderer mismatch/inconclusive review, matched 1952 -> 1953.**
The 4 renderer mismatches re-run with `--only` (build/compare/mm4, missions 1/4/5/6): all are the recorded
stale-stack causes (nglRenderSceneTree/TranslucentList: an uninitialized shader-constant lane, words 21 and 52 -
dead stack: the original's 0x0007553c / old values vs a host return address in our library; R_RenderView: unset
mesh-record matrix lanes; DObj_UpdateSkeletons: the unwritten quaternion w byte, ≡ 0xc mod 16). No C fix; they
stay on record. Fixed: nglReadBackBufferSize dereferenced the 0x00902d84 slot once too often (the GetDesc stand-in
refused the bogus surface: 255/255 inconclusive) - now 2/2 matched (one test per boot). The other 8 inconclusive
renderer functions are boot-time and blocked in the original's host calls (PersistDisplay/Reset, device creation,
XGetVideoFlags' EEPROM read, XAPI heap page commits, swizzle) - they need kernel/D3D stand-ins (item 3).
Decision (user, 2026-10-25): stale-stack mismatches are accepted (tools/data/accepted.csv, progress.py state
`accepted`, docs/comparison-harness.md step 5): the 4 above plus NAL_AnimTrack_Sample / AnimTree_CalcPose_r (same
w word) - matched 1953 -> 1959 by reclassification, no new C. Still mismatched, not checked yet: SV_UserMove and
SV_PacketEvent (a .data byte at 0x00929b40, b0 vs 70 - likely a real bug), CG_ClipMoveToEntities and the SV_ trace
family (SV_Trace, SV_CapsuleTrace, SV_SectorSightTrace_r, SV_PointTraceToEntities_r, SV_ClipMoveToEntities_r,
SV_TraceStaticModels_r: stack words, look stale but unverified). Next: renderer step 2 (the 225 left).

**Earlier (decomp, 2026-10-25, day 21c): naming pass over .text finished, named 10393 -> 11498/16517 · matched 1952/16517.**
Fresh ExportNaming first, then read-only subagents (one fresh agent per ~100-function list, 2 at a time) read
every remaining unnamed .text function outside library code: 208 call trees (unnamed 1-caller chains under an
unnamed root), 537 zero-caller functions (vtable/table/script-slot targets) and 61 functions whose callers got
names during the pass. Correction: NAL_PackedPosOri_* is NAL_Packed16EntropyIKSpin_* (channel descriptor table
0x008bb130), renamed in Ghidra, sources and CSVs (build + quick ctest pass; the long leaf test was not rerun).
Findings per list: docs/naming.md, "Tree pass". Left: ~700 .text functions read and skipped as needing their
module's decompile, ~2,320 level-section functions (deferred). Next: decompiling - renderer remainder, then
client/UI (memory "renderer next").

**Earlier (decomp, 2026-10-25, day 21b): naming push with subagents, named 9549 -> 10393.** Naming files
synced first (day 20f rows applied, ParamList_* fixed, full ExportNaming redone). Then read-only subagents (shared
brief; each writes its own CSV; the coordinator merges, applies in Ghidra, sorts modules, smoke-tests, commits per
batch) read every unnamed .text hub (3+ callers), every aligned 2-caller function and every 1-caller function whose
caller is named; the front-end UI classes and the HUD component classes were named from their vtables. Details and
struct findings: docs/naming.md, "Hub pass 3". Next: the ~560 1-caller functions whose caller is unnamed and the
~720 0-caller (pointer/vtable/script) functions; then decompiling (renderer remainder, client/UI). Re-export
build/naming (ExportNaming, 3 chunks) before the next naming batch - it predates today's ~850 names.

**Earlier (decomp, 2026-10-25, day 21): offline-test harness fixed; next is hub naming.** Two worker-heap
corruptions found and fixed in tests/leaf_test.cpp (details in docs/comparison-harness.md, "Offline tests"):
wide-text arena words were never filtered against host memory (NAL_Float1_CopyState stored into malloc's chunks;
off the pending list now), and computed pointers can reach the brk heap (NAL_Quat_BeginBlock's loop overwrites its
own pointer cell) - the brk heap is read-only during every call, handlers write only static state, a page below
each host range counts as host. D3D log stubs (item 4): 40 setters/draws stubbed, but most freed functions store
D3D shadow state (blocked as "writes SDK state"), net +3: 497 eligible. Items 5-6 dropped: a simulation with
XAPI and CRT-allocator stand-ins frees 1 of the 134 blocked (their trees reach CRT locks, SEH, _getptd, file I/O).
Debug aid worth reusing: copy [heap] before each untraced run, compare after, print the input (not committed).
Full run after the fixes: 497 functions, exit 0, no worker deaths, no mismatches; counter 1954 -> 1952 because offline
coverage varies run to run around the 90% bar (10 functions lost "offline", 8 gained + nglSetPaletteCached). Open:
LEAF_CSV should keep each function's best clean row (most coverage, no mismatch) instead of the last run's; check
Cmd_ArgsFrom (96% -> 36% coverage, larger than the usual spread).
Next: hub naming (naming.md, memory "hubs, then decompile"); apply day 20f's manual_names.csv rows in Ghidra first.

**Earlier (decomp, 2026-10-24, day 20e/f): matched 1911 -> 1954.** Pending entries fixed; CG_Draw2D's whole tree
(cgame HUD, weapon reticle) and client batch 2 (cl.cpp commands, fe.cpp front end) decompiled; offline tests 494
eligible. Next: the bullet "**Next:**" below; front-end functions (fe.cpp, 15 untested) want a user-steered menu run.

**Earlier (decomp, 2026-10-24, day 20d): offline tests widened 298 -> 436 functions; matched 1830 -> 1911.**
Full run about 13 min (765 s, ctest timeout 1800 s). Details: docs/comparison-harness.md, "Offline tests".
- Inputs: fault-learned pointer fields, a mutation corpus of quick non-crashing inputs (arguments stay
  distinct chunks), per-function budget (15 s once 1000 inputs ran), 10 ms timeout untraced.
- leafscan: stand-ins (Z_Malloc, XPhysicalAlloc/Free, _aligned_*, _vsprintf/_sprintf/__vsnprintf),
  pinned free hook 0x009098ec, int3 allowed, reachable code only, calls through constant tables and
  fixed vtable slots, eh_vector_constructor_iterator's pushed constructor, depth 12, kFunctions names.
- Robustness: arena, call stack and stand-in heap each inside a 32 MB no-access reservation (wild writes
  corrupted ld.so/libc/malloc - 83 functions lost in one run); workers non-dumpable (no coredump
  notifications); a dead worker is replaced at once.
- Fixed in the C: ParamSet_TestCondition (strings read *value only for ops 0/9), R_DrawTextWithShadow and
  ngl target-size conversions on the x87 (new platform_x87_int_to_float / _mul_ftol; drift on the ngl
  projection family 10% -> 0), skip-list remove's level loop is a do-while, NAL CopyState/CopyChannels
  copy front to back (new platform_rep_movs; glibc memcpy stores its tail first).
- Test: a difference of close floats mixed with NaN pairs counts as drift, not a mismatch.
- Done after (day 20e): the pending entries. Fixed in the C: nglBuildBoneMatrixPalette re-reads the count and the
  bone list each iteration (out overlapping section crashed ours), NAL_RleReader_ReadBytes stores the pointer
  after each byte, NAL_Entropy_SkipStatic/NAL_Packed8Float1_SkipStatic return the stream pointer read before
  their stores. NAL_EntropyMode19's 2 rare mismatches did not come back in 4 runs (~10k inputs); taken off the
  list (watch the full run). Vehicle_Controller stays (explained: title callee's uninitialized 4th lane).
- cgame HUD (day 20e): CG_Draw2D and its tree in cgame.cpp - CG_DrawHudElem, CG_DrawHudElemText with
  HudElem_SubstituteLabel / FormatTimer / FormatTenthsTimer / GetWidth / GetHeight, Loc_CountChars, CG_ScreenFade,
  CG_DrawFrontBufferBlend, CG_DrawStatusLabel, CG_DrawViewDirIcon, CG_UpdateWhiteFlashColor(2); CG_FillRect (C existed)
  registered and called by our C. All matched in missions 1/4/5/6; the timers and CG_FillRect start with a call
  (unhookable) and Loc_CountChars/SubstituteLabel are not reached in game - matched offline (Loc_CountChars at 32%
  coverage: the multi-byte path needs 0x0093e750 and a language record). Left in CG_Draw2D: 0x00285260 (2.2 KB weapon
  hud/crosshair) with its callees 0x00282620 (1.7 KB) and 0x002b4d80 (0.5 KB, also FireWeapon's) - done in day 20f (CG_DrawWeaponReticle, CG_DrawAdsOverlay, BG_GetWeaponSpread). CG_Draw2D's one
  mismatch (CG_DrawStatusLabel's second colour: alpha 0.7, not blue) fixed and its row reset.
- HudElem setters (day 20e): HudElem_ClearAll, HudElem_SetDefaults (matched offline), HudElem_SetTimerKind and
  HudElem_SetShaderTimer (not reached in missions 1/4/5/6: no scripted timer there; offline blocked by the CRT's FS
  read in __ceil_default/_ftol paths). New platform_x87_mul_double / platform_x87_int_mul_double (FSTP QWORD).
- Offline tests widened (day 20e, subagent): the blocked 128 (was 168: items 1-3 done - allocator object
  0x008bb760's vtable slots followed, hooks 0x009098e8/0x009098dc pinned, Swap_Init byte-order presets, cgame import
  slots as log stubs; 476 eligible, all pass; FxStage/EmitterTable_Resize compute their loop end once, as the
  original). A full run with LEAF_JOBS=2 under load ran past ctest's 1800 s timeout: run the binary directly then.
- **Next (done day 21):** NAL_Float1_CopyState in leaf_pending.csv (worker heap corrupted by a rare input since the merge, layout-dependent); then the ranked plan for the blocked 144 (494 eligible after the merge): 4. log stubs for D3D setters our C calls by title address,
  ~14; 5. fake vtables on argument objects (stub page; callee-pop byte counts), ~32; 6. callback
  arguments. 29 call trees reach callees outside any section (misdisassembly in some symbol ranges?). CG_ParseConfigStringFloat9 tests nothing (its slot returns on the x87, the stub pushes none); R_DrawPic matches only because the stub leaves its out-parameters unwritten.

**Earlier (decomp, 2026-10-24, day 20f, client subagent; branch client-b2): CG_Draw2D's last part, UIText.** Named
(manual_names.csv, not applied in Ghidra yet): 0x00285260 CG_DrawWeaponReticle, 0x00282620 CG_DrawAdsOverlay,
0x002b4d80 BG_GetWeaponSpread (bg_misc.cpp; FireWeapon's too). CG_Draw2D now calls the C. All three settled in
missions 1/4/5/6 (`--only --every 4`: 4591 / 1303 / 1262 matched), BG_GetWeaponSpread also offline (1663, 61%
coverage; leafscan emits only untested rows, so it was added to the table for that run alone). Not checked
separately: CG_DrawAdsOverlay's scope picture and reticle modes 2-4 (only reached while aiming down a scoped
sight; level runs do not aim). CG_DrawAdsOverlay keeps x87 for cx - w * 0.5, cy - h * 0.5, w * 0.75 and cy - 1 (no
MXCSR flags); BG_GetWeaponSpread's entry saves XMM2-7 (FireWeapon keeps XMM2 across the call). fe.cpp:
UIText_Init (settled, 195) and UIText_Construct (25 matched; its caller's ECX/EDX low bytes are UIText_Init's
flag arguments, and nglGetTextExtent's height lands in an argument slot holding -1).

**Earlier (decomp, 2026-10-24, day 20, client subagent; branch client-b2): client batch 2, 32 functions.** cl.cpp:
console commands (CL_Setenv_f, CL_ShellExecute_URL_f, CL_ForwardToServer_f, Key_Bindlist_f, Key_Unbind/Unbindall_f,
CL_Dec/IncAnimWeight_f), CL_ShutdownInput, SCR_RedrawFrontBuffer, CL_ExecControllerConfig. New fe.cpp (front end):
the FE_Bonus* labels/texts, FE_FormatPlayTime(2), FE_CommandDisplayName, FE_SaveSlotsUpdateText, FE_AspectOptionLabel,
FE_SetDifficultyPicture, UIMenu_Init/FreeItems, Panel_FindText/FindPicture, Profile_SetDefaults, UI_PlayTypeSound,
PauseMenu_ReloadCheckpoint. Matched in game (missions 1/4/5/6): Key_Unbind_f, Panel_FindText, Panel_FindPicture,
Profile_SetDefaults, UIMenu_FreeItems. Offline (leaf_test, all matched): the cl.cpp commands except
SCR_RedrawFrontBuffer/CL_ExecControllerConfig, FE_FormatPlayTime(2), FE_CommandDisplayName, Profile_SetDefaults
(CL_IncAnimWeight_f needs LEAF_ITERS=40000: random cvar pointers crash most inputs). Not tested (front-end only, vtable
calls block the offline test): the other 15. Skipped: ret-only stubs (CL_Disconnect_f, IN_SprintUp/Down), the
FE_Bonus*FreeImage functions (CALL in the first 5 bytes), CL_ResumeFromPause (tail-jumps through a vtable),
Panel_GetDefaultPicture. FE_BonusVehicle/WeaponSetText: 0x00312e30 is stdcall (ret 8), so the counts are words 10-15 /
0-9 of its buffer. Next: the user-steered menu run for the front-end functions.

**Earlier (decomp, 2026-10-24, day 20c): the offline test passes on all 298 functions; matched 1826 -> 1830.**
`tools/data/leaf_pending.csv` is empty: the 9 pending functions and 4 rare-input finds are fixed or explained.
`tools/leafscan.py` now also takes decompiled gap functions (no symbol, state `unsorted`): 298 eligible.
- Fixed in the C: apsActionPlaneBounce_Apply (the unit normal's 4th lane stays 0, not 0 * inverse: a NaN there
  hid DE flags), apsActionFrenetMove_Apply (curvature and torsion re-read before their clamps),
  NAL_EntropyFloat3_InitReaders and NAL_PosOri/Quat_BlendWeighted (read through the stream pointer before
  advancing it), NAL_Skeleton_BoneTransform (out[6] is z: the second `[esp+0x24]` follows a POP; the
  "title stores y twice" comment was wrong), the NAL entropy Decoder (now the title's register model: buffered
  words, masked shifts, unsigned bit count; Mode3D's own 32-bit reader), inflate_fast (the title's inflate_mask
  table at 0x008a1d80, not a 17-entry copy), cgame `g()` returns `const float&` (a float returned by value
  goes through x87 and quiets a signaling NaN).
- Test: unaligned NaNs count as NaN differences; a mismatch with equal registers reruns with an arena write log
  and counts as nan when the first divergent store is NaN on both sides (aps particle overlaps cascaded NaN
  payloads); `LEAF_DUMP` / `LEAF_REPLAY` (+ `LEAF_REPLAY_STACK`, under `setarch -R`) replay one input.
  See docs/comparison-harness.md, "Offline tests".
- **Next:** inputs-shape hints for functions with few inputs (apsActionPlaneBounce 30, apsOrientToVelocity ~90,
  the NAL entropy modes ~10 per 4000 - all reader/pointer chains), then the 306 blocked (indirect calls:
  stand-ins for Z_Malloc/XPhysicalAlloc, vtables), then naming.

**Earlier (decomp, 2026-10-24, day 20b): offline tests widened to every module; matched 1707 -> 1826.**
`tools/leafscan.py` now defaults to every module and keeps offline functions in the table (re-checked
each run): 219 eligible (renderer 56, xanim 48, qcommon 47, game 34, client 23, cgame 5, others), 211 in
`tests/leaf_functions.inc` (ctest leaf_test, about 10 min; timeout 1800 s). Test changes: constants in
writable sections stay at the image's value (leafscan's `constant()`), an x87-result function returning
with an empty x87 stack is skipped like a crash, our C gets a 2 s rerun when only it timed out (-O0),
offline.csv no longer keeps its header as a row.
- Fixed in the C (offline mismatches): hurt_use and Vehicle_FreeDefs are void (EAX was lahf/Z_Free
  leftovers; callers ignore it), CG_BobCycleVertical/Lateral return the x87 value unrounded (and
  CG_CalcWeaponBob compares it unrounded, as FCOMIP does), Phys_FindBodyForEntity walks to
  base + count * 4 (count mod 2^30), ParamSet_TestCondition reads *value only for handled operators,
  ngl's STL sort counts nodes as bytes SAR 3 (`node_count`: GCC folded `last - first > 1` into
  `bytes > 8`, one sort step too many on a 12-byte range).
- "0 of 0" in the progress lines = the original crashed on every random input (inflate_fast,
  NAL_IK_TwoBoneAngles, Actor_GrenadeTargetClear...): they need input shapes, not counted.
  Then the 304 blocked (mostly indirect calls: stand-ins for Z_Malloc/XPhysicalAlloc, vtables), then naming.

**Earlier (decomp, 2026-10-24, day 20): offline tests of the functions the campaign never runs.**
New `tests/leaf_test.cpp` (ctest leaf_test, about 3.5 min) maps default.xbe at its addresses and runs
the original's bytes and our C on random inputs (registers, XMM, stack, the globals read, a 32 KB
arena of pointer chains), compared as in the game; `tools/leafscan.py --emit tests/leaf_functions.inc`
picks what can run offline (no indirect calls, kernel/D3D/DSound/XOnline/XNet/XPP calls or
privileged code in the call tree). Details: docs/comparison-harness.md, "Offline tests". Renderer:
56 of its 208 open functions are eligible (the rest: Z_Malloc -> XPhysicalAlloc, vtable calls, D3D,
kernel). New state **offline** (user, 2026-10-24: counts in the matched counter): 100+ inputs
matched, none mismatched, 90%+ coverage - `LEAF_CSV=tools/data/offline.csv build/linux-loader/bin/leaf_test`
records, progress.py reads it.
- Found and fixed (all only reachable with values the game never has, but the C is now exact):
  nglEvalPointLight squares w too (packed MULPS: its overflow flag), nglNode_ViewDepth returns the
  x87 sum unrounded (long double: overflow happens at the caller's FSTP), ResourceManager_Destruct
  searches by slot address (wraps like the pointer), R_SetInstanceColor re-reads *params.
  nglSelectMeshLodWithMatrix's kFunctions arg_bytes 0 -> 8.
- Test-side lessons (each a false mismatch first): argument slots are callee scratch; arguments must
  be distinct objects (self == src); pointers stay near a 1 KB chunk's start; random words must not
  point into host memory (-inf = 0xff800000 lands in the main stack's growth gap); a pointer computed
  from random fields can hit the call stack (rerun the original with the stack moved: unstable); a
  worker can die from a wild write into its own heap (functions rerun in fresh workers).
- NaN-only differences (SSE keeps the first operand's NaN, GCC commutes; signaling NaNs quieted)
  are reported in their own column, not failures.
- Not offline yet (too few inputs reach them, or coverage under 90%): cdDebug_GetSortInfo,
  nglTextureGetPixel, nglSelectMeshProjectors, nglPaletteSetEntry, FxEffectDef_SetElementValue,
  FxPool_SetSlotTag, FX_FindEvictableEffect, R_SetInstanceColor, XModel_CalcBoneChildren,
  ResourceManager_Destruct - shape hints per function (e.g. the pool/registry layout) would get them.
Next: (1) widen leafscan to other modules (`--module cgame,game,qcommon,...` - same test), (2) the
renderer's remaining ~150 open: forced-call harness feature for shutdown/destructor paths, stand-ins
for XPhysicalAlloc (Z_Malloc -> the 25 VertexIter_Clone), check the shader vtable-slot callers in
Ghidra, (3) then back to naming.

**Earlier (decomp, 2026-10-24, day 19): matched 1578 -> 1628; the renderer's 41 functions without C are down to 1
(and then 0).** Main session + two subagents
(branches day18-debugdraw, day18-fxcmd, both merged; agents resumed on new batches).
- Harness: (1) the trampoline relocates a CALL (`push <original return address>; jmp target`, so the callee returns into
  the original code with the original's return address on the stack) or JMP in the first 5 bytes (loader/compare_linux.cpp
  relative_transfer) - all 23 shader-class Init functions (`push 0xc; call Z_Malloc`) and cdSimpleInstance_FreeList
  matched at once; the C-only functions left out of kFunctions for that reason are being registered (debugdraw agent).
  `push imm8; call [mem]` (FF /2) stays unhookable. (2) RtlEnterCriticalSection/RtlLeaveCriticalSection are recorded
  calls (the XAPI heap lock under RtlAllocateHeap: CRT malloc, XGAssembleShader); heap growth
  (RtlpFindAndCommitPages -> NtAllocateVirtualMemory) still ends tests inconclusive.
- r_init.cpp (main): GetRefAPI, ResourceManager_Construct/Destruct (registry 0x009719f8, 20 slots), SceneFX_Init,
  PakRender_Shutdown, nglAssemblePixelShader/VertexShader (XGAssembleShader stores stack addresses in 0x0095f734 and
  0x00419560: gap entry, vsprintf/sprintf/XGAssembleShader at the original's ESP, text at the original's frame offsets -
  matched), nglAllocFrameMemory (= five gated nglSetFrameMemorySize calls), nglInit (reached at boot, inconclusive:
  D3D device creation), nglCreateNormalCubeMap and nglDumpCamera/nglDumpSceneMeshes (no callers: C only).
  compiler.h: platform_mxcsr_or.
- ngl_debug.cpp (agent): nglDrawFpsCounter, R_DrawDObjSkeleton, DebugDraw_AddLine settled; nglDebugMesh_AddBox,
  nglInitDebugMeshes, DebugDraw_AddString matched. Unreachable: nglDrawFrameTimeBar (nothing writes its switch
  0x00902c01), nglDebugDrawLine/Axes and nglDebugLineBatch_Draw (callers 0x0006d200/0x0006d220/0x0006c050 uncalled).
  nglDrawFpsCounter's format has one %d more than arguments (prints a never-written stack word; C passes 0).
- fx_cmd.cpp (agent): FXCmd_* builders, FXCmd_Alloc/Commit, FXParam_Broadcast, FxSpawnCmd_Copy (0x00024520, was
  FUN_), FxInstance_Init (0x00296d80) - 15 matched, none mismatched; not reached: FxSpawnCmd_Copy (64-slot queue full),
  FXCmd_Type6AtTransform, FXCmd_PlayWeaponSoundLoop, FXCmd_PlayWeaponSoundAtBoneEx, FXCmd_Type2AtTransform. Builders run
  FXCmd_Alloc/Commit at the original's ESP (Alloc can call the depth-sensitive FX_FlushSpawnQueue). Found: snd.cpp
  play_alias calls FXCmd_PlaySoundAlias without ECX = owner entity (fix in progress, FX agent). 0x002a5db0 is an uncreated,
  uncalled Type8-at-transform variant.
- --exec runs at ~8 s while the mission still loads: vid_restart / map_restart / disconnect there reached nothing;
  exec_sweep groups now prefix `wait 2400`. Level changes reboot (re-exec) the loader, so shutdown paths need an
  in-process trigger.
- Second agent batches (merged): the C-only unhookable functions are registered - SndVoice_ApplyParams,
  S_ClearVoiceAttenuation, Snd_UpdateListenerAxis, Snd_UpdateTime, Actor_PushState, Actor_AnimStationary,
  Snd_CountBusyVoices, SV_UpdateInfoConfigstrings settled; Scr_Acos/Scr_Asin/Scr_GetEntObjectOrEmpty got C (not
  reached); FUN_00068f70 = nglBuildBoneMatrixPalette (C, no callers). snd.cpp play_alias passes ECX = owner entity
  (S_UpdateAmbientEmitters 9408/0). FX paths: FxOp_SetTextures, FxOp1d5_AddToList settled; the FX shutdown/free paths
  (FxSystemList_DestroyAll, FxPools_*, FxEmitters_*, FxEffectPool_Release, ...) need a run that ends the level or quits
  to the menu (`disconnect` via --exec is a no-op on the Xbox build: the mission plays on); 11 of 12 FxAction*_Init use
  particle action tags no level pack contains; the 5b/d5/dd render ops are never created; FxSystem_Stop / FxNull_Query
  stay unhookable (5-byte functions), FxNop_* are bare rets.
- Renderer now: 969 functions, all with C; 743 matched, 214 untested, 7 inconclusive, 4 on-record mismatches
  (nglRenderSceneTree, nglRenderTranslucentList, R_RenderView, DObj_UpdateSkeletons: stale-stack words).
- User-steered quit-to-menu run (build/compare/quit_user, 390 s, 348 untested functions of renderer/cgame/client/
  server/qcommon/sound/streaming, --only --every 1): quitting to the menu and starting a mission both REBOOT the title
  (HalReturnToFirmware -> the loader re-execs), so the in-process shutdown paths (SV_Shutdown, CG_Shutdown,
  FxSystemList_DestroyAll, PakRender_Shutdown, ...) never run in the campaign. Reached, all matched (+9):
  FXCmd_PlayWeaponSoundAtBoneEx, FxOp31_AddToList, SCR_UpdateLoadingDots settled; FxLightSet_SelectObjectLights,
  FxNode_ComputeObjectLights, FxMatrix_RigidInverse, apsPtrVector_Reserve, Com_BitCheck matching; Anim_InitManagers
  inconclusive (FUN_00093a10 file read). Testing the shutdown paths would need a test entry the title never takes
  (e.g. a forced call from a loader hook at a quiet point) - not done.
- User-steered pause-menu "Restart level" run (build/compare/restart_user, 180 s, 339 untested functions): no reboot -
  the restart stays in process but is a soft restart (like map_restart): only CG_ParseServerinfo and
  CG_ParseConfigStringFloat11 ran (+2 matched); no renderer/FX/server shutdown or reload path. So no campaign action runs
  those paths: they need forced test calls (harness feature, planned) - see the plan for the renderer's 208 below.
- Plan for the renderer's 208 (195 untested, 9 inconclusive, 4 mismatched; user asked 2026-10-24): ~60 reachable by
  more running (rare gameplay 22, other levels 15, UI/text/screenshot 16, load-time 7); ~130 never run in the shipped
  campaign (no callers 35, shader vertex-iterator vtable slots 39 - check for slot callers, shutdown/destructors 24, FX
  data no level has 20, stubs under 5 bytes 15). Tools: offline tests of pure leaves (XBE bytes in mmap vs C, like
  tests/nal_entropy_test.cpp), a forced-call harness feature (fork a test process at a quiet point and call the function
  there on live state; the game itself never calls it), stand-ins for XGetVideoFlags / heap growth / D3D device
  creation+reset / file reads, and a rerun of the 4 mismatched callers with their callees switched to our C.
- Renderer Replaced before naming is finished: no (user, 2026-10-24) - its whole structure must be known before modern
  features go in.
Next: the renderer's untested list (shader NewVertexIter/Clone: vtable slots not reached in
missions 1/4 - check which missions use each shader class; FX paths: FX agent; text-with-icons: --press hint prompts);
then back to naming (renderer stays Compare until naming is done).

**Earlier (decomp, 2026-10-23, day 18): matched 1545 -> 1578.** Day 17's names are applied in Ghidra (85;
ApplyNames now leaves rows without a function uncreated unless given `create`; gap functions stay uncreated). Loader
`--exec "cmd; cmd"`: Cbuf_AddText on the first frame cls.state (0x0092bdf0) is 2, after forcing sv_cheats 1 (Cvar_Set2
refuses cheat cvars, flag 0x200, otherwise); first boot only. The renderer's real debug switches are the game's
command table g_devCommands (0x008ddae0: nglstats, nglfpsdisplay, wireframe, fullbright, solidcolor, toggleshader
<cdname>, texturemip, texturesize, texturetiling, fogswitch, entity_stats, memreport, mempools, plus give/god/noclip);
r_fullbright, r_LightScale, r_showtris and ~30 other r_ cvars are registered in R_Register but never read on the Xbox.
`tools/exec_sweep.py` runs one `--compare --only --every 1` pass per command group over the module's untested
decompiled functions (commands 30 frames apart with `wait 30`: the harness has 4 test slots, and 22 toggles in one
frame got 2 tests). Reached and matched: all 22 cd*_Toggle, nglToggleShaderByName, R_SetDebugViewMode,
R_SetTextureMip, FxEmitters_Init, FxSystems_New, FxSimpleShader_GetName, cdWorld_InitPixelShaders2,
cdWorldBlend_InitVertexShaders2. Reports: build/exec_sweep/day18{,b}/report.txt.
Next:
0. Renderer: 174 decompiled functions still untested - ~30 shutdown/free paths (end or change a level: a test that
   quits to the menu or loads a second mission), ~39 init/load-time (run before the harness is active? check when
   hooks go in), 23 FX paths, the text-with-icons functions (hint prompts / menus: --press), the rest per function.
   The debug-draw functions (nglDrawFpsCounter, nglDrawFrameTimeBar, R_DrawDObjSkeleton, DebugDraw_AddLine) have no C
   yet: decompile them, then `exec_sweep.py --groups stats,debugdraw,rcvars`. Handler names for g_devCommands are
   in manual_names.csv (apply next naming batch). Open question to the user: let the renderer become Replaced before
   all naming is done (exception to the rule)?
   Renderer's 266 untested = 174 hookable not reached + 50 the harness cannot hook ("cannot move the instruction at
   ...; not tested" at startup: the cd*_Init shader setups, nglShutdown, nglResetDevice, nglSaveScreenshot, buffer/
   texture/palette unlocks, ...; no run will test them - needs a test entry past the first instruction or a test
   through the caller) + 42 without C. Main-menu run steered by the user (build/compare/menu_user, 393 s, every menu
   option): only +2 matched (ScriptString_Find, CL_PauseForDialog, Cmd_Argv, SV_SetSkillMedium, IN_UpUp,
   IN_ActivateUp) - the UI screen classes (~0x31xxxx-0x33xxxx) are not decompiled yet; repeat that steered run once
   the client/UI module has C (the user offered to steer).
1. Harness: record the DirectSound calls (audio::dsound_functions(), like kRecordedD3dCalls; GetStatus needs a stand-in
   reading the replacement's buffer state). That makes CL_Frame, the sound update 0x000c3700 (SndVoice_ApplyParams,
   Snd_UpdateListenerAxis are C but untested), S_StopAllSounds, CL_ClearState and CL_MapLoading testable.
2. Game: Water_GetHeight (0x00044ee0, 1.2 KB packed SSE), BG_FindItem (0x002b6a70), PathNode_First/Next, further
   `rank.py game 1` rows.
3. Collision/physics leftovers: 0x0020da90, the 0x00231770 physics siblings, 0x002635c0; game rank.py rows.
4. On record (dead-stack words only): SV_SectorSightTrace_r, SV_ClipMoveToEntities_r, SV_CapsuleTrace, SV_Trace,
   the point/box sector walkers.

**Earlier (decomp, 2026-10-23, day 17): matched 1419 -> 1545 (main session + two subagents, all merged).** Main
session: CL_Frame and its per-frame callees, Actor_PopBlockingStates (state in ECX), Sentient/Actor goal-node
functions, sound and streaming; subagents: collision sight traces, the server module, sound update leaves.

**Earlier (decomp, 2026-10-23, day 17, main session): CL_Frame and its per-frame callees, sound and streaming.**
- CL_Frame (0x002f2630) is C: gap entry (C frame 0x20000 below), every callee at the original's ESP with the
  original's registers (`call_at`), the original's saved EBX/ESI and the float time written into the dead frame; the
  screen update calls whatever the call at 0x002f27a4 targets (the loader's --level hook). Inconclusive only: the
  per-frame sound update 0x000c3700 -> 0x000c7730 calls the DirectSound replacement (19 setters such as
  IDirectSound_SetPosition/SynchPlayback, IDirectSoundBuffer_SetVolume/Play/StopEx, DirectSoundDoWork, and
  IDirectSoundBuffer_GetStatus, a getter that would need a stand-in reading the replacement's buffer state). Recording
  those (audio::dsound_functions(), like kRecordedD3dCalls) would make CL_Frame, S_StopAllChannels, S_StopAllSounds and
  the whole sound update testable.
- Harness: KeQuerySystemTime is a recorded kernel call (SdkStandIn::SystemTime: zero time on both sides), so the CRT
  _clock path (Snd_UpdateTime 0x000cb200, file request timers) no longer ends tests.
- Loader: the --level thunk restores the original `call 0x002f1e60` at 0x002f27a4 once the mission has started (tests
  of CL_Frame reached host code on every frame before).
- Matched (most settled): streaming view position (Stream_SetViewPosition, Stream_UpdateZoneDistances,
  Stream_SetZoneDistancesFromMask, Stream_PickVolumePoint, Stream_NearestPoint, Stream_PointInAnyBox,
  Stream_BoundsContainPoint, Zone_SetDistance_r, Pak_ResetZoneDistances), S_UpdateAmbientEmitters (jump table states
  0-4 at 0x002ea80c), S_UpdateCrossfade, SoundHandle_Resolve/Free, the voice pool (HandlePool_First/Next/Resolve,
  SndVoice_ForEach/ClearAttenuation/GetState/SetParam/SetOrigin/Resolve), S_StopChannel, S_StopSoundHandle,
  S_FindSoundByHash (the argument is a name pointer), SndGroup_FindByName, SndSource_CommitDirty, SndHw_ReleaseSlot,
  S_SetMasterAttenuation, Snd_GetAliasName, Zone_AddRef_r/Release_r/FindUnloadedChild/FindByHash,
  StreamMem_CountUsed/CountFree/GetBlock, G_EntityList_Compact, Subtitle_Draw.
- Register contracts: StreamMem_Alloc keeps XMM3 across StreamMem_CountFree (the entry saves it).
- C only: S_ClearVoiceAttenuation (0x000c53b0) and Snd_UpdateTime (CALL in the first 5 bytes). Not reached:
  FX_FindEffectById (the ambient replay path).
Next: DirectSound recorded calls (above); the rest of the sound update (0x000c4af0 voice parameter apply, 1.5 KB;
0x000c6080, 0x000c85c0); Zone_UpdatePriority (two-level jump table at 0x0028c8fc/0x0028c904); CL_UpdateRumble and
S_CheckWaveBankNotifies.

**Earlier (decomp, 2026-10-23, day 17, collision subagent; branch day17-collision, merged): matched 1419 -> 1441 on
its branch.** The sight-trace tree is C: CM_SightTrace (0x002e4c00, the root) with CM_ModelBrushesBlockSight,
CM_TempBoxBlocksSight, CM_CapsuleBlocksSight, CM_SphereBlocksSight and CM_VerticalCylinderBlocksSight (x87 nudge
(b * 0.125) / length, kept in asm); CM_TransformedSightTrace calls the C root and is Result::Eax. Also C: the
candidate-list traces under actor/player movement (CM_CandidateCapsuleTrace, CM_TraceThroughCandidates,
CM_TestInCandidates, CM_PushPointOutOfBrush, CM_PushSphereOutOfTriangle, G_PushCapsuleOutOfSolids), CM_RayHitsTriangle,
SV_EntityVisibleFromPoint (Q3's lastCluster off-by-one kept) and the G_SightTrace entity clips (SV_SightClipToEntity,
SV_BoxSightClipToEntity, SV_SectorBoxSightTrace_r, SV_EntitySightTrace); all settled (`--only --every 1`, missions
1/3/4/7). On record: SV_SectorSightTrace_r (0x002e8350) differs only in 1-2 dead-stack words (a value 0xb0 apart),
also with its callees at title addresses. Rarely reached (1-3 tests): CM_TraceRayThroughCandidates,
CM_CandidateRayTrace, G_RayTraceCachedCandidates; not reached: CM_RayHitsWorldMeshes (0x002d27c0). 0x002e80b0 and
0x002e88d0 were done on both branches; the merge kept the server versions.

**Earlier (decomp, 2026-10-23, day 17, server subagent; branch day17-server, merged): matched 1419 -> 1445 on its
branch.** sv.cpp covers entity linking (SV_LinkEntity with SV_LinkToSector, SV_SplitSector, SV_AllocSector; sectors
also hold static models at +0x14), the SV_Trace tree (SV_ClipToEntity, SV_ClipMoveToEntity, SV_PointTraceToEntity (DObj
hit boxes), SV_TraceStaticModels(_r) with SV_TraceStaticModel, SegmentMissesBounds, the box and point sector walkers,
SV_CapsuleTrace, SV_LocationalTrace), the per-frame server (SV_Frame, SV_PacketEvent, SV_ExecuteClientMessage,
SV_UserMove) and the send path (SV_SendMessages, SV_SendClientMessages, SV_BuildClientSnapshot,
SV_AddEntitiesVisibleFromPoint, SV_WriteSnapshotToClient, SV_UpdateServerCommandsToClient), SV_SetConfigstring,
SV_SetBrushModel. SV_PacketEvent, SV_ExecuteClientMessage, SV_UserMove and SV_Frame run their title callees at the
original's ESP (ClientThink stores a stack address at 0x00929b40). On record (mismatch only on trace words the code
never writes, stale stack in the original): the three sector walkers, SV_Trace, SV_CapsuleTrace; the box walker and
SV_CapsuleTrace also differ on fixed words 0x804fff2c and 0x81e23c5c/6c (possibly per-thread data below the Xbox
StackBase, written by title collision code - unconfirmed). SV_UpdateInfoConfigstrings is C only (unhookable).
SV_ClientCommand and SV_LocationalTrace not reached. XMM4 is a register input of SegmentMissesBounds: check the
title's xmodel caller 0x0023d070 before Replaced. Left in the module: load-time/menu functions only (SV_InitGameProgs,
SV_RestartGameProgs, SV_ShutdownServer, SV_SpawnServer, SV_Map_f, SV_ConnectionlessPacket).

**Earlier (decomp, 2026-10-23, day 17, server subagent, sound batch; merged): callees of the per-frame sound update
0x000c3700 (snd.cpp).** Settled: Snd_PopCount64, Snd_GetMixState, Snd_GetGroups, Snd_NormalizeVector (x87 length; ECX
restored), SndHw_AllocSlot, SndBank_UpdateLoads. Matching: SndBank_PollLoad (bank loads through File_QueueRequest),
Snd_InitRequest, SndVoice_ReleaseHwSlot. Not reached: SndBank_Free. C only and untested (CALL in the first 5 bytes, and
their caller 0x000c3700 ends in the DirectSound path): SndVoice_ApplyParams (0x000c4af0, 1.5 KB),
Snd_UpdateListenerAxis (0x000c85c0). Still title code: 0x000c8250, 0x000c8380, 0x000c8de0, 0x000c9db0, 0x000c91e0,
0x000c6fb0, the qsort comparator 0x000c5790.

**Earlier (decomp, 2026-10-23, day 17, server subagent, game batch; merged): ready rows from `rank.py game 1`.**
Settled: PathNode_Construct, Path_BaseName, ActorBlockList_RemoveEntity, PathNode_FromHandle / FromHandle2,
PathNode_IsAvailableFor, PathNode_CanSee, ActorMove_AddTouchEnt, AddLeanToPosition, G_RunThink (the think callback
runs at the original's ESP through a gap entry: a callback stores a stack address at 0x0098a4fc - 39 one-byte
mismatches before, 28545/0 after). Matching: ActorBlockList_Bump. Not reached: Scr_Atan2 (script table only),
PathNode_IsLinkInBadPlace. C only (CALL at byte 3): Actor_AnimStationary.

**Earlier (decomp, 2026-10-23, day 17, collision subagent, batches 2-3; merged): movement collision, game side.**
G_PushAwayFromEntities (0x002d5d50; compares p.x against the entity's x + 72 where a z test would be expected - kept),
G_SeparateFromSolids (0x002e6260, PmoveSingle's separation step), G_FilterCachedCandidates (0x00203f60),
CM_ClipSegmentToBrush (0x002cc0e0), CM_RayHitsListedObjects / CM_PushSphereOutOfListedObjects (lists 0x008fcee0 /
0x008fd3f0 / 0x008fd680), Phys_MoveAttachedPoint / Phys_UndoAttachedPushes (0x0002a340 / 0x0002ab10; the original adds a
never-written stack word to a w lane, the C adds 0 as tests see), CM_TraceSegmentThroughCandidates (0x002d2b50, vehicle
physics), Vec3_PitchDegrees (0x002a11f0: the title's own arcsine series, __CIsqrt by address, XMM0 result) - all
settled. Next near this tree: 0x0020da90 (four corner traces through G_FilterCachedCandidates and SV_CapsuleTrace /
SV_Trace), the 0x00231770 physics siblings (0x0002a620, 0x00229d20 / 0x00227290, 0x0022d7d0, unnamed physics
callees), 0x00228bf0 (only via the never-reached CM_RayHitsWorldMeshes), the turret test 0x002635c0.

**Earlier (decomp, 2026-10-23, day 16 later): client per-frame/input, matched 1402 -> 1419.** CL_KeyEvent
(299 tests, settled), SCR_UpdateScreen (5954 matched, rest inconclusive: _clock / SDK log full), CL_TogglePause (4),
14 of 37 button commands (IN_*Up/Down: lean, reload, activate, button0, speed, UpUp/UpDown) matched with a wider
press script (rt/b/x/y/lt/black/white/ls/rs/dpad/start/back after 100 s; scratchpad press list = default A presses
plus those). C exists, not reached: the keyboard-only movement commands (forward/back/left/right/move*/look*/strafe,
IN_DownUp/Down, IN_CenterView, IN_MLook*), CL_PauseForDialog (UI dialogs). CL_ClearState and CL_MapLoading end
inconclusive only (_clock under the sound stop). SCR_DrawScreenField is a C helper (CALL in its first 5 bytes).
- SCR_UpdateScreen runs its title callees at the original's ESP (asm entry 0x20000 below, title_call_at_regs):
  the first version mismatched 5558 times because title code under CG_DrawActiveFrame stores a stack-local address
  in 0x0095e2d0 (0x00246fca). Row reset to the post-fix counts.
- std::initializer_list in game code: at -O0 its begin() is an out-of-line weak symbol bound to the loader's copy
  ("new called host code" on every test); use plain arrays.
- CL_Frame next: before it becomes Replaced, the loader's --level hook rewrites its call to SCR_UpdateScreen
  (0x002f27a4, loader/patches.cpp kCallClientFrame) - the C must keep that hook. Its pushes are callee-popped
  (S_StopAllChannels etc. are stdcall; check each callee's ret N).

**Earlier (decomp, 2026-10-23, day 16): client module (cl.cpp) started, matched 1364 -> 1402.** Day 15's 224
names applied in Ghidra (symbols exported, smoke test clean). cl.cpp: 46 functions, all matched over missions 1/4/5/6
(most settled): the pad readers (IN_Pad*), key states (CL_KeyState, IN_KeyUp/Down), user commands (CL_CmdButtons,
CL_KeyMove, CL_MouseMove, CL_JoystickMove, CL_AdjustAngles, CL_FinishMove, CL_CreateCmd, CL_CreateNewCommands),
CL_GetSnapshot/GetUserCmd, CL_WritePacket, CL_PacketEvent, CL_ParseServerMessage/ParseSnapshot, CL_GetServerCommand,
CL_ConfigstringModified, CL_SystemInfoChanged, CL_ResetLocalConnection, CL_ShutdownCGame, CL_InitRenderer/InitInput,
IN_InitCvars, CL_RefPrintf, Key_* helpers, CL_DObj*.
- Register contracts the harness does not check (title callers read them; found by scanning each call site's next
  instructions): CL_KeyMove/CL_AdjustAngles keep constants in XMM2-6 across CL_KeyState (its entry saves XMM1-7),
  CL_CreateCmd reads EAX after CL_CmdButtons and EDX after CL_FinishMove, CG_InterpolatePlayerState EDX after
  CL_GetUserCmd, CL_WritePacket EAX after MSG_WriteLong (now Result::Eax).
- C exists, never reached in level runs: CL_CgameSystemCalls (the cgame calls the renderer directly),
  SCR_AdjustFrom640, CL_AddReliableCommand, Char_IsDigit (hex key names only). CL_AdjustAngles' accelerated path
  (cl_bomberKeyboardMode) is probably mission 5's turret; not checked separately.
- Before the client module becomes Replaced: the loader's PC mouse patch rewrites the CL_MouseMove call inside
  CL_CreateCmd (0x002c7ad7, loader/patches.cpp) - move that hook into the C.
- Left in the reached list: the XInput pad functions (IN_PadInit/Create/Update/CheckDeviceChanges/StopAllRumble,
  IN_SetRumbleMotor: XPP calls end inconclusive unless recorded), SCR_InitLoadingSpinner (Sys_Main, before tests),
  FE_LiberatorCreateCrossIcons (ESI-this constructor 0x00317050), CL_InitRef (refimport table + GetRefAPI).
  CL_ScaledMilliseconds starts with a CALL (unhookable).
- Module sorting (later the same day): every named function is now in a module (tools/data/modules.csv: prefix
  rules plus exact-name rules; new modules script, levelscript, sound, streaming, online, crt). Unassigned = only
  the 1607 MS library functions and the 3102 unnamed ones. Level-section functions always stay with their mission
  (progress.py). New names need a rule unless an existing prefix covers them (naming.md workflow step 4).
- Completion rule (user): no module is finished, so none becomes Replaced, while any function is unnamed or
  unsorted (the 3102 FUN_ and the ~3800 uncreated gap functions included).
Next: CL_Frame and the rest of the per-frame client (rank.py client 1), then the collision/server items below;
the XInput recorded calls / stand-ins in the harness if the user wants the pad functions testable.

**Earlier (decomp, 2026-10-23, day 15, closed): renderer + engine (user: "focus on the renderer and
engine"). matched 1116 -> 1364.** All work merged and pushed; the subagent worktrees are removed (cod_bro/wt/
keeps only the `unpacked` symlink). Names of the day's unnamed functions are in manual_names.csv, not applied in
Ghidra yet (apply + ExportSymbols + smoke test next session).
- Mine: qcommon (qcommon.cpp: strings, va/Com_sprintf, command system + Cbuf, Cvar_Get/Set2 and the cvar commands,
  info strings, MSG byte/usercmd readers + writers, NET helpers, COM_ParseExt, memset, aligned free), the
  collision module start (cm.cpp: leafs/areas/PVS, CM_BoxLeafnums(_r), CM_TempBoxModel, CM_PointContents + quad
  trees, the trace start/brush/capsule/sphere/cylinder tests, ClosestPointOnTriangle, Vec4Normalize, the box-tree
  walk stack), the server module start (sv.cpp: SV_UnlinkEntity, SV_AreaEntities(_r), SV_PointContents,
  SV_ClipHandleForEntity, the reliable command buffer and send functions, SV_RunGameFrame (G_RunFrame at the
  original's ESP), SV_DObj*Skeleton, SV_Init/AddOperatorCommands, RadiusFromBounds).
- Subagents: the renderer ready list, the whole effect loader and particle runtime (fx_load.cpp), nglBeginFrame/
  EndFrame (harness: title RDTSC recorded as EDX:EAX = 0), R_ModelBoneMatrices; the collision world trace up to
  CM_BoxTrace / CM_TransformedBoxTrace (paragraphs below).
- Never reached in level runs (C exists): console-only command helpers, FS path helpers, Com_PushEvent,
  NET_AdrToString, COM_ParseExt, area-portal flooding, CM_TraceThroughSphere, the skill setters (menu).
Next:
1. (done day 16) Client module start.
2. Collision: the rest of the sight-trace tree (collision paragraph below), CM_TransformedSightTrace -> Result::Eax.
3. Server: SV_LinkEntity (1.7 KB) after its helpers 0x002ba9d0 and 0x002b1f70 (-> 0x002aae40); SV_ClipToEntity,
   SV_Trace (needs 0x002db3e0, 0x002e84d0, 0x002e88d0).
4. Renderer leftovers: 0x00242730 (1.9 KB SSE/x87, ~1754 calls).

**Earlier (decomp, 2026-10-23, day 15, collision subagent; branch day15-collision, worktree wt/collision): matched
1312 -> 1326.** The world trace under CM_BoxTrace is C, all settled with plain C (no frame rebuilds; every callee
already C), tested with `--only --every 1` over missions 1/3/4/7: CM_ClipSphereSweepToTriangle (0x002dae30),
CM_QuadTreeTraceLeafs_r (0x002c6bf0), CM_TraceRayThroughWorldTrees (0x002d37f0), CM_TraceThroughWorldTrees
(0x002e04e0), CM_TraceThroughWorld (0x002e4980), CM_BoxTrace, CM_TransformedBoxTrace, CM_TransformedSightTrace
(0x002e6dc0), CM_FilterBoxCandidates, CM_GatherBoxCandidates + CM_GatherQuadTreeCandidates_r (0x002c6840), and
from the sight-trace tree CM_SegmentCrossesMesh (0x002d1980), CM_SightTraceRayThroughWorldTrees (0x002d2f90),
CM_SightTraceThroughWorldTrees (0x002e1740). Never-set stack words handed to CM_BoxTrace (the capsule block's
size/w words, rotation row w lanes) are zero-initialized: tests zero the dead stack. Skipped: CM_LoadMapFile (FS
file I/O: tests would end inconclusive).
Left of the sight-trace tree, bottom-up: 0x002d41f0 (model brushes, CM_TraceBoxThroughBrush until a hit), 0x002d3e10
(temp-box capsule setup + box trace), 0x002d3f40 / 0x002d4050 (sight sphere / vertical cylinder, x87 like
CM_TraceThroughSphere, return 0 when blocked), 0x002da1d0 (capsule vs capsule, 0 / -1), then 0x002e4c00 (the sight
trace root: CM_BoxTrace-style setup, returns the callee's EAX - no stack address, so CM_TransformedSightTrace could be
Result::Eax), 0x002e4940 / 0x002da080 (no direct callers). Also open: 0x002e1150 (another CM_ClipSphereSweepToTriangle
caller, via 0x002e20e0).

**Earlier (decomp, 2026-10-22, day 14, closed): matched 935 -> 1116.** All work merged and pushed; the
subagent worktrees are removed (cod_bro/wt/ keeps only the `unpacked` symlink worktree tools need).
- Mine: NAL quaternion decoders and channel-class slots (nal_codec.cpp; most classes are never used in missions 0-7,
  docs/nal_animation.md), anim managers (nal_anim.cpp), AI paths and the actor state machine (actor.cpp: Path_*,
  Actor_ApplyStateTransitions/ThinkStateMachine/CallStateSlot5 with callbacks at the original's ESP, Start/Finish
  callbacks, cover validity, move history, FindNearbyActor ...), G_MissileTrace / Bullet_RandomSpread (g_utils.cpp),
  BG_EvaluateTrajectoryDelta (bg_misc.cpp).
- Subagents: AnimScript_Update (anim_script.cpp), the rest of the NAL anim range, six cgame batches (cg_ents.cpp,
  cgame.cpp, cg_view.cpp), vehicles/turrets (g_vehicle.cpp), entity/script helpers (g_script.cpp).
- EAX rule: tools/eax_check.py lists Result::None entries whose original keeps an EAX input - now none.
- Open: CG_ClipMoveToEntities mismatches only in dead-stack words (callers match); unhookable C-only functions:
  Actor_PushState, the Scr_StartThread_* wrappers (`push imm8; call [mem]`; harness could accept FF /2 with an
  absolute operand), three deleting destructors.
- Names: all 152 of the day's names for existing functions are applied in Ghidra (2026-10-22 23:40, symbols
  exported, smoke test clean); the 19 gap-function names in manual_names.csv stay unapplied (gap functions uncreated).
Next:
1. cgame: CG_CalculateWeaponAngles + its six unnamed callees (0x002782a0, 0x00278400, 0x00278790, 0x002788b0,
   0x002789f0, 0x00282ef0), CG_DrawHudElem/CG_DrawHudElemText, CG_UpdateViewWeaponAnim, CG_CalcVehicleViewAngles,
   CG_ConfigStringModified, CG_ClearState, CG_UpdateShellShockSound.
2. game: Path_TrimAtGoal / Actor_TrimPathAtGoal, Actor_IsAtGoal (callee 0x00201e90), Actor_RegisterSuppression /
   EventBullet / SendLineEvent, Actor_AnimStationary + Grenade_Recover/Cower, Actor_ForgetSentient,
   Sentient_SetGoalNode, Actor_DropClaimedNode, Actor_GatherCoverCandidates, Actor_Exposed_Finish; Scr_Atan2,
   BG_CanItemBeGrabbed, G_InitAnimScripts.
3. Ranking: `tools/rank.py MODULE [MAX_PENDING]` (bottom-up: naming meta callees x progress.csv).

**Earlier (decomp, 2026-10-23, day 15 round 5, renderer subagent): renderer leftovers, client usage probe.**
fx_load.cpp: FxAxis_Frame (0x0009ddb0, 772 B packed SSE, settled), FxMatrix_RigidInverse, FxSystem_InitBounds,
FxSystems_New, FxEmitters_Init (startup, not reached). tr_main.cpp: R_ModelBoxBounds (0x00240f70, matched) and
R_ModelBoneMatrices (0x00241ff0, settled). A probe showed 0x00240930 (skeleton debug draw) never runs; 0x00242730
(1.9 KB SSE/x87, 1754 calls) is left. Client module: not decompiled yet; a usage probe (dummy registrations, `--only`,
missions 1/4/5/6, rows removed afterwards) over the 126 ready rows (rank.py client, 0-1 pending) found these called in
level runs: IN_PadCheckDeviceChanges IN_PadInit IN_PadCreate IN_PadUpdate IN_PadIsConnected IN_PadButtonDown IN_PadConsumeReleased IN_PadButtonPressed IN_PadConsumePressed IN_PadGetStick IN_SetRumbleMotor CL_DObjCreateSkeleton CL_DObjGetBoneMatrix SCR_InitLoadingSpinner CL_RefPrintf Key_KeynumToString CL_FinishMove CL_CmdButtons CL_KeyState IN_KeyUp IN_KeyDown CL_GetSnapshot CL_GetUserCmd Key_StringToKeynum CL_WritePacket CL_MouseMove CL_JoystickMove CL_KeyMove CL_AdjustAngles CL_ShutdownCGame IN_InitCvars CL_ResetLocalConnection CL_SystemInfoChanged CL_ParseSnapshot CL_CreateNewCommands CL_ParseServerMessage CL_InitRenderer Key_ClearStates CL_GetServerCommand CL_InitInput CL_InitRef FE_LiberatorCreateCrossIcons Key_GetKeyNameForCommand. The other ~80 (most FE_* text/menu helpers, CL_*_f commands, Key_Bindlist_f,
CL_ShellExecute_URL_f, CL_Setenv_f ...) never run in --level runs: start cl.cpp with the reached ones.

**Earlier (decomp, 2026-10-23, day 15 round 4, renderer subagent): particle runtime, NAL class check.** fx_load.cpp
gained the particle runtime in 0x0009b000-0x000a0000: pool statistics/slot functions, FxAction_Compact/Destroy,
apsPtrVector_DeleteAll/Reserve, apsLog_Printf, FxEmitters_FreeAll/Destroy, FxEffect_ViewDepth, object light selection
and FxNode_ComputeObjectLights, FxOp_SetupNode + all ten render ops' AddToList, the fade / depth-range shader constants,
FxParticleBuffers_Init, FxCamera_BillboardAxes (settled 7899/7899); ~25 matched, none mismatched. Left there:
0x0009ddb0 (772 bytes packed SSE), 0x0009ff50, 0x0009f1d0/0x0009f250. NAL classes (probe: temporary registrations with a
dummy body, `--only` over missions 0-9, rows removed afterwards - 0 calls each): the four Float4 classes (vtables
0x007c2738/0x007c27a0/0x007c2808/0x007c2870: 0x0008c0c0, 0x0008c4a0, 0x00080140..0x00081840), the classes at 0x007c2a88
(0x000823a0..0x00082580), 0x007c2af0 (0x00085170..0x000853b0, 0x00082680/0x00082750) and 0x007c2b58 (0x00082c30), the
packed PosOri class (0x0008cc40, NAL_PackedPosOri_Blend) and 0x00093250 (from NAL_GenericAnim_EvaluatePose) never run
in missions 0-9 - not decompiled. Note: `--only` runs do not count calls of the other functions (settled ones are not
hooked), so a probe must be in the --only list itself. R_ range: R_LodDistanceForRadius, R_FindEffectByName (not reached);
0x00240930, 0x00240f70, 0x00241ff0, 0x00242730 remain (large packed SSE / x87).

**Earlier (decomp, 2026-10-23, day 15 round 3, renderer subagent): the clock in tests, frame begin/end, the whole
effect loader.** Harness: the recorded title_rdtsc now sets EDX:EAX = 0 (new `Clock` stand-in; EDX used to keep the
caller's value) and D3DDevice_InsertCallback/InsertFence/BlockOnFence/Swap are recorded calls (docs/comparison-harness.md).
Decompiled code reads the clock through the title's XAPI QueryPerformanceCounter (0x001ed6ca, a patched RDTSC site) - no
loader export. nglBeginFrame / nglWaitFrameInterval match, nglEndFrame settled (132/132; it calls nglRenderSceneTree by
its title address because the shader Draws read a stale constant lane; new platform_x87_int64_mul for MSVC's int64 to
float); nglVBlankCallback is unhookable (its RDTSC trampoline jump is in the first 5 bytes). Effect loader complete:
FxDomain_Create (was FUN_00098820) + the disc/circle domain SSE constructors, apsParseActions (dispatch tables walked
by a script), FX_LoadEffectFile (its two parameter buffers persist across stages/actions like the original stack slots:
FX_ParseEffectTexture fills only some words) and FX_LoadEffectFromStream (EAX = MemStream_Destroy's flag byte over the
effect slot address) all settled at the first run. Then particle pool/runtime helpers (FxPool*, FxNode_AddToScene, ...)
and 11 small NAL helpers (not reached: unused classes). Left in the range: the larger NAL class methods
0x00080000-0x00086000, 0x0008c0c0/c4a0/cc40, 0x00093250, and effect runtime 0x0009b830, 0x0009c3c0/c470, 0x0009cf00,
0x0009d840-0x0009e0c0, 0x0009f3c0, 0x0009ff50.

**Earlier (decomp, 2026-10-23, day 15 round 2, renderer subagent): the effect loader tree.** New game/code/fx_load.cpp,
~120 functions named from their bodies (manual_names.csv, Fx* names): the effect object (FxEffect_Init/SetName/
SetStageCount/SetStage/ComputeStageSizes/...), growable arrays (FxArray16, stage and emitter tables), the particle action
base FxAction_Init + 35 fixed-argument action constructors (generated from the disassembly), render op constructors,
FX_CreateRenderer (settled 429/429) and its ten creators (two families, one C routine each), the range/constant parameter
objects, the ops' shader setup and small FxSystem helpers. Tested at level load with `--only --every 1` (missions
1/4/5/6): ~55 matched, none mismatched. Not done: FUN_00098820 (the "domain" factory) with its two 813-byte SSE
constructors 0x000a5a80/0x000a5f20, apsParseActions (2.3 KB), then FX_LoadEffectFile itself (stack-address callees,
allocation context word 0x00909948) and FX_LoadEffectFromStream. RDTSC functions (nglBeginFrame/EndFrame/
WaitFrameInterval/VBlankCallback) stay skipped: the recorded title_rdtsc returns EAX = 0 but leaves EDX as the caller had
it, and these store EDX (the high word); a C version needs a clock entry point the loader provides plus EDX = 0 in the
recording (harness change, not made). Still open in the range (rank.py unassigned 0-1): ~100, mostly the NAL classes at
0x00080000-0x00093000 and effect runtime helpers 0x0009b000-0x000a0000.

**Earlier (decomp, 2026-10-23, day 15, renderer subagent): the renderer's bottom-up ready list.** ~85 functions in C
(ngl.cpp, shaders.cpp, nal_codec.cpp, nal_anim.cpp, tr_main.cpp, dobj.cpp, fx.cpp): the 18 shader NewVertexIter slots,
cdDebug_GetSortInfo, the NGL managers' skip-list clears + 7 deleting destructors, nglResourceManager_Add/ForEachPass,
nglDrawQuad (settled 252/252; the original keeps the depth in its argument slot: registered with 4 argument bytes),
nglSetFrameMemorySize, nglInitMultisample/InitFrameBuffers/SelectVideoMode/InitShaders, nglSelectMeshLodWithMatrix,
nglSelectMeshProjectors (was FUN_0006fb30), the debug mesh draws, nglReadFile, TGA/screenshot writers, nglDumpPrintf,
nglToggleShaderByName + Str_EqualN (was FUN_000442a0), the remaining entropy float DecodeFrames (Float1/Float4,
Packed8/16 Float3 + channel decoders), XModel_Free, R_Init, RE_BeginRegistration/RE_EndFrame, R_DrawTextWithShadow,
Anim_InitManagers, FX_ResetParticleSystem. Matched: XModel_Free, nglMeshSection_FreeBuffers, R_Init,
NalManager_SkipListRemoveKey8, nglDrawQuad, nglSetFrameMemorySize (startup: `--only --every 1 --missions 1`). Inconclusive
only (host calls: D3D Reset, XGAssembleShader heap, back-buffer GetDesc): nglReadBackBufferSize, nglInitMultisample,
nglInitFrameBuffers, nglInitShaders. Not reached in missions 1/4/5/6: iterators, manager teardown, the NAL classes above,
debug draws/dumps, projectors (no type-2 projectors in these levels). Unhookable (CALL/JMP/RET in the first 5 bytes):
nglShutdown, cdSimpleInstance_Init/FreeList, nglUpdateScene_Nop, nglResetDevice, nglResourceManager_Add, RE_EndFrame,
RE_BeginRegistration, nglDrawSingleQuadFrame, nglSaveScreenshot, Anim_InitManagers, FX_ResetParticleSystem.
Skipped: RDTSC-bound nglBeginFrame/EndFrame/WaitFrameInterval/VBlankCallback; dead code with no references
(nglDebugDrawLine/Axes, nglCreateNormalCubeMap, FUN_00068f70); FX_LoadEffectFromStream (Stream_LoadBpe returns its EAX =
MemStream_Destroy's leftover, called at the original's ESP); nglAllocFrameMemory, nglDebugMesh_AddBox/InitDebugMeshes,
nglAssemble*Shader, nglDumpCamera/SceneMeshes, R_DrawDObjSkeleton (startup/debug, large). Gotcha: fnpack disassembles a
switch's jump table as garbage - read the table words (xbedis.py) instead of guessing the case order
(nglSetFrameMemorySize mismatched on it).

**Earlier (decomp, 2026-10-22, day 13f): Float3 + quaternion class methods.** 31 functions in C
(game/code/nal_codec.cpp; docs/nal_animation.md "Float3 and quaternion classes"): the shared Float3 / quaternion slots
(type, size, Blend, BlendWeighted, Fixup, Init/Free/CopyState), the plain Float3 class's block methods, the entropy
Float3 class's BeginBlock/DecodeFrames/SkipBlock/Evaluate, the packed quaternion class's SkipBlock/SkipCachedFrame,
NAL_Entropy_SkipStatic4 and the quaternion entropy reader NAL_QuatReader_Init (0x000843c0, settled 301/301) with its
BeginBlock 0x000842e0. Missions 1/4/5/6: 22 matched (12 settled), none mismatched; not reached: the plain Float3
block methods, the entropy Float3 BeginBlock/DecodeFrames, NAL_EntropyQuat_BeginBlock, NAL_Const4, NAL_Quat_TypeId
(other levels / --only --every 1 runs). One m4 run crashed in nglInit's host device creation (parallel DXVK startup).
Next:
1. 0x00086280 (1.1 KB packed SSE, rebuilds w then quaternion products; psssym.py) - it unlocks the quaternion slots
   6/7 (0x000861b0, 0x00086730) and 0x0008d510; then 0x00082170/0x000822b0 and the other quaternion kinds' slots
   (list in docs/nal_animation.md "Not done yet").
2. The entropy PosOri block methods (0x00085600/0x000856e0, 0x000857b0/0x00084970, 0x00085930/0x00085480).
3. 0x0001e150 (2.5 KB vtable method, "camera"), then the cgame ones with one title callee left.

**Earlier (decomp, 2026-10-22, day 13e): the position + orientation classes.** 27 functions in C
(game/code/nal_codec.cpp, nal_math.cpp; docs/nal_animation.md "Position + orientation classes"): the shared PosOri
methods (Blend, BlendWeighted, Fixup, Init/Free/CopyState, SkipCachedFrame, type/size), the entropy trajectory
class's SkipBlock/Evaluate/ApplyStatic/ApplyWraps, the cursor helpers in most vtables, NAL_Trajectory_AccumulateRoot
(0x0008d3f0, root motion across clip wraps) with QuatTrans_Inverse and QuatTrans_Power, and the plain trajectory
class's ApplyWraps (0x00082ee0) + AccumulateRoot copy (0x0008d2d0). Missions 1/4/5/6: 22 matched (8 settled), none
mismatched; not reached: NAL_Cursor_Skip8, the plain trajectory pair. Harness fix: xbcmp_resume left EFLAGS below
the entry ESP (AccumulateRoot's never-written result w read it: 156 false mismatches, reset in functions.csv).
NAL_AnimTrack_Sample / AnimTree_CalcPose_r / DObj_UpdateSkeletons still differ only in w words: Blend never writes
w either, and the sampling temporaries are on the stack - they stay on record (the earlier "match once 0x0008d3f0
is ours" was wrong). Next:
1. The quaternion (0x007c2a30) and position (0x007c2620) classes, with the entropy PosOri block methods that share
   their readers (0x00085600/0x000856e0, 0x000857b0/0x00084970, 0x00085930/0x00085480, 0x000843c0).
2. 0x0001e150 (2.5 KB vtable method, "camera"), then the cgame ones with one title callee left (CG_AddLocalEntities,
   CG_Check*Events, CG_CopySaveState).
Gap functions (user, 2026-10-22): decompile them uncreated - register by address, bound each at the next
table/vtable entry (fnpack overstates their sizes); do not create them in Ghidra. Later one CreateGapFunctions.java
pass over the NAL range 0x0007c2b0-0x00093090 + ApplyNames, when the user wants the counter complete.

**Earlier (decomp, 2026-10-22, day 13d): the entropy mode decoders.** All 46 distinct decoders behind the
64-entry table 0x008bb4a0 are C code (game/code/nal_entropy.cpp; NAL_EntropyReader_Decode dispatches through our
table). New test tests/nal_entropy_test.cpp maps default.xbe's decoder bytes (pure, relative jumps only) and runs
them against ours on random streams: 64/64 modes identical (skipped without the XBE). In game (missions 1/4/5/6):
modes 0x00-0x1d reached, 1,351 tests matched, none mismatched; the negative-scale-only decoders 0x2f-0x3d and
0x1e are not reached in these levels (covered by the offline test). Only 11 of the 46 are created functions in
Ghidra; the 35 gap ones stay uncreated (names in manual_names.csv, not applied yet).

**Earlier (decomp, 2026-10-22, day 13c): DObj_UpdateSkeletons' whole animation subtree, a cgame batch, the
game module started.** Matched 649 -> 907 with both subagents' batches. Renderer/stream (subagent, merged 9283f88): the texture/mesh load chain, nglParseMesh, the stream zone searches and file-type handlers, MemContext_Push/Pop - 73 matched, 11 not reached, Stream_LoadStb inconclusive only (LIBCMT _clock); harness gained a D3DSurface_GetDesc stand-in and MmSetAddressProtect as a recorded kernel call. Skipped there: nglBeginFrame/EndFrame (RDTSC), FX_LoadEffectFromStream (needs Stream_LoadBpe's frame rebuild). Names from this session are applied in Ghidra (2918bd8) except 0x0007c360 NAL_SignalCounter_SkipBlock, an uncreated gap function.
Game module (subagent, merged ca925e7): game/code/g_scr_math, g_scr_runtime, g_utils, actor, bg_misc - 142 matched,
30 not reached (script math, actor state callbacks, pmove ammo helpers, trivial SP_* spawns; list in that commit's
branch report), none mismatched; Scr_GetEntObjectOrEmpty, Scr_Acos, Scr_Asin are unhookable (CALL in the first
5 bytes). cgame (a012446): CG_AllocLocalEntity, CG_DrawHudElemAlignedText/Material, CG_StartShakeCamera,
CG_CalcWeaponBob, CG_UpdateCvars, CG_DObjEnsureSkeleton, CG_GetHudElemText, CG_DrawScreenBlackout,
CG_GetCurrentWeaponDef, CG_DrawDamageBlend matched; CG_Viewpos_f, CG_AddNearCulledLocalEntity, CG_ScoresDown_f not
reached. NAL_EntropyReader_DecodeFloats + the entropy Float3 DecodeBlock/DecodeChannelBlock matched (the bit
position needs 64 bits: byte address * 8). Next cgame: the ones with one title callee left (CG_AddLocalEntities,
CG_CycleWeaponSlot, CG_DrawProgressBar, CG_Check*Events, CG_CopySaveState; rank with fnpack as in this session). New modules: game/code/nal_pose.cpp (NAL_GenericAnim_EvaluatePose, the byte-code
pose evaluator: settled at the first run; its IK opcode 5 is written but never reached in missions 0-6, nor
NAL_IK_TwoBoneAngles / Mat44_InverseRigid; 0x00093250 is still called at its title address), game/code/nal_anim.cpp
(anim states, block cache, track sampling, AnimTree_CalcPose_r), game/code/nal_codec.cpp (stream readers + 17 small
channel-class methods; only the Float3/Entropy ones are reached here), DObj_UpdateSkeletons / DObj_GetAnimRootTransform /
XModel_TransformBones in dobj.cpp (dobj_call_regs: one thunk for the EAX/ECX/EDX game callees). 21 of the subtree
matched (20 settled); DObj_UpdateSkeletons, AnimTree_CalcPose_r and NAL_AnimTrack_Sample differ from the original
only in the w word (+0x1c) of PositionOrientation records, which the title never sets (stale stack in 0x0008d3f0):
leave them on record until that channel class is C code (docs/nal_animation.md). Not reached: NAL_FindSkeleton,
NAL_RleReader_ReadBytes and 14 codec methods of classes these clips don't use; untested: Packed8Float1 Skip/DecodeBlock,
SignalCounter Decode/SkipBlock, EntropyFloat1_Evaluate. (The e4a0e9a commit message's "27 matched / 19 not reached /
6 untested" is off: 21 / 16 / 5.) Next: the entropy mode decoders (table 0x008bb4a0, 32 prefix-code readers, reached
every frame) and NAL_EntropyReader_DecodeFloats (0x0007cb10), then the used channel classes - most of their methods
(vtables 0x007c2a30 quaternion, 0x007c2620 position, 0x007c2bd0 position+orientation) are uncreated gap functions in
Ghidra: ask before creating them. Also 0x0001e150 (2.5 KB vtable method, "camera", calls the anim-state functions).

**Earlier (decomp, 2026-10-22, day 13b): cgame batch + NAL math leaves.** Matched 637 -> 649. cgame: CG_Fade_f,
CG_DrawFlashFade, CG_DrawCenterString, CG_DrawRotatedQuadPic (ESI/XMM0 entry), CG_ShareWeaponTagBModels (ESI) matched;
CG_DrawTextScaled (EAX/EDX/ECX/XMM1) and CG_DrawRotatedPic (XMM0) not reached in level runs. New module
game/code/nal_math.cpp (DObj_UpdateSkeletons' math): Mat44_MultiplyInPlace, Quat_ToMat33, Vec3_MulMat44,
QuatTrans_Concat, QuatTrans_ToMat44, Quat_Slerp, Quat_FromAxis3 all settled at the first run; Mat44_InverseRigid not
reached (IK opcodes only). Lifting: psssym.py handles the shuffles (start after the `mov reg,[ebp+N]` argument loads,
pass --in for the pointers; it ignores `xorps` with a mask - here 0x0082e0b0 = all sign bits - and x87 code: the
"radicand" factor it prints is really the x87 0.5/sqrt). New compiler.h helpers: platform_x87_half_over_sqrt,
platform_x87_sqrt_sub_mul. Next: the rest of DObj_UpdateSkeletons bottom-up (NAL_IK_TwoBoneAngles, 0x00093250, then
the pose evaluator), the earlier list below otherwise unchanged.

**Earlier (decomp, 2026-10-22, day 13): renderer runtime nearly done, cgame started, harness stand-ins.**
Matched 576 -> 637 this session (subagents in worktrees for part of it; all merged). Renderer: FX spawn path
(FX_SpawnQueuedEffect, FX_UpdateEffect, FX_SpawnEffect, FX_KillEffectsOnEntity, FX_ReleaseEffectHandle),
RE_RenderScene, RE_StretchPicRotate, RE_AddPolyToScene + mesh-builder helpers, R_DrawFogPass, R_RenderTestShadow,
cdOcean_SetWaveConstants, DObj_Create/DObj_Free/AnimTree_Free, DObj_GetOwnerPlacement. Patterns that made them match:
callees at the original's ESP (stack-address globals, stale stack read by later callees: RE_RenderScene, DObj_Create,
FX_SpawnQueuedEffect), and a still-mismatching callee of ours called by title address (R_RenderView).
Harness: RDTSC trampolines executable in test processes + title_rdtsc recorded (Sys_Milliseconds callers testable);
stand-ins for GetLevelCount, GetSurfaceLevel2/GetCubeMapSurface2 (surface pool in the test area), Release-to-0,
texture/surface/VB/palette locks (pure arithmetic; arg kind `x` = popped, not logged); the Lock path found a real bug
in our nglUpdateFogTable (fixed). cgame: game/code/cgame.cpp (small HUD/draw/fade/ammo functions) and cg_view.cpp
(gun offsets, kick, shell shock, tilt, camera shake, tracers): 24 + 10 matched. Names: 214 applied in Ghidra
(renderer FUN_ pass, the NAL animation codec - docs/nal_animation.md, DObj_UpdateSkeletons' subtree, 46 hubs).
Not reachable in level runs: ~100 renderer console toggles/front-end paths (an --only --every 1 sweep reached 2),
the icon text, CG_CalcCubemapViewValues (cube-map capture only). Next:
1. cgame bottom-up (tools/progress.py --module cgame; 42 were ready, ~34 done): CG_DrawCenterString, CG_DrawFlashFade,
   CG_Fade_f, CG_DrawRotated(Quad)Pic, CG_DrawTextScaled (EDX/XMM1/ECX/EAX inputs), CG_ShareWeaponTagBModels.
2. DObj_UpdateSkeletons bottom-up: math leaves first (Mat44_MultiplyInPlace, QuatTrans_*, Quat_ToMat33,
   Mat44_InverseRigid; conventions in docs/nal_animation.md), the NAL pose evaluator (10 KB byte-code interpreter) last.
3. Texture load chain (nglLoadDDS & co.: D3DSurface_GetDesc still has no stand-in), nglBeginFrame/EndFrame (RDTSC
   site inside: game/code needs a title-clock hook), R_RegisterModel (fixed-string helpers).
4. Hub naming: next targets listed in docs/naming.md.

**Earlier (decomp, 2026-10-22, day 12c): all four FX_Add*Components done.** FX_AddSoundComponents
(0x0029eea0), FxSoundComponent_Create (0x0029df60, ESI), SndVoice_GetCount/SndVoice_Get - all settled at the first
run (784 tests). Snd_CountBusyVoices (0x00293310, CALL in its first 5 bytes) is a C helper, not registered. Sound
differs from particles: a source with its own aliased sound (ZoneAliasList_NextVariant) makes one component without
triggers; only the first trigger with choices is used; only single-choice lists are cached; weapon cache at +0xac.
0x0096d8f8 = the name's stack address in the original (((ESP - 4) & ~7) - 0x6b0); S_FindSoundByHash compares
strings (bsearch + stricmp), so our own buffer gives the same result. Names for FxSoundComponent_Update's callees are
in manual_names.csv (15 rows, not applied in Ghidra yet). Then those leaves (new module game/code/snd.cpp: SoundRef_* {voice, alias, flag +8, ..., +0x28
source; 0x30 bytes}, S_PlaySoundAlias2 - S_StartAliasVoice2 takes EAX/EBX(out)/ESI/EDI, asm thunk; the voice
helpers moved there; fx.cpp: FX_LimitSoundComponents, Entity_EndSoundNotify with a Scr_Notify ESI thunk): 10 matched
(S_PlaySoundAlias2 14 tests), none mismatched. Then FxSoundComponent_Update 0x0029d7c0 (1.9 KB): settled at
the first run (347 tests); its asm entry passes the caller's EBX (the original pushes EBX with only its low bytes
replaced as an S_PlaySoundAlias argument); FxSoundComponent_Create now calls it with EBX 0 (as the original). The
whole FX component tree under FX_SpawnQueuedEffect is now ours except 0x00293310 (unhookable, C helper). The day's
15 names are applied in Ghidra (symbols exported, smoke test clean). Next: the next renderer/FX callers bottom-up (tools/progress.py --module renderer --state untested).

**Earlier (decomp, 2026-10-22, day 12b): three of the four FX_Add*Components done.** Since 14ba77a: the
ParamSet resolver (ParamSet_Resolve 0x0029b9f0 with ParamNode_CollectChoices, ParamSet_CompareValue,
ParamSet_TestCondition), FX_AddShakeRumbleComponents and FX_AddParticleComponents - all settled at the first run
(TestCondition not reached: these levels only branch on kind 0/9 parameters, which use the binary search). Light and
shake/rumble use list 1 of the resolver's output (highest priority), particles list 0 (every choice). Byte-sized
parameters (0x18, 0x3a, 0x3b) index their value arrays without the * 4 (fx.cpp choice_byte). FX_AddParticleComponents
stores the address of its stack copy of the effect file name in 0x00971800: the C version writes the original's
address, computed from the entry ESP (((ESP - 4) & ~7) - 0x770), and uses its own buffer - no frame rebuild needed
because nothing reads the global after the function's own hash loop. Next: FX_AddSoundComponents (3.4 KB, stack
address in 0x0096d8f8) and its sound tree (0x0029df60 -> 0x0029d7c0, 1.9 KB, 18 callees); 0x00293310 cannot be
hooked (CALL in its first 5 bytes).

**Earlier (decomp, 2026-10-22, day 12): the FX component helpers under FX_Add*Components.** New module
game/code/param_set.cpp: parameter sets (ParamList_Init/Clear/Add/Delete, ParamSet_Destruct/Delete/AddString/
AddValue/AddFxOverrides, ParamChoice_GetValue; the stack object FX_Add*Components and the animscript event builders
0x00200450/0x00203280 fill). In fx.cpp: ParamChoice_GetFlags, ParamChoiceList_PickRandom/PickPerGroup (weighted pick,
particle LCG 0x008d5a70), FX_CollectTriggers, FXTriggerTable_Find, FixedStr32_Format, ZoneAliasList_NextVariant,
FxComponent_GetEntity/GetEntityOrigin/GetPosition, DObjHandle_Resolve, WeaponModel_GetDObjHandle, FX_AxesFromForward,
FX_RandomInOctaves, FX_AnglesToMatrix, the camera shake (CameraShake_Start/AllocSlot/InitSlot/SetAmplitude/
SetDistanceScale), the rumble (Rumble_SetScale, ShakeRumble_Start, ShakeRumbleRecord_Init, ShakeRumbleDesc_Init/
Destroy) and the shake/rumble component's Update (0x002a3570, vtable 0x008376d0 slot 1); UintMap_Find (ngl.cpp),
AnglesToAxis and MatrixMultiply43To44 (qcommon.cpp). Harness: `Result::Xmm0` (float results in XMM0) and the hook's
instruction decoder takes SSE arithmetic/compare opcodes (CameraShake_SetAmplitude starts with UCOMISS). Title CRT x87
intrinsics (__CIsqrt/__CIsin/__CIcos/__CIpow) are called at their title addresses (fx.cpp title_ci_*), so error
cases match. Gotcha: cdecl functions that reuse an argument slot as scratch (FX_RandomInOctaves, AnglesToAxis) get
that slot compared - the asm entry hands the C version the slot's address. Not reached in missions 1/4/5/6: the
ParamList/ParamSet destructors, FixedStr32_Format (only with a trigger prefix). Also: FX_PlayEffectWithAxis/
AlongDir, the particle component's Update (FxParticleComponent_Update 0x002a46d0: reads the spawn parameters
FX_AddParticleComponents leaves in 0x009717f4-0x00971838) and the first of the four, FX_AddLightComponents (settled
at once; it calls the ParamSet resolver 0x0029b9f0 through the set's vtable). FX_PlayEffectAt now returns
FX_SpawnEffect's result (registered Eax; it was None, so the dropped return value went unnoticed). 44 new functions
this session (commit 14ba77a says 45/41 by mistake: 44 new, 40 matched, 4 not reached), none mismatched. Next: the ParamSet resolver 0x0029b9f0 (952 bytes: a decision tree over the
definition's nodes; callees 0x00296e50, 0x00299310, 0x002994f0), the sound component 0x0029d7c0/0x0029df60, then
FX_AddShakeRumble/Particle/SoundComponents; 0x00293310 cannot be hooked (CALL in its first 5 bytes).

**Earlier (decomp, 2026-10-22, day 11d): the emitter Apply and the last particle actions.** 16 more, 10 matched,
6 not reached, none mismatched: apsActionEmitter_Apply (0x000a2e40, SrcA/Brst/RSpn: the 16 generators at self +0xc,
vtable slot 1, called in a fixed order; settled), FxEmitter_AddParticle, FxBox_Expand, apsSinCos (0x000a06e0 =
scene_sine_poly), Quat_FromAxisAngle, Quat_FromEuler, Mat44_TransformVector, AngT (apsActionAngleToVelocity_Apply),
Traj (apsActionTrajectory_Apply, path lookup 0x00909940) + apsPath_FindSegment - all settled or matching. Not reached
in missions 0-7: LAtr/DLAt (line attract), KTau (apsActionFrenetMove_Apply; particle fields +0x9b-0x9f) and AngT's
flag-0x20 subtree (apsOrientToVelocity, Mat44_FromDirection, Quat_FromMat44); grep of the level .cod files: Traj in
prologue/gela/omaha, WPRf in oran, AngT in many; LAtr/DLAt/KTau/PAtr/ATEX not found (maybe packed). New x87 helpers:
platform_x87_add_sqrt, platform_x87_sub_sub_add_sqrt. Missions 8-11 reach none of them either (start-of-level
runs): they need a played --compare run (XBCMP_ONLY = these + ATEX/Y/Z, PAtr, WPRf). Day-11 names (60) applied in
Ghidra (13 functions created: total 16517), symbols exported. Next: the rest of the particle library
(FX_Add*Components and their param-block writer tree).

**Earlier (decomp, 2026-10-22, day 11c): more particle actions.** 11 more, 6 matched, 5 not reached, none mismatched (commit 8ac7562 says 12/8 by mistake):
apsAtan2 (0x000a4ac0, reuses FxSystem_SetView's asin series), ClSh, MFxV, Forc, OMov + Quat_RotateVector (0x0009f6c0),
all settled or matching; not reached in missions 1/4/5/6: ATEX/ATEY/ATEZ, PAtr, WPRf (need a played --compare run or
other missions). New x87 helpers: platform_x87_add_mul, platform_x87_rcp_norm4 (and the portable fallbacks, also for
exp2_series). Never-set stack lanes in ClSh/WPRf are taken as 0 (test processes zero dead stack). Names still not
applied in Ghidra (day 11 batches). Next: AngT 0x000a4640 (callees 0x000a4460, 0x000a4d30), LAtr 0x000a3f00, DLAt
0x000a41c0, KTau 0x000a2300, Traj 0x000a2aa0, the emitter Apply 0x000a2e40; then a played run over effect-heavy
levels for the not-reached actions.

**Earlier (decomp, 2026-10-22, day 11b): the particle action classes.** The action vtables (0x007c8df0-0x007c90a4,
5 slots: Apply, type (four-character code, names the class), a4980 (returns 1.0), dtor a4d10, size 0x0009c370; the
emitters SrcA/Brst/RSpn share Apply 0x000a2e40 and add slot 5 = particles to emit). Class list with addresses: fx.h.
Particle field offsets (emitter +0x8d..+0xa0 bytes): fx.cpp "The particle actions". 22 more, 15 matched (13 settled),
none mismatched: FxEmitter_QueueParticleKill, the counts of SrcA/Brst (RSpn not reached), Life, AlFd/AFIO/RFIO, LnSc/
LScW/LScH, ExSc (EScW/EScH not reached; x87 FYL2X series = platform_x87_exp2_series), Move, PsMv, VlDr/AVDr (VAVD not
reached), UVFr not reached, apsMemoryPool_FreeSlots/apsPoolSet_Clear not reached (shutdown). Renamed: 'SpDe' = spawn on
death (apsActionSpawnOnDeath_Apply, apsElementList_SpawnDeathEffect; 0x00909f10 = the current element list). Names
not applied in Ghidra yet (this and the previous batch). Next: the bigger actions - OMov 0x000a18f0, MFxV 0x000a39f0,
Forc 0x000a3bb0, ClSh 0x000a1bc0, WPRf 0x000a1d20, AngT 0x000a4640, ATEX/Y/Z 0x000a1fa0/0x000a20c0/0x000a21e0, PAtr
0x000a3d80, LAtr 0x000a3f00, DLAt 0x000a41c0, KTau 0x000a2300, Traj 0x000a2aa0, the emitter Apply 0x000a2e40; the 34
type getters (mov eax,imm; ret) if worth registering.

**Earlier (decomp, 2026-10-22, day 11): the particle pools and spawn actions.** 11 more, 6 matched, none
mismatched: apsMemoryPool_AllocSlots (0x0009c210) / apsMemoryPool_Init (0x0009b9d0), apsPoolSet_AddPool (0x0009bf50) +
apsAddPool (0x0009c050, global set), apsPtrVector_Resize (0x0009c620), apsPtrVector_PushFront (0x0009cb60: with room
left the original only shifts the items, item not stored), apsElementList_Add (0x0009cb00, FX_LoadEffectFile; 'SpDe'
element type 0x53704465 remembered at +8), apsElementList_SpawnDeathEffect (0x0009c9e0), the spawn actions
apsActionSpawnTimed_Apply (0x000a1730, vtable 0x007c907c) / apsActionSpawnOnDeath_Apply (0x000a1850, 0x007c9090; birth
"SpDe" = spawn on death, pass = delta 8888.89), FxSystemList_DestroyAll (0x00095300, CG_Shutdown tail). Not reached: apsAddPool/AddPool (17 calls
from 0x00234a00 at startup, before tests run; also with --only --every 1), apsElementList_SpawnDeathEffect (its caller 0x000a0970 is
a vtable slot), OnBirth, DestroyAll. Dead code (no refs): 0x0009c3c0 (array compaction), 0x00095360 (an older
apsEffect_Init). Names not applied in Ghidra yet. Next: the other Resize/Init callers (0x0009bee0, 0x0009bff0,
0x0009c070 from Mem_InitSmallBlockPools, 0x000951c0, 0x0009f250), 0x000a0970, then the remaining action classes
(apsParseActions 0x00097cd0 vtables) and the FX_Add*Components tree.

**Earlier (decomp, 2026-10-22, day 10): particle allocation and effect creation.** 19 more, all matched,
none mismatched: apsMemoryPool_Alloc, apsPoolSet_Alloc (best-fit pool set, 0x0009bba0) + apsAlloc (global set
0x00909f08), apsGroup_Create (emitter setup), apsEffect_Init/Create/Spawn (active list 0x009099ac, at most 0x78;
the definition's created count +0xac is raised twice), Mat44_TransformPoint, Mat44_FromQuatTranslation (quaternion
scaled by sqrt 2; each row's 4th lane repeats its first), apsPtrVector_PushBack (0x0009f570, seeded flag 0 around
the heap calls) and the effect arrays (new ApsArray {data, int16 capacity, count}: apsArray_Free, apsPtrArray_* and
apsParamArray_* Alloc/Resize/Reserve/PushBack - heap calls with effect id -1, seeded flag 1). FxEmitterList_Add and
FxEmitter_TransformBounds now call ours. Not reached: apsParamArray_Alloc/Resize/PushBack (only effects with
parameter overrides); Mat44_FromQuatTranslation 4 tests. ApplyNames.java skips the CSV's first row (header): start
name files with one. Next: the other callers of the new helpers - 0x0009bf50 (-> 0x0009b9d0), 0x0009c3c0
(-> 0x0009c620), 0x0009cb00 (-> 0x0009cb60, from FX_LoadEffectFile), 0x0009c9e0 / 0x000a1730 / 0x000a1850
(apsEffect_Spawn callers), 0x00095300 (CG_Shutdown tail); then the particle action classes (Apply methods: vtables
from apsParseActions 0x00097cd0) and the FX_Add*Components param-block writer tree (see below).

**Earlier (decomp, 2026-10-22, night 9): the particle update tree.** FxSystem_Update (0x00094c60) and its whole
subtree are ours: FxEmitterDef_Update/Finish (0x0009c7b0/0x0009c730: actions = vtable slot 0 Apply(begin, end,
emitter, system, time, delta) inside +4 {start, end} windows; emitter-level ones have +0x14 clear; hidden emitters wait
+0xb4 from a depth curve, max step 0.3), FxEmitter_BeginParticles / AdvanceParticles (one range per pass; ages
particles, grows the bounds accumulator 0x00909ee0/f0 by sprite or two-size extents) / EndParticles / CompactParticles
/ UpdateVisibility / TransformBounds, FxBox_GetCorners, FxCurve_Evaluate, FxSystem_SwapParams, FxParamBlock_Set,
FxSystemDef_IsEmitterVisible, FxEmitter_FreeParticles, apsMemoryPool_Free, FxEmitterList_Add/Remove, FxSystem_Destroy,
FxSystemList_Update, FX_DrawGlobalSystems, FxRandom_Reset/Restore. compiler.h: platform_x87_mul,
platform_x87_greater (FCOMIP, no MXCSR flags). Batches 1-2: 17 matched (all settled), none mismatched; batch 3:
7 matched (all settled), none mismatched - 24 this session. Names applied in Ghidra (also night 8's), symbols exported. Next: the rest of the particle library bottom-up - apsMemoryPool_Alloc (0x0009ba90), the emitter
list's push_back (0x0009f570), Mat44_TransformPoint (0x00097ba0, packed), apsGroup_Create (0x00097a10),
apsEffect_Create/Init, the particle action classes (Apply methods: find their vtables from apsParseActions
0x00097cd0); the four FX_Add*Components still need the param-block writer tree (0x00023690/0x000237b0/0x00023b20 ->
0x00024630/0x00024730 -> 0x00023710).

**Earlier (decomp, 2026-10-22, night 8): big functions + the particle library.** FX_UpdateEffects fixed:
0x0096d8f8/0x00971800 hold addresses of stack locals (FX_AddSoundComponents, FX_AddParticleComponents), so its
callees must run at the original's ESP - FX_UpdateEffects_entry moves the C frame 128 KB down and calls each
callee through the new `title_call_at_regs` (title_data.h; all six registers); old mismatches reset. New:
XModel_CalcSkeleton, XModel_CalcBoneChildren, BoneMatrix_Load/Store (0x0029c790/0x0029c9e0), FX_GetBoltMatrix,
FX_DrawEffects (FxSystem_SetView takes a 2nd argument, -1.0f, left on the stack from nglGetMatrix's pushes), and
the start of the particle library ("aps", 0x94000-0xa0000, 217 unnamed): FxSystem_SetView (atan2 series, x87
helpers platform_x87_rcp_hypot / platform_x87_sqrt_abs_sub_mul), FxBuffers_Flip, FxMatrix_Multiply, FxSystem_SetMatrix,
FxEmitter_SetMatrix, FxSystem_IsFinished/Stop/UpdateSortKey, FxSystem_Draw, FxEmitter_Draw, FxBox_Center/Radius/Sphere
- 22 matched (all settled), none mismatched; XModel_CalcBoneChildren not reached (only DObj_CalcAnimatedSkeleton
calls it). Next: FxSystem_Update (0x00094c60) bottom-up - leaves read and understood:
0x000965a0 (def +0x50 [i*0x3c +0xc] & 1), 0x0009c330 (obj+4 [i] = value, ret 8), 0x00094990 (swap the +0x5c param
list {obj, index, value} 0xc each into the objects), 0x000971a0 (free +0xbc via (*0x008bb760) slot 2, 0x00909ec0 -=
+0xc0; ret 4 with an unread argument), 0x0009b770 (pool free: index (ptr - +0x18) / +0x14, free list +0x20),
0x0009c6e0 (curve: 4 keys {time +8, value +0xc}, first time > t, else +4 * +0 on the x87); then 0x00097200,
0x00097580 (642 bytes packed SSE), 0x000979d0, 0x000973b0, 0x0009c7b0, 0x0009c730, 0x0009f2d0/0x0009f350; then
FX_DrawGlobalSystems (0x00236ae0, needs 0x00095250). Also open: the four FX_Add*Components (7.7 KB) need the
param-block writer tree (0x00023690/0x000237b0/0x00023b20 -> 0x00024630/0x00024730 -> 0x00023710) first.

**Earlier (decomp, 2026-10-21, night 7): renderer bottom-up, ninth batch.** 14 more: FX_PlayBoltedEffect
(EAX origin; FX_GetBoltMatrix EDI thunk; bolt rows rotated axes-first), FX_ParseEffectTexture (EDI stream, ESI out),
FX_InitEffectPool, FX_UpdateEffects (EAX list; FX_UpdateEffect EBX and FX_SpawnQueuedEffect EDI thunks), RE_TextWidth,
RE_RegisterShader (EDX name; StringMap_Find EBX thunk; "FrontEnd" zone bank then +0x34, else white texture),
R_LoadCellMeshes (Stream_FindMeshFile EBX thunk, 5th push unread), RE_Shutdown, nglInitTextureManager,
nglCreateSysFont, nglFreeTexture, and two new names cdOcean_LoadWaveSet/StoreWaveSet (0x0004ef90/0x0004f370:
wave set rows <-> tweak globals). FX_ParseEffectTexture, RE_TextWidth, FX_PlayBoltedEffect settled; RE_RegisterShader,
R_LoadCellMeshes, FX_InitEffectPool, RE_Shutdown (--only run) matching. Open: FX_UpdateEffects 73 of 463 mismatched,
one byte each at 0x0096d8f8/0x00971800 (88 vs 40, c8 vs 80); not a register input (FX_UpdateEffect, 0x00296c90,
FX_SpawnQueuedEffect checked) and calling the title's FX_UpdateFadingComponents changes nothing - probably stale
stack read by a component vtable method; find the writer of those bytes next. Inconclusive (SDK host code):
nglInitTextureManager, nglCreateSysFont, nglFreeTexture. Not reached: the cdOcean wave-set pair (only when the ocean
material is the tweak slot, cdOcean_SetWaveConstants). Next: FX_UpdateEffects' byte writer; then rebuild the list;
candidates cdOcean helper 0x00050910 (packed matrix, psssym), cdOcean_SetWaveConstants, nglSelectVideoMode,
nglInitFrameBuffers (SDK), 0x0007cb10.

**Earlier (decomp, 2026-10-21, night 6): renderer bottom-up, eighth batch.** 16 more: FX_SortEffects,
FX_ClearEffects, FX_FreeAllEffects (ESI entry), FX_FreeAllEffectLights (0x00236aa0, was also named FX_FreeAllEffects),
DObj_CreateSkeleton (DObj +0x2f = bone_count), XModel_ResolveBoneNames (ESI entry), and 10 newly named leaves:
R_Set/GetDebugViewMode (0x00901e94), R_AddFogPass (+ R_DrawFogPass 0x00045850 named only), ShaderPCUV_InitMaterial,
Map_EmptyKey, nglSkipList_RandomLevel/InitHead (NglSkipList +0 random_bits, +4 bits_left - were seed/max_level),
nglFlushDebugLineBatch, nglTexture_FreeHeaders, nglIndexBuffer_Free. PtrVector_Resize/PushBack (ESI/EBX) called
through asm thunks in fx.cpp. 14 matched (6 settled), none mismatched; not reached: R_SetDebugViewMode (console
toggles only), nglIndexBuffer_Free. Ready-list notes: most remaining zero-pending renderer FUN_s have no direct
callers - 22 shader-class pairs (45/47 bytes: a functor {vtable, this, 0} cloned with Z_Malloc; vtables next to
each class name string, e.g. 0x007b441c) and a 0x7c000-0x80000 cluster of size-counting/serializer methods; the
skip-list inserts (0x00063f10, 0x00069e00, 0x00083db0, 0x00083f70) sit behind unrecognised gap code. Next: rebuild
the list; candidates FX_PlayBoltedEffect, FX_InitEffectPool, R_LoadCellMeshes, nglInitTextureManager,
RE_TextWidth, the cdOcean helpers (0x0004ef90, 0x0004f370, 0x00050910), 0x0007cb10 (54 callers).

**Earlier (decomp, 2026-10-21, night 5): renderer bottom-up, seventh batch.** 7 more: nglRenderTextNode
(the text node's Draw: one raw pushbuffer of quads; new NglTextNode, NglFont +0x40..+0x48 named), FX_StopEffect,
nglMatrixOrthoOffCenter, nglUnprojectPoint, FX_PlayEffectAt (ESI origin), RE_ParseButtonToken (EBX out, stdcall),
nglPalette_Unlock (bare ret, called from nglCreateTexture). Harness: BeginPush/EndPush stand-ins (the pushbuffer's
dwords are compared; planted-bug check caught). compiler.h: platform_x87_unsigned_mul_add. nglRenderTextNode 301
and FX_StopEffect 156 matched (settled), none mismatched; not reached: the two matrix functions (no callers),
FX_PlayEffectAt, RE_ParseButtonToken (only text with "{"). Skipped: RE_RenderScene (calls R_RenderView, which
mismatches on stale stack), nglEndFrame (D3D Swap/fences not recorded), debug overlays, zlib. Callees outside
the module (_ftol, HashStr_Init, PtrVector_*) do not block: call them by address. Next: rebuild the bottom-up list;
candidates FX_SortEffects, XModel_ResolveBoneNames, FX_ParseEffectTexture, DObj_CreateSkeleton, FX_ClearEffects,
FX_PlayBoltedEffect; RE_ParseButtonToken needs a "{button}" text run (XBCMP_ONLY).

**Earlier (decomp, 2026-10-21, night 4): renderer bottom-up, sixth batch.** 11 more: the pack relocation
chain (Pack_RelocateTable, Pack_RelocateChain (EAX chain, EBX bits; asm entry), Pack_DecodeRelocWord - new names),
nglCreateTexture (NglTexture +0x38 companion surface, +0x54 name), nglCreateWhiteTexture, nglCreateFogTextures,
RE_DrawTextExt (^N colours, {button} icons through RE_ParseButtonToken: stdcall + EBX out buffer, called with an asm
thunk), R_FindFont, Q_strncpyLen, Q_strncpySubLen (new name). 9 matched (RE_DrawTextExt 124, R_FindFont 332, the
Q_ copies and Pack_DecodeRelocWord settled), none mismatched. nglCreateTexture/WhiteTexture/FogTextures mostly
inconclusive: nglTextureGetSurfaces calls host D3D getters (no stand-in). Not reached / no callers:
RE_StretchPicRotate, RE_TextWidthWithIcons, nglDrawFogPass, RE_EndFrame (skipped). Next: rebuild the bottom-up
list; candidates nglEndFrame (1201 bytes, x87; unblocks RE_EndFrame, nglDrawSingleQuadFrame), R_DrawTextWithShadow
(debug callers only), nglLoadTexture chain (file I/O), RE_BeginRegistration/R_Init/R_Register (startup).

**Earlier (decomp, 2026-10-21, night 3): renderer bottom-up, fifth batch.** 14 more: nglParseFontFile (EDI
entry; new NglFont/NglFontGlyph), nglLoadFontFile, nglLoadFont, nglUpdateFogTable (takes over only the first
changed fog value per call), nglParseDDSPalette, MemStream_Destroy/DeletingDestroy, DObj_GetTagIndex (obj EAX,
hash ESI) + Com_HashKeyNoCase, FX_FreeEffectsById, R_CheckCvarRange, FastSinCos (= scene_sine_poly lanes),
RE_RegisterModel, RE_DrawText - 11 matched, none mismatched; not reached: MemStream_DeletingDestroy,
nglParseDDSPalette, RE_RegisterModel. compiler.h: platform_x87_div_by_unsigned. Skipped: nglBeginFrame (RDTSC).
Next: rebuild the bottom-up list; candidates RE_DrawTextExt (1333 bytes, x87), Pack_RelocateTable (name
FUN_00055ab0/FUN_00055b40 first), nglLoadDDSTexture chain.

**Earlier (decomp, 2026-10-21, night 2): renderer bottom-up, fourth batch.** 25 more: nglGetTextExtentf/
SizeF, nglCreateVertexBuffer(Physical), nglInitTextureHeader, nglAllocTextureMemory, nglCreatePalette (palette
size 32/64/128/256 -> 3/2/1/0, others 0), nglMeshBuilder_Begin/AddSection/End, nglDynamicVB_Begin/EndFrame,
nglQuadBatch_Reserve, nglAllocSceneRenderTargets, nglAllocFogTextures, nglFontParseKeyInt (regparm(2)),
nglIndexBuffer_Alloc, XModelParts_FindBone (EBX/ESI asm entry) + DObj_FindBoneIndex2 - all matched, none
mismatched. Inconclusive only: nglTextureGetSurfaces and nglDynamicVB_BeginFrame (D3D getters/Lock are host code;
no stand-in - GetSurfaceLevel allocates host surfaces). Not reached: nglCreateDDSPalette. Unhookable bare-ret
stubs, newly named: nglVertexBuffer_Unlock, nglVertexBuffer_GetD3D, nglTexture_Unlock, nglBeginFrame_Nop.
NglTexture now has format +8, width +0x10, height +0x14, levels +0x18, surfaces +0x34; XBoneInfo +0 = name text.
Next: rebuild the bottom-up list (progress.py --module renderer --state named -> CalleeNames.java ->
bottomup_rank.py FILE 1); ready-after-this: nglParseFontFile, nglParseDDSPalette, nglWriteTGA, nglUpdateFogTable,
nglBeginFrame, RE_DrawText (RE_DrawTextExt first); still skipped: zlib/deflate, nglReadFile and
nglSwizzleTextureLevels (file I/O / D3D getters), debug overlays.

**Earlier (decomp, 2026-10-21, late night): renderer bottom-up, third batch.** 33 more, all matched, none
mismatched: ShaderCache_FindOrAdd (the shader cache 0x00902ac0 is a skip list keyed by NglHashedName, returns the
existing node or null after inserting) + ShaderCache_RandomLevel (was FUN_00093c30), Mem_AlignedAlloc (new
qcommon module), nglGetTextExtent, nglBeginSceneRender (SetViewport and Clear now recorded calls),
nglInitRenderStates, nglFreeSceneRenderTargets, nglFreeFogTextures (mostly inconclusive: Release stand-in), and
the 22 shader-class Register functions (vtable slot 0; GetName is slot 1). Register runs once at startup in a
burst: test it with `compare_batch.py NAMES --only --every 1 --missions 1 --seconds 45` four names at a time
(4 tests at once). Trivial stubs nglIndexBuffer_Unlock / nglShader_Nop cannot be hooked (ret in the first 5
bytes), WaitTilOutputInst1_str_VFunc2 not reached. compiler.h: x87 helpers mul_mul_add, unsigned_mul_mul,
add_mul_mul_add_ftol, round_to_int. Next: rebuild the bottom-up list (167 renderer functions left) - skipped so
far: nglDrawFrameTimeBar (debug overlay), nglRenderTextNode (BeginPush/EndPush need a stand-in), nglDumpPrintf,
nglAssemble*Shader.

**Earlier (decomp, 2026-10-21, late night): renderer bottom-up, second batch.** Ten more whose callees
are all ours (bottom-up list rebuilt: 34 ready, 19 of them zlib/deflate - skipped, not used in --level runs):
nglSetDefaultSceneParams, nglSetDefaultRenderStates / nglRestoreRenderStates (the state cache sent to D3D,
inlined setters write D3D's shadow table and dirty bits in title memory), nglInitShaderConstants,
nglBuildTextGlyphs (text -> runs of glyphs, control codes 1/2/3/9/10/32; new NglTextRun), nglQuadBatch_SetMaterial,
nglNode_GetWorldViewRotation (cdGlass_Draw now calls ours), nglGenerateRenderTargetMips,
nglReleaseTextureSurfaces, strcmpcasenosensitive_internal. 7 matched, none mismatched; not reached in
missions 1/4/5/6: RestoreRenderStates, GenerateRenderTargetMips, strcmpcase; ReleaseTextureSurfaces only
inconclusive (Release stand-in: count reaching 0). Harness: recorded-call sizes `:aK*aM` (count x stride),
DrawVerticesUP recorded; compiler.h platform_x87_mul_add. Skipped: RE_RenderScene (drives R_RenderView,
which still mismatches on stale stack), nglMatrixOrthoOffCenter and nglUnprojectPoint (no callers),
nglWaitFrameInterval/nglReadBackBufferSize (frame pacing patch, GetDesc read-back). Next: the 85
1-pending-callee renderer functions (rebuild the list: progress.py --module renderer --state named ->
CalleeNames.java -> bottomup_rank.py), then the shader classes' startup slots.

**Earlier (decomp, 2026-10-21, night): renderer decomp resumed** (naming is done for now: hubs
dry, naming.md). Fixed the cdWorld_Draw/cdWorldBlend_Draw mismatch (word 54 = constant 18.w): the light
vector's w is the light record's +0x24 (point-light range, 1e24 directional) that nglLightVectorWorld
returns, not light->flags, and the record is 0x28 bytes (our out[4] overflowed); 30,390 tests since, none
mismatched, old rows reset to the post-fix counts. Then 30 renderer functions whose callees were all done
(bottom-up list: CalleeNames.java over `progress.py --module renderer --state named`, then count callees
still named/unnamed): NGL texture/vertex-buffer/palette/render-target wrappers, nglListAddTextf,
nglQuadBatch_Draw, the FX by-id helpers and fading components, a new stream module (MemStream_*,
Stream_Read*: a read that writes nothing returns the original's PUSH ECX slot = the stream pointer).
22 matched, none mismatched. Recorded calls added (loader/patches.cpp): SetRenderTarget(Fast),
BlockUntilIdle, BlockUntilNotBusy, D3DResource_Register. Still inconclusive: nglLockVertexBuffer,
nglTexture_Lock (Lock calls need a stand-in), nglVertexBuffer_Free (XPhysicalProtect). Next: the
renderer's 1-pending-callee list (170) bottom-up, and the shader classes' startup slots (109 untested).

**Earlier (decomp, 2026-10-21, evening):** script container templates (hash_map/list/vector/DynArray)
named in .text and their level-section copies by code shape, functor dtors, drone/mortar/scripted-anim clusters
and ~120 small script leaves up to 0x1b4000 (naming.md "Containers and script-range pass"). Next: the rest of
0x1b4000-0x1e4000, then the level sections. Counter: named 7005/16503.

**Earlier (decomp, 2026-10-21, late night):** script functors named from their RTTI (thread-start
helpers, ctors, invoke/dtor slots; 176 functions created) and the anim-handle tables (naming.md "Functor RTTI
and anim handles"). Next: the rest of 0x120000-0x1e4000, the anim-handle globals, then accessor templates
over the level sections. Counter: named 6032/16460.

**Earlier (decomp, 2026-10-21, night):** anim-script mains from their name hashes, 881 missing
functions created in the script range, 399 field accessors by body template (naming.md "Uncovered code and
hash words"). Next: the rest of 0x120000-0x1e4000, then accessor templates over the level sections.
Counter: named 5129/16284.

**Earlier (decomp, 2026-10-21, late):** compiled common scripts from callers and field/value hashes. Counter: named 4653/15399.

**Earlier (decomp, 2026-10-21, later):** open script slots named (152 functions + 2 globals). Counter: named 2990/15260.

**Earlier (decomp, 2026-10-21):** script builtin slot bodies named (137 functions + 100 slot
pointers; naming.md "Slot bodies, third pass"). Counter: named 2838/15191.

**Earlier (decomp, 2026-10-20, late night):** address-order pass over the engine range
0x1fa000-0x2f6d30 done (126 names; naming.md "Engine range, address-order pass"). Next: the
script builtin bodies 0x2f4be0-0x30c000 by slot (SlotStores.java on the init functions), then
the front end 0x30c000-0x340000 beyond its string-anchored functions (32 named: bonus screens, loading quote, hud setup, pause, profile settings), then the streaming loader (77: one load handler per file extension, load/unload states, per-zone resource managers). Counter: named 2701/15191.

**Earlier (decomp, 2026-10-20, night):** engine range 0x1fa000-0x30c000 named hub-first
(the skill's `HubRank.java` ranks unnamed functions by distinct callers): every function with
3+ callers that had a readable role, plus the string-anchored ones (95 names; naming.md
"Engine hubs"). Next: the remaining ~1,450 unnamed in the range have 1-2 callers - read them
in address order with DumpUnnamed (now that the hubs are named the dumps read better), then
the front end 0x30c000-0x340000 (bonus screens, MEM_* dialogs). Counter: named 2384/15171.

**Earlier (decomp, 2026-10-20, late):** the script interface's field accessors
(slots 590-827) and 180 builtin slots by body are named (naming.md "Script engine
interface"; the skill's `DecompToFile.java` dumps many decompiles to one file). Next: the
engine range 0x1fa000-0x340000 in address order (it also covers the open builtin slots'
callees). Counter: named 2289/15171.

**Earlier (decomp, 2026-10-20):** back on naming (step 1) before more rewriting
(user, 2026-10-20). Microsoft library code is now named by signature (3008 functions,
tools/data/library_names.csv; still counted in the total); next: the engine range
0x1fa000-0x340000 in address order, then the Treyarch libraries and the front end; the
compiled common scripts last (no bulk lever yet). Details in [naming.md](naming.md), "Next".

**Resume here (decomp, 2026-10-18, end of session):** naming is step 1 above
([naming.md](naming.md), "Next"). The game module is named, AI included (a
handful of low-value AI helpers stay open, listed in naming.md); next is the
cgame from CG_DrawActiveFrame (done except ~40 low-value leftovers) and the XAnim
tree module (core named); the client/UI is in progress (Q3 client paths, CL_Frame's
per-frame subsystems, the front-end panel/screen setup named; next: the screen classes'
vtables, memory-unit dialogs, the streaming loader - see naming.md). Names are neutral and
descriptive (user, 2026-10-18): no identifiers from later titles of the series.
**Next module (user decision 2026-10-18): the renderer**, ahead of the rest of the
client/UI. There are requests for a recomp, and the renderer is needed for the decomp
anyway: it is the game code tied to the hardware (NV2A commands and inlined D3D, now
translated by render/), so a decompiled renderer that calls a PC graphics API serves
both. Scope: the NGL scene code (~0x0005d000-0x00066000), the RE_* refexport side and the
inlined render-state code. Start: measure its size and list its entry points, then
name -> decompile -> --compare (screenshots), replaced as one module.
Started 2026-10-18: the tr_* layer, NGL (frame, scenes, quads, meshes, textures, fonts,
states), the 23 shader classes, lighting, models, effects, NGL's matrices, fog pass, light sets and
mesh builder are named (third pass 2026-10-18); open items in naming.md (Renderer).
Decompiling began 2026-10-18: game/code/ngl.cpp (quad setters and rotate/scale, scene
setters, matrix builders, nglDepthToZValue, light sets, frame allocators, texture helpers,
mesh-builder bounds; second batch: scene depth/time setters, pre-swap/end-frame
callbacks, the mesh (file) manager setters/getters and mesh walk, mesh relocation,
shader-handle and index-buffer helpers, nglEvalPointLight, the draw lists' std::sort
helpers), game/code/tr_main.cpp (myGLMultMatrix, RE_ClearScene, R_ClearFrameLists, far
cull, GLimp_Init, Q_PrintStrlen, RE_LoadWorldMap, R_AddViewModelRefEntity, R_CullPortal,
R_SetupProjection, R_CopyFrustumPlanes, R_ClipPolygonToPlane), game/code/dobj.cpp,
game/code/fx.cpp and game/code/unzip.cpp (inflate_trees_fixed); 86 functions matched,
none mismatched. Third batch (2026-10-18): nglSkipList_Find, nglMeshFileFindMesh,
nglVideoMode_Init/Copy, nglResetFrameStats, nglTextureSetPixel, nglSetTextureAddressMode,
nglLightSetAddProjector, nglGetTargetSampleScale, nglInitShaderConstantTable,
R_AddCellEntities, FX_FindEvictableEffect, XModel_GetBoneCount, nglSelectMeshLod,
nglScaleMeshMatrix; 101 matched in all. Fourth batch (2026-10-18): the big leaves -
DObj_TraceBoneBoxes (0x0023a590, bullet traces against bone hit boxes; its sqrt test
runs on the x87: platform_x87_sub_sqrt_ge), nglBuildProjectorPlanes (0x00070b00),
nglMatrixSceneClipToScreen (packed DIVPS lanes kept for MXCSR's invalid flag:
platform_keep), inflate_fast; 103 matched in all.
Fifth batch (2026-10-18), the first callers: the scene camera/viewport setters
(nglSetSceneViewportRect, nglSetViewportRect, nglSetAspect/Perspective/Orthographic,
nglSetViewMatrix, nglSetCameraMatrix, nglGetViewMatrix), nglListAddQuad,
nglAddDirectionalLight, nglEvalFirstObjectLight, R_SetupFog, RE_SetColor (MSVC _ftol:
platform_ftol), RE_StretchPic, RE_DrawQuadPic, RE_TextHeight; title functions not
decompiled yet are called through `title_function<F>(address)` (title_data.h), and a
hooked function reached inside a test process now runs its original straight from the
stub. 123 matched in all (recounted from functions.csv). Not reached: nglGetMatrixCopy,
nglProjectPointCopy, nglEvalLight, R_DrawPic.
Sixth batch (2026-10-18): nglSetViewportNormalized, nglGetMatrix, nglProjectPoint
(all four DIVPS/MULPS lanes kept), nglInitScene, nglPushScene (scene tree links +0x2d4..
+0x2dc, slot block +0x3d8), nglSphereInFrustum (0x00029200, newly named), nglAddPointLight,
R_ClearAlpha; 129 matched in all. nglProjectPoint (and the *Copy wrappers) not reached
by --level 1/4; R_ClearAlpha always inconclusive (its last scene's nglSetDefaultSceneParams
calls the replaced nglGetBackBufferTexture - host code); nglAddPointLight 23 of 23 so far.
nglUpdateScene (2026-10-18, ~1,900 instructions): viewport mapping, the field of view's
packed sine polynomial (cos/sin of half the angle), projection = base * viewport *
target scale, the camera (rigid inverse of the view matrix), view * projection,
clip-to-screen, the six world-space frustum planes (+0x270; x87 1/sqrt(v*v+1):
platform_x87_rcp_sqrt_sq_plus_one) and screen-to-target; 280 matched in missions 1 and 4,
none mismatched (130 matched in all). std::fabs is out of line at -O0 (the loader's copy
runs: "called host code") - use abs_bits.
Seventh batch (2026-10-18): the target scale matrix (nglMatrixTargetScale 0x00073ce0,
nglGetSceneTargetScale 0x00073e90; nglUpdateScene now calls ours, re-checked with
--compare-all: 1,229 matched), nglAddShadowProjector (0x000704f0, RCPPS + Newton step:
platform_rcp_estimate), nglNode_ViewDepth, nglNode_CalcWorldMatrix, nglListAddNode (scene
+0x304..+0x310 opaque/translucent lists), nglMaterial_Relocate/Relocate2 and the resource
managers' find/add-ref/release wrappers (vtable slots 2/7/8); 143 matched in all.
nglReleaseMeshFile is always inconclusive (the release reaches the replaced D3D code),
nglFindFontAddRef not reached.
Eighth batch (2026-10-18): nglListAddMesh (0x00068850; the per-instance NglMeshDrawNode in
scene memory: world matrix, world * view_projection transposed, LOD, frustum cull, each
section's shader slot 2), nglComputeObjectLights (0x00070200, four object lights in object
space + colours + ambient), nglNode_GetWorldInverse (0x00049750, formerly misnamed
GetWorldViewProj: the cached rigid inverse), nglSetDecalZBias (x87 unsigned-to-float:
platform_x87_unsigned_to_float; inconclusive whenever it sends a render state, which
reaches the replaced SetRenderState_Simple), nglMaterialFindTexture(OrDefault) and the
eight nglMaterial_* load/release slots; 154 matched in all. Not tested yet:
nglMaterial_LoadTextureRef, nglMaterial_ReleaseTwoTextures(Clear) (no tested calls in
missions 1/4). functions.csv is now written under a lock with a per-process temporary
file, so --compare runs of several missions can run at once (a shared .tmp had
interleaved two runs' writes).
Not reached by --level 1/2/4 runs: nglColorToVec4, nglPaletteSetEntry, RE_GetFarCullDist,
Q_PrintStrlen, nglScaleQuadVerts, nglRotateQuad, std_PushHeap_* (heap sort fallback
only), nglIndexBuffer_Clear, inflate_trees_fixed, FX_FindEvictableEffect (pool never
full), XModel_GetBoneCount. x87 sin/cos/tan go through platform_x87_* and RSQRTSS through
platform_rsqrt_estimate (compiler.h). `--compare` runs without the PC features
(`--pc-patches off`). Skipped on purpose:
nglDynamicVB_EndFrame (tail calls into undecompiled code), DObj_AnyModelHasFlags (flags
in EDI - needs an asm entry shim; also R_AddCellEntityLink, 0x0023b2e0 in ESI),
nglSelectMeshLightSet, 0x0023be90 and huft_build (paths copy uninitialized stack
slots; huft_build also takes EAX/ECX), nglWaitFrameInterval (busy-waits on the vblank
thread's counter),
0x00235890 (radius in XMM2), R_LayoutTextWithIcons (colour out-pointer in EBX), nglResourceManager_Find (its tail JMP sits in the first 5
bytes), 1-4 byte functions (too short for the harness's jump).
Ninth batch (2026-10-19): the shader classes' small slots, game/code/shaders.cpp - Toggle,
Init, GetName and AddToList of all 22 classes (AddToList: a 0x18-byte NglMeshNode in scene
memory onto the opaque list keyed by the class id, or the translucent list by
nglNode_ViewDepth; decal ids | bit 31, water/ocean/river fixed keys, sky 1000 - layer,
cdWorld/cdWorldBlend cull the section's sphere first), Register's 15 shader-table loops
(newly named cd*_InitVertex/PixelShaders) and cdWorldBlend_Load/ReleaseTextures,
cdGlass_LoadTextures (107 functions). Missions 1-5: 26 tested, all matched (181 matched in
all). The Init functions cannot be hooked (CALL in the first 5 bytes; startup only); the
other startup calls (GetName, the loops) mostly hit "all busy" in the startup burst; Toggle
only runs from nglToggleShaderByName. Not reached in missions 1-5: the AddToList of cdGun,
cdDebug, cdRiver, cdOcean, cdWater, cdWorldVertexLit.
Tenth batch (2026-10-19): test processes now record the D3D replacements' calls
instead of stopping there ([comparison harness](comparison-harness.md), "Recorded SDK
calls"; 39 state setters and draws), so code that draws is testable - nglSetDecalZBias,
inconclusive in 2,018 tests before, matched 80 of 80 at once. Then NGL's state layer:
the render-state cache's eight setters (0x00902f08: alpha test/function/reference,
blend enable/source/destination/equation/colour; newly named nglStateCache_*),
nglSetBlendMode (modes 0..8), nglSetPixelShaderProgramCached, nglSetVertexShaderCached,
nglSetTexture (animated frames, references, SwitchTexture within a format, palette,
address mode), nglUnbindTextures, nglSetPaletteCached, nglSetVertexStreams,
nglDrawMeshSection, nglSetTransformConstants (fog parameters and the camera in object
space); 16 of 17 matched in missions 1 and 4 (133-202 tests each, none mismatched or
inconclusive; 197 matched in all). nglSetPaletteCached is not reached (no palettized
textures there). Next: the shader classes' Draw slots (0x00046900-0x00054ad0, 22
classes, 490-2,800 bytes; they inline the state cache's tests and call the helpers
above), then their Register slots (startup only).
Eleventh batch (2026-10-19): the first shader Draw slots in
game/code/shaders.cpp with shared helpers (cull mode, scene fog colour/enable, WVP
constants 6..9, object lights 6..17, stage-0 texture, blend-cache reset): cdWorldVertexLit,
cdBackground, cdSimplePrelit, cdPrelit (fog off, optional stage-1 texture), ShaderPCUV
(decal bias from the instance parameters), cdGunSight/cdGun/cdSimple (one lit template),
plus nglSetTextureAnimFrame (0x000476b0, newly named); all reached ones matched in
missions 1/4 (cdSimple 201, cdSimplePrelit 200, cdGunSight 143, cdBackground 42,
ShaderPCUV 24); cdGun, cdPrelit, cdWorldVertexLit not reached there - try missions 2/3/5.
Twelfth batch (2026-10-19), the remaining shader Draw slots (read with the skill's
`xbefunc.py`, no Ghidra session): cdChar (skinning constants through the title's
nglSetSkinningConstants), cdSimpleUVAnim (+ cdSimpleUVAnim_GetUVParams 0x0004d560, newly
named), cdDebug, cdDecal, cdWater, cdOcean (shaders from the node class's vtable;
cdOcean_SetWaveConstants 0x0004f810 newly named, called through title_function),
cdPropeller, cdWorld and cdWorldBlend (one draw_world with per-class textures: lights,
the first shadow projector, the dynamic-light passes), cdAirplaneMetal, cdSky (x87
angle: platform_x87_sin_cos_of_product), cdGlass (nglNode_GetWorldViewRotation
0x00049280, newly named) and nglDebugShader_Draw; packed lane math moved to
game/code/lanes.h. Several Draws send constant lanes the title never sets: test
processes now zero the dead stack below the entry ESP ([comparison
harness](comparison-harness.md)), and the decomp writes zeros there (planted check: without
it cdWorld_Draw mismatched 839 of 1,344). Missions 1-12: 16 of 21 Draws reached, all
matched, none mismatched (cdWorld 1,081, cdChar 645, cdSky 608, cdWorldBlend 535, cdDecal
514, cdGlass 349, cdSimpleUVAnim 287, cdPropeller 203, cdAirplaneMetal 124, cdOcean 100);
219 matched in all. Not reached in 1-12: cdGun, cdWater, cdDebug, cdWorldVertexLit,
nglDebugShader (their AddToList slots are not reached either). The three new names are applied in Ghidra and exported.
Thirteenth batch (2026-10-19): register-argument entries - `PLATFORM_ASM_ENTRY` (platform/compiler.h)
defines a title-convention entry in assembly that moves ESI/EDI/EBX/XMM arguments to the
stack and calls an extern "C" hidden C version ([comparison harness](comparison-harness.md),
"Register arguments"). With it: cdSimpleAlpha_Draw (0x00049e40, the last Draw: material or
instance colour, debug override 0x0089aff0, depth pre-pass, blend modes) with
cdSimpleAlpha_PulseColor (0x00049cd0: EAX/ECX + XMM1, newly named) and
cdSimpleAlpha_ClampDebugPulse (0x00049c30), nglStateCache_SetFogEnable (0x00046d50),
DObj_AnyModelHasFlags (EDI), R_DrawFilledRect (ESI colour), R_CullPointAndRadius (XMM2,
0x00235890), R_SetInstanceColor (ESI, 0x0023b2e0); with the zeroed dead stack:
nglSelectMeshLightSet (0x0006fe00; the Draws call ours now) and DObj_GetBounds (0x0023be90).
Missions 1-5: 7 matched (nglSelectMeshLightSet 404, DObj_AnyModelHasFlags 400,
DObj_GetBounds 400, cdSimpleAlpha_Draw/PulseColor/SetFogEnable 139, R_DrawFilledRect 20
with Back held), none mismatched; 226 matched in all. A planted bug in PulseColor's XMM1
speed was caught (helper and Draw). Not reached: cdSimpleAlpha_ClampDebugPulse (debug
override only), R_CullPointAndRadius (its only caller 0x00235910 has no callers),
R_SetInstanceColor (the mesh-builder draw 0x002d0680, behind the jump stub 0x0001d0d6).
huft_build stays skipped (zlib is not used in --level runs).
Fourteenth batch (2026-10-19), the ESI/EDI callers in game/code/tr_main.cpp:
R_ApplyCachedLighting (ESI; RCachedLighting: ambient, three colours, three directions,
count), R_AddRefEntityToScene (ESI = refEntity, record starts 0x80 before it),
R_AddCellEntityLink (EDI/ECX/EDX; RCellEntityLink), R_AddStaticModel (EAX; RStaticModel:
cached lighting, light-grid lookup at the origin then raised twice, scale block, bone
matrices times the model's in packed lanes, nglListAddMesh per bone), R_RotateForEntity
(EAX/ESI/EDI; ROrientation), R_SetupEntityOrientation, R_LayoutTextWithIcons (EAX/EBX + 3
stack args). Missions 1-5: R_AddRefEntityToScene, R_AddCellEntityLink,
R_ApplyCachedLighting 300 matched each, R_AddStaticModel 108, none mismatched; 233 matched
in all. Not reached: R_SetupEntityOrientation and R_RotateForEntity (no callers at all -
dead code), R_LayoutTextWithIcons (only RE_DrawTextWithIcons/RE_TextWidthWithIcons through
the refexport table; not reached in missions 1-5 or the front end).
Fifteenth batch (2026-10-19): the rest of the draw lists' std::sort in game/code/ngl.cpp, one
template set over both node types (MSVC VC7 Dinkumware: _Adjust_heap, _Make_heap, sort_heap,
_Insertion_sort with _Rotate, _Median, _Unguarded_partition with its hidden-pointer pair,
_Sort as nglSortOpaqueNodes_r/nglSortTranslucentNodes_r; 14 functions). Missions 1/4/5: the
two sorts, both insertion sorts, both medians and both partitions matched (50-300 tests
each), none mismatched; 241 matched in all. The heap fallback (std_MakeHeap/SortHeap/
AdjustHeap/PushHeap) never runs: _Sort only takes it when `ideal` runs out.
Cell/lighting batch done (2026-10-19, tr_main.cpp): CM_CellForPoint, R_CellForViewOrigin,
R_SetDefaultLighting, R_GetCellLighting (R_AddStaticModel now calls ours), R_UnlinkEntityFromCells,
R_LinkEntityToCell (EDI entry), R_LinkEntityToCells_r, R_AddCellModels, R_AddCellSurfaces (ESI
entry), R_VisitCell_r; all 10 matched in missions 1/4/5, none mismatched.
Then R_ClipFrustumToPortal and R_ClipPortalWinding (the portal frustum: edge planes
through Q_rsqrt, bevels from the projected bounds with r_portalbevels 0.7; MSVC's 4x
unrolled loops sum in a different order in the first copy of each group, see
`unrolled_first`): both matched (200 tests), R_VisitCell_r calls ours.
View batch (2026-10-19, tr_main.cpp): R_SetupCullPlanes, R_AddWorldSurfaces (asm entry: its
0x2000-byte link buffer sits right below the return address and 0x0095e2d0 keeps that
address), R_SetupPortalBevels with MatrixInverse4x4 (0x00294610, newly named; its cofactor
sums lifted with the skill's scripts/ssesym.py), R_SetupFrustum, R_RotateForViewer,
R_AddEntitiesToCells, R_AddEntityToCells_r, R_AddEntitySurfaces: all matched (139-313 tests),
none mismatched. R_ClearAlpha is testable now (310 matched) through the harness's new
reference-count/back-buffer stand-ins. R_RenderView (0x00247350, the 3D view pass) is
decompiled and rebuilds the original's frame so that every callee runs at the original's
stack position (title_call_at); it matches with R_AddEntitySurfaces original, but with ours
~75% of its tests mismatch in mesh records that R_AddEntityModel's subtree (title code)
fills with permuted rotation values - most likely matrix lanes it never sets and takes from
stale stack (in the original, what nglGetMatrix left there). Expected to go away once
R_AddEntityModel / R_AddDObjSurfaces are ours (zero those lanes); until then R_RenderView
stays Compare with these mismatches on record.
DObj batch (2026-10-19, tr_main.cpp): R_AddDObjSurfaces (0x00244020, 6.4 KB: lighting,
LOD limits, rigid/skinned meshes, far-distance pull-in, entity scale, shadow copies) and
R_AddEntityModel (0x00245a20, EAX). Both entries rebuild the original's frame:
R_AddDObjSurfaces' two instance blocks (frame +0xf0 rigid, +0x130 skinned) are only partly
written and nglListAddMesh copies all 0x30 bytes, so they stay at the original addresses
with whatever is there; R_AddEntityModel calls it at the original's position
(title_call_at). Mission 4: R_AddDObjSurfaces 5,593, R_AddEntityModel 5,602,
R_AddEntitySurfaces (now calling both) 204 matched, none mismatched. Newly named:
DObj_SetMinLod, DObj_SetForcedLod, DObj_GetCoarsestLod, XModel_GetLodParts,
R_DrawDObjSkeleton. R_RenderView's remaining mismatches are only those unset block
fields (copied stack garbage): R_AddEntitySurfaces calls R_AddDObjSurfaces at two depths
0x120 apart (directly, and via R_AddEntityModel), so the direct call's unset fields hold
the via call's spilled matrix rows (frame +0x210..+0x27f), and the rest comes from
whatever else ran at that depth. Matching that needs every function in the subtree to
leave the same stack garbage (our C bodies and the 16 KB scrub after our calls do not):
left as known, R_RenderView stays Compare with them on record.
Lighting/draw-list batch (2026-10-19): R_SampleLightGrid (0x00242e90: bilinear 5-bit grid
colours on the x87, light references merged with x87 + _ftol weights -
platform_x87_mul_add_ftol -, the three strongest as directional lights; point lights
normalized with RSQRTSS + a Newton step) and R_SetupEntityLighting (EAX/EDI entry; timer
from the title's LCG on the x87: platform_x87_mul_div_add), both now called by ours
(R_AddStaticModel, R_AddDObjSurfaces); the LOD setters DObj_SetForcedLod/SetMinLod/SetLod
(EDI/ESI entry)/GetCoarsestLod and XModel_GetLodParts; NGL's draw-list walk
nglRenderOpaqueList/TranslucentList, nglRenderSceneTree, nglSetObjectLightConstants,
nglSetSkinningConstants (0x00076620, the packed A*B*W per bone, lifted with the skill's new
scripts/psssym.py), nglListAddText with nglAllocSceneMemory and nglListAddTranslucent
(newly named), nglListAddCustomNode; FX_SortEffects_r, FX_FreeFinishedEffects, FX_FreeEffect
(ESI entry; FX_ReleaseEffectHandle newly named). Missions 1/4/5/6, none mismatched:
R_SampleLightGrid 304, R_SetupEntityLighting 369, the LOD helpers 99-315, nglRenderOpaqueList
297, nglSetObjectLightConstants 300, nglSetSkinningConstants 291, nglListAddText 252,
FX_SortEffects_r 382, FX_FreeFinishedEffects 345. Known: nglRenderTranslucentList matches
except where a title shader Draw sends an uninitialized constant lane and picks up the
walker's dead stack (the original's sort-call return address 0x0007553c, or a host stack
address on our side) - unmatchable, left on record; nglRenderSceneTree is inconclusive
(SetRenderTarget and the push-buffer quads reach host code); nglListAddCustomNode cannot be
hooked (CALL in its first 5 bytes); FX_FreeEffect not reached. Then DObj_CalcBasePose (8
matched; few calls) and DObj_CalcAnimatedSkeleton (not reached) with XModel_CalcBoneChildren
newly named, RE_BeginFrame (always inconclusive: nglBeginFrame reaches a kernel call) with
nglAdvanceShaderTime and nglGetSceneFogColor (both newly named, 341 matched). Mismatch
reports now have their own cap of 5 (inconclusive ones no longer hide them).
**Resume here (2026-10-19, end of session):** first fix the new mismatch in cdWorld_Draw
(8 of 774 tests) and cdWorldBlend_Draw (5 of 216): only on rare calls (after call
~18,000), always SDK call D3DDevice_SetVertexShaderConstantNotInlineFast word 54 =
110.0 in the original vs 2.0 in ours - a path our Draw code misses. Seen in the first
--compare-all that reached gameplay (earlier runs had no A presses and stayed on "Press
A to start", so gameplay/AI code was never tested before; all else settled: 233).
Workflow tools now: tools/fnpack.py (context per function), tools/scaffold.py
(boilerplate), tools/compare_batch.py (runs + summary; `--report DIR` re-reads,
`XBCMP_ONLY` via `--only`). Then: more renderer callers bottom-up
(`tools/progress.py --module renderer --state named`), then the shader classes'
Register slots. Start parallel --compare runs ~5 s apart: three at once once
crashed DXVK at startup (VK_INCOMPLETE querying surface formats).
From now on (user, 2026-10-19), --compare batches run missions 1, 4, 5 and 6: mission 6
(Operation Husky) covers the water-related code (cdWater/cdOcean/cdRiver shaders and the like).
Not reached by --level 1/4 runs (zlib unused there): inflate_fast. Left among the leaves:
nglMatrixOrthoOffCenter, 0x000683a0 and 0x0006fb30 (no callers), nglInitRenderStates
(tail call). Next batch: more callers, bottom-up: nglListAddMesh's callers (R_AddStaticModel
0x002438b0 - EAX argument, calls R_ApplyCachedLighting (ESI) - and the other R_* model
adders; the debug shape draws 0x0006cb80/cc50/cd30 are reached only from nglDebugDrawLine/Axes),
the shader classes' Register and Draw slots, the tr_* callers of R_SetupProjection;
nglSetTransformConstants (0x000776f0) ends in the replaced SetVertexShaderConstant; nglSetDefaultSceneParams (0x00062a70) is testable now that
nglGetBackBufferTexture's D3D calls have stand-ins. ESI-argument functions (R_AddRefEntityToScene,
R_ApplyCachedLighting) need an asm entry shim. Needs an asm
shim: R_SetupEntityOrientation (EAX/ECX/EDX in, calls R_RotateForEntity with ESI/EDI),
R_DrawFilledRect (colour pointer in ESI).
Each batch: CSV rows -> ApplyNames -> save -> ExportSymbols ->
tools/smoke_test.sh -> tools/progress.py (refreshes tools/data/progress.csv: every
function's section, module and state; `--module renderer` lists what is left, e.g.
`--state untested` for decompiled functions no run has reached yet; modules are name
prefixes and address ranges in tools/data/modules.csv - add ranges as modules get
mapped, 7,900 .text functions are still "unassigned") -> commit. User decision 2026-10-17: decompilation only, no
static recompilation.

**Resume here (loader, 2026-10-17, end of session):** v0.1.1 is out (private pre-release,
Linux + Windows): 60 fps by default and system-link multiplayer from the campaign's
Multiplayer item or the launcher's button, with mouse look, style remap, 60 fps,
widescreen menus and aim assist in MP too (user-tested on one PC; design and open
items in [multiplayer.md](multiplayer.md)). Since then: v0.1.2 (private test pre-release)
adds sharper text (all four game fonts redrawn from bundled OFL fonts; launcher
toggle, default on); not yet user-playtested. v0.1.3 (private test pre-release) adds
the mouse in the menus (campaign and multiplayer); v0.1.4 (private test pre-release):
"Press A to start the game" after loading takes a click too (in-game screen 5, ready flag +0x38;
user-tested); v0.1.5 (test pre-release, 2026-10-20): keyboard/mouse button prompts,
clickable option rows (arrows, sliders), a Language setting (issues #1-#4); v0.1.6
(test pre-release, 2026-10-21): system Direct3D 9 diagnostics and a software vertex
processing fallback (issue #6, Intel HD 3000: every CreateVertexShader failed, black
menu), the player log capped at 8 MB, the launcher's update check (GitHub releases).
v0.1.7 (test pre-release, 2026-10-21): the issue #6 cause (the vertex shader epilogue read c192 and c193 in one
mad; Windows' D3D9 runtime allows one constant register per vs_3_0 instruction, DXVK
does not check), the launcher's Update now (issue #7, launcher/updater.h; tested
against the v0.1.6 Linux archive, not yet on real Windows). v0.1.8 (test pre-release, 2026-10-23,
issue #8, Intel HD 3000, drops from 30 to 20 fps): pixel shaders also keep one
constant register per instruction (C0/C1 inputs failed CreatePixelShader on Windows'
D3D9); the profiler works on Windows (loader/profiler_win32.cpp: SuspendThread +
GetThreadContext on threads whose QueryThreadCycleTime moved; Wine lacks that call, so
there every thread is sampled), `--profile` and the launcher's "Measure performance";
tools/resolve_profile.py names our offsets from the unstripped build - keep each
release's build directory binaries for that. Windows side tested under Wine only.
The launcher has tabs (General, Graphics, Controls, Multiplayer, Help).
v0.2.2 (2026-11-01): Windows starts when something (likely a DLL injected at its default base) sits in 0x10000000-0x50000000 (issue #16): the kernel takes another free range, the stub imports only kernel32, the log names what occupies an unheld range. v0.2.1 (2026-10-31): the arrow keys, Enter and Backspace can be bound (issue #14); in menus they still navigate. v0.1.14 (test pre-release, 2026-10-28): key bindings in the launcher (issue #14) and variables after a library's code in the release map (issue #9). v0.1.13 (test pre-release, 2026-10-27): the release map hashes code by instruction (issue #9, below). v0.1.12 (test pre-release, 2026-10-26): other releases through the release map (issue #9, below). v0.1.11 (test pre-release, 2026-10-25): everything below up to v0.1.10. After 0.1.10 (issue #8, eight Windows/Intel HD 3000 logs; the 0.1.11 logs showed both cores busy with no hot spot left: answered 2026-10-26 with the 30 fps advice): the
automatic profile ran once per run, right after the level start, never at the spots
reported; it now reports at each slow second (at most once a minute, 20 times a run)
with the mission number. Main thread there: busy only 41-60% at 20-27 fps, 15-25% of
its samples in driver Lock/Unlock/Render/waits (every draw was DrawPrimitiveUP), 3-13%
in texture_for_stage (whole-texture hash of every bound texture every frame). Now:
draws fill our own dynamic vertex/index buffers (NOOVERWRITE, DISCARD on wrap; UP
remains the fallback), and textures are sample-hashed each frame, whole only every 8
frames or while they changed in the last 60. Linux/DXVK A/B (missions 1 and 9, fixed
10 s window): texture_for_stage 11-13% -> under 2% of main-thread samples, memcpy
roughly halved, pictures unchanged. Windows' own D3D9 not measured (Wine untested so
far). Also: the PC menu's ImGui init asserted in Linux debug builds (InitForD3D).
`tools/resolve_profile.py --summary` sums each report per place. Re-created on one core
shared with two busy loops (mission 9): land ~28 fps, sea 60, land through binoculars 57;
F12 draw dumps: a land frame ~800 draws / 100k vertices / 7 MB converted, sea ~170 / 30k
/ 1.7 MB. Now static vertex sources (unchanged for 3 frames; sample hash per frame, whole
every 8 or while recently changed, as textures) stay converted in their own GPU buffers
(256 MB, LRU, unused 600 frames go), and the rest converts straight into the locked draw
buffer. A/B vs 0.1.10 (missions 1, 9, fixed window): draw_streams 6.5-8.5% -> 1.3-1.9%,
memcpy 7.6-8.5% -> 2.5%, main thread busy 55-67% -> 47-48%, pictures unchanged. Next
idea if the draws still dominate: vertices in their real types instead of a float4 per
attribute.
v0.1.10 (test pre-release, 2026-10-25): the release check by code fingerprint, other releases' campaigns start (issue #9). v0.1.9 (test pre-release, 2026-10-24) has everything below. After 0.1.8 (issue #8 logs): the frame end's spin on the vblank flag (SP 0x000651c3, MP
0x00066123) sleeps until the next vertical blank instead (one core less: 102 s -> 51 s
CPU in 100 s of mission 1 at 30 fps; Linux, Wine checked); late frames are shown at once
(the game's own mode 3/4 flag; `--late-frames wait` = the Xbox's 20 fps step); the
Windows profiler's busy is the thread's cycles, and it samples only threads that ran
a quarter of the tick (0.1.8 reported every thread at 100%). Not yet on real Windows.
Issue #9: the reporter's codmp_xboxf.xbe (fr/es/it disc) is another build (.rdata
0x20 later, RDTSC sites from 0x2074fa on differ), so every MP address misses. The loader
now refuses other builds (`--any-release`). After 0.1.9: the check compared the header time stamp and size, which also refused the EU French/Spanish/Italian campaign XBE that ran fine on 0.1.8 (issue #9 comments); it now compares a fingerprint of the section table (names, addresses, sizes, header SHA-1 digests of code sections; data sections by place and size only) and logs every section on a mismatch. Another campaign XBE starts anyway with a warning (multiplayer stays refused without `--any-release`). Not yet confirmed on that disc.
Issue #9, after 0.1.11: other releases through a release map (loader/release_map.h).
On a build with another fingerprint the loader matches the supported build's functions
(tools/data/<xbe>_signatures.txt: hashes of each function's first 32 bytes and of all
of it, with rel32 targets and in-image addresses masked) at every byte of the other
build's code sections; then neighbours' offsets for the rest. Data addresses come from
the same code there: each variable the code refers to has one recorded site, read in
the other build; addresses between two known variables that moved alike move alike.
Every title address in patches.cpp/main.cpp goes through title_address(); the symbol
table is relocated. Unfound addresses are logged; multiplayer then refuses to start
(`--any-release` overrides). Tested on a Collector's Edition dump (testing only; kept
outside the repo in cod_bro/Collectors editions/): its campaign code sections equal
ours (only .rdata and the title image differ, so the fingerprint matches); its
multiplayer has identical code but part of .data (0x49c790-0x49e7b0) in another order
(code references -0x68 / +0x1e98): 11490/11493 functions unchanged, all port
addresses found, the menus work. The EU multiplayer XBE (issue #9 logs) has the CE's
.rdata/.data sizes, .text 0x30 longer and everything after it 0x20 later; its campaign
equals the CE's except the LIBEATOR section (same size). The map costs 0.27 s (MP, release build) per start of another build.
After 0.1.12 (issue #9, EU MP log 2026-10-27): 9899/11493 unchanged, 1094 entry only, 500
missing, 31 port addresses missed (Direct3D_CreateDevice among them). Cause: the hash
masked any 4 bytes that read as an image address, left to right, so a window across
two instructions (85 C9 C7 06 00 = test ecx,ecx; mov [esi],0x003bc800 reads 0x0006c7c9)
hid the real address after it, and once the data moved the masks differed. Library code
(D3D/DSOUND/XNET, same in SP and MP at other addresses): 102 of 375 functions differed.
Now the hash decodes x86 instructions (release_map.cpp `decode`) and masks rel32 and 32-bit
operands that read as addresses, plus jump-table dwords: 14 of 375 differ (constants
inside one image's range only). A head match must land in the section of the same name,
and functions still unmatched are tried at their section's offset (the MP_* map sections
are code alike, so their heads are not unique; 0.1.12's odd MP_* offsets in the EU log were
matches into other map sections). CE MP: 11493/11493. Signatures regenerated. Map now
0.8 s (MP, release build).
After 0.1.13 (EU MP log 2026-10-28): 11468 unchanged, 18 entry only, 7 missing; only 3
port addresses missed: D3D device pointer/render states/texture states (0x32a358,
0x32a368, 0x327ca8). The SDK libraries keep their variables after their code in the
same section (D3D 0x3262d0-0x32b77c is one "function" symbol of data, changed there), and
data references were only recorded for data sections. Now references to any in-image
address are recorded; an address inside a changed code-section symbol (not its start)
is looked up like data, bounded by that symbol. Their sites are in .text that the EU
log shows unchanged (+0x0). Signatures regenerated (MP 19010 / SP 44805 references). CE
MP still 11493/11493, all found. EU not yet confirmed by the reporters.
Issue #14 (2026-10-28, for 0.1.14): key bindings. input/bindings.h: 22 actions (the
default layout's pad controls + move/look keys), 2 inputs each (keys, mouse 1-5, wheel
up/down/either); settings.ini `keys = action=input ...` (only changed actions) ->
`--keys`; sdl_devices reads them, then the style remap as before; key prompts show each
action's first input. Launcher Controls tab: table, click + press, right-click clears,
a key moves from the action that had it; menu keys/Esc/Tab/F10-F12 fixed.
bindings_test. User-tested (melee on mouse 5). 0.1.14 published 2026-10-28; issues #9/#14 answered.
Open: under Wine, the Windows ctest's kernel tests (audio, critical_section, dispatcher,
filesystem, memory) fail at start with "cannot reserve contiguous memory at 80000000"
in both Windows presets (the packaged loader runs fine: its stub reserves the ranges
first). Not caused by 0.1.14's changes (no xbox/ code touched); not yet checked
whether it predates them or comes from the Wine version.
Issue #10: pictures wider than 16:9 (`--aspect 21:9` etc., launcher choices marked
experimental), campaign only, see docs/rendering.md "Wider than 16:9". The PC menu (`
key, Dear ImGui in-game) with a field of view slider (campaign; issue #5), docs/rendering.md.
Next candidates:
1. Multiplayer between two PCs, and v0.1.1 on real Windows (firewall prompt).
2. The full native build plan + a list of every game-code patch and its intent (asked for).
3. Heavy fights at 60 fps (mission 5 town), missions 7-13 playtests.
4. MP: split screen (if any), the ~40 startup memory warnings. One release-build MP
   sample read 30 fps in-match once (not reproduced; log not kept).

## Current state (2026-10-16)

- Mouse in the menus (2026-10-18), campaign and multiplayer: hovering selects an item,
  left click selects it and presses A, right click B, the wheel moves up/down; the
  cursor is free while a menu is up and the mouse is captured again when it closes.
  The UI's managers (SP front end 0x0095f8f8, in-game 0x0095f8fc, dialogs 0x0095f900;
  MP 0x004b31f8 / 0x004b3200) poll the pad each frame (FUN_0031fc10, MP
  FUN_0028d080); a thunk there hit-tests the active screen's items against their
  text widgets' boxes (only widgets drawn in the last frames, hooked at the text draw
  FUN_00321160 / MP FUN_0028e5d0) and selects through the screen's own vtable. Layout
  and offsets in loader/patches.cpp ("Menus with the mouse"). Checked unattended with
  scripted pointer positions (boxes on screenshots: profile manager, main menu, dialog,
  pause menu in 16:9, MP menus). Played with a real mouse (2026-10-20, issues #2/#3):
  option rows take clicks right of their label too - left/right of the value's middle
  (the texts on the row: value and front-end arrows; sliders: the other rows' values,
  or halfway to the screen edge for a lone one) tap left/right; a click on a screen
  that is still locked waits up to 30 frames. `--debug` logs every menu click and
  what it hit.

- Keyboard button prompts (2026-10-20, issue #4): `--button-prompts auto|keyboard|
  controller` (launcher "Button prompts", default auto = the last used device). The
  icon font i_button_icons_xb_<en|fr|it|sp> (.dds/.fdf, glyphs 33-49: RT A X Y B LT
  white black, D-pad up/right/left/down, start, left-right, up-down, D-pad, back) and
  Xbox_A/B/X/YButton.dds (menu footers) are redrawn as keycaps/mouse pictures
  (text/key_prompts.cpp) with the keys of the active controller style (inverse of the
  keyboard remap; in menus Enter/Bksp for A/B); BackendConfig::replacement_version
  makes the renderer re-ask when the labels change. User-tested: switching between
  keyboard and controller pictures works.

- Language (2026-10-20, issue #1): `--language english|german|french|spanish|italian`
  (launcher "Language") sets XC_LANGUAGE. The title takes its language from the console
  and the disc (FUN_0028cbd0: a "de" disc is German, an "eu" disc French/Spanish/
  Italian by console language, English console -> French); the North American Standard Edition disc is English
  only (a French console still shows English). The log lists the disc's text languages
  (Global.<code>.stb).

- Sharper text (2026-10-17): `--sharp-text on|off` (default on; launcher "Sharper
  text"). The game's text is four bitmap fonts, each a DDS atlas + a plain-text
  `.fdf` glyph table in the packs (sp/global.cod: garamond 512x256 DXT1,
  GemFontOne 256x256 DXT5, arialblack_14 256x256 DXT1 (a bold Times); sp/frontend.cod:
  impact 256x256 DXT1 with mipmaps; mp/ has identical copies). All are white with
  on/off alpha; all but impact have a 1-pixel dark shadow to the lower right inside
  the glyph box. `text/` reads the packs (format in text/kapf.h), redraws each atlas
  at max(2, ceil(render scale)) from the OFL stand-ins in fonts/, every glyph scaled
  into its original ink box (the game's spacing stays), and the renderer swaps it in
  by content hash (BackendConfig::replace_texture). GARA.TTF is a regular weight and
  only feeds XFONT (Xbox Live UI), so it is not used. Checked in screenshots at
  render scale 3: menus, dialogs, mission title cards, briefing hints; all four fonts
  are replaced in a menu-to-mission run (Linux, and Wine for the Windows build).
  Not swapped: the button icon fonts, the legal screen (an image).

- **Prototype release:** private GitHub pre-release v0.1.0 (2026-10-16), Linux
  tar.gz + Windows zip; license GPL-2.0-or-later. Before going public:
  1. ~~Fullscreen: check Alt+Enter and a resized window.~~ Both work (user, 2026-10-16).
  2. ~~A run of the Windows zip on real Windows.~~ Runs (2026-10-16, RTX 3080).
- **After the release:**
  - XFONT (the XOnline sign-in UI's text) writes through the frame buffer's tiled
    view (address | 0xF0000000), which is not mapped; only reachable through Sign In,
    which is cut off now.
  - Windows (2026-10-16): runs on real Windows (RTX 3080; Alt+Enter and resizing
    confirmed) and under Wine. cod_bro_loader.exe is a stub at 0x10000 that holds
    the Xbox ranges and loads cod_bro_loader_core.dll; FS stays the TEB
    (StackBase/StackLimit switched to the Xbox stack, 36 KPCR-field reads patched to
    TEB TLS slots); `os::keep_executable_headers` keeps valid PE headers at 0x10000
    (real Windows reads them for CreateThread's stack sizes; Wine does not). D3D9 is
    loaded at run time: bundled DXVK (`dxvk/d3d9.dll`) first, the system's as the
    fallback (`--d3d9`). Every run writes `log.txt` in the user data folder; error
    boxes on startup failures, crashes and uncaught C++ exceptions. Untested:
    non-ASCII paths (narrow strings use the ANSI code page).
  - Mission 6+ playtests; death/mission-failed screens and dialogs in widescreen.
  - ~~Launcher: a .desktop file / icon for Linux desktops.~~ Done (0.2.2): the
    supplied icon is embedded in Windows PE resources and baked into
    `platform/window_icon.cpp` for SDL windows on both platforms; Linux releases
    ship `cod_bro.png`, a `.desktop` template and `install-linux-icon.sh`.
- Mission 6 (Gela) slowdown (2026-10-17): ~20 fps for ~13 s in the opening
  landing-craft cutscene, only in the Debug build (`linux-loader`, -O0) that the
  playtests used: the title's render thread was CPU-bound in our unoptimized draw
  translation (vertex fetch `draw_streams`, texture `hash_bytes`, std::fill, TLS
  lookups). The Release build (`linux-loader-release`, as players get) holds 30 fps
  there. Playtests now run the release build. Found with the sampling profiler
  (`XB_PROFILE=auto`, loader/profiler.h; see the README).
- Gamma and brightness (2026-10-17): the game sets one gamma ramp at device
  creation (FUN_00064bc0, static table 0x008a29b0: an S-curve, 0/128/255 -> 3/123/
  252) and never changes it; its GLimp_SetGamma-like wrapper 0x00064540 has no
  callers, and the menus have no brightness option (the Brightness00-10 widgets in
  frontend.cod have no code or text behind them). The ramp is now applied while
  copying to the window (ps_2_0 lookup pass, `D3D9Backend::draw_with_gamma`;
  checked pixel-exact against the table), so the picture has the Xbox's contrast.
  PC option on top: `--brightness X` (0.5-2, launcher slider), 255*(ramp/255)^(1/X).
  Screenshots (`--screenshots`, --diag) are taken before the ramp.
- Launcher (2026-10-17): "Reporting a problem" with Open log folder / Open save
  folder buttons, for players attaching log.txt and saves to issues.
- 60 fps (2026-10-17): `--fps 60|30`, launcher "Frame rate"; default 60 since the
  Gela playtest (2026-10-17). The title waits a number of vblanks per frame (0x008a2cb0), set only
  by FUN_000645c0(mode) - always mode 2 (startup x2, movie player); the patch
  replaces it so mode 2 means 1 vblank. Game time is clock-based (Com_Frame msec
  from Com_EventLoop): its accumulated time at 0x008ddaa8 runs at 1.000x wall time
  at both rates (Gela, 3 x 20 s). Gela release build: 59.7 fps average, 2 hitches
  in 120 s (30 fps: 10). 1/30 s constants checked: FUN_0032d890 (a fade loop, 2
  frames per 1/30 s step -> half as long at 60), FUN_00094610 (time-based update
  intervals), FUN_0022c910 (initial value, overwritten with the real time). Movies
  (.xbv) show one frame per game frame (2x speed at 60, user playtest): frames with
  a movie frame (decoder vtable 0x007cf390 slot 2) wait 2 vblanks, chosen per frame
  where the wait is set up (jumps to 0x00064370). Gela's briefing: 24 s at 30 fps,
  then 60. Tracers starting beside the boat gun: the same at 30 fps (original).
  Gela gameplay at 60 confirmed by the user (normal speed). Open: heavy fights
  (mission 5 town) at 60; per-frame tuning in later missions.
- Aim assist (2026-10-16): `--aim-assist on|off`, launcher checkbox (default on).
  Off patches the sticky aim function (FUN_002db7d0) to clear its target and
  return; confirmed by the user (moving enemies no longer tracked).
- Water (2026-10-16): vertex program EXP/LOG now follow the Xbox (vs_1_1 expp/logp:
  EXP = 2^floor, frac, 2^s, 1). The water's wave program uses EXP.y as its phase;
  with D3D9's full-precision exp the sea was flat murky fog. Confirmed by the user in
  mission 6.
- Fullscreen (2026-10-16): `--fullscreen`/`--windowed`, `fullscreen = true` in
  settings.ini, "Display mode" in the launcher, Alt+Enter while playing. Borderless at
  the desktop's mode; the swap chain is the largest display's size and the window
  shows its corner (Present source rect), so no device Reset. Render scale defaults
  to the window's/screen's height (2.25 at 1080p). Fullscreen start, Alt+Enter and
  resizing confirmed by the user.
- Multiplayer and Xbox Live (2026-10-17): the Multiplayer item (console command
  `load_mp_exe`) reboots into codmp_xboxf.xbe, and its "Back to Single Player" into
  default.xbe (checked both ways). Sign In (SP main menu, Xbox Live Options) and MP's
  Play Xbox Live keep the player in the menu with a 4 s notice over the game
  (`render::show_notice`); the status line says "Not signed in". See
  [multiplayer.md](multiplayer.md).
- Sound through the release's bundled OpenAL Soft confirmed by the user
  (2026-10-16).
- Launcher (2026-10-16): `cod_bro_launcher` (Dear ImGui on SDL3) sets the game-file
  folder and the player options, saved as `settings.ini` (see the README, Running),
  and starts the game; the game starts it when it finds no game files.
- Release (2026-10-16): `packaging/sniper/build.sh` (README, Releasing) builds
  cod_bro-0.1.0-linux.tar.gz in the Steam Runtime sniper SDK: glibc 2.30+, bundles
  SDL3/OpenAL Soft built from source and, since 0.2.2, the official prebuilt
  DXVK 2.7.1 binary. Checked: starts on bare 32-bit Debian
  11; plays on Fedora with its own libraries (mission 1, same frames as the dev
  build). Sound confirmed by the user.
- Missions 1-5 played through or started clean under `--diag` (see
  [playtesting.md](playtesting.md)). Mission 2 confirmed polygon offset: the barracks
  sign, floor and wall decals, maps and posters no longer z-fight. Mission 3 (tank
  level) and mission 4 (19 min, through the menus with the profile) played clean with
  F11 on: no untranslated features, no new warnings. Mission 5 (Liberator, 15 and
  16 min) clean, 30 fps; one note from it is open: a dead gunner's body sank through
  the floor grate of the turret well (likely the original's, not checked).
- Mission 5 frame pacing: frames that missed a vblank (50 ms) clustered in heavy
  fights (the town battle at the end, ~25 in 25 s). Likely cause, fixed: a vsync'd
  Present waited on the display's clock on top of the title's wait for our vblank;
  Present is now immediate + DXVK tearFree (mailbox) and the vblank runs at the
  display's rate. Not yet confirmed in that fight.
- No game file has a cube or volume texture (all 9,652 DDS in the .cod packs and the
  229 loose ones are 2D; the DDS loader `0x6b09e` sets the cube flag only from caps2,
  and the code-built normalization cube `nglNormalCubeMapTex`, `FUN_0006f080`, is
  never called), so those stay unbuilt until a run hits one.
- Native loader focus: replace graphics (D3D8 -> D3D9/DXVK), sound, input, kernel and
  saves. Game code stays original; decompile only what the loader needs (raw GPU
  command sites `BeginPush`/`RunPushBuffer`, PC bugs/timing, wanted features).
  Otherwise: run, implement what it stops at, repeat.

## Kernel and XAPI (`xbox/`)

- Threads (`PsCreateSystemThread[Ex]`, `PsTerminateSystemThread`), handles
  (`NtClose`, `xbox/objects.cpp`), critical sections (`xbox/sync.cpp`, in place on
  the title's struct). `HalRegisterShutdownNotification` records registrations (not
  yet run).
- Dispatcher (`xbox/dispatcher.cpp`): waits (single/multiple, timeouts), events,
  mutants, timers, DPCs on a kernel thread, IRQL (DISPATCH_LEVEL = lock shared with
  DPCs). User APCs (`ReadFileEx`/`WriteFileEx` completions) run at the thread's next
  alertable user-mode wait. Not yet: named objects, semaphores.
- Settings (`xbox/settings.cpp`): a full default EEPROM image (NTSC, English);
  `XboxHDKey` is a fixed port key, so saves from a real console won't verify.
- Memory (`xbox/kernel_memory.cpp`, host side `platform/vmem*`): NT-style virtual memory at
  0x10000000-0x50000000, contiguous memory at 0x80000000 + physical, and one physical
  page budget (`--memory MB`, default 64 like a retail Xbox) shared by the image,
  contiguous and committed memory. Our own renderer/audio code is outside it.
- Files (`xbox/filesystem.cpp`, host side `platform/file*`): symbolic links,
  `\Device\CdRom0` = assets (read-only), partition 1 (E:/T:/U:) and the cache (Z:) in
  the user data folder, case-insensitive paths. File handles are FILE_OBJECTs that
  `ObReferenceObjectByHandle`/`IoQueryVolumeInformation` work on.
- Saves: XAPI signs them with the kernel's SHA-1/HMAC (`xbox/crypto.cpp`; the
  signature key is the zero-filled data export, so saves only verify on this port).

## Loader and title patches (`loader/`)

- XAPI's GDT code-selector restriction is a no-op; `XMountUtilityDrive` links Z: to
  the cache folder (no raw partition table / FATX format).
- Every SDK function in a replaced library's section that we do not replace traps by
  name (`loader/patches.cpp`).
- Title reboots (`XLaunchNewImage` -> `HalReturnToFirmware(QuickReboot)`): the game
  reboots into `default.xbe` between levels and when quitting to the menu, with its
  state in the LaunchDataPage; the loader re-execs itself with the page (memory file)
  and the same options (minus `--press`); log and `--diag` times/marks continue.
  `--launch-data 3 3<DIR/launch_data.bin` repeats the last reboot. Reboots between
  default.xbe and codmp_xboxf.xbe switch the title setup (addresses, symbols);
  XONLINE's launch hook is cleared.
- Timing: the title reads the CPU's time stamp counter (RDTSC, 27 sites incl. XAPI
  QueryPerformanceCounter) and assumes the Xbox's 733 MHz; each site goes through a
  trampoline to `xbox::cpu_time_stamp_counter()`. Without it the game ran ~5x fast on
  a 3.6 GHz TSC. Unverified against real hardware: overall game speed (judged by eye).
- SEH (`loader/seh.cpp`): RtlRaiseException/RtlUnwind call the title's handlers on
  its own stack; the script VM's coroutines throw C++ exceptions in normal play.
- Threads run truly in parallel and ignore priorities (the Xbox is single-core with a
  priority scheduler) - watch for races that the original never had.

## Audio (`audio/`)

DirectSound is replaced on OpenAL Soft: the loader redirects the 53 DSOUND functions
the game calls, found by name in the Ghidra symbols. Buffers play from title memory
with play/loop regions, streams take packets through the XMediaObject vtable, 3D uses
the game's rolloff curve, HRTF on headphones. `--silent` plays into a silent loopback
device. Unverified: ADPCM's 65th sample (dropped); envelopes and doppler are ignored.
File media objects do not close the title's handle (the game streams every sound from
one handle to sp/global.cod).

## Input (`input/`)

SDL3. Port 0 is always connected (keyboard and mouse plus the first PC gamepad); more
gamepads take ports 1-3 with hot-plug. The keyboard layout is in `input/devices.h`
(CoD BRO's default pad layout from config/default.cfg, PC-style keys). The action
keys follow the profile's controller style (style_a..l.cfg rebind the pad):
`loader/patches.cpp` moves each to the button bound to the same command and logs the
pad's bindings. Mouse look adds to cl.viewangles in a thunk before CL_MouseMove,
because the Xbox build runs its right stick through the Quake 3 mouse path (stick
acceleration, 370 deg/s limit). `--mouse-sensitivity` sets degrees per count (default
0.11). `--aim-assist off` turns off sticky aim (the game has no setting for it).

## Rendering (`render/`)

See [rendering.md](rendering.md).

## Online (`online/`)

Xbox Live is unavailable: no network, no accounts.

## Later

- Multiplayer (outline, unverified): system link needs no server software (a
  player's game hosts, Q3-style listen server). Steps: run codmp_xboxf.xbe (own
  symbols/patches), replace XNet with host UDP sockets, LAN discovery + join by IP;
  an optional lobby server later. First: SdkCallUsage on its XNET/XONLINE calls,
  dedicated-server and split-screen code present or not.

- Raise the memory budget and enlarge the game's memory pools (`FUN_00043fb0`
  allocates fixed 35.2 MB + 3.7 MB arenas) for bigger assets/mods.
- Full native build: decompile the rest; see [comparison-harness.md](comparison-harness.md).
