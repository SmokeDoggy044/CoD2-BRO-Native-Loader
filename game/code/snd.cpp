#include "snd.h"

#include <cstring>

#include "cg_ents.h"
#include "cl.h"
#include "fe.h"
#include "fx.h"
#include "g_scr_runtime.h"
#include "movie.h"
#include "ngl.h"
#include "qcommon.h"
#include "r_init.h"
#include "title_data.h"
#include "ui_menu.h"
#include "ui_panel.h"

namespace game {

uint32_t XB_CDECL SndVoice_GetCount() { return title_global<uint32_t>(0x008bed00); }

uint8_t* XB_CDECL SndVoice_Get(uint32_t index) { return &title_global<uint8_t>(0x00911548 + index * 0xe0); }

namespace {

uint32_t voice_state(uint32_t voice) { return title_function<uint32_t XB_CDECL(uint32_t)>(0x000c4400)(voice); }  // SndVoice_GetState

float voice_param(uint32_t voice, uint32_t param, float fallback) {
    return title_function<float XB_CDECL(uint32_t, uint32_t, float)>(0x000c59b0)(voice, param, fallback);  // SndVoice_GetParam
}

bool playing_state(uint32_t state) { return state == 4 || state == 2 || state == 3 || state == 5; }

uint32_t ref_word(const uint8_t* ref, uint32_t offset) { return *reinterpret_cast<const uint32_t*>(ref + offset); }

// S_StartAliasVoice2 (0x002bd770): EAX (byte +9 in AL), EBX, ESI ref, EDI alias, 8 words, ret 0x20.
extern "C" PLATFORM_HIDDEN void XB_CDECL S_StartAliasVoice2_call(uint32_t eax, uint32_t ebx, uint8_t* ref, uint32_t alias,
                                                                const uint32_t* args);
PLATFORM_ASM_ENTRY(S_StartAliasVoice2_call,
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 32(%esp), %ecx\n"
                   "pushl 28(%ecx)\n"
                   "pushl 24(%ecx)\n"
                   "pushl 20(%ecx)\n"
                   "pushl 16(%ecx)\n"
                   "pushl 12(%ecx)\n"
                   "pushl 8(%ecx)\n"
                   "pushl 4(%ecx)\n"
                   "pushl (%ecx)\n"
                   "movl 48(%esp), %eax\n"
                   "movl 52(%esp), %ebx\n"
                   "movl 56(%esp), %esi\n"
                   "movl 60(%esp), %edi\n"
                   "movl $0x002bd770, %ecx\n"
                   "call *%ecx\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

int32_t Snd_CountBusyVoices() {
    const uint32_t count = SndVoice_GetCount();
    int32_t busy = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (voice_state(ref_word(SndVoice_Get(i), 0xcc))) ++busy;
    }
    return busy;
}

float XB_REGS1 XB_CDECL SoundRef_GetParam0(const uint8_t* ref) { return voice_param(ref_word(ref, 4), 0, 1.0f); }

float XB_REGS1 XB_CDECL SoundRef_GetParam1a(const uint8_t* ref) { return voice_param(ref_word(ref, 4), 0x1a, 1.0f); }

PLATFORM_ASM_ENTRY(SoundRef_SetVolumeScale_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(SoundRef_SetVolumeScale) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL SoundRef_SetVolumeScale(uint8_t* ref, float scale) {
    const uint32_t voice = ref_word(ref, 0);
    if (voice == 0xffffffffu) return;
    const float volume = *reinterpret_cast<const float*>(ref + 0x1c) * scale;
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, float)>(0x000c44f0)(voice, 0, volume);  // SndVoice_SetParam
    *reinterpret_cast<float*>(ref + 0x10) = scale;
}

uint8_t XB_REGS1 XB_CDECL SoundRef_IsState3(const uint8_t* ref) {
    const uint32_t voice = ref_word(ref, 0);
    if (voice == 0xffffffffu) return 0;
    return voice_state(voice) == 3;
}

PLATFORM_ASM_ENTRY(SoundRef_IsBusy_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(SoundRef_IsBusy) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL SoundRef_IsBusy(const uint8_t* ref) {
    const uint32_t voice = ref_word(ref, 0);
    const bool playing = voice != 0xffffffffu && playing_state(voice_state(voice));
    if (!playing && !ref[8]) return 0;
    return title_function<uint32_t XB_CDECL(uint32_t)>(0x000c5a80)(ref_word(ref, 4)) ? 1 : 0;
}

uint8_t XB_REGS1 XB_CDECL SoundRef_IsDone(const uint8_t* ref) {
    if (ref[8]) return 0;
    const uint32_t voice = ref_word(ref, 0);
    if (voice == 0xffffffffu) return 1;
    return playing_state(voice_state(voice)) ? 0 : 1;
}

void XB_REGS3 XB_CDECL SoundRef_SetSource(const uint8_t* source, uint32_t, uint8_t* ref) {
    *reinterpret_cast<uint32_t*>(ref + 0x28) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source));
    const uint32_t voice = ref_word(ref, 0);
    if (voice == 0xffffffffu) return;
    float position[3];
    position[0] = *reinterpret_cast<const float*>(source + 0x30);
    position[1] = *reinterpret_cast<const float*>(source + 0x34);
    position[2] = *reinterpret_cast<const float*>(source + 0x38);
    title_function<uint32_t XB_CDECL(uint32_t, const float*)>(0x000c4690)(voice, position);
}

uint32_t* XB_REGS3 XB_STDCALL S_PlaySoundAlias2(uint32_t alias, uint32_t, uint8_t* table, uint32_t* out, uint32_t a1,
                                                uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                                uint32_t a7, uint32_t a8, uint32_t a9) {
    // 0x002bd3b0: a free reference of the table (ECX), its index or -1.
    const int32_t index = title_function<int32_t XB_REGS3 XB_CDECL(uint32_t, uint32_t, uint8_t*)>(0x002bd3b0)(0, 0, table);
    if (index < 0) {
        title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
        *out = 0;
        return out;
    }
    if (alias == 0xffffffffu) {
        *out = 0;
        return out;
    }
    uint8_t* ref = table + index * 0x30;
    const uint32_t args[8] = {a6, a7, a8, a9, a4, a5, a1, a2};
    // AL = a3's low byte over a8 (the original's EAX at the call).
    S_StartAliasVoice2_call((a8 & ~0xffu) | (a3 & 0xff), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out)), ref, alias,
                            args);
    *out = ref_word(ref, 0x24);
    return out;
}

namespace {

uint8_t* const kVoicePool = &title_global<uint8_t>(0x0090a8b4);

uint8_t* voice_record(int32_t index) {
    if (index == -1) return nullptr;
    return title_global<uint8_t*>(0x0090a8e0) + uint32_t(index) * 0xf8;
}

}  // namespace

uint32_t XB_CDECL SndVoice_ForEach(void(XB_CDECL* callback)(int32_t)) {
    uint32_t smallest = 0;
    int32_t handle = HandlePool_First(kVoicePool);
    while (handle != -1) {
        const int32_t voice = handle;
        handle = HandlePool_Next(kVoicePool, voice);
        const uint8_t* const record = voice_record(HandlePool_Resolve(kVoicePool, voice));
        if (callback) callback(voice);
        const uint32_t level = record[0xd4];
        if (smallest > level) smallest = level;
    }
    return smallest;
}

void XB_CDECL SndVoice_ClearAttenuation(int32_t voice) {
    uint8_t* const record = voice_record(HandlePool_Resolve(kVoicePool, voice));
    if (record) *reinterpret_cast<uint32_t*>(record + 0xf4) = 0;
}

uint32_t S_ClearVoiceAttenuation() {
    using Callback = void XB_CDECL(int32_t);
    return SndVoice_ForEach(title_function<Callback>(0x000c52b0));
}

namespace {

template <typename T>
T& emitter_word(const uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(const_cast<uint8_t*>(base) + offset);
}

const uint32_t* effect_list() { return title_global<const uint32_t*>(0x0095f5b4); }

// Rand_StepSeed (0x00293e10, XMM0 = high, XMM1 = low): (high - low) * (seed >> 17) / 32768 + low.
float random_between(float low, float high) {
    uint32_t& seed = title_global<uint32_t>(0x008d5a70);
    seed = seed * 0x343fd + 0x269ec3;
    const float step = platform_x87_unsigned_to_float(seed >> 17);
    return (high - low) * step / title_global<float>(0x0083ebc8) + low;
}

// FXCmd_PlaySoundAlias takes the owner entity in ECX (here the view's entity, read once at
// S_UpdateAmbientEmitters' start: (0x0095f534)->+8); regparm(3) puts it there.
uint32_t play_alias(const uint8_t* emitter, int32_t group, uint32_t owner) {
    using Play = uint32_t XB_REGS3 XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
    return title_function<Play>(0x002a6990)(0, 0, owner, emitter_word<uint32_t>(emitter, 0x40), 0, group,
                                            0);  // FXCmd_PlaySoundAlias
}

}  // namespace

PLATFORM_ASM_ENTRY(FX_FindEffectById_entry, "pushl %edi\n"
                                            "pushl %esi\n" PLATFORM_ASM_CALL(FX_FindEffectById) "addl $8, %esp\n"
                                            "ret")

uint8_t* XB_CDECL FX_FindEffectById(const uint32_t* list, uint32_t id) {
    for (uint32_t i = 0; i < list[0]; ++i) {
        uint8_t* const effect = reinterpret_cast<uint8_t*>(uintptr_t{list[1 + i]});
        if (emitter_word<uint32_t>(effect, 0x4c) == id) return effect;
    }
    return nullptr;
}

void XB_STDCALL S_UpdateAmbientEmitters(uint8_t* level, float frame_time) {
    if (frame_time == 0.0f) return;
    emitter_word<uint32_t>(level, 0x198) = 0;
    emitter_word<uint32_t>(level, 0x19c) = 0;
    const uint32_t owner = emitter_word<uint32_t>(title_global<uint8_t*>(0x0095f534), 8);
    for (int32_t group_index = 0; group_index < 0x63; ++group_index) {
        const uint8_t* const group = emitter_word<uint8_t*>(level, 4 + uint32_t(group_index) * 4);
        if (!group) continue;
        for (uint32_t j = 0; j < emitter_word<uint32_t>(group, 0x30); ++j) {
            uint8_t* const emitter = emitter_word<uint8_t*>(group, 0x34) +
                                     (j < emitter_word<uint32_t>(group, 0x30) ? j : 0) * 0x60;
            int32_t& state = emitter_word<int32_t>(emitter, 0x54);
            if (state == 5) continue;
            float& delay = emitter_word<float>(emitter, 0x58);
            uint32_t& id = emitter_word<uint32_t>(emitter, 0x50);
            uint8_t* effect = FX_FindEffectById(effect_list(), id);
            const auto trigger_on = [emitter] {
                const uint8_t* const link = emitter_word<uint8_t*>(emitter, 0x44);
                return link ? emitter_word<uint32_t>(link, 8) != 0 : true;
            };
            bool start = false;
            switch (state) {
            case 0: {
                delay = delay - title_global<float>(0x0083eb20);
                if (delay > 0.0f) continue;
                const uint32_t key = emitter_word<uint32_t>(emitter, 0x44);
                if (key) {
                    emitter_word<uint32_t>(emitter, 0x44) = 0;
                    const uint32_t* const table = emitter_word<uint32_t*>(level, 0x190);
                    for (uint32_t k = 0; k < table[0]; ++k) {
                        const uint8_t* const entry =
                            reinterpret_cast<const uint8_t*>(uintptr_t{table[1]}) + (k < table[0] ? k : 0) * 12;
                        if (emitter_word<uint32_t>(entry, 4) == key) {
                            emitter_word<const uint8_t*>(emitter, 0x44) = entry;
                            break;
                        }
                    }
                }
                if (!trigger_on()) {
                    state = 4;
                    break;
                }
                id = play_alias(emitter, group_index, owner);
                effect = FX_FindEffectById(effect_list(), id);
                state = 1;
                break;
            }
            case 1:
                if (!effect) {
                    state = 5;
                    continue;
                }
                state = 2;
                break;
            case 2: {
                const bool on = trigger_on();
                if (effect) {
                    if (on) break;
                    FX_FreeEffectsById(reinterpret_cast<FxIdList*>(const_cast<uint32_t*>(effect_list())), id);
                    id = 0;
                    state = 4;
                    continue;
                }
                if (!on) {
                    state = 4;
                    continue;
                }
                if (!(emitter[0x48] & 1)) {
                    state = 5;
                    continue;
                }
                const float scale = title_global<float>(0x0083eb94);  // 0.01
                const float low = float(int32_t(emitter_word<uint16_t>(emitter, 0x4c))) * scale;
                const float high = float(int32_t(emitter_word<uint16_t>(emitter, 0x4e))) * scale;
                const float wait = random_between(low, high);
                if (!(wait == 0.0f)) {
                    state = 3;
                    delay = wait;
                    continue;
                }
                start = true;
                break;
            }
            case 3:
                delay = delay - frame_time;
                if (!(0.0f >= delay)) break;
                start = true;
                break;
            case 4:
                if (emitter_word<uint8_t*>(emitter, 0x44) && emitter_word<uint32_t>(emitter_word<uint8_t*>(emitter, 0x44), 8))
                    state = 2;
                break;
            default:
                break;
            }
            if (start) {
                state = 2;
                id = play_alias(emitter, group_index, owner);
                effect = FX_FindEffectById(effect_list(), id);
            }
            if (!effect) continue;
            for (uint32_t n = 0; n < emitter_word<uint32_t>(effect, 0); ++n) {
                const uint32_t component = emitter_word<uint32_t>(effect, 4 + n * 4);
                using Update = void XB_THISCALL(uint32_t, uint8_t*);
                const uint32_t vtable = *reinterpret_cast<const uint32_t*>(uintptr_t{component});
                title_function<Update>(*reinterpret_cast<const uint32_t*>(uintptr_t{vtable + 0x24}))(component, emitter);
            }
        }
    }
}

