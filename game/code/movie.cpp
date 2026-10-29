#include "movie.h"

#include "cl.h"
#include "movie_vid.h"
#include "ngl.h"
#include "qcommon.h"
#include "stream.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
// A vtable slot of the player (title code: the Movie/MovieBase vtables at 0x007cf390/0x007cf600).
template <typename F>
F* slot(const Movie* movie, int index) {
    return title_function<F>(address_of(movie->vtable[index]));
}

Movie*& the_movie() { return title_global<Movie*>(0x0095f454); }
NglQuad*& movie_quad() { return title_global<NglQuad*>(0x0095f458); }
// Set while the player is unavailable (Movie_Open/Stop/DrawFrame do nothing).
uint8_t movie_blocked() { return title_global<uint8_t>(0x0095f916); }
uint8_t* stream_memory() { return title_global<uint8_t*>(0x0095f62c); }
// The stream-memory slots the player holds (StreamMem_Alloc's result, four words).
uint32_t* movie_slots() { return &title_global<uint32_t>(0x0095fd18); }
void*& audio_buffer() { return title_global<void*>(0x00920850); }  // movie_audioBuffer

void free_aligned(void* p) { Mem_AlignedFree(p); }
void operator_delete(void* p) { title_function<void XB_CDECL(void*)>(0x00011050)(p); }  // Z_Free (0x00011050)
void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }            // Z_Free
void* z_malloc(uint32_t size) { return title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(size); }

uint32_t dsound_get_status(void* buffer) {
    uint32_t status = 0;
    title_function<int32_t XB_STDCALL(void*, uint32_t*)>(0x003c77ef)(buffer, &status);  // IDirectSoundBuffer_GetStatus
    return status;
}

// The title CRT.
char* crt_strncpy(char* dst, const char* src, uint32_t n) {
    return title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(dst, src, n);
}
char* crt_strupr(char* text) { return title_function<char* XB_CDECL(char*)>(0x001fa3d5)(text); }
char* crt_strtok(char* text, const char* delimiters) {
    return title_function<char* XB_CDECL(char*, const char*)>(0x001eeea4)(text, delimiters);
}
float crt_atof(const char* text) { return title_function<float XB_CDECL(const char*)>(0x001ef056)(text); }
template <typename... Args>
int32_t crt_sprintf(char* buffer, const char* format, Args... args) {
    return title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901)(buffer, format, args...);
}

}  // namespace

extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

Movie* XB_THISCALL Movie_Construct(Movie* movie) {
    MovieBase_Construct(movie);
    movie->vtable = pointer<void*>(0x007cf390);
    uint32_t* block = static_cast<uint32_t*>(Mem_AlignedAlloc(0x80, 0x40, 0));
    movie->block = reinterpret_cast<int16_t*>(block);
    for (int i = 0; i < 0x20; ++i) block[i] = 0;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(movie);
    bytes[0x2245] = 0;
    bytes[0x2244] = 0;
    title_global<uint32_t>(0x00920854) = 0;
    title_global<uint32_t>(0x0092084c) = 0;
    movie->has_audio = 0;
    return movie;
}

int32_t XB_THISCALL Movie_ReadHeader(Movie* movie, int32_t file, uint8_t flag_74, uint32_t offset, uint32_t) {
    movie->state = -1;
    uint8_t* header = static_cast<uint8_t*>(Mem_AlignedAlloc(0x400, 0x40, 0));
    const int32_t read =
        title_function<int32_t XB_CDECL(int32_t, uint32_t, void*, uint32_t)>(0x000caff0)(file, offset, header, 0x400);  // File_ReadAt
    if (read != 0x400) return -1;
    movie->file = file;
    movie->data_offset = offset;
    movie->flag_74 = flag_74;
    movie->file_size = *reinterpret_cast<const uint32_t*>(header + 4);
    movie->width = *reinterpret_cast<const uint16_t*>(header + 8);
    movie->height = *reinterpret_cast<const uint16_t*>(header + 0xa);
    movie->frame_count = *reinterpret_cast<const uint16_t*>(header + 0xc);
    movie->format = header[0xe];
    platform_rep_movs(movie->quant[0], header + 0x10, 0x40);
    platform_rep_movs(movie->quant[1], header + 0x50, 0x40);
    free_aligned(header);
    movie->chunk_size = 0x40000;
    if (static_cast<int32_t>(movie->file_size) < 0x100000) {
        do {
            movie->chunk_size >>= 1;
        } while (static_cast<int32_t>(movie->file_size) < movie->chunk_size * 4);
    }
    movie->frame_bytes = 0;
    switch (movie->format) {
    case 1: movie->frame_bytes = movie->width * 3 * 8; break;
    case 3: movie->frame_bytes = movie->width * movie->height * 3; break;
    case 7: movie->frame_bytes = (movie->height + 8) * movie->width * 3; break;
    default: return -1;
    }
    if (movie->flag_74) movie->frame_bytes = movie->frame_bytes + (movie->width << 6);
    slot<void XB_THISCALL(Movie*)>(movie, 3)(movie);
    return 0;
}

