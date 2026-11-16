# Component status

What each part of the port does today and how it was checked, by component. (The dated work journal is kept
outside the repository.)

## Current state (2026-10-16)

- Mouse in the menus (2026-10-18), campaign and multiplayer: hovering selects an item,
  left click selects it and presses A, right click B, the wheel moves up/down; the
  cursor is free while a menu is up and the mouse is captured again when it closes.
  The UI's managers (SP front end 0x0095f8f8, in-game 0x0095f8fc, dialogs 0x0095f900;
  MP 0x004b31f8 / 0x004b3200) poll the pad each frame (FUN_0031fc10, MP
  FUN_0028d080); a thunk there hit-tests the active screen's items against their
  text widgets' boxes (only widgets drawn in the last frames, hooked at the text draw
  FUN_00321160 / MP FUN_0028e5d0) and selects through the screen's own vtable. Layout
  and offsets in src/runtime/loader/patches.cpp ("Menus with the mouse"). Checked unattended with
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
  (src/text/key_prompts.cpp) with the keys of the active controller style (inverse of the
  keyboard remap; in menus Enter/Bksp for A/B); BackendConfig::replacement_version
  makes the renderer re-ask when the labels change. Tested: switching between
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
  the glyph box. `src/text/` reads the packs (format in src/text/kapf.h), redraws each atlas
  at max(2, ceil(render scale)) from the OFL stand-ins in src/text/fonts/, every glyph scaled
  into its original ink box (the game's spacing stays), and the renderer swaps it in
  by content hash (BackendConfig::replace_texture). GARA.TTF is a regular weight and
  only feeds XFONT (Xbox Live UI), so it is not used. Checked in screenshots at
  render scale 3: menus, dialogs, mission title cards, briefing hints; all four fonts
  are replaced in a menu-to-mission run (Linux, and Wine for the Windows build).
  Not swapped: the button icon fonts, the legal screen (an image).

- **Prototype release:** private GitHub pre-release v0.1.0 (2026-10-16), Linux
  tar.gz + Windows zip; license GPL-2.0-or-later. Fullscreen (Alt+Enter, a resized window) and the Windows zip on
  real Windows (RTX 3080) checked.
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
  - Launcher: a .desktop file / icon for Linux desktops.
- Mission 6 (Gela) slowdown (2026-10-17): ~20 fps for ~13 s in the opening
  landing-craft cutscene, only in the Debug build (`linux-loader`, -O0) that the
  playtests used: the title's render thread was CPU-bound in our unoptimized draw
  translation (vertex fetch `draw_streams`, texture `hash_bytes`, std::fill, TLS
  lookups). The Release build (`linux-loader-release`, as players get) holds 30 fps
  there. Playtests now run the release build. Found with the sampling profiler
  (`XB_PROFILE=auto`, src/runtime/loader/profiler.h; see the README).
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
  (.xbv) show one frame per game frame (2x speed at 60, seen in play): frames with
  a movie frame (decoder vtable 0x007cf390 slot 2) wait 2 vblanks, chosen per frame
  where the wait is set up (jumps to 0x00064370). Gela's briefing: 24 s at 30 fps,
  then 60. Tracers starting beside the boat gun: the same at 30 fps (original).
  Gela gameplay at 60 checked in play (normal speed). Open: heavy fights
  (mission 5 town) at 60; per-frame tuning in later missions.
- Aim assist (2026-10-16): `--aim-assist on|off`, launcher checkbox (default on).
  Off patches the sticky aim function (FUN_002db7d0) to clear its target and
  return; checked in play (moving enemies no longer tracked).
- Water (2026-10-16): vertex program EXP/LOG now follow the Xbox (vs_1_1 expp/logp:
  EXP = 2^floor, frac, 2^s, 1). The water's wave program uses EXP.y as its phase;
  with D3D9's full-precision exp the sea was flat murky fog. Checked in mission 6.
- Fullscreen (2026-10-16): `--fullscreen`/`--windowed`, `fullscreen = true` in
  settings.ini, "Display mode" in the launcher, Alt+Enter while playing. Borderless at
  the desktop's mode; the swap chain is the largest display's size and the window
  shows its corner (Present source rect), so no device Reset. Render scale defaults
  to the window's/screen's height (2.25 at 1080p). Fullscreen start, Alt+Enter and
  resizing checked.
- Multiplayer and Xbox Live (2026-10-17): the Multiplayer item (console command
  `load_mp_exe`) reboots into codmp_xboxf.xbe, and its "Back to Single Player" into
  default.xbe (checked both ways). Sign In (SP main menu, Xbox Live Options) and MP's
  Play Xbox Live keep the player in the menu with a 4 s notice over the game
  (`render::show_notice`); the status line says "Not signed in". See
  [multiplayer.md](../developers/multiplayer.md).
- Sound through the release's bundled OpenAL Soft checked (2026-10-16).
- Launcher (2026-10-16): `cod_bro_launcher` (Dear ImGui on SDL3) sets the game-file
  folder and the player options, saved as `settings.ini` (see the README, Running),
  and starts the game; the game starts it when it finds no game files.
- Release (2026-10-16): `packaging/sniper/build.sh` (README, Releasing) builds
  cod_bro-0.1.0-linux.tar.gz in the Steam Runtime sniper SDK: glibc 2.30+, bundles
  SDL3/OpenAL Soft/DXVK built from source. Checked: starts on bare 32-bit Debian
  11; plays on Fedora with its own libraries (mission 1, same frames as the dev
  build). Sound checked.
- Missions 1-5 played through or started clean under `--diag` (see
  [playtesting.md](../developers/playtesting.md)). Mission 2 confirmed polygon offset: the barracks
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

## Kernel and XAPI (`src/runtime/xbox/`)

- Threads (`PsCreateSystemThread[Ex]`, `PsTerminateSystemThread`), handles
  (`NtClose`, `src/runtime/xbox/objects.cpp`), critical sections (`src/runtime/xbox/sync.cpp`, in place on
  the title's struct). `HalRegisterShutdownNotification` records registrations (not
  yet run).
- Dispatcher (`src/runtime/xbox/dispatcher.cpp`): waits (single/multiple, timeouts), events,
  mutants, timers, DPCs on a kernel thread, IRQL (DISPATCH_LEVEL = lock shared with
  DPCs). User APCs (`ReadFileEx`/`WriteFileEx` completions) run at the thread's next
  alertable user-mode wait. Not yet: named objects, semaphores.
- Settings (`src/runtime/xbox/settings.cpp`): a full default EEPROM image (NTSC, English);
  `XboxHDKey` is a fixed port key, so saves from a real console won't verify.
- Memory (`src/runtime/xbox/kernel_memory.cpp`, host side `src/platform/vmem*`): NT-style virtual memory at
  0x10000000-0x50000000, contiguous memory at 0x80000000 + physical, and one physical
  page budget (`--memory MB`, default 64 like a retail Xbox) shared by the image,
  contiguous and committed memory. Our own renderer/audio code is outside it.
- Files (`src/runtime/xbox/filesystem.cpp`, host side `src/platform/file*`): symbolic links,
  `\Device\CdRom0` = assets (read-only), partition 1 (E:/T:/U:) and the cache (Z:) in
  the user data folder, case-insensitive paths. File handles are FILE_OBJECTs that
  `ObReferenceObjectByHandle`/`IoQueryVolumeInformation` work on.
- Saves: XAPI signs them with the kernel's SHA-1/HMAC (`src/runtime/xbox/crypto.cpp`; the
  signature key is the zero-filled data export, so saves only verify on this port).