uint8_t* XB_REGS1 XB_CDECL SoundHandle_Resolve(const uint32_t* handle) {
    const uint32_t value = *handle;
    const uint32_t index = value & 0xfff;
    if (index >= 0x200) return nullptr;
    if ((value >> 12) != title_global<uint32_t>(0x00962864 + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x00962860 + index * 8);
}

namespace {

// One crossfaded channel: param 0 of its voice = base (+0x1c) * GetParam(0) * master * volume; +0x10 = the product
// before the base.
void set_fade_volume(const uint8_t* fade, const uint32_t* handle, float volume, float* slot) {
    if (!SoundHandle_Resolve(handle)) return;
    using GetParam = long double XB_CDECL(uint32_t, uint32_t, float);
    const long double param = title_function<GetParam>(0x000c59b0)(emitter_word<uint32_t>(SoundHandle_Resolve(handle), 4),
                                                                   0, 1.0f);  // SndVoice_GetParam
    *slot = float(param * emitter_word<float>(fade, 8) * volume);
    uint8_t* const channel = SoundHandle_Resolve(handle);
    const uint32_t voice = emitter_word<uint32_t>(channel, 0);
    if (voice == 0xffffffff) return;
    using SetParam = void XB_CDECL(uint32_t, uint32_t, float);
    title_function<SetParam>(0x000c44f0)(voice, 0, emitter_word<float>(channel, 0x1c) * *slot);  // SndVoice_SetParam
    emitter_word<float>(channel, 0x10) = *slot;
}

}  // namespace

void XB_REGS1 XB_STDCALL S_UpdateCrossfade(uint8_t* fade, float time) {
    int32_t& state = emitter_word<int32_t>(fade, 0x1c);
    if (state != 1 && state != 2) return;
    const bool fading_in_first = state == 2;
    float& elapsed = emitter_word<float>(fade, 0x18);
    float& out = emitter_word<float>(fade, state == 1 ? 0xc : 0x10);
    float& in = emitter_word<float>(fade, state == 1 ? 0x10 : 0xc);
    const float t = time + elapsed;
    const float length = emitter_word<float>(fade, 0x14);
    const float one = title_global<float>(0x0083eb20);
    const float ratio = t / length;
    elapsed = t;
    in = ratio;
    out = one - ratio;
    if (!(t < length)) {
        out = 0.0f;
        in = one;
        state = 0;
        uint32_t* const second = &emitter_word<uint32_t>(fade, 4);
        if (fading_in_first && SoundHandle_Resolve(second)) {  // state 2: stop the faded-out channel
            TitleRegs regs{};
            regs.edi = uint32_t(uintptr_t(SoundHandle_Resolve(second)));
            cg_call_regs(0x002c3400, &regs, nullptr, 0);  // S_StopChannel (channel in EDI)
            *second = 0;
        }
    }
    set_fade_volume(fade, &emitter_word<uint32_t>(fade, 0), emitter_word<float>(fade, 0xc), &time);
    set_fade_volume(fade, &emitter_word<uint32_t>(fade, 4), emitter_word<float>(fade, 0x10), &time);
}

int32_t XB_CDECL S_SetMasterAttenuation(int32_t value) {
    const uint32_t sign = uint32_t(value >> 31);
    const int32_t attenuation = -int32_t((uint32_t(value) ^ sign) - sign);
    title_global<int32_t>(0x008bd650) = attenuation;
    return attenuation;
}

namespace {

uint8_t* voice_record_of(int32_t voice) {
    const int32_t index = HandlePool_Resolve(kVoicePool, voice);
    if (index == -1) return nullptr;
    return title_global<uint8_t*>(0x0090a8e0) + uint32_t(index) * 0xf8;
}

}  // namespace

uint32_t XB_CDECL SndVoice_GetState(int32_t voice) {
    const uint8_t* const record = voice_record_of(voice);
    if (!record) return 0;
    const uint8_t state = record[0xd4];
    if (state == 5 || (record[0xd7] & 2)) return 5;
    const uint8_t flags = record[0xd6];
    if (flags & 2) return 4;
    if (flags & 1) return (state == 3 ? 1u : 0u) + 2;
    return state;
}

void XB_CDECL SndVoice_SetParam(int32_t voice, uint32_t param, uint32_t value_bits) {
    uint8_t* const record = voice_record_of(voice);
    if (!record || param >= 0x2e) return;
    const uint64_t bit = uint64_t{1} << param;
    emitter_word<uint32_t>(record, 8) |= uint32_t(bit);
    emitter_word<uint32_t>(record, 0xc) |= uint32_t(bit >> 32);
    emitter_word<uint32_t>(record, 0x10 + param * 4) = value_bits;
}

void XB_CDECL SndVoice_SetOrigin(int32_t voice, const uint32_t* origin) {
    uint8_t* const record = voice_record_of(voice);
    if (!record) return;
    for (uint32_t i = 0; i < 3; ++i) emitter_word<uint32_t>(record, 0x5c + i * 4) = origin[i];
    emitter_word<uint32_t>(record, 8) |= 0x380000;
}

uint8_t* XB_CDECL SndBank_GetSoundEntry(uint32_t handle) {
    const uint32_t index = handle & 0xffff;
    const uint32_t key = handle | 0xffff;
    const uint8_t* const slot = &title_global<uint8_t>(0x008bd770 + ((key >> 16) % 12) * 32);
    if (emitter_word<uint32_t>(slot, 0) != key || emitter_word<uint32_t>(slot, 4) != 3) return nullptr;
    const uint8_t* const bank = emitter_word<uint8_t*>(slot, 0x18);
    if (index > emitter_word<uint32_t>(bank, 0x40)) return nullptr;
    return emitter_word<uint8_t*>(bank, 0x44) + index * 40;
}

int32_t XB_CDECL S_FindSoundByHash(const char* name) {
    if (!name) return -1;
    using Compare = int32_t XB_CDECL(const void*, const void*);
    using Search = void* XB_CDECL(const void*, const void*, uint32_t, uint32_t, Compare*);
    const uint32_t key = uint32_t(uintptr_t(name));
    for (uint32_t slot = 0; slot < 12; ++slot) {
        const uint32_t base = 0x008bd770 + slot * 32;
        if (title_global<uint32_t>(base + 4) != 3) continue;
        const uint8_t* const bank = title_global<uint8_t*>(base + 0x18);
        const uint8_t* const entries = emitter_word<uint8_t*>(bank, 0x44);
        const auto* const found = static_cast<const uint8_t*>(title_function<Search>(0x001f123f)(
            &key, entries, emitter_word<uint32_t>(bank, 0x40), 0x28, title_function<Compare>(0x000c5790)));  // _bsearch
        if (!found) continue;
        const int32_t index = int32_t(found - entries) / 40;
        if (index == -1) continue;
        return int32_t((title_global<uint32_t>(base) & 0xffff0000) | uint32_t(index));
    }
    return -1;
}

uint8_t* XB_CDECL SndGroup_FindByName(const char* name) {
    if (!name) return nullptr;
    using Compare = int32_t XB_CDECL(const char*, const char*, uint32_t);
    int32_t& count = title_global<int32_t>(0x008beb70);
    int32_t n = count;
    for (int32_t i = 0; i < n; ++i) {
        uint8_t* const group = &title_global<uint8_t>(0x008bd8f0 + uint32_t(i) * 0x128);
        if (title_function<Compare>(0x001f1486)(name, reinterpret_cast<const char*>(group + 0x108), 0x20) == 0)  // __strnicmp
            return group;
        n = count;
    }
    if (n == 15) return nullptr;
    uint8_t* const group = &title_global<uint8_t>(0x008bd8f0 + uint32_t(n) * 0x128);
    count = n + 1;
    emitter_word<int32_t>(group, 0) = -1;
    emitter_word<int32_t>(group, 4) = -1;
    emitter_word<float>(group, 8) = title_global<float>(0x0083eb20);
    emitter_word<float>(group, 0xc) = title_global<float>(0x0083eb20);
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(reinterpret_cast<char*>(group + 0x108), name,
                                                                          0x20);  // _strncpy
    return group;
}

PLATFORM_ASM_ENTRY(SoundHandle_Free_entry, "pushl 4(%esp)\n"
                                           "pushl %esi\n" PLATFORM_ASM_CALL(SoundHandle_Free) "addl $8, %esp\n"
                                           "ret $4")

void XB_CDECL SoundHandle_Free(uint8_t* table, uint32_t handle) {
    if (!handle) return;
    const uint32_t index = handle & 0xfff;
    if (index >= 0x200) return;
    uint32_t& generation = emitter_word<uint32_t>(table, 0x44 + index * 8);
    if (generation != handle >> 12) return;
    emitter_word<uint32_t>(table, uint32_t(int32_t(index) >> 5) * 4) |= 1u << (index & 31);
    ++generation;
    emitter_word<uint32_t>(table, 0x40 + index * 8) = 0;
}

const char* XB_CDECL Snd_GetAliasName(uint32_t handle) {
    const uint8_t* const entry = SndBank_GetSoundEntry(handle);
    return entry ? emitter_word<const char*>(entry, 0) : nullptr;
}

PLATFORM_ASM_ENTRY(S_StopChannel_entry, "pushl %edi\n" PLATFORM_ASM_CALL(S_StopChannel) "addl $4, %esp\n"
                                        "ret")

void XB_CDECL S_StopChannel(uint8_t* channel) {
    using Free = void XB_CDECL(int32_t);
    if (emitter_word<int32_t>(channel, 0) != -1) {
        Snd_GetAliasName(emitter_word<uint32_t>(channel, 4));  // result unused
        SoundHandle_Free(&title_global<uint8_t>(0x00962820), emitter_word<uint32_t>(channel, 0x24));
        emitter_word<uint32_t>(channel, 0x24) = 0;
        title_function<Free>(0x000c4140)(emitter_word<int32_t>(channel, 0));  // SndVoice_Free (thunk)
        title_function<Free>(0x000c3f50)(emitter_word<int32_t>(channel, 0));  // SndVoice_Free
        emitter_word<int32_t>(channel, 0) = -1;
    }
    const uint32_t handle = emitter_word<uint32_t>(channel, 0x20);
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8)) {
        if (uint8_t* const entity = title_global<uint8_t*>(0x0097a968 + index * 8)) {
            const uint32_t hash = Com_HashKeyNoCase(0, Snd_GetAliasName(emitter_word<uint32_t>(channel, 4)));
            const uint32_t pending = emitter_word<uint32_t>(entity, 0x3c4);
            if (pending && emitter_word<uint32_t>(entity, 0x3c8) == hash) {
                Scr_Notify(entity, pending);
                emitter_word<uint32_t>(entity, 0x3c4) = 0;
                emitter_word<uint32_t>(entity, 0x3c8) = 0;
            }
            if (const uint32_t notify = emitter_word<uint32_t>(channel, 0x2c)) Scr_Notify(entity, notify);
        }
    }
    emitter_word<float>(channel, 0x14) = title_global<float>(0x0083ec30);  // 50
    channel[8] = 0;
    emitter_word<uint32_t>(channel, 0x2c) = 0;
    emitter_word<uint32_t>(channel, 0x28) = 0;
    emitter_word<int32_t>(channel, 0) = -1;
    emitter_word<int32_t>(channel, 4) = -1;
    channel[9] = 1;
    emitter_word<float>(channel, 0xc) = title_global<float>(0x0083eb20);
    emitter_word<float>(channel, 0x10) = title_global<float>(0x0083eb20);
    emitter_word<float>(channel, 0x18) = title_global<float>(0x0083edb0);  // 1500
    emitter_word<float>(channel, 0x1c) = title_global<float>(0x0083eb20);
}

PLATFORM_ASM_ENTRY(S_StopSoundHandle_entry, "pushl %esi\n" PLATFORM_ASM_CALL(S_StopSoundHandle) "addl $4, %esp\n"
                                            "ret")

void XB_CDECL S_StopSoundHandle(uint32_t* handle) {
    if (!SoundHandle_Resolve(handle)) return;
    S_StopChannel(SoundHandle_Resolve(handle));
    *handle = 0;
}

PLATFORM_ASM_ENTRY(S_StopAllSounds_entry, "pushl %ebx\n" PLATFORM_ASM_CALL(S_StopAllSounds) "addl $4, %esp\n"
                                          "ret")

void XB_CDECL S_StopAllSounds(uint8_t* system) {
    for (uint32_t i = 0; i < 0x200; ++i) S_StopChannel(system + i * 0x30);
    uint8_t* const fade = title_global<uint8_t*>(0x0095f4cc);
    uint32_t* const first = &emitter_word<uint32_t>(fade, 0);
    if (SoundHandle_Resolve(first)) {
        S_StopChannel(SoundHandle_Resolve(first));
        *first = 0;
    }
    uint8_t* const current = title_global<uint8_t*>(0x0095f4cc);
    emitter_word<uint32_t>(current, 0x1c) = 2;
    emitter_word<float>(current, 0x14) = 0.0f;
    emitter_word<float>(current, 0x18) = 0.0f;
    title_function<void XB_STDCALL(uint8_t*)>(0x002c8c20)(system);  // S_StopAllChannels
}

void XB_CDECL SndSource_CommitDirty() {
    uint8_t* const pool = &title_global<uint8_t>(0x0090a8e4);
    int32_t handle = HandlePool_First(pool);
    while (handle != -1) {
        const int32_t source = handle;
        handle = HandlePool_Next(pool, source);
        const int32_t index = HandlePool_Resolve(pool, source);
        uint32_t* const record =
            index == -1 ? nullptr : reinterpret_cast<uint32_t*>(title_global<uint8_t*>(0x0090a910) + uint32_t(index) * 0xc8);
        record[0] |= record[2];
        record[1] |= record[3];
        record[2] = 0;
        record[3] = 0;
    }
}

void XB_CDECL SndHw_ReleaseSlot(uint32_t slot) {
    if (slot >= title_global<uint32_t>(0x008bed00)) return;
    uint8_t* const hardware = SndVoice_Get(slot);
    const uint8_t state = hardware[0xc0];
    emitter_word<int32_t>(hardware, 0xc8) = -1;
    if (!state) return;
    if (state == 1) {
        hardware[0xc0] = 0;
    } else {
        hardware[0xc1] |= 8;
    }
}

uint32_t XB_CDECL Snd_UpdateTime() {
    const int32_t ticks = title_function<int32_t XB_CDECL()>(0x001f12cb)();  // _clock
    const uint32_t now = uint32_t(uint64_t(int64_t(ticks) * 1000) / 1000);
    title_global<uint32_t>(0x0092082c) = now;
    return now;
}

