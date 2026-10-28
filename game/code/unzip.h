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
// 0x00058020
int32_t XB_CDECL inflate(ZStream* z, int32_t flush);
// 0x000585c0
uint32_t XB_CDECL crc32(uint32_t crc, const uint8_t* buf, uint32_t len);
// 0x0005a010
void XB_CDECL inflate_blocks_reset(InflateBlocksState* s, ZStream* z, uint32_t* c);
// 0x0005a090
InflateBlocksState* XB_CDECL inflate_blocks_new(ZStream* z, uint32_t checkfn, uint32_t w);
// 0x0005a130
int32_t XB_CDECL inflate_blocks(InflateBlocksState* s, ZStream* z, int32_t r);
// 0x0005acc0
int32_t XB_CDECL inflate_blocks_free(InflateBlocksState* s, ZStream* z);
// 0x0005ada0
uint32_t XB_CDECL adler32(uint32_t adler, const uint8_t* buf, uint32_t len);
// 0x0005af00: malloc(items * size), not zeroed.
void* XB_CDECL zcalloc(uint32_t opaque, uint32_t items, uint32_t size);
// 0x0005ca10
InflateCodesState* XB_CDECL inflate_codes_new(uint32_t bl, uint32_t bd, const InflateHuft* tl,
                                              const InflateHuft* td, ZStream* z);
// 0x0005ca50
int32_t XB_CDECL inflate_codes(InflateBlocksState* s, ZStream* z, int32_t r);
// 0x0005d170
void XB_CDECL inflate_codes_free(InflateCodesState* c, ZStream* z);
// 0x0005d190 (b in ECX, m in EAX): builds the Huffman decoding tables for n code lengths.
// frame: the two words at entry - 0xd8 and entry - 0xd4, which the original reads without
// setting them (the asm entry passes them in and writes them back).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL huft_build(uint32_t* b, uint32_t* m, uint32_t n, uint32_t s,
                                                       const uint32_t* d, const uint32_t* e, InflateHuft** t,
                                                       InflateHuft* hp, uint32_t* hn, uint32_t* v, uint32_t* frame);
extern "C" void huft_build_entry(void);
// 0x0005d680
int32_t XB_CDECL inflate_trees_bits(uint32_t* c, uint32_t* bb, InflateHuft** tb, InflateHuft* hp, ZStream* z);
// 0x0005d730
int32_t XB_CDECL inflate_trees_dynamic(uint32_t nl, uint32_t nd, uint32_t* c, uint32_t* bl, uint32_t* bd,
                                       InflateHuft** tl, InflateHuft** td, InflateHuft* hp, ZStream* z);
// 0x0005d8f0: copies what it can from the window to the output.
int32_t XB_CDECL inflate_flush(InflateBlocksState* s, ZStream* z, int32_t r);

// 0x0005d8c0: the fixed Huffman tables (bit counts 9 and 5); always Z_OK. `stream`
// is unused.
int32_t XB_CDECL inflate_trees_fixed(uint32_t* bl, uint32_t* bd, void** tl, void** td, void* stream);

// 0x0005da20: decodes literal/length and distance codes while at least 258 bytes of
// window and 10 input bytes are left; Z_OK, Z_STREAM_END or Z_DATA_ERROR (msg set).
int32_t XB_CDECL inflate_fast(uint32_t bl, uint32_t bd, const InflateHuft* tl, const InflateHuft* td,
                              InflateBlocksState* s, ZStream* z);

// ---- deflate (no caller in the title reaches it: compress2's callers 0x00057e40 and
// 0x00248370 have no callers or stored pointers) ---------------------------------------

// ct_data: freq or code, dad or len.
struct CtData {
    uint16_t fc;
    uint16_t dl;
};

// static_tree_desc_s.
struct StaticTreeDesc {
    const CtData* static_tree;
    const int32_t* extra_bits;
    int32_t extra_base;
    int32_t elems;
    int32_t max_length;
};

// tree_desc_s.
struct TreeDesc {
    CtData* dyn_tree;
    int32_t max_code;
    const StaticTreeDesc* stat_desc;
};

