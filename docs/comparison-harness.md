# Comparison harness

Checks automatically, while the game runs, that a decompiled function behaves like
the original. Code: `loader/compare.h`, `loader/compare_linux.cpp` (Linux only;
`compare_win32.cpp` only installs replaced functions). Test: `tests/compare_test.cpp`.

## Using it

1. Decompile a function into `game/code/` (portable C++, the original's calling
   convention: `XB_THISCALL`, `XB_STDCALL`, ...; layout `static_assert`s under
   `#if UINTPTR_MAX == 0xFFFFFFFF`).
2. Add it to `kFunctions` in `game/code/functions.cpp`: name, default.xbe address,
   codmp_xboxf.xbe address (0 until matched), stack argument bytes, result kind
   (`None`, `Al`, `Eax`, `EdxEax`, `St0`, `Xmm0`) and `Status::Compare`. `Xmm0` is for LTCG
   functions that return a float in XMM0: the C version returns it on the x87 and an asm
   entry moves it (FX_RandomInOctaves_entry in game/code/fx.cpp).
3. Play or run unattended with `--compare` (`--level N` gets there fastest; the PC
   features are off, see below). Sampled
   calls are tested: the first 16 of each function, then every 64th. A function with
   100 matched tests over all runs (`functions.csv`) and no mismatch or drift is
   *settled*: not hooked in later runs, and no longer tested in the run that reaches
   it (`Config::enough_matches`). A mismatch or drift on record keeps it tested.
4. Read `tools/data/functions.csv` (counts, merged over runs - also over runs at the
   same time: each flush merges under a lock (`functions.csv.lock`); `functions_mp.csv` for
   multiplayer) and the `compare` lines in the log (the first 5 issues per function,
   with the differing registers or bytes; inconclusive tests and mismatches/drift have
   separate caps of 5, so front-end inconclusive calls do not hide the first mismatches).
   `tools/compare_batch.py [FUNCTION...]` does steps 3 and 4 in one go: builds, runs
   missions 1, 4, 5 and 6 (5 s apart, 200 s each, A pressed into the level, empty data
   folders; without presses a run stays on "Press A to start"), and prints what
   the runs added per function (default: the kFunctions entries changed since HEAD)
   with the first issue of each kind; `--all`, `--only`, `--every N` as below.
5. When a module is done, test it all once more: `--compare-all` tests the settled
   functions too (final check; counts add to the same CSV). A function with many
   tests, no mismatches and no drift then becomes
   `Status::Replaced`: from then on it runs instead of the original, in every build.
   Drift on a function that does float math can be accepted after reading it.
   Mismatches in values the title never sets (stale stack: uninitialized lanes, unwritten
   struct words - they depend on whatever ran before at that stack depth, so no C version
   can reproduce them) are accepted too (user decision 2026-10-25): `tools/data/accepted.csv`
   lists the function, a regex for its recorded issue and the reason. progress.py then
   counts it as `accepted` (rewritten; it no longer blocks its module's Replaced) while
   functions.csv's latest issue still matches the regex - a different kind of mismatch
   turns it back to `mismatched`. It stays Compare and keeps being tested.

A static check without a game run: `tools/vc71match/vc71match.py` compiles
`game/code/` with the title's own compiler (Visual C++ 7.1 from XDK 5849, under Wine)
and scores each function's instructions against default.xbe (see its README.md;
LTCG register arguments and inlining are expected misses).