namespace {

template <typename T>
T& snd_at(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

}  // namespace

uint32_t XB_CDECL Snd_PopCount64(uint32_t low, uint32_t high) {
    uint64_t v = (static_cast<uint64_t>(high) << 32) | low;
    v = ((v >> 1) & 0x5555555555555555ull) + (v & 0x5555555555555555ull);
    v = ((v >> 2) & 0x3333333333333333ull) + (v & 0x3333333333333333ull);
    v = ((v >> 4) & 0x0f0f0f0f0f0f0f0full) + (v & 0x0f0f0f0f0f0f0f0full);
    v = ((v >> 8) & 0x00ff00ff00ff00ffull) + (v & 0x00ff00ff00ff00ffull);
    v = ((v >> 16) & 0x0000ffff0000ffffull) + (v & 0x0000ffff0000ffffull);
    return static_cast<uint32_t>(v >> 32) + static_cast<uint32_t>(v);
}

void XB_CDECL SndVoice_ReleaseHwSlot(int32_t handle) {
    const int32_t index = HandlePool_Resolve(reinterpret_cast<const uint8_t*>(uintptr_t{0x0090a8b4}), handle);
    if (index == -1) return;
    uint8_t* voice = title_global<uint8_t*>(0x0090a8e0) + index * 0xf8;
    if (!voice) return;
    const int16_t slot = snd_at<int16_t>(voice, 0xd0);
    if (slot == -1) return;
    SndHw_ReleaseSlot(static_cast<uint32_t>(static_cast<int32_t>(slot)));
    snd_at<int16_t>(voice, 0xd0) = -1;
}

uint8_t* XB_CDECL Snd_GetGroups() { return reinterpret_cast<uint8_t*>(uintptr_t{0x008bd8f0}); }

uint8_t* XB_CDECL Snd_GetMixState() { return reinterpret_cast<uint8_t*>(uintptr_t{0x008bda18}); }

uint32_t XB_CDECL Snd_InitRequest(uint8_t* request, uint32_t flags, uint32_t a, uint32_t b, int32_t c) {
    snd_at<uint32_t>(request, 0x14) = b;
    snd_at<uint32_t>(request, 0x10) = a;
    snd_at<int32_t>(request, 8) = -1;
    snd_at<int32_t>(request, 0xc) = -1;
    snd_at<uint32_t>(request, 0x18) = 0x0091f780;
    snd_at<uint32_t>(request, 4) = flags;
    snd_at<int32_t>(request, 0x20) = c;
    if (c != -1) snd_at<uint32_t>(request, 4) = flags | 2;
    std::memset(reinterpret_cast<void*>(uintptr_t{0x0091f780}), 0xbf, 0x1000);
    snd_at<uint32_t>(request, 0) = 0;
    return 0xbfbfbfbf;
}

// ECX is restored as in the original (its frame slot is a PUSH ECX).
PLATFORM_ASM_ENTRY(Snd_NormalizeVector_entry,
                   "pushl %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Snd_NormalizeVector) "addl $8, %esp\n"
                   "popl %ecx\n"
                   "ret")

float* XB_CDECL Snd_NormalizeVector(float* out, const float* in) {
    const long double x = in[0], y = in[1], z = in[2];
    long double length = (x * x + y * y) + z * z;
    __asm__("fsqrt" : "+t"(length));
    const float rounded = static_cast<float>(length);
    const long double epsilon = title_global<float>(0x007ce5c8);
    if (epsilon >= length) {  // FCOMIP; JB (unordered: normalize)
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return out;
    }
    const float inv = 1.0f / rounded;
    out[0] = in[0];
    out[1] = in[1];
    out[0] = out[0] * inv;
    out[2] = in[2];
    out[1] = inv * out[1];
    out[2] = out[2] * inv;
    return out;
}

int32_t XB_CDECL SndHw_AllocSlot(uint32_t sound, uint32_t owner, uint32_t any_slot) {
    uint8_t* entry = SndBank_GetSoundEntry(sound);
    const uint8_t type = static_cast<uint8_t>(title_function<uint32_t XB_CDECL(uint8_t*)>(0x000c6fb0)(entry));
    const int32_t last = static_cast<int32_t>(title_global<uint32_t>(0x008bed00)) - 1;
    for (int32_t slot = 0; slot < last; ++slot) {
        if (!any_slot && slot <= 1) continue;
        uint8_t* record = reinterpret_cast<uint8_t*>(uintptr_t{0x00911548 + static_cast<uint32_t>(slot) * 0xe0});
        if (record[0xc2] != type || record[0xc0]) continue;
        std::memset(record, 0, 0xe0);
        snd_at<uint32_t>(record, 0xc8) = sound;
        record[0xc2] = type;
        record[0xc0] = 1;
        record[0xc1] = 0x40;
        snd_at<uint32_t>(record, 0xcc) = owner;
        record[0xc3] = entry[5];
        record[0xc4] = entry[6];
        title_global<uint8_t>(0x0091f678 + type) = static_cast<uint8_t>(slot + 1);
        return slot;
    }
    return -1;
}

void XB_CDECL Snd_UpdateListenerAxis() {
    uint8_t* m = Snd_GetMixState();
    auto f = [&](uint32_t offset) { return snd_at<float>(m, offset); };
    float* a = &title_global<float>(0x0091f654);
    float* b = a + 3;
    float* c = a + 6;
    a[0] = f(0xd0) * f(0xc8) - f(0xd4) * f(0xc4);
    a[1] = f(0xd4) * f(0xc0) - f(0xc8) * f(0xcc);
    a[2] = f(0xcc) * f(0xc4) - f(0xd0) * f(0xc0);
    Snd_NormalizeVector(a, a);
    b[0] = a[2] * f(0xc4) - a[1] * f(0xc8);
    b[1] = a[0] * f(0xc8) - f(0xc0) * a[2];
    b[2] = f(0xc0) * a[1] - a[0] * f(0xc4);
    Snd_NormalizeVector(b, b);
    c[0] = b[2] * a[1] - b[1] * a[2];
    c[1] = b[0] * a[2] - b[2] * a[0];
    c[2] = b[1] * a[0] - b[0] * a[1];
    Snd_NormalizeVector(c, c);
    const float* p = &title_global<float>(0x008bd668);
    float* out = &title_global<float>(0x008bd678);
    for (int32_t i = 0; i < 3; ++i) out[i] = (c[i] * p[2] + b[i] * p[1]) + a[i] * p[0];
    const float* q = &title_global<float>(0x008bd688);
    float* out2 = &title_global<float>(0x008bd698);
    for (int32_t i = 0; i < 3; ++i) out2[i] = (c[i] * q[2] + b[i] * q[1]) + a[i] * q[0];
}

namespace {

bool mask_bit(uint32_t low, uint32_t high, int32_t bit) {
    return ((static_cast<uint64_t>(high) << 32 | low) >> bit) & 1;
}

// A curve factor: the word's selected bit set -> the value at the popcount of the bits up to it, else `fallback`.
float curve_factor(const uint8_t* curve, uint32_t bit, uint32_t below, float fallback) {
    const uint32_t word = *reinterpret_cast<const uint32_t*>(curve);
    if (!(word & bit)) return fallback;
    const int32_t index = static_cast<int32_t>(Snd_PopCount64(word & below, 0)) - 1;
    if (index == -1) return fallback;
    return reinterpret_cast<const float*>(curve + 8)[index];
}

}  // namespace

void XB_CDECL SndVoice_ApplyParams() {
    Snd_GetMixState();
    uint8_t* const groups = Snd_GetGroups();
    uint8_t* const mix = Snd_GetMixState() + 8;
    const uint8_t* const pool = reinterpret_cast<const uint8_t*>(uintptr_t{0x0090a8b4});
    const uint8_t* const source_pool = reinterpret_cast<const uint8_t*>(uintptr_t{0x0090a8e4});
    using Free = void XB_CDECL(int32_t);
    constexpr uint32_t kFree = 0x000c3f50;  // SndVoice_Free
    int32_t handle = HandlePool_First(pool);
    while (handle != -1) {
        const int32_t next = HandlePool_Next(pool, handle);
        const int32_t index = HandlePool_Resolve(pool, handle);
        uint8_t* v = index == -1 ? nullptr : title_global<uint8_t*>(0x0090a8e0) + index * 0xf8;
        uint8_t* entry = SndBank_GetSoundEntry(snd_at<uint32_t>(v, 0xc8));
        do {
            if (!entry) break;
            if (v[0xd4] == 1) {
                if (!(v[0xd6] & 1)) break;
                v[0xd4] = 2;
            }
            uint32_t mask_low = snd_at<uint32_t>(v, 8);
            uint32_t mask_high = snd_at<uint32_t>(v, 0xc);
            snd_at<uint32_t>(v, 8) = 0;
            snd_at<uint32_t>(v, 0xc) = 0;
            const int32_t source_index = HandlePool_Resolve(source_pool, snd_at<int32_t>(v, 0xcc));
            uint8_t* source =
                source_index == -1 ? nullptr : title_global<uint8_t*>(0x0090a910) + source_index * 0xc8;
            if (source) {
                const uint32_t source_low = snd_at<uint32_t>(source, 8);
                const uint32_t source_high = snd_at<uint32_t>(source, 0xc);
                mask_low |= source_low;
                mask_high |= source_high;
                for (int32_t bit = 0; bit < 0x2e; ++bit)
                    if (mask_bit(source_low, source_high, bit))
                        snd_at<uint32_t>(v, 0x10 + bit * 4) = snd_at<uint32_t>(source, 0x10 + bit * 4);
            } else if (snd_at<int32_t>(v, 0xcc) != -1) {
                const bool free_it = v[0xd6] & 8;
                snd_at<int32_t>(v, 0xcc) = -1;
                if (free_it) {
                    title_function<Free>(kFree)(handle);
                    break;
                }
            }
            snd_at<uint32_t>(v, 0) |= mask_low;
            snd_at<uint32_t>(v, 4) |= mask_high;
            const float dx = snd_at<float>(v, 0x5c) - snd_at<float>(mix, 0x4c);
            const float dy = snd_at<float>(v, 0x60) - snd_at<float>(mix, 0x50);
            const float dz = snd_at<float>(v, 0x64) - snd_at<float>(mix, 0x54);
            const float range = snd_at<float>(v, 0x78);
            const float dist2 = (dz * dz + dx * dx) + dy * dy;
            const float range2 = range * range;
            const uint8_t flags = entry[5];
            const bool culls = (flags & 1) && (flags & 8);
            bool need_slot;
            if (culls && dist2 >= range2) {
                if (snd_at<int16_t>(v, 0xd0) == -1) {
                    need_slot = true;
                } else if (dist2 >= range2 * title_global<float>(0x0083f278)) {
                    v[0xd6] |= 0x20;
                    SndVoice_ReleaseHwSlot(handle);
                    break;
                } else {
                    need_slot = false;
                }
            } else {
                need_slot = snd_at<int16_t>(v, 0xd0) == -1;
            }
            if (need_slot) {
                if (culls && dist2 >= range2) break;
                const int32_t slot = SndHw_AllocSlot(snd_at<uint32_t>(v, 0xc8), static_cast<uint32_t>(handle),
                                                     v[0xd6] & 0x40);
                snd_at<int16_t>(v, 0xd0) = static_cast<int16_t>(slot);
                if (static_cast<uint16_t>(slot) == 0xffff) {
                    if (++snd_at<uint32_t>(v, 0xd8) > 10 && !(entry[5] & 8)) title_function<Free>(kFree)(handle);
                    break;
                }
                if (v[0xd6] & 0x20) {
                    v[0xd6] &= 0xdf;
                    v[0xd4] = 1;
                }
                mask_low |= snd_at<uint32_t>(v, 0);
                mask_high |= snd_at<uint32_t>(v, 4);
                snd_at<uint32_t>(v, 0xd8) = 0;
            }
            uint8_t* hw = reinterpret_cast<uint8_t*>(
                uintptr_t{0x00911548 + static_cast<uint32_t>(static_cast<int32_t>(snd_at<int16_t>(v, 0xd0)) * 0xe0)});
            if (hw[0xc0] == 6) {
                if (!(entry[5] & 8))
                    title_function<Free>(kFree)(handle);
                else
                    hw[0xc0] = 1;
                break;
            }
            if (v[0xd7] & 2) {
                v[0xd7] &= 0xfd;
                if (v[0xd4] == 4) hw[0xc1] |= 2;
                v[0xd5] = v[0xd4];
                v[0xd4] = 5;
            }
            if (v[0xd7] & 4) {
                v[0xd7] &= 0xfb;
                v[0xd4] = v[0xd5];
                if (v[0xd4] == 4) hw[0xc1] |= 4;
            }
            if (v[0xd4] == 2 && hw[0xc0] == 3) v[0xd4] = 3;
            if (v[0xd4] == 3 && (v[0xd6] & 2)) {
                v[0xd4] = 4;
                hw[0xc1] |= 1;
            }
            if (v[0xd7] & 8) {
                v[0xd7] &= 0xf7;
                hw[0xc1] |= (v[0xd6] & 0x10) ? 0x20 : 0x40;
            }
            for (int32_t bit = 0; bit < 0x2e; ++bit)
                if (mask_bit(mask_low, mask_high, bit)) snd_at<uint32_t>(hw, 8 + bit * 4) = snd_at<uint32_t>(v, 0x10 + bit * 4);
            float volume = snd_at<float>(v, 0x10);
            float pitch = snd_at<float>(v, 0x14);
            float extra = snd_at<float>(v, 0x7c);
            if (const uint8_t* curve = snd_at<uint8_t*>(entry, 0x14)) {
                volume = curve_factor(curve, 1, 1, 1.0f) * volume;
                pitch = curve_factor(curve, 2, 3, 1.0f) * pitch;
                extra = curve_factor(curve, 0x8000000, 0xfffffff, 1.0f) * extra;
            }
            if (const uint8_t* group = snd_at<uint8_t*>(entry, 0x10)) {
                volume = snd_at<float>(const_cast<uint8_t*>(group), 8) * volume;
                pitch = snd_at<float>(const_cast<uint8_t*>(group), 0xc) * pitch;
            }
            volume = snd_at<float>(groups, 8) * volume;
            pitch = snd_at<float>(groups, 0xc) * pitch;
            if (snd_at<int32_t>(v, 0xf4) > 0) volume = title_global<float>(0x008bd768) * volume;
            snd_at<float>(hw, 8) = volume;
            snd_at<float>(hw, 0x74) = extra;
            snd_at<float>(hw, 0xc) = pitch;
            snd_at<uint32_t>(hw, 0) |= mask_low | 0x8000003;
            snd_at<uint32_t>(hw, 4) |= mask_high;
        } while (false);
        handle = next;
    }
}

uint32_t XB_CDECL SndBank_Free(uint32_t key) {
    uint8_t* slot = reinterpret_cast<uint8_t*>(uintptr_t{0x008bd770 + ((key >> 16) % 12) * 0x20});
    if (snd_at<uint32_t>(slot, 0) != key) return 0;
    const uint32_t state = snd_at<uint32_t>(slot, 4);
    if (state == 2)  // still loading: cancel the request (tail call)
        return title_function<uint32_t XB_CDECL(uint32_t)>(0x000c91e0)(0x0090a914);
    if (state == 3) {
        if (uint8_t* bank = snd_at<uint8_t*>(slot, 0x18)) {
            if (bank[0x8c]) title_function<void XB_CDECL(uint32_t)>(0x000c8380)(snd_at<uint32_t>(bank, 0x88));
            snd_at<uint32_t>(snd_at<uint8_t*>(slot, 0x18), 0x88) = 0;
            snd_at<uint8_t*>(slot, 0x18)[0x8c] = 1;
            snd_at<uint32_t>(snd_at<uint8_t*>(slot, 0x18), 8) &= ~2u;
            title_function<void XB_CDECL(uint8_t*)>(0x000c3830)(snd_at<uint8_t*>(slot, 0x18));  // Mem_AlignedFree
            snd_at<uint8_t*>(slot, 0x18) = nullptr;
        }
    }
    snd_at<uint32_t>(slot, 0) += 0xc0000;
    snd_at<uint32_t>(slot, 4) = 0;
    return 0;
}

void XB_CDECL SndBank_UpdateLoads() {
    using Poll = int32_t XB_CDECL(uint32_t);
    constexpr uint32_t kPoll = 0x000c8fa0;
    uint8_t* const request = reinterpret_cast<uint8_t*>(uintptr_t{0x0090a914});
    int32_t best = -1;
    uint32_t best_priority = 0xffffffff;
    for (int32_t i = 0; i < 12; ++i) {
        uint8_t* slot = reinterpret_cast<uint8_t*>(uintptr_t{0x008bd770 + static_cast<uint32_t>(i) * 0x20});
        const uint32_t state = snd_at<uint32_t>(slot, 4);
        if (state == 0 || state == 3) continue;
        if (state == 1) {
            const uint32_t priority = snd_at<uint32_t>(slot, 0x14);
            if (best_priority > priority) {
                best_priority = priority;
                best = i;
            }
            continue;
        }
        if (state != 2) continue;
        const int32_t result = title_function<Poll>(kPoll)(0x0090a914);
        if (result == 1) {
            uint8_t* bank = title_global<uint8_t*>(0x0090a92c);
            snd_at<uint8_t*>(slot, 0x18) = bank;
            snd_at<uint32_t>(slot, 4) = 3;
            title_function<void XB_CDECL(void*, uint32_t, uint32_t, uint32_t)>(0x001ef880)(  // qsort
                snd_at<void*>(bank, 0x44), snd_at<uint32_t>(bank, 0x40), 0x28, 0x000c5790);
        } else if (result < 0) {
            snd_at<uint32_t>(slot, 4) = 3;
            SndBank_Free(snd_at<uint32_t>(slot, 0));
        } else {
            continue;
        }
        snd_at<uint32_t>(request, 0) = 0;
    }
    if (best == -1 || snd_at<uint32_t>(request, 0)) return;
    uint8_t* slot = reinterpret_cast<uint8_t*>(uintptr_t{0x008bd770 + static_cast<uint32_t>(best) * 0x20});
    snd_at<uint32_t>(slot, 4) = 2;
    Snd_InitRequest(request, snd_at<uint32_t>(slot, 8), snd_at<uint32_t>(slot, 0xc), snd_at<uint32_t>(slot, 0x10),
                    snd_at<int32_t>(slot, 0x1c));
    title_function<Poll>(kPoll)(0x0090a914);
}

int32_t XB_CDECL SndBank_PollLoad(uint8_t* request) {
    int32_t* r = reinterpret_cast<int32_t*>(request);
    using Queue = int32_t XB_CDECL(int32_t, int32_t, int32_t, int32_t);
    constexpr uint32_t kQueue = 0x000caed0;  // File_QueueRequest
    using Wait = int32_t XB_CDECL(int32_t);
    constexpr uint32_t kWait = 0x000c9db0;
    if (r[0] == 0x100) {
        if (r[2] != -1) {
            const int32_t result = title_function<Wait>(kWait)(r[2]);
            if (result != -1) return r[0];
            r[2] = result;
        }
        r[0] = -1;
        return r[0];
    }
    if (r[0] == 0) r[0] = 2;
    if (r[0] == 2) {
        r[2] = title_function<Queue>(kQueue)(r[4], r[5], r[6], 0x1000);
        if (r[2] != -1) r[0] = 3;
    }
    if (r[0] == 3) {
        const int32_t result = title_function<Wait>(kWait)(r[2]);
        if (result == -1) {
            r[2] = result;
            const uint8_t* header = reinterpret_cast<const uint8_t*>(uintptr_t{static_cast<uint32_t>(r[6])});
            if (std::memcmp(header, reinterpret_cast<const void*>(uintptr_t{0x007ce6bc}), 6) || header[6] != '1' ||
                header[7] != '1') {
                r[0] = -5;
                return r[0];
            }
            const uint32_t raw_size = *reinterpret_cast<const uint32_t*>(header + 0x14);
            r[7] = *reinterpret_cast<const int32_t*>(header + 0x10);
            r[9] = static_cast<int32_t>(raw_size);
            r[6] = static_cast<int32_t>(title_function<uint32_t XB_CDECL(int32_t)>(0x000c37f0)(r[7]));
            const uint32_t flags = static_cast<uint32_t>(r[1]);
            const bool shared = (flags >> 1) & 1;
            if (r[9]) {
                r[9] = static_cast<int32_t>((static_cast<uint32_t>(r[9]) + 0xfff) & 0xfffff000u);
                if (!shared)
                    r[8] = static_cast<int32_t>(title_function<uint32_t XB_REGS2 XB_CDECL(uint32_t, uint32_t, uint32_t, int32_t)>(
                        0x000c8250)(0, raw_size, flags, r[9]));
            }
            if (r[6] && (!r[9] || r[8])) {
                r[0] = 4;
            } else {
                if (!shared) title_function<void XB_CDECL(int32_t)>(0x000c8380)(r[8]);
                title_function<void XB_CDECL(int32_t)>(0x000c3830)(r[6]);  // Mem_AlignedFree
                r[6] = 0;
                r[8] = 0;
                r[0] = -4;
                return r[0];
            }
        }
    }
    if (r[0] == 4) {
        r[2] = title_function<Queue>(kQueue)(r[4], r[5], r[6], r[7]);
        if (r[2] != -1) r[0] = 5;
    }
    if (r[0] == 5) {
        const int32_t result = title_function<Wait>(kWait)(r[2]);
        if (result == -1) {
            r[2] = result;
            r[0] = r[9] ? 7 : 0xb;
        }
    }
    if (r[0] == 7) {
        r[2] = title_function<Queue>(kQueue)(r[4], r[7] + r[5], r[8], r[9]);
        if (r[2] != -1) r[0] = 8;
    }
    if (r[0] == 8) {
        const int32_t result = title_function<Wait>(kWait)(r[2]);
        if (result == -1) {
            r[2] = result;
            r[0] = 0xb;
        }
    }
    if (r[0] == 0xb) {
        r[0] = 1;
        title_function<void XB_CDECL(uint8_t*)>(0x000c8de0)(request);
    }
    return r[0];
}

uint32_t XB_CDECL Snd_GetSpeakerMode() {
    const int32_t value = title_global<int32_t>(0x008bd650);
    const uint32_t sign = uint32_t(value >> 31);
    return (uint32_t(value) ^ sign) - sign;
}

uint32_t XB_REGS3 XB_CDECL SndArena_Alloc(uint32_t, uint32_t, uint32_t alignment, uint32_t size) {
    uint32_t& used = title_global<uint32_t>(0x0090a894);
    const uint32_t old_used = used;
    const uint32_t base = title_global<uint32_t>(0x0090a890);
    const uint32_t aligned = (old_used + base + alignment - 1) / alignment * alignment;
    const uint32_t new_used = old_used + (size + alignment) - 1;
    used = new_used;
    if (!base) return 1;
    return title_global<uint32_t>(0x0090a898) >= new_used ? aligned : 0;
}

void XB_CDECL SndArena_EmptyStub() {}

void XB_CDECL Snd_Printf() {}

void XB_CDECL SndSys_EmptyStub() {}

void XB_CDECL SndArena_Reset() {
    SndArena_EmptyStub();
    title_global<uint32_t>(0x0090a890) = 0;
    title_global<uint32_t>(0x0090a894) = 0;
    title_global<uint32_t>(0x0090a898) = 0;
}

void* XB_CDECL SndBank_AllocAligned64(uint32_t size) { return Mem_AlignedAlloc(size, 0x40, 0); }

namespace {

constexpr uint32_t kSndMemFreed = 0x80000000u;
constexpr uint32_t kSndMemBlockCount = 0x40;

uint32_t& snd_mem_block(int32_t index) { return title_global<uint32_t>(0x0091f548 + uint32_t(index) * 4); }
int32_t& snd_mem_low_top() { return title_global<int32_t>(0x008bed08); }
int32_t& snd_mem_high_bottom() { return title_global<int32_t>(0x008bed04); }

}  // namespace

uint32_t XB_CDECL SndMem_Compact(uint32_t* low_end, uint32_t* high_end) {
    int32_t low = snd_mem_low_top();
    uint32_t high_total = title_global<uint32_t>(0x0091f64c);
    uint32_t low_total = 0;
    if (low >= 0) {
        while (snd_mem_block(low) & kSndMemFreed) {
            --low;
            snd_mem_block(low + 1) = 0;
            if (low < 0) break;
        }
    }
    snd_mem_low_top() = low;
    for (int32_t i = low; i >= 0; --i) low_total += snd_mem_block(i) & ~kSndMemFreed;
    int32_t high = snd_mem_high_bottom();
    while (high < int32_t(kSndMemBlockCount) && (snd_mem_block(high) & kSndMemFreed)) {
        snd_mem_block(high) = 0;
        ++high;
    }
    snd_mem_high_bottom() = high;
    if (high < int32_t(kSndMemBlockCount)) {
        // A pointer walk up to the end of the table (signed compare), as in the original.
        for (uint32_t at = 0x0091f548 + uint32_t(high) * 4; int32_t(at) < int32_t(0x0091f648); at += 4)
            high_total -= title_global<uint32_t>(at) & ~kSndMemFreed;
    }
    if (low_end) *low_end = low_total;
    if (high_end) *high_end = high_total;
    const uint32_t free_bytes = high_total - low_total;
    title_global<uint32_t>(0x0091f650) = free_bytes;
    return free_bytes;
}

uint32_t XB_CDECL SndMem_Alloc(uint32_t flags, int32_t size) {
    const uint32_t rounded = uint32_t((size + 0x23f) / 0x240 * 0x240);
    uint32_t low_end = 0, high_end = 0;
    if (SndMem_Compact(&low_end, &high_end) < rounded) return 0;
    const int32_t next = snd_mem_low_top() + 1;
    const int32_t high = snd_mem_high_bottom();
    if (next >= high) return 0;
    if (flags & 1) {
        const uint32_t address = title_global<uint32_t>(0x0091f648) - rounded + high_end;
        snd_mem_high_bottom() = high - 1;
        snd_mem_block(high - 1) = rounded;
        SndMem_Compact(nullptr, nullptr);
        return address;
    }
    snd_mem_low_top() = next;
    snd_mem_block(next) = rounded;
    const uint32_t address = title_global<uint32_t>(0x0091f648) + low_end;
    SndMem_Compact(nullptr, nullptr);
    return address;
}

uint32_t XB_CDECL SndMem_GetBase() { return title_global<uint32_t>(0x0091f648); }

uint32_t XB_CDECL SndMem_GetSize() { return title_global<uint32_t>(0x0091f64c); }

void XB_CDECL SndMem_Init(uint32_t address, uint32_t size) {
    title_global<uint32_t>(0x0091f648) = (address + 0x23f) / 0x240 * 0x240;
    title_global<uint32_t>(0x0091f64c) = (size - 0x240) / 0x240 * 0x240;
    snd_mem_high_bottom() = int32_t(kSndMemBlockCount);
    snd_mem_low_top() = -1;
}

void XB_CDECL SndMem_Free(uint32_t address) {
    if (!address) return;
    const uint32_t offset = address - title_global<uint32_t>(0x0091f648);
    uint32_t end = title_global<uint32_t>(0x0091f64c);
    if (offset >= end) return;
    const int32_t low = snd_mem_low_top();
    uint32_t start = 0;
    for (int32_t i = 0; low >= 0 && i <= low; ++i) {
        if (start == offset) {
            if (int32_t(snd_mem_block(i)) < 0) break;
            snd_mem_block(i) |= kSndMemFreed;
            SndMem_Compact(nullptr, nullptr);
            return;
        }
        start += snd_mem_block(i) & ~kSndMemFreed;
    }
    const int32_t high = snd_mem_high_bottom();
    if (high > int32_t(kSndMemBlockCount) - 1) return;
    for (int32_t i = int32_t(kSndMemBlockCount) - 1; i >= high; --i) {
        end -= snd_mem_block(i) & ~kSndMemFreed;
        if (end == offset) {
            if (int32_t(snd_mem_block(i)) < 0) return;
            snd_mem_block(i) |= kSndMemFreed;
            SndMem_Compact(nullptr, nullptr);
            return;
        }
    }
}

// ECX is restored as in the original (its frame slot is a PUSH ECX).
PLATFORM_ASM_ENTRY(Snd_NormalizeVectorDup_entry,
                   "pushl %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Snd_NormalizeVectorDup) "addl $8, %esp\n"
                   "popl %ecx\n"
                   "ret")

float* XB_CDECL Snd_NormalizeVectorDup(float* out, const float* in) {
    const long double x = in[0], y = in[1], z = in[2];
    long double length = (x * x + y * y) + z * z;
    __asm__("fsqrt" : "+t"(length));
    const float rounded = static_cast<float>(length);
    const long double epsilon = title_global<float>(0x007ce5c8);
    if (epsilon >= length) {  // FCOMIP; JB (unordered: normalize)
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return out;
    }
    const float inv = 1.0f / rounded;
    out[0] = in[0];
    const float in1 = in[1];
    const float p0 = out[0] * inv;
    out[1] = in1;
    const float in2 = in[2];
    out[0] = p0;
    const float p1 = inv * out[1];
    out[2] = in2;
    out[1] = p1;
    out[2] = out[2] * inv;
    return out;
}

void XB_CDECL Snd_SetListenerOrigin(const uint32_t* origin) {
    for (uint32_t i = 0; i < 3; ++i) title_global<uint32_t>(0x008bda6c + i * 4) = origin[i];
    title_global<uint32_t>(0x008bda18) |= 0x380000;
}

void XB_CDECL Snd_SetListenerOrientation(const uint32_t* a, const uint32_t* b) {
    for (uint32_t i = 0; i < 3; ++i) title_global<uint32_t>(0x008bdae4 + i * 4) = b[i];
    for (uint32_t i = 0; i < 3; ++i) title_global<uint32_t>(0x008bdad8 + i * 4) = a[i];
    float* const first = &title_global<float>(0x008bdad8);
    float* const second = &title_global<float>(0x008bdae4);
    Snd_NormalizeVectorDup(first, first);
    Snd_NormalizeVectorDup(second, second);
    title_global<uint32_t>(0x008bda1c) |= 0xfc000;
}

uint32_t XB_REGS1 XB_CDECL SndFormat_GetChannelCount(const uint8_t* format) {
    if (!format) return 0;
    const uint8_t mask = format[6];
    if (!mask) return 1;
    const uint8_t pairs = uint8_t(((mask >> 1) & 0x55) + (mask & 0x55));
    const uint8_t nibbles = uint8_t(((pairs >> 2) & 0x33) + (pairs & 0x33));
    return uint32_t(nibbles >> 4) + (nibbles & 0xf);
}

const char* XB_CDECL SndBank_GetEntryGroupName(uint32_t handle) {
    const uint8_t* const entry = SndBank_GetSoundEntry(handle);
    if (!entry) return nullptr;
    return reinterpret_cast<const char*>(emitter_word<uint8_t*>(entry, 0x10) + 0x108);
}

uint32_t XB_CDECL SndBank_GetEntryFlag3(uint32_t handle) {
    const uint8_t* const entry = SndBank_GetSoundEntry(handle);
    if (!entry) return 0;
    return uint32_t(entry[5] >> 3) & 1;
}

uint32_t XB_CDECL Snd_PopCount64_2(uint32_t low, uint32_t high) { return Snd_PopCount64(low, high); }

uint32_t XB_CDECL Snd_PopCount64_3(uint32_t low, uint32_t high) { return Snd_PopCount64(low, high); }

int32_t XB_REGS1 XB_CDECL Snd_MaskBitIndex64(const uint32_t* mask, uint32_t bit_low, uint32_t bit_high) {
    const uint32_t low = mask[0];
    const uint32_t high = mask[1];
    if ((low & bit_low) != bit_low || (high & bit_high) != bit_high) return -1;
    const uint64_t below = ((uint64_t{bit_high} << 32 | bit_low) * 2) - 1;
    return int32_t(Snd_PopCount64_2(uint32_t(below) & low, uint32_t(below >> 32) & high)) - 1;
}

int32_t XB_CDECL SndBank_IsLoading(uint32_t key) {
    const uint32_t slot = 0x008bd770 + ((key >> 16) % 12) * 32;
    if (title_global<uint32_t>(slot) != key) return -1;
    return title_global<uint32_t>(slot + 4) != 3 ? 1 : 0;
}

uint32_t XB_CDECL SndBank_AddLoadRequest(uint32_t a, int32_t file, uint32_t b, uint32_t c) {
    if (file == -1) return 0xffffffffu;
    uint32_t slot = 0;
    for (; slot < 12; ++slot) {
        if (title_global<uint32_t>(0x008bd770 + slot * 32 + 4) == 0) break;
    }
    if (slot == 12) return 0xffffffffu;
    uint8_t* const record = &title_global<uint8_t>(0x008bd770 + slot * 32);
    const uint32_t generation = emitter_word<uint16_t>(record, 2);
    uint32_t key = (((generation / 12) + 1) * 3 << 18) & 0x7fff0000u;
    key = (key + (slot << 16)) | 0xffff;
    emitter_word<uint32_t>(record, 8) = a;
    emitter_word<uint32_t>(record, 0x10) = b;
    uint32_t& serial = title_global<uint32_t>(0x0090a93c);
    emitter_word<uint32_t>(record, 0x14) = serial;
    ++serial;
    emitter_word<int32_t>(record, 0xc) = file;
    emitter_word<uint32_t>(record, 0) = key;
    emitter_word<uint32_t>(record, 4) = 1;
    emitter_word<uint32_t>(record, 0x18) = 0;
    emitter_word<uint32_t>(record, 0x1c) = c;
    return key;
}

uint32_t XB_CDECL SndBank_LoadFromFile(uint32_t a, int32_t file, uint32_t b) {
    return SndBank_AddLoadRequest(a, file, b, 0xffffffffu);
}

PLATFORM_ASM_ENTRY(SndBank_FindSound_entry, "pushl %esi\n"
                                            "pushl %eax\n" PLATFORM_ASM_CALL(SndBank_FindSound) "addl $8, %esp\n"
                                            "ret")

int32_t XB_CDECL SndBank_FindSound(const char* name, const uint8_t* bank) {
    using Compare = int32_t XB_CDECL(const void*, const void*);
    using Search = void* XB_CDECL(const void*, const void*, uint32_t, uint32_t, Compare*);
    const uint32_t key = name ? uint32_t(uintptr_t(name)) : 0x0082dfebu;
    const auto* const found = static_cast<const uint8_t*>(title_function<Search>(0x001f123f)(
        &key, emitter_word<uint8_t*>(bank, 0x44), emitter_word<uint32_t>(bank, 0x40), 0x28,
        title_function<Compare>(0x000c5790)));  // _bsearch
    if (!found) return -1;
    return int32_t(found - emitter_word<uint8_t*>(bank, 0x44)) / 40;
}

PLATFORM_ASM_ENTRY(Snd_ApproxAngle2D_entry, "subl $8, %esp\n"
                                            "movss %xmm1, 4(%esp)\n"
                                            "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(Snd_ApproxAngle2D) "fstps (%esp)\n"
                                            "movss (%esp), %xmm0\n"
                                            "addl $8, %esp\n"
                                            "ret")

float XB_CDECL Snd_ApproxAngle2D(float x, float y) {
    const float zero = 0.0f;
    const float scale = title_global<float>(0x0083eea0);
    const float c1 = title_global<float>(0x0083eba8);
    const float c2 = title_global<float>(0x007bd0c8);
    const float c3 = title_global<float>(0x0083ec5c);
    if (y == zero && x == zero) return zero * scale;
    if (x > zero) {
        if (y > zero) {
            if (y > x) return x / y * scale;
            return (c1 - y / x) * scale;
        }
        if (x > zero - y) return (c1 - y / x) * scale;
        return (x / y + c2) * scale;
    }
    if (zero > y) {
        if (zero - y > zero - x) return (x / y - c2) * scale;
        return (c3 - y / x) * scale;
    }
    if (zero - x > y) {
        if (x == zero) return zero * scale;
        return (c3 - y / x) * scale;
    }
    if (y == zero) return zero * scale;
    return x / y * scale;
}

uint32_t XB_CDECL SndVoice_AllocPools(const uint32_t* config) {
    if (title_global<uint32_t>(0x0090a894)) return 0xffffffffu;
    uint32_t voices, sources;
    if (config) {
        voices = config[0];
        title_global<uint32_t>(0x008bd654) = voices;
        sources = config[1];
        title_global<uint32_t>(0x008bd658) = sources;
        title_global<uint32_t>(0x008bd65c) = config[2];
        title_global<uint32_t>(0x008bd660) = config[3];
    } else {
        sources = title_global<uint32_t>(0x008bd658);
        voices = title_global<uint32_t>(0x008bd654);
    }
    const uint32_t base = title_global<uint32_t>(0x0090a890);
    struct Pool {
        uint32_t address, alignment, size;
    };
    const Pool pools[] = {{0x0090a8dc, 8, voices * 12},
                          {0x0090a8e0, 8, voices * 0xf8},
                          {0x0090a90c, 8, sources * 12},
                          {0x0090a910, 8, sources * 0xc8},
                          {0x0090a8a0, 0x10, 0x10}};
    const uint32_t header = SndArena_Alloc(0, 0, 0x10, 0x10);  // inlined in the original
    title_global<uint32_t>(0x0090a89c) = header;
    bool ok = header != 0;
    for (uint32_t i = 0; ok && i < 5; ++i) {
        const uint32_t block = SndArena_Alloc(0, 0, pools[i].alignment, pools[i].size);
        title_global<uint32_t>(pools[i].address) = block;
        ok = block != 0;
    }
    if (!ok && base) return 0xffffffffu;
    return title_global<uint32_t>(0x0090a894);
}

namespace {

using PoolAlloc = int32_t XB_CDECL(uint8_t*);
using PoolFree = void XB_CDECL(uint8_t*, int32_t);

}  // namespace

int32_t XB_CDECL SndVoice_Create(uint32_t sound, uint32_t any_slot) {
    const uint8_t* const entry = SndBank_GetSoundEntry(sound);
    if (!entry) return -1;
    const int32_t handle = title_function<PoolAlloc>(0x000cb510)(kVoicePool);  // HandlePool_Alloc
    const int32_t index = HandlePool_Resolve(kVoicePool, handle);
    if (index == -1) return -1;
    uint8_t* const record = title_global<uint8_t*>(0x0090a8e0) + uint32_t(index) * 0xf8;
    if (!record) return -1;
    std::memset(record, 0, 0xf8);
    emitter_word<uint32_t>(record, 0xc8) = sound;
    emitter_word<uint16_t>(record, 0xd0) = 0xffff;
    record[0xd4] = 1;
    emitter_word<int32_t>(record, 0xcc) = -1;
    const uint16_t rate = emitter_word<uint16_t>(entry, 0x20);
    emitter_word<uint32_t>(record, 0xe0) =
        rate ? uint32_t(uint64_t{emitter_word<uint32_t>(entry, 0xc)} * 1000 / rate) : 0;
    if (entry[5] & 1) {
        const uint32_t dirty = emitter_word<uint32_t>(record, 8);
        emitter_word<float>(record, 0x74) = title_global<float>(0x0083eb98);
        emitter_word<float>(record, 0x78) = title_global<float>(0x0083ee38);
        emitter_word<float>(record, 0x80) = title_global<float>(0x0083eb20);
        emitter_word<uint32_t>(record, 8) = dirty | 0x16000000;
        if (any_slot) record[0xd6] |= 0x40;
    } else {
        const int32_t slot = SndHw_AllocSlot(sound, uint32_t(handle), any_slot);
        emitter_word<uint16_t>(record, 0xd0) = uint16_t(slot);
        if (uint16_t(slot) == 0xffff && !any_slot) {
            title_function<PoolFree>(0x000cb580)(kVoicePool, handle);  // HandlePool_Free
            return -1;
        }
    }
    const float one = title_global<float>(0x0083eb20);
    if (const uint32_t* const params = emitter_word<const uint32_t*>(entry, 0x14)) {
        using Unpack = void XB_CDECL(uint8_t*, uint32_t, uint32_t, const uint32_t*);
        title_function<Unpack>(0x000c3b40)(record + 0x10, params[0], params[1], params + 2);  // BitMask64_Unpack
        const uint32_t* const set = emitter_word<const uint32_t*>(entry, 0x14);
        emitter_word<uint32_t>(record, 8) |= set[0];
        emitter_word<uint32_t>(record, 0xc) |= set[1];
    }
    emitter_word<uint32_t>(record, 8) |= 0x8000003;
    emitter_word<float>(record, 0x14) = one;
    emitter_word<float>(record, 0x10) = one;
    emitter_word<float>(record, 0x7c) = one;
    return handle;
}

void XB_CDECL SndVoice_Free(int32_t voice) {
    const uint8_t* const record = voice_record_of(voice);
    if (!record) return;
    const int16_t slot = emitter_word<int16_t>(record, 0xd0);
    if (slot != -1) SndHw_ReleaseSlot(uint32_t(int32_t(slot)));
    title_function<PoolFree>(0x000cb580)(kVoicePool, voice);  // HandlePool_Free
}

void XB_CDECL SndVoice_Queue(int32_t voice) {
    if (uint8_t* const record = voice_record_of(voice)) record[0xd6] |= 1;
}

void XB_CDECL SndVoice_Play(int32_t voice) {
    if (uint8_t* const record = voice_record_of(voice)) record[0xd6] |= 3;
}

void XB_CDECL SndVoice_Pause(int32_t voice) {
    uint8_t* const record = voice_record_of(voice);
    if (!record) return;
    uint16_t& count = emitter_word<uint16_t>(record, 0xd2);
    const uint16_t old = count;
    count = uint16_t(old + 1);
    if (old) return;
    if (record[0xd4] == 5)
        record[0xd7] &= 0xfb;
    else
        record[0xd7] |= 2;
}

void XB_CDECL SndVoice_Resume(int32_t voice) {
    uint8_t* const record = voice_record_of(voice);
    if (!record || !SndBank_GetSoundEntry(emitter_word<uint32_t>(record, 0xc8))) return;
    uint16_t& count = emitter_word<uint16_t>(record, 0xd2);
    if (!count) return;
    count = uint16_t(count - 1);
    if (count) return;
    if (record[0xd4] == 5)
        record[0xd7] |= 4;
    else
        record[0xd7] &= 0xfd;
}

void XB_CDECL SndVoice_SetVelocity(int32_t voice, const uint32_t* velocity) {
    uint8_t* const record = voice_record_of(voice);
    if (!record) return;
    for (uint32_t i = 0; i < 3; ++i) emitter_word<uint32_t>(record, 0x68 + i * 4) = velocity[i];
    emitter_word<uint32_t>(record, 8) |= 0x1c00000;
}

void XB_CDECL SndVoice_SetSource(int32_t voice, int32_t source) {
    const int32_t source_index = HandlePool_Resolve(&title_global<uint8_t>(0x0090a8e4), source);
    const uint8_t* const source_record =
        source_index == -1 ? nullptr : title_global<uint8_t*>(0x0090a910) + uint32_t(source_index) * 0xc8;
    uint8_t* const record = voice_record_of(voice);
    if (!record) return;
    if (!source_record) {
        emitter_word<int32_t>(record, 0xcc) = -1;
        return;
    }
    const uint32_t low = emitter_word<uint32_t>(source_record, 8) | emitter_word<uint32_t>(source_record, 0);
    const uint32_t high = emitter_word<uint32_t>(source_record, 0xc) | emitter_word<uint32_t>(source_record, 4);
    const uint32_t old_high = emitter_word<uint32_t>(record, 0xc);
    emitter_word<int32_t>(record, 0xcc) = source;
    const uint32_t old_low = emitter_word<uint32_t>(record, 8);
    emitter_word<uint32_t>(record, 0xc) = old_high | high;
    emitter_word<uint32_t>(record, 8) = old_low | low;
    const uint64_t mask = uint64_t{high} << 32 | low;
    for (uint32_t i = 0; i < 0x2e; ++i) {
        if (mask & (uint64_t{1} << i))
            emitter_word<uint32_t>(record, 0x10 + i * 4) = emitter_word<uint32_t>(source_record, 0x10 + i * 4);
    }
}

uint32_t XB_CDECL Snd_SetVoiceVolumeScale(uint32_t scale_bits) {
    title_global<uint32_t>(0x008bd768) = scale_bits;
    return SndVoice_ForEach(title_function<void XB_CDECL(int32_t)>(0x000c5270));
}

uint8_t* XB_CDECL SndVoice_GetSound(int32_t voice) {
    const int32_t index = HandlePool_Resolve(kVoicePool, voice);
    if (index == -1) return reinterpret_cast<uint8_t*>(uintptr_t{0xffffffffu});
    const uint8_t* const record = title_global<uint8_t*>(0x0090a8e0) + uint32_t(index) * 0xf8;
    if (!record) return nullptr;
    return SndBank_GetSoundEntry(emitter_word<uint32_t>(record, 0xc8));
}

float XB_CDECL SndVoice_GetParam(uint32_t sound, uint32_t param, float fallback) {
    const uint8_t* const entry = SndBank_GetSoundEntry(sound);
    if (!entry || param >= 0x2e) return fallback;
    const uint32_t* const params = emitter_word<const uint32_t*>(entry, 0x14);
    if (!params) return fallback;
    using IndexOfBit = int32_t XB_REGS1 XB_CDECL(const uint32_t*, uint32_t, uint32_t);
    const uint64_t bit = uint64_t{1} << param;
    const int32_t index =
        title_function<IndexOfBit>(0x000c56b0)(params, uint32_t(bit), uint32_t(bit >> 32));  // BitMask64_IndexOfBit
    if (index == -1) return fallback;
    return *reinterpret_cast<const float*>(params + 2 + index);
}

int32_t XB_CDECL S_SetMasterVolume(uint32_t volume_bits) {
    if (uint8_t* const group = SndGroup_FindByName(reinterpret_cast<const char*>(uintptr_t{0x007ce5cc}))) {
        emitter_word<uint32_t>(group, 0) |= 1;
        emitter_word<uint32_t>(group, 8) = volume_bits;
    }
    return S_SetMasterAttenuation(int32_t(Snd_GetSpeakerMode()));
}

float XB_CDECL Snd_GetMasterGroupVolume() {
    const uint8_t* const group = SndGroup_FindByName(reinterpret_cast<const char*>(uintptr_t{0x007ce5cc}));
    return group ? emitter_word<float>(group, 8) : 0.0f;
}

void XB_REGS1 XB_CDECL S_ResumeAllChannels(uint8_t* channels) {
    for (uint32_t i = 0; i < 0x200; ++i) {
        uint8_t* const channel = channels + i * 0x30;
        const int32_t voice = emitter_word<int32_t>(channel, 0);
        if (voice == -1) continue;
        SndVoice_Resume(voice);
        channel[8] = 0;
    }
}

void XB_REGS1 XB_CDECL S_PauseAllChannels(uint8_t* channels) {
    for (uint32_t i = 0; i < 0x200; ++i) {
        uint8_t* const channel = channels + i * 0x30;
        const int32_t voice = emitter_word<int32_t>(channel, 0);
        if (voice == -1) continue;
        SndVoice_Pause(voice);
        channel[8] = 1;
    }
}

void XB_CDECL SndBank_CancelLoad(int32_t* request) {
    if (request[0] == -1 || request[0] == 0x100) return;
    if (request[2] != -1) title_function<void XB_CDECL(int32_t)>(0x000ca0e0)(request[2]);  // File_AbortRequest
    request[0] = 0x100;
}

namespace {

uint32_t float_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

float bits_float(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// A value argument when positive, else the sound's param (default `fallback`).
uint32_t param_or_default(uint32_t value_bits, uint32_t alias, uint32_t param, float fallback) {
    if (bits_float(value_bits) > title_global<float>(0x0083eafc)) return value_bits;
    return float_bits(SndVoice_GetParam(alias, param, fallback));
}

// S_StartAliasVoice / S_StartAliasVoice2: `queue_only` = the second (SndVoice_Queue first, no SndVoice_Play).
void start_alias_voice(bool queue_only, uint32_t eax, uint8_t* channel, uint32_t alias, uint32_t volume,
                       uint32_t pitch, uint32_t near_bits, uint32_t far_bits, const uint32_t* origin,
                       const uint32_t* velocity, uint32_t owner, uint32_t any_slot) {
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    emitter_word<uint32_t>(channel, 4) = alias;
    channel[9] = uint8_t(eax);
    const int32_t voice = SndVoice_Create(alias, any_slot & 0xff);
    emitter_word<int32_t>(channel, 0) = voice;
    if (voice == -1) return;
    if (queue_only) SndVoice_Queue(voice);
    const char* group = SndBank_GetEntryGroupName(alias);
    if (uintptr_t(group) < 0x10000) group = reinterpret_cast<const char*>(uintptr_t{0x0083755c});
    // Both results are unused (a stripped debug check).
    if (title_function<Stricmp>(0x001f1443)(group, reinterpret_cast<const char*>(uintptr_t{0x0083755c})))
        title_function<Stricmp>(0x001f1443)(group, reinterpret_cast<const char*>(uintptr_t{0x00837554}));
    emitter_word<float>(channel, 0x1c) = title_global<float>(0x0083eb20);
    const uint32_t v0 = param_or_default(volume, alias, 0, 1.0f);
    if (emitter_word<int32_t>(channel, 0) != -1) {
        SndVoice_SetParam(emitter_word<int32_t>(channel, 0), 0,
                          float_bits(emitter_word<float>(channel, 0x1c) * bits_float(v0)));
        emitter_word<uint32_t>(channel, 0x10) = v0;
    }
    const uint32_t v1 = param_or_default(pitch, alias, 1, 1.0f);
    if (emitter_word<int32_t>(channel, 0) != -1) {
        SndVoice_SetParam(emitter_word<int32_t>(channel, 0), 1, v1);
        emitter_word<uint32_t>(channel, 0xc) = v1;
    }
    const uint32_t v3 = param_or_default(far_bits, alias, 0x1a, 1500.0f);
    const uint32_t v2 = param_or_default(near_bits, alias, 0x19, 50.0f);
    if (emitter_word<int32_t>(channel, 0) != -1) {
        SndVoice_SetParam(emitter_word<int32_t>(channel, 0), 0x19, v2);
        SndVoice_SetParam(emitter_word<int32_t>(channel, 0), 0x1a, v3);
        SndVoice_SetParam(emitter_word<int32_t>(channel, 0), 0x1c, 0x40800000);  // 4.0
        const int32_t current = emitter_word<int32_t>(channel, 0);
        emitter_word<uint32_t>(channel, 0x14) = v2;
        emitter_word<uint32_t>(channel, 0x18) = v3;
        if (current != -1) {
            const uint32_t position[3] = {origin[0], origin[1], origin[2]};
            SndVoice_SetOrigin(current, position);
            if (emitter_word<int32_t>(channel, 0) != -1) {
                const uint32_t speed[3] = {velocity[0], velocity[1], velocity[2]};
                SndVoice_SetVelocity(emitter_word<int32_t>(channel, 0), speed);
            }
        }
    }
    SndVoice_GetSound(emitter_word<int32_t>(channel, 0));  // result unused
    if (!queue_only) SndVoice_Play(emitter_word<int32_t>(channel, 0));
    emitter_word<uint32_t>(channel, 0x20) = owner;
}

}  // namespace

// EAX (AL), ESI channel, EDI alias, 8 stack words, ret 0x20.
#define SND_START_ALIAS_ENTRY(NAME)                                                                                   \
    PLATFORM_ASM_ENTRY(NAME##_entry, "pushl 32(%esp)\n"                                                               \
                                     "pushl 32(%esp)\n"                                                               \
                                     "pushl 32(%esp)\n"                                                               \
                                     "pushl 32(%esp)\n"                                                               \
                                     "pushl 32(%esp)\n"                                                               \
                                     "pushl 32(%esp)\n"                                                               \
                                     "pushl 32(%esp)\n"                                                               \
                                     "pushl 32(%esp)\n"                                                               \
                                     "pushl %edi\n"                                                                   \
                                     "pushl %esi\n"                                                                   \
                                     "pushl %eax\n" PLATFORM_ASM_CALL(NAME) "addl $44, %esp\n"                        \
                                     "ret $0x20")

SND_START_ALIAS_ENTRY(S_StartAliasVoice)
SND_START_ALIAS_ENTRY(S_StartAliasVoice2)

void XB_CDECL S_StartAliasVoice(uint32_t eax, uint8_t* channel, uint32_t alias, uint32_t volume, uint32_t pitch,
                                uint32_t near_bits, uint32_t far_bits, const uint32_t* origin, const uint32_t* velocity,
                                uint32_t owner, uint32_t any_slot) {
    start_alias_voice(false, eax, channel, alias, volume, pitch, near_bits, far_bits, origin, velocity, owner, any_slot);
}

void XB_CDECL S_StartAliasVoice2(uint32_t eax, uint8_t* channel, uint32_t alias, uint32_t volume, uint32_t pitch,
                                 uint32_t near_bits, uint32_t far_bits, const uint32_t* origin, const uint32_t* velocity,
                                 uint32_t owner, uint32_t any_slot) {
    start_alias_voice(true, eax, channel, alias, volume, pitch, near_bits, far_bits, origin, velocity, owner, any_slot);
}

namespace {

// HandleTable512_Alloc (0x0001ee70): table in EAX, out pointer in EBX; returns EBX.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL snd_handle_table_alloc(uint8_t* table, uint32_t* out);
PLATFORM_ASM_ENTRY(snd_handle_table_alloc, "pushl %ebx\n"
                                           "movl 8(%esp), %eax\n"
                                           "movl 12(%esp), %ebx\n"
                                           "movl $0x0001ee70, %ecx\n"
                                           "call *%ecx\n"
                                           "popl %ebx\n"
                                           "ret")

}  // namespace

int32_t XB_REGS3 XB_CDECL S_AllocChannelHandle(uint32_t, uint32_t, uint8_t* table) {
    int32_t index = 0;
    while (emitter_word<int32_t>(table, uint32_t(index) * 0x30) != -1) {
        if (uint32_t(++index) >= 0x200) return -1;
    }
    uint8_t* const channel = table + uint32_t(index) * 0x30;
    SoundHandle_Free(&title_global<uint8_t>(0x00962820), emitter_word<uint32_t>(channel, 0x24));
    uint32_t handle = 0;
    const uint32_t value = *snd_handle_table_alloc(&title_global<uint8_t>(0x00962820), &handle);
    emitter_word<uint32_t>(channel, 0x24) = value;
    const uint32_t slot = value & 0xfff;
    if (slot < 0x200) title_global<uint8_t*>(0x00962860 + slot * 8) = channel;
    return index;
}

uint32_t* XB_REGS3 XB_STDCALL S_PlaySoundAlias(uint32_t alias, uint32_t, uint8_t* table, uint32_t* out, uint32_t a1,
                                               uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                               uint32_t a7, uint32_t a8, uint32_t a9) {
    const int32_t index = S_AllocChannelHandle(0, 0, table);
    if (index < 0) {
        title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
        *out = 0;
        return out;
    }
    if (alias == 0xffffffffu || title_global<int32_t>(0x0092bdf0) == 1) {  // cls_state
        *out = 0;
        return out;
    }
    uint8_t* const channel = table + uint32_t(index) * 0x30;
    S_StartAliasVoice(a3, channel, alias, a6, a7, a8, a9, reinterpret_cast<const uint32_t*>(uintptr_t(a4)),
                      reinterpret_cast<const uint32_t*>(uintptr_t(a5)), a1, a2);
    *out = emitter_word<uint32_t>(channel, 0x24);
    return out;
}

uint32_t* XB_REGS1 XB_STDCALL S_PlayLocalSoundByHash(uint8_t* table, uint32_t* out, const char* name, uint32_t owner,
                                                     uint32_t any_slot) {
    uint32_t velocity[4], origin[4];
    for (uint32_t i = 0; i < 4; ++i) {
        velocity[i] = title_global<uint32_t>(0x0082e110 + i * 4);
        origin[i] = title_global<uint32_t>(0x0082e290 + i * 4);
    }
    const int32_t alias = S_FindSoundByHash(name);
    const int32_t index = S_AllocChannelHandle(0, 0, table);
    if (index < 0) title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
    if (index < 0 || alias == -1) {
        *out = 0;
        return out;
    }
    uint8_t* const channel = table + uint32_t(index) * 0x30;
    S_StartAliasVoice2(0, channel, uint32_t(alias), 0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000, origin, velocity,
                       owner, any_slot);
    *out = emitter_word<uint32_t>(channel, 0x24);
    return out;
}

void XB_REGS1 XB_CDECL S_StopUnpausedChannels(uint8_t* channels) {
    for (uint32_t i = 0; i < 0x200; ++i) {
        if (!channels[i * 0x30 + 8]) S_StopChannel(channels + i * 0x30);
    }
}

void XB_REGS1 XB_CDECL S_StopCrossfadeChannels(const uint32_t* handles) {
    if (SoundHandle_Resolve(&handles[0])) S_StopChannel(SoundHandle_Resolve(&handles[0]));
    if (SoundHandle_Resolve(&handles[1])) S_StopChannel(SoundHandle_Resolve(&handles[1]));
}

namespace {

// One channel of S_SetHandleVolume: param 0 of its voice = base (+0x1c) * (GetParam(0) * volume * master); +0x10 =
// the product.
void set_handle_volume(const uint8_t* fade, const uint32_t* handle, float volume) {
    if (!SoundHandle_Resolve(handle)) return;
    using GetParam = long double XB_CDECL(uint32_t, uint32_t, float);
    const long double param = title_function<GetParam>(0x000c59b0)(emitter_word<uint32_t>(SoundHandle_Resolve(handle), 4),
                                                                   0, 1.0f);  // SndVoice_GetParam
    const float product = float(param * volume * emitter_word<float>(fade, 8));
    uint8_t* const channel = SoundHandle_Resolve(handle);
    const int32_t voice = emitter_word<int32_t>(channel, 0);
    if (voice == -1) return;
    SndVoice_SetParam(voice, 0, float_bits(emitter_word<float>(channel, 0x1c) * product));
    emitter_word<float>(channel, 0x10) = product;
}

}  // namespace

PLATFORM_ASM_ENTRY(S_SetHandleVolume_entry, "subl $4, %esp\n"
                                            "movss %xmm0, (%esp)\n"
                                            "pushl %eax\n" PLATFORM_ASM_CALL(S_SetHandleVolume) "addl $8, %esp\n"
                                            "ret")

void XB_CDECL S_SetHandleVolume(uint8_t* fade, uint32_t volume_bits) {
    emitter_word<uint32_t>(fade, 8) = volume_bits;
    set_handle_volume(fade, &emitter_word<uint32_t>(fade, 0), emitter_word<float>(fade, 0xc));
    set_handle_volume(fade, &emitter_word<uint32_t>(fade, 4), emitter_word<float>(fade, 0x10));
}

uint8_t XB_REGS1 XB_STDCALL SndBankList_IsLoaded(const uint8_t* list, const uint32_t* hash) {
    const int32_t count = *reinterpret_cast<const int32_t*>(list + 8);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t* entry = reinterpret_cast<const uint32_t*>(list + 0xc + i * 0x84);
        int32_t k = 0;
        while (k < 8 && entry[k] == hash[k]) ++k;
        if (k < 8) continue;
        for (int32_t s = 0; s < 6; ++s) {
            const uint32_t state = entry[9 + s];
            if (state == 1 || state == 3) return 0;
        }
        return 1;
    }
    return 1;
}

PLATFORM_ASM_ENTRY(S_WaitForWaveBanks_entry, "pushl %edi\n" PLATFORM_ASM_CALL(S_WaitForWaveBanks) "addl $4, %esp\n"
                                             "ret")

void XB_CDECL S_WaitForWaveBanks(uint8_t* manager) {
    bool done;
    do {
        title_function<void XB_STDCALL(uint8_t*)>(0x002c8f60)(manager);  // S_CheckWaveBankNotifies
        done = true;
        const int32_t count = emitter_word<int32_t>(manager, 8);
        for (int32_t i = 0; i < count; ++i) {
            for (uint32_t j = 0; j < 6; ++j) {
                const int32_t state = emitter_word<int32_t>(manager, 0x30 + uint32_t(i) * 0x84 + j * 4);
                if (state == 1 || state == 3) done = false;
            }
        }
        if (count <= 0) break;
    } while (!done);
}

// S_OpenWaveBank (0x002ce830): path in EAX, bank entry in EDI, wave bank index in ESI; (manager, no_wait) on the
// stack, ret 8.
PLATFORM_ASM_ENTRY(S_OpenWaveBank_entry, "pushl 8(%esp)\n"
                                         "pushl 8(%esp)\n"
                                         "pushl %esi\n"
                                         "pushl %edi\n"
                                         "pushl %eax\n" PLATFORM_ASM_CALL(S_OpenWaveBank) "addl $20, %esp\n"
                                         "ret $8")

void XB_CDECL S_OpenWaveBank(const char* path, uint8_t* bank, uint32_t index, uint8_t* manager, uint32_t no_wait) {
    using OpenRead = int32_t XB_CDECL(uint32_t, const char*);
    const uint32_t name = emitter_word<uint32_t>(bank, 0x3c + index * 4);
    const int32_t file = title_function<OpenRead>(0x000c9d40)(title_global<uint32_t>(0x0095f6f4), path);  // File_OpenRead
    emitter_word<int32_t>(bank, 0x54 + index * 4) = file;
    if (file == -1 || emitter_word<int32_t>(bank, 0x6c + index * 4) != -1) {
        manager[5] = 1;
        return;
    }
    const uint32_t key = SndBank_LoadFromFile(0, file, name);
    emitter_word<uint32_t>(bank, 0x6c + index * 4) = key;
    emitter_word<uint32_t>(bank, 0x24 + index * 4) = 1;
    if (key == 0xffffffffu) {
        manager[5] = 1;
        title_function<void XB_CDECL(int32_t)>(0x000cac90)(emitter_word<int32_t>(bank, 0x54 + index * 4));  // File_Close
        emitter_word<int32_t>(bank, 0x54 + index * 4) = -1;
        return;
    }
    if (uint8_t(no_wait)) return;
    if (SndBank_IsLoading(key) == 1) {
        do {
            title_function<void XB_STDCALL(uint8_t*)>(0x002c8f60)(manager);  // S_CheckWaveBankNotifies
        } while (SndBank_IsLoading(emitter_word<uint32_t>(bank, 0x6c + index * 4)) == 1);
    }
    TitleRegs regs{};
    regs.eax = title_global<uint32_t>(0x0095f638);
    const uint32_t one = 0x3f800000;
    cg_call_regs(0x0028bd30, &regs, &one, 1);  // Pak_ReportProgress (EAX, 1.0; ret 4)
}

void XB_STDCALL S_LoadSoundBank(uint8_t* manager, const uint32_t* name, uint32_t no_wait) {
    const int32_t count = emitter_word<int32_t>(manager, 8);
    uint8_t* bank = nullptr;
    for (int32_t i = 0; i < count && !bank; ++i) {
        uint8_t* const candidate = manager + 0xc + uint32_t(i) * 0x84;
        bool same = true;
        for (uint32_t k = 0; k < 8 && same; ++k) same = emitter_word<uint32_t>(candidate, k * 4) == name[k];
        if (same) bank = candidate;
    }
    if (!bank) {
        manager[5] = 1;
        return;
    }
    const int32_t zone = emitter_word<int32_t>(bank, 0x20);
    uint32_t path = 8;
    if (zone >= 0 && zone < 0x63) path += title_global<uint32_t>(title_global<uint32_t>(0x0095f638) + uint32_t(zone) * 4 + 0x38);
    const char* const dir = reinterpret_cast<const char*>(uintptr_t(path));
    if (emitter_word<uint32_t>(bank, 0x50)) S_OpenWaveBank(dir, bank, 5, manager, no_wait);
    if (emitter_word<uint32_t>(bank, 0x3c + title_global<uint32_t>(0x0095f5dc) * 4)) {
        S_OpenWaveBank(dir, bank, title_global<uint32_t>(0x0095f5dc), manager, no_wait);
        if (emitter_word<uint32_t>(bank, 0x3c + title_global<uint32_t>(0x0095f5dc) * 4)) return;
    }
    if (!emitter_word<uint32_t>(bank, 0x50)) title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
}

// --- Sound options menu ---

namespace {

template <typename T>
T& menu_field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

// A title object's vtable slot (byte offset), called thiscall.
template <typename F>
F* menu_method(uint8_t* object, uint32_t offset) {
    return title_function<F>(menu_field<uint32_t>(menu_field<uint8_t*>(object, 0), offset));
}

void menu_call(uint8_t* object, uint32_t offset) { menu_method<void XB_THISCALL(uint8_t*)>(object, offset)(object); }
void menu_call(uint8_t* object, uint32_t offset, uint32_t a) {
    menu_method<void XB_THISCALL(uint8_t*, uint32_t)>(object, offset)(object, a);
}

void menu_z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }  // Z_Free

// The volume moved by `step` and clamped to 0..50 (the move sound +0x14 unless clamped), then the sliders.
void step_volume(uint8_t* menu, uint32_t step) {
    if (menu_field<int16_t>(menu, 0x22) != 0) return;
    int32_t& volume = menu_field<int32_t>(menu, 0x40);
    volume = int32_t(uint32_t(volume) + step);
    if (volume < 0) {
        volume = 0;
    } else if (volume > 0x32) {
        volume = 0x32;
    } else {
        menu_call(menu, 0x14);
    }
    SoundMenu_UpdateSliders(menu);
}

}  // namespace