// deflate.h's internal_state (0x16b8 bytes).
struct DeflateState {
    ZStream* strm;
    int32_t status;  // INIT_STATE 42, BUSY_STATE 113, FINISH_STATE 666
    uint8_t* pending_buf;
    uint32_t pending_buf_size;
    uint8_t* pending_out;
    int32_t pending;
    int32_t noheader;
    uint8_t data_type;
    uint8_t method;
    uint8_t pad_1e[2];
    int32_t last_flush;
    uint32_t w_size;
    uint32_t w_bits;
    uint32_t w_mask;
    uint8_t* window;
    uint32_t window_size;
    uint16_t* prev;
    uint16_t* head;
    uint32_t ins_h;
    uint32_t hash_size;
    uint32_t hash_bits;
    uint32_t hash_mask;
    uint32_t hash_shift;
    int32_t block_start;
    uint32_t match_length;
    uint32_t prev_match;
    int32_t match_available;
    uint32_t strstart;
    uint32_t match_start;
    uint32_t lookahead;
    uint32_t prev_length;
    uint32_t max_chain_length;
    uint32_t max_lazy_match;
    int32_t level;
    int32_t strategy;
    uint32_t good_match;
    int32_t nice_match;
    CtData dyn_ltree[573];
    CtData dyn_dtree[61];
    CtData bl_tree[39];
    TreeDesc l_desc;
    TreeDesc d_desc;
    TreeDesc bl_desc;
    uint16_t bl_count[16];
    int32_t heap[573];
    int32_t heap_len;
    int32_t heap_max;
    uint8_t depth[573];
    uint8_t pad_168d[3];
    uint8_t* l_buf;
    uint32_t lit_bufsize;
    uint32_t last_lit;
    uint16_t* d_buf;
    uint32_t opt_len;
    uint32_t static_len;
    uint32_t matches;
    int32_t last_eob_len;
    uint16_t bi_buf;
    uint16_t pad_16b2;
    int32_t bi_valid;
};

#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(offsetof(DeflateState, dyn_ltree) == 0x8c);
static_assert(offsetof(DeflateState, dyn_dtree) == 0x980);
static_assert(offsetof(DeflateState, bl_tree) == 0xa74);
static_assert(offsetof(DeflateState, l_desc) == 0xb10);
static_assert(offsetof(DeflateState, bl_count) == 0xb34);
static_assert(offsetof(DeflateState, heap) == 0xb54);
static_assert(offsetof(DeflateState, heap_len) == 0x1448);
static_assert(offsetof(DeflateState, depth) == 0x1450);
static_assert(offsetof(DeflateState, l_buf) == 0x1690);
static_assert(offsetof(DeflateState, bi_valid) == 0x16b4);
static_assert(sizeof(DeflateState) == 0x16b8);
#endif

// 0x00057d90
int32_t XB_CDECL compress2(uint8_t* dest, uint32_t* dest_len, const uint8_t* source, uint32_t source_len,
                           int32_t level);
// 0x000587e0 (s in EAX, b in ECX; EAX kept)
DeflateState* XB_REGS3 XB_CDECL putShortMSB(DeflateState* s, uint32_t edx_unused, uint32_t b);
// 0x00058810 (strm in EAX)
void XB_REGS1 XB_CDECL flush_pending(ZStream* strm);
// 0x00058880
int32_t XB_CDECL deflate(ZStream* strm, int32_t flush);
// 0x00058b10
int32_t XB_CDECL deflateEnd(ZStream* strm);
// 0x00058df0 (s in EDX)
void XB_REGS2 XB_CDECL lm_init(uint32_t eax_unused, DeflateState* s);
// 0x00058e80 (s in ESI, cur_match in EAX)
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL longest_match(DeflateState* s, uint32_t cur_match);
extern "C" void longest_match_entry(void);
// 0x00058ff0 (s in EBX)
extern "C" PLATFORM_HIDDEN void XB_CDECL fill_window(DeflateState* s);
extern "C" void fill_window_entry(void);
// 0x00059180, 0x00059390, 0x00059700: configuration_table's compressors (need_more 0,
// block_done 1, finish_started 2, finish_done 3).
int32_t XB_CDECL deflate_stored(DeflateState* s, int32_t flush);
int32_t XB_CDECL deflate_fast(DeflateState* s, int32_t flush);
int32_t XB_CDECL deflate_slow(DeflateState* s, int32_t flush);
// 0x00059af0
int32_t XB_CDECL deflateReset(ZStream* strm);
// 0x00059df0
int32_t XB_CDECL deflateInit2_(ZStream* strm, int32_t level, int32_t method, int32_t window_bits,
                               int32_t mem_level, int32_t strategy, const char* version, int32_t stream_size);