void XB_THISCALL Movie_StartAudio(Movie* movie) {
    if (!movie->has_audio) return;
    const long double master = title_function<long double XB_CDECL()>(0x000c64d0)();  // Snd_GetMasterGroupVolume
    float volume = static_cast<float>(master);
    const float least = title_global<float>(0x0083ed50);  // 0.0001
    if (static_cast<long double>(least) > master) {
        volume = least;
    } else {
        const float one = title_global<float>(0x0083eb20);
        if (volume > one) volume = one;
    }
    const int32_t level = platform_x87_log10_mul_ftol(volume, title_global<float>(0x0083ecc8));  // * 2000
    title_function<int32_t XB_STDCALL(void*, int32_t)>(0x003c76db)(audio_buffer(), level);  // IDirectSoundBuffer_SetVolume
    if (dsound_get_status(audio_buffer()) & 1) return;
    title_function<int32_t XB_STDCALL(void*, uint32_t)>(0x003c780b)(audio_buffer(), title_global<uint32_t>(0x0092084c));  // SetCurrentPosition
    title_function<int32_t XB_STDCALL(void*, uint32_t, uint32_t, uint32_t)>(0x003c7753)(audio_buffer(), 0, 0, 1);  // PlayEx, looping
}

int32_t XB_THISCALL Movie_Dtor(Movie* movie) {
    movie->vtable = pointer<void*>(0x007cf390);
    free_aligned(movie->block);
    if (movie->has_audio && (dsound_get_status(audio_buffer()) & 1))
        title_function<int32_t XB_STDCALL(void*)>(0x003c7777)(audio_buffer());  // IDirectSoundBuffer_Stop
    return MovieBase_Dtor(movie);
}

Movie* XB_THISCALL Movie_DeletingDtor(Movie* movie, uint32_t flags) {
    Movie_Dtor(movie);
    if (flags & 1) operator_delete(movie);
    return movie;
}

int32_t XB_THISCALL Movie_FreeResources(Movie* movie) {
    nglSetEndFrameCallback(nullptr, nullptr);
    if (movie->owned & 1) nglReleaseTexture(movie->texture2);
    movie->owned = movie->owned >> 1;
    if (movie->owned & 1) nglReleaseTexture(movie->texture);
    movie->owned = movie->owned >> 1;
    if (movie->owned & 1) free_aligned(movie->frame);
    movie->owned = movie->owned >> 1;
    if (movie->owned & 1)
        for (int i = 0; i < 4; ++i) free_aligned(movie->buffers[i]);
    movie->owned = movie->owned >> 1;
    if (movie->owned & 1) free_aligned(movie->memory);
    const int32_t file = movie->file;
    movie->owned = 0;
    return file;
}

PLATFORM_ASM_ENTRY(MovieBase_Reset_entry, "pushl %ecx\n"
                                          "pushl %ecx\n" PLATFORM_ASM_CALL(MovieBase_Reset) "addl $4, %esp\n"
                                          "popl %ecx\n"
                                          "ret")

void XB_CDECL MovieBase_Reset(Movie* movie) {
    movie->first_slot = 0xf;
    movie->last_slot = 0xf;
    movie->bit_ptr = movie->buffers[0];
    movie->read_ptr = movie->buffers[0];
    movie->frame_index = 0;
    *reinterpret_cast<uint32_t*>(&movie->time) = 0;  // +0.0f
    movie->field_38 = 0;
    movie->queue_slot = 0;
    movie->read_slot = 3;
    movie->next_read_slot = 0;
    movie->last_bytes = 0;
    movie->read_pos = 0;
    for (int i = 0; i < 4; ++i) {
        movie->requests[i] = -1;
        movie->ready[i] = 0;
    }
}

void XB_THISCALL Movie_QueueReads(Movie* movie) {
    // File_QueueRead's request: {file, 0, callback, 0, 2, offset, buffer, size, 0, context}.
    uint32_t request[10] = {0xffffffff, 0, 0, 0xffffffff, 2, 0, 0, 0, 0, 0};
    if (movie->queue_slot == movie->read_slot) return;
    do {
        const uint32_t slot_index = movie->queue_slot;
        if (movie->requests[slot_index] != -1) return;
        request[0] = static_cast<uint32_t>(movie->file);
        const uint32_t position = movie->read_pos;
        request[1] = 0;
        request[2] = 0x000cf820;  // completion: Movie_OnReadComplete
        request[3] = 0;
        request[4] = 2;
        request[5] = movie->data_offset + position;
        request[6] = address_of(movie->buffers[slot_index]);
        request[8] = 0;
        request[9] = address_of(movie);
        if (position == 0) movie->first_slot = slot_index;
        const uint32_t left = movie->file_size - position;
        const uint32_t chunk = static_cast<uint32_t>(movie->chunk_size);
        if (left > chunk) {
            request[7] = chunk;
            movie->read_pos = position + chunk;
        } else {
            movie->last_bytes = left;
            movie->last_slot = slot_index;
            movie->read_pos = 0;
            request[7] = (left + 0x7ff) & 0xfffff800;
        }
        const int32_t id = title_function<int32_t XB_CDECL(uint32_t*)>(0x000c9f70)(request);  // File_QueueRead
        movie->requests[movie->queue_slot] = id;
        movie->queue_slot = (movie->queue_slot + 1) & 3;
    } while (movie->queue_slot != movie->read_slot);
}

Movie* XB_THISCALL MovieBase_Construct(Movie* movie) {
    movie->vtable = pointer<void*>(0x007cf600);
    movie->memory = nullptr;
    for (int i = 0; i < 4; ++i) movie->buffers[i] = nullptr;
    movie->texture2 = nullptr;
    movie->texture = nullptr;
    movie->flag_74 = 0;
    movie->owned = 0;
    movie->height = 0;
    movie->width = 0;
    movie->extra_bytes = 0;
    movie->chunk_size = 0;
    movie->frame_bytes = 0;
    movie->audio_track = 0;
    MovieBase_Reset(movie);
    return movie;
}

