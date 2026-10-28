#pragma once

// The title's zip reader and zlib: Gilles Vollant's unzip 0.15 (as Quake 3 ships it,
// with stdio files and the title CRT's malloc/free) over zlib 1.1.4 ("inflate 1.1.4",
// "deflate 1.1.4" in the XBE). unzip.cpp holds the zip reader, zlib.cpp zlib itself;
// both follow the upstream sources and keep their notices.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// ---- zlib ----------------------------------------------------------------------------

// inflate_huft: one Huffman table entry.
struct InflateHuft {
    uint8_t exop;  // extra bits or operation
    uint8_t bits;  // bits in this code or subcode
    uint8_t pad[2];
    uint32_t base;  // literal, length/distance base, or table offset
};

// z_stream. zalloc/zfree are title code addresses (zcalloc 0x0005af00, zcfree 0x0005af20 by
// default), called as cdecl (opaque, items, size) / (opaque, address).
struct ZStream {
    const uint8_t* next_in;
    uint32_t avail_in;
    uint32_t total_in;
    uint8_t* next_out;
    uint32_t avail_out;
    uint32_t total_out;
    const char* msg;
    void* state;  // InflateState or the deflate state
    uint32_t zalloc;
    uint32_t zfree;
    uint32_t opaque;
    int32_t data_type;
    uint32_t adler;
    uint32_t reserved;
};

// inflate.c's internal_state.
struct InflateState {
    uint32_t mode;  // METHOD 0 .. BLOCKS 7, CHECK4..CHECK1 8-11, DONE 12, BAD 13
    uint32_t sub0;  // method, check.was or marker
    uint32_t sub1;  // check.need
    int32_t nowrap;
    uint32_t wbits;
    struct InflateBlocksState* blocks;
};

// infcodes.c's inflate_codes_state.
struct InflateCodesState {
    uint32_t mode;  // START 0 .. BADCODE 9
    uint32_t len;
    uint32_t sub0;  // code.tree / copy.get / lit
    uint32_t sub1;  // code.need / copy.dist
    uint8_t lbits;
    uint8_t dbits;
    uint8_t pad[2];
    const InflateHuft* ltree;
    const InflateHuft* dtree;
};

// infblock.c's inflate_blocks_state.
struct InflateBlocksState {
    uint32_t mode;  // TYPE 0, LENS, STORED, TABLE, BTREE, DTREE, CODES 6, DRY, DONE, BAD 9
    uint32_t sub[5];  // left / trees {table, index, blens, bb, tb} / decode {codes}
    uint32_t last;
    uint32_t bitk;  // bits in the bit buffer
    uint32_t bitb;  // bit buffer
    InflateHuft* hufts;  // MANY (1440) entries
    uint8_t* window;  // sliding window
    uint8_t* end;     // one past its end
    uint8_t* read;    // window read pointer
    uint8_t* write;   // window write pointer
    uint32_t checkfn;  // title address of adler32 (0x0005ada0) or 0
    uint32_t check;
};

#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(InflateHuft) == 8);
static_assert(sizeof(ZStream) == 0x38);
static_assert(sizeof(InflateState) == 0x18);
static_assert(sizeof(InflateCodesState) == 0x1c);
static_assert(sizeof(InflateBlocksState) == 0x40);
static_assert(offsetof(InflateBlocksState, bitk) == 0x1c);
static_assert(offsetof(InflateBlocksState, write) == 0x34);
static_assert(offsetof(ZStream, msg) == 0x18);
#endif

// 0x00057cf0: inflates a whole zlib stream into dest; *dest_len is the room in, the length out.
int32_t XB_CDECL uncompress(uint8_t* dest, uint32_t* dest_len, const uint8_t* source, uint32_t source_len);
// 0x00057e60
int32_t XB_CDECL inflateReset(ZStream* z);
// 0x00057eb0
int32_t XB_CDECL inflateEnd(ZStream* z);
// 0x00057f00: w < 0 = raw deflate data (no zlib header), as the zip reader uses it.
int32_t XB_CDECL inflateInit2_(ZStream* z, int32_t w, const char* version, int32_t stream_size);
// 0x00058000
int32_t XB_CDECL inflateInit_(ZStream* z, const char* version, int32_t stream_size);
// 0x000585c0
uint32_t XB_CDECL crc32(uint32_t crc, const uint8_t* buf, uint32_t len);
// 0x0005a010
void XB_CDECL inflate_blocks_reset(InflateBlocksState* s, ZStream* z, uint32_t* c);
// 0x0005a090
InflateBlocksState* XB_CDECL inflate_blocks_new(ZStream* z, uint32_t checkfn, uint32_t w);
// 0x0005acc0
int32_t XB_CDECL inflate_blocks_free(InflateBlocksState* s, ZStream* z);
// 0x0005ada0
uint32_t XB_CDECL adler32(uint32_t adler, const uint8_t* buf, uint32_t len);
// 0x0005af00: malloc(items * size), not zeroed.
void* XB_CDECL zcalloc(uint32_t opaque, uint32_t items, uint32_t size);
// 0x0005ca10
InflateCodesState* XB_CDECL inflate_codes_new(uint32_t bl, uint32_t bd, const InflateHuft* tl,
                                              const InflateHuft* td, ZStream* z);
