#pragma once

// The movie player (.xbv files under movies\): one object at 0x0095f454 (Movie_Open), drawn as a
// full-screen quad (0x0095f458). The file streams through four read buffers filled by queued file
// reads (File_QueueRead, completion callback 0x000cf820); the video decoder (movie_vid.cpp) reads
// its bitstream from them through Movie_FetchBytes. The decoder state is the same object: its
// fields above +0x140 are named in movie_vid.h.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct NglTexture;

// 0x2248 bytes (Z_Malloc'd by Movie_Open). Vtables: MovieBase 0x007cf600 {DeletingDtor,
// MovieBase_Start, MovieBase_DecodeFrame (returns -1), Reset}; Movie 0x007cf390 {DeletingDtor,
// Movie_Start, Movie_DecodeFrame, Reset}.
struct Movie {
    void** vtable;
    int32_t width;          // +0x04, header +0x08 (u16)
    int32_t height;         // +0x08, header +0x0a (u16)
    int32_t frame_bytes;    // +0x0c, per format (Movie_ReadHeader)
    int32_t extra_bytes;    // +0x10, between the frame buffer and the read buffers
    int32_t chunk_size;     // +0x14, read buffer size (0x40000 halved while > a quarter of the file)
    uint8_t* memory;        // +0x18, the stream-memory block (Movie_Load)
    uint8_t* frame;         // +0x1c, memory + frame_bytes
    uint8_t* buffers[4];    // +0x20, the read buffers, chunk_size apart
    NglTexture* texture;    // +0x30, the drawn frame (Movie_GetFrameTexture)
    NglTexture* texture2;   // +0x34
    int32_t field_38;       // +0x38
    int32_t frame_count;    // +0x3c, header +0x0c (u16)
    int32_t frame_index;    // +0x40
    int32_t state;          // +0x44, -1 = stopped/failed (Movie_IsReady is true then); Movie_Open sets 3
    int32_t audio_track;    // +0x48, by language (Movie_Open)
    float time;             // +0x4c
    uint32_t data_offset;   // +0x50, file offset of the stream
    int32_t file;           // +0x54, file handle (Movie_FreeResources returns it)
    int32_t requests[4];    // +0x58, queued read per buffer, -1 = none
    uint8_t ready[4];       // +0x68, buffer filled
    int32_t owned;          // +0x6c, bits: texture2, texture, frame, buffers, memory (Movie_FreeResources)
    int32_t format;         // +0x70, header +0x0e: 1, 3 or 7
    uint8_t flag_74;        // +0x74, Movie_Load passes 1: frame_bytes += width * 64
    uint8_t pad_75[3];
    uint32_t file_size;     // +0x78, header +0x04
    uint32_t read_pos;      // +0x7c, next read's offset in the stream (0 after the last one)
    uint32_t queue_slot;    // +0x80, next buffer to queue a read into
    uint32_t read_slot;     // +0x84, buffer the decoder reads
    uint32_t next_read_slot;  // +0x88
    uint32_t first_slot;    // +0x8c, buffer holding the stream start (0xf = none)
    uint32_t last_slot;     // +0x90, buffer holding the stream end (0xf = none)
    uint32_t last_bytes;    // +0x94, bytes of the stream in last_slot
    uint8_t quant[2][64];   // +0x98, header +0x10 / +0x50 (MovieVid_BuildQuantTables)
    uint8_t* planes[3][3];  // +0x118, Y/U/V: two reference pictures (full frames) and one macroblock row (B pictures)
    uint8_t* planes_end;    // +0x13c, past the planes in memory
    uint8_t* bit_ptr;       // +0x140, the decoder's read position (Movie_FetchBytes)
    uint8_t* read_ptr;      // +0x144, consumed up to here
    uint8_t has_audio;      // +0x148
    uint8_t pad_149[0xb];
    int16_t* block;         // +0x154, 0x80-byte coefficient block
};
#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(offsetof(Movie, texture) == 0x30);
static_assert(offsetof(Movie, requests) == 0x58);
static_assert(offsetof(Movie, file_size) == 0x78);
static_assert(offsetof(Movie, quant) == 0x98);
static_assert(offsetof(Movie, planes_end) == 0x13c);
static_assert(offsetof(Movie, bit_ptr) == 0x140);
static_assert(offsetof(Movie, block) == 0x154);
#endif