Conventions in `game/code/`: the title's globals are reached with `title_global<T>(address)`
(title_data.h; such functions stay campaign-only until their multiplayer addresses are
known); functions taking arguments in EAX/EDX/ECX use `XB_REGS3` (platform/compiler.h);
the library is built with `-msse -mfpmath=sse` so float math rounds like the title's SSE
code (keep the original's operation order; `0 - x` is not `-x` for x = 0). MXCSR's
exception flags are compared too: where the original computes whole packed vectors
(DIVPS) and uses one lane, compute the other lanes as well and keep them with
`platform_keep` (compiler.h) - a 0/0 in an unused lane sets the invalid flag. Int to float
the title does on the x87 (FILD/FSTP, FLD/FMUL into _ftol) goes through the platform_x87_*
helpers: CVTSI2SS sets the precision flag above 2^24 and rounds by MXCSR. The title's inline
memcpy (REP MOVSD + REP MOVSB) is `platform_rep_movs`: front to back, like the title on
overlapping ranges. No range-for
over a braced list (`for (int i : {1, 2})`): the Debug build calls
`std::initializer_list` members out of line, the loader's copies win, and every test
ends inconclusive ("called host code"); use a static array. `--compare`
implies `--pc-patches off`: the loader's PC changes to the game's code (60 fps pacing,
mouse look, aim assist off, widescreen menu/objectives hooks) stay out, so compared
functions run as on the Xbox (30 fps) and patched ones such as nglSetVBlankMode can be
tested. The presentation is the Xbox's as well: settings.ini is ignored and, unless
given explicitly, 4:3 (no EEPROM widescreen flag, which changes the game's own code
paths), the game's fonts (no sharper text), render scale 1, no anisotropic filtering,
brightness 1 (user, 2026-10-18: test with nothing but what the test needs). Before
that date every run had the widescreen flag and sharper text on; the counts were
re-checked with `--compare-all` in the corrected setup. Only what the game needs on a PC stays (privileged code, RDTSC, SDK replacements,
the MP heap lock, `--level`). `--compare --pc-patches on` plays with them; then grep
`loader/` before adding a function - a patched one is not compared. A function the PC
features patch cannot become `Replaced` until that patch calls the decompiled version.

## Forced calls (paths the campaign never takes)

`--force "NAME(ARGS)@S*N?W"` (with `--compare`, campaign; `compare_batch.py --force`, repeatable)
tests N calls of a hooked function the title does not make - shutdown and free paths (the campaign
reboots instead of shutting down) - on copies of the game: `compare::force_call` starts an ordinary
test from the client frame's call site (`client_frame_hook`, at most one per spec and frame, from S
seconds after the level is active, on frames where W is nonzero, or with `?A=B` where A equals B), as if the title had called NAME
there; the game goes on without the call. ARGS: `eax=` ... `ebp=` and up to 32 stack words (8 before
2026-10-28; NAL_IK_BuildChainMatrices takes 12 and its three vectors follow them), written
above the return slot in the test processes (dead stack there); values are numbers, `[E]` (the
title word at E, read without faulting) and `E+N`/`E-N`/`E*N`, evaluated right to left (`A+B*C` is
A + B * C, `A*B+C` is A * (B + C)): the view weapon's anim tree is
`[[0x975fa8+[0x009725d0]*0xc4]]`, and `?[R+0xdc]+[[R+0x7c]+0x10]*0=1` waits for a zone record R
that is loading (state 1) and has its image (the `*0` term only has to be readable). The log's `force:` line shows the values
used. Check that the call does work before counting it: FxSystemList_DestroyAll(ecx=0x009099a8)
matched 39 times with its list count ([0x00909b8c]) 0 on every client frame of missions 1 and 4 -
only the early return ran; the row was reset. Functions nothing calls or points to (no CALL/JMP,
no pointer in data: XModel_FreeAnimStates) are dead code and not tested (user, 2026-10-25).
A `?W` guard that cannot be read yet (the objects it walks do not exist before the level has them) counts as
"not now", not as a failed spec. `&N` is the address of stack word N in the test processes, so stack words can stand in for an object's fields or a
short string: `nglMaterial_LoadTextureRef(&1-0x18,[0x902700]+0x54)` (a material whose name field +0x18 is word 1),
`RE_ParseButtonToken(ebx=&0-0x800,0,&4,&0,&0,0x427b6261,0x4f545455,0x7d315f4e,0x00006463)` ("ab{BUTTON_1}cd" in
words 4-7, the out buffer in dead stack below).
A `scene:` prefix (`--force "scene:nglDebugDrawLine(...)@5*10"`) starts the test from R_RenderView's call of
R_AddSimpleInstancesToScene (0x0024758a) instead, with the main scene pushed: draw-list code (nglListAddMesh/AddText,
*_AddToList) reads ngl_curScene (0x0090259c), which is null at the client frame. That thunk also runs inside the
original R_RenderView in test processes; it checks `compare::in_test_flag()` first and stays executable there
(Config::host_code, now sorted: the protection pass walks the ranges in address order).

## How a test runs

The function's entry jumps to a stub (`push index; jmp xbcmp_entry`) that saves all
registers and calls the dispatcher. For a tested call:

- The game forks a **coordinator** - a copy of its memory at the call - and goes on
  at once with the original, through a trampoline, so it always continues with the
  original's results. Up to `Config::parallel` tests (default: half the CPUs) run at
  once; a sampled call that finds them all busy is not tested.
- The coordinator forks a child, which forks two test processes before anything runs and
  waits for them, so that both start from the same copy of memory: one runs the
  **original**, the other the **decompiled** version. (Until 2026-10-28 the child ran the
  original and forked the other itself; glibc's fork handler then rewrote the malloc arenas'
  bookkeeping in one side only.)
- Each test process first makes every host mapping **non-executable** except the
  title's code, the harness's section (`xbcmp_text`), `libcod_bro_game.so` and the C
  runtime (libc, libm, libstdc++, libgcc_s, ld.so). Any call into other host code -
  a kernel export, an SDK replacement, the window, GPU or sound - faults, and the
  test ends as *inconclusive* with the caller named. So a test process can never
  touch the game's devices, sockets or files. File calls are the exception (below, File
  calls); a seccomp filter (`install_file_guard`) keeps them read-only: in a test process
  every system call that changes a file, sends data or starts a process fails with EPERM,
  and writes and ioctls reach only the harness's own pipe, clear_refs and pagemap.