int32_t XB_THISCALL MovieBase_Dtor(Movie* movie) {
    movie->vtable = pointer<void*>(0x007cf600);
    return Movie_FreeResources(movie);
}

uint8_t XB_THISCALL Movie_FetchBytes(Movie* movie, int32_t count, uint8_t consume) {
    if (movie->read_slot != movie->next_read_slot) {
        movie->ready[movie->read_slot] = 0;
        movie->read_slot = movie->next_read_slot;
        Movie_QueueReads(movie);
    }
    const uint32_t current = movie->read_slot;
    if (!movie->ready[current]) return 0;
    const uint32_t last = movie->last_slot;
    uint8_t* const position = movie->read_ptr;
    uint8_t* const buffer = movie->buffers[current];
    const uint32_t end = current == last ? movie->last_bytes : static_cast<uint32_t>(movie->chunk_size);
    const uint32_t left = end - address_of(position) + address_of(buffer);
    if (count <= static_cast<int32_t>(left)) {
        movie->bit_ptr = position;
        if (consume) movie->read_ptr = position + count;
        return 1;
    }
    const uint32_t next = (current + 1) & 3;
    if (!movie->ready[next]) return 0;
    if (current == last) {
        // The stream's end: restart in the next buffer.
        movie->ready[current] = 0;
        const uint32_t restart = (movie->read_slot + 1) & 3;
        movie->read_slot = restart;
        movie->next_read_slot = restart;
        movie->bit_ptr = movie->buffers[restart];
        movie->read_ptr = movie->buffers[restart];
        movie->last_slot = 0xf;
        Movie_QueueReads(movie);
        if (consume) movie->read_ptr = movie->read_ptr + count;
        return 1;
    }
    if (!consume) {
        // Peek: the rest of this buffer moved to its start, the next buffer's first bytes after it.
        platform_rep_movs(buffer, position, left);
        uint8_t* const to = movie->buffers[movie->read_slot] + left;
        platform_rep_movs(to, movie->buffers[(movie->read_slot + 1) & 3], count - left);
        movie->bit_ptr = movie->buffers[movie->read_slot];
        return 1;
    }
    // Consume: the rest of this buffer (32-byte aligned) moved down by the aligned shortfall, the
    // next buffer's start copied after it; reading continues in the next buffer.
    movie->next_read_slot = next;
    const uint32_t shortfall = count - left;
    const uint32_t tail = (left + 0x1f) & 0xffffffe0;
    uint8_t* const from = pointer<uint8_t>(address_of(position) & 0xffffffe0);
    const uint32_t head = (shortfall + 0x1f) & 0xffffffe0;
    uint8_t* const to = from - head;
    platform_rep_movs(to, from, tail);
    platform_rep_movs(to + tail, movie->buffers[movie->next_read_slot], head);
    uint8_t* const read = movie->read_ptr;
    movie->bit_ptr = read - head;
    movie->read_ptr = pointer<uint8_t>(address_of(movie->buffers[movie->next_read_slot]) -
                                       address_of(movie->buffers[movie->read_slot]) -
                                       static_cast<uint32_t>(movie->chunk_size) + address_of(read) +
                                       static_cast<uint32_t>(count));
    return 1;
}

uint8_t XB_THISCALL Movie_IsReady(Movie* movie) {
    if (movie->state == -1) return 1;
    uint8_t ready = 1;
    if (!movie->ready[0] && movie->read_slot != 0) ready = 0;
    if (!movie->ready[1] && movie->read_slot != 1) ready = 0;
    if (!movie->ready[2] && movie->read_slot != 2) ready = 0;
    if (!movie->ready[3] && movie->read_slot != 3) ready = 0;
    if (!ready) Movie_QueueReads(movie);
    return ready;
}

void XB_THISCALL Movie_OnReadComplete(Movie* movie, int32_t status, int32_t request) {
    int32_t index = 0;
    while (index < 4 && movie->requests[index] != request) ++index;
    if (index == 4) return;
    if (status) {
        movie->state = -1;
        return;
    }
    movie->requests[index] = -1;
    movie->ready[index] = 1;
    Movie_QueueReads(movie);
}

Movie* XB_THISCALL MovieBase_DeletingDtor(Movie* movie, uint32_t flags) {
    movie->vtable = pointer<void*>(0x007cf600);
    Movie_FreeResources(movie);
    if (flags & 1) operator_delete(movie);
    return movie;
}

int32_t XB_CDECL Movie_DrawFrame() {
    if (movie_blocked()) return 4;
    Movie* movie = the_movie();
    if (!movie) return -1;
    const int32_t result = slot<int32_t XB_THISCALL(Movie*)>(movie, 2)(movie);  // decode a frame
    Subtitle_Draw();
    NglTexture* texture = the_movie()->texture;  // Movie_GetFrameTexture
    NglQuad* quad = movie_quad();
    quad->texture = texture;
    nglListAddQuad(quad);
    return result;
}

