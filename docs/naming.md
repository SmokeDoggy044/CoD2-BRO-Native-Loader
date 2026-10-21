# Naming engine functions

default.xbe has no symbols. Before decompiling a subsystem we name its functions and
globals in Ghidra after their Quake 3 / RTCW counterparts (GPL source, the engine's
ancestry), so decompiled code reads like the original and callers make sense.

State (2026-10-21): 2838 functions named by us (counter), 900+ globals, plus 3008 Microsoft
library functions named by signature; all applied in Ghidra and exported to
`tools/data/default_xbe_symbols.txt`.

## Files

| File | What |
|---|---|
| `tools/data/q3_name_proposals.csv` | Automatic proposals from [tools/q3match](../tools/q3match/README.md) (96 applied) |
| `tools/data/manual_names.csv` | Names from reading the function bodies: address, name, kind (`function`/`data`), confidence (`high`/`medium`), evidence |
| `tools/data/cvar_globals.csv` | cvar_t* globals named after their cvars (`tools/ghidra/CvarGlobals.java`) |
| `tools/ghidra/ApplyNames.java` | Applies a CSV: creates missing functions, renames only `FUN_`/default names, `data` rows become labels |
| `tools/ghidra/ExportMatchData.java` | Per-function strings and callees for q3match |
| `tools/ghidra/ExportSymbols.java` | Re-export the symbol file after renaming |
| `tools/data/library_names.csv` | Microsoft library code in the title's own sections, matched byte for byte (relocations masked) against the XDK 5849 .lib files (the XBE's library table says 5849) with the skill's `scripts/libmatch.py` + `libnames.py`: libcmt, xapilib, uix, xvoice, xgraphics, d3dx8, dsound, xonlines. Applied by `tools/ghidra/ApplyLibNames.java` into the library's namespace (UIX::CPopupScreen_Output); state `library` in progress.py (still counted in the total, user decision 2026-10-18) |

## Workflow per batch

1. Decompile a group (a region of `.text` or the callees of a named function) and
   compare with the Q3/RTCW source. Write a row per name into `manual_names.csv`;
   evidence says what pins it (strings, call order, table slot, argument count).
   `high` = body matches; `medium` = position or role only, or a CoD-only function
   named by convention (`Cmd_UFO_f`).
2. Run `ApplyNames.java` with the CSV, save the program, run `ExportSymbols.java`.
   ApplyNames never overwrites a name that is set: to correct an applied name, fix
   the CSV and rename that function directly in Ghidra.
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
- Cgame vmCvars come from cgameCvarTable (0x008d9d60, 137 entries of 4 dwords,
  CG_UpdateCvars); also named vm_<cvar>, vm_cg_<cvar> where the game table already
  has that name (g_reloading, developer, cl_languagewarningsaserrors).
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
  10 animscripted, 11 animcustom, 12 negotiation. actor_t: +0 ent, +4 sentient, +8 state stack
  with its level at +0x20, transitions {type 1 set/2 push/3 pop, state} at +0x38
  (count +0xa0), simulated copy +0xa4/+0xbc; thinks queue transitions
  (Actor_SetState/PushState/PopState), Actor_ApplyStateTransitions applies them next
  frame. Each state has sub-states in actor +0x28 (cover 100/101, exposed 200-0xd7,
  death 400/401, grenade 600-0x25f, lmg 0x2bd/0x2be); the think's switch sets a
  debug string per case, which named the cover/exposed/grenade sub-behaviours.
- AI animation: the anim scripts are compiled script functions ("BroFunc"), found
  by name hash in g_broFuncs (0x00927170, 0x34 x {hash, function}). Filled by the
  level's compiled script module itself: G_LoadLevelScriptModule (0x0030b960) passes
  it an import table (g_levelScriptImports, 0x009270d0) with BroFunc_Clear (+0x90)
  and BroFunc_Register (+0x94), plus the engine interface 0x0095f49c; pathnodes get
  their animscript functions the same way (PathNode_ResolveAnimScripts). G_InitAnimScripts (0x0024a070) stores "anim_main_<name>" at +8 of a
  0x10-byte record per script (g_animScript_combat ... _walk, 0x00987a98-0x00987bf7)
  and a second function at +0xc, looked up by a precomputed hash (no string; name
  unknown). The states set actor +0x254 (attempted script record) and +0x27c (anim
  mode); Actor_UpdateAnimScript commits them and calls the record's +8 with the
  entity number. Turret weapons have their own records (g_turretAnimScripts).
