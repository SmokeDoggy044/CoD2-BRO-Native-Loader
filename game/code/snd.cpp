#include "snd.h"

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

}  // namespace game
