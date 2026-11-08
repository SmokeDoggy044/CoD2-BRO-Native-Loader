// Console settings: ExQueryNonVolatileSetting and the hard-disk key.
//
// The Xbox keeps its settings in a 256-byte EEPROM. Titles read single values
// (XGetLanguage -> index 7, ...) but also whole blocks (index 0xFF = the user
// section, 0xFFFF = everything) that XAPI parses itself, so we keep a full EEPROM
// image and serve every index as a slice of it. Layout as on xboxdevwiki; the offsets
// XAPI uses in XapipQueryTimeZoneInformation match it.
//
// Defaults are those of an NTSC (North America) console in English with nothing
// special enabled.

#include <array>
#include <cstring>
#include <mutex>

#include "compiler.h"
#include "kernel_internal.h"
#include "objects.h"
#include "types.h"

namespace xbox {

namespace {

constexpr uint32_t kStatusBufferTooSmall = 0xC0000023;
constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;

// Registry-style value types.
constexpr uint32_t kRegBinary = 3;
constexpr uint32_t kRegDword = 4;

// Hard-disk key, used by XAPI to sign saves (XCalculateSignature). Any non-zero
// value works for saves made here; saves from a real console would need its key.
uint8_t XboxHDKey[16] = {0x43, 0x6f, 0x44, 0x32, 0x42, 0x52, 0x4f, 0x2d,
                         0x70, 0x6f, 0x72, 0x74, 0x2d, 0x6b, 0x65, 0x79};  // "CoD2BRO-port-key"

struct Setting {
    uint32_t index;
    uint32_t type;
    uint32_t offset;
    uint32_t size;
};

// XC_* value indices.
constexpr Setting kSettings[] = {
    {0x00, kRegDword, 0x64, 4},     // XC_TIMEZONE_BIAS
    {0x01, kRegBinary, 0x68, 4},    // XC_TZ_STD_NAME
    {0x02, kRegDword, 0x78, 4},     // XC_TZ_STD_DATE
    {0x03, kRegDword, 0x88, 4},     // XC_TZ_STD_BIAS
    {0x04, kRegBinary, 0x6C, 4},    // XC_TZ_DLT_NAME
    {0x05, kRegDword, 0x7C, 4},     // XC_TZ_DLT_DATE
    {0x06, kRegDword, 0x8C, 4},     // XC_TZ_DLT_BIAS
    {0x07, kRegDword, 0x90, 4},     // XC_LANGUAGE
    {0x08, kRegDword, 0x94, 4},     // XC_VIDEO (flags)
    {0x09, kRegDword, 0x98, 4},     // XC_AUDIO (flags)
    {0x0A, kRegDword, 0x9C, 4},     // XC_P_CONTROL_GAMES
    {0x0B, kRegDword, 0xA0, 4},     // XC_P_CONTROL_PASSWORD
    {0x0C, kRegDword, 0xA4, 4},     // XC_P_CONTROL_MOVIES
    {0x0D, kRegDword, 0xA8, 4},     // XC_ONLINE_IP_ADDRESS
    {0x0E, kRegDword, 0xAC, 4},     // XC_ONLINE_DNS_ADDRESS
    {0x0F, kRegDword, 0xB0, 4},     // XC_ONLINE_DEFAULT_GATEWAY_ADDRESS
    {0x10, kRegDword, 0xB4, 4},     // XC_ONLINE_SUBNET_ADDRESS
    {0x11, kRegDword, 0xB8, 4},     // XC_MISC (flags)
    {0x12, kRegDword, 0xBC, 4},     // XC_DVD_REGION
    {0xFF, kRegBinary, 0x60, 0x60},  // XC_MAX_OS: the user section (size unverified)
    {0x100, kRegBinary, 0x34, 12},   // XC_FACTORY_SERIAL_NUMBER
    {0x101, kRegBinary, 0x40, 6},    // XC_FACTORY_ETHERNET_ADDR
    {0x102, kRegBinary, 0x48, 16},   // XC_FACTORY_ONLINE_KEY
    {0x103, kRegDword, 0x58, 4},     // XC_FACTORY_AV_REGION
    {0x104, kRegDword, 0x2C, 4},     // XC_FACTORY_GAME_REGION
    {0xFFFF, kRegBinary, 0x00, 256},  // XC_MAX_ALL: the whole EEPROM
};

struct Eeprom {
    std::mutex mutex;
    std::array<uint8_t, 256> bytes{};

    Eeprom() {
        auto put32 = [&](uint32_t offset, uint32_t value) { std::memcpy(&bytes[offset], &value, 4); };
        std::memcpy(&bytes[0x1C], XboxHDKey, sizeof(XboxHDKey));  // encrypted section: HDD key
        put32(0x2C, 0x00000001);                                     // game region: North America
        std::memcpy(&bytes[0x34], "000000000000", 12);               // serial number
        const uint8_t mac[6] = {0x00, 0x50, 0xF2, 0x00, 0x00, 0x01};  // Microsoft OUI
        std::memcpy(&bytes[0x40], mac, sizeof(mac));
        put32(0x58, 0x00400100);  // AV region: NTSC-M
        put32(0x90, 0x00000001);  // language: English
        put32(0xBC, 0x00000001);  // DVD region 1
        // Time zone (UTC, no DST), video/audio flags, parental control, network and
        // misc flags (auto power-down off) stay 0. Checksums are not computed.
    }
};

Eeprom& g_eeprom = immortal<Eeprom>();

uint32_t XB_STDCALL ExQueryNonVolatileSetting(uint32_t value_index, uint32_t* type, void* value,
                                              uint32_t value_length, uint32_t* result_length) {
    const Setting* setting = nullptr;
    for (const Setting& s : kSettings)
        if (s.index == value_index) setting = &s;
    if (!setting) {
        LOG_WARN("kernel", "ExQueryNonVolatileSetting: unknown setting %#x", value_index);
        return kStatusObjectNameNotFound;
    }
    if (type) *type = setting->type;
    if (result_length) *result_length = setting->size;
    if (value_length < setting->size) return kStatusBufferTooSmall;
    {
        std::lock_guard lock(g_eeprom.mutex);
        std::memcpy(value, &g_eeprom.bytes[setting->offset], setting->size);
    }
    KTRACE("ExQueryNonVolatileSetting(%#x) -> %u bytes", value_index, setting->size);
    return kStatusSuccess;
}

const KernelImpl kSettingsImpls[] = {
    KIMPL(ExQueryNonVolatileSetting),
    KIMPL_DATA(XboxHDKey),
};

}  // namespace

std::span<const KernelImpl> settings_kernel_impls() { return kSettingsImpls; }

void settings_init(const KernelConfig& config) {
    // XC_VIDEO: the flags are in the high word (XGetVideoFlags shifts them down);
    // 0x00010000 = XC_VIDEO_FLAGS_WIDESCREEN, kept by XAPI for every AV pack.
    constexpr uint32_t kVideoWidescreen = 0x00010000;
    std::lock_guard lock(g_eeprom.mutex);
    uint32_t video;
    std::memcpy(&video, &g_eeprom.bytes[0x94], 4);
    video = config.widescreen ? video | kVideoWidescreen : video & ~kVideoWidescreen;
    std::memcpy(&g_eeprom.bytes[0x94], &video, 4);
    std::memcpy(&g_eeprom.bytes[0x90], &config.language, 4);
}

}  // namespace xbox