uint8_t XB_FASTCALL Movie_Load(const char* name) {
    uint32_t size = 0;
    const int32_t file = title_function<int32_t XB_CDECL(uint32_t, const char*, uint32_t*)>(0x000c9d60)(
        title_global<uint32_t>(0x0095f6f4), name, &size);  // File_OpenReadEx
    if (file == -1) return 0;
    if (Movie_ReadHeader(the_movie(), file, 1, 0, size)) return 0;
    const float eight = title_global<float>(0x0083ed78);
    uint32_t allocated[4] = {};
    const uint32_t* slots = title_function<uint32_t* XB_REGS1 XB_STDCALL(uint8_t*, uint32_t*, float, float, float, float)>(
        0x0028d5e0)(stream_memory(), allocated, eight, eight, eight, 0.0f);  // StreamMem_Alloc
    uint32_t* held = movie_slots();
    held[0] = slots[0];
    held[1] = slots[1];
    held[2] = slots[2];
    held[3] = slots[3];
    uint32_t block[2] = {};
    StreamMem_GetBlock(held, stream_memory(), block, 0);
    Movie* movie = the_movie();
    uint8_t* memory = pointer<uint8_t>(block[0]);
    uint8_t* frame = memory + movie->frame_bytes;
    movie->frame = frame;
    movie->memory = memory;
    movie->buffers[0] = frame + movie->extra_bytes;
    movie->buffers[1] = movie->buffers[0] + movie->chunk_size;
    movie->buffers[2] = movie->buffers[1] + movie->chunk_size;
    movie->buffers[3] = movie->buffers[2] + movie->chunk_size;
    slot<void XB_THISCALL(Movie*)>(movie, 1)(movie);
    return 1;
}

uint8_t XB_CDECL Movie_CheckSkipInput() {
    using PadCreate = uint8_t* XB_CDECL();
    title_function<void XB_THISCALL(uint8_t*)>(0x00055e70)(title_function<PadCreate>(0x00055e40)());  // IN_PadUpdate
    if (title_global<float>(0x0095f460) > title_global<float>(0x0095f45c)) return 0;
    static const uint32_t kButtons[12] = {5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 15, 14};
    int32_t& active = title_global<int32_t>(0x008d50d8);
    int32_t pad = 0;
    bool skip = false;
    if (active == -1) {
        for (pad = 0; pad < 4 && !skip; ++pad) {
            title_function<PadCreate>(0x00055e40)();
            if (IN_PadButtonPressed(pad, 0xe)) title_global<uint8_t>(0x0095f91c) = 1;
            for (uint32_t button : kButtons) {
                title_function<PadCreate>(0x00055e40)();
                if (IN_PadButtonPressed(pad, button)) {
                    skip = true;
                    break;
                }
            }
            if (skip) break;
        }
    } else {
        title_function<PadCreate>(0x00055e40)();
        if (IN_PadButtonPressed(active, 0xe)) title_global<uint8_t>(0x0095f91c) = 1;
        for (uint32_t button : kButtons) {
            title_function<PadCreate>(0x00055e40)();
            if (IN_PadButtonPressed(active, button)) {
                skip = true;
                pad = active;
                break;
            }
        }
    }
    if (!skip) return 0;
    title_function<PadCreate>(0x00055e40)();
    for (uint32_t button = 0; button <= 0xf; ++button) IN_PadConsumePressed(pad, button);
    title_global<uint8_t>(0x0095f915) = 1;
    return 1;
}

void XB_CDECL Movie_Stop(uint8_t clear) {
    if (movie_blocked()) return;
    if (!the_movie()) return;
    using HasPending = uint8_t XB_THISCALL(Movie*);
    while (title_function<HasPending>(0x000cf140)(the_movie()))  // any request queued
        title_function<void XB_CDECL()>(0x000ca690)();         // File_PumpRequests
    title_function<void XB_REGS2 XB_STDCALL(uint8_t*, uint32_t*, uint8_t*, int32_t)>(0x0028d4c0)(
        stream_memory(), movie_slots(), stream_memory(), -1);  // StreamMem_MoveSlots
    const int32_t file = Movie_FreeResources(the_movie());
    title_function<void XB_CDECL(int32_t)>(0x000cac90)(file);  // File_Close
    NglQuad* quad = movie_quad();
    title_global<uint32_t>(0x0095f470) = 0;
    title_global<uint32_t>(0x0095f474) = 0;
    z_free(quad);
    if (Movie* movie = the_movie()) slot<Movie* XB_THISCALL(Movie*, uint32_t)>(movie, 0)(movie, 1);
    the_movie() = nullptr;
    if (!clear) return;
    for (int i = 0; i < 2; ++i) {
        nglBeginFrame();
        nglSetClearFlags(0xf3);
        nglSetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        nglEndFrame();
    }
}

