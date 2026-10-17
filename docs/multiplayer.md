# Multiplayer (codmp_xboxf.xbe)

The disc's second title. The same native loader runs it: the campaign's Multiplayer
item reboots into it (console command `load_mp_exe`, SP FUN_002cf920) and its "Back
to Single Player" back (MP FUN_0023fa90; MP FUN_00262020 restarts MP itself), or
directly with `--xbe <assets>/codmp_xboxf.xbe`.

## How the loader tells the titles apart

`loader::find_title` (loader/patches.cpp) picks a `TitleInfo` by XBE file name:
symbol file (`tools/data/codmp_xbe_symbols.txt`, exported from the Ghidra project
with scripts/ExportSymbols.java), D3D/XPP globals, XAPI patches, RDTSC sites and
unnamed SDK functions. MP addresses were found by matching function hashes against
default.xbe (skill scripts FunctionHashes.java + match_hashes.py): about 80% of MP's
engine functions have an identical twin in SP, the SDK sections nearly all.
Mouse look and the keyboard's controller-style remap work in both titles (MP client
globals: same instructions in the twin functions CL_CreateCmd FUN_0024c130,
CL_MouseMove FUN_002259f0, key label FUN_0027fa60; skill script DataRefs.java lists
them in order). MP's pad bindings come from the same style_*.cfg files
(default_mp.cfg is a PC keyboard leftover). The other PC features (60 fps,
widescreen menus, --level, aim assist) use SP addresses and are off in MP.

## System link

The title's network code is Demonware's bdNet (bdSocket, bdDiscovery,
bdUnicastConnection) on XNET:

- Discovery: a client broadcasts a request (type 0x1b) to UDP port 50000; the
  server answers the sender with type 0x1c and its game info (XNADDR, session key).
  Session list -> Join.
- The client registers the session key, turns the host's XNADDR into an IN_ADDR
  (XNetXnAddrToInAddr), XNetConnect, then talks over a VDP socket (port 1000).
- XNetQos*, XOnlineMatch* are Xbox Live matchmaking only.

Replacement (online/xnet.cpp, online/lan.cpp): the title's sockets are virtual
(protocol + port on a virtual 10.x.y.z address) and travel over one host UDP socket
per game, ports 3074-3077 (the first free one), with a 21-byte header. Broadcasts go
to 255.255.255.255 and 127.0.0.1 on all four ports, so up to four games on one PC
and games on other PCs in the LAN see each other. XNADDR.ina is the virtual
address; XnAddr <-> InAddr is the identity; keys are only remembered; nothing is
encrypted. Not interoperable with real Xboxes.

Two games on one PC (separate save folders):

    L=build/linux-loader/bin/cod_bro_loader; A=build/linux-loader/bin/assets
    $L --data /tmp/host --xbe $A/codmp_xboxf.xbe \
       --press "down@11,a@12,down@14,a@15,down@18,down@18.5,down@19,down@19.5,down@20,down@20.5,down@21,a@22.5" &
    sleep 25; $L --data /tmp/client --xbe $A/codmp_xboxf.xbe --press "down@11,a@12,a@15,a@22"

(host: System Link > Create Match > Start Game; client: System Link > Find Match >
join the first session). Checked 2026-10-17: both in the same Bizerte match, the
host shows "Unknown Soldier Connected".

## Threads

- MPLevelLoader::run (FUN_00189af0, "Level pump thread", created at priority
  HIGHEST by FUN_00268700 with bdThread FUN_000d3750) pumps the network
  (FUN_00189280) every 200 ms while the main thread loads a level. The game heap
  (dlmalloc-style; current heap 0x00451648; malloc 0x00043a10, memalign 0x00043850,
  free 0x00043630, free-in-heap 0x00043470) has no lock: the client crashed in free
  while joining. All four entry points now run under one recursive lock
  (install_multiplayer_heap_lock). Other data the pump shares with the main thread
  is unchecked.

## Xbox Live

Play Xbox Live calls the sign-in starter FUN_0018edd0 (twin of SP FUN_00221eb0;
online object at [0x004b2fec]), which would open the XOnline sign-in UI (XFONT):
replaced, as in SP, by a notice; the menu stays at "Not signed in".

## Not done

- Split screen (if the title has it), 60 fps / widescreen / aim assist for MP,
  launcher and release, Windows test.
