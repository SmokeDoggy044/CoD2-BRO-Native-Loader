#include "title_stdio.h"

#include <cstring>

#include "title_data.h"
#include "title_xapi.h"
#include "title_crt.h"
#include "title_heap.h"
#include "title_host.h"
#include "title_objects.h"

namespace game {

namespace {

// The title's stream table: _iob (20 FILEs; stdin, stdout, stderr first), __piob (pointers, _nstream of them).
constexpr uint32_t kIob = 0x008d13a0;
constexpr uint32_t kIobLast = 0x008d1600;
constexpr uint32_t kPiob = 0x0098f9f0;
constexpr uint32_t kNstream = 0x0098f9f4;
// The handle table: __pioinfo (64 blocks of 32 ioinfo {osfhnd, osfile +4, pipech +5, lockinitflag +8, lock +0xc},
// 0x28 bytes each), _nhandle, __badioinfo; the stream counter _cflush, _commode, _fmode, _umaskval.
constexpr uint32_t kPioinfo = 0x0098f8e0;
constexpr uint32_t kNhandle = 0x0098f8c4;
constexpr uint32_t kBadIoinfo = 0x008d1960;
constexpr uint32_t kCflush = 0x00926a60;
constexpr uint32_t kCommode = 0x00926d44;
constexpr uint32_t kFmode = 0x00926d48;
constexpr uint32_t kUmask = 0x00926a64;

// The lock table _locktable (36 x {CRITICAL_SECTION*, kind: 1 = preallocated}); the preallocated sections
// (0x1c bytes each); __pfnInitCritSecAndSpinCount; _amsg_exit (fatal, still the title's).
constexpr uint32_t kLockTable = 0x008d1790;
constexpr uint32_t kPreallocatedLocks = 0x00926a90;
constexpr uint32_t kInitCritSecFn = 0x00926d40;
constexpr uint32_t kAmsgExit = 0x001f3537;
constexpr uint32_t kRtlInitializeCriticalSection = 0x007b31d0;
constexpr uint32_t kChsizeLk = 0x001f9bd1; // _chsize_lk(fh, size)
constexpr uint32_t kCommit = 0x001f4264;   // _commit(fh)
constexpr uint32_t kFlsall = 0x001efe29;   // flsall(mode)
constexpr uint32_t kRtlEnterCriticalSection = 0x007b3130;
constexpr uint32_t kRtlLeaveCriticalSection = 0x007b312c;

// FILE flags
constexpr uint32_t kIoRead = 0x1, kIoWrt = 0x2, kIoNbf = 0x4, kIoMyBuf = 0x8, kIoEof = 0x10, kIoErr = 0x20,
                   kIoStrg = 0x40, kIoRw = 0x80, kIoYourBuf = 0x100, kIoSetVBuf = 0x400, kIoCtrlZ = 0x2000,
                   kIoCommit = 0x4000;
// osfile flags
constexpr uint8_t kFOpen = 0x01, kFEofFlag = 0x02, kFCrLf = 0x04, kFPipe = 0x08, kFAppend = 0x20, kFDev = 0x40,
                  kFText = 0x80;

uint32_t addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}
template <typename T>
T* at(uint32_t address) {
    return reinterpret_cast<T*>(uintptr_t{address});
}

void crt_lock(int32_t n) { title_lock(n); }
void crt_unlock(int32_t n) { title_unlock(n); }
int32_t init_crit_sec(uint32_t section) { return title_crt_init_crit_sec_and_spin_count(section, 0xfa0); }
uint32_t& lock_entry(int32_t n) { return title_global<uint32_t>(kLockTable + static_cast<uint32_t>(n) * 8); }
// Our own critical sections under --own-main (title_objects.h), the kernel's otherwise.
void enter_crit_sec(uint32_t section) {
    if (native_platform()) return objects::enter_critical_section(section);
    title_function<void XB_STDCALL(uint32_t)>(title_global<uint32_t>(kRtlEnterCriticalSection))(section);
}
void leave_crit_sec(uint32_t section) {
    if (native_platform()) return objects::leave_critical_section(section);
    title_function<void XB_STDCALL(uint32_t)>(title_global<uint32_t>(kRtlLeaveCriticalSection))(section);
}
uint32_t crt_malloc(uint32_t size) { return own_function<uint32_t XB_CDECL(uint32_t)>(&title_crt_malloc)(size); }
void crt_free(uint32_t block) { own_function<void XB_CDECL(uint32_t)>(&title_crt_free)(block); }
int32_t& crt_errno() { return *own_function<int32_t* XB_CDECL()>(&title_errno)(); }
uint32_t& crt_doserrno() { return *own_function<uint32_t* XB_CDECL()>(&title_doserrno)(); }
void dosmaperr(uint32_t error) { own_function<void XB_CDECL(uint32_t)>(&title_dosmaperr)(error); }

uint32_t* piob() { return at<uint32_t>(title_global<uint32_t>(kPiob)); }

// The ioinfo of handle fh (no range check).
uint8_t* ioinfo(int32_t fh) {
    return at<uint8_t>(title_global<uint32_t>(kPioinfo + (static_cast<uint32_t>(fh) >> 5) * 4)) + (fh & 0x1f) * 0x28;
}
uint8_t& osfile(int32_t fh) { return ioinfo(fh)[4]; }
uint32_t& osfhnd(int32_t fh) { return *reinterpret_cast<uint32_t*>(ioinfo(fh)); }

bool handle_open(int32_t fh) {
    return static_cast<uint32_t>(fh) < title_global<uint32_t>(kNhandle) && (osfile(fh) & kFOpen);
}

void bad_handle() {
    crt_errno() = 9;  // EBADF
    crt_doserrno() = 0;
}

}  // namespace

// --- Streams ----------------------------------------------------------------------------------------------------------

