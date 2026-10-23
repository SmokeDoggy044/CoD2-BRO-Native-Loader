#include "snd.h"

#include <cstring>

#include "cg_ents.h"
#include "fx.h"
#include "g_scr_runtime.h"
#include "qcommon.h"
#include "title_data.h"

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

uint32_t play_alias(const uint8_t* emitter, int32_t group) {
    using Play = uint32_t XB_CDECL(uint32_t, uint32_t, int32_t, uint32_t);
    return title_function<Play>(0x002a6990)(emitter_word<uint32_t>(emitter, 0x40), 0, group, 0);  // FXCmd_PlaySoundAlias
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
                id = play_alias(emitter, group_index);
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
                id = play_alias(emitter, group_index);
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

uint8_t* XB_CDECL SndVoice_Resolve(uint32_t handle) {
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
    const uint8_t* const entry = SndVoice_Resolve(handle);
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
    uint8_t* entry = SndVoice_Resolve(sound);
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
        uint8_t* entry = SndVoice_Resolve(snd_at<uint32_t>(v, 0xc8));
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

}  // namespace game
