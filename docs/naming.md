# Naming engine functions

default.xbe has no symbols. Before decompiling a subsystem we name its functions and
globals in Ghidra after their Quake 3 / RTCW counterparts (GPL source, the engine's
ancestry), so decompiled code reads like the original and callers make sense.

State (2026-10-17): 640 functions and 385 globals named, all applied in Ghidra and
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
- ClientThink_real follows Q3 call for call: Pmove (pmove_msec chunks), then
  BG_PlayerStateToEntityStateExtraPolate, pm.mins/maxs copied, ClientEvents,
  SV_LinkEntity, G_TouchTriggers, ClientImpacts, oldbuttons/latched buttons
  (0x40 = use). pm.trace is G_TraceCapsule; every G_ trace wrapper ends in SV_Trace
  (0x002e9af0, 11 arguments).
- Entity callbacks are indices into one table per kind (entry 0 empty), not pointers:
  think +0x32c -> 0x008da788 (28), reached +0x330 -> 0x008da7fc, blocked +0x334 ->
  0x008da808, touch +0x338 -> 0x008da814, use +0x33c -> 0x008da840, pain +0x340 ->
  0x008da874, die +0x344 -> 0x008da88c, +0x348 -> 0x008da8a4 (unknown, actors),
  controller +0x34c -> 0x008da8b0 (turret/vehicle bone aiming).
  Entries are named from the SP_/Init function that stores the index where Q3 assigns
  that callback (SP_func_door: blocked[1] = Blocked_Door), or from their bodies.
  Mover states follow RTCW's enum (POS1 0, POS2 1, POS3 2, 1TO2 3, 2TO1 4, 2TO3 5,
  3TO2 6, rotate POS1/POS2/1TO2/2TO1 7-10). ~27 entries had no function in Ghidra
  (reached only through the tables) and were created on 2026-10-17. The skill's
  scripts/HandlerStores.java lists every immediate store into these fields.
- Spawn variables are looked up by key hash (Com_HashKeyNoCase, cached in static
  guards next to each SP_ function): G_SpawnFloat/Int/Vector take the hash in EDX.
- gentity_t (SP_func_door, InitMover): pos1 +0x2c0, pos2 +0x2d0, speed +0x2fc,
  wait +0x384, movedir +0x310, moverState +0x298 (byte), spawnflags +0x2a0,
  flags +0x2a4, takedamage +0x29c, nextthink +0x328.
- Notify names, classnames and other script constants are string hashes
  (Com_HashKeyNoCase) kept in globals: GScr_LoadConsts (0x002557c0) fills 132 of them
  (scr_const_*, 0x0097a650-0x0097a888) once; entities keep their classname hash at
  +0x260. Extracted with the skill's scripts/hashinit.py. G_Damage keeps Q3's
  knockback and splits the rest (health, die/pain, the "damage"/"death" notifies) into
  G_ApplyDamage; there is no armor code.
- Maps are not spawned from an entity string: G_SpawnEntitiesFromString walks 99
  precompiled map zones (SP_worldspawn, then G_SpawnGEntityFromSpawnVars per entity,
  G_SpawnVehicleNode per vehicle node) between level.spawning set/clear. Entities are
  0x460-byte objects from a pool (G_Spawn), not a fixed g_entities array slot.
- Entity sub-objects: +0x22c anim tree (XAnim), +0x23c gclient, +0x240 actor,
  +0x244 sentient (team at +4), +0x248 vehicle (a g_vehicles record), +0x24c turret
  info. Vehicles also have physics bodies (list 0x008feb70) stepped once per frame in
  G_VehiclePhysicsFrame.
- Correction (2026-10-17): 0x0095f694 / 0x00986000 are the vehicle pool and its slot
  count (g_vehicles, g_vehicleSlotCount), not g_entities / level_num_entities: the
  0x340-byte records hold an entity handle (+0x170) and a vehicle def index (+0x178).