// 0x00059fe0
int32_t XB_CDECL deflateInit_(ZStream* strm, int32_t level, const char* version, int32_t stream_size);
// 0x0005af40 (s in EDX)
void XB_REGS2 XB_CDECL init_block(uint32_t eax_unused, DeflateState* s);
// 0x0005afb0 (s in EAX, tree in EDI; EAX kept)
extern "C" PLATFORM_HIDDEN DeflateState* XB_CDECL pqdownheap(DeflateState* s, CtData* tree, int32_t k);
extern "C" void pqdownheap_entry(void);
// 0x0005b080 (s in EAX, desc in ECX; EAX kept)
DeflateState* XB_REGS3 XB_CDECL gen_bitlen(DeflateState* s, uint32_t edx_unused, TreeDesc* desc);
// 0x0005b2c0 (tree in EAX, max_code in ECX)
void XB_REGS3 XB_CDECL scan_tree(CtData* tree, uint32_t edx_unused, int32_t max_code, DeflateState* s);
// 0x0005b3a0 (s in EAX, tree in ECX; EAX kept)
DeflateState* XB_REGS3 XB_CDECL send_tree(DeflateState* s, uint32_t edx_unused, CtData* tree, int32_t max_code);
// 0x0005b8b0 (s in EAX; EAX kept)
DeflateState* XB_REGS1 XB_CDECL send_all_trees(DeflateState* s, int32_t lcodes, int32_t dcodes, int32_t blcodes);
// 0x0005bbd0 (s in EAX; EAX kept)
DeflateState* XB_REGS1 XB_CDECL compress_block(DeflateState* s, const CtData* ltree, const CtData* dtree);
// 0x0005bff0 (s in ECX)
void XB_FASTCALL set_data_type(DeflateState* s);
// 0x0005c0a0 (s in EAX; EAX kept)
DeflateState* XB_REGS1 XB_CDECL bi_flush(DeflateState* s);
// 0x0005c120 (s in EAX; EAX kept)
DeflateState* XB_REGS1 XB_CDECL bi_windup(DeflateState* s);
// 0x0005c190 (s in EAX, buf in EDX, len in ECX; EAX kept)
DeflateState* XB_REGS3 XB_CDECL copy_block(DeflateState* s, const uint8_t* buf, uint32_t len, int32_t header);
// 0x0005c220
void XB_CDECL _tr_init(DeflateState* s);
// 0x0005c290 (tree in EDI, max_code in EBX, bl_count in EDX)
extern "C" PLATFORM_HIDDEN void XB_CDECL gen_codes(CtData* tree, int32_t max_code, const uint16_t* bl_count);
extern "C" void gen_codes_entry(void);
// 0x0005c310 (s in ESI)
extern "C" PLATFORM_HIDDEN void XB_CDECL build_tree(DeflateState* s, TreeDesc* desc);
extern "C" void build_tree_entry(void);
// 0x0005c510 (s in EAX): the index of the last bit length code sent.
int32_t XB_REGS1 XB_CDECL build_bl_tree(DeflateState* s);
// 0x0005c580
DeflateState* XB_CDECL _tr_stored_block(DeflateState* s, const uint8_t* buf, uint32_t stored_len, int32_t eof);
// 0x0005c620
void XB_CDECL _tr_align(DeflateState* s);
// 0x0005c820
void XB_CDECL _tr_flush_block(DeflateState* s, const uint8_t* buf, uint32_t stored_len, int32_t eof);

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
