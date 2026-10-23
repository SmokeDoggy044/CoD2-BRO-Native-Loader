#include "snd.h"

#include "cg_ents.h"
#include "fx.h"
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

}  // namespace game