uint8_t XB_REGS1 XB_CDECL Subtitle_Start(const char* name, const char* category) {
    char key[0x34];
    uint8_t fallback = 0;
    if (title_global<int32_t>(0x0095f5dc)) return 0;  // g_languageIndex
    char* subtitle = &title_global<char>(0x00926e44);  // 0x2d bytes
    int32_t& counter = title_global<int32_t>(0x00926e7c);
    uint32_t& line1 = title_global<uint32_t>(0x0095f470);
    uint32_t& line2 = title_global<uint32_t>(0x0095f474);
    if (name) {
        crt_strncpy(subtitle, name, 0x2d);
        subtitle[0x2c] = 0;
        crt_strncpy(subtitle, crt_strupr(subtitle), 0x2d);
        counter = 1;
        title_global<uint32_t>(0x0092708c) = 0;  // 0.0f
    }
    if (!category) {
        char* level = &title_global<char>(0x00988c58);
        category = level[0] ? crt_strupr(level) : &title_global<char>(0x0082d7f8);  // "MOVIE"
    }
    crt_strncpy(&title_global<char>(0x00926e30), category, 0x10);
    const int32_t n = counter;
    counter = n + 1;
    crt_sprintf(key, &title_global<char>(0x0082d7d8), category, subtitle, n);  // "%s_%s_%d"
    const uint32_t hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x00024440)(key);  // Loc_HashKey
    const uint8_t* entry = Zone_FindByHash(title_global<const uint8_t*>(0x0095f5d8), hash);  // rm_locStrings
    const char* text = key;
    if (entry) {
        text = *reinterpret_cast<const char* const*>(entry + 8);
        if (!text) text = &title_global<char>(0x008379b4);  // "NO STRING"
    }
    if (text == key || !text) {
        if (counter != 2) {
            line2 = 0;
            line1 = 0;
            return 0;
        }
        crt_sprintf(key, &title_global<char>(0x0082d7d0), category, subtitle);  // "%s_%s"
        text = pointer<const char>(title_call_regs(0x0029d5f0, 0, address_of(key), 0));  // Loc_GetString (ESI)
        if (text == key || !text) {
            line2 = 0;
            line1 = 0;
            return 0;
        }
        fallback = 1;
    }
    char* const lines = &title_global<char>(0x00926d80);
    crt_strncpy(lines, text, 0xa3);
    const char* delimiters = title_global<const char*>(0x008d20a4);
    float& start = title_global<float>(0x00926e74);
    uint32_t& duration = title_global<uint32_t>(0x00926e24);
    if (!fallback) {
        // "start duration line1 [line2]"
        char* token = crt_strtok(lines, delimiters);
        if (!token) return 0;
        start = crt_atof(token);
        token = crt_strtok(nullptr, delimiters);
        if (!token) return 0;
        title_global<float>(0x00926e24) = crt_atof(token);
        token = crt_strtok(nullptr, delimiters);
        line1 = address_of(token);
        if (!token) return 0;
    } else {
        // "[duration] line1 [line2]": a leading number other than 0 is the duration (start 0),
        // else the first token is text shown for 3 s.
        char* first = crt_strtok(lines, delimiters);
        if (!first) return 0;
        start = crt_atof(first);
        if (!(start == 0.0f)) {
            duration = *reinterpret_cast<const uint32_t*>(&start);
            *reinterpret_cast<uint32_t*>(&start) = 0;
            char* token = crt_strtok(nullptr, delimiters);
            line1 = address_of(token);
            if (!token) return 0;
        } else {
            duration = title_global<uint32_t>(0x0083ec9c);  // 3.0f
            line1 = address_of(first);
        }
    }
    char* token = crt_strtok(nullptr, delimiters);
    line2 = address_of(token);
    if (!token) {
        line2 = line1;
        line1 = 0;
    }
    // Line positions: x = 0x140 - half the text width; the wide flags mark lines wider than 0x268.
    uint16_t& wide2 = title_global<uint16_t>(0x00926e78);
    uint16_t& wide1 = title_global<uint16_t>(0x00926e40);
    int32_t& x2 = title_global<int32_t>(0x00926e80);
    int32_t& y2 = title_global<int32_t>(0x00926e2c);
    const uint8_t* font = title_global<const uint8_t*>(0x0095f8e0);
    wide1 = 0;
    wide2 = 0;
    nglGetTextExtentf(font, &x2, &y2, pointer<const char>(line2));
    int32_t half = static_cast<int32_t>(static_cast<uint32_t>(x2) >> 1);
    y2 = 0x1a4;
    if (half > 0x134) {
        if (half > 0x140) half = 0x140;
        wide2 = 1;
    }
    x2 = 0x140 - half;
    if (line1) {
        int32_t& x1 = title_global<int32_t>(0x00926e28);
        int32_t& y1 = title_global<int32_t>(0x00927088);
        nglGetTextExtentf(font, &x1, &y1, pointer<const char>(line1));
        half = static_cast<int32_t>(static_cast<uint32_t>(x1) >> 1);
        if (half > 0x134) {
            if (half > 0x140) half = 0x140;
            wide1 = 1;
        }
        y1 = 0x190;
        x1 = 0x140 - half;
    }
    if (!wide1 && !wide2) return 1;
    wide2 = 1;
    wide1 = 1;
    return 0;
}