void XB_CDECL title_lock_file(TitleFile* file) {
    // _iob streams use the lock table (_STREAM_LOCKS 0x10 + index), the others their own section (+0x20).
    const uint32_t a = addr(file);
    if (a >= kIob && a <= kIobLast)
        crt_lock(static_cast<int32_t>(a - kIob) / 0x20 + 0x10);
    else
        enter_crit_sec(a + 0x20);
}

void XB_CDECL title_unlock_file(TitleFile* file) {
    const uint32_t a = addr(file);
    if (a >= kIob && a <= kIobLast)
        crt_unlock(static_cast<int32_t>(a - kIob) / 0x20 + 0x10);
    else
        leave_crit_sec(a + 0x20);
}

void XB_CDECL title_lock_file2(int32_t index, TitleFile* file) {
    if (index < 0x14)
        crt_lock(index + 0x10);
    else
        enter_crit_sec(addr(file) + 0x20);
}

void XB_CDECL title_unlock_file2(int32_t index, TitleFile* file) {
    if (index < 0x14)
        crt_unlock(index + 0x10);
    else
        leave_crit_sec(addr(file) + 0x20);
}

TitleFile* XB_CDECL title_getstream() {
    // The first free stream (no _IOREAD|_IOWRT|_IORW), locked; an empty __piob slot gets a new _FILEX. Under the
    // scan lock (1). Null when there is none.
    TitleFile* found = nullptr;
    crt_lock(1);
    const int32_t count = title_global<int32_t>(kNstream);
    for (int32_t i = 0; i < count; ++i) {
        uint32_t* const slots = piob();
        if (slots[i]) {
            if (at<TitleFile>(slots[i])->flag & 0x83) continue;
            if (i > 2 && i < 0x14 && !title_mtinitlocknum(i + 0x10)) break;
            title_lock_file2(i, at<TitleFile>(piob()[i]));
            TitleFile* const file = at<TitleFile>(piob()[i]);
            if (!(file->flag & 0x83)) {
                found = file;
                break;
            }
            title_unlock_file2(i, file);
            continue;
        }
        piob()[i] = crt_malloc(0x3c);
        const uint32_t block = piob()[i];
        if (!block) break;
        if (!init_crit_sec(block + 0x20)) {
            crt_free(piob()[i]);
            piob()[i] = 0;
            break;
        }
        enter_crit_sec(piob()[i] + 0x20);
        found = at<TitleFile>(piob()[i]);
        break;
    }
    if (found) {
        found->cnt = 0;
        found->flag = 0;
        found->base = 0;
        found->ptr = 0;
        found->tmpfname = 0;
        found->file = -1;
    }
    crt_unlock(1);
    return found;
}

TitleFile* XB_CDECL title_openfile(const char* name, const char* mode, int32_t share, TitleFile* file) {
    // The mode string: r / w / a, then + b t c n S R T D (each once). _O_RDONLY 0, _O_WRONLY|_O_CREAT|_O_TRUNC
    // 0x301, _O_WRONLY|_O_CREAT|_O_APPEND 0x109; _O_RDWR 2, _O_BINARY 0x8000, _O_TEXT 0x4000, _O_SEQUENTIAL 0x20,
    // _O_RANDOM 0x10, _O_SHORT_LIVED 0x1000, _O_TEMPORARY 0x40; the stream starts with _commode (c/n: _IOCOMMIT).
    uint32_t stream_flag = title_global<uint32_t>(kCommode);
    int32_t mode_flag;
    switch (*mode) {
    case 'r':
        mode_flag = 0;
        stream_flag |= kIoRead;
        break;
    case 'w':
        mode_flag = 0x301;
        stream_flag |= kIoWrt;
        break;
    case 'a':
        mode_flag = 0x109;
        stream_flag |= kIoWrt;
        break;
    default:
        return nullptr;
    }
    bool more = true, commit_set = false, scan_set = false;
    while (*++mode && more) {
        switch (*mode) {
        case '+':
            if (mode_flag & 2) {
                more = false;
            } else {
                mode_flag = (mode_flag & ~1) | 2;
                stream_flag = (stream_flag & ~3u) | kIoRw;
            }
            break;
        case 'b':
            if (mode_flag & 0xc000) more = false;
            else mode_flag |= 0x8000;
            break;
        case 't':
            if (mode_flag & 0xc000) more = false;
            else mode_flag |= 0x4000;
            break;
        case 'c':
            if (commit_set) {
                more = false;
            } else {
                commit_set = true;
                stream_flag |= kIoCommit;
            }
            break;
        case 'n':
            if (commit_set) {
                more = false;
            } else {
                commit_set = true;
                stream_flag &= ~kIoCommit;
            }
            break;
        case 'S':
            if (scan_set) {
                more = false;
            } else {
                scan_set = true;
                mode_flag |= 0x20;
            }
            break;
        case 'R':
            if (scan_set) {
                more = false;
            } else {
                scan_set = true;
                mode_flag |= 0x10;
            }
            break;
        case 'T':
            if (mode_flag & 0x1000) more = false;
            else mode_flag |= 0x1000;
            break;
        case 'D':
            if (mode_flag & 0x40) more = false;
            else mode_flag |= 0x40;
            break;
        default:
            more = false;
            break;
        }
    }
    const int32_t fh = title_sopen(name, mode_flag, share, 0x1a4);
    if (fh < 0) return nullptr;
    ++title_global<int32_t>(kCflush);
    file->flag = stream_flag;
    file->cnt = 0;
    file->ptr = 0;
    file->base = 0;
    file->tmpfname = 0;
    file->file = fh;
    return file;
}

TitleFile* XB_CDECL title_fsopen(const char* name, const char* mode, int32_t share) {
    TitleFile* const file = title_getstream();
    if (!file) {
        crt_errno() = 0x18;  // EMFILE
        return nullptr;
    }
    TitleFile* const result = title_openfile(name, mode, share, file);
    title_unlock_file(file);
    return result;
}