// 0x000cbc10: MovieBase_Construct, the Movie vtable, the coefficient block (zeroed); clears the
// audio globals 0x0092084c/0x00920854 and the decoder bytes +0x2244/+0x2245.
Movie* XB_THISCALL Movie_Construct(Movie* movie);
// 0x000cbce0: reads the 0x400-byte header at offset; sizes, quant matrices, chunk size; then
// vtable[3] (Reset). 0, or -1 (short read, unknown format). The last argument is unused.
int32_t XB_THISCALL Movie_ReadHeader(Movie* movie, int32_t file, uint8_t flag_74, uint32_t offset, uint32_t unused);
// 0x000cbe80: with audio, sets movie_audioBuffer's volume from the master group volume
// (2000 * log10, clamped to 0.0001..1) and starts it looping when it is not playing.
void XB_THISCALL Movie_StartAudio(Movie* movie);
// 0x000ce1c0: frees the block, stops the audio buffer if it plays, MovieBase_Dtor. Returns the
// file handle (Movie_FreeResources).
int32_t XB_THISCALL Movie_Dtor(Movie* movie);
// 0x000cefd0 / 0x000cf840: vtable slot 0; frees with 0x00011050 when flags & 1.
Movie* XB_THISCALL Movie_DeletingDtor(Movie* movie, uint32_t flags);
Movie* XB_THISCALL MovieBase_DeletingDtor(Movie* movie, uint32_t flags);
// 0x000cf010: clears the end-frame callback, frees what `owned` marks; returns the file handle.
int32_t XB_THISCALL Movie_FreeResources(Movie* movie);
// 0x000cf170: vtable slot 3, back to the stream start. ECX is kept (MovieBase_Construct returns
// it): the C version is registered through MovieBase_Reset_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL MovieBase_Reset(Movie* movie);
extern "C" void MovieBase_Reset_entry(void);
// 0x000cf1e0: queues reads into the free buffers from queue_slot on (until read_slot).
void XB_THISCALL Movie_QueueReads(Movie* movie);
// 0x000cf310
Movie* XB_THISCALL MovieBase_Construct(Movie* movie);
// 0x000cf360: the MovieBase vtable, then Movie_FreeResources (tail jump).
int32_t XB_THISCALL MovieBase_Dtor(Movie* movie);
// 0x000cf550: makes count bytes readable at bit_ptr (stitching the next buffer on when they
// cross its end); consume moves read_ptr past them. 0 while the buffers are not ready.
uint8_t XB_THISCALL Movie_FetchBytes(Movie* movie, int32_t count, uint8_t consume);
// 0x000cf750: true when every buffer but the read one is filled (or the player stopped);
// otherwise queues reads.
uint8_t XB_THISCALL Movie_IsReady(Movie* movie);
// 0x000cf7c0: a queued read finished (status 0 = ok): marks its buffer ready and queues more;
// an error stops the player (state -1).
void XB_THISCALL Movie_OnReadComplete(Movie* movie, int32_t status, int32_t request);

// 0x000cbc70: vtable slot 1: the decoder's strides and limits from the size (+0x21f8..+0x2214), the
// reference picture index +0x222c = 0, then MovieBase_Start.
int32_t XB_THISCALL Movie_Start(Movie* movie);
// 0x000ce210: vtable slot 2: the next video packet (audio packets of audio_track on the way are copied
// into the audio ring 0x00920848 + 0x00920854, 0x12000 bytes; late B pictures dropped while
// 0x00920859), its picture header, the slices into the planes, converted into the locked texture2
// (flag_74: the back buffer), then texture/texture2 swapped. 0 = a frame, 3 = the last frame,
// 1 = data not ready, -1 = a broken slice chain (state -1).
int32_t XB_THISCALL Movie_DecodeFrame(Movie* movie);
// 0x000cf0c0 .. 0x000cf140: accessors (0x000cf0d0/0x000cf0e0/0x000cf0f0/0x000cf110 are shorter than
// the harness hook: unhookable).
void XB_THISCALL Movie_SetState(Movie* movie, int32_t state);
int32_t XB_THISCALL Movie_GetState(Movie* movie);
int32_t XB_THISCALL Movie_GetFrameCount(Movie* movie);
int32_t XB_THISCALL Movie_GetFrameIndex(Movie* movie);
int32_t XB_THISCALL MovieBase_DecodeFrame(Movie* movie);
// 0x000cf120: 0x00920859, lets Movie_DecodeFrame drop B pictures while over a frame behind
// (SndSys_InitDevice).
void XB_CDECL Movie_SetDropLateFrames(uint8_t drop);
int32_t XB_THISCALL Movie_SetAudioTrack(Movie* movie, int32_t track);
// 0x000cf140: any read request queued.
uint8_t XB_THISCALL Movie_HasPendingReads(Movie* movie);
// 0x000cf370: vtable slot 1: allocates what Movie_Load did not provide (memory, the read buffers,
// frame, both textures unless flag_74; `owned` records which), Reset, state 1, queues the reads, and
// lays out the planes in memory by format. Returns 0.
int32_t XB_THISCALL MovieBase_Start(Movie* movie);
// 0x000cf820: the read-completion callback File_QueueRead calls (status, request, movie): status 1
// (still busy) is ignored, else Movie_OnReadComplete.
void XB_CDECL Movie_ReadCallback(int32_t status, int32_t request, Movie* movie);

// 0x0030fe00: decodes a frame (vtable[2]), draws subtitles and the frame quad; returns the
// decoder's result, 4 while 0x0095f916 is set, -1 without a movie.
int32_t XB_CDECL Movie_DrawFrame();
// 0x00315f00 (name in ECX): opens the file, reads the header, allocates the buffers from stream
// memory (0x0095f62c; block kept at 0x0095fd18), vtable[1].
uint8_t XB_FASTCALL Movie_Load(const char* name);
// 0x00316010: once the movie time 0x0095f45c reaches 0x0095f460, a press of buttons 4-15 on the
// active pad (0x008d50d8; each of the four when -1) skips (0x0095f915 = 1, the pad's pressed edges
// taken); button 14 also sets 0x0095f91c.
uint8_t XB_CDECL Movie_CheckSkipInput();
// 0x00316320: waits for the queued reads, returns the stream memory, closes the file, frees the
// quad and the player; clear: two cleared black frames.
void XB_CDECL Movie_Stop(uint8_t clear);
// 0x0032f650 (name in EAX, may be null): the subtitle "<category>_<name>_<n>" (category: the
// argument, the upper-cased 0x00988c58 or "MOVIE") from the localized strings, else (only for
// n == 1) "<category>_<name>"; parses time/duration/lines into the subtitle globals and lays out
// both lines (centered, 0x140 - half width). English only (g_languageIndex 0).
uint8_t XB_REGS1 XB_CDECL Subtitle_Start(const char* name, const char* category);
// 0x00332030: "movies\<name>.xbv": allocates the player and its quad, loads, picks the audio
// track for the language, waits until the buffers are filled (SCR_LoadingPump).
uint8_t XB_CDECL Movie_Open(const char* name);

}  // namespace game