uint8_t XB_CDECL Movie_Open(const char* name) {
    if (movie_blocked()) return 0;
    char path[0x200];
    const uint32_t* prefix = &title_global<uint32_t>(0x0082d7f0);  // "movies\"
    reinterpret_cast<uint32_t*>(path)[0] = prefix[0];
    title_global<uint8_t>(0x0095f46c) = 0;
    reinterpret_cast<uint32_t*>(path)[1] = prefix[1];
    uint32_t length = 0;
    while (name[length]) ++length;
    char* end = path;
    while (*end) ++end;
    platform_rep_movs(end, name, length + 1);
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001ee960)(path, &title_global<char>(0x0082d7e8), 7);  // _strncat ".xbv"
    void* memory = z_malloc(0x2248);
    if (!memory) {
        the_movie() = nullptr;
        return 0;
    }
    Movie* movie = Movie_Construct(static_cast<Movie*>(memory));
    the_movie() = movie;
    if (!movie) return 0;
    uint32_t* quad = static_cast<uint32_t*>(z_malloc(0x64));
    if (!quad) {
        movie_quad() = nullptr;
        if (Movie* m = the_movie()) slot<Movie* XB_THISCALL(Movie*, uint32_t)>(m, 0)(m, 1);
        the_movie() = nullptr;
        return 0;
    }
    for (int i = 0; i < 0x19; ++i) quad[i] = 0;
    movie_quad() = reinterpret_cast<NglQuad*>(quad);
    if (!Movie_Load(path)) {
        if (Movie* m = the_movie()) slot<Movie* XB_THISCALL(Movie*, uint32_t)>(m, 0)(m, 1);
        z_free(movie_quad());
        the_movie() = nullptr;
        return 0;
    }
    if (!title_global<uint8_t>(0x0095f46d)) {
        const int32_t language = title_global<int32_t>(0x0095f5dc);  // g_languageIndex
        if (language) {
            const int32_t track = language == 4 ? 1 : language == 3 ? 2 : 0;
            title_function<int32_t XB_THISCALL(Movie*, int32_t)>(0x000cf130)(the_movie(), track);  // audio_track = track
        }
    }
    title_global<uint8_t>(0x0095f46d) = 0;
    title_function<void XB_THISCALL(Movie*, int32_t)>(0x000cf0c0)(the_movie(), 3);  // state = 3
    nglInitQuad(movie_quad());
    nglSetQuadRect(movie_quad(), 0.0f, 0.0f, 640.0f, 480.0f);
    nglSetQuadZ(movie_quad(), 500.0f);
    nglSetQuadTexture(movie_quad(), the_movie()->texture);
    title_global<uint32_t>(0x0095f45c) = 0;  // movie time 0.0f
    while (!Movie_IsReady(the_movie())) title_function<void XB_CDECL()>(0x00331fb0)();  // SCR_LoadingPump
    title_global<uint8_t>(0x0095f46c) = 1;
    return 1;
}


// ---- Vtable slots and accessors (no Ghidra functions: decompiled by address) ----

namespace {

// Decoder fields past the Movie struct (movie_vid.cpp names them where it uses them).
template <typename T>
T& dec(Movie* movie, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(movie) + offset);
}

// The audio ring the packets are copied into (movie_audioBuffer's data), its write offset and the
// offset where the stream started.
uint8_t* audio_ring() { return title_global<uint8_t*>(0x00920848); }
int32_t& audio_write() { return title_global<int32_t>(0x00920854); }

// Destination planes +0x221c..+0x2224 = a plane set.
void set_dest_planes(Movie* movie, uint8_t* const* set) {
    dec<uint8_t*>(movie, 0x221c) = set[0];
    dec<uint8_t*>(movie, 0x2220) = set[1];
    dec<uint8_t*>(movie, 0x2224) = set[2];
}

}  // namespace

int32_t XB_THISCALL Movie_Start(Movie* movie) {
    const int32_t width = movie->width;
    dec<int32_t>(movie, 0x21fc) = width >> 1;                       // chroma stride
    dec<int32_t>(movie, 0x2200) = width << 4;                       // a luma macroblock row
    dec<int32_t>(movie, 0x21f8) = width;                            // luma stride
    dec<int32_t>(movie, 0x2204) = (width << 4) >> 2;                // a chroma macroblock row
    const int32_t height = movie->height;
    dec<int32_t>(movie, 0x2208) = width * 2 - 0x20;                 // x limit (half pixels)
    dec<int32_t>(movie, 0x2214) = (height - 0x10) * 2;              // frame y limit
    dec<int32_t>(movie, 0x222c) = 0;                                // reference picture index
    dec<int32_t>(movie, 0x2210) = height * 2 - 0x10;
    dec<int32_t>(movie, 0x220c) = height - 0x10;                    // field y limit
    return MovieBase_Start(movie);
}

