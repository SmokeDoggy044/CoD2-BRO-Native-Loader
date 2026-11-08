// The title's zip reader: unzip 0.15 as Quake 3 ships it (stdio files, getShort/getLong
// that ignore read errors), with the upstream crc check kept and a 16 KB read buffer.
// Written from the title's code with the upstream source as the guide:
//
//   unzip.c -- IO on .zip files using zlib
//   Version 0.15 beta, Mar 19th, 1998,
//   Copyright (C) 1998 Gilles Vollant
//
// Under the zlib licence (as minizip 0.15 states); see zlib.cpp for its text. Altered:
// rewritten in C++ for this port from the compiled title code.

#include "unzip.h"

#include "title_data.h"

namespace game {

namespace {

constexpr int32_t UNZ_OK = 0;
constexpr int32_t UNZ_EOF = 0;
constexpr int32_t UNZ_END_OF_LIST_OF_FILE = -100;
constexpr int32_t UNZ_ERRNO = -1;
constexpr int32_t UNZ_PARAMERROR = -102;
constexpr int32_t UNZ_BADZIPFILE = -103;
constexpr int32_t UNZ_INTERNALERROR = -104;
constexpr int32_t UNZ_CRCERROR = -105;

constexpr uint32_t UNZ_BUFSIZE = 0x4000;
constexpr uint32_t BUFREADCOMMENT = 0x400;
constexpr uint32_t SIZECENTRALDIRITEM = 0x2e;
constexpr uint32_t SIZEZIPLOCALHEADER = 0x1e;

constexpr uint32_t kZlibVersion = 0x007baddc;  // "1.1.4"

// The title CRT's stdio and heap.
int32_t title_fseek(void* f, int32_t offset, int32_t origin) {
    return title_function<int32_t XB_CDECL(void*, int32_t, int32_t)>(0x001f06fb)(f, offset, origin);
}
uint32_t title_ftell(void* f) { return title_function<uint32_t XB_CDECL(void*)>(0x001f062b)(f); }
uint32_t title_fread(void* buf, uint32_t size, uint32_t count, void* f) {
    return title_function<uint32_t XB_CDECL(void*, uint32_t, uint32_t, void*)>(0x001f019a)(buf, size, count, f);
}
int32_t title_fclose(void* f) { return title_function<int32_t XB_CDECL(void*)>(0x001f0790)(f); }
void* title_malloc(uint32_t size) { return title_function<void* XB_CDECL(uint32_t)>(0x001f0d14)(size); }
void title_free(void* p) { title_function<void XB_CDECL(void*)>(0x001f0c69)(p); }

// Quake 3's unzlocal_getShort/getLong: fread into one stack word whatever the result, so a
// failed read keeps the word's last value (a short read only its low half); shorts are
// sign-extended.
struct WordReader {
    void* file;
    uint32_t word;
    uint32_t get_long() {
        title_fread(&word, 4, 1, file);
        return word;
    }
    uint32_t get_short() {
        title_fread(&word, 2, 1, file);
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(word & 0xffff)));
    }
};

}  // namespace

int32_t XB_REGS2 XB_CDECL strcmpcasenosensitive_internal(const char* name1, const char* name2) {
    // minizip's, with the characters compared as signed and the result from name2's
    // side: -1 when name2 sorts first.
    for (;;) {
        int8_t c2 = static_cast<int8_t>(*name2++);
        int8_t c1 = static_cast<int8_t>(*name1++);
        if (c2 >= 'a' && c2 <= 'z') c2 = static_cast<int8_t>(c2 - 0x20);
        if (c1 >= 'a' && c1 <= 'z') c1 = static_cast<int8_t>(c1 - 0x20);
        if (c2 == 0) return c1 != 0 ? -1 : 0;
        if (c1 == 0) return 1;
        if (c2 < c1) return -1;
        if (c2 > c1) return 1;
    }
}

uint32_t XB_CDECL unzlocal_SearchCentralDir(void* fin) {
    uint32_t max_back = 0xffff;
    uint32_t pos_found = 0;
    if (title_fseek(fin, 0, 2) != 0) return 0;  // SEEK_END
    const uint32_t size_file = title_ftell(fin);
    if (max_back > size_file) max_back = size_file;

    uint8_t* buf = static_cast<uint8_t*>(title_malloc(BUFREADCOMMENT + 4));
    if (buf == nullptr) return 0;

    uint32_t back_read = 4;
    while (back_read < max_back) {
        if (back_read + BUFREADCOMMENT > max_back)
            back_read = max_back;
        else
            back_read += BUFREADCOMMENT;
        const uint32_t read_pos = size_file - back_read;
        const uint32_t read_size =
            (BUFREADCOMMENT + 4) < (size_file - read_pos) ? (BUFREADCOMMENT + 4) : (size_file - read_pos);
        if (title_fseek(fin, static_cast<int32_t>(read_pos), 0) != 0) break;
        if (title_fread(buf, read_size, 1, fin) != 1) break;

        for (int32_t i = static_cast<int32_t>(read_size) - 3; (i--) > 0;) {
            if (buf[i] == 0x50 && buf[i + 1] == 0x4b && buf[i + 2] == 0x05 && buf[i + 3] == 0x06) {
                pos_found = read_pos + static_cast<uint32_t>(i);
                break;
            }
        }
        if (pos_found != 0) break;
    }
    title_free(buf);
    return pos_found;
}

