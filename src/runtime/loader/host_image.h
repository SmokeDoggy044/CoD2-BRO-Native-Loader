#pragma once

// `--host` (own engine host 0.3, the step before our own executable): the title image as the host keeps it. Right
// after the XBE is mapped, every byte of the non-level code sections (.text, XONLINE, XNET, D3DX, DSOUND, D3D, XGRPH,
// XPP) becomes INT3 except the ranges in KEEP_LISTS (tools/data/code_section_data.txt: constants the linker merged
// into code; tools/data/level_eh_code.txt: the levels' EH stubs and funclets in .text). Every hook is installed on
// top of that, so nothing at setup can depend on original engine code, and code that still runs stops with a
// backtrace (the crash handler's INT3).

#include <cstdint>
#include <filesystem>
#include <vector>

namespace loader {

class XbeImage;
struct XbeSectionHeader;

// Pages a section occupies, charged to the Xbox memory budget while it is loaded.
int32_t section_pages(const XbeSectionHeader& section);
// Pages of the image that are in memory before the title starts (headers and preloaded sections).
int32_t initial_image_pages(const XbeImage& image);

// Fills the code sections with INT3 except the ranges in keep_lists ('begin end' hex lines), then also every range in
// clear_lists (any section: the x86 census clears the level functions' instructions this way).
bool clear_code_sections(const XbeImage& image, const std::vector<std::filesystem::path>& keep_lists,
                         const std::vector<std::filesystem::path>& clear_lists = {});
// x86 census (developers): when `address` (the INT3 that trapped) lies in a range clear_lists filled, logs it once,
// puts that range's original bytes back and returns true - the caller resumes at `address`. False otherwise.
bool census_resume(uint32_t address);

}  // namespace loader
