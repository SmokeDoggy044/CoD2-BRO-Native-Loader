#include "xbe.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "log.h"
#include "os.h"

namespace loader {

namespace {

constexpr uint32_t kEntryRetail = 0xA8FC57AB;
constexpr uint32_t kEntryDebug = 0x94859D4B;
constexpr uint32_t kThunkRetail = 0x5B6D40B6;
constexpr uint32_t kThunkDebug = 0xEFB1F152;
constexpr uint32_t kPageSize = 0x1000;

template <typename T>
T* at(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

void add_shared_page_ref(uint32_t counter_addr, int delta) {
    if (counter_addr) *at<uint16_t>(counter_addr) += delta;
}

bool is_data_section(const std::string& name) {
    for (const char* data : {".rdata", ".data", ".data1", "XON_RD", ".XTLID", "$$XTIMAGE", "$$XSIMAGE"})
        if (name == data) return true;
    return false;
}

void fnv1a(uint64_t& hash, const void* bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) hash = (hash ^ static_cast<const uint8_t*>(bytes)[i]) * 0x100000001b3ull;
}

}  // namespace

bool XbeImage::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LOG_ERROR("xbe", "cannot open %s", path.string().c_str());
        return false;
    }
    file_.assign(std::istreambuf_iterator<char>(in), {});

    if (file_.size() < sizeof(XbeImageHeader) || std::memcmp(file_.data(), "XBEH", 4) != 0) {
        LOG_ERROR("xbe", "%s is not an XBE", path.string().c_str());
        return false;
    }
    XbeImageHeader file_header;
    std::memcpy(&file_header, file_.data(), sizeof(file_header));
    if (file_header.size_of_headers > file_.size() || file_header.size_of_headers > file_header.size_of_image) {
        LOG_ERROR("xbe", "corrupt header sizes");
        return false;
    }

    const uint32_t base = file_header.base_address;
    const uint32_t map_size = (file_header.size_of_image + kPageSize - 1) & ~(kPageSize - 1);
    void* mapped = os::map_image(base, map_size);
    if (!mapped) {
        LOG_ERROR("xbe", "cannot map %08x-%08x", base, base + map_size);
        return false;
    }
    std::memcpy(mapped, file_.data(), file_header.size_of_headers);
    os::keep_executable_headers(base);
    header_ = at<const XbeImageHeader>(base);

    // Retail and debug XBEs scramble these with different keys; pick the one that
    // lands inside the image.
    entry_point_ = header_->entry_point ^ kEntryRetail;
    kernel_thunk_table_ = header_->kernel_thunk_addr ^ kThunkRetail;
    if (!contains(entry_point_)) {
        entry_point_ = header_->entry_point ^ kEntryDebug;
        kernel_thunk_table_ = header_->kernel_thunk_addr ^ kThunkDebug;
    }
    if (!contains(entry_point_) || !contains(kernel_thunk_table_)) {
        LOG_ERROR("xbe", "cannot decode entry point / kernel thunk table");
        return false;
    }

    for (XbeSectionHeader* section : sections()) {
        if (!contains(section->virtual_addr) ||
            section->virtual_size > base + size() - section->virtual_addr ||
            section->raw_addr > file_.size() || section->raw_size > file_.size() - section->raw_addr) {
            LOG_ERROR("xbe", "section %s lies outside the image or file", section_name(*section).c_str());
            return false;
        }
        // Everything is copied up front; the address ranges do not overlap, so the
        // non-preload (per-level) sections can simply stay resident. Reference
        // counts still follow the kernel's rules.
        copy_section_data(*section);
        if (section->flags & kSectionPreload) load_section(*section);
    }

    LOG_INFO("xbe", "mapped %s at %08x-%08x, %u sections, entry %08x, kernel thunks %08x",
             path.filename().string().c_str(), base, base + size(), header_->num_sections, entry_point_,
             kernel_thunk_table_);
    return true;
}

const XbeTlsDirectory* XbeImage::tls() const {
    return header_->tls_addr ? at<const XbeTlsDirectory>(header_->tls_addr) : nullptr;
}

std::vector<XbeSectionHeader*> XbeImage::sections() const {
    std::vector<XbeSectionHeader*> result;
    auto* first = at<XbeSectionHeader>(header_->section_headers_addr);
    for (uint32_t i = 0; i < header_->num_sections; ++i) result.push_back(first + i);
    return result;
}

std::string XbeImage::section_name(const XbeSectionHeader& section) const {
    if (!contains(section.name_addr)) return "?";
    const char* name = at<const char>(section.name_addr);
    return std::string(name, strnlen(name, 16));
}

XbeSectionHeader* XbeImage::find_section(uint32_t address) const {
    for (XbeSectionHeader* section : sections())
        if (address >= section->virtual_addr && address - section->virtual_addr < section->virtual_size)
            return section;
    return nullptr;
}

uint64_t XbeImage::code_fingerprint() const {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const XbeSectionHeader* section : sections()) {
        const std::string name = section_name(*section);
        fnv1a(hash, name.c_str(), name.size() + 1);
        fnv1a(hash, &section->virtual_addr, 4);
        fnv1a(hash, &section->virtual_size, 4);
        if (!is_data_section(name)) fnv1a(hash, section->digest, sizeof(section->digest));
    }
    return hash;
}

void XbeImage::log_sections() const {
    for (const XbeSectionHeader* section : sections()) {
        char digest[41];
        for (int i = 0; i < 20; ++i) snprintf(digest + 2 * i, 3, "%02x", section->digest[i]);
        LOG_INFO("xbe", "  %-10s %08x size %08x sha1 %s", section_name(*section).c_str(), section->virtual_addr,
                 section->virtual_size, digest);
    }
}

void XbeImage::copy_section_data(const XbeSectionHeader& section) {
    uint8_t* dest = at<uint8_t>(section.virtual_addr);
    const uint32_t copy = std::min(section.raw_size, section.virtual_size);
    std::memcpy(dest, file_.data() + section.raw_addr, copy);
    std::memset(dest + copy, 0, section.virtual_size - copy);
}

void XbeImage::load_section(XbeSectionHeader& section) {
    if (section.reference_count++ == 0) {
        // A fresh load resets the section's contents, as reading it from disc would.
        copy_section_data(section);
        add_shared_page_ref(section.head_shared_ref_count_addr, 1);
        add_shared_page_ref(section.tail_shared_ref_count_addr, 1);
    }
}

void XbeImage::unload_section(XbeSectionHeader& section) {
    if (section.reference_count == 0) return;
    if (--section.reference_count == 0) {
        add_shared_page_ref(section.head_shared_ref_count_addr, -1);
        add_shared_page_ref(section.tail_shared_ref_count_addr, -1);
    }
}

}  // namespace loader