// 0x0005d170
void XB_CDECL inflate_codes_free(InflateCodesState* c, ZStream* z);
// 0x0005d8f0: copies what it can from the window to the output.
int32_t XB_CDECL inflate_flush(InflateBlocksState* s, ZStream* z, int32_t r);

// 0x0005d8c0: the fixed Huffman tables (bit counts 9 and 5); always Z_OK. `stream`
// is unused.
int32_t XB_CDECL inflate_trees_fixed(uint32_t* bl, uint32_t* bd, void** tl, void** td, void* stream);

// 0x0005da20: decodes literal/length and distance codes while at least 258 bytes of
// window and 10 input bytes are left; Z_OK, Z_STREAM_END or Z_DATA_ERROR (msg set).
int32_t XB_CDECL inflate_fast(uint32_t bl, uint32_t bd, const InflateHuft* tl, const InflateHuft* td,
                              InflateBlocksState* s, ZStream* z);

// ---- zip reader ----------------------------------------------------------------------

// unz_file_info (0x50 bytes): the central directory record of one file.
struct UnzFileInfo {
    uint32_t version;
    uint32_t version_needed;
    uint32_t flag;
    uint32_t compression_method;
    uint32_t dos_date;
    uint32_t crc;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t size_filename;
    uint32_t size_file_extra;
    uint32_t size_file_comment;
    uint32_t disk_num_start;
    uint32_t internal_fa;
    uint32_t external_fa;
    uint32_t tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
};

// file_in_zip_read_info_s: the file being read.
struct UnzReadInfo {
    uint8_t* read_buffer;  // UNZ_BUFSIZE (0x4000) bytes of compressed data
    ZStream stream;
    uint32_t pos_in_zipfile;
    uint32_t stream_initialised;
    uint32_t offset_local_extrafield;
    uint32_t size_local_extrafield;
    uint32_t pos_local_extrafield;
    uint32_t crc32;
    uint32_t crc32_wait;
    uint32_t rest_read_compressed;
    uint32_t rest_read_uncompressed;
    void* file;  // the title CRT's FILE*
    uint32_t compression_method;
    uint32_t byte_before_the_zipfile;
};

// unz_s: an open zip file.
struct UnzState {
    void* file;  // the title CRT's FILE*
    uint32_t number_entry;  // unz_global_info
    uint32_t size_comment;
    uint32_t byte_before_the_zipfile;
    uint32_t num_file;
    uint32_t pos_in_central_dir;
    uint32_t current_file_ok;
    uint32_t central_pos;
    uint32_t size_central_dir;
    uint32_t offset_central_dir;
    UnzFileInfo cur_file_info;
    uint32_t offset_curfile;  // unz_file_info_internal
    UnzReadInfo* pfile_in_zip_read;
};

#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(UnzFileInfo) == 0x50);
static_assert(sizeof(UnzReadInfo) == 0x6c);
static_assert(offsetof(UnzReadInfo, crc32) == 0x50);
static_assert(sizeof(UnzState) == 0x80);
static_assert(offsetof(UnzState, cur_file_info) == 0x28);
static_assert(offsetof(UnzState, pfile_in_zip_read) == 0x7c);
#endif

// 0x00056b80: case-insensitive compare of two file names.
int32_t XB_REGS2 XB_CDECL strcmpcasenosensitive_internal(const char* name1, const char* name2);
// 0x00056c20: the offset of the end-of-central-directory record (0 when not found).
uint32_t XB_CDECL unzlocal_SearchCentralDir(void* fin);
// 0x00056e10: reads the central directory record at pos_in_central_dir (file in EAX).
int32_t XB_REGS1 XB_CDECL unzlocal_GetCurrentFileInfoInternal(UnzState* file, UnzFileInfo* pfile_info,
                                                              uint32_t* pfile_info_internal, char* file_name,
                                                              uint32_t file_name_size, void* extra_field,
                                                              uint32_t extra_field_size, char* comment,
                                                              uint32_t comment_size);
// 0x00057260
int32_t XB_CDECL unzGoToNextFile(UnzState* file);
// 0x000572f0: Quake 3's addition; always UNZ_OK once the file is set.
int32_t XB_CDECL unzSetCurrentFileInfoPosition(UnzState* file, uint32_t pos);
// 0x00057470 (s in ESI): checks the local header against the central directory record.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL unzlocal_CheckCurrentFileCoherencyHeader(
    UnzState* s, uint32_t* size_var, uint32_t* offset_local_extrafield, uint32_t* size_local_extrafield);
extern "C" void unzlocal_CheckCurrentFileCoherencyHeader_entry(void);
// 0x00057620
int32_t XB_CDECL unzReadCurrentFile(UnzState* file, void* buf, uint32_t len);
// 0x00057810
int32_t XB_CDECL unztell(UnzState* file);
// 0x000578f0
int32_t XB_CDECL unzCloseCurrentFile(UnzState* file);
// 0x00057b90
int32_t XB_CDECL unzClose(UnzState* file);
// 0x00057bd0
int32_t XB_CDECL unzOpenCurrentFile(UnzState* file);

}  // namespace game
