# zlib end-to-end test (manual)

The zlib module (game/code/zlib.cpp, unzip.cpp) is never reached by shipped data, so the compare
harness cannot test it in game. This test maps default.xbe, swaps the title CRT's malloc/free/stdio
for host stand-ins and runs ~1000 inflate/deflate/zip scenarios through the title's entry points:
first the original code, then each decompiled function patched in alone, then all of them. Results
and the whole allocation heap must match. `gen.py` builds the inputs (needs Python's zlib).

Not wired into CMake yet; build it 32-bit against libcod_bro_game like tests/leaf_test.cpp. Last run
(2026-10-28): all 1056 scenarios matched in every mode; only heap bytes nothing reads differ
(inflate_huft bytes 2-3 from huft_build's stack temporary; one freed deflate-state word in compress2).