TitleFile* XB_CDECL title_fopen(const char* name, const char* mode) {
    return title_fsopen(name, mode, 0x40);  // _SH_DENYNO
}

void XB_CDECL title_getbuf(TitleFile* file) {
    // A 4 KB buffer from the heap, else the stream's one-character buffer (unbuffered).
    ++title_global<int32_t>(kCflush);
    file->base = crt_malloc(0x1000);
    if (file->base) {
        file->flag |= kIoMyBuf;
        file->bufsiz = 0x1000;
    } else {
        file->flag |= kIoNbf;
        file->base = addr(&file->charbuf);
        file->bufsiz = 2;
    }
    file->ptr = file->base;
    file->cnt = 0;
}

int32_t XB_CDECL title_filbuf(TitleFile* file) {
    // Refills the buffer from the handle; returns the first character, or -1 (EOF) with _IOEOF / _IOERR set.
    uint32_t flag = file->flag;
    if (!(flag & 0x83) || (flag & kIoStrg)) return -1;
    if (flag & kIoWrt) {
        file->flag = flag | kIoErr;
        return -1;
    }
    file->flag = flag | kIoRead;
    if (!(file->flag & (kIoMyBuf | kIoNbf | kIoYourBuf)))
        title_getbuf(file);
    else
        file->ptr = file->base;
    const int32_t count = title_read(file->file, at<void>(file->base), static_cast<uint32_t>(file->bufsiz));
    file->cnt = count;
    if (count == 0 || count == -1) {
        file->flag |= count ? kIoErr : kIoEof;
        file->cnt = 0;
        return -1;
    }
    if (!(file->flag & (kIoWrt | kIoRw))) {
        const uint8_t* const info = file->file == -1 ? at<uint8_t>(kBadIoinfo) : ioinfo(file->file);
        if ((info[4] & (kFText | kFEofFlag)) == (kFText | kFEofFlag)) file->flag |= kIoCtrlZ;
    }
    if (file->bufsiz == 0x200 && (file->flag & kIoMyBuf) && !(file->flag & kIoSetVBuf)) file->bufsiz = 0x1000;
    file->cnt = count - 1;
    const uint8_t c = *at<uint8_t>(file->ptr);
    ++file->ptr;
    return c;
}

void XB_CDECL title_freebuf(TitleFile* file) {
    if ((file->flag & 0x83) && (file->flag & kIoMyBuf)) {
        crt_free(file->base);
        file->flag &= ~(kIoMyBuf | kIoSetVBuf);
        file->ptr = 0;
        file->base = 0;
        file->cnt = 0;
    }
}

int32_t XB_CDECL title_flush(TitleFile* file) {
    // Writes a write stream's buffered bytes (_write); 0, or -1 with _IOERR. The buffer is empty afterwards.
    int32_t result = 0;
    if ((file->flag & 3) == kIoWrt && (file->flag & (kIoMyBuf | kIoYourBuf))) {
        const int32_t count = static_cast<int32_t>(file->ptr - file->base);
        if (count > 0) {
            if (title_write(file->file, reinterpret_cast<const void*>(uintptr_t{file->base}),
                            static_cast<uint32_t>(count)) == count) {
                if (file->flag & kIoRw) file->flag &= ~kIoWrt;
            } else {
                file->flag |= kIoErr;
                result = -1;
            }
        }
    }
    file->ptr = file->base;
    file->cnt = 0;
    return result;
}

int32_t XB_CDECL title_fflush_lk(TitleFile* file) {
    if (title_flush(file)) return -1;
    if (file->flag & kIoCommit)
        return title_function<int32_t XB_CDECL(int32_t)>(kCommit)(file->file) ? -1 : 0;
    return 0;
}

int32_t XB_CDECL title_fflush(TitleFile* file) {
    if (!file) return title_function<int32_t XB_CDECL(int32_t)>(kFlsall)(0);
    title_lock_file(file);
    const int32_t result = title_fflush_lk(file);
    title_unlock_file(file);
    return result;
}

uint32_t XB_CDECL title_fread_lk(void* buffer, uint32_t size, uint32_t count, TitleFile* file) {
    // From the buffer while it holds data; whole buffer-sized blocks straight from the handle; the rest through
    // _filbuf. Returns the complete items read.
    uint8_t* data = static_cast<uint8_t*>(buffer);
    const uint32_t total = size * count;
    if (total == 0) return 0;
    uint32_t left = total;
    uint32_t block = (file->flag & (kIoMyBuf | kIoNbf | kIoYourBuf)) ? static_cast<uint32_t>(file->bufsiz) : 0x1000;
    while (left) {
        if ((file->flag & (kIoMyBuf | kIoNbf | kIoYourBuf)) && file->cnt) {
            const uint32_t n = left < static_cast<uint32_t>(file->cnt) ? left : static_cast<uint32_t>(file->cnt);
            std::memcpy(data, at<void>(file->ptr), n);
            left -= n;
            file->cnt -= static_cast<int32_t>(n);
            file->ptr += n;
            data += n;
        } else if (left >= block) {
            const uint32_t n = block ? left - left % block : left;
            const int32_t read = title_read(file->file, data, n);
            if (read == 0) {
                file->flag |= kIoEof;
                return (total - left) / size;
            }
            if (read == -1) {
                file->flag |= kIoErr;
                return (total - left) / size;
            }
            left -= static_cast<uint32_t>(read);
            data += read;
        } else {
            const int32_t c = title_filbuf(file);
            if (c == -1) return (total - left) / size;
            *data++ = static_cast<uint8_t>(c);
            --left;
            block = static_cast<uint32_t>(file->bufsiz);
        }
    }
    return count;
}

uint32_t XB_CDECL title_fread(void* buffer, uint32_t size, uint32_t count, TitleFile* file) {
    title_lock_file(file);
    const uint32_t result = title_fread_lk(buffer, size, count, file);
    title_unlock_file(file);
    return result;
}

