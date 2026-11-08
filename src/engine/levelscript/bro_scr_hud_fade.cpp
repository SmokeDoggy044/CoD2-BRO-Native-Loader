#include "bro_scr_hud_fade.h"

#include <cstring>

#include "g_scr_runtime.h"
#include "scr_access.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A script-interface slot (pScr_*: a word in the import block at 0x00922xxx) called as F.
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

using Set = void XB_CDECL(uint32_t, uint32_t);       // ScrHudElem_Set* (handle, value bits)
using HudOp = void XB_CDECL(uint32_t, uint32_t);     // GScr_HudFadeOverTime (&handle, seconds bits)
using HudDestroy = void XB_CDECL(uint32_t);          // GScr_HudDestroy (&handle)
using NewHud = uint32_t* XB_CDECL(uint32_t*);        // GScr_NewHudElem (a new hud elem)
using HudSetText = void XB_CDECL(uint32_t, ScriptString*);
using HudSetShader = void XB_CDECL(uint32_t, ScriptString*, uint32_t, int32_t);

uint32_t level_object() { return title_global<uint32_t>(0x00922ee0); }
uint32_t& level_word(uint32_t offset) { return *pointer<uint32_t>(level_object() + offset); }

// The level entity's handle: the word at +4 of the level object, read through null.
uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(field));
}

void queue_notify(uint32_t ent, uint32_t notify) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, notify); }

// Whether cvar "checkpoint" is other than "0" (the cvar's string released first).
bool checkpoint_set() {
    ScriptString value;
    const bool zero = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x008353b8), pointer<const char>(0x0082e098));
    ScriptString_Release(&value);
    return !zero;
}

}  // namespace