int32_t XB_THISCALL Movie_DecodeFrame(Movie* movie) {
    if (!movie->field_38) {
        movie->field_38 = 1;
        if (movie->format == 7) {
            --movie->frame_index;
            slot<int32_t XB_THISCALL(Movie*)>(movie, 2)(movie);
        }
    }
    // The next video packet: {size, flags, track / f_codes, f_codes, DC precision} and size bytes.
    uint8_t* packet;
    for (;;) {
        if (!Movie_FetchBytes(movie, 8, 0)) return 1;
        if (movie->read_slot == movie->first_slot) {  // skip the stream's 0x90-byte start
            movie->read_ptr += 0x90;
            movie->bit_ptr = movie->read_ptr;
            movie->first_slot = 0xf;
            title_global<int32_t>(0x0092084c) = audio_write();
        }
        if (!Movie_FetchBytes(movie, *reinterpret_cast<int32_t*>(movie->bit_ptr) + 8, 1)) return 1;
        packet = movie->bit_ptr;
        const uint8_t flags = packet[4];
        if (flags & 0x80) {  // audio
            if (static_cast<int32_t>(packet[5]) != movie->audio_track) continue;
            const uint32_t size = *reinterpret_cast<uint32_t*>(packet);
            movie->has_audio = 1;
            platform_rep_movs(audio_ring() + audio_write(), packet + 8, size);
            audio_write() += static_cast<int32_t>(size);
            if (audio_write() >= 0x12000) audio_write() = 0;
            continue;
        }
        // Over a frame behind: drop B pictures.
        if (title_global<uint8_t>(0x00920859) && movie->time > title_global<float>(0x0083eb20) && (flags & 0xf0) == 0x60) {
            movie->time = movie->time - title_global<float>(0x0083eb20);
            ++movie->frame_index;
            continue;
        }
        break;
    }
    movie->time = movie->time + title_global<float>(0x007cf3a0);
    const uint32_t flags = packet[4];
    const int32_t coding_type = static_cast<int32_t>((flags >> 4) & 3) + 1;  // 1 I, 2 P, 3 B
    dec<int32_t>(movie, 0x2164) = coding_type;
    dec<uint32_t>(movie, 0x2198) = packet[5] >> 4;  // f_codes
    dec<uint32_t>(movie, 0x219c) = packet[5] & 0xf;
    dec<uint32_t>(movie, 0x21d0) = packet[6] >> 4;
    dec<uint32_t>(movie, 0x21d4) = packet[6] & 0xf;
    dec<uint32_t>(movie, 0x2234) = packet[7];        // intra DC precision
    dec<uint32_t>(movie, 0x2238) = flags & 8;        // frame_pred_frame_dct
    dec<uint8_t>(movie, 0x2246) = static_cast<uint8_t>(flags & 4);  // q_scale_type
    dec<uint32_t>(movie, 0x223c) = flags & 2;        // intra VLC format
    dec<uint32_t>(movie, 0x2240) = (flags & 1) ? 0x008bff60 : 0x008bff20;  // alternate / zigzag scan
    if (movie->format & 2) {
        const int32_t current = dec<int32_t>(movie, 0x222c);
        // Forward references +0x2168, backward +0x21a0.
        if (coding_type == 3) {
            uint8_t* const* forward = movie->planes[current ^ 1];
            dec<uint8_t*>(movie, 0x2168) = forward[0];
            dec<uint8_t*>(movie, 0x216c) = forward[1];
            dec<uint8_t*>(movie, 0x2170) = forward[2];
            dec<uint8_t*>(movie, 0x21a0) = movie->planes[current][0];
            dec<uint8_t*>(movie, 0x21a4) = movie->planes[current][1];
            dec<uint8_t*>(movie, 0x21a8) = movie->planes[current][2];
            MovieVid_BuildQuantTables(reinterpret_cast<uint8_t*>(movie), 1);
            MovieVid_BuildQuantTables(reinterpret_cast<uint8_t*>(movie), 0);
        } else if (coding_type == 2 || coding_type == 1) {
            if (coding_type == 2) {
                dec<uint8_t*>(movie, 0x2168) = movie->planes[current][0];
                dec<uint8_t*>(movie, 0x216c) = movie->planes[current][1];
                dec<uint8_t*>(movie, 0x2170) = movie->planes[current][2];
                MovieVid_BuildQuantTables(reinterpret_cast<uint8_t*>(movie), 1);
            }
            MovieVid_BuildQuantTables(reinterpret_cast<uint8_t*>(movie), 0);
            dec<int32_t>(movie, 0x222c) ^= 1;
        }
    }
    if (movie->flag_74) {
        movie->texture2 = nglGetBackBufferTexture();
        nglTexture_Lock(movie->texture2, 0x40);
    } else {
        nglTexture_Lock(movie->texture2, 0);
    }
    uint8_t* pixels = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(movie->texture2) + 0x20);  // locked bits
    // Per row: convert each macroblock row as it is decoded (format 1, B pictures: one-row planes).
    bool per_row;
    if (movie->format == 1) {
        per_row = true;
        set_dest_planes(movie, movie->planes[0]);
    } else if (dec<int32_t>(movie, 0x2164) == 3) {
        per_row = true;
        set_dest_planes(movie, movie->planes[2]);
    } else {
        set_dest_planes(movie, movie->planes[dec<int32_t>(movie, 0x222c)]);
        per_row = false;
    }
    uint8_t*& y_dest = dec<uint8_t*>(movie, 0x221c);
    // The slices: each starts with 0xfaca<length to the next> (0xfacadeca: none follows); the next
    // marker is overwritten with 0xa6a6a6a6 to stop the bit reader.
    movie->bit_ptr += 8;
    uint8_t* slice = movie->bit_ptr;
    uint32_t marker = *reinterpret_cast<uint32_t*>(slice);
    int32_t& row_y = dec<int32_t>(movie, 0x2218);
    row_y = 0;
    if (movie->height > 0) {
        do {
            movie->bit_ptr = slice;
            if (marker != 0xfacadeca) {
                slice += marker & 0xffff;
                marker = *reinterpret_cast<uint32_t*>(slice);
                if ((marker & 0xffff0000) != 0xfaca0000) {
                    movie->state = -1;
                    return -1;
                }
                *reinterpret_cast<uint32_t*>(slice) = 0xa6a6a6a6;
            }
            movie->bit_ptr += 4;
            MovieVid_DecodeMacroblocks(reinterpret_cast<uint8_t*>(movie));
            if (per_row) {
                MovieVid_ConvertMacroblockRow(&y_dest, pixels, movie->width);
                pixels += movie->width << 6;
            } else {
                y_dest += dec<int32_t>(movie, 0x2200);
                dec<uint8_t*>(movie, 0x2220) += dec<int32_t>(movie, 0x2204);
                dec<uint8_t*>(movie, 0x2224) += dec<int32_t>(movie, 0x2204);
            }
            row_y += 0x10;
        } while (row_y < movie->height);
    }
    if (!per_row) {
        // Whole pictures: convert the one to show (MPEG order: the previous reference unless format 3).
        int32_t shown = dec<int32_t>(movie, 0x222c);
        if (movie->format != 3) shown ^= 1;
        set_dest_planes(movie, movie->planes[shown]);
        row_y = 0;
        if (movie->height > 0) {
            do {
                MovieVid_ConvertMacroblockRow(&y_dest, pixels, movie->width);
                y_dest += dec<int32_t>(movie, 0x2200);
                dec<uint8_t*>(movie, 0x2220) += dec<int32_t>(movie, 0x2204);
                dec<uint8_t*>(movie, 0x2224) += dec<int32_t>(movie, 0x2204);
                pixels += movie->width << 6;
                row_y += 0x10;
            } while (row_y < movie->height);
        }
    }
    nglTexture_Unlock();
    NglTexture* const drawn = movie->texture2;
    movie->texture2 = movie->texture;
    movie->texture = drawn;
    if (movie->frame_index != movie->frame_count && movie->frame_index != 0) {
        ++movie->frame_index;
    } else {
        Movie_StartAudio(movie);
        movie->frame_index = 1;
    }
    if (movie->frame_index != movie->frame_count) return 0;
    if (movie->has_audio && (dsound_get_status(audio_buffer()) & 1))  // DSBSTATUS_PLAYING
        title_function<int32_t XB_STDCALL(void*)>(0x003c7777)(audio_buffer());  // IDirectSoundBuffer_Stop
    return 3;
}