- Pathfinding (2026-10-18): Nodes
  are 0x80-byte records reached by a ushort handle (block (h >> 9) - 1 of
  g_pathNodeBlocks, index h & 0x1ff; 0 and 0xffff = none; PathNode_FromHandle), origin
  +0x44, radius +0x54, type +0x28 (0x10/0x11 = negotiation begin/end), A* transient
  fields +0x68..+0x7c. the path record sits at actor +0x3c0: 32 path point of 0x1c (origin,
  dir2D, length, node), then the point count +0x380, the original point count +0x382, dodge count
  +0x384, negotiation start +0x386, lookahead node +0x388, dodge entity +0x38c,
  final goal +0x390,
  lookahead dir +0x39c, forward dir2D +0x3a8, lookahead dist +0x3b0, lookahead amount
  +0x3b4 (64..65536), dist to next node +0x3b8, flags +0x3bc, time +0x3c0, team
  +0x3c4, current segment length +0x3c8, current point +0x3cc. Named: Path_AStarSearch (+ a
  NotCrossingPlanes copy), Path_BuildFromNodes, Path_Route*, PathNode_FindNearest,
  Node types (Actor_IsCoverValidAgainst): 2 cover stand, 3 crouch, 4 prone, 5 right,
  6 left, 7 wide right, 8 wide left, 9-11 concealment stand/crouch/prone, 0x10/0x11
  negotiation begin/end.
  Path_TryDodge, Path_TraceAhead, the lookahead updaters (Path_RefreshLookahead
  and helpers), Sentient_CachedNearestNode (cached at sentient +0x58), Sentient_SetGoalNode
  (+0x50). Scr_Notify (0x002b75c0) queues notifies on 0x008dd200.
  Spawning: Actor_Spawn (actors bypass G_CallSpawn) with Actor_Alloc / Sentient_Alloc.
  Awareness (2026-10-18): per-sentient records of 0x50 at actor +0x850 (one per
  sentient, 33): +0x14 attacked-me time (Actor_WasAttackedBy, last attacker at actor
  +0x12a4), +0x18 last-known time (Actor_RecordSentientPosition), +0x1c a threat-until
  time. Point events (Actor_HandlePointEvent) 2/3/6 notice, 4 learn from another
  actor, 5 attacked, 8/9 grenade, 10 notice; line events (Actor_SendLineEvent)
  0x10 bullet, 0x11 attacked. Orient: mode at actor +0x12c (override record +0x140),
  named by the binary's ai_orient_* strings (Actor_UpdateOrient: 2 to_motion, 3 to_enemy,
  4 to_enemy_or_motion, 5 to_goal); sentient +0x20/+0x24 goal yaw
  and arc (-1 = none).
  Actor physics (2026-10-18): a Pmove-shaped movement step for actors -
  Actor_RunMovement on the record at actor +0x290 (g_actorMove, locals g_actorMoveLocals),
  ActorMove_CheckGround/WalkMove/AirMove/FlyMove/FootSteps/FoliageSounds; move mode
  at actor +0x278 (1 path: Actor_MoveAlongPath, 7: 0x00212a20). Move history: 10
  vec2 samples at +0x1398 (Actor_*MoveHistory).
  Leftovers named (2026-10-18): ActorMove_SlideMove/StepSlideMove/AddTouchEnt (the
  Pmove trio), move mode 7 = Actor_MoveOutOfTheWay (steps away from the entity at
  actor +0x1390, which Actor_AskActorsToMoveAside sets on actors standing in the
  way), the grenade escape searches (Actor_FindPathAwayFromGrenade and its
  Path_*AwayFromGrenade* chain), Actor_Grenade_FindCover, corpse ground tilt
  (G_UpdateGroundTilt), PathNode_CanSee (node visibility bit matrix).
  Still open in the AI range (low value, unclear roles): 0x001fd6f0 (chain at
  +0x7c8, from Actor_AvoidOthers), 0x0020d110 / 0x0020c6d0 (exposed reacquire),
  0x002057b0, 0x00218890 / 0x00216080 (node search using PathNode_CanSee; callers
  0x0021bc00, 0x002196d0), the slot-5 role, the level script module's interface
  (0x0095f49c, filled by five init functions). 0x00201e90 and 0x001fd3d0 have no
  effect (every path returns).
  XAnim trees (2026-10-18, 20 functions, AnimTree_*/AnimData_*): a tree
  (AnimTree_Create: anim list +8, owner +0xc, heap +0x10, info count +0x14, ushort info
  index per anim +0x18) points at an anim list (+4 count, +8 entries of 0x38: +0x20
  child count, +0x22 parent, +0x24 data, +0x28 notetracks {name, time} of 0xc, +0x2c
  load generation, +0x34 flags, +0x36 first child). Per-anim playback state is a
  0x30-byte info from the pool at 0x00965cf0 (free list +0xa/+0xc; +2 next notetrack,
  +0x10 sync time, +0x14 time, +0x18/+0x1a cycles, +0x1c blend rate, +0x20 goal
  weight, +0x24 weight, +0x28 rate, +0x2c playback object). The current tree for the
  recursive walkers is 0x00928588. Open: 0x002bf7d0 (goal-time check in
  AnimTree_AdvanceTimeForModel), 0x002b7e40 (notetrack build, "COD_Note"/"fakeroot"),
  0x002b7c60, 0x002af080, and the memory-context push/pop pair 0x0028f6c0/0x0028c690
  (stack of 0x80 at +0x374 of the 0x0095f5d0 record).