uint8_t* XB_REGS3 XB_CDECL SoundMenu_Construct(uint8_t* owner, uint32_t, uint8_t* menu) {
    UIMenu_Init(menu, 3, owner, 0x140, 0xf0, 8, 0);
    menu_field<uint16_t>(menu, 0x2a) |= 0x180;
    menu_field<uint32_t>(menu, 0) = 0x00829fa0;  // vtable
    menu[0x33] = 5;
    menu_field<uint32_t>(menu, 0x34) = 0;
    menu_field<uint32_t>(menu, 0x38) = 0;
    menu_field<int32_t>(menu, 0x40) = 0x32;
    menu_field<uint32_t>(menu, 0x3c) = 0;
    return menu;
}

void XB_REGS1 XB_CDECL SoundMenu_Dtor(uint8_t* menu) {
    uint8_t* const panel = menu_field<uint8_t*>(menu, 0x34);
    menu_field<uint32_t>(menu, 0) = 0x00829fa0;
    if (panel) Panel_Delete(panel);
    if (uint8_t* const help = menu_field<uint8_t*>(menu, 0x38)) menu_call(help, 0, 1);
    menu_field<uint32_t>(menu, 0) = 0x0082a628;  // UIMenu vtable
    UIMenu_FreeItems(menu);
}