int32_t XB_REGS1 XB_CDECL unzlocal_GetCurrentFileInfoInternal(UnzState* file, UnzFileInfo* pfile_info,
                                                              uint32_t* pfile_info_internal, char* file_name,
                                                              uint32_t file_name_size, void* extra_field,
                                                              uint32_t extra_field_size, char* comment,
                                                              uint32_t comment_size) {
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzState* s = file;
    UnzFileInfo info;
    int32_t err = UNZ_OK;
    WordReader in{s->file, 0};

    if (title_fseek(s->file, static_cast<int32_t>(s->pos_in_central_dir + s->byte_before_the_zipfile), 0) != 0)
        err = UNZ_ERRNO;
    if (err == UNZ_OK && in.get_long() != 0x02014b50) err = UNZ_BADZIPFILE;
    info.version = in.get_short();
    info.version_needed = in.get_short();
    info.flag = in.get_short();
    info.compression_method = in.get_short();
    info.dos_date = in.get_long();
    // unzlocal_DosDateToTmuDate
    const uint32_t date = info.dos_date >> 16;
    info.tm_mday = date & 0x1f;
    info.tm_mon = ((date & 0x1e0) / 0x20) - 1;
    info.tm_year = ((date & 0xfe00) / 0x200) + 1980;
    info.tm_hour = (info.dos_date & 0xf800) / 0x800;
    info.tm_min = (info.dos_date & 0x7e0) / 0x20;
    info.tm_sec = 2 * (info.dos_date & 0x1f);
    info.crc = in.get_long();
    info.compressed_size = in.get_long();
    info.uncompressed_size = in.get_long();
    info.size_filename = in.get_short();
    info.size_file_extra = in.get_short();
    info.size_file_comment = in.get_short();
    info.disk_num_start = in.get_short();
    info.internal_fa = in.get_short();
    info.external_fa = in.get_long();
    const uint32_t offset_curfile = in.get_long();

    uint32_t seek = info.size_filename;
    if (err == UNZ_OK && file_name != nullptr) {
        uint32_t size_read;
        if (info.size_filename < file_name_size) {
            file_name[info.size_filename] = '\0';
            size_read = info.size_filename;
        } else {
            size_read = file_name_size;
        }
        if (info.size_filename > 0 && file_name_size > 0)
            if (title_fread(file_name, size_read, 1, s->file) != 1) err = UNZ_ERRNO;
        seek -= size_read;
    }

    if (err == UNZ_OK && extra_field != nullptr) {
        const uint32_t size_read = info.size_file_extra < extra_field_size ? info.size_file_extra : extra_field_size;
        if (seek != 0) {
            if (title_fseek(s->file, static_cast<int32_t>(seek), 1) == 0)
                seek = 0;
            else
                err = UNZ_ERRNO;
        }
        if (info.size_file_extra > 0 && extra_field_size > 0)
            if (title_fread(extra_field, size_read, 1, s->file) != 1) err = UNZ_ERRNO;
        seek += info.size_file_extra - size_read;
    } else {
        seek += info.size_file_extra;
    }

    if (err == UNZ_OK && comment != nullptr) {
        uint32_t size_read;
        if (info.size_file_comment < comment_size) {
            comment[info.size_file_comment] = '\0';
            size_read = info.size_file_comment;
        } else {
            size_read = comment_size;
        }
        if (seek != 0)
            if (title_fseek(s->file, static_cast<int32_t>(seek), 1) != 0) err = UNZ_ERRNO;
        if (info.size_file_comment > 0 && comment_size > 0)
            if (title_fread(comment, size_read, 1, s->file) != 1) err = UNZ_ERRNO;
    }

    if (err == UNZ_OK && pfile_info != nullptr) *pfile_info = info;
    if (err == UNZ_OK && pfile_info_internal != nullptr) *pfile_info_internal = offset_curfile;
    return err;
}