void XB_CDECL BroScr_CheckpointFade(ScriptString line0, ScriptString line1, ScriptString line2, ScriptString line3) {
    uint32_t handle;
    level_word(0x2e0) = *slot<NewHud>(0x0092248c)(&handle);
    slot<Set>(0x00922a48)(level_word(0x2e0), 0);  // ScrHudElem_Set_04 (x)
    slot<Set>(0x00922a50)(level_word(0x2e0), 0);  // ScrHudElem_Set_08 (y)
    if (checkpoint_set()) slot<Set>(0x00922a68)(level_word(0x2e0), 0xc4424000);  // ScrHudElem_SetFloat_6c -777
    {
        ScriptString shader;
        ScriptString_FromCString(&shader, pointer<const char>(0x007f51f8));  // "black"
        slot<HudSetShader>(0x009228ec)(level_object() + 0x2e0, &shader, 0x280, 0x1e0);  // GScr_HudSetShader 640x480
        ScriptString_Release(&shader);
    }
    slot<Set>(0x00922844)(level_word(0x7f8), 1);  // GScr_PlayerSetInt_58c (the player)
    if (checkpoint_set()) {
        Scr_Wait(as_float(0x40000000));  // 2
        slot<Set>(0x00922a68)(level_word(0x2e0), 0);
        queue_notify(level_handle(), 0xcc232f0f);
        while (*pointer<uint8_t>(level_object() + 0x260)) Scr_Wait(as_float(0x3dcccccd));  // 0.1
        slot<HudOp>(0x0092290c)(level_object() + 0x2e0, 0x3fc00000);  // GScr_HudFadeOverTime 1.5
        slot<Set>(0x00922a78)(level_word(0x2e0), 0);                   // ScrHudElem_SetAlpha
        queue_notify(level_handle(), 0x5da71cc2);
        Scr_Wait(as_float(0x3f000000));  // 0.5
        slot<Set>(0x00922844)(level_word(0x7f8), 0);
        queue_notify(level_handle(), 0xd06e3679);
        Scr_Wait(as_float(0x40000000));  // 2
        slot<HudDestroy>(0x0092291c)(level_object() + 0x2e0);
    } else {
        Scr_Wait(as_float(ScriptString_Equals(pointer<const ScriptString>(level_object() + 0x1c4), pointer<const char>(0x0082bc58))
                              ? 0x40000000    // "tunisia": 2
                              : 0x3dcccccd));  // 0.1
        if (line3.rep) {
            level_word(0x384) = *slot<NewHud>(0x0092248c)(&handle);
            slot<Set>(0x00922a48)(level_word(0x384), 0x140);       // x 320
            slot<Set>(0x00922a50)(level_word(0x384), 0xc8);        // y 200
            slot<Set>(0x00922a58)(level_word(0x384), 1);           // ScrHudElem_Set_14
            slot<Set>(0x00922a60)(level_word(0x384), 1);           // ScrHudElem_Set_18
            slot<Set>(0x00922a68)(level_word(0x384), 0x3f800000);  // SetFloat_6c 1.0
            slot<Set>(0x00922a70)(level_word(0x384), 0x40000000);  // SetFloat_0c 2.0
            slot<HudSetText>(0x009228e8)(level_object() + 0x384, &line3);
            slot<Set>(0x00922a78)(level_word(0x384), 0);
            slot<HudOp>(0x0092290c)(level_object() + 0x384, 0x3f99999a);  // 1.2
            slot<Set>(0x00922a78)(level_word(0x384), 0xc8);
        }
        Scr_Wait(as_float(0x3fc00000));  // 1.5
        if (line0.rep) {
            level_word(0x31c) = *slot<NewHud>(0x0092248c)(&handle);
            slot<Set>(0x00922a48)(level_word(0x31c), 0x140);
            slot<Set>(0x00922a50)(level_word(0x31c), 0x104);  // y 260
            slot<Set>(0x00922a58)(level_word(0x31c), 1);
            slot<Set>(0x00922a60)(level_word(0x31c), 1);
            slot<Set>(0x00922a68)(level_word(0x31c), 0x3f800000);
            slot<Set>(0x00922a70)(level_word(0x31c), 0x3fc00000);  // 1.5
            slot<HudSetText>(0x009228e8)(level_object() + 0x31c, &line0);
            slot<Set>(0x00922a78)(level_word(0x31c), 0);
            slot<HudOp>(0x0092290c)(level_object() + 0x31c, 0x3f99999a);
            slot<Set>(0x00922a78)(level_word(0x31c), 0xc8);
        }
        if (line1.rep) {
            level_word(0x320) = *slot<NewHud>(0x0092248c)(&handle);
            slot<Set>(0x00922a48)(level_word(0x320), 0x140);
            slot<Set>(0x00922a50)(level_word(0x320), 0x122);  // y 290
            slot<Set>(0x00922a58)(level_word(0x320), 1);
            slot<Set>(0x00922a60)(level_word(0x320), 1);
            slot<Set>(0x00922a68)(level_word(0x320), 0x3f800000);
            slot<Set>(0x00922a70)(level_word(0x320), 0x3fc00000);
            slot<HudSetText>(0x009228e8)(level_object() + 0x320, &line1);
            slot<Set>(0x00922a78)(level_word(0x320), 0);
            slot<HudOp>(0x0092290c)(level_object() + 0x320, 0x3f99999a);
            slot<Set>(0x00922a78)(level_word(0x320), 0xc8);
        }
        if (line2.rep) {
            level_word(0x5d8) = *slot<NewHud>(0x0092248c)(&handle);
            slot<Set>(0x00922a48)(level_word(0x5d8), 0x140);
            slot<Set>(0x00922a50)(level_word(0x5d8), 0x140);  // y 320
            slot<Set>(0x00922a58)(level_word(0x5d8), 1);
            slot<Set>(0x00922a60)(level_word(0x5d8), 1);
            slot<Set>(0x00922a68)(level_word(0x5d8), 0x3f800000);
            slot<Set>(0x00922a70)(level_word(0x5d8), 0x3fc00000);
            slot<HudSetText>(0x009228e8)(level_object() + 0x5d8, &line2);
            slot<Set>(0x00922a78)(level_word(0x5d8), 0);
            slot<HudOp>(0x0092290c)(level_object() + 0x5d8, 0x3f99999a);
            slot<Set>(0x00922a78)(level_word(0x5d8), 0xc8);
        }
        Scr_Wait(as_float(0x400ccccd));  // 2.2
        const float extra = as_float(level_word(0x5ac));
        if (extra != as_float(title_global<uint32_t>(0x00922eac)) &&  // g_scrUndefinedFloat
            static_cast<float>(extra > 0.0f ? 1 : 0) != 0.0f)
            Scr_Wait(as_float(level_word(0x5ac)));
        queue_notify(level_handle(), 0xcc232f0f);
        Scr_Wait(as_float(0x3f800000));  // 1
        while (*pointer<uint8_t>(level_object() + 0x260)) Scr_Wait(as_float(0x3dcccccd));
        slot<HudOp>(0x0092290c)(level_object() + 0x2e0, 0x3fc00000);
        slot<Set>(0x00922a78)(level_word(0x2e0), 0);
        queue_notify(level_handle(), 0x5da71cc2);
        Scr_Wait(as_float(0x3f000000));
        slot<Set>(0x00922844)(level_word(0x7f8), 0);
        queue_notify(level_handle(), 0xd06e3679);
        if (line3.rep) {
            slot<HudOp>(0x0092290c)(level_object() + 0x384, 0x3f99999a);
            slot<Set>(0x00922a78)(level_word(0x384), 0);
        }
        if (line0.rep) {
            slot<HudOp>(0x0092290c)(level_object() + 0x31c, 0x3fc00000);
            slot<Set>(0x00922a78)(level_word(0x31c), 0);
        }
        if (line1.rep) {
            slot<HudOp>(0x0092290c)(level_object() + 0x320, 0x3fc00000);
            slot<Set>(0x00922a78)(level_word(0x320), 0);
        }
        if (line2.rep) {
            slot<HudOp>(0x0092290c)(level_object() + 0x5d8, 0x3fc00000);
            slot<Set>(0x00922a78)(level_word(0x5d8), 0);
        }
        Scr_Wait(as_float(0x3fcccccd));  // 1.6
        slot<HudDestroy>(0x0092291c)(level_object() + 0x2e0);
        slot<HudDestroy>(0x0092291c)(level_object() + 0x31c);
        slot<HudDestroy>(0x0092291c)(level_object() + 0x320);
        slot<HudDestroy>(0x0092291c)(level_object() + 0x5d8);
        if (level_word(0x384) != 0xffffffffu) slot<HudDestroy>(0x0092291c)(level_object() + 0x384);
    }
    ScriptString_Release(&line0);
    ScriptString_Release(&line1);
    ScriptString_Release(&line2);
    ScriptString_Release(&line3);
}