uint8_t* XB_THISCALL SoundMenu_DeletingDtor(uint8_t* menu, uint32_t flags) {
    SoundMenu_Dtor(menu);
    if (flags & 1) menu_z_free(menu);
    return menu;
}

void XB_THISCALL SoundMenu_Draw(uint8_t* menu) {
    if (uint8_t* const panel = menu_field<uint8_t*>(menu, 0x34)) {
        TitleRegs regs{};
        regs.esi = uint32_t(uintptr_t(panel));
        regs.edi = uint32_t(uintptr_t(menu));
        ui_call_regs(0x00310010, &regs, 0, nullptr);  // Panel_Draw
    }
    // FEMENU_HELPBAR_BACK / FEMENU_OP_HELPBAR
    menu_call(menu_field<uint8_t*>(menu, 0x38), 0x6c, menu_field<int16_t>(menu, 0x22) < 1 ? 0x0082c560u : 0x0082c37cu);
    menu_call(menu_field<uint8_t*>(menu, 0x38), 0x3c);
    UIMenu_Draw(menu);
    if (title_global<uint8_t>(0x0095f916) || !title_global<uint8_t*>(0x0095f454)) return;
    menu_call(title_global<uint8_t*>(0x0095f454), 8);
    Subtitle_Draw();
    uint8_t* const texture =
        title_function<uint8_t* XB_THISCALL(uint8_t*)>(0x000cf100)(title_global<uint8_t*>(0x0095f454));  // Movie_GetFrameTexture
    uint8_t* const quad = title_global<uint8_t*>(0x0095f458);
    menu_field<uint8_t*>(quad, 0x60) = texture;
    nglListAddQuad(reinterpret_cast<const NglQuad*>(quad));
}

