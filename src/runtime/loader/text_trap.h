#pragma once

// Original-code coverage for the own engine host (step 0.3): an INT3 on the first byte of every original
// function that still has its own bytes (not hooked). The first time one runs, the trap logs it with its
// caller, puts the byte back and lets it run on. What the log lists is the original code our host still needs.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace loader {

// `list`: lines "hexaddress name" (tools/text_trap_list.py). `out`: the log, one line per first entry
// ("address name caller"). Call after every hook is in place. False when the platform has no support or a
// file cannot be opened.
bool install_text_trap(const std::filesystem::path& list, const std::filesystem::path& out);

// The host's own check (step 0.3): every original byte of the non-level code sections (.text, XONLINE, XNET, D3DX,
// DSOUND, D3D, XGRPH, XPP) becomes INT3, except what our hooks wrote and the ranges in KEEP_LISTS
// (tools/data/code_section_data.txt, tools/text_clear_keep.py: constants the linker merged into those sections;
// tools/data/level_eh_code.txt, tools/level_eh_code.py: the levels' EH stubs and funclets the linker put in .text,
// level code like the level sections). `snapshot_code_sections` keeps the original
// bytes; call it right after the image is mapped. `install_text_clear` (after every hook) clears the rest; when
// cleared code runs, the trap logs "address caller" to OUT, puts back the original bytes from there to the next
// 16-byte boundary after INT3 padding (one function) and lets it run on. Linux only.
class XbeImage;
void snapshot_code_sections(const XbeImage& image);
bool install_text_clear(const std::filesystem::path& out, const std::vector<std::filesystem::path>& keep_lists);
// XB_CLEAR_NOACCESS=SECTION (debugging): takes that section's access away; call after the restart snapshot.
void text_clear_protect();

}  // namespace loader