void XB_THISCALL Movie_SetState(Movie* movie, int32_t state) { movie->state = state; }

int32_t XB_THISCALL Movie_GetState(Movie* movie) { return movie->state; }

int32_t XB_THISCALL Movie_GetFrameCount(Movie* movie) { return movie->frame_count; }

int32_t XB_THISCALL Movie_GetFrameIndex(Movie* movie) { return movie->frame_index; }

int32_t XB_THISCALL MovieBase_DecodeFrame(Movie*) { return -1; }

void XB_CDECL Movie_SetDropLateFrames(uint8_t drop) { title_global<uint8_t>(0x00920859) = drop; }

int32_t XB_THISCALL Movie_SetAudioTrack(Movie* movie, int32_t track) {
    movie->audio_track = track;
    return 0;
}

uint8_t XB_THISCALL Movie_HasPendingReads(Movie* movie) {
    uint8_t pending = 0;
    if (movie->requests[0] != -1) pending = 1;
    if (movie->requests[1] != -1) pending = 1;
    if (movie->requests[2] != -1) pending = 1;
    if (movie->requests[3] != -1) pending = 1;
    return pending;
}

int32_t XB_THISCALL MovieBase_Start(Movie* movie) {
    movie->owned = 2;
    if (!movie->memory) {
        movie->memory = static_cast<uint8_t*>(Mem_AlignedAlloc(movie->frame_bytes, 0x40, 0));
        movie->owned |= 1;
    }
    movie->owned <<= 1;
    if (!movie->buffers[0]) {
        for (uint8_t*& buffer : movie->buffers) buffer = static_cast<uint8_t*>(Mem_AlignedAlloc(movie->chunk_size, 0x40, 0));
        movie->owned |= 1;
    }
    movie->owned <<= 1;
    if (!movie->frame && movie->extra_bytes) {
        movie->frame = static_cast<uint8_t*>(Mem_AlignedAlloc(movie->extra_bytes, 0x80, 0));
        movie->owned |= 1;
    }
    movie->owned <<= 1;
    if (!movie->texture && !movie->flag_74) {
        movie->texture = nglCreateTexture(0x204, movie->width, movie->height, 0, 0);
        movie->owned |= 1;
    }
    movie->owned <<= 1;
    if (!movie->texture2 && !movie->flag_74) {
        movie->texture2 = nglCreateTexture(0x204, movie->width, movie->height, 0, 0);
        movie->owned |= 1;
    }
    slot<void XB_THISCALL(Movie*)>(movie, 3)(movie);  // Reset
    movie->state = 1;
    for (int32_t& request : movie->requests) request = -1;
    Movie_QueueReads(movie);
    uint8_t* p = movie->memory;
    switch (movie->format) {
    case 1: {  // one macroblock row
        const int32_t luma = movie->width << 4, chroma = luma >> 2;
        movie->planes[0][0] = p;
        p += luma;
        movie->planes[0][1] = p;
        p += chroma;
        movie->planes[0][2] = p;
        p += chroma;
        break;
    }
    case 3:
    case 7: {  // two reference pictures and a B row
        const int32_t width = movie->width;
        const int32_t luma = movie->height * width, chroma = luma >> 2;
        movie->planes[0][0] = p;
        p += luma;
        movie->planes[0][1] = p;
        p += chroma;
        movie->planes[0][2] = p;
        p += chroma;
        movie->planes[1][0] = p;
        p += luma;
        movie->planes[1][1] = p;
        p += chroma;
        movie->planes[1][2] = p;
        p += chroma;
        movie->planes[2][0] = p;
        const int32_t row = width << 4, row_chroma = row >> 2;
        p += row;
        movie->planes[2][1] = p;
        p += row_chroma;
        movie->planes[2][2] = p;
        p += row_chroma;
        break;
    }
    default:
        break;
    }
    movie->planes_end = p;
    return 0;
}

void XB_CDECL Movie_ReadCallback(int32_t status, int32_t request, Movie* movie) {
    if (status == 1) return;  // still pending
    Movie_OnReadComplete(movie, status, request);
}

}  // namespace game