void XB_THISCALL SoundMenu_OnLeft(uint8_t* menu, uint32_t) { step_volume(menu, 0xffffffffu); }

void XB_THISCALL SoundMenu_OnRight(uint8_t* menu, uint32_t) { step_volume(menu, 1); }

uint32_t XB_THISCALL SoundMenu_RestoreSelection(uint8_t* menu) {
    UIMenu_RestoreSelection(menu);
    if (!title_global<uint8_t>(0x0095f91a) && Movie_Open(&title_global<const char>(0x0082cacc))) {  // "Propgnda"
        nglSetQuadRect(title_global<NglQuad*>(0x0095f458), 0.0f, 0.0f, 640.0f, 480.0f);
        title_global<uint8_t>(0x0095f91a) = 1;
    }
    menu_field<uint32_t>(menu, 0x40) = title_global<uint32_t>(0x008dce88);
    return SoundMenu_UpdateSliders(menu);
}

uint32_t XB_REGS1 XB_CDECL SoundMenu_UpdateSliders(uint8_t* menu) {
    const float one = title_global<float>(0x0083eb20);
    const float level = float(menu_field<int32_t>(menu, 0x40)) * title_global<float>(0x00830670);  // 0.02
    float clamped;
    if (0.0f > level) {
        clamped = 0.0f;
    } else if (level > one) {
        clamped = one;
    } else {
        clamped = level;
    }
    uint8_t* const slider = menu_field<uint8_t*>(menu, 0x3c);
    const float width = menu_field<float>(slider, 0x34);
    uint8_t* const quads = *menu_field<uint8_t**>(slider, 0x20);
    float fraction = clamped;
    if (clamped > one) fraction = title_global<float>(0x0083eba8) - clamped;  // 2
    NglQuad* const bar = reinterpret_cast<NglQuad*>(quads + 0x40);
    const float right = (menu_field<float>(quads, 0x24) - menu_field<float>(quads, 0)) * width * fraction +
                        menu_field<float>(quads, 0x40);
    const float x0 = menu_field<float>(quads, 0x40);
    const float y0 = menu_field<float>(quads, 0x44);
    const float y3 = menu_field<float>(quads, 0x80);
    const float u0 = menu_field<float>(quads, 0x48);
    const float u1 = menu_field<float>(quads, 0x5c);
    const float epsilon = title_global<float>(0x0083eb4c);  // 0.001
    // The bar's u runs along vertices 0 -> 2 (rotated) when vertices 0 and 1 share it.
    const bool rotated = u0 >= u1 ? epsilon > u0 - u1 : epsilon > u1 - u0;
    float x[4] = {x0, rotated ? x0 : right, rotated ? right : x0, right};
    float y[4] = {y0, rotated ? y3 : y0, rotated ? y0 : y3, y3};
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexPos(bar, i, x[i], y[i]);
    const float u_start = menu_field<float>(quads, 0x48);
    const float u_end = fraction + u_start;
    const float v_start = menu_field<float>(quads, 0x4c);
    const float v_end = menu_field<float>(quads, 0x88);
    float u[4] = {u_start, rotated ? u_start : u_end, rotated ? u_end : u_start, u_end};
    float v[4] = {v_start, rotated ? v_end : v_start, rotated ? v_start : v_end, v_end};
    for (int32_t i = 0; i < 4; ++i) nglSetQuadVertexUV(bar, i, u[i], v[i]);
    S_SetHandleVolume(title_global<uint8_t*>(0x0095f4cc), float_bits(level));
    uint8_t* const system = title_global<uint8_t*>(0x0095f4c4);
    float& volume = menu_field<float>(system, 0x6018);
    volume = level;
    if (!(0.0f < level)) volume = 0.0f;
    if (!(volume < one)) volume = one;
    const float quiet = one - volume;
    return uint32_t(S_SetMasterVolume(float_bits(one - quiet * quiet)));
}