## Loader and title patches (`src/runtime/loader/`)

- XAPI's GDT code-selector restriction is a no-op; `XMountUtilityDrive` links Z: to
  the cache folder (no raw partition table / FATX format).
- Every SDK function in a replaced library's section that we do not replace traps by
  name (`src/runtime/loader/patches.cpp`).
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
- SEH (`src/runtime/loader/seh.cpp`): RtlRaiseException/RtlUnwind call the title's handlers on
  its own stack; the script VM's coroutines throw C++ exceptions in normal play.
- Threads run truly in parallel and ignore priorities (the Xbox is single-core with a
  priority scheduler) - watch for races that the original never had.

## Audio (`src/audio/`)

DirectSound is replaced on OpenAL Soft: the loader redirects the 53 DSOUND functions
the game calls, found by name in the Ghidra symbols. Buffers play from title memory
with play/loop regions, streams take packets through the XMediaObject vtable, 3D uses
the game's rolloff curve, HRTF on headphones. `--silent` plays into a silent loopback
device. Unverified: ADPCM's 65th sample (dropped); envelopes and doppler are ignored.
File media objects do not close the title's handle (the game streams every sound from
one handle to sp/global.cod).

## Input (`src/input/`)

SDL3. Port 0 is always connected (keyboard and mouse plus the first PC gamepad); more
gamepads take ports 1-3 with hot-plug. The keyboard layout is in `src/input/devices.h`
(CoD BRO's default pad layout from config/default.cfg, PC-style keys). The action
keys follow the profile's controller style (style_a..l.cfg rebind the pad):
`src/runtime/loader/patches.cpp` moves each to the button bound to the same command and logs the
pad's bindings. Mouse look adds to cl.viewangles in a thunk before CL_MouseMove,
because the Xbox build runs its right stick through the Quake 3 mouse path (stick
acceleration, 370 deg/s limit). `--mouse-sensitivity` sets degrees per count (default
0.11). `--aim-assist off` turns off sticky aim (the game has no setting for it).

## Rendering (`src/render/`)

See [rendering.md](../developers/rendering.md).

## Online (`src/online/`)

Xbox Live is unavailable: no network, no accounts.
