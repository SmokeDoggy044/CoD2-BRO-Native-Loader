#include "title_start.h"

#include <cstring>

#include "qcommon_fs.h"
#include "static_init.h"
#include "title_crt.h"
#include "title_data.h"
#include "title_heap.h"
#include "title_stdio.h"
#include "title_xapi.h"

namespace game {

namespace {

// Title globals the start-up sets (addresses from the original's start-up code).
constexpr uint32_t kTlsSize = 0x0098f668;       // the TLS block size CreateThread gives every thread
constexpr uint32_t kTlsIndex = 0x009268b0;      // _tls_index: the TLS slot, relative to the stack base
constexpr uint32_t kProcessHeap = 0x0098f660;   // XapiProcessHeap
constexpr uint32_t kNotifyNode = 0x00926a74;    // the CRT's XTHREAD_NOTIFICATION {next, previous, routine}
constexpr uint32_t kNotifyHead = 0x008cfd04;    // XapiThreadNotifyRoutineList {first, last}
constexpr uint32_t kLowioTable = 0x0098f8e0;    // __pioinfo[0]
constexpr uint32_t kLowioCount = 0x0098f8c4;    // _nhandle
constexpr uint32_t kStreams = 0x0098f9f0;       // __piob
constexpr uint32_t kStreamCount = 0x0098f9f4;   // _nstream
constexpr uint32_t kIob = 0x008d13a0;           // _iob[20], 0x20 bytes each
constexpr uint32_t kOnexitBegin = 0x0098f9e4;   // __onexitbegin
constexpr uint32_t kOnexitEnd = 0x0098f9e8;     // __onexitend
constexpr uint32_t kStartTime = 0x00926a58;     // ___inittime's FILETIME
constexpr uint32_t kFltCvt = 0x008d1988;        // _cfltcvt_tab[6]
constexpr uint32_t kMbctype = 0x0098f680;       // _mbctype[0x101]
constexpr uint32_t kMbcasemap = 0x0098f7c0;     // _mbcasemap[0x100]
constexpr uint32_t kMbulinfo = 0x0098f7a0;      // __mbulinfo[6]
constexpr uint32_t kMbcodepage = 0x0098f784;
constexpr uint32_t kIsmbcodepage = 0x0098f678;
constexpr uint32_t kMblcid = 0x0098f670;
constexpr uint32_t kMbcinfo = 0x0098f674;       // __ptmbcinfo
constexpr uint32_t kMbcsInitDone = 0x0098f9ec;  // __initmbctable's "done" flag
constexpr uint32_t kSbcsUsed = 0x00926d38;      // set by _setmbcp(-4) only

constexpr uint32_t kFreeptd = 0x001f3472;       // _freeptd (still the title's: only on a thread's exit)
constexpr uint32_t kThreadAttachHook = 0x008d137c;
constexpr uint32_t kThreadDetachHook = 0x008d1380;

uint32_t& word(uint32_t address) { return title_global<uint32_t>(address); }

// INTERIM until 5c (native files): our file calls still go through the loader's kernel, which knows paths by Xbox
// drive letter, so the letters XapiInitProcess made are made here too - D: = the disc (the title's ANSI_STRINGs
// 0x008cfee0 -> 0x008cfee8, kernel IoCreateSymbolicLink at import slot 0x007b3000), T:/U: = the title's save
// folders (XapiSetupPerTitleDriveLetters, still the title's), Z: = the cache (XMountUtilityDrive, the loader's).
// 5c maps title paths to host folders in one place and this goes.
void map_drive_letters() {
    using Link = uint32_t XB_STDCALL(uint32_t, uint32_t);
    (*reinterpret_cast<Link* const*>(uintptr_t{0x007b3000}))(0x008cfee0, 0x008cfee8);
    title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x001edba4)(word(0x00010184 + 8), 0x00010190);
    title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e9b66)(0);
}

// The CRT's thread notify routine (0x001f8400): a new thread gets nothing (its per-thread data comes with the first
// _getptd), an ending one frees its per-thread data.
void XB_STDCALL crt_thread_notify(uint32_t create) {
    if (create) {
        if (const uint32_t hook = word(kThreadAttachHook)) title_function<void XB_CDECL()>(hook)();
        return;
    }
    if (const uint32_t hook = word(kThreadDetachHook)) title_function<void XB_CDECL()>(hook)();
    title_function<void XB_CDECL(uint32_t)>(kFreeptd)(0);
}

// _mtinit: the lock table, the main thread's per-thread data (thread id and handle -1 only; seed and mbcinfo stay
// zero as in the title, unlike a _getptd-made block) in its TLS block, and the notify routine at the list's end.
void init_threads() {
    title_mtinitlocks();
    const uint32_t data = title_crt_calloc(1, 0x88);
    word(word(title_fs_word(0x04) + word(kTlsIndex) * 4) + 0xc) = data;
    word(data) = GetCurrentThreadId();
    word(data + 4) = 0xffffffffu;
    word(kNotifyNode + 8) = own_address(&crt_thread_notify);
    const uint32_t last = word(kNotifyHead + 4);
    word(kNotifyNode) = kNotifyHead;
    word(kNotifyNode + 4) = last;
    word(last) = kNotifyNode;
    word(kNotifyHead + 4) = kNotifyNode;
}