int32_t XB_CDECL title_ftell_lk(TitleFile* file) {
    // The handle's position less what is still buffered for reading / plus what is buffered for writing; text mode
    // counts the CR of each LF in the buffer.
    const int32_t fh = file->file;
    if (file->cnt < 0) file->cnt = 0;
    int32_t position = title_lseek(fh, 0, 1);
    if (position < 0) return -1;
    if (!(file->flag & (kIoMyBuf | kIoYourBuf))) return position - file->cnt;
    uint32_t offset = file->ptr - file->base;
    if (file->flag & (kIoWrt | kIoRead)) {
        if (osfile(fh) & kFText)
            for (uint32_t p = file->base; p < file->ptr; ++p)
                if (*at<uint8_t>(p) == '\n') ++offset;
    } else if (!(file->flag & kIoRw)) {
        crt_errno() = 0x16;  // EINVAL
        return -1;
    }
    if (position == 0) return static_cast<int32_t>(offset);
    if (file->flag & kIoRead) {
        if (file->cnt == 0) {
            offset = 0;
        } else {
            uint32_t read_count = static_cast<uint32_t>(file->cnt) + (file->ptr - file->base);
            if (osfile(fh) & kFText) {
                if (title_lseek(fh, 0, 2) == position) {
                    const uint32_t end = file->base + read_count;
                    for (uint32_t p = file->base; p < end; ++p)
                        if (*at<uint8_t>(p) == '\n') ++read_count;
                    if (file->flag & kIoCtrlZ) ++read_count;
                } else {
                    title_lseek(fh, position, 0);
                    if (read_count <= 0x200 && (file->flag & kIoMyBuf) && !(file->flag & kIoSetVBuf))
                        read_count = 0x200;
                    else
                        read_count = static_cast<uint32_t>(file->bufsiz);
                    if (osfile(fh) & kFCrLf) ++read_count;
                }
            }
            position -= static_cast<int32_t>(read_count);
        }
    }
    return position + static_cast<int32_t>(offset);
}

int32_t XB_CDECL title_ftell(TitleFile* file) {
    title_lock_file(file);
    const int32_t result = title_ftell_lk(file);
    title_unlock_file(file);
    return result;
}

int32_t XB_CDECL title_fseek_lk(TitleFile* file, int32_t offset, int32_t origin) {
    if (!(file->flag & 0x83) || (origin != 0 && origin != 1 && origin != 2)) {
        crt_errno() = 0x16;  // EINVAL
        return -1;
    }
    file->flag &= ~kIoEof;
    if (origin == 1) {
        offset += title_ftell_lk(file);
        origin = 0;
    }
    title_flush(file);
    if (file->flag & kIoRw)
        file->flag &= ~3u;
    else if ((file->flag & kIoRead) && (file->flag & kIoMyBuf) && !(file->flag & kIoSetVBuf))
        file->bufsiz = 0x200;
    return title_lseek(file->file, offset, origin) == -1 ? -1 : 0;
}

int32_t XB_CDECL title_fseek(TitleFile* file, int32_t offset, int32_t origin) {
    title_lock_file(file);
    const int32_t result = title_fseek_lk(file, offset, origin);
    title_unlock_file(file);
    return result;
}

int32_t XB_CDECL title_fclose_lk(TitleFile* file) {
    int32_t result = -1;
    if (file->flag & 0x83) {
        result = title_flush(file);
        title_freebuf(file);
        if (title_close(file->file) < 0) {
            result = -1;
        } else if (file->tmpfname) {
            crt_free(file->tmpfname);
            file->tmpfname = 0;
        }
    }
    file->flag = 0;
    return result;
}

int32_t XB_CDECL title_fclose(TitleFile* file) {
    // A string stream (sprintf's) is only marked free.
    if (file->flag & kIoStrg) {
        file->flag = 0;
        return -1;
    }
    title_lock_file(file);
    const int32_t result = title_fclose_lk(file);
    title_unlock_file(file);
    return result;
}

// --- Handles ----------------------------------------------------------------------------------------------------------

int32_t XB_CDECL title_lock_fhandle(int32_t fh) {
    // The handle's lock, created on first use (under lock 0xa); 0 when it cannot be created.
    uint8_t* const info = ioinfo(fh);
    uint32_t& init = *reinterpret_cast<uint32_t*>(info + 8);
    if (init == 0) {
        crt_lock(0xa);
        if (init == 0) {
            if (!init_crit_sec(addr(info + 0xc))) {
                crt_unlock(0xa);
                return 0;
            }
            ++init;
        }
        crt_unlock(0xa);
    }
    enter_crit_sec(addr(ioinfo(fh) + 0xc));
    return 1;
}

void XB_CDECL title_unlock_fhandle(int32_t fh) { leave_crit_sec(addr(ioinfo(fh) + 0xc)); }