- Cgame (2026-10-18, from CG_DrawActiveFrame, Q3 cg_view.c order): the frame
  (CG_UpdateCvars, CG_PredictPlayerState, CG_InterpolatePlayerState, CG_CalcViewValues,
  CG_ShakeCamera, CG_DrawActive, CG_AddViewWeapon), the snapshot code (cg_snapshot.c:
  CG_ReadNextSnapshot, CG_SetInitialSnapshot, CG_SetNextSnap, CG_TransitionSnapshot,
  CG_CheckEvents/CG_CheckPlayerstateEvents -> CG_EntityEvent, plus a pre-event pass
  with its own counter), CG_ConfigStringModified, CG_RegisterGraphics/Items, and the
  2D pass (CG_Draw2D: crosshair, centre print, damage direction icons, fades, hud
  elems, objectives); the 137 vmCvars; cg_t fields cg_snap/cg_time/... as labels.
  Range 0x00275040-0x0028bc50: 220 functions, 73 named after this batch.
  Open: CG_EntityEvent's body (0x00289790, big switch), 0x00285260 (2188 bytes, HUD),
  0x0027ff20 (projects a view direction to a screen offset), 0x00283120/0x00285e00
  (per-client model reload by name), 0x0027cad0 (cg state init: clears cg, parses
  configstrings), 0x002764c0/0x00276530 (identical fade stubs).
  Sweep of the range (2026-10-18, 80 more): CG_AddPacketEntities -> CG_AddCEntity and
  one adder per eType (the binary's ET_* name table at 0x008d5090: 0 general, 1 player,
  2 item, 3 missile, 4 mover, 5 portal, 6 invisible, 7 script mover, 8 sound blend,
  9 loop fx, 10 mg42, 11 actor, 12 actor spawner, 13 actor corpse, 14 vehicle,
  15 vehicle corpse, 16 vehicle collmap), CG_Trace/CG_ClipMoveToEntities, the view
  (CG_OffsetFirst/ThirdPersonView, tank/vehicle/linked views, CG_CalcFov, CG_StepOffset),
  the view weapon (CG_RegisterWeapon, CG_CalculateWeaponPosition/Angles, bob, sway,
  gun move/rot offsets pinned by the cg_gun_* cvars, CG_AddPlayerWeapon), weapon
  events (CG_FireWeapon, tracers, whizby), local entities, camera shake, hud element
  drawers and timers, shellshock sound/view, CG_RegisterCvars, CG_ParseServerinfo,
  objectives configstrings. Still open in the range (~65): the big HUD/compass draws
  (0x00285260, 0x00282620, 0x002824f0), CG_CalcViewValues' 0x002812d0, the view weapon
  anim helpers (0x002808b0, 0x00280ed0), weapon cycle helpers, save/restore of cg
  state (0x00276b10, 0x00278cb0), configstring sound/fx registration (0x0027b9a0).
  Leftovers (2026-10-18, 24 more): CG_AddLocalEntities with the tracer (CG_Tracer)
  and near-culled types, CG_PointContents, CG_Save/LoadViewModelAnimTrees (their
  debug strings) and the save-state pair (CG_vmMain 0xf/0x10), CG_ReadShellShockCvars,
  CG_GetViewFov, bob cycles, gun kick, weapon cycling helpers. Still open (~40, low
  value): the big HUD/compass draws, CG_CalcViewValues' 0x002812d0, view weapon anim
  helpers (0x002808b0, 0x00280ed0), the weapon tag/pose helpers using the DObj
  (0x0027f8e0, 0x0027f920, 0x0027fa50, 0x00284d00, 0x00288510), configstring float
  readers (0x0027abb0, 0x0027ad70).