// --- Format, bank manager ---

uint8_t XB_CDECL SndFormat_GetVoiceType(const uint8_t* format) {
    if (format[4] == 6) return 2;
    const uint8_t flags = format[5];
    if (!(flags & 2)) return (flags & 1) ? 5 : 6;
    if (SndFormat_GetChannelCount(format) == 6) return 1;
    if (SndFormat_GetChannelCount(format) == 2) {
        return *reinterpret_cast<const uint16_t*>(format + 0x20) > 0x5dc0 ? 7 : 8;
    }
    return (flags & 1) ? 3 : 4;
}

void XB_STDCALL SoundBankManager_Dtor(uint8_t* manager) {
    menu_field<uint32_t>(manager, 0) = 0x008327a8;  // vtable
    for (int32_t bank = 0; bank < menu_field<int32_t>(manager, 8); ++bank) {
        for (uint32_t wave = 0; wave < 6; ++wave) {
            const uint32_t at = 0x78 + uint32_t(bank) * 0x84 + wave * 4;
            const uint32_t key = menu_field<uint32_t>(manager, at);
            if (key == 0xffffffffu) continue;
            SndBank_Free(key);
            title_function<void XB_CDECL(uint32_t)>(0x000cac90)(menu_field<uint32_t>(manager, at - 0x18));  // File_Close
            menu_field<uint32_t>(manager, at) = 0xffffffffu;
            menu_field<uint32_t>(manager, at - 0x18) = 0xffffffffu;
        }
    }
    ResourceManager_Destruct(manager);
}

uint8_t* XB_THISCALL SoundBankManager_DeletingDtor(uint8_t* manager, uint32_t flags) {
    SoundBankManager_Dtor(manager);
    if (flags & 1) menu_z_free(manager);
    return manager;
}

// --- Per-frame update ---

void XB_STDCALL S_StopAllChannels(uint8_t* channels) {
    SndSys_Update();
    for (uint32_t i = 0; i < 0x200; ++i) {
        uint8_t* const channel = channels + i * 0x30;
        const int32_t solo = title_global<int32_t>(0x008dda84);
        if (solo == -1) {
            if (channel[9]) {
                if (emitter_word<int32_t>(channel, 0) == -1) continue;
                if (!channel[8] && !playing_state(voice_state(emitter_word<uint32_t>(channel, 0)))) S_StopChannel(channel);
            }
            if (emitter_word<int32_t>(channel, 0) == -1) continue;
            if (channel[8] || playing_state(voice_state(emitter_word<uint32_t>(channel, 0))))
                S_ReapplyChannelParams(channel);
            if (emitter_word<int32_t>(channel, 0) == -1 || channel[8]) continue;
            if (playing_state(voice_state(emitter_word<uint32_t>(channel, 0)))) continue;
            S_StopChannel(channel);
        } else {
            if (emitter_word<int32_t>(channel, 0x20) == solo) {
                if (emitter_word<int32_t>(channel, 0) == -1) continue;
                if (channel[8] || playing_state(voice_state(emitter_word<uint32_t>(channel, 0)))) {
                    S_ReapplyChannelParams(channel);
                    continue;
                }
            }
            if (emitter_word<int32_t>(channel, 0) != -1) S_StopChannel(channel);
        }
    }
}

uint32_t XB_CDECL SndSys_Update() {
    // Both guards compared as 4 dwords (REPZ CMPSD).
    const uint32_t* const guards[2] = {title_global<const uint32_t*>(0x0090a89c), title_global<const uint32_t*>(0x0090a8a0)};
    const uint32_t* const expected[2] = {&title_global<const uint32_t>(0x007ce5b8), &title_global<const uint32_t>(0x007ce5a8)};
    for (uint32_t g = 0; g < 2; ++g) {
        for (uint32_t k = 0; k < 4; ++k) {
            if (guards[g][k] != expected[g][k]) return 0;  // FUN_000cb150: a bare ret
        }
    }
    const uint32_t last = title_global<uint32_t>(0x0090a8ac);
    title_global<uint32_t>(0x0090a8a4) = title_global<uint32_t>(0x0090a8b0) ? last - title_global<uint32_t>(0x0090a8a8) : 0;
    title_global<uint32_t>(0x0090a8a8) = last;
    const uint32_t now = Snd_UpdateTime();
    const uint32_t frames = title_global<uint32_t>(0x0090a8b0) + 1;
    title_global<uint32_t>(0x0090a8ac) = now;
    title_global<uint32_t>(0x0090a8b0) = frames;
    SndBank_UpdateLoads();
    SndVoice_ApplyParams();
    SndSource_CommitDirty();
    Snd_UpdateListenerAxis();
    for (int32_t i = 0; i < title_global<int32_t>(0x008bed00); ++i) {
        uint8_t* const voice = &title_global<uint8_t>(0x00911548 + uint32_t(i) * 0xe0);
        if (!voice[0xc0]) continue;
        if (SndBank_GetSoundEntry(emitter_word<uint32_t>(voice, 0xc8))) continue;
        SndVoice_Free(emitter_word<int32_t>(voice, 0xcc));
        SndHw_ReleaseSlot(uint32_t(i));
    }
    return title_function<uint32_t XB_CDECL()>(0x000c7730)();  // SndHw_Update (tail jump)
}