int32_t XB_CDECL title_alloc_osfhnd() {
    // The first ioinfo that is not open, locked (its lock created on first use), under the handle table lock (0xb);
    // a new block of 32 when all are taken. -1 when there is none.
    int32_t fh = -1;
    if (!title_mtinitlocknum(0xb)) return -1;
    crt_lock(0xb);
    for (int32_t i = 0; i < 0x40; ++i) {
        const uint32_t block = title_global<uint32_t>(kPioinfo + i * 4);
        if (!block) {
            const uint32_t fresh = crt_malloc(0x500);
            if (fresh) {
                title_global<uint32_t>(kPioinfo + i * 4) = fresh;
                title_global<uint32_t>(kNhandle) += 0x20;
                for (uint32_t p = fresh; p < title_global<uint32_t>(kPioinfo + i * 4) + 0x500; p += 0x28) {
                    at<uint8_t>(p)[4] = 0;
                    *at<uint32_t>(p) = 0xffffffff;
                    at<uint8_t>(p)[5] = 0xa;
                    *at<uint32_t>(p + 8) = 0;
                }
                fh = i * 0x20;
                if (!title_lock_fhandle(fh)) fh = -1;
            }
            break;
        }
        for (uint32_t p = block; p < title_global<uint32_t>(kPioinfo + i * 4) + 0x500; p += 0x28) {
            if (at<uint8_t>(p)[4] & kFOpen) continue;
            uint32_t& init = *at<uint32_t>(p + 8);
            if (init == 0) {
                crt_lock(0xa);
                if (init == 0) {
                    if (!init_crit_sec(p + 0xc)) {
                        crt_unlock(0xa);
                        crt_unlock(0xb);
                        return -1;
                    }
                    ++init;
                }
                crt_unlock(0xa);
            }
            enter_crit_sec(p + 0xc);
            if (at<uint8_t>(p)[4] & kFOpen) {
                leave_crit_sec(p + 0xc);
                continue;
            }
            *at<uint32_t>(p) = 0xffffffff;
            fh = static_cast<int32_t>((p - title_global<uint32_t>(kPioinfo + i * 4)) / 0x28) + i * 0x20;
            break;
        }
        if (fh != -1) break;
    }
    crt_unlock(0xb);
    return fh;
}

int32_t XB_CDECL title_set_osfhnd(int32_t fh, uint32_t value) {
    if (static_cast<uint32_t>(fh) < title_global<uint32_t>(kNhandle) && osfhnd(fh) == 0xffffffff) {
        osfhnd(fh) = value;
        return 0;
    }
    bad_handle();
    return -1;
}

int32_t XB_CDECL title_free_osfhnd(int32_t fh) {
    if (static_cast<uint32_t>(fh) < title_global<uint32_t>(kNhandle) && (osfile(fh) & kFOpen) &&
        osfhnd(fh) != 0xffffffff) {
        osfhnd(fh) = 0xffffffff;
        return 0;
    }
    bad_handle();
    return -1;
}

uint32_t XB_CDECL title_get_osfhandle(int32_t fh) {
    if (handle_open(fh)) return osfhnd(fh);
    bad_handle();
    return 0xffffffff;
}

namespace {

// _tsopen_lk (LTCG: the share flag in ECX): the handle for `path` opened with CreateFileA from the _O_* flags.
// *unlock is set once a handle is allocated (and locked).
int32_t tsopen_lk(int32_t* unlock, int32_t* handle, const char* path, int32_t oflag, int32_t pmode, int32_t share) {
    uint8_t file_flags = 0;
    if (!(oflag & 0x8000) && ((oflag & 0x4000) || title_global<int32_t>(kFmode) != 0x8000)) file_flags = kFText;
    uint32_t access;
    switch (oflag & 3) {
    case 0: access = 0x80000000; break;
    case 1: access = 0x40000000; break;
    case 2: access = 0xc0000000; break;
    default:
        crt_errno() = 0x16;
        crt_doserrno() = 0;
        return -1;
    }
    uint32_t share_mode;
    switch (share) {
    case 0x10: share_mode = 0; break;
    case 0x20: share_mode = 1; break;
    case 0x30: share_mode = 2; break;
    case 0x40: share_mode = 3; break;
    default:
        crt_errno() = 0x16;
        crt_doserrno() = 0;
        return -1;
    }
    uint32_t create;
    switch (oflag & 0x700) {
    case 0x000:
    case 0x400: create = 3; break;  // OPEN_EXISTING
    case 0x100: create = 4; break;  // OPEN_ALWAYS
    case 0x200:
    case 0x600: create = 5; break;  // TRUNCATE_EXISTING
    case 0x300: create = 2; break;  // CREATE_ALWAYS
    case 0x500:
    case 0x700: create = 1; break;  // CREATE_NEW
    default:
        crt_errno() = 0x16;
        crt_doserrno() = 0;
        return -1;
    }
    uint32_t attributes = 0x80;  // FILE_ATTRIBUTE_NORMAL
    if ((oflag & 0x100) && !((pmode & ~title_global<int32_t>(kUmask)) & 0x80)) attributes = 1;  // read-only
    if (oflag & 0x40) {  // _O_TEMPORARY: delete on close
        access |= 0x10000;
        attributes |= 0x4000000;
        share_mode |= 4;
    }
    if (oflag & 0x1000) attributes |= 0x100;
    if (oflag & 0x20)
        attributes |= 0x8000000;
    else if (oflag & 0x10)
        attributes |= 0x10000000;
    const int32_t fh = title_alloc_osfhnd();
    if (fh == -1) {
        crt_errno() = 0x18;  // EMFILE
        crt_doserrno() = 0;
        return -1;
    }
    *unlock = 1;
    *handle = fh;
    const uint32_t os_handle = CreateFileA(path, access, share_mode, 0, create, attributes, 0);
    if (os_handle == 0xffffffff) {
        dosmaperr(GetLastError());
        return -1;
    }
    title_set_osfhnd(fh, os_handle);
    file_flags |= kFOpen;
    osfile(fh) = file_flags;
    const uint8_t device = file_flags & (kFDev | kFPipe);
    if (!device && (file_flags & kFText) && (oflag & 2)) {
        // Text read/write: a final CTRL-Z is cut off so appends do not land behind it.
        const int32_t end = title_lseek_lk(fh, -1, 2);
        if (end == -1) {
            if (crt_doserrno() != 0x83) {
                title_close_lk(fh);
                return -1;
            }
        } else {
            uint8_t last = 0;
            if (title_read_lk(fh, &last, 1) == 0 && last == 0x1a &&
                title_function<int32_t XB_CDECL(int32_t, int32_t)>(kChsizeLk)(fh, end) == -1) {
                title_close_lk(fh);
                return -1;
            }
            if (title_lseek_lk(fh, 0, 0) == -1) {
                title_close_lk(fh);
                return -1;
            }
        }
    }
    if (!device && (oflag & 8)) osfile(fh) |= kFAppend;
    return fh;
}

}  // namespace