int32_t XB_CDECL unzGoToNextFile(UnzState* file) {
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzState* s = file;
    if (!s->current_file_ok) return UNZ_END_OF_LIST_OF_FILE;
    if (s->num_file + 1 == s->number_entry) return UNZ_END_OF_LIST_OF_FILE;
    s->pos_in_central_dir += SIZECENTRALDIRITEM + s->cur_file_info.size_filename +
                             s->cur_file_info.size_file_extra + s->cur_file_info.size_file_comment;
    s->num_file++;
    const int32_t err = unzlocal_GetCurrentFileInfoInternal(file, &s->cur_file_info, &s->offset_curfile, nullptr,
                                                            0, nullptr, 0, nullptr, 0);
    s->current_file_ok = err == UNZ_OK;
    return err;
}

int32_t XB_CDECL unzSetCurrentFileInfoPosition(UnzState* file, uint32_t pos) {
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzState* s = file;
    s->pos_in_central_dir = pos;
    const int32_t err = unzlocal_GetCurrentFileInfoInternal(file, &s->cur_file_info, &s->offset_curfile, nullptr,
                                                            0, nullptr, 0, nullptr, 0);
    s->current_file_ok = err == UNZ_OK;
    return UNZ_OK;
}

PLATFORM_ASM_ENTRY(unzlocal_CheckCurrentFileCoherencyHeader_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(unzlocal_CheckCurrentFileCoherencyHeader) "addl $16, %esp\n"
                   "ret")

int32_t XB_CDECL unzlocal_CheckCurrentFileCoherencyHeader(UnzState* s, uint32_t* size_var,
                                                          uint32_t* offset_local_extrafield,
                                                          uint32_t* size_local_extrafield) {
    int32_t err = UNZ_OK;
    *size_var = 0;
    *offset_local_extrafield = 0;
    *size_local_extrafield = 0;
    if (title_fseek(s->file, static_cast<int32_t>(s->offset_curfile + s->byte_before_the_zipfile), 0) != 0)
        return UNZ_ERRNO;

    // The original reads into size_var's own argument slot: a failed first read keeps the
    // pointer's bits.
    WordReader in{s->file, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(size_var))};
    if (in.get_long() != 0x04034b50) err = UNZ_BADZIPFILE;
    in.get_short();  // version
    const uint32_t flags = in.get_short();
    const uint32_t method = in.get_short();
    if (err == UNZ_OK && (method != s->cur_file_info.compression_method ||
                          (s->cur_file_info.compression_method != 0 && s->cur_file_info.compression_method != 8)))
        err = UNZ_BADZIPFILE;
    in.get_long();  // date/time
    const uint32_t crc = in.get_long();
    if (err == UNZ_OK && crc != s->cur_file_info.crc && (flags & 8) == 0) err = UNZ_BADZIPFILE;
    const uint32_t compressed = in.get_long();
    if (err == UNZ_OK && compressed != s->cur_file_info.compressed_size && (flags & 8) == 0) err = UNZ_BADZIPFILE;
    const uint32_t uncompressed = in.get_long();
    if (err == UNZ_OK && uncompressed != s->cur_file_info.uncompressed_size && (flags & 8) == 0)
        err = UNZ_BADZIPFILE;
    const uint32_t size_filename = in.get_short();
    if (err == UNZ_OK && size_filename != s->cur_file_info.size_filename) err = UNZ_BADZIPFILE;
    *size_var += size_filename;
    const uint32_t size_extra_field = in.get_short();
    *offset_local_extrafield = s->offset_curfile + SIZEZIPLOCALHEADER + size_filename;
    *size_local_extrafield = size_extra_field;
    *size_var += size_extra_field;
    return err;
}

int32_t XB_CDECL unzReadCurrentFile(UnzState* file, void* buf, uint32_t len) {
    int32_t err = UNZ_OK;
    uint32_t read = 0;
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzState* s = file;
    UnzReadInfo* info = s->pfile_in_zip_read;
    if (info == nullptr) return UNZ_PARAMERROR;
    if (info->read_buffer == nullptr) return UNZ_END_OF_LIST_OF_FILE;
    if (len == 0) return 0;

    info->stream.next_out = static_cast<uint8_t*>(buf);
    info->stream.avail_out = len;
    if (len > info->rest_read_uncompressed) info->stream.avail_out = info->rest_read_uncompressed;

    while (info->stream.avail_out > 0) {
        if (info->stream.avail_in == 0 && info->rest_read_compressed > 0) {
            uint32_t read_this = UNZ_BUFSIZE;
            if (info->rest_read_compressed < read_this) read_this = info->rest_read_compressed;
            if (read_this == 0) return UNZ_EOF;
            if (title_fseek(info->file, static_cast<int32_t>(info->pos_in_zipfile + info->byte_before_the_zipfile),
                            0) != 0)
                return UNZ_ERRNO;
            if (title_fread(info->read_buffer, read_this, 1, info->file) != 1) return UNZ_ERRNO;
            info->pos_in_zipfile += read_this;
            info->rest_read_compressed -= read_this;
            info->stream.next_in = info->read_buffer;
            info->stream.avail_in = read_this;
        }

        if (info->compression_method == 0) {
            const uint32_t copy =
                info->stream.avail_out < info->stream.avail_in ? info->stream.avail_out : info->stream.avail_in;
            for (uint32_t i = 0; i < copy; i++) info->stream.next_out[i] = info->stream.next_in[i];
            info->crc32 = crc32(info->crc32, info->stream.next_out, copy);
            info->rest_read_uncompressed -= copy;
            info->stream.avail_in -= copy;
            info->stream.avail_out -= copy;
            info->stream.next_out += copy;
            info->stream.next_in += copy;
            info->stream.total_out += copy;
            read += copy;
        } else {
            const uint32_t total_before = info->stream.total_out;
            const uint8_t* buf_before = info->stream.next_out;
            err = inflate(&info->stream, 2);  // Z_SYNC_FLUSH
            const uint32_t out_this = info->stream.total_out - total_before;
            info->crc32 = crc32(info->crc32, buf_before, out_this);
            info->rest_read_uncompressed -= out_this;
            read += out_this;
            if (err == 1) return static_cast<int32_t>(read);  // Z_STREAM_END
            if (err != 0) break;
        }
    }
    if (err == 0) return static_cast<int32_t>(read);
    return err;
}

