# Naming engine functions

default.xbe has no symbols. Before decompiling a subsystem we name its functions and
globals in Ghidra after their Quake 3 / RTCW counterparts (GPL source, the engine's
ancestry), so decompiled code reads like the original and callers make sense.

State (2026-10-17): 380 functions and 157 globals named, all applied in Ghidra and
exported to `tools/data/default_xbe_symbols.txt`.

## Files

| File | What |
|---|---|
| `tools/data/q3_name_proposals.csv` | Automatic proposals from [tools/q3match](../tools/q3match/README.md) (96 applied) |
| `tools/data/manual_names.csv` | Names found by hand: address, name, kind (`function`/`data`), confidence (`high`/`medium`), evidence |
| `tools/data/cvar_globals.csv` | cvar_t* globals named after their cvars (`tools/ghidra/CvarGlobals.java`) |
| `tools/ghidra/ApplyNames.java` | Applies a CSV: creates missing functions, renames only `FUN_`/default names, `data` rows become labels |
| `tools/ghidra/ExportMatchData.java` | Per-function strings and callees for q3match |
| `tools/ghidra/ExportSymbols.java` | Re-export the symbol file after renaming |

## Workflow per batch

1. Decompile a group (a region of `.text` or the callees of a named function) and
   compare with the Q3/RTCW source. Write a row per name into `manual_names.csv`;
   evidence says what pins it (strings, call order, table slot, argument count).
   `high` = body matches; `medium` = position or role only, or a CoD-only function
   named by convention (`Cmd_UFO_f`).
2. Run `ApplyNames.java` with the CSV, save the program, run `ExportSymbols.java`.
   ApplyNames never overwrites a name that is set: to correct an applied name, fix
   the CSV and rename that function by hand.
3. Check no new function name equals a name the loader looks up (`grep -rw '"NAME"'`
   over loader/, audio/, render/, input/, online/, xbox/), then
   `tools/smoke_test.sh OUT` (mission 4, A presses past the movie and the briefing,
   ~130 s) and look at `OUT/last.png`: the last frames must show the level with the
   HUD. A run without presses only reaches the opening movie and the loading screen,
   which does not exercise game code (runs before 2026-10-17 21:45 were like that).
4. Commit the CSV, the symbol file and this document's state line.

Running a script by name (`run_ghidra_script`) needs a copy in `~/ghidra_scripts/`;
copy the repository version there first so it is not stale.

## What the binary looks like (findings)

- Release build with messages stripped: only ~1,200 of 9,200 `.text` functions
  reference a string. Errors compile to `__break(code, message)` (Com_Error,
  G_Error), prints to an empty vsprintf (CL_RefPrintf, CG_Printf).
- Many small functions take arguments in registers (EAX/EDX/ESI/EDI/EBX); the
  decompiler shows them as `in_EAX`/`unaff_ESI`.
- q_shared, cvar, cmd, files, common, msg and the CM area/PVS helpers are close to
  Q3: cvar_t is Q3's (0x2c bytes, hash chains), fileHandleData_t is 0x120 bytes,
  Swap_Init stores the 8 byte-order helpers in Q3's order.