int32_t XB_CDECL title_sopen(const char* path, int32_t oflag, int32_t share, int32_t pmode) {
    int32_t unlock = 0;
    int32_t fh = 0;
    const int32_t result = tsopen_lk(&unlock, &fh, path, oflag, pmode, share);
    if (unlock) title_unlock_fhandle(fh);
    return result;
}

int32_t XB_CDECL title_read_lk(int32_t fh, void* buffer, uint32_t count) {
    // ReadFile; text mode drops CRs before LFs and stops at CTRL-Z (end of file for files). 0 at the end, -1 on
    // failure (EBADF for ERROR_ACCESS_DENIED; a broken pipe reads as the end).
    int32_t total = 0;
    uint8_t* out = static_cast<uint8_t*>(buffer);
    if (count == 0 || (osfile(fh) & kFEofFlag)) return 0;
    if ((osfile(fh) & (kFPipe | kFDev)) && ioinfo(fh)[5] != 0xa) {
        *out++ = ioinfo(fh)[5];
        --count;
        total = 1;
        ioinfo(fh)[5] = 0xa;
    }
    uint32_t read = 0;
    if (!ReadFile(osfhnd(fh), out, count, &read, nullptr)) {
        const uint32_t error = GetLastError();
        if (error == 5) {  // ERROR_ACCESS_DENIED
            crt_errno() = 9;
            crt_doserrno() = 5;
            return -1;
        }
        if (error == 0x6d) return 0;  // ERROR_BROKEN_PIPE
        dosmaperr(error);
        return -1;
    }
    total += static_cast<int32_t>(read);
    if (!(osfile(fh) & kFText)) return total;
    uint8_t* const base = static_cast<uint8_t*>(buffer);
    if (read != 0 && *base == '\n')
        osfile(fh) |= kFCrLf;
    else
        osfile(fh) &= ~kFCrLf;
    uint8_t* p = base;
    uint8_t* q = base;
    uint8_t* const end = base + total;
    while (p < end) {
        if (*p == 0x1a) {
            if (!(osfile(fh) & kFDev)) osfile(fh) |= kFEofFlag;
            break;
        }
        if (*p != '\r') {
            *q++ = *p++;
            continue;
        }
        if (p < end - 1) {
            if (p[1] == '\n') {
                p += 2;
                *q++ = '\n';
            } else {
                *q++ = *p++;
            }
            continue;
        }
        ++p;
        uint8_t peek;
        uint32_t peek_read = 0;
        uint32_t error = 0;
        if (!ReadFile(osfhnd(fh), &peek, 1, &peek_read, nullptr)) error = GetLastError();
        if (error || peek_read == 0) {
            *q++ = '\r';
        } else if (osfile(fh) & (kFDev | kFPipe)) {
            if (peek == '\n') {
                *q++ = '\n';
            } else {
                *q++ = '\r';
                ioinfo(fh)[5] = peek;
            }
        } else if (q == base && peek == '\n') {
            *q++ = '\n';
        } else {
            title_lseek_lk(fh, -1, 1);
            if (peek != '\n') *q++ = '\r';
        }
    }
    return static_cast<int32_t>(q - base);
}

int32_t XB_CDECL title_read(int32_t fh, void* buffer, uint32_t count) {
    if (!handle_open(fh)) {
        bad_handle();
        return -1;
    }
    title_lock_fhandle(fh);
    int32_t result;
    if (osfile(fh) & kFOpen) {
        result = title_read_lk(fh, buffer, count);
    } else {
        bad_handle();
        result = -1;
    }
    title_unlock_fhandle(fh);
    return result;
}

int32_t XB_CDECL title_lseek_lk(int32_t fh, int32_t offset, int32_t origin) {
    const uint32_t os_handle = title_get_osfhandle(fh);
    if (os_handle == 0xffffffff) {
        crt_errno() = 9;
        return -1;
    }
    const uint32_t position = SetFilePointer(os_handle, static_cast<uint32_t>(offset), nullptr,
                                             static_cast<uint32_t>(origin));
    const uint32_t error = position == 0xffffffff ? GetLastError() : 0;
    if (error) {
        dosmaperr(error);
        return -1;
    }
    osfile(fh) &= ~kFEofFlag;
    return static_cast<int32_t>(position);
}

int32_t XB_CDECL title_lseek(int32_t fh, int32_t offset, int32_t origin) {
    if (!handle_open(fh)) {
        bad_handle();
        return -1;
    }
    title_lock_fhandle(fh);
    int32_t result;
    if (osfile(fh) & kFOpen) {
        result = title_lseek_lk(fh, offset, origin);
    } else {
        bad_handle();
        result = -1;
    }
    title_unlock_fhandle(fh);
    return result;
}

int32_t XB_CDECL title_close_lk(int32_t fh) {
    // stdout and stderr on the same OS handle: only the handle entry is freed.
    uint32_t error = 0;
    if (title_get_osfhandle(fh) != 0xffffffff &&
        !((fh == 1 || fh == 2) && title_get_osfhandle(1) == title_get_osfhandle(2))) {
        if (!CloseHandle(title_get_osfhandle(fh))) error = GetLastError();
    }
    title_free_osfhnd(fh);
    osfile(fh) = 0;
    if (error) {
        dosmaperr(error);
        return -1;
    }
    return 0;
}

int32_t XB_CDECL title_close(int32_t fh) {
    if (!handle_open(fh)) {
        bad_handle();
        return -1;
    }
    title_lock_fhandle(fh);
    int32_t result;
    if (osfile(fh) & kFOpen) {
        result = title_close_lk(fh);
    } else {
        crt_errno() = 9;
        result = -1;
    }
    title_unlock_fhandle(fh);
    return result;
}

// --- Errors and the write side (2026-11-11) --------------------------------------------------------------------------