- Renderer (user decision 2026-10-18, see status.md; started 2026-10-18), then back to the client/UI.
  Three layers: (1) the Q3-shaped tr_* side at 0x00234a00-0x00247ad0 (RE_* exports, R_Init/R_Register
  with 48 r_* cvars, R_RenderView -> R_RotateForViewer/R_SetupFrustum/R_SetupProjection, the
  cell-and-portal world walk of tr_dpvs.cpp: R_AddWorldSurfaces -> R_VisitCell_r -> R_AddCellSurfaces/
  Models, R_AddEntitySurfaces; tr.viewParms at 0x0095e3a0 is Q3's viewParms_t); (2) NGL, Treyarch's
  scene library (the binary's own 'ngl*' names: nglwhite, nglSysFont, nglFrontBuffer) at
  0x0005dd00-0x0007c000: frame (nglBeginFrame/nglEndFrame), a scene stack (nglPushScene/PopScene,
  setters on the current scene 0x0090259c), 2D quads (nglInitQuad, nglSetQuad*, nglListAddQuad), meshes
  (.xbmesh, nglListAddMesh), textures (DDS loader, managers), fonts, render-state caches; (3) the shader
  classes at 0x00045790-0x00055400 ('cdWorld' .. 'cdGlass' and 'PCUV'): singletons with a 9-slot vtable
  (Register, GetName, AddToList, Load/ReleaseTexture, Relocate) and a Draw each, toggled by
  nglToggleShaderByName (disable mask 0x00901e8c).
  Second pass (2026-10-18): lighting (R_SetupEntityLighting with a per-entity cache, R_SampleLightGrid,
  NGL light sets), models (R_RegisterModel, DObj_Create/CreateSkeleton/Free, XModel_CalcSkeleton),
  the cell links (CM_CellForPoint, R_AddEntitiesToCells, R_LinkEntityToCell), the effect pool
  (FX_InitEffectPool, FX_SpawnEffect, FX_DrawEffects) and the particle library's entry points
  ('aps*': apsEffect_Create/Init, apsGroup_Create), Q3's myGLMultMatrix and R_RotateForEntity, the
  scene draw (nglRenderSceneTree: pre-callback, opaque list, mid-callback, translucent list,
  post-callback, then nglGenerateRenderTargetMips), refexport slots 9/14 (RE_Get/SetFarCullDist).
  Third pass (2026-10-18): NGL's matrix builders (perspective/ortho/viewport/screen-to-clip, the scene's
  matrix block: +0x10 projection, +0x50 viewport, +0xd0 view, +0x1d0, +0x210), the depth-fog pass
  (0x00073380 = nglDrawFogPass, R_RenderView's mid-scene callback: depth buffer read as a texture + fog
  lookup texture, fog range at scene +0x388..0x394, colour +0x3a0), the light sets (point/directional
  records, object-light constants, shadow projectors), the mesh builder (Begin/AddSection/SetBounds/End,
  also used by RE_AddPolyToScene), debug lines/axes, the draw-list sorts' std::sort helpers (opaque:
  uint key, translucent: float depth), render-target and palette helpers, the FX pool helpers.
  Still open in NGL: scene bytes +0x384/+0x385 (0x0005e490/0x0005e4b0, default 1, cleared by
  R_ClearAlpha: probably depth test/write), the frame-memory pools (0x000778b0, 0x00077b50), the
  container destructors (0x00069b00.., 0x0007bd20..), the debug shape draws 0x0006cb80/cc50/cd30.
  Shader classes: vtable = [Draw, nglShader_Nop | Register, GetName, AddToList, LoadTexture,
  ReleaseTexture, Relocate] (the class pointer is the Register slot); the texture slots are shared
  between classes (nglMaterial_*). The 45/47-byte pairs next to each class (a vtable call that makes
  an object, and a Z_Malloc copy of {vtable, a, b}) are functor clone boilerplate - left unnamed.
  Particle library: only three strings (apsEffect pool/create, apsGroup particles, apsMemory pool);
  the ~40 tiny constructors at 0x000a08c0-0x000a0e00 make its memory pools (0x0009c520). Not named
  further: body reading only, and outside the renderer proper.
  Size (2026-10-18): shader classes 241 functions (138 named), NGL core 0x5dd00-0x7c000 307 (146),
  tr_* 149 (100), particle library 0x94000-0xa8000 324 (5). Open: the memory heap at
  0x00042000-0x00044200 (Z_Malloc's debug heap - not renderer), NGL's dynamic vertex buffers
  (0x00066da0..), the skinned-mesh constants (0x00076620, cdChar), the draw-node sorts' helpers,
  0x00073380 (R_RenderView's mid-scene callback), the particle actions.
- Client/UI (2026-10-18, started; SCR_DrawScreenField itself is only cvar + CG_DrawActiveFrame
  + the in-game menu draws). The Q3 client is thin in this single-player build: CL_Frame ->
  CL_CreateNewCommands, CL_WritePacket -> Netchan_Transmit (loopback inlined),
  CL_ParseServerMessage (svc nop 1, serverCommand 5, snapshot 7, EOF 8; CL_ParseSnapshot
  copies the player state from the local server, no delta), CL_ConfigstringModified /
  CL_SystemInfoChanged, clc_* globals in Q3 order. cls_keyCatchers 0x0092bdf4 (2 UI, 8 cgame).
  The rest of CL_Frame is CoD-specific per-frame work: sound (S_StopAllSounds,
  S_CheckWaveBankNotifies 'wbk_loaded', S_UpdateCrossfade; sound handles are
  index | generation << 12 into 0x00962860), CL_UpdateRumble, FX_UpdateEffects (effects of
  vtable components; SOUND/PARTICLE/LIGHT/SHAKEANDRUMBLE.FX), the streaming loader
  (0x00292c20 and 0x0028c000-0x00292000: AUDIO/VIDEO/ANIM streams, file extension table),
  0x002ea4f0 (99 per-entity effect slots, state machine), 0x0024b140.
  Front end (0x0030e000-0x0033a000, ~600 functions, mostly C++ vtable code): menus are
  .panel files (Panel_Load / Panel_LoadNode_r: node 0x91 picture, 0x94 text), bound to their
  screen by FE_SetupPanel, one FE_Setup<Screen> per panel (names from the panel file names);
  Panel_FindPicture/Panel_FindText by element name; dialogs via UI_ShowDialog (MEM_* memory
  unit dialogs at 0x00333000-0x0033a700 still unnamed). FE_CreateMenus (Com_Init) builds the
  front-end (0xf8) and in-game (0x24) menu objects; SCR_UpdateScreen calls the UI object's
  update (vtable +0xc) or 0x00315520/0x00315560. Open: the screen classes' vtables
  (0x00828xxx-0x0082axxx tables, one per screen), the memory-unit flow, 0x0032f650 (Com_Frame,
  'MOVIE'), the NGL 2D scene code at 0x0005d000-0x00066000 (loader names kSceneCreate etc.).
  RTTI: the binary keeps 266 type descriptors, but only for the level-script runtime
  (AeThreadFunctorN<...>, WaitTilOutputInstN<...>, Broc::entity) and std exceptions - a lever
  for the script runtime's vtables, not for the UI.
- CoD-specific names: Q3-style prefix by module (G_, CG_, CL_, SV_, RE_, Scr_, AI_ ...)
  plus what the body does; `high` only when strings, tables or calls pin the role,
  else `medium`. Evidence column says which.
- re slots 2, 9/14 and the never-filled 3/4/13; the unnamed pointer tables above.
- 27 functions created for table entries are still unnamed (`FUN_`).
- Session 2026-10-20: library pass (tools/data/library_names.csv, 3008 functions) took
  .text unnamed from 7616 to 6243. Then zlib 1.1.4 (deflate, trees, inflate helpers),
  minizip, the gamepad layer IN_Pad* (per-pad 0x58 records at 0x00902400: +0x1c pressed,
  +0x26 released edges), the effect-file stream readers, bad places (BadPlace_*: 32
  records of 0x28 at 0x0098a574) and grenade-throw checks (Actor_CheckGrenadeThrow and
  its solvers: the method compares at 0x00984b88/90/94 are script strings, mapping of
  the three solvers medium). Where the rest is (unnamed .text, 2026-10-20): compiled
  common scripts 0x110000-0x1e3fff (~1,900; no bulk lever yet - ~20 carry their own
  "file::function" string, e.g. 0x0019b440 _spawner_gmi::Allies_Death), engine
  0x1fa000-0x340000 (~1,970; dump with the skill's DumpUnnamed.java and read in slices),
  0x10000-0x45000 (~950: front-end bonus screens, STL instantiations, Z_Malloc's heap),
  XAnim key decoders 0x7c000-0x8a000 (vtable classes, bit-packed channels), the particle
  library 0x94000-0xa8000, NSL sound 0xb0000-0xd0000. Libmatch with --min 5 adds ~50
  more but short patterns false-match (YaccSetStream at 8 places): not applied.
  Pre-existing clashes to fix with RenameFromMap: MSG_ReadString twice in
  manual_names.csv (0x002aa480/0x002aa4f0), Z_Malloc/Z_Free/Mem_AlignedAlloc/
  Mem_AlignedFree each on two addresses.

- Script engine interface (2026-10-20, later): DAT_0095f49c is the level module's import
  block at 0x00922140 (`*param_1 = &DAT_00922140` in each module entry), so every
  runtime pointer 0x00922xxx is slot (addr - 0x00922140) / 4. Filled by 0x0030adb0 and
  its sub-inits 0x003094a0, 0x00301df0, 0x003088b0 (632 slots; the skill's
  SlotStores.java dumps slot -> function). Named so far: 36 slot pointers pScr_<name>,
  string-anchored builtins GScr_*, runtime hubs (ScriptString_*, Scr_Alloc/Free,
  SmallHeap_*, DynArray_*, Scr_Wait/WaitTill, Scr_CreateThread = slot 11, 3972 call
  sites). Call-site evidence per slot: SlotCallStrings.java (e.g. slot 33 called with
  "trigger"/"targetname" = GetEntArray 0x00307d40, slot 35 "targetname" = GetNode
  0x002faf40, slot 25 notify names = queued notify 0x002fbe00, slots 103-107 cvar
  get/set with "r_zfar", slot 164 GScr_GetAI "axis"/"allies").
  Field tables: entity spawn fields at 0x0082ff38 {name, offset, type, 0}: classname
  0x25c, origin 0x140, model 0x258, spawnflags 0x2a0, speed/time 0x2fc, closespeed 0x300,
  target 0x26c, targetname 0x264, teamname 0x2f8, wait 0x384, random 0x388, count/shard
  0x370, health 0x350, dmg 0x358, angles 0x150, rotate 0x390, degrees 0x2f4, key 0x3b4,
  delay 0x38c, spawnitem 0x3b8, groupname 0x274, script_noteworthy 0x27c, maxhealth
  0x354, animname 0x284, persistent_index 0x290, takedamage 0x29c. Path-node fields at
  0x008d3990 (targetname 0x30, script_noteworthy 0x34, target 0x38, animscript 0x3c,
  origin 0x44, angles 0x50, radius 0x54, spawnflags 0x2c, type 0x28); vehicle-node
  fields at 0x008d3a40 (targetname 0, target 4, speed 8, lookahead 0xc,
  script_noteworthy 0x10, origin 0x14, angles 0x2c; nodes in table 0x0095c228).
  Accessor slots 590-827 (code 0x00015dd0-0x0001b580) are named (2026-10-20): even slot =
  getter, odd = setter. Getters return the value (float10/uint) or, for strings, vectors
  and handles, write through param_1 with the handle in param_2; setters are
  `void f(handle, value)`. Prefixes by object: ScrHudElem_ (590-597, byte fields of the
  color dword +0x1c of the 0x7c hud records at 0x0094cc88), ScrPathNode_, ScrVehNode_
  (616-629), ScrEnt_ (field names from the tables above), ScrActor_ (entity +0x240) and
  ScrSentient_ (entity +0x244). Actor/sentient fields have no table: named by type and
  offset (ScrActor_GetFloat_d4, ScrSentient_GetRef_48); Team (+4) and GoalNode (+0x50),
  GoalArc (+0x24) from the AI notes. Rename those when a decompile shows what the field
  is. Per-field debug hooks at 0x008d20e8-0x008d2170 (no names). Slots with no pair in
  the dump (638/639, 646-651, 658/659, ...) are unfilled.
  Builtin slots by body (2026-10-20, later): 180 more named from their bodies and call-site
  strings (GScr_*/Scr_*, each slot pointer labelled pScr_<name>): math 59-84 (random,
  trig, vector helpers; the angle-vector order up/right/forward is by slot order), cvars
  102-107, spawns 108-113, objectives 87-101 (configstring 0x10 + index), mission end
  189-192, links 237-243, doors 253-255, turrets 270-330 (turret info at entity +0x24c),
  anims 290-301, vehicles 463-484 (+0x248), hud 490-503 and 578-589 (fields by offset).
  Slot bodies, fourth pass (2026-10-21, 152 + 2 globals): SlotStores.java on the four init
  functions plus 0x00303bb0 (script movers) and 0x00309030 (actor builtins) - 69 slot targets
  had no function; created them first. Movers follow the builtin order: 405 moveTo, 406-408
  moveX/Y/Z (GScr_MoveAxis 0/1/2), 409 moveGravity (trType 5), 410 rotateTo, 411-413
  rotatePitch/Yaw/Roll, 414 rotateVelocity, 415/416 solid/notsolid. Actor slots 336-404:
  goal pos/node/entity, canSee/canShoot, critical section (state 9), stance, turret, anim
  and orient modes. Hud elements: 0x0094cc88, 16 records of 0x7c, type at +0 (3 shader,
  5 timer, 4/6/7 other timer kinds, 8/9 shader timers). Entity string hashes: groupname
  +0x278, script_noteworthy +0x280, classname +0x260, target +0x270, targetname +0x268,
  animname +0x288 (G_FindEntsBy*). Empty slots -> GScr_EmptyStub<slot>, 509-511
  GScr_ReturnFalse<slot>. Thread/wait primitives 10/12/13/15-17/26/29 are `low` (record
  kinds by vtable, meaning unread). Open in the slots: 176-179 (vector math via
  FUN_002e6710), 185 (configstring 9 "%g"), 340/344/346-352/363/365/366/370/372/375-378/
  382/385/393-396/400/402/404, 565-568 (setters of 0x00897954-60, read by NGL code at
  0x45xxx), 572-575 (a list at 0x008dda50), 828-838 (0x00044bc0 range).
  Slot bodies, third pass (2026-10-21, 137 + 100 slot pointers): every slot the four init
  functions fill with a direct store now has a name. Sound slots 39-47 build commands on the
  0x0095f5b4 queue (FXCmd_*: type at +0x1b4, 3 = sound alias by name, 4 = weapon sound by
  weapon name + index; the ids they return feed FX_StopEffectById / FX_IsEffectActive);
  objective strings 96-98 (G_SetObjectiveString); checkpoint 120 (G_SaveCheckpoint);
  persistent setters 126/127; g_hudManager (0x0095f8f0, made in Com_Init, drawn from
  SCR_UpdateScreen) with hud modes (slot 136) and component setters 132-151; shake/rumble
  157/158 (SHAKEANDRUMBLE.FX); TR_GRAVITY move 318. Many are `low` with field/offset names
  (GScr_SetEntFlag400, GScr_Vehicle_Set200): rename when the field is understood. Unnamed
  callees left: 0x002ae080, 0x00271b20/0x002712a0, 0x002cf330 (anim knob set), 0x0032f650,
  0x0024b700, 0x002c9b30, 0x002028a0; slots 154, 272, 289, 292, 302, 334 unnamed.
  Earlier note - left open (~210): empty stubs, one-line wrappers into unnamed engine code, the
  thread/wait primitives 10/12/15-17 (wait records with vtables 0x00831800-0x00831824)
  and the 0x0095f8f0 setters 132-151 - name them with their callees.
  Compiled scripts: each level module entry (LevelScript_<level>_ModuleEntry, via
  G_FindLevelScriptModule) registers the 48 shared anim/negotiation scripts by name
  (BroFuncRegs.java -> Bro_*); thread starts pass null file/name except the 14
  "InternalMain" ones, but BroThreadSites.java still lists the 2110 thread targets and
  their functor RTTI signature (AeThreadFunctorN<entity, ...>). Gela's entry is split
  at 0x0060201b (function boundary to fix).

- Engine hubs (2026-10-20, night): HubRank.java over 0x1fa000-0x30c000 (1,559 unnamed, 209
  with 3+ callers) -> 95 names. Pinned: bg_weaponDefs (0x0095f4d8, def +4 name hash, +0x98
  type, +0x9c inventory slot type, +0x598 ammo index, +0x5a0 clip index, +0x718 alternate
  weapon, +0x800 AI range) with bg_lastWeaponIndex and BG_FindWeaponIndexForHash; playerState
  weapon bits +0x3f0, slot bytes +0x3f8, ammo +0x110, clip +0x280; the memory contexts
  (MemContext_Push/Pop/Alloc_r/Free_r on the 0x0095f5d0 record); Q3 collision/server names
  (CM_BoxTrace, CM_TransformedBoxTrace, CM_TempBoxModel, CM_PointContents,
  CM_AdjustAreaPortalState, SV_AreaEntities_r, SV_ClipToEntity, SV_ClipHandleForEntity);
  game helpers (G_SetOrigin/G_SetAngle - each has a byte-identical copy named ...2,
  G_SetModel, G_EntLinkTo, G_TryPushingEntity, TeleportPlayer, SetClientViewAngle,
  SaveRegisteredItems, G_RegisterWeapon, Add_Ammo, Drop_Weapon, fire_grenade); pmove
  (PM_AddEvent, PM_AddTouchEnt, PM_CmdScale, PM_WeaponUseAmmo); RTCW's AddLeanToPosition;
  sound channels (S_StopChannel, S_StopSoundHandle, S_Pause/ResumeAllChannels). Left open
  among the hubs: 002ac7e0 (pm anim with toggle bit 0x200 - torso or weapon anim), the
  0x0095f5b4 0x210-byte record pool (002a5a40, filled as type 3/4 with a name), the vehicle
  physics body 0026a3c0, the SIMD box gathers 002c6630/002cb0f0, the trace helpers
  002e95b0/002e84d0/002e88d0, AnimTree/vectoangles variants (002a0ea0/002a11f0/002a1540).
- Engine range, address-order pass (2026-10-20, late night): DumpUnnamed over
  0x1fa000-0x2f6d30, 126 names + 2 globals (manual_names.csv). Pinned: actor blocking pairs
  (ActorBlockList_*, Actor_ResolveBlockingPairs, step-aside node search), bad places
  (BadPlace_Create, Path_CrossesBadPlace), AI shooting (Actor_ComputeHitChance/RollHit/
  ShootWeapon), the Live/voice wrapper (Live_Init/Frame/Render, Voice_*), ragdoll and
  physics-object pools (Ragdoll_Alloc/Update/Free, PhysObj_Create/UpdateAll,
  G_PhysicsLaunch), vehicles (g_vehicleDefs, Vehicle_UpdatePhysics/Turret/FreePhysics),
  Q3 names by body (ConcatArgs, LookAtKiller, TossClientItems, Pickup_Ammo,
  BG_CanItemBeGrabbed, COM_SkipPath, CM_FloodAreaConnections, CM_TransformedPointContents,
  SV_AdjustAreaPortalState, NET_OutOfBandPrint, SV_SendClientMessages), sound banks, the
  FX command queue (FXCmd_Alloc/Commit; the 0x0095f5b4 0x210-byte records are FX commands,
  type 3/4/8 = component kinds, not yet told apart). Still open in that range: the
  streaming/pack loader internals (0x28bc50-0x2926c0), pmove weapon-state helpers, the
  collision trace internals (0x2c6-0x2e8), the cgame view-weapon code - each needs a
  per-module decompile rather than a skim.
  Builtin bodies by slot (same night): SlotStores.java on 0x0030adb0/0x003094a0/0x00301df0/
  0x003088b0 + SlotCallStrings.java at base 0x00922140, joined per slot, then DecompToFile on
  the 145 unnamed bodies: 27 more named (GScr_Attach/DetachAll/GetAttach*, persistent values
  0x0095f4e0+0x560 that carry level_jeep_origin across levels, zone load/query, turret
  usable flag 0x10, physics launch/stop, music stop). ~115 slots stay open: one-line flag
  setters on entity +0x2a4/+0x10 bits, UI-state setters on 0x0095f8f0, hud timer variants
  (0x002fb500 kind 4/6/7), vehicle path/speed setters - name them when those fields are known.
  Front end 0x30c000-0x340000 (same night): its 495 unnamed functions dumped; the 32 with
  distinctive strings named (FE_Bonus* labels/texts, FE_DrawLoadingQuote, Hud_Setup* panel
  hooks, CL_TogglePause/ResumeFromPause, CL_ApplyProfileSettings, SCR_DrawLoadingScreen,
  Save_CheckVersion). Left: ~30 MEM_* memory-unit dialogs (one per error case; name them from
  their callers' state machine 0x0033a530/0x003381c0), duplicate UI_Highlight/UI_Select
  sound hooks (one per screen class - name with the class vtables), and the string-less rest.
- Streaming loader (2026-10-20, later): Stream_GetLoadHandler (0x00292420) picks a loader by
  the HashStr hash (rol-5/xor, lowercase) of the file extension without the dot; hashing the
  binary's extension strings named all 31 handlers Stream_Load<Ext> (19 had no function in
  Ghidra). dds/ddsmp/ifl share Stream_LoadTexture (modes 0/1/3; was R_LoadFogRamp, low),
  xbmesh/xbcell Stream_LoadMeshFile. Per frame Stream_Update runs Stream_UpdateLoad (type 1)
  or Stream_UpdateUnload (type 2: state 4 notify the resource managers in 0x009719fc, 5 release
  the zone's meshes/fonts/textures/anims, 6 free memory). Zones are memory contexts; the loaded
  resources go into per-zone hash maps through wrapper managers (vtables 0x0083813c texture,
  0x00838184 mesh file, 0x008382b8 mesh, 0x008382dc anim, 0x008381a8 skeleton; slot 3 = Add)
  and are found again by Stream_Find<Type> over Stream_ListSearchZones. Open: what btr, atrb,
  aitb, xmpb, xmb, zbb, spg, scn, dcg, cg, db, ib, dlg hold (handlers named by extension only),
  the wrapper managers at 0x00838118/0x0083824c (globals 0x00909828/0x00909830), the
  read path (0x00291960, 0x00292220, 0x0028f2f0) and the zone priority code (0x0028c870,
  0x0028d150, 0x0028e460).
- Naming packets, 2026-10-21 (218 names in three batches, `namepack.py rank/pack` after a fresh
  ExportNaming): the string- and caller-ranked engine leftovers (map restart, subtitles
  Subtitle_Start/Draw, player death, bonus-screen image loaders FE_Bonus*LoadImage/FreeImage, pause
  and profile screens, CL_PlayMovie, IN_Frame), low-range hubs (Heap_* on the heap list 0x008ff638 +
  n*0x494 with current heap 0x00901ae8; HashStr_Hash/Loc_HashKey; StringMap_Find/UintMap_Find =
  implicit binary trees in arrays; IntHashMap_Find = 0xdeadbeef-keyed buckets), the file layer
  (File_OpenRead/GetSize/ReadAt/Close over HandlePool_Resolve, File_PumpRequests), sound voices
  (0xf8-byte records in pool 0x0090a8b4, SndVoice_SetParam marks 0x2e params dirty; mix groups
  'NSL_MASTER'/'NSL_LISTENER'), and the streaming loader's open list from the 2026-10-20 note:
  the pak (0x0095f638) keeps a zone graph (children +0x1c/+0x20, id +0x28, refcount +0x2c,
  distance +0x30/+0x34, priority +0x3c), load requests at pak +0x38[id] (state +0xdc: 0 idle,
  1 reading, 2 unloading, 3 done; chunk state +0xe0), and stream memory as slot sets (two bitsets
  +0/+8: both bits = free full slot, one = half slot; StreamMem_*). Pak_ParseFli sets the region
  (0x0095f5dc from 'de'/'eu' + XGetLanguage) and the collector's-edition flag 0x0095e7f8 from the
  pack name. Still open there: 0x0028ef90, 0x00290350/0x002904b0 (finders by resource kind),
  0x0028ea20/0x0028eaa0 (a per-zone named-entry map at +0xd8). Compiled common scripts
  (0x110000-0x1e3fff) now top the rank by strings; 0x00148d10 is the shared level setup every
  LevelScript_*_InternalMain calls (left unnamed: needs a naming scheme for script threads).
- Compiled script functions by name hash (2026-10-21): each level module stores a
  dispatcher in import slot +0x80 (LevelScript_<level>_CallFunctionByHash, 14) that maps the
  HashStr hash of a "file::function" name to the compiled function (called or thread-started).
  The binary keeps ~235 such names as string values (death/damage callbacks, trigger funcs:
  `self.deathfunc = "prologue::Vehicle_TankDeath"`), so hashing them named 209 targets
  (skill scripts BroHashDispatch.java + broname_hash.py; 139 were not functions yet).
  Scheme: `Bro_<file>__<function>` (double underscore between file and function; the
  file keeps its leading underscore: Bro__spawner_gmi__tanksquish). 10 are low guesses from
  file x identifier combinations (piano_lupo CheckPoint_N, ...); the hash is weak, so a
  guess needs a single candidate (tunisia_utility "JJ20" was rejected). 0x00148d10 is
  Bro__load_gmi__main (file name from the MP XBE's "_load_gmi::main_trigger"). Left: 380
  dispatcher entries whose names have no string (hash only) and the ~1,750 other compiled
  script functions; name those from their callers now that the callbacks read.
- Compiled-script runtime and field accessors (2026-10-21, later): the 257 still unlabeled slot
  pointers of the script interface (slots 0-838 at 0x00922140) got `pScr_<target>` labels, which
  made the runtime 0x119a00-0x121000 readable: 76 one-slot wrappers `ScrCall_<target>`, the
  ScriptString operations (Append*/Concat/Substring/FromInt/FromFloat/Trim*), the key table
  (ScriptKeyTable_Set/Remove/Clear/Clone/FromPairs; values copied/released through the type
  lookup g_scrValueTypeLookup 0x00922e70), the wait family (Scr_WaitExact, Scr_WaitFrames,
  Scr_WaitTillTimeout/Args, Scr_WaitOnObject over wait records 15/16/17; Scr_Suspend* save the
  thread locals and trap with int3), the undefined sentinels g_scrUndefinedFloat/Int, and 18
  script-class destructors (ScrClass_<vtable>_Dtor; 0x007f8b00 is the base of most).
  Entity script fields (`ent.name` in the source) live in a per-entity key table keyed by
  Com_HashKeyNoCase(name); every compiled function reads them through tiny accessors
  (GetEntScriptObject + ScriptKeyTable_Find(hash) [+ Set(hash, undefined)]) or
  isdefined checks. Collecting the key of every ScriptKeyTable_Find call (KeyHashSites.java) and
  reversing the hashes against the XBEs' strings + English words (skill scripts keyhash_*.py: the
  hash is linear, so word pairs/triples are a meet-in-the-middle) recovered 214 of 487 field names
  (anim_movement, script_accuracy, goalnode, script_delay_min/max, last_shot_time, ...). Named
  ScrField_<name> / ScrField_<name>_IsDefined (level copies suffixed with the section), 223 more
  as ScrField_h<hash> until their names are found (rename with RenameFromMap.java). Also
  BroScr_MagicBulletShield, BroScr_Exploder, BroScr_GetFriendlyChainNode, BroScr_HandleNoteTracks.
  Next: the ~1,500 other common-script functions by packet (`namepack.py next N --range
  00120000-001e4000`), now that fields and runtime calls read.