- CM_LoadMap only registers cm_noCurves/cm_playerCurveClip; the collision map is
  loaded elsewhere (and the renderer's world comes from it, RE_LoadWorldMap).
- Modules are linked statically. VM_Create (0x002f2b80) fills a 3-slot vm_t table
  (0x00927c38, 0x8c each): entryPoint +0x88 is G_vmMain (0x00273d70) or CG_vmMain
  (0x0028b960). VM_Call (0x002a84e0) passes 16 arguments.
- The syscall dispatchers are dead code: SV_GameSystemCalls only errors (the game
  calls engine functions directly), CL_CgameSystemCalls has no callers (the cgame
  calls the renderer through `re`).
- G_vmMain: 0 G_InitGame, 1 G_ShutdownGame, 2 ClientConnect, 3 ClientBegin,
  4 ClientDisconnect, 5 ClientCommand, 6 ClientThink, 7 unknown, 8 reload
  (g_reloading), 10 advances level.time, 0xc ConsoleCommand. CoD has no
  ClientUserinfoChanged export, so from 4 on Q3's numbers are off by one - each case
  was checked against its body.
  Cases 0-6 were checked against their bodies or calls (pers.connected goes 1 in
  ClientConnect, 2 in ClientBegin, 0 in ClientDisconnect; GAME_INIT is called with
  levelTime, Com_Milliseconds(), restart). Case 7 is not G_RunFrame: G_RunFrame is 0x00274660, called directly (SV_SpawnServer
  settles a map with five 50 ms calls).
- CG_vmMain: 0 CG_Init, 1 CG_Shutdown, 2 CG_ConsoleCommand, 3 CG_DrawActiveFrame;
  the rest are CoD's.
- Renderer interface: CL_InitRef (0x002f10d0) builds refimport_t in Q3 order
  (Printf, Error, Milliseconds, Hunk x3, Malloc/Free, Cvar_Get at slot 8, CoD adds
  Cvar_FindVar before Cvar_Set) and copies GetRefAPI's 40-slot refexport_t into
  `re` (0x009284e8). re slots: 0 RE_Shutdown, 1 RE_BeginRegistration, 2 ?,
  5 RE_LoadWorldMap, 9/14 a float get/set pair (unknown), 11 RE_ClearScene,
  12 RE_AddPolyToScene, 15 RE_RenderScene, 17 RE_SetColor, 18 RE_StretchPic,
  20 RE_StretchPicRotate, 21 RE_DrawQuadPic, 24 RE_BeginFrame, 25 RE_EndFrame,
  34 RE_TextWidth, 35 RE_TextHeight, 36 RE_DrawText, 37 RE_TextWidthWithIcons,
  38 RE_DrawTextWithIcons, 39 RE_DrawTextExt. Empty stubs: 10, 26, 30-33. Never
  filled by GetRefAPI: 3, 4, 6-8, 13, 16, 19, 22, 23, 27-29 (3, 4 and 13 are still
  called - from the dead syscall code and two cgame functions; unresolved). The dead
  cgame syscall cases give each slot's argument count, which confirmed them.
- Command tables: cgame `cg_commands` (0x0082f420), game cheat commands
  (0x008ddb70) and developer commands (0x008ddae0), all `{name, function}`.
- Spawn table g_spawns (0x009885e0): `{classname handle, SP_ function}` x32, built by a
  C++ static initializer (0x00377330) that hashes each classname with Com_HashKeyNoCase (0x00292fe0) -
  not a data table, so pointer-pair scans miss it; parse the initializer (MSVC loads
  the next classname into EDX before storing the previous result). G_CallSpawn
  (0x00260220) uses it, refuses `actor_*`, then falls back to bg_itemlist. Other
  name tables may be built the same way: look for runs of `mov edx, str / call /
  mov [tbl], eax / mov [tbl+4], func`.
- Function-pointer tables without names: five init functions (0x00309030-0x0030b00c,
  ~640 stores) fill an object behind 0x0095f49c (`mov [reg+0x540..], func`;
  G_InitGame calls slot +0x868). The level scripts call engine builtins through
  another table (0x009221xx-0x009225xx, bss, never written through its own address).
  Neither has names next to the pointers - only bodies identify them (not done).
  Scanning all of .text for immediate code-pointer stores found no other name
  tables than GetRefAPI, the spawn initializer and Swap_Init.
- Game entities are reached through handle tables (index & 0xfff, generation >> 12;
  entities at 0x0097a968, 0x540 entries); ent->think is an index into a function
  table at 0x008da788.
- G_RunEntity dispatches like Q3's G_RunFrame loop: eType 3 G_RunMissile, then
  physicsObject (+0x294) G_RunItem, eType 4 (also 7, 0xb) G_RunMover, else G_RunThink;
  an entity with a tag parent (+0x3cc) runs its parent first and is placed by
  G_GeneralLink. gentity_t: inuse +0x224, currentOrigin +0x140, s.pos +0x0c,
  teamchain +0x37c. level_locals: framenum, time, previousTime at 0x00985408.. (Q3 order).
- `tools/ghidra` has no tree view; the skill's scripts/CalleeTree.java lists a
  function's unnamed callees with size, caller count and strings (where to look next).
- `__FILE__` strings survive in Z_MallocDebug calls (`c:\cod\code\game\*.cpp`) in a
  few functions: cl_cgame, sv_game, common, cm_load, com_files, tr_dpvs.

## Corrections made along the way

- 0x00929c04 is `cl_disable_ads`, not `cl_run` (0x00934c70); CL_Init showed it.
- FS_FOpenFileWrite / FS_FOpenFileAppend were swapped until FS_FOpenFileByMode and the
  `"wb"`/`"ab"` mode strings settled them.
- 0x00272760 (G_vmMain case 7) was named G_RunFrame (medium) by position; the real one
  is 0x00274660. Reset to its default name.
- 0x0029ea80 is FS_ListFilteredFiles; FS_ListFiles (0x002a1fb0) is its wrapper.
- q3match proposals rejected: Q_stricmp for 0x00300b30 (a key-table store), a graph
  guess swapping COM_ParseExt (0x002b0250) and SkipWhitespace (0x002a9b30).

## Next

Decision (user, 2026-10-17): naming continues before step 2 (decompile + compare),
CoD-specific functions included, so later decompilation covers both kinds.

- The game module from G_RunFrame (entities, clients, AI) and the cgame from
  CG_DrawActiveFrame; the client/UI from SCR_DrawScreenField.
- CoD-specific names: Q3-style prefix by module (G_, CG_, CL_, SV_, RE_, Scr_, AI_ ...)
  plus what the body does; `high` only when strings, tables or calls pin the role,
  else `medium`. Evidence column says which.
- re slots 2, 9/14 and the never-filled 3/4/13; the unnamed pointer tables above.
- 27 functions created for table entries are still unnamed (`FUN_`).