uint32_t* XB_CDECL title_doserrno() { return &title_thread_data()->dos_error; }

void XB_CDECL title_dosmaperr(uint32_t error) {
    // _doserrno = the OS error; errno from the CRT's 45-entry table {os error, errno} at 0x008d1208, else EACCES for
    // ERROR_WRITE_PROTECT..ERROR_SHARING_BUFFER_EXCEEDED (0x13..0x24), ENOEXEC for 0xbc..0xca, EINVAL otherwise.
    TitleThreadData* const data = title_thread_data();
    data->dos_error = error;
    for (uint32_t i = 0; i < 0x2d; ++i)
        if (title_global<uint32_t>(0x008d1208 + i * 8) == error) {
            data->error = static_cast<int32_t>(title_global<uint32_t>(0x008d120c + i * 8));
            return;
        }
    if (error >= 0x13 && error <= 0x24) data->error = 13;
    else if (error >= 0xbc && error <= 0xca) data->error = 8;
    else data->error = 22;
}

int32_t XB_CDECL title_isatty(int32_t fh) {
    if (static_cast<uint32_t>(fh) >= title_global<uint32_t>(kNhandle)) return 0;
    return static_cast<int8_t>(osfile(fh)) & kFDev;
}

int64_t XB_CDECL title_lseeki64_lk(int32_t fh, int64_t offset, int32_t origin) {
    const uint32_t os_handle = title_get_osfhandle(fh);
    if (os_handle == 0xffffffff) {
        crt_errno() = 9;
        return -1;
    }
    uint32_t high = static_cast<uint32_t>(static_cast<uint64_t>(offset) >> 32);
    const uint32_t low = SetFilePointer(os_handle, static_cast<uint32_t>(offset), &high, static_cast<uint32_t>(origin));
    if (low == 0xffffffff) {
        const uint32_t error = GetLastError();
        if (error) {
            dosmaperr(error);
            return -1;
        }
    }
    osfile(fh) &= ~kFEofFlag;
    return static_cast<int64_t>(uint64_t{high} << 32 | low);
}

int32_t XB_CDECL title_write_lk(int32_t fh, const void* buffer, uint32_t count) {
    // WriteFile; append handles seek to the end first; text mode writes LF as CR LF through a 1 KB staging buffer
    // (the result counts the caller's bytes). Nothing written: -1 with EBADF for ERROR_ACCESS_DENIED, the mapped
    // error for others, ENOSPC without an error - except a device whose first byte is CTRL-Z (0).
    if (count == 0) return 0;
    if (osfile(fh) & kFAppend) title_lseeki64_lk(fh, 0, 2);
    const auto* const in = static_cast<const uint8_t*>(buffer);
    uint32_t written = 0, added_crs = 0, error = 0;
    if (osfile(fh) & kFText) {
        uint32_t used = 0;  // caller bytes taken so far
        while (used < count) {
            uint8_t staging[0x404];  // an LF at 0x3ff takes it to 0x401
            uint32_t n = 0;
            while (used < count && n < 0x400) {
                const uint8_t c = in[used++];
                if (c == '\n') {
                    ++added_crs;
                    staging[n++] = '\r';
                }
                staging[n++] = c;
            }
            uint32_t done = 0;
            if (!WriteFile(osfhnd(fh), staging, n, &done, nullptr)) {
                error = GetLastError();
                break;
            }
            written += done;
            if (static_cast<int32_t>(done) < static_cast<int32_t>(n)) break;
        }
    } else {
        uint32_t done = 0;
        if (WriteFile(osfhnd(fh), buffer, count, &done, nullptr)) written = done;
        else error = GetLastError();
    }
    if (written) return static_cast<int32_t>(written - added_crs);
    if (error) {
        if (error == 5) {  // ERROR_ACCESS_DENIED
            crt_errno() = 9;
            crt_doserrno() = 5;
        } else {
            dosmaperr(error);
        }
        return -1;
    }
    if ((osfile(fh) & kFDev) && in[0] == 0x1a) return 0;
    crt_errno() = 28;  // ENOSPC
    crt_doserrno() = 0;
    return -1;
}

int32_t XB_CDECL title_write(int32_t fh, const void* buffer, uint32_t count) {
    if (!handle_open(fh)) {
        bad_handle();
        return -1;
    }
    title_lock_fhandle(fh);
    int32_t result;
    if (osfile(fh) & kFOpen) {
        result = title_write_lk(fh, buffer, count);
    } else {
        bad_handle();
        result = -1;
    }
    title_unlock_fhandle(fh);
    return result;
}

int32_t XB_CDECL title_flsbuf(int32_t ch, TitleFile* file) {
    // putc's slow path: the character into a fresh buffer (one is allocated unless stdout/stderr is a device) and
    // the old contents written, or straight out for an unbuffered stream. The character (0..255), or -1 with _IOERR.
    uint32_t flag = file->flag;
    const int32_t fh = file->file;
    if (!(flag & (kIoWrt | kIoRw)) || (flag & kIoStrg)) {
        file->flag = flag | kIoErr;
        return -1;
    }
    if (flag & kIoRead) {
        file->cnt = 0;
        if (!(flag & kIoEof)) {
            file->flag = flag | kIoErr;
            return -1;
        }
        file->ptr = file->base;
        file->flag = flag & ~kIoRead;
    }
    file->flag = (file->flag | kIoWrt) & ~kIoEof;
    file->cnt = 0;
    int32_t written = 0, wanted;
    if (!(file->flag & (kIoMyBuf | kIoNbf | kIoYourBuf)) &&
        !((addr(file) == kIob + 0x20 || addr(file) == kIob + 0x40) && title_isatty(fh)))
        title_getbuf(file);
    const uint8_t c = static_cast<uint8_t>(ch);
    if (file->flag & (kIoMyBuf | kIoYourBuf)) {
        wanted = static_cast<int32_t>(file->ptr - file->base);
        file->ptr = file->base + 1;
        file->cnt = file->bufsiz - 1;
        if (wanted > 0) {
            written = title_write(fh, reinterpret_cast<const void*>(uintptr_t{file->base}), static_cast<uint32_t>(wanted));
        } else {
            const uint8_t* info = fh == -1 ? at<uint8_t>(kBadIoinfo) : ioinfo(fh);
            if (info[4] & kFAppend) title_lseek(fh, 0, 2);
        }
        *at<uint8_t>(file->base) = c;
    } else {
        wanted = 1;
        written = title_write(fh, &c, 1);
    }
    if (written != wanted) {
        file->flag |= kIoErr;
        return -1;
    }
    return c;
}