void XB_CDECL BroScr_FadeHudBlack(float total, float duration, uint32_t closing, float height) {
    Scr_WaitFrames(0);
    const bool close = static_cast<uint8_t>(closing) != 0;
    // The script's comparisons go through float truth values.
    if (duration != as_float(title_global<uint32_t>(0x00922eac)) &&  // g_scrUndefinedFloat
        static_cast<float>(duration > 0.0f ? 1 : 0) != 0.0f) {
        float elapsed = 0.0f;
        using FrameTime = long double XB_CDECL();
        if (!close) {
            while (static_cast<float>(duration > elapsed ? 1 : 0) != 0.0f) {
                elapsed = static_cast<float>(slot<FrameTime>(0x009223c8)()) + elapsed;  // GScr_GetFrameTime
                const float h = (as_float(title_global<uint32_t>(0x0083eb20)) - elapsed / total) * height;  // (1 - t) * height
                if (static_cast<float>(h > 0.0f ? 1 : 0) != 0.0f && level_word(0x79c) != 0xffffffffu) {
                    ScriptString shader;
                    ScriptString_FromCString(&shader, pointer<const char>(0x007f51f8));  // "black"
                    slot<HudSetShader>(0x009228ec)(level_object() + 0x79c, &shader, 0x280, static_cast<int32_t>(h));
                    ScriptString_Release(&shader);
                }
                slot<Set>(0x00922a50)(level_word(0x270),
                                      static_cast<uint32_t>(static_cast<int32_t>(
                                          as_float(title_global<uint32_t>(0x0083ec08)) - h)));  // y = 480 - h
                Scr_WaitFrames(0);
            }
        } else {
            while (static_cast<float>(duration > elapsed ? 1 : 0) != 0.0f) {
                elapsed = static_cast<float>(slot<FrameTime>(0x009223c8)()) + elapsed;
                const float h = height / total * elapsed;
                if (static_cast<float>(h > 0.0f ? 1 : 0) != 0.0f && level_word(0x79c) != 0xffffffffu) {
                    ScriptString shader;
                    ScriptString_FromCString(&shader, pointer<const char>(0x007f51f8));
                    slot<HudSetShader>(0x009228ec)(level_object() + 0x79c, &shader, 0x280, static_cast<int32_t>(h));
                    ScriptString_Release(&shader);
                }
                slot<Set>(0x00922a50)(level_word(0x270), static_cast<uint32_t>(static_cast<int32_t>(
                                                             as_float(title_global<uint32_t>(0x0083ec08)) - h)));
                Scr_WaitFrames(0);
            }
            return;
        }
    } else if (close) {
        return;
    }
    if (level_word(0x79c) != 0xffffffffu) slot<HudDestroy>(0x0092291c)(level_object() + 0x79c);
    if (level_word(0x270) != 0xffffffffu) slot<HudDestroy>(0x0092291c)(level_object() + 0x270);
}

void XB_CDECL BroScr_CheckpointFadeLocalized(uint32_t key0, uint32_t key1, uint32_t key2) {
    // The arguments are built in place, last first.
    ScriptString line3, line2, line1, line0;
    ScriptString_FromRep(&line3, nullptr);
    ScrCall_GScr_GetLocalizedString(&line2, key2);
    ScrCall_GScr_GetLocalizedString(&line1, key1);
    ScrCall_GScr_GetLocalizedString(&line0, key0);
    BroScr_CheckpointFade(line0, line1, line2, line3);
}

void XB_CDECL BroScr_SetIntroScreenLine(uint32_t key0, uint32_t key1, uint32_t key2, uint32_t key3) {
    ScriptString line3, line2, line1, line0;
    ScrCall_GScr_GetLocalizedString(&line3, key3);
    ScrCall_GScr_GetLocalizedString(&line2, key2);
    ScrCall_GScr_GetLocalizedString(&line1, key1);
    ScrCall_GScr_GetLocalizedString(&line0, key0);
    BroScr_CheckpointFade(line0, line1, line2, line3);
}

}  // namespace game
