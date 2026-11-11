#pragma once

// Our own versions of the title CRT's stdio file layer (VC7.1 LIBCMT, statically linked into default.xbe) for the
// own engine host: streams (fopen, fread, ftell, fseek, fclose, fflush) and the low-level handles under them
// (_sopen, _read, _lseek, _close, the handle table), on the title's own data - the stream table (_iob 0x008d13a0,
// __piob 0x0098f9f0), the handle table (__pioinfo 0x0098f8e0), their locks and buffers from the title's heap - so
// original code that still shares a stream (fwrite, fprintf) sees the same state. Pieces of the CRT core they use
// (the lock table, malloc/free, errno, _write) are still the title's and called at their addresses.
// Registered as Compare rows at the original addresses (src/engine/shared/functions.cpp).

#include <cstdint>

#include "compiler.h"

namespace game {

// A stream (FILE): {_ptr, _cnt, _base, _flag, _file, _charbuf, _bufsiz, _tmpfname}; streams past the first 20 are
// _FILEX blocks with their lock (CRITICAL_SECTION) at +0x20.
struct TitleFile {
    uint32_t ptr;
    int32_t cnt;
    uint32_t base;
    uint32_t flag;
    int32_t file;
    uint32_t charbuf;
    int32_t bufsiz;
    uint32_t tmpfname;
};

// --- Streams ---
void XB_CDECL title_lock_file(TitleFile* file);             // 0x001f187f
void XB_CDECL title_unlock_file(TitleFile* file);           // 0x001f18d1
void XB_CDECL title_lock_file2(int32_t index, TitleFile* file);    // 0x001f18ae
void XB_CDECL title_unlock_file2(int32_t index, TitleFile* file);  // 0x001f1900
TitleFile* XB_CDECL title_getstream();                       // 0x001f4486
TitleFile* XB_CDECL title_openfile(const char* name, const char* mode, int32_t share, TitleFile* file);  // 0x001f431e
TitleFile* XB_CDECL title_fsopen(const char* name, const char* mode, int32_t share);  // 0x001eff57
TitleFile* XB_CDECL title_fopen(const char* name, const char* mode);                  // 0x001effb3
int32_t XB_CDECL title_filbuf(TitleFile* file);   // 0x001f45d3
void XB_CDECL title_getbuf(TitleFile* file);      // 0x001f80e4
void XB_CDECL title_freebuf(TitleFile* file);     // 0x001f45a8
int32_t XB_CDECL title_flush(TitleFile* file);    // 0x001efd9e
int32_t XB_CDECL title_fflush_lk(TitleFile* file);  // 0x001efdfb
int32_t XB_CDECL title_fflush(TitleFile* file);     // 0x001efefe
uint32_t XB_CDECL title_fread_lk(void* buffer, uint32_t size, uint32_t count, TitleFile* file);  // 0x001f00a2
uint32_t XB_CDECL title_fread(void* buffer, uint32_t size, uint32_t count, TitleFile* file);     // 0x001f019a
int32_t XB_CDECL title_ftell_lk(TitleFile* file);  // 0x001f04c9
int32_t XB_CDECL title_ftell(TitleFile* file);     // 0x001f062b
int32_t XB_CDECL title_fseek_lk(TitleFile* file, int32_t offset, int32_t origin);  // 0x001f066c
int32_t XB_CDECL title_fseek(TitleFile* file, int32_t offset, int32_t origin);     // 0x001f06fb
int32_t XB_CDECL title_fclose_lk(TitleFile* file);  // 0x001f0744
int32_t XB_CDECL title_fclose(TitleFile* file);     // 0x001f0790

// --- Handles ---
int32_t XB_CDECL title_alloc_osfhnd();                          // 0x001f8ef1
int32_t XB_CDECL title_set_osfhnd(int32_t fh, uint32_t value);  // 0x001f8d5f
int32_t XB_CDECL title_free_osfhnd(int32_t fh);                 // 0x001f8da5
uint32_t XB_CDECL title_get_osfhandle(int32_t fh);              // 0x001f8dee
int32_t XB_CDECL title_lock_fhandle(int32_t fh);                // 0x001f8e2f
void XB_CDECL title_unlock_fhandle(int32_t fh);                 // 0x001f8ecf
int32_t XB_CDECL title_sopen(const char* path, int32_t oflag, int32_t share, int32_t pmode);  // 0x001f9303
int32_t XB_CDECL title_read_lk(int32_t fh, void* buffer, uint32_t count);  // 0x001f46b4
int32_t XB_CDECL title_read(int32_t fh, void* buffer, uint32_t count);     // 0x001f488b
int32_t XB_CDECL title_lseek_lk(int32_t fh, int32_t offset, int32_t origin);  // 0x001f4f43
int32_t XB_CDECL title_lseek(int32_t fh, int32_t offset, int32_t origin);     // 0x001f4fb5
int32_t XB_CDECL title_close_lk(int32_t fh);  // 0x001f5060
int32_t XB_CDECL title_close(int32_t fh);     // 0x001f50e1

}  // namespace game
