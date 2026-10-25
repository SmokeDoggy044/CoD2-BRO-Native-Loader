#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace loader {

// XBE image header, as found at the start of the file and at the base address.
struct XbeImageHeader {
    char magic[4];  // "XBEH"
    uint8_t signature[256];
    uint32_t base_address;
    uint32_t size_of_headers;
    uint32_t size_of_image;
    uint32_t size_of_image_header;
    uint32_t time_date;
    uint32_t certificate_addr;
    uint32_t num_sections;
    uint32_t section_headers_addr;
    uint32_t init_flags;
    uint32_t entry_point;  // XOR-scrambled
    uint32_t tls_addr;
    uint32_t pe_stack_commit;
    uint32_t pe_heap_reserve;
    uint32_t pe_heap_commit;
    uint32_t pe_base_address;
    uint32_t pe_size_of_image;
    uint32_t pe_checksum;
    uint32_t pe_time_date;
    uint32_t debug_pathname_addr;
    uint32_t debug_filename_addr;
    uint32_t debug_unicode_filename_addr;
    uint32_t kernel_thunk_addr;  // XOR-scrambled
    uint32_t nonkernel_import_dir_addr;
    uint32_t num_library_versions;
    uint32_t library_versions_addr;
    uint32_t kernel_library_version_addr;
    uint32_t xapi_library_version_addr;
    uint32_t logo_bitmap_addr;
    uint32_t logo_bitmap_size;
};
static_assert(offsetof(XbeImageHeader, base_address) == 0x104);
static_assert(offsetof(XbeImageHeader, entry_point) == 0x128);
static_assert(offsetof(XbeImageHeader, kernel_thunk_addr) == 0x158);
static_assert(offsetof(XbeImageHeader, num_library_versions) == 0x160);

// XBEIMAGE_SECTION. XeLoadSection/XeUnloadSection receive pointers to these.
struct XbeSectionHeader {
    uint32_t flags;
    uint32_t virtual_addr;
    uint32_t virtual_size;
    uint32_t raw_addr;
    uint32_t raw_size;
    uint32_t name_addr;
    uint32_t reference_count;
    uint32_t head_shared_ref_count_addr;  // -> uint16_t
    uint32_t tail_shared_ref_count_addr;  // -> uint16_t
    uint8_t digest[20];
};
static_assert(sizeof(XbeSectionHeader) == 56);

constexpr uint32_t kSectionPreload = 0x2;

struct XbeTlsDirectory {
    uint32_t data_start;
    uint32_t data_end;
    uint32_t tls_index_addr;
    uint32_t callback_addr;
    uint32_t size_of_zero_fill;
    uint32_t characteristics;
};

class XbeImage {
public:
    // Reads the file and maps it at its base address. Returns false (after logging)
    // if the file is invalid or the address range is taken.
    bool load(const std::filesystem::path& path);

    uint32_t base() const { return header_->base_address; }
    uint32_t size() const { return header_->size_of_image; }
    bool contains(uint32_t address) const { return address >= base() && address - base() < size(); }

    const XbeImageHeader& header() const { return *header_; }
    uint32_t entry_point() const { return entry_point_; }
    uint32_t kernel_thunk_table() const { return kernel_thunk_table_; }
    const XbeTlsDirectory* tls() const;

    std::vector<XbeSectionHeader*> sections() const;
    std::string section_name(const XbeSectionHeader& section) const;
    XbeSectionHeader* find_section(uint32_t address) const;

    // Identifies the build by its contents rather than its header: every section's name,
    // address and size, and the SHA-1 digest of each section holding code. Data-only
    // sections (strings, tables) count by place and size only, so a release whose
    // localized data differs but whose code and layout match gets the same value.
    uint64_t code_fingerprint() const;
    // Logs each section with its digest, so a report shows where another build differs.
    void log_sections() const;

    // Kernel-side section management (XeLoadSection / XeUnloadSection).
    void load_section(XbeSectionHeader& section);
    void unload_section(XbeSectionHeader& section);

private:
    void copy_section_data(const XbeSectionHeader& section);

    std::vector<uint8_t> file_;
    const XbeImageHeader* header_ = nullptr;  // points into the mapped image
    uint32_t entry_point_ = 0;
    uint32_t kernel_thunk_table_ = 0;
};

}  // namespace loader
