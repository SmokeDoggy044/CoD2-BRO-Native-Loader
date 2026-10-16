// Windows: FS stays the thread's TEB, which Windows owns and needs. Its NT_TIB has
// the KPCR's layout, and that is all title code uses of it besides a few KPCR fields:
// - the title's SEH records link into the thread's own chain at FS:[0];
// - FS:[4] (StackBase) finds TLS, so StackBase/StackLimit are switched to the Xbox
//   stack while title code runs, as a fiber switch does;
// - reads of KPCR fields past the NT_TIB (Prcb, Irql, CurrentThread, ...) are
//   patched to read TEB TLS slots that hold the same values.

#include "thread_env.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <mutex>
#include <string>

#include "log.h"
#include "xbe.h"

namespace loader {

namespace {

constexpr uint32_t kTebTlsSlots = 0xE10;  // TEB.TlsSlots[64]
constexpr uint32_t kSectionExecutable = 0x4;

// KPCR fields title code reads through FS that the TEB has something else at.
constexpr uint32_t kKpcrFields[] = {0x20 /* Prcb */, 0x24 /* Irql */, 0x28 /* CurrentThread */, 0x58};
constexpr size_t kIrqlField = 1;
constexpr uint32_t kTibSize = 0x1C;

DWORD g_slots[std::size(kKpcrFields)];
bool g_slots_ready = false;

template <typename T>
uint32_t addr(T* pointer) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
}

NT_TIB& teb_tib() { return *reinterpret_cast<NT_TIB*>(NtCurrentTeb()); }

bool allocate_slots() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (DWORD& slot : g_slots) {
            slot = TlsAlloc();
            // Only the first 64 slots live in the TEB itself, at a fixed FS offset.
            if (slot == TLS_OUT_OF_INDEXES || slot >= 64) {
                LOG_ERROR("thread", "no TEB TLS slot below 64 for the KPCR fields (got %lu)", slot);
                return;
            }
        }
        g_slots_ready = true;
    });
    return g_slots_ready;
}

uint32_t slot_offset(size_t field) { return kTebTlsSlots + 4 * g_slots[field]; }

int field_index(uint32_t offset) {
    for (size_t i = 0; i < std::size(kKpcrFields); ++i)
        if (kKpcrFields[i] == offset) return static_cast<int>(i);
    return -1;
}

}  // namespace

bool detail::activate_fs(ThreadEnv& env) {
    if (!allocate_slots()) return false;
    for (size_t i = 0; i < std::size(kKpcrFields); ++i) {
        uint32_t value;
        std::memcpy(&value, reinterpret_cast<const uint8_t*>(env.kpcr) + kKpcrFields[i], 4);
        TlsSetValue(g_slots[i], reinterpret_cast<void*>(uintptr_t{value}));
    }
    return true;
}

void detail::enter_xbox_stack(ThreadEnv& env) {
    NT_TIB& tib = teb_tib();
    env.host_stack[0] = addr(tib.StackBase);
    env.host_stack[1] = addr(tib.StackLimit);
    tib.StackBase = reinterpret_cast<void*>(uintptr_t{env.stack_base});
    tib.StackLimit = reinterpret_cast<void*>(uintptr_t{env.stack_limit});
    env.seh_chain_end = addr(tib.ExceptionList);
}

void detail::leave_xbox_stack(ThreadEnv& env) {
    NT_TIB& tib = teb_tib();
    tib.StackBase = reinterpret_cast<void*>(uintptr_t{env.host_stack[0]});
    tib.StackLimit = reinterpret_cast<void*>(uintptr_t{env.host_stack[1]});
    // After exit_xbox_thread the title's records are abandoned with its stack.
    tib.ExceptionList = reinterpret_cast<EXCEPTION_REGISTRATION_RECORD*>(uintptr_t{env.seh_chain_end});
}

NtTib& xbox_tib() { return *reinterpret_cast<NtTib*>(NtCurrentTeb()); }

uint8_t* current_irql_location() {
    if (!current_thread_env() || !g_slots_ready) return nullptr;
    return reinterpret_cast<uint8_t*>(NtCurrentTeb()) + slot_offset(kIrqlField);
}

bool prepare_fs_access(const XbeImage& image) {
    if (!allocate_slots()) return false;
    int patched = 0;
    for (const XbeSectionHeader* section : image.sections()) {
        // XBEs flag their constant data executable too.
        const std::string name = image.section_name(*section);
        if (!(section->flags & kSectionExecutable) || name == ".rdata" || name == ".data") continue;
        auto* code = reinterpret_cast<uint8_t*>(uintptr_t{section->virtual_addr});
        const uint32_t size = section->virtual_size;
        // FS-prefixed moves with an absolute 32-bit offset (the forms MSVC emits):
        // A1/A3 (EAX), 8B/89 with ModRM [disp32], 0F B6/B7 (MOVZX) with ModRM [disp32].
        // A byte scan, not a disassembly: it also finds code Ghidra never turned into
        // functions (CoD2: BRO has 36 sites, Ghidra's listing 34).
        for (uint32_t i = 0; i + 8 <= size; ++i) {
            if (code[i] != 0x64) continue;
            const uint8_t* op = code + i + 1;
            uint32_t disp_at;
            if (op[0] == 0xA1 || op[0] == 0xA3)
                disp_at = 2;
            else if ((op[0] == 0x8B || op[0] == 0x89) && (op[1] & 0xC7) == 0x05)
                disp_at = 3;
            else if (op[0] == 0x0F && (op[1] == 0xB6 || op[1] == 0xB7) && (op[2] & 0xC7) == 0x05)
                disp_at = 4;
            else
                continue;
            uint32_t offset;
            std::memcpy(&offset, code + i + disp_at, 4);
            if (offset < kTibSize) continue;  // NT_TIB: the same in the TEB
            // Beyond the KPCR: a 0x64 byte inside another instruction, not an FS access.
            if (offset >= sizeof(Kpcr)) continue;
            const int field = field_index(offset);
            if (field < 0) {
                LOG_WARN("thread", "%08x: FS:[%x] is a KPCR field without a TEB slot", section->virtual_addr + i,
                         offset);
                continue;
            }
            const uint32_t slot = slot_offset(static_cast<size_t>(field));
            std::memcpy(code + i + disp_at, &slot, 4);
            ++patched;
            i += disp_at + 3;
        }
    }
    LOG_INFO("thread", "%d title accesses to KPCR fields through FS redirected to TEB TLS slots", patched);
    return true;
}

}  // namespace loader
