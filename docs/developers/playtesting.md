# Playtesting

## `--diag` sessions

`--diag DIR [S]` writes DIR/log.txt and a screenshot every S seconds
(auto_<seconds>.bmp, default 10).

- **F12** saves mark_<n>.bmp, then freezes the game (sound paused) for a one-line
  note (log + marks.txt), then logs the next frame's draw dump ("MARK n"). A corner
  overlay (not in screenshots) counts the marks.
- **Automatic marks:** each new warning or error once frames are shown (minus known
  harmless ones) gets automark_<n>.bmp, an "A<n>" line in marks.txt (once per folder)
  and a draw dump.
- **F11** draws whatever needs an untranslated feature (texture mode, cube/volume
  texture, texture format) in magenta; the overlay counts those draws and the ones not
  drawn at all (FVF) per frame.
- **Summary:** on exit (window closed, crash, stop on an unimplemented function,
  Ctrl+C) DIR/summary.txt lists how the session ended, the marks, every distinct
  warning/error with count, first time and screenshot, and a frame rate section.
- **Frame pacing** (`src/render/frame_pacing.h`): a "frames" log line per second (fps,
  worst frame, hitches = frames over 1.4x the recent average, e.g. a missed vblank),
  "stall" lines for gaps of 1 s+ (loading), fps on the overlay, and in summary.txt
  slow seconds, worst hitches and stalls with the nearest screenshot. Time --diag
  spends (screenshots, a note prompt) is left out. The title runs at 30 fps.
- Section loads (XeLoadSection) are logged at info.

Reading a session: summary.txt first, then marks.txt, then only the mark screenshots
and "MARK n" log sections that need it.

## Scripted presses

`--press LIST` presses buttons on port 0 at set times, e.g.
`start@20,a@25.5,back@100+15` (seconds after start; `+S` holds the button S seconds,
otherwise a 0.25 s tap). Buttons: `a b x y black white lt rt start back up down left
right ls rs`. Screens that only show while a button is held (the objectives on back)
need a hold.

## Unattended runs

Run on a copy of the save folder (`--data COPY`) so the real saves stay untouched.

- `--level N` (0-13, 0 = prologue) starts mission N directly once the menus are up.
  Press A every 4 s until ~90 s to get past the briefing ("Press A to start"), whose
  load time varies; the level shows at ~60-70 s. Those presses also act in the level:
  in mission 5 they fire the turret and fail the mission ("You failed to obey
  orders!"), so use mission 4 for tests that need the player alive and in control.
- Through the menus instead: `--press
  "a@4,start@5.5,a@7,start@8.5,a@10,start@11.5,a@15,a@18,a@21,a@24,a@27"` plus `a`
  every 4 s from 36 to 76 (profile -> Single Player -> New Game -> difficulty ->
  briefing). Standing still, the player dies at ~90 s.
- `tools/smoke_test.sh OUT [MISSION] [SECONDS]`: one mission (default 4) into the level
  with A presses, frames every 1200, `OUT/last.png` of the last three - the check after
  renaming symbols or replacing a function.
- `tools/feature_sweep.sh OUT [MISSIONS] [SECONDS]` runs each mission's start
  unattended and gathers every summary.txt into OUT/report.txt (mission 4's start: no
  new warnings; full sweep not run yet).

## Recordings for bug reports

A recording replays a run exactly (see [own-engine-host.md](own-engine-host.md), record/replay), so a bug that
happened once can be watched again, stepped through and checked after a fix.

What the player does:
1. Copy the save folder first (Linux `~/.local/share/cod_bro`, Windows `%APPDATA%\cod_bro`) - the replay has to
   start from the saves as they were.
2. Play with `--record bug.rep` added to the command line (with `--diag DIR` too, F12 marks still work). A recording
   covers one boot; quitting to the dashboard or rebooting ends it.
3. Send `bug.rep`, the save-folder copy and the mission played. The recording carries its own settings.ini and
   command line, so the player's later changes do not matter. Size: ~9 MB per minute of play (mostly per-entity
   state hashes, which do not compress).

What we do: `tools/replay_check.sh bug.rep SAVES MISSION OUT` (Linux release, debug, Windows under Wine at once;
`SHOTS=...` adds pictures of chosen frames), or one build with `--replay bug.rep --replay-continue` and `--data` on a
copy of the saves to watch it or attach gdb. The replay logs the first frame that differs from the recording.

`tools/profile_edit.py [SAVE_DIR]` lists the profiles (name, chapters unlocked, play time, signature);
`--unlock-all FILE` unlocks every chapter and bonus (what the chapter-select cheat does: hold both triggers,
Up Up Down Down Left Left Right Right X Right X Right X) and re-signs the file. Use it on a run's `--data` copy
to reach every front-end screen in steered runs.