int32_t XB_CDECL unztell(UnzState* file) {
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzReadInfo* info = file->pfile_in_zip_read;
    if (info == nullptr) return UNZ_PARAMERROR;
    return static_cast<int32_t>(info->stream.total_out);
}

int32_t XB_CDECL unzCloseCurrentFile(UnzState* file) {
    int32_t err = UNZ_OK;
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzState* s = file;
    UnzReadInfo* info = s->pfile_in_zip_read;
    if (info == nullptr) return UNZ_PARAMERROR;
    if (info->rest_read_uncompressed == 0 && info->crc32 != info->crc32_wait) err = UNZ_CRCERROR;
    title_free(info->read_buffer);
    info->read_buffer = nullptr;
    if (info->stream_initialised) inflateEnd(&info->stream);
    info->stream_initialised = 0;
    title_free(info);
    s->pfile_in_zip_read = nullptr;
    return err;
}

int32_t XB_CDECL unzClose(UnzState* file) {
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzState* s = file;
    if (s->pfile_in_zip_read != nullptr) unzCloseCurrentFile(file);
    title_fclose(s->file);
    title_free(s);
    return UNZ_OK;
}

int32_t XB_CDECL unzOpenCurrentFile(UnzState* file) {
    if (file == nullptr) return UNZ_PARAMERROR;
    UnzState* s = file;
    if (!s->current_file_ok) return UNZ_PARAMERROR;
    if (s->pfile_in_zip_read != nullptr) unzCloseCurrentFile(file);

    uint32_t size_var, offset_local_extrafield, size_local_extrafield;
    if (unzlocal_CheckCurrentFileCoherencyHeader(s, &size_var, &offset_local_extrafield, &size_local_extrafield) !=
        UNZ_OK)
        return UNZ_BADZIPFILE;

    UnzReadInfo* info = static_cast<UnzReadInfo*>(title_malloc(sizeof(UnzReadInfo)));
    if (info == nullptr) return UNZ_INTERNALERROR;
    info->read_buffer = static_cast<uint8_t*>(title_malloc(UNZ_BUFSIZE));
    info->offset_local_extrafield = offset_local_extrafield;
    info->size_local_extrafield = size_local_extrafield;
    info->pos_local_extrafield = 0;
    if (info->read_buffer == nullptr) {
        title_free(info);
        return UNZ_INTERNALERROR;
    }

    info->stream_initialised = 0;
    const bool store = s->cur_file_info.compression_method == 0;
    info->crc32_wait = s->cur_file_info.crc;
    info->crc32 = 0;
    info->compression_method = s->cur_file_info.compression_method;
    info->file = s->file;
    info->byte_before_the_zipfile = s->byte_before_the_zipfile;
    info->stream.total_out = 0;
    if (!store) {
        info->stream.zalloc = 0;
        info->stream.zfree = 0;
        info->stream.opaque = 0;
        if (inflateInit2_(&info->stream, -15, reinterpret_cast<const char*>(uintptr_t{kZlibVersion}),
                          sizeof(ZStream)) == 0)
            info->stream_initialised = 1;
    }
    info->rest_read_compressed = s->cur_file_info.compressed_size;
    info->rest_read_uncompressed = s->cur_file_info.uncompressed_size;
    info->pos_in_zipfile = s->offset_curfile + SIZEZIPLOCALHEADER + size_var;
    info->stream.avail_in = 0;
    s->pfile_in_zip_read = info;
    return UNZ_OK;
}

}  // namespace game