uint32_t XB_CDECL title_fwrite_lk(const void* buffer, uint32_t size, uint32_t count, TitleFile* file) {
    // Fills the stream's buffer; whole buffer-sized blocks (all of it when unbuffered) go straight to _write after a
    // flush; a remainder smaller than the buffer goes through _flsbuf one byte, then the buffer again. Returns the
    // number of whole items written.
    const uint32_t total = size * count;
    if (total == 0) return 0;
    const auto* data = static_cast<const uint8_t*>(buffer);
    uint32_t left = total;
    uint32_t block = (file->flag & (kIoMyBuf | kIoNbf | kIoYourBuf)) ? static_cast<uint32_t>(file->bufsiz) : 0x1000;
    while (left) {
        if ((file->flag & (kIoMyBuf | kIoYourBuf)) && file->cnt) {
            const uint32_t n = left < static_cast<uint32_t>(file->cnt) ? left : static_cast<uint32_t>(file->cnt);
            std::memcpy(at<uint8_t>(file->ptr), data, n);
            left -= n;
            file->cnt -= static_cast<int32_t>(n);
            file->ptr += n;
            data += n;
        } else if (left >= block) {
            if ((file->flag & (kIoMyBuf | kIoYourBuf)) && title_flush(file)) return (total - left) / size;
            const uint32_t n = block ? left - left % block : left;
            const int32_t done = title_write(file->file, data, n);
            if (done == -1) {
                file->flag |= kIoErr;
                return (total - left) / size;
            }
            left -= static_cast<uint32_t>(done);
            data += done;
            if (static_cast<uint32_t>(done) < n) {
                file->flag |= kIoErr;
                return (total - left) / size;
            }
        } else {
            if (title_flsbuf(static_cast<int8_t>(*data), file) == -1) return (total - left) / size;
            ++data;
            --left;
            block = file->bufsiz > 0 ? static_cast<uint32_t>(file->bufsiz) : 1;
        }
    }
    return count;
}

uint32_t XB_CDECL title_fwrite(const void* buffer, uint32_t size, uint32_t count, TitleFile* file) {
    title_lock_file(file);
    const uint32_t result = title_fwrite_lk(buffer, size, count, file);
    title_unlock_file(file);
    return result;
}

// --- The lock table (2026-11-11) -------------------------------------------------------------------------------------

int32_t XB_STDCALL title_crt_init_crit_sec_no_spin_count(uint32_t section, uint32_t) {
    if (native_platform())
        objects::initialize_critical_section(section);
    else
        title_function<void XB_STDCALL(uint32_t)>(title_global<uint32_t>(kRtlInitializeCriticalSection))(section);
    return 1;
}

int32_t XB_CDECL title_crt_init_crit_sec_and_spin_count(uint32_t section, uint32_t spin) {
    // Through __pfnInitCritSecAndSpinCount, set to the no-spin-count version on first use (the SEH frame turning a
    // STATUS_NO_MEMORY exception into 0 + ERROR_NOT_ENOUGH_MEMORY is not ported: our Rtl call does not raise).
    uint32_t& fn = title_global<uint32_t>(kInitCritSecFn);
    if (!fn) fn = own_address(&title_crt_init_crit_sec_no_spin_count);
    return title_function<int32_t XB_STDCALL(uint32_t, uint32_t)>(fn)(section, spin);
}

int32_t XB_CDECL title_mtinitlocks() {
    // The preallocated locks (kind 1) get the static sections in order; 0 when one fails (its entry cleared).
    uint32_t next = kPreallocatedLocks;
    for (int32_t n = 0; n < 0x24; ++n) {
        if (title_global<uint32_t>(kLockTable + static_cast<uint32_t>(n) * 8 + 4) != 1) continue;
        lock_entry(n) = next;
        next += 0x1c;
        if (!init_crit_sec(lock_entry(n))) {
            lock_entry(n) = 0;
            return 0;
        }
    }
    return 1;
}

int32_t XB_CDECL title_mtinitlocknum(int32_t n) {
    // A lock made on first use: a 0x1c-byte section from malloc, set up under lock 10 (_LOCKTAB_LOCK); a section
    // another thread made first wins and ours is freed. 0 (errno ENOMEM) when malloc or the set-up fails.
    if (lock_entry(n)) return 1;
    const uint32_t section = crt_malloc(0x1c);
    if (!section) {
        crt_errno() = 12;
        return 0;
    }
    crt_lock(10);
    if (!lock_entry(n)) {
        if (!init_crit_sec(section)) {
            crt_free(section);
            crt_errno() = 12;
            crt_unlock(10);
            return 0;
        }
        lock_entry(n) = section;
    } else {
        crt_free(section);
    }
    crt_unlock(10);
    return 1;
}

void XB_CDECL title_lock(int32_t n) {
    if (!lock_entry(n) && !title_mtinitlocknum(n)) title_function<void XB_CDECL(int32_t)>(kAmsgExit)(0x11);  // _RT_LOCK
    enter_crit_sec(lock_entry(n));
}

void XB_CDECL title_unlock(int32_t n) { leave_crit_sec(lock_entry(n)); }

}  // namespace game