// --- Channel params, level sounds, wave banks ---

void XB_REGS1 XB_CDECL S_ReapplyChannelParams(uint8_t* channel) {
    int32_t& voice = emitter_word<int32_t>(channel, 0);
    if (voice == -1) return;
    const uint32_t volume = emitter_word<uint32_t>(channel, 0x10);
    SndVoice_SetParam(voice, 0, float_bits(emitter_word<float>(channel, 0x1c) * bits_float(volume)));
    emitter_word<uint32_t>(channel, 0x10) = volume;
    const uint32_t pitch = emitter_word<uint32_t>(channel, 0xc);
    if (voice != -1) {
        SndVoice_SetParam(voice, 1, pitch);
        emitter_word<uint32_t>(channel, 0xc) = pitch;
    }
    const uint32_t far_bits = emitter_word<uint32_t>(channel, 0x18);
    const uint32_t near_bits = emitter_word<uint32_t>(channel, 0x14);
    if (voice != -1) {
        SndVoice_SetParam(voice, 0x19, near_bits);
        SndVoice_SetParam(voice, 0x1a, far_bits);
        SndVoice_SetParam(voice, 0x1c, 0x40800000);  // 4.0
        emitter_word<uint32_t>(channel, 0x14) = near_bits;
        emitter_word<uint32_t>(channel, 0x18) = far_bits;
    }
    SndVoice_SetParam(voice, 0x1c, 0x40800000);
    const uint32_t handle = emitter_word<uint32_t>(channel, 0x20);
    const uint32_t index = handle & 0xfff;
    const uint8_t* entity = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        entity = title_global<const uint8_t*>(0x0097a968 + index * 8);
    const uint8_t* const source = emitter_word<const uint8_t*>(channel, 0x28);
    uint32_t origin[3];
    if (source) {
        if (voice != -1) {
            std::memcpy(origin, source + 0x30, 12);
            SndVoice_SetOrigin(voice, origin);
        }
    } else {
        if (!entity) return;
        uint32_t position[4];
        std::memcpy(position, entity + 0x140, 16);
        if (voice != -1) {
            std::memcpy(origin, position, 12);
            SndVoice_SetOrigin(voice, origin);
        }
    }
    uint32_t velocity[4];
    std::memcpy(velocity, &title_global<uint32_t>(0x0082e110), 16);  // zero vector
    if (voice == -1) return;
    std::memcpy(origin, velocity, 12);
    SndVoice_SetVelocity(voice, origin);
}

void XB_REGS1 XB_CDECL S_FreeLevelSounds(uint8_t* system) {
    S_StopAllSounds(system);
    const uint32_t bank = emitter_word<uint32_t>(system, 0x6014);
    if (bank != 0xffffffffu) SndBank_Free(bank);
    SndArena_Reset();
    menu_z_free(emitter_word<void*>(system, 0x6000));
    emitter_word<uint32_t>(system, 0x6000) = 0;
    for (int32_t i = 0x1ff; i >= 0; --i) {
        uint8_t* const channel = system + uint32_t(i) * 0x30;
        const uint32_t handle = emitter_word<uint32_t>(channel, 0x20);
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) continue;
        uint8_t* const entity = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (!entity) continue;
        if (const uint32_t notify = emitter_word<uint32_t>(channel, 0x2c)) Scr_Notify(entity, notify);
    }
}

namespace {

// The level entity (0x0095f534 +8) gets the notify `name` ("wbk_loaded" / "wbk_unloaded").
void notify_level(uint32_t name) {
    uint8_t* const entity = emitter_word<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8);
    if (!entity) return;
    Scr_Notify(entity, Com_HashKeyNoCase(0, &title_global<const char>(name)));
}

constexpr uint32_t kWbkUnloaded = 0x00837560;  // "wbk_unloaded"
constexpr uint32_t kWbkLoaded = 0x00837570;    // "wbk_loaded"

void file_close(uint32_t file) { title_function<void XB_CDECL(uint32_t)>(0x000cac90)(file); }  // File_Close

}  // namespace

void XB_STDCALL S_CheckWaveBankNotifies(uint8_t* manager) {
    title_function<void XB_CDECL()>(0x000ca690)();  // File_PumpRequests
    SndSys_Update();
    if (manager[4]) notify_level(kWbkUnloaded);
    if (manager[5]) notify_level(kWbkLoaded);
    manager[4] = 0;
    manager[5] = 0;
    for (int32_t bank = 0; bank < emitter_word<int32_t>(manager, 8); ++bank) {
        for (uint32_t wave = 0; wave < 6; ++wave) {
            uint8_t* const slot = manager + 0x30 + uint32_t(bank) * 0x84 + wave * 4;
            uint32_t& state = emitter_word<uint32_t>(slot, 0);
            int32_t& key = emitter_word<int32_t>(slot, 0x48);
            if (key == -1) continue;
            const int32_t loading = SndBank_IsLoading(uint32_t(key));
            if (state == 1) {
                if (loading == 0) {
                    state = 2;
                } else if (loading == -1) {
                    const uint32_t file = emitter_word<uint32_t>(slot, 0x30);
                    state = 0;
                    file_close(file);
                    emitter_word<int32_t>(slot, 0x30) = -1;
                    key = -1;
                } else {
                    continue;
                }
                notify_level(kWbkLoaded);
            } else if (state == 3) {
                if (loading != -1) continue;
                state = 0;
                key = -1;
                notify_level(kWbkUnloaded);
            }
        }
    }
}

PLATFORM_ASM_ENTRY(S_UnloadSoundBank_entry, "pushl 8(%esp)\n"
                                            "pushl 8(%esp)\n"
                                            "pushl %edi\n" PLATFORM_ASM_CALL(S_UnloadSoundBank) "addl $12, %esp\n"
                                            "ret $8")

void XB_CDECL S_UnloadSoundBank(uint8_t* manager, const uint32_t* name, uint32_t no_wait) {
    for (int32_t i = 0; i < emitter_word<int32_t>(manager, 8); ++i) {
        uint8_t* const bank = manager + 0xc + uint32_t(i) * 0x84;
        uint32_t k = 0;
        while (k < 8 && emitter_word<uint32_t>(bank, k * 4) == name[k]) ++k;
        if (k < 8) continue;
        for (uint32_t wave = 0; wave < 6; ++wave) {
            if (emitter_word<uint32_t>(bank, 0x24 + wave * 4) != 2) {
                manager[4] = 1;
                continue;
            }
            SndBank_Free(emitter_word<uint32_t>(bank, 0x6c + wave * 4));
            file_close(emitter_word<uint32_t>(bank, 0x54 + wave * 4));
            emitter_word<uint32_t>(bank, 0x24 + wave * 4) = 3;
            emitter_word<int32_t>(bank, 0x54 + wave * 4) = -1;
            if (uint8_t(no_wait)) continue;
            if (SndBank_IsLoading(emitter_word<uint32_t>(bank, 0x6c + wave * 4)) < 0) continue;
            do {
                S_CheckWaveBankNotifies(manager);
            } while (SndBank_IsLoading(emitter_word<uint32_t>(bank, 0x6c + wave * 4)) >= 0);
        }
        return;
    }
    manager[4] = 1;
}

namespace {

// A bank offset made a pointer: -1 -> null.
uint32_t rebase(uint32_t offset, uint32_t base) { return offset == 0xffffffffu ? 0 : offset + base; }

}  // namespace

void XB_CDECL SndBank_Relocate(uint8_t* request) {
    const uint32_t scale_enabled = title_global<uint32_t>(0x008bed10);
    const float scale = title_global<float>(0x008bed0c);
    uint8_t* const bank = emitter_word<uint8_t*>(request, 0x18);
    const uint32_t base = uint32_t(uintptr_t(bank));
    emitter_word<uint32_t>(bank, 0x80) = emitter_word<uint32_t>(request, 0x10);
    emitter_word<uint32_t>(bank, 0x84) = emitter_word<uint32_t>(request, 0x14);
    emitter_word<uint32_t>(bank, 0x88) = emitter_word<uint32_t>(request, 0x20);
    bank[0x8c] = uint8_t(~(emitter_word<uint32_t>(request, 4) >> 1) & 1);
    emitter_word<uint32_t>(bank, 0x44) = rebase(emitter_word<uint32_t>(bank, 0x44), base);
    emitter_word<uint32_t>(bank, 0x54) = rebase(emitter_word<uint32_t>(bank, 0x54), base);
    emitter_word<uint32_t>(bank, 0x5c) = rebase(emitter_word<uint32_t>(bank, 0x5c), base);
    emitter_word<uint32_t>(bank, 0x64) = rebase(emitter_word<uint32_t>(bank, 0x64), base);
    const uint32_t data = emitter_word<uint32_t>(bank, 0x84);
    const uint32_t a = emitter_word<uint32_t>(bank, 0x18) + data;
    const uint32_t b = emitter_word<uint32_t>(bank, 0x10) + data;
    emitter_word<uint32_t>(bank, 0x18) = a;
    emitter_word<uint32_t>(bank, 0x10) = b;
    uint32_t previous = 0;
    for (int32_t i = 0; i < emitter_word<int32_t>(bank, 0x40); ++i) {
        uint8_t* const entry = emitter_word<uint8_t*>(bank, 0x44) + uint32_t(i) * 0x28;
        emitter_word<uint32_t>(entry, 0x24) = emitter_word<uint32_t>(bank, 0x80);
        uint32_t group = 0;
        if (const uint32_t names = emitter_word<uint32_t>(bank, 0x64)) {
            const uint32_t name = emitter_word<uint32_t>(entry, 0x10);
            if (name != 0xffffffffu && names + name != 0)
                group = uint32_t(uintptr_t(SndGroup_FindByName(reinterpret_cast<const char*>(uintptr_t{names + name}))));
        }
        emitter_word<uint32_t>(entry, 0x10) = group;
        const uint32_t names = emitter_word<uint32_t>(bank, 0x64);
        emitter_word<uint32_t>(entry, 0) =
            names && emitter_word<uint32_t>(entry, 0) != 0xffffffffu ? names + emitter_word<uint32_t>(entry, 0) : 0;
        const uint32_t blocks = emitter_word<uint32_t>(bank, 0x54);
        const uint32_t params =
            blocks && emitter_word<uint32_t>(entry, 0x14) != 0xffffffffu ? emitter_word<uint32_t>(entry, 0x14) + blocks : 0;
        emitter_word<uint32_t>(entry, 0x14) = params;
        const uint32_t extra = emitter_word<uint32_t>(bank, 0x5c);
        emitter_word<uint32_t>(entry, 0x18) =
            extra && emitter_word<uint32_t>(entry, 0x18) != 0xffffffffu ? extra + emitter_word<uint32_t>(entry, 0x18) : 0;
        const uint8_t flags = entry[5];
        emitter_word<uint32_t>(entry, 0x1c) += flags & 2 ? emitter_word<uint32_t>(bank, 0x84) : emitter_word<uint32_t>(bank, 0x88);
        if (entry[6] && (flags & 1)) entry[5] = uint8_t(flags & 0xfe);
        if (previous >= params) continue;
        if (scale_enabled && params) {
            uint32_t* const block = reinterpret_cast<uint32_t*>(uintptr_t{params});
            const int32_t at = Snd_MaskBitIndex64(block, 1, 0);
            if (at != -1) {
                float& value = *reinterpret_cast<float*>(uintptr_t{params + 8 + uint32_t(at) * 4});
                value = scale * value;
            }
        }
        previous = emitter_word<uint32_t>(entry, 0x14);
    }
}

// --- Ambient sound and music ---

namespace {

// The alias `name` started on a free channel of 0x0095f4c4 at the origin 0x0082e290 with velocity 0x0082e110 (all
// four values from the sound); its handle, 0 for an unknown name.
uint32_t start_fade_alias(const char* name) {
    uint32_t velocity[4];
    uint32_t origin[4];
    std::memcpy(velocity, &title_global<uint32_t>(0x0082e110), 16);
    std::memcpy(origin, &title_global<uint32_t>(0x0082e290), 16);
    uint8_t* const table = title_global<uint8_t*>(0x0095f4c4);
    const int32_t alias = S_FindSoundByHash(name);
    if (alias == -1) return 0;
    uint32_t handle;
    S_PlaySoundAlias(uint32_t(alias), 0, table, &handle, 0, 1, 0, uint32_t(uintptr_t(origin)),
                     uint32_t(uintptr_t(velocity)), 0xbf800000, 0xbf800000, 0xbf800000, 0xbf800000);
    return handle;
}

}  // namespace

void XB_REGS2 XB_STDCALL S_AmbientPlay(uint8_t* fade, const char* name, uint32_t time_bits) {
    if (title_global<uint32_t>(0x0096d968) || !name) return;
    uint32_t* const handle = &emitter_word<uint32_t>(fade, 4);
    if (SoundHandle_Resolve(handle)) {
        emitter_word<uint32_t>(fade, 0x1c) = 2;
        emitter_word<float>(fade, 0x14) = 0.0f;
        emitter_word<float>(fade, 0x18) = 0.0f;
    }
    *handle = start_fade_alias(name);
    emitter_word<float>(fade, 0x10) = 0.0f;
    emitter_word<uint32_t>(fade, 0x1c) = 1;
    emitter_word<uint32_t>(fade, 0x14) = time_bits;
    emitter_word<float>(fade, 0x18) = 0.0f;
    float volume;
    set_fade_volume(fade, handle, emitter_word<float>(fade, 0x10), &volume);
}

void XB_REGS1 XB_STDCALL Music_Play(uint8_t* fade, const char* name) {
    if (title_global<uint32_t>(0x0096d968) || !name) return;
    uint32_t* const handle = &emitter_word<uint32_t>(fade, 0);
    if (SoundHandle_Resolve(handle)) S_StopSoundHandle(handle);
    *handle = start_fade_alias(name);
    if (!SoundHandle_Resolve(handle)) return;
    using GetParam = long double XB_CDECL(uint32_t, uint32_t, float);
    const long double param = title_function<GetParam>(0x000c59b0)(emitter_word<uint32_t>(SoundHandle_Resolve(handle), 4),
                                                                   0, 1.0f);  // SndVoice_GetParam
    const float volume = float(param * emitter_word<float>(fade, 0xc) * emitter_word<float>(fade, 8));
    uint8_t* const channel = SoundHandle_Resolve(handle);
    const int32_t voice = emitter_word<int32_t>(channel, 0);
    if (voice == -1) return;
    SndVoice_SetParam(voice, 0, float_bits(emitter_word<float>(channel, 0x1c) * volume));
    emitter_word<float>(channel, 0x10) = volume;
}

}  // namespace game