// _ioinit: 32 low-level handle records of 0x28 bytes (handle -1, flags 0, pipe char LF, lock not made); the first
// three are FOPEN|FDEV|FTEXT (0xc1) with no handle - Xbox titles have no standard handles.
void init_lowio() {
    const uint32_t table = title_crt_malloc(0x500);
    word(kLowioTable) = table;
    word(kLowioCount) = 0x20;
    for (uint32_t record = table; record < table + 0x500; record += 0x28) {
        word(record) = 0xffffffffu;
        word(record + 8) = 0;
        title_global<uint8_t>(record + 4) = 0;
        title_global<uint8_t>(record + 5) = 0x0a;
    }
    for (uint32_t i = 0; i < 3; ++i) title_global<uint8_t>(table + i * 0x28 + 4) = 0xc1;
}

// _initstdio: room for 0x200 streams, the first 20 are _iob; stdin/out/err get file -1 (their lowio handles are -1).
void init_stdio() {
    if (word(kStreamCount) == 0) word(kStreamCount) = 0x200;
    else if (static_cast<int32_t>(word(kStreamCount)) < 0x14) word(kStreamCount) = 0x14;
    const uint32_t streams = title_crt_calloc(word(kStreamCount), 4);
    word(kStreams) = streams;
    for (uint32_t i = 0; i < 0x14; ++i) word(streams + i * 4) = kIob + i * 0x20;
    const uint32_t lowio = word(kLowioTable);
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t handle = word(lowio + i * 0x28);
        if (handle == 0xffffffffu || handle == 0) word(kIob + i * 0x20 + 0x10) = 0xffffffffu;
    }
}

// __initmbctable -> _setmbcp(_MB_CP_ANSI): the title's ANSI code page is 0, so _setSBCS (single-byte tables
// cleared), and a fresh multibyte info block copied from them becomes __ptmbcinfo; done under lock 13.
void init_code_page() {
    word(kMbcodepage) = 0xfffffffdu;
    title_lock(0xd);
    word(kSbcsUsed) = 0;
    const uint32_t info = title_crt_malloc(0x220);
    std::memset(reinterpret_cast<void*>(uintptr_t{kMbctype}), 0, 0x101);
    std::memset(reinterpret_cast<void*>(uintptr_t{kMbulinfo}), 0, 0x18);
    word(kMbcodepage) = 0;
    word(kIsmbcodepage) = 0;
    word(kMblcid) = 0;
    word(info) = 0;
    word(info + 4) = word(kMbcodepage);
    word(info + 8) = word(kIsmbcodepage);
    word(info + 0xc) = word(kMblcid);
    std::memcpy(reinterpret_cast<void*>(uintptr_t{info + 0x10}), reinterpret_cast<const void*>(uintptr_t{kMbulinfo}),
                10);
    std::memcpy(reinterpret_cast<void*>(uintptr_t{info + 0x1c}), reinterpret_cast<const void*>(uintptr_t{kMbctype}),
                0x101);
    std::memcpy(reinterpret_cast<void*>(uintptr_t{info + 0x11d}),
                reinterpret_cast<const void*>(uintptr_t{kMbcasemap}), 0x100);
    word(kMbcinfo) = info;
    title_unlock(0xd);
    word(kMbcsInitDone) = 1;
}

}  // namespace

uint32_t XB_STDCALL title_main_thread(uint32_t) {
    word(kProcessHeap) = RtlCreateHeap(2, 0, 0x100000, 0x1000, 0, 0);
    map_drive_letters();
    // _rtinit
    init_threads();
    init_lowio();
    // _cinit: _fpmath (float conversion table, 53-bit precision), then the .CRT$XI initializers in table order.
    const uint32_t cfltcvt = 0x001f6949;
    const uint32_t conversions[6] = {cfltcvt, 0x001f65f6, 0x001f665b /* _fassign */, 0x001f659e, 0x001f6641, cfltcvt};
    for (uint32_t i = 0; i < 6; ++i) word(kFltCvt + i * 4) = conversions[i];
    title_controlfp(0x10000, 0x30000);
    const uint32_t onexit = title_crt_malloc(0x80);
    word(kOnexitEnd) = onexit;
    word(onexit) = 0;
    word(kOnexitBegin) = onexit;
    GetSystemTimeAsFileTime(&word(kStartTime));
    init_stdio();
    init_code_page();
    run_static_initializers();
    Sys_Main();
    return 0;  // not reached: the title quits from its main loop
}

void XB_CDECL title_start() {
    word(kTlsSize) = 0x94;
    word(kTlsIndex) = 0xffffffdbu;
    CloseHandle(CreateThread(0, 0, own_address(&title_main_thread), 0, 0, nullptr));
}

}  // namespace game