- Game vmCvars (vmCvar_t, Q3 layout, integer at +0xc) come from gameCvarTable
  (0x008da8e8, 57 entries); they are named vm_<cvar> because several cvars also have
  an engine cvar_t* global of the same name (sv_cheats, developer, g_reloading, ...).
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

- Game module (2026-10-17, from G_RunFrame): entity runners, ClientThink_real,
  ClientEndFrame, spawn/mover/trigger paths, the callback tables and the G_RunFrame
  helpers are named. Small leftovers: pain[4] (0x0024c5e0, unclear), the +0x348
  table (actors - do with the AI), 0x002df880 (per-model pose/tag update) and
  0x002be8f0 ('COD_Note' list), 0x0024c9f0 (entity list bookkeeping), the 12
  scr_const slots GScr_LoadConsts does not fill.
- AI (2026-10-17, started): the state machine is named. g_aiStateFuncs (0x00830c28)
  is 13 states x 9 callbacks: Start (returns a state to chain to, 0 = none), Finish
  (actor, next state), Suspend (default: Finish), Resume (default: Start), Think,
  slot 5 (role unknown, Actor_CallStateSlot5 via the 0x0095f49c interface), Touch,
  Pain, NodeClaimRevoked (releases the claimed node; Actor_DropClaimedNode asks the
  states top down). States, from the
  debug strings the thinks store at actor +0x13e8: 1 cover, 2 exposed, 3 turret,
  4 lmg, 5 noncombat, 6 grenade, 7 death, 8 pain, 9 critical section,
  10 animscripted, 11 animcustom, 12 negotiation (like CoD4's ai_state_t plus
  cover/lmg/noncombat/critical section). actor_t: +0 ent, +4 sentient, +8 eState[]
  with stateLevel +0x20, transitions {type 1 set/2 push/3 pop, state} at +0x38
  (count +0xa0), simulated copy +0xa4/+0xbc; thinks queue transitions
  (Actor_SetState/PushState/PopState), Actor_ThinkStateTransitions applies them next
  frame. Each state has sub-states in actor +0x28 (cover 100/101, exposed 200-0xd7,
  death 400/401, grenade 600-0x25f, lmg 0x2bd/0x2be); the think's switch sets a
  debug string per case, which named the cover/exposed/grenade sub-behaviours.
- AI animation: the anim scripts are compiled script functions ("BroFunc"), found
  by name hash in g_broFuncs (0x00927170, 0x34 x {hash, function}; no direct
  writes - probably filled through a pointer, filler not found yet). G_InitAnimScripts (0x0024a070) stores "anim_main_<name>" at +8 of a
  0x10-byte record per script (g_animScript_combat ... _walk, 0x00987a98-0x00987bf7)
  and a second function at +0xc, looked up by a precomputed hash (no string; name
  unknown). The states set actor +0x254 (attempted script record) and +0x27c (anim
  mode); Actor_UpdateAnimScript commits them and calls the record's +8 with the
  entity number. Turret weapons have their own records (g_turretAnimScripts).
  Next: 0x00214050 (avoidance trace), 0x001fee70 (path progress before a clear),
  0x0020c820 / 0x00219230 / 0x002095c0 (path search, probably Path_* in 0x002c-0x002d),
  0x00201e90 (no visible effect), who fills g_broFuncs, the slot-5 role, then Actor_* outside
  the table (`CalleeTree.java Actor_* 1 1`).
- After the game module (AI included): the cgame from CG_DrawActiveFrame, then the
  client/UI from SCR_DrawScreenField (user, 2026-10-17: module by module).
- CoD-specific names: Q3-style prefix by module (G_, CG_, CL_, SV_, RE_, Scr_, AI_ ...)
  plus what the body does; `high` only when strings, tables or calls pin the role,
  else `medium`. Evidence column says which.
- re slots 2, 9/14 and the never-filled 3/4/13; the unnamed pointer tables above.
- 27 functions created for table entries are still unnamed (`FUN_`).
