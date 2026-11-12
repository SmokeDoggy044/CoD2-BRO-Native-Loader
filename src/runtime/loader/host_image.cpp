#include "host_image.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

#include "log.h"
#include "xbe.h"

namespace loader {

bool clear_code_sections(const XbeImage& image, const std::vector<std::filesystem::path>& keep_lists) {
    std::vector<std::pair<uint32_t, uint32_t>> keep;
    for (const std::filesystem::path& list : keep_lists) {
        std::ifstream in(list);
        if (!in) {
            LOG_ERROR("host", "cannot read %s", list.string().c_str());
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line);
            std::string begin, end;
            if (fields >> begin >> end)
                keep.emplace_back(static_cast<uint32_t>(std::stoul(begin, nullptr, 16)),
                                  static_cast<uint32_t>(std::stoul(end, nullptr, 16)));
        }
    }
    static const char* const kCode[] = {".text", "XONLINE", "XNET", "D3DX", "DSOUND", "D3D", "XGRPH", "XPP"};
    size_t cleared = 0, kept = 0;
    for (const XbeSectionHeader* section : image.sections()) {
        const std::string name = image.section_name(*section);
        if (std::find(std::begin(kCode), std::end(kCode), name) == std::end(kCode)) continue;
        const uint32_t begin = section->virtual_addr, end = begin + section->virtual_size;
        std::vector<uint8_t> keep_byte(end - begin, 0);
        for (const auto& [b, e] : keep)
            for (uint32_t a = std::max(b, begin); a < std::min(e, end); ++a) keep_byte[a - begin] = 1;
        auto* code = reinterpret_cast<uint8_t*>(uintptr_t{begin});
        for (uint32_t i = 0; i < end - begin; ++i) {
            if (keep_byte[i]) {
                ++kept;
            } else {
                code[i] = 0xCC;
                ++cleared;
            }
        }
    }
    LOG_INFO("host", "code sections: %zu bytes INT3, %zu kept (data and level EH code)", cleared, kept);
    return true;
}

}  // namespace loader