- The dead stack below the caller's stack pointer (up to 16 KB, within the thread's
  stack) is zeroed as the function starts (`xbcmp_resume`), on both sides. Code that
  sends uninitialized locals - shader constant blocks whose unused lanes the title
  never sets (cdWorld_Draw, cdPropeller_Draw) - then sends the same bytes, and the
  decompiled version writes zeros there. Without it those calls mismatch on stack
  garbage (checked: cdWorld_Draw 839 of 1,344 tests).
  xbcmp_resume pops EFLAGS and returns through two words it pushes below that ESP; it zeroes
  the EFLAGS word again afterwards (MOV), else the original's never-written locals there read
  0x00200246 on one side only (0x0008d3f0's result w: 156 false mismatches). The target word at
  ESP - 4 would stay: it held the original's address on one side and the decompiled entry's on
  the other, and NAL_Clip_FindChannelByName / FindPosOriChannel / FindFloat3Channel copy the word
  below their entry ESP into out[3] (68 false mismatches). Test processes therefore resume into
  `xbcmp_test_jump`, which zeroes that word (MOV, flags kept) and jumps to the function through
  `xbcmp_test_target`, so it reads 0 on both sides (2026-10-28: the three then matched 63 of 63;
  a `--compare-all` run of mission 4, 32,856 tests, showed no other function changing).
- Then it clears the soft-dirty bits (`/proc/self/clear_refs`) and runs the function
  with the caller's registers and a return address of `xbcmp_landing`, which saves
  the registers and x87/SSE state (FXSAVE). Pages written are the soft-dirty ones
  found with the PAGEMAP_SCAN ioctl on `/proc/self/pagemap` (private anonymous
  mappings; per-page pagemap reads where the kernel lacks it).
- Both report over pipes; the coordinator asks both for every page either one wrote,
  compares, and sends the verdict (one pipe write) to the game's collector thread,
  which records it, logs and flushes the CSV every 64 tests. A reaper thread waits
  for the coordinators to exit. `compare::drain()` waits for the tests in flight;
  `flush()` (exit, reboot) drains first.
- Lifetimes: test processes die with their coordinator (PDEATHSIG). Coordinators
  cannot use PDEATHSIG, which fires when the forking *thread* ends (title threads
  may end before their test does): they watch a pipe whose write end only the game
  holds and exit at its EOF. The collector kills a coordinator still silent after
  `timeout_ms` + 3 s. Coordinators run with the default crash signals (not the
  game's handlers, which log and flush) and do not log.
- Calls the tested function makes into other hooked functions run their originals:
  each stub checks `xbcmp_in_test` (set in test processes) and jumps straight to the
  trampoline, since the dispatcher is not executable there. Decompiled code reaches
  title functions not decompiled yet with `title_function<F>(address)`.
- After the mprotect a test process runs only `xbcmp_text` code, with raw system
  calls: no C runtime, no compiler-added calls (`-fno-stack-protector`,
  `-fno-tree-loop-distribute-patterns` for that file). Check after changes:
  `objdump -d -j xbcmp_text build/linux-loader/bin/cod_bro_loader | grep call`
  must show only calls inside the section.

## Register arguments

Arguments in ESI, EDI or EBX, or floats in XMM registers, have no C spelling. Such a
function gets an entry in assembly (`PLATFORM_ASM_ENTRY(name, body)`, platform/compiler.h)
that pushes them, copies any stack arguments below them (`pushl 4*N(%esp)` N times for N
of them), calls the C version (`PLATFORM_ASM_CALL`: an `extern "C" PLATFORM_HIDDEN`
function, so the call in the shared library needs no PLT), pops and returns as the
original does; `functions.cpp` registers the entry. The harness hands it the caller's
registers, XMM included (FXRSTOR in `xbcmp_resume`). Example: cdSimpleAlpha_PulseColor
(EAX out, ECX colour, XMM1 speed, five stack floats) in game/code/shaders.cpp. Checked
with a planted bug in its XMM1 argument (caught in the helper and its caller).

## Recorded SDK calls

Code that draws calls the D3D replacements (state setters, draws), which are host
code. For the ones listed in `kRecordedD3dCalls` (loader/patches.cpp: name and argument
layout) a test process does not end the test there: the fault on the replacement's
entry is caught in the signal handler, which logs the call - its index and arguments,
for a pointer argument the bytes it points to (a fixed size, or another argument times
a size: constant counts, stream counts) - and returns to the caller as the
replacement would (EAX = 0, stack arguments popped). Both sides must make the same
calls with the same arguments in the same order; the first difference is reported
("SDK call 3 D3DDevice_SetTexture: arguments differ at word 2"). Only functions the
title reads nothing back from are listed (no getters, no Lock), and the replacement's
own writes (D3D's render-state block in title memory) are skipped on both sides alike.
The arguments are compared as values; pointers to data on the stack are recorded by
content, since the two sides' stack frames differ. `tests/compare_test.cpp` checks a
right and a wrong recorded call.

The clock: title RDTSC sites jump to loader trampolines that call `title_rdtsc` (patches.cpp);
in test processes the trampoline pages stay executable and `title_rdtsc` is a recorded call
with the `Clock` stand-in: EDX:EAX = 0 on both sides (2026-10-23; before, EDX kept whatever the
caller had, so code storing the high word - nglBeginFrame, nglVBlankCallback - could not match
a C version). Decompiled code reads the same clock through the title's XAPI
QueryPerformanceCounter (0x001ed6ca, itself a patched RDTSC site): no loader export needed.
RE_RenderScene re-checked afterwards (2,636 tests matched).
Frame end: D3DDevice_InsertCallback (the callback is not run in tests), InsertFence,
BlockOnFence and Swap are recorded calls too (EAX 0 on both sides), which made nglEndFrame
testable (settled 132/132; half its tests end "SDK call log full" - the whole scene render).

Stand-ins: a few recorded calls also do what the replacement would, so that code reading
the result back stays testable (`compare::SdkStandIn`, `kRecordedD3dCalls`):
D3DResource_AddRef/Release change the resource's reference count (the low 16 bits of its
first word, title memory) and return it, D3DDevice_GetBackBuffer2 returns the back buffer
(read from render's host global) after an AddRef. Cases they do not cover (a count reaching
0, a parent surface) end the test as inconclusive. This made nglGetBackBufferTexture's
callers (R_ClearAlpha, nglPushScene's nglSetDefaultSceneParams) testable.
Raw pushbuffer data: the D3DDevice_BeginPush_4 stand-in returns space in the test
process's own area (8192 dwords, zeroed like the replacement's; those pages are not
compared as writes), and the D3DDevice_EndPush one logs the dword count and every dword
written, so two sides that build different NV2A commands differ at that call ("SDK call
4 D3DDevice_EndPush: arguments differ at word 4"). Checked with a planted bug in
nglRenderTextNode's begin word. More space than that, or an EndPush outside the space,
ends the test as inconclusive.
Texture levels (nglTextureGetSurfaces, reached from nglCreateTexture and the per-frame
nglAllocFogTextures): D3DBaseTexture_GetLevelCount returns the mip count from the
texture's Format; D3DTexture_GetSurfaceLevel2 / D3DCubeTexture_GetCubeMapSurface2 hand
out surfaces from a 64-entry pool in the test process's own area (the same address in
both test processes; not compared as writes), filled as render's create_level_surface
does - count 1, Data/Format/Size from `render/surface_layout.h` (the layout code render
uses too, compiled a second time into the harness's section), Parent = the texture - and
add a reference to the texture (title memory, compared). What the title reads from them
is the real surface's content; the calls' arguments are logged, so a wrong level differs
at the call ("SDK call 1 D3DTexture_GetSurfaceLevel2: arguments differ at word 2", a
planted level + 1 in nglTextureGetSurfaces). AddRef/Release work on pool and title
resources alike as render's do: a surface's first reference adds one to its texture, its
last releases it, and a resource without D3D-internal references released to 0 is
destroyed (count 0; render's other work then - freeing its own level surfaces, host
copies - leaves title memory alone). This made nglReleaseTextureSurfaces/nglFreeTexture
testable.
Locks: on the Xbox a lock returns a pointer into the resource's own memory, so the
stand-ins compute it. D3DTexture_LockRect / D3DSurface_LockRect write {pitch, bits} into
the D3DLOCKED_RECT as render's lock() does (surface_layout.h, RECT offset), and
D3DVertexBuffer_Lock2 / D3DPalette_Lock2 return Data | 0x80000000. The D3DLOCKED_RECT
pointer is an `x` argument (a stack slot that is not logged: it often points into the
caller's frame, which differs between the sides); what is written there is compared like
any other write. The first test through the lock path found a real bug
(nglUpdateFogTable wrapped its columns at the width instead of the height); a planted
0xfe in its texels was caught (4 of 53 calls, the ones that refill the table).

## File calls

The title's statically linked XAPI file functions (CreateFileA, ReadFile/ReadFileEx,
SetFilePointer, GetFileSize, CloseHandle, FindFirstFileA, XGetDiskClusterSizeA,
XGetFilePhysicalSortKey, GetOverlappedResult, SleepEx...) end in kernel exports. Those are
**pass-through** calls in test processes (`SdkStandIn::Pass*`, the kernel list in
`recorded_sdk_calls`, loader/patches.cpp): NtCreateFile/NtOpenFile (FILE_OPEN without write
access only), NtReadFile, NtQueryInformationFile, NtSetInformationFile (the file position
only), NtQueryFullAttributesFile, NtQueryDirectoryFile, NtQueryVolumeInformationFile,
IoQueryVolumeInformation, NtClose, ObReferenceObjectByHandle/ObfDereferenceObject,
NtWaitForSingleObjectEx, KeDelayExecutionThread, RtlInitAnsiString and RtlNtStatusToDosError.
Each side runs the loader's own export in its own process (a copy of the game), so its
handles, file positions, events and queued APCs stay consistent with its own title memory;
nothing is replayed between the sides. Both sides' calls go into the recorded-call log -
the arguments, the path or file mask (OBJECT_ATTRIBUTES / ANSI_STRING contents), then a
`(result)` record with EAX - and must agree like any recorded call; with file calls in a test
the log is compared before memory, so a different call is the first issue reported ("SDK call
2 NtCreateFile: arguments differ at word 12"). Calls that would change a file (write access,
create or overwrite dispositions, delete on close, NtSetInformationFile other than
FilePositionInformation) and NtWriteFile, NtFlushBuffersFile and the rest end the test as
inconclusive with the reason ("made a file call the harness does not run (it would change a
file): NtCreateFile from 001e7dc1 XAPILIB::CreateFileA+0x6b").

While an export runs, the loader's code is executable and the title's is not
(`pass_mode`); the export returns through `xbcmp_pass_return`, which logs the result and
switches back. A title routine the export calls (an APC from an alertable wait: ReadFileEx's
completion) faults, runs in title mode, and switches back when it returns into the host (a
stack of frames, 8 deep). The dead stack below each pass-through call's ESP (128 KB within the
thread's stack) is zeroed first: host code copies struct padding it never set.
Determinism of the host side: the host heap is compared like any written page, and the two
sides must leave the same bytes there. Three things made them differ and were fixed: the
fork order above, the pipe descriptors (both sides now move theirs above all eight pipe
numbers, so files opened later get the same descriptor numbers on both sides) and
heap blocks the test processes free before the test (prepare_maps reads /proc/self/maps into a
static buffer; the seccomp builder has no padding). Checked by running the original on both
sides: StreamRequest_OpenFile, Stream_QueueLoad and StreamRequest_Init, 41 tests, all matched.

What stays inconclusive: anything waiting for another title thread - the file system's request
thread (File_InitSystem's CreateThread), which a test process does not have: Stream_LoadSync
and SV_SpawnServer spin in Stream_Update until it finishes; such tests time out, and the timeout
message says what the test process was doing (" (running)", " (waiting: futex)", from
/proc/PID/syscall). `XBCMP_TIMEOUT_MS=N` raises the per-test limit for long synchronous work.
`XBCMP_POKE=ADDR=VALUE[,...]` (up to 8) writes title words in every test process before the
test, on both sides and before the written-page tracking starts: `0x008bfd80=0` puts the file
system in its single-thread mode (File_PumpRequests then pumps the devices itself instead of
waiting for the request thread), which is how Stream_LoadSync can finish in a test process.
The recorded-call log holds 512 KB per side (level streaming makes many calls).

## Sound calls

The title's sound layer (SndHw_Update 0x000c7730 and the rest) calls the DirectSound/XAudio
replacements in `audio/` (buffers, streams, the listener, XMediaObject methods through our own
vtables). In test processes these are pass-through calls too (`SdkStandIn::PassSound`, the
`kRecordedSoundCalls` table in loader/patches.cpp: the SDK functions and the vtable methods,
`audio::dsound_methods()`, by our names): each side runs our replacement on its own copy of the
voices. Their arguments are logged (structures by content: DSBUFFERDESC, DS3DBUFFER, WAVEFORMATEX,
DSMIXBINS, XMEDIAPACKET, rolloff curves; output pointers are `x`), then the result, and must agree.
Since 2026-10-28 that made SndSys_Update, S_UpdateChannels, S_CheckWaveBankNotifies,
S_StopAllSounds, CL_ClearState, CL_MapLoading, FE_FadeOut and CL_Frame testable (they all ended
"called host code from SndHw_Update" before).
Since 2026-10-29 the decompiled sound code (Replaced) calls our own sound device
(audio/sound_device.h) instead of DirectSound. Its methods are pass-through calls as well
(`kRecordedDeviceCalls` in loader/patches.cpp, entries from `audio::device_methods()`, the device's
vtable): the C's virtual call faults on the method's entry, is logged and runs. Original code reaches
the device inside a DirectSound translation, already in host mode, so only the DirectSound call is
logged there. Member functions take `this` in the first stack slot; status() and listener() return
their structure through a hidden pointer before it (`x`). Results: `ret:al` for bools (the rest of
EAX is stale), `ret:void` for void methods and the structure returns (EAX = the caller's buffer).
Structures are logged by content without padding (VoiceDesc has an explicit reserved byte; Spatial
41 bytes, up to its mode). First run (mission 4, XBCMP_ONLY): CL_Frame 79 matched, no inconclusive
test besides the `--level` hook's frame; CL_ClearState, CL_MapLoading, FE_FadeOut matched.

- No output there (`audio/test_mode.h`): `audio::set_test_mode` gets the harness's in-test flag, and
  with it set the replacements make no OpenAL call (no device, no mixer thread, nothing logged);
  voices created in a test have no source. Voices keep the state they had at the fork, so play
  positions, packet statuses and GetStatus results are the same on both sides.
- The fork: the game's OpenAL mixer thread changes voices at any time and holds a voice's lock
  while it renders. `Config::before_fork` / `after_fork` (`audio::hold_for_fork` /
  `release_after_fork`) take every DirectSound lock (state, voices, stream callbacks, in that order)
  around the game's fork of each coordinator, so the copy is consistent and unlocked (the mixer
  waits a few microseconds). A lock still held in a test process (`audio::Guard` tries it) ends the
  test as inconclusive with the reason ("original found a voice locked at the fork (another thread
  held it): Stream_GetStatus from 000c79ac SndHw_Update+0x27c"; `compare::host_lock_held`) instead
  of waiting forever; without the fork locking that hit about 1 test in 40.
- Only 16 KB below a sound call's ESP are zeroed (128 KB for file calls): decompiled callers that
  run title code below their own frame keep 128 KB free there, and with 128 KB zeroing CL_Frame's
  sound calls returned into a zeroed C frame. CL_Frame's gap is now 0x30000: File_PumpRequests'
  file reads under it still lost the frame twice in 302 tests with 0x20000.
- Void SDK functions (DirectSoundDoWork, DirectSoundUseFullHRTF, XAudioCreatePcmFormat,
  XAudioCreateAdpcmFormat) return EAX = 0: their "(result)" record held stack garbage that
  differed between the sides' frames.
- Proof of determinism: `XBCMP_ORIGINAL_TWICE=1` runs the original on both sides (counts go to
  `tools/data/functions_twice.csv`, not functions.csv; delete it afterwards). Missions 1 and 4/5:
  SndSys_Update 725, S_UpdateChannels 390, S_CheckWaveBankNotifies 357, CL_Frame 344 and the rest,
  all matched; the only inconclusive tests were other host code (input rumble, renderer, level load
  timeouts, the `--force` client-frame hook inside CL_Frame).

The same runs fixed the log reader: records with `x` arguments were parsed with a word per `x`
(nothing is logged for them), so two different logs read as "SDK call log damaged" instead of
naming the call.

## Callers that rebuild the original's frame

A decompiled caller runs its callees deeper on the stack than the original, and some title
code depends on its stack position: it stores addresses of stack buffers (R_AddWorldSurfaces'
link buffer in 0x0095e2d0) or copies locals it never set (stale stack). R_RenderView shows
the remedy: its asm entry rebuilds the original's frame (the same aligned ESP, locals passed
by pointer at the original's offsets) with 128 KB kept free below it, and every call goes
through `title_call_at` with ESP where the original's call has it (frame minus the words it
still had pushed). After calls into our own functions the 16 KB below are zeroed again (the
title's later reads of uninitialized locals then see what a fresh test has). A gap must clear
what the callees' pass-through file calls zero (128 KB below their ESP, File calls above): with
a 128 KB gap Stream_UpdateLoad's File_PumpRequests zeroed its own C frame in tests (a return to
0); its gap is 192 KB. Stale-stack reads inside title callees can still differ (R_AddEntityModel's subtree): bisect by calling
original addresses instead of ours for groups of callees.

Debugging variables: `XBCMP_ORIGINAL_TWICE=1` (both sides run the original, Sound calls);
`XBCMP_ONLY=name,name` hooks only those functions and keeps testing
them past 100 matches (settled ones too); `XBCMP_EVERY=N` tests every N-th call instead of
every 64th (reaches calls the sampling skips); `XBCMP_SHOW=N` lists N runs of differences
per report (default 6; above 6 also the entry ESP and the stack limit). Reports are capped at
PIPE_BUF, so static debug buffers in host memory crowd them out: keep debug data on the stack.

## What is compared

- Registers: the result (by kind; `Al` = low byte only, `Xmm0` = XMM0's low lane), EBX/ESI/EDI/EBP, ESP after
  the return (a wrong calling convention or pop size), EFLAGS.DF, x87 control word,
  MXCSR, x87 stack depth and its values.
- Memory: every byte of every page either side wrote, except the stack below the
  caller's stack pointer plus the return address and arguments (callee scratch),
  and the calling thread's C runtime block (glibc's thread descriptor, which holds
  the thread id, and its rseq area, where the kernel writes the current CPU).

## Outcomes

- **matched**: identical.
- **drift**: only aligned 32-bit values differ, and each pair reads as two normal
  floats within 1e-5 of each other (x87 vs SSE rounding, another operation order).
  Plain integers can look like that too (a counter next to text: one run had it),
  so drift is never counted as matched.
- **mismatched**: anything else differs, or only the new version crashed.
- **inconclusive**: either side reached host code, did not finish in 2 s (a wait
  for a thread that does not exist in the test process), wrote over 128 MB, or the
  original crashed.

## Cost

The numbers in this paragraph are from the first version (the game waited about 10 ms
per test, two forks of the whole loader; Debug build). Mission 1 with
the first three functions: about 2,200 tests in 110 s; the game stays playable but
slower while many tested functions are called often. Settling keeps that cost on the
new functions: mission 1 with 104 functions (62 settled at start) ran 186 tests in
60 s instead of about 75 per second. Tune `first_calls`/`every`/`enough_matches` in
`compare::Config` if needed.

Each flush logs a `timing` line: mean and max ms per phase of the completed tests,
pages scanned and written, and the game's VmSize/VmRSS/VmPTE. Mission 4, Debug build,
`--compare-all`, 130 s (2026-10-18; the function itself took 0.01 ms):

| phase | ms before | ms now | what |
|---|---|---|---|
| fork | 1.8 | 1.9 | the parent's fork (1.9 GB mapped, 280 MB resident, 1 MB page tables) |
| start | 3.8 | 3.8 | until both test processes run: includes the child's fork of the second |
| maps | 1.0 | 0.7 | each test process reads and parses /proc/self/maps |
| protect+clear | 1.1 | 0.8 | mprotect of host code + clear_refs over the whole process |
| collect | 0.9 | 0.25 | written pages out of ~93,000 scanned (PAGEMAP_SCAN now, pagemap read before) |
| reap | 4.2 | 0.01 | waiting for the test processes to exit (now a reaper thread does) |
| reports, pages, compare | 0.2 | 0.15 | |
| total | 11.1 | 5.7 | 10,880 tests in the run before, 20,304 now |

Then tests moved beside the game (coordinator + collector, 4 at once on 8 CPUs): the
game waits only for its fork of the coordinator, 2.1 ms per test ("stall"). The
coordinator's own work rose to 8.4 ms (4 tests sharing the CPUs with the game), and
"latency" (call to verdict) is about 12 ms. Same run, 130 s: 29,531 tests; 163,291
sampled calls found all slots busy. Frame rate, mission 4, 100 s (`--diag`), mean from
30 s on:

| mode | game waits for tests | tests beside the game |
|---|---|---|
| `--compare-all` | 4.0 fps, 13,583 tests | 14.5 fps, 21,926 tests |
| `--compare` (most functions settled) | 30 fps, 20 tests | 30 fps, 19 tests |

What is left for the game is the fork itself: with every function tested
(`--compare-all`) it still waits about half the time. Lower `first_calls`/`every`
for hot functions or fewer slots would trade tests for frame rate.

PAGEMAP_SCAN (Linux 6.7+; the pagemap read stays as the fallback) was checked against the
pagemap read in every test of three runs: they differed twice, both times by the page of
the calling thread's rseq area, which the kernel wrote between the two reads (it is not
compared anyway).

## Offline tests of functions the campaign never runs

`tests/leaf_test.cpp` (ctest `leaf_test`, Linux loader builds; skipped without default.xbe)
tests decompiled functions without the game: it maps default.xbe at its addresses in a
32-bit test process, so the original's bytes run as they are and our C finds the title's
globals and functions where it expects them. `tools/leafscan.py --emit tests/leaf_functions.inc`
picks the functions (default: every module's untested, inconclusive, mismatched and offline
ones, so the offline ones stay checked, plus decompiled gap functions - no symbol, so not in
progress.csv - as state `unsorted`; `--module`, `--states`): no indirect calls (kernel thunks, vtables, callbacks), no FS or
privileged instructions, no calls into D3D/DSOUND/XONLINE/XNET/XPP, callees passing the same
test; `int3` is fine: the original crashes and the input is skipped; call depth up to 12).
Only instructions reachable from the entry count. A call through a constant table
(`call [reg*4+table]` in .rdata/.data, the NAL entropy modes) has the table's leading code
addresses as callees; eh_vector_constructor_iterator's callers have the constructor they push.
Stand-ins (`kStandins`, leafscan's STANDINS): Z_Malloc (0x00042cb0), XPhysicalAlloc/Free and
_aligned_malloc/free jump to the test's bump heap (1 MB of its own mapping between no-access guard
pages: in .bss it sat below malloc's heap and an overrun corrupted the worker), reset and zeroed
before each run, compared after it like the arena, with the list of freed pointers; _vsprintf,
_sprintf and __vsnprintf (they reach _getptd through FS) format with the host's printf (an i386
va_list is the same pointer), refusing %n; our C reaches them through the title's
addresses, so both sides get the same answers. Pinned (never randomized): the hooks 0x009098ec
(free), 0x009098e8 (Mem_AlignedAlloc) and 0x009098dc (nglFreeFileBuffer) stay 0, and a jmp/call
through a register loaded from one is not a blocker; the effect allocator 0x008bb760 and its object's
vtable pointer keep the image's values, and `call [reg+N]` with reg = [[0x008bb760]] calls vtable
0x007c4d90's slot (+4 0x94120, +8 0x94140: they reach Mem_AlignedAlloc/Free and the stand-ins, so no
stand-in of their own); leafscan follows the registers in address order. Presets (`kPresets`,
PRESETS): .bss function pointers the game sets at startup, written into the image (Swap_Init's
little-endian byte-order functions, 0x0092f138 etc.); a call through one calls that function. Log
stubs (LOGGED): the import slots 0x009284f4-0x00928578 (cgame) and 0x0095e634/0x0095e6b0 (renderer)
hold cdecl stubs that log the slot and its first words and return 0; the log is compared like the
frees. Words logged per slot (`kSlotArgs`): the fewest any call site pops right after the call, else
0 (logging 6 compared the callers' stack words past the arguments: return addresses, locals); a word
pointing into the call stack logs as a marker. A stub writes no out-parameter (R_DrawPic's
coordinates stay as they are) and pushes no x87 result (CG_ParseConfigStringFloat9: every input skipped). D3D stubs (`kD3dStubs`,
D3D_STUBS): 40 D3D setters, draws and waits (SetRenderState_*, SetTexture, Draw*, Clear, RunPushBuffer,
BlockUntilIdle...) jump to log stubs of their own convention (stdcall 0-6 words, fastcall ECX/EDX +
0-1; checked against each one's `ret N` and register reads) that log under the function's address
and return 0. Getters (GetDesc, Lock*, GetBackBuffer2, Register, Reset) stay blockers: a stub cannot
fill their out-parameters, and so does a store into an SDK section (`writes SDK state`: D3D's shadow
render state and tables in its zero-filled tail, LTCG inlines them into nglRestoreRenderStates and the
cd*_Draw functions; the test maps SDK sections read-only with their code). Globals are allowed; the ones read by the function and its callees are randomized,
except constants in writable sections: every instruction naming the address reads it, none
writes it or takes its address, and nothing within 16 bytes below is a base (address taken, or
indexed by a register) - FastSinCos's rounding constant 0x008febd0 lives in .data. A heuristic:
a structure based further away could still reach such a global.

Inputs are random: registers, XMM registers, 8 stack words (always 8: a caller's stack has
words above the arguments, and kFunctions' arg_bytes can be short - nglSelectMeshLodWithMatrix
says 0 but takes two), the globals, and a 32 KB arena of 1 KB chunks whose words are small
integers, bit masks, floats (0, -0, NaN, infinities, denormals...), random words, text or
pointers to near the start of a chunk (pointer chains to any depth). The arena sits between
no-access guard pages (it is mapped first, right below the libraries: a write past its end
corrupted ld.so and libc). The arguments each get a
chunk of their own (two arguments are never the same object; with self == src FxOpdd_Init's
order of stores differed); an argument the original faulted near becomes mostly a pointer from
then on (address - value < 4 KB), and so does an arena field: a fault within 4 KB above at most 8
arena words gives each of their chunk offsets 1/n, and from 1 on that offset is mostly a pointer
in every chunk. Half of the inputs mutate an earlier one the original ran without crashing (a
corpus of 256: 1-4 changes to arena words near a chunk's start, input slots, or the x87/SSE
modes; a pointer argument is redrawn when it would share another argument's chunk), only
inputs the original finished within 2 ms (a long loop's input would fill the corpus with slow ones),
so the rare input that gets through a chain of pointers and small indexes leads to more
like it (NAL_EntropyMode0C: 8 inputs of 4000 to 1862; Actor_GrenadeTargetClear 0 to 742). Compare
constants (leafscan's 6th field, at most 32: the function's own, then its callees' breadth first):
cmp/test immediates, the value an add/sub right before a jcc zeroes, a switch's range (`add -k; cmp
n` gives k..k+n, the first 16) and image words an fcomp/comiss/cmp reads; 0, 1 and -1 left out. A
fifth mutation kind and 1 fresh input in 4 get one dictionary step: half the time (when the
function has constants) a constant, 1 in 4 off by one, into an input slot (1/3) or as an 8/16/32-bit
field (as wide as it needs at least) at one chunk offset (half the time in the first 16 bytes) in every chunk
(3/4) or in one; otherwise a word copied from a slot or an arena field to a slot (not into another
argument's chunk), or to an arena field the same way, sometimes a run of 2-8 (an array of equal
values), sometimes a second field too. FxRenderer_NewOp5b's switch on a 16-bit field that must be
4..7, FxConst_Within's two floats that must be equal. A third dictionary kind (and a corpus mutation) sets an argument to a word 1-3 pointer steps from another argument through
arena fields: an object a structure the call gets already holds (a skip list's remove of a listed node's key,
`[[[list+8]+4]]`: nglSkipList_RemoveIndirect 68-84% -> 94-100%). Coverage is noisy: the same function lands
on 44% or 90% depending on the random stream (bimodal: one lucky input breeds), so compare such
changes over several `LEAF_SEED=N` (default 0). The x87 control word and MXCSR vary per input (24/53/64-bit
precision, truncation; FTZ, round down). The calls run on their own stack. Random words never
point into host memory (any mapping but the arena and the image, and 64 MB below the main
stack, which the kernel grows into on any access: -inf = 0xff800000 as a pointer read fresh
pages there instead of faulting, and a word inside a library's data read host state). The ranges
come from /proc/self/maps after the test's own buffers are mapped, plus a page below each (a value
just under [heap] aligned up by `(p + 3) & ~3` landed inside), 256 MB above [heap] and below the
lowest mapping above it (later brk growth and malloc's mmaps); text fills redraw words that
point there (two 16-bit "wide text" words made 0x00000000-0x13ffffff, the test's executable and
heap). Pointers a function computes can still get anywhere (NAL_Quat_BeginBlock's store loop
overwrites its own pointer cell): the brk heap is read-only during every call, so such a store
faults and the input is skipped, and the signal handlers write only static state (the printf
stand-ins open the heap while the host's printf runs, with SIGALRM held back). Some console
commands (CL_DecAnimWeight_f, IN_DownUp...) are bimodal: in some runs no input gets through va's
ring-buffer copy, so all 4000 are skipped; LEAF_CSV then keeps the function's earlier row.

Compared as in the game (below, What is compared): the result by kind, EBX/ESI/EDI, ESP after
the return, DF, the x87 control word, depth and values, MXCSR, the arena, and every title data
page either side wrote (they are read-only during a call; the first write to each faults and is
recorded). Stack arguments are not compared (callee scratch). Inputs the original crashes on
or runs over 10 ms with (50 ms when single-stepped) are skipped (random counts make long loops,
and each timed-out input costs the whole limit; our C, slower at -O0, gets a second run with 2 s), and so
are inputs an x87-result function returns from with an empty x87 stack (FPTAN out of range
pushes nothing: Scr_Tan's caller would underflow), and so are unstable ones: on a mismatch the original runs
again with the stack 0x2040 bytes lower, and if its own result changes, a pointer computed from
random fields landed on the call stack, where the two versions' frames differ
(FxPool_SetSlotTag's entry = pool + index * 8: 1 to 5 inputs in 10,000). Outcomes: matched, drift (every difference a close float or two
NaNs, as projections give both), **nan** (every difference is two
NaNs: SSE returns the first operand's NaN and GCC swaps commutative operands - only random
inputs hold NaNs, not game data; not a failure; a byte counts as a NaN difference when some 4
bytes around it are NaN on both sides, since random strides put particle floats at any address),
mismatched (fails the test). A NaN difference can cascade: on overlapping records (stride 1,
velocity and tangent at the same offset) later iterations read the NaN's bytes back as other
numbers. So a mismatch whose registers agree (MXCSR aside) and whose arena differs is rerun
with an **arena write log** (the arena read-only, every store faulting, single-stepped, the bytes
it changed recorded). If the first store where the two runs differ leaves a NaN on both sides,
the input counts as nan. Store by store when both made as many stores (exact), otherwise by
each byte's sequence of changes. Checked by restoring a fixed bug (FrenetMove clamping its
cached curvature instead of re-reading it): still 4 mismatches. The original is
single-stepped (TF) on every fourth input: coverage is the share of its instructions reachable
from the entry that ran. A function stops after 15 s once 1000 inputs ran (the slowest loops).
Worker processes (`LEAF_JOBS`, default half the CPUs, at most 4) take functions from a shared
queue (between guard pages: next to the arena, an overrun once filled its counters). A worker can
die outside a call (a pointer computed from random fields wrote into its own heap): it names the
function it was on, and a fresh worker takes the dead one's place at once and runs that function
first, on a clean heap: the corruption is often done by an earlier function and noticed by malloc
later, so the victim passes there. Within one run every worker has the same arena address, so a
function that kills a fresh worker kills all three tries and fails the test.
`LEAF_ITERS=N` (default 4000), `LEAF_ONLY=a,b`, `LEAF_VERBOSE=1`
`LEAF_REACHED=DIR` writes `DIR/<name>.reached` (the instruction addresses the traced inputs started; mark the
missing ones against `tools/fnpack.py --asm DIR <name>` to see which branch no input takes).
(where the original crashed: the fixed sites tell which input needs a better shape).
`LEAF_DUMP=DIR` writes each function's first mismatch as `DIR/<name>.case` (arena address,
registers, stack, globals, arena) plus both arenas after the call; `LEAF_REPLAY=DIR/<name>.case
LEAF_ONLY=<name>` runs it once more (with the write-log verdict) and writes
`<name>.replay.original/ours.bin`. Run both under `setarch -R`: the case needs its arena at the
same address, and ASLR moves the libraries over it. `LEAF_REPLAY_STACK=1=0x...` changes a stack
word: shortening a particle loop's `end` bisects to the first iteration that differs.
`LEAF_CSV=tools/data/offline.csv` records the results (rows of functions not run are kept);
tools/progress.py gives a function without a conclusive in-game test the state **offline** when
it has 100+ inputs matched, none mismatched or drifted, and 90%+ coverage (user, 2026-10-24: it
counts towards the matched counter; an in-game test later moves it to matched/settled as usual).
Checked with planted bugs in nglColorToVec4 (alpha ^ 1: 279 of 279 inputs) and
nglVec4ToColor (alpha from blue: 75 of 285).

## Limits

- Functions whose first 5 bytes hold a call, a jump or an instruction the decoder
  does not know are not hooked (logged). Conditional branches there are moved
  (re-encoded as near branches in the trampoline) unless they jump back into those
  5 bytes. Code elsewhere in the function that branches back into those bytes (a loop
  whose head is the second instruction) would land in the middle of the hook's JMP: since
  2026-10-28 the hook sweeps the function's code linearly from the moved bytes to its end
  (the first RET/JMP no earlier branch jumps past) and refuses it ("its code branches into
  the hooked bytes ..; not tested"). CoD BRO S_WaitForWaveBanks (jumps back to
  0x002ce9c1) crashed every run at boot before; the first run refused it and five more
  (IN_PadConsumeAllPressed, strcmpcasenosensitive_internal, FS_FilenameCompare,
  CG_WeaponSlotFromName, Key_Unbindall_f), all real loops back to their second
  instruction. The sweep stops at an instruction it cannot size, so it is best effort;
  branches from other functions are not looked for.
- A decompiled function taken by address in the loader executable would get its
  PLT entry, which is host code to the harness: take addresses inside the library
  (`game::functions()` does).
- Calls into kernel exports and SDK replacements not recorded or passed through (above) end
  a test, so functions that need them (file writes, locks, D3D getters and Lock) stay
  inconclusive. Possible later: pure stand-ins for
  common ones inside the test processes (critical sections without contention,
  Rtl string functions).
- Later, optional: record a function's inputs during play and replay them offline
  as unit tests.
