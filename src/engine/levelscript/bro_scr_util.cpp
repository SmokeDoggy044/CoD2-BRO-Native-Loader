#include "bro_scr_util.h"

#include <cstring>

#include "g_scr_runtime.h"
#include "g_script.h"
#include "scr_field.h"
#include "title_data.h"
#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_wait_notify.h"
#include "script_string.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A script-interface slot (pScr_*: a word in the import block at 0x00922xxx) called as F.
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn

ScriptStringRep* release(uint32_t* word) { return ScriptString_Release(reinterpret_cast<ScriptString*>(word)); }

void free_block(uint32_t block) {
    if (block) Scr_Free(pointer<void>(block));
}

void set_cvar(uint32_t name, uint32_t value) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(name, value); }  // GScr_SetCvar

// Whether the cvar's value equals the C string (a temporary script string, released).
bool cvar_equals(uint32_t name, uint32_t text) {
    ScriptString value;
    const bool equal = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, name), pointer<const char>(text));
    ScriptString_Release(&value);
    return equal;
}

// The result block of GScr_LocationalTraceSurface (word 0 not set by the callers here).
struct TraceSurface {
    uint32_t unknown0;
    uint32_t position[3];
    uint32_t hit;
    uint32_t normal[3];
    ScriptString surface;
};

// Traces from `start` to `end` (skipping `skip`) and copies the hit position to `out`.
uint32_t* trace_ground(uint32_t* out, const ScrVec3* start, const float* end, uint32_t skip) {
    TraceSurface trace{};
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);
    for (int i = 0; i < 3; ++i) trace.position[i] = undefined;
    trace.hit = 0;
    for (int i = 0; i < 3; ++i) trace.normal[i] = undefined;
    ScriptString_FromRep(&trace.surface, nullptr);
    slot<void XB_CDECL(TraceSurface*, const ScrVec3*, const float*, uint32_t, uint32_t)>(0x00922414)(&trace, start, end, 0,
                                                                                                    skip);
    out[0] = trace.position[0];
    out[1] = trace.position[1];
    out[2] = trace.position[2];
    ScriptString_Release(&trace.surface);
    return out;
}

void queue_notify(uint32_t ent, uint32_t name) {
    uint32_t hash;
    const uint32_t value = *Scr_HashName(&hash, pointer<const char>(name));
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, value);  // Scr_QueueNotify
}

int32_t random_int(int32_t limit) { return slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(limit); }  // Scr_RandomInt
// Scr_RandomFloat: the result in ST0, used at the x87's precision.
long double random_float(uint32_t limit_bits) { return slot<long double XB_CDECL(uint32_t)>(0x00922230)(limit_bits); }

// By reference: a float returned by value goes through the x87 (FLD quiets a signalling NaN).
const float& bits_float(const uint32_t& bits) { return *reinterpret_cast<const float*>(&bits); }
uint32_t float_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

ScriptKeyTable* script_object_or_empty(uint32_t ent) {
    const uint32_t object = title_function<uint32_t XB_CDECL(uint32_t)>(0x0011db20)(ent);  // GScr_GetEntScriptObject
    if (object) return pointer<ScriptKeyTable>(object);
    return &title_global<ScriptKeyTable>(0x00922e94);
}

bool handle_field_defined(bool* XB_CDECL (*is_defined)(bool*, uint32_t), uint32_t ent) {
    bool defined;
    is_defined(&defined, ent);
    return defined;
}

void delete_attached(uint32_t ent, uint32_t gun) {
    if (!handle_field_defined(ScrField_h4225d5c5_IsDefined, ent)) return;
    slot<void XB_CDECL(uint32_t)>(0x00922540)(*BroScr_FieldHandleSlot(ent, 0x4225d5c5));  // GScr_Delete
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d94)(ent, 1);                           // ScrActor_SetInt_1378
    own_function<void XB_CDECL(uint32_t, uint32_t)>(&BroScr_PutGunInHand)(ent, gun);               // BroScr_PutGunInHand
}

// The cleared turret fields: 0, or the word at 0x00922e84 (re-read for each).
void delete_turrets(uint32_t ent, bool global_value) {
    if (handle_field_defined(ScrField_leftturret_IsDefined, ent))
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*BroScr_FieldHandleSlot(ent, 0xef6522f9));  // GScr_Delete
    if (handle_field_defined(ScrField_rightturret_IsDefined, ent))
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*BroScr_FieldHandleSlot(ent, 0xeead8089));
    uint32_t* left = BroScr_FieldHandleSlot(ent, 0xef6522f9);
    *left = global_value ? title_global<uint32_t>(0x00922e84) : 0;
    uint32_t* right = BroScr_FieldHandleSlot(ent, 0xeead8089);
    *right = global_value ? title_global<uint32_t>(0x00922e84) : 0;
}

void plane_crash_sound(uint32_t ent, uint32_t thread) {
    using GetOrigin = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    uint32_t buffer[3];
    const uint32_t* o = slot<GetOrigin>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    uint32_t origin[3] = {o[0], o[1], o[2]};
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    ScriptString* const sound = title_function<GetString>(0x00458da0)(script_object_or_empty(ent), 0x22282ca0);  // ScriptKeyTable_GetString
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t*, uint32_t, uint32_t)>(0x009221e0)(ent, sound, origin, 0,
                                                                                           0);  // GScr_PlaySoundAtPosition
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(0x008245e8));  // "Plane_crash"
    uint32_t buffer2[3];
    o = slot<GetOrigin>(0x00922b20)(buffer2, ent);
    uint32_t origin2[3] = {o[0], o[1], o[2]};
    const uint32_t args[8] = {0, 0, 0, 0, thread, ent, address_of(&name), address_of(origin2)};
    Scr_StartThreadFunctorN(args);
    ScriptString_Release(&name);
}

ScriptString* team_field(uint32_t ent) { return ScrField_script_team(ent); }
bool equals(const ScriptString* string, uint32_t text) { return ScriptString_Equals(string, pointer<const char>(text)); }

}  // namespace

ScriptString* XB_CDECL ScriptString_FromCStringPlusVec(ScriptString* out, const char* text, ScrVec3 v) {
    ScriptString temp;
    ScriptString_FromRep(&temp, nullptr);
    title_function<void XB_CDECL(ScriptString*, const char**, ScrVec3*)>(0x00458ce0)(&temp, &text, &v);
    ScriptString_Copy(out, &temp);
    ScriptString_Release(&temp);
    return out;
}

uint8_t* XB_THISCALL BroScr_GetMap_h4fcdc42e(uint8_t* self, uint8_t* out) {
    title_function<void XB_THISCALL(uint8_t*, uint8_t*, uint32_t)>(0x0052f900)(self - 0x14, out, 0x4fcdc42e);
    return out;
}

uint32_t* XB_CDECL BroRuntime_SetImportCallbacks(uint32_t* block) {
    block[1] = own_address(&ScriptKeyTable_FromPairs);  // ScriptKeyTable_FromPairs
    block[2] = own_address(&ScriptKeyTable_Delete);  // ScriptKeyTable_Delete
    block[3] = 0x0011e800;
    block[4] = own_address(&ScriptKeyTable_Clone);  // ScriptKeyTable_Clone
    return block;
}

uint8_t* XB_THISCALL BroScr_SpawnerGroupParams_Init(uint8_t* self) {
    // Each word in the original's order: the undefined float (F), vector lane (V), int (I), handle (H),
    // a null script string (S) or 0 (Z).
    static const char kKinds[] = "FFVVVVVVVVVFFFIIIIFFIVVVIHIFFFIIISHIFZIIZIIIIIISSSSHIIII";
    for (uint32_t i = 0; kKinds[i]; ++i) {
        uint32_t* const word = reinterpret_cast<uint32_t*>(self + i * 4);
        switch (kKinds[i]) {
            case 'F': *word = title_global<uint32_t>(0x00922eac); break;
            case 'V': *word = title_global<uint32_t>(0x00922eb8); break;
            case 'I': *word = title_global<uint32_t>(0x00922ec0); break;
            case 'H': *word = title_global<uint32_t>(0x00922eb4); break;
            case 'S': ScriptString_FromRep(reinterpret_cast<ScriptString*>(word), nullptr); break;
            default: *word = 0; break;
        }
    }
    return self;
}

uint8_t* XB_CDECL BroScr_IsVecAllMinusOne(uint8_t* out, const int32_t* v) {
    *out = v[0] == -1 && v[1] == -1 && v[2] == -1;
    return out;
}

void XB_CDECL BroScr_MoveToThread(uint32_t ent, ScrVec3 pos, uint32_t time) {
    end_on(ent, 0xf9ed2681);
    slot<void XB_CDECL(uint32_t, ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x00922794)(ent, &pos, time, 0, 0);  // GScr_MoveTo
}

void XB_CDECL BroScr_RotateToFaceDirection(uint32_t ent, ScrVec3 from, ScrVec3 to, uint32_t time) {
    end_on(ent, 0xf9ed2681);
    float a[3], b[3], dir[3];
    std::memcpy(a, from.v, 12);
    std::memcpy(b, to.v, 12);
    dir[0] = b[0] - a[0];
    dir[1] = b[1] - a[1];
    dir[2] = b[2] - a[2];
    // The angles go to the `to` slot (preset to the undefined lane), then are copied over `from`.
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);
    to.v[0] = to.v[1] = to.v[2] = undefined;
    slot<void XB_CDECL(ScrVec3*, float*)>(0x00922284)(&to, dir);  // Scr_VectorToAngles
    from = to;
    using GetAngles = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    uint32_t buffer1[3], buffer2[3];
    const uint32_t* current = slot<GetAngles>(0x00922ba0)(buffer1, ent);  // ScrEnt_GetAngles
    const uint32_t roll = current[2];
    current = slot<GetAngles>(0x00922ba0)(buffer2, ent);
    ScrVec3 angles = {{current[0], from.v[1], roll}};
    slot<void XB_CDECL(uint32_t, ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x009227a8)(ent, &angles, time, 0, 0);  // GScr_RotateTo
}

BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncStrArg_11)
uint32_t XB_CDECL BroScr_EmptyFuncStrArg_11(uint32_t* args) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(release(&args[4]))); }
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncStrArg_12)
uint32_t XB_CDECL BroScr_EmptyFuncStrArg_12(uint32_t* args) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(release(&args[4]))); }
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncStrArg_13)
uint32_t XB_CDECL BroScr_EmptyFuncStrArg_13(uint32_t* args) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(release(&args[4]))); }
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncStrArg)
uint32_t XB_CDECL BroScr_EmptyFuncStrArg(uint32_t* args) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(release(&args[4]))); }
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncStrArg_2)
uint32_t XB_CDECL BroScr_EmptyFuncStrArg_2(uint32_t* args) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(release(&args[10]))); }
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncStrArg_3)
uint32_t XB_CDECL BroScr_EmptyFuncStrArg_3(uint32_t* args) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(release(&args[12]))); }

BRO_SCR_ARGS_ENTRY(BroScr_EmptyFunc2StrArgs)
uint32_t XB_CDECL BroScr_EmptyFunc2StrArgs(uint32_t* args) {
    release(&args[4]);
    release(&args[8]);
    return 0;
}
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFunc2StrArgs_2)
uint32_t XB_CDECL BroScr_EmptyFunc2StrArgs_2(uint32_t* args) {
    release(&args[4]);
    release(&args[11]);
    return 0;
}

BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncArrStrArgs)
uint32_t XB_CDECL BroScr_EmptyFuncArrStrArgs(uint32_t* args) {
    free_block(args[1]);
    release(&args[4]);
    return 0;
}
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncArrStrArgs_2)
uint32_t XB_CDECL BroScr_EmptyFuncArrStrArgs_2(uint32_t* args) {
    free_block(args[1]);
    release(&args[7]);
    return 0;
}
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncArrStrArgs_3)
uint32_t XB_CDECL BroScr_EmptyFuncArrStrArgs_3(uint32_t* args) {
    free_block(args[1]);
    release(&args[10]);
    return 0;
}
BRO_SCR_ARGS_ENTRY(BroScr_EmptyFuncArrArg)
uint32_t XB_CDECL BroScr_EmptyFuncArrArg(uint32_t* args) {
    free_block(args[1]);
    return 0;
}

void XB_CDECL BroScr_SetFlak88AnimsByType(uint32_t, ScriptString type, uint32_t* out) {
    if (ScriptString_Equals(&type, pointer<const char>(0x00823544))) {  // "flaktank"
        out[0] = title_global<uint32_t>(0x008cd470);
        out[1] = title_global<uint32_t>(0x008cd488);
        out[2] = title_global<uint32_t>(0x008cd434);
        out[5] = title_global<uint32_t>(0x008cd458);
        out[6] = title_global<uint32_t>(0x008cd4a0);
    }
    if (ScriptString_Equals(&type, pointer<const char>(0x00823538))) {  // "flakairlow"
        out[2] = title_global<uint32_t>(0x008cd428);
        out[5] = title_global<uint32_t>(0x008cd44c);
    } else {
        out[0] = title_global<uint32_t>(0x008cd464);
        out[1] = title_global<uint32_t>(0x008cd47c);
        out[2] = title_global<uint32_t>(0x008cd41c);
        out[5] = title_global<uint32_t>(0x008cd440);
        out[6] = title_global<uint32_t>(0x008cd494);
    }
    ScriptString_Release(&type);
}

void XB_CDECL BroScr_VehicleSetUnusable(uint32_t vehicle, uint32_t unusable) {
    if (static_cast<uint8_t>(unusable))
        slot<void XB_CDECL(uint32_t)>(0x009228b4)(vehicle);  // GScr_Vehicle_MakeUnusable
    else
        slot<void XB_CDECL(uint32_t)>(0x009228b8)(vehicle);  // GScr_Vehicle_MakeUsable
}

void XB_CDECL BroScr_DeleteIfGameSkill3(uint32_t ent) {
    if (cvar_equals(0x00832fec, 0x008398e0))                 // "g_gameskill" == "3"
        slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL BroScr_InitLevelCvars() {
    static const uint32_t kDefaults[][2] = {
        {0x00837c3c, 0x0082e058}, {0x008235b0, 0x0082e058}, {0x008235a4, 0x0082e058}, {0x0082358c, 0x0082e058},
        {0x00823580, 0x00832c8c}, {0x0082356c, 0x0082357c}, {0x008351cc, 0x0082e098}, {0x008392b8, 0x008392b4},
        {0x0083b60c, 0x0083b608}, {0x0083c34c, 0x0083c354}, {0x0083c35c, 0x0080d8e4},
    };
    for (const auto& d : kDefaults) set_cvar(d[0], d[1]);
    slot<void XB_CDECL(uint32_t)>(0x009228e4)(1);  // GScr_SetHudVisible
    set_cvar(0x0083365c, 0x00833668);
    // Unset ("") debug cvars: 0x0082c0e0 is set to its own name, the others to 0x0082e098.
    static const uint32_t kChecks[][2] = {
        {0x0082c0e0, 0x0082c0e0}, {0x007f4ccc, 0x0082e098}, {0x00823564, 0x0082e098},
        {0x0082355c, 0x0082e098}, {0x00823550, 0x0082e098}, {0x00818b94, 0x0082e098},
    };
    for (const auto& c : kChecks)
        if (cvar_equals(c[0], 0x0082dfeb)) set_cvar(c[0], c[1]);
}

uint32_t* XB_CDECL BroScr_VectorSubtract(uint32_t* out, ScrVec3 a, ScrVec3 b) {
    for (int i = 0; i < 3; ++i) out[i] = float_bits(bits_float(a.v[i]) - bits_float(b.v[i]));
    return out;
}

uint32_t* XB_CDECL BroScr_MakeVec0Y0(uint32_t* out, ScrVec3 v) {
    out[0] = 0;
    out[1] = v.v[1];
    out[2] = 0;
    return out;
}

uint32_t* XB_CDECL BroScr_MakeVecXY0(uint32_t* out, ScrVec3 v) {
    out[0] = v.v[0];
    out[1] = v.v[1];
    out[2] = 0;
    return out;
}

uint32_t* XB_CDECL BroScr_TraceGroundPos(uint32_t* out, uint32_t, ScrVec3 start) {
    float z = bits_float(start.v[2]) + title_global<float>(0x0083eeac);  // 32
    z = z - title_global<float>(0x0083ec28);                               // 5000
    const float end[3] = {bits_float(start.v[0]), bits_float(start.v[1]), z};
    return trace_ground(out, &start, end, 0);
}

uint32_t* XB_CDECL BroScr_TraceGroundPos_2(uint32_t* out, ScrVec3 start) {
    const float z = bits_float(start.v[2]) + title_global<float>(0x0083eb9c);  // 1000
    start.v[2] = float_bits(z);
    const float end[3] = {bits_float(start.v[0]), bits_float(start.v[1]), z - title_global<float>(0x0083ecc8)};  // 2000
    return trace_ground(out, &start, end, title_global<uint32_t>(0x00922e84));
}

uint32_t* XB_CDECL BroScr_TraceGroundPos_3(uint32_t* out, uint32_t skip, ScrVec3 start) {
    float z = bits_float(start.v[2]) + title_global<float>(0x0083eeac);  // 32
    z = z - title_global<float>(0x0083ebec);                               // 500
    const float end[3] = {bits_float(start.v[0]), bits_float(start.v[1]), z};
    return trace_ground(out, &start, end, skip);
}

void XB_CDECL BroScr_DropToGround(uint32_t ent) {
    end_on(ent, 0x06730ae8);
    uint32_t buffer[3];
    const uint32_t* origin = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const ScrVec3 start = {{origin[0], origin[1], origin[2]}};
    uint32_t ground[3];
    BroScr_TraceGroundPos(ground, ent, start);
    const ScrVec3 position = {{ground[0], ground[1], ground[2]}};
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, position);  // ScrEnt_SetOrigin
}

void XB_CDECL BroScr_SetAmericanScreens() {
    set_cvar(0x00838e68, 0x008236f4);  // cg_deadscreen_backdrop
    set_cvar(0x00838e34, 0x008236c4);  // cg_victoryscreen_backdrop
    set_cvar(0x00832fb4, 0x008236b8);  // ui_campaign = "american"
    ScriptString model;
    ScriptString_FromCString(&model, pointer<const char>(0x0083b300));  // "xmodel/viewmodel_hands_us"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x00922818)(title_global<uint32_t>(0x00922eec), &model);  // GScr_PlayerSetViewModel
    ScriptString_Release(&model);
}

void XB_CDECL BroScr_SetRandomVictoryQuote() {
    static const uint32_t kQuotes[38] = {
        0x00823ce8, 0x00823cc4, 0x00823c98, 0x00823c74, 0x00823c50, 0x00823c2c, 0x00823c00, 0x00823bd8,
        0x00823bb4, 0x00823b8c, 0x00823b68, 0x00823b44, 0x00823b1c, 0x00823af8, 0x00823acc, 0x00823aa4,
        0x00823a7c, 0x00823a54, 0x00823a30, 0x00823a08, 0x008239e0, 0x008239bc, 0x00823994, 0x00823970,
        0x00823948, 0x00823920, 0x008238fc, 0x008238d4, 0x008238b0, 0x00823888, 0x00823860, 0x00823838,
        0x00823814, 0x008237ec, 0x008237c4, 0x0082379c, 0x00823774, 0x00823748,
    };
    set_cvar(0x00823738, kQuotes[random_int(38)]);  // ui_victoryquote
}

void XB_CDECL BroScr_NotifyExplode(uint32_t ent) { queue_notify(ent, 0x00824310); }

void XB_CDECL BroScr_FlakPanzerLoopSound(uint32_t ent) {
    queue_notify(ent, 0x00824330);  // "flakpanzer_loopoff"
    queue_notify(ent, 0x00824390);  // "flakpanzer_loop_faston"
}

void XB_CDECL BroScr_InitTankDebugCvars() {
    // drawcone, tankque, stundraw, tanksamples, tankall, tanknone
    static const uint32_t kCvars[6] = {0x00824400, 0x008243f0, 0x008243e0, 0x008243cc, 0x008243bc, 0x008243ac};
    for (const uint32_t name : kCvars)
        if (cvar_equals(name, 0x0082dfeb)) set_cvar(name, 0x0080da58);  // "" -> "off"
    if (!cvar_equals(0x008243bc, 0x0080da58)) {
        set_cvar(0x008243e0, 0x008243a8);  // "on"
        set_cvar(0x008243f0, 0x008243a8);
        set_cvar(0x008243cc, 0x008243a8);
        set_cvar(0x008243bc, 0x0080da58);
    }
    if (!cvar_equals(0x008243ac, 0x0080da58)) {
        set_cvar(0x008243e0, 0x0080da58);
        set_cvar(0x008243f0, 0x0080da58);
        set_cvar(0x008243cc, 0x0080da58);
        set_cvar(0x008243ac, 0x0080da58);
    }
}

void XB_CDECL BroScr_NormalizeTeamName(uint32_t ent, ScriptString team) {
    uint32_t assign = 0;
    bool done = false;
    bool defined;
    ScrField_script_team_IsDefined(&defined, ent);
    if (defined) {
        if (equals(team_field(ent), 0x0082dcd8)) {  // "friendly"
            assign = 0x00832084;            // "allies"
        } else if (equals(team_field(ent), 0x0082dcbc)) {  // "enemy"
            assign = 0x0083208c;                   // "axis"
        } else if (equals(team_field(ent), 0x0083208c) || equals(team_field(ent), 0x00832084)) {
            done = true;
        }
    }
    if (!assign && !done) {
        if (equals(&team, 0x0083208c))
            assign = 0x0083208c;
        else if (equals(&team, 0x00832084))
            assign = 0x00832084;
        else if (equals(&team, 0x00831f14))  // "neutral"
            assign = 0x00831f14;
    }
    if (assign) ScriptString_AssignCString(team_field(ent), pointer<const char>(assign));
    ScriptString_Release(&team);
}

void XB_CDECL BroScr_WoodGibLaunch(uint32_t ent, ScrVec3 from) {
    uint32_t buffer[3];
    const uint32_t* origin = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    float o[3], d[3];
    for (int i = 0; i < 3; ++i) o[i] = bits_float(origin[i]);
    for (int i = 0; i < 3; ++i) d[i] = o[i] - bits_float(from.v[i]);
    // The direction is normalized into the `from` slot (preset to the undefined lane).
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);
    from.v[0] = from.v[1] = from.v[2] = undefined;
    slot<void XB_CDECL(ScrVec3*, const float*)>(0x00922280)(&from, d);  // Scr_VectorNormalize
    float n[3];
    for (int i = 0; i < 3; ++i) n[i] = bits_float(from.v[i]);
    const float speed = static_cast<float>(random_int(200) + 150);
    float velocity[3];
    velocity[0] = speed * n[0];
    velocity[1] = n[1] * speed;
    velocity[2] = speed * n[2];
    const long double up = random_float(0x43480000) + title_global<float>(0x0083ef08);  // RandomFloat(200) + 200
    const bool right = velocity[0] > 0.0f;
    velocity[2] = static_cast<float>(up);
    const long double turn = random_float(0x451c4000) + title_global<float>(0x0083edb0);  // RandomFloat(2500) + 1500
    const float roll = right ? static_cast<float>(-turn) : static_cast<float>(turn);
    slot<void XB_CDECL(uint32_t, float, uint32_t, uint32_t, uint32_t)>(0x009227b4)(ent, roll, 0x41200000, 0,
                                                                                   0x40e00000);  // GScr_RotateRoll
    slot<void XB_CDECL(uint32_t, float*, uint32_t)>(0x009227a4)(ent, velocity, 0x41400000);  // GScr_MoverMoveGravity
}

void XB_CDECL BroScr_WoodGibInit(uint32_t ent, ScrVec3 pos, ScriptString name) {
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(ent, copy);  // ScrEnt_SetTargetname (releases its copy)
    slot<void XB_CDECL(uint32_t, ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x00922794)(ent, &pos, 0x3e4ccccd, 0x3e4ccccd,
                                                                                      0);  // GScr_MoveTo
    const float yaw = static_cast<float>(random_float(0x41f00000) - title_global<float>(0x0083ef10));  // RandomFloat(30) - 15
    random_int(10);  // both outcomes make the same call
    slot<void XB_CDECL(uint32_t, float, uint32_t, uint32_t, uint32_t)>(0x009227b0)(ent, yaw, 0x3e4ccccd, 0x3e4ccccd,
                                                                                   0);  // GScr_RotateYaw
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_SetHealthOrDefault(uint32_t ent, uint32_t health) {
    if (health == title_global<uint32_t>(0x00922ec0)) health = 0x258;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(ent, health);  // ScrEnt_SetHealth
}

void XB_CDECL BroScr_DeleteAndCountDown(uint32_t ent) {
    if (!slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(ent)) return;  // Scr_EntityHandleIsValid
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);                   // GScr_Delete
    uint8_t* const level = pointer<uint8_t>(title_global<uint32_t>(0x00922ee0));
    *reinterpret_cast<int32_t*>(level + 0x508) -= 1;
}

ScriptKeyTable* BroScr_ScriptObjectOrEmpty(uint32_t ent) { return script_object_or_empty(ent); }

uint32_t* BroScr_FieldSlot(uint32_t ent, uint32_t hash) {
    ScriptKeyTable* const table = script_object_or_empty(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, hash)) return value;
    return ScriptKeyTable_Set(table, hash, title_global<uint32_t>(0x00922ec0));
}

uint32_t* BroScr_FieldHandleSlot(uint32_t ent, uint32_t hash) {
    ScriptKeyTable* const table = script_object_or_empty(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, hash)) return value;
    const uint32_t none = 0;
    using SetValue = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*);
    return title_function<SetValue>(0x00458e10)(table, hash, &none);  // ScriptKeyTable_SetValue
}

void XB_CDECL BroScr_AnimNotetrack_Alert(uint32_t ent, uint32_t target, int32_t ms) {
    if (static_cast<uint32_t>(ms) == title_global<uint32_t>(0x00922ec0)) ms = 0x2710;
    const float time = static_cast<float>(ms);
    using LookAt = void XB_CDECL(uint32_t, uint32_t, float, uint32_t, uint32_t, uint32_t);
    own_function<LookAt>(&BroScr_LookAtEntity)(ent, target, time, 0x06279a34, 0, 0);  // BroScr_LookAtEntity
}

void XB_CDECL BroScr_AnimNotetrack_DeleteAttached(uint32_t ent) { delete_attached(ent, 0x003798b4); }
void XB_CDECL BroScr_AnimNotetrack_DeleteAttached_2(uint32_t ent) { delete_attached(ent, 0x07151174); }

void XB_CDECL BroScr_TankShellReadySetHealth(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    *BroScr_FieldSlot(ent, 0xa027aabf) = 1;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(ent, 0x258);  // ScrEnt_SetHealth
}

void XB_CDECL BroScr_SetTargetsGroupAndEnt(uint32_t ent, uint32_t target) {
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    ScriptString* const group = title_function<GetString>(0x00458da0)(script_object_or_empty(ent), 0x46861906);  // ScriptKeyTable_GetString
    ScriptString_Assign(group, &title_global<ScriptString>(0x00922e90));
    *BroScr_FieldHandleSlot(ent, 0x0d60fb3d) = target;
}

void XB_CDECL BroScr_PlaneCrashSound(uint32_t ent) { plane_crash_sound(ent, own_address(&BroScr_PlaySoundOnTempOrigin)); }
void XB_CDECL BroScr_PlaneCrashSound_2(uint32_t ent) { plane_crash_sound(ent, own_address(&BroScr_PlaySoundOnTempOrigin_2)); }
void XB_CDECL BroScr_PlaneCrashSound_3(uint32_t ent) { plane_crash_sound(ent, own_address(&BroScr_PlaySoundOnTempOrigin_3)); }

void XB_CDECL BroScr_DeleteVehicleTurrets(uint32_t ent) { delete_turrets(ent, false); }
void XB_CDECL BroScr_DeleteVehicleTurrets_2(uint32_t ent) { delete_turrets(ent, true); }
void XB_CDECL BroScr_DeleteVehicleTurrets_3(uint32_t ent) { delete_turrets(ent, true); }

void XB_CDECL BroScr_RestoreSpawnerSavedState(uint32_t ent) {
    const uint32_t value = *BroScr_FieldSlot(ent, 0x56aed3da);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b8c)(ent, value);
    const uint32_t* origin = reinterpret_cast<const uint32_t*>(ScrField_hcc045c84(ent));
    const ScrVec3 position = {{origin[0], origin[1], origin[2]}};
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, position);  // ScrEnt_SetOrigin
}

void XB_CDECL BroScr_SetFollowEnt(uint32_t ent, uint32_t target, int32_t count) {
    *BroScr_FieldHandleSlot(ent, 0xd952b5cf) = target;
    if (count < 0) count = 0;
    *BroScr_FieldSlot(ent, 0x5cd3d8b8) = static_cast<uint32_t>(count);
}

void XB_CDECL BroScr_SetLagDistance(uint32_t ent, int32_t distance) {
    if (static_cast<uint32_t>(distance) == title_global<uint32_t>(0x00922ec0)) distance = 0;
    *BroScr_FieldSlot(ent, 0x96f9883f) = static_cast<uint32_t>(distance);
    const int32_t bound = static_cast<int32_t>(*BroScr_FieldSlot(ent, 0xea4a313a));
    if (static_cast<int32_t>(*BroScr_FieldSlot(ent, 0x96f9883f)) >= bound - 1) return;
    const int32_t again = static_cast<int32_t>(*BroScr_FieldSlot(ent, 0xea4a313a));
    *BroScr_FieldSlot(ent, 0x96f9883f) = static_cast<uint32_t>(again - 1);
}

void XB_CDECL BroScr_WindowBreakSetup(uint32_t ent) {
    using Ent = void XB_CDECL(uint32_t);
    auto target_ent = [](const uint32_t* of) {
        ScriptString target;
        using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
        const uint32_t found = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(of, &target), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
        ScriptString_Release(&target);
        return found;
    };
    auto valid = [](uint32_t e) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(e) != 0; };  // Scr_EntityHandleIsValid
    ScriptString target;
    const bool no_target = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep == nullptr;
    ScriptString_Release(&target);
    if (no_target) {
        slot<Ent>(0x00922540)(ent);  // GScr_Delete
        return;
    }
    const uint32_t window = target_ent(&ent);
    if (!valid(window)) {
        slot<Ent>(0x00922540)(ent);
        return;
    }
    const uint32_t pane = target_ent(&window);
    if (!valid(pane)) {
        slot<Ent>(0x00922540)(ent);
        return;
    }
    slot<Ent>(0x00922644)(ent);     // GScr_SetEntFlag40000
    slot<Ent>(0x00922650)(window);  // GScr_SetEntFlag80000
    slot<Ent>(0x00922564)(window);  // GScr_ClearEntFlag400
    slot<Ent>(0x00922568)(pane);    // GScr_SetEntFlag400
    slot<Ent>(0x009227c0)(pane);    // GScr_MoverNotSolid
    uint32_t hash;
    const uint32_t value = *Scr_HashName(&hash, pointer<const char>(0x00824590));
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221bc)(ent, 0xe2a210cf, value);  // GScr_SetEntVar
}

void XB_CDECL BroScr_PlayShellExploSound(uint32_t ent) {
    const auto last = []() { return pointer<int32_t>(title_global<uint32_t>(0x00922ee0) + 0x310); };
    if (static_cast<uint32_t>(*last()) == title_global<uint32_t>(0x00922ec0)) *last() = -1;
    int32_t pick = 0;
    while (pick == *last()) pick = random_int(4) + 1;  // only runs when the last pick was 0
    *last() = pick;
    const uint32_t level_ent = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 4);
    bool interior = false;
    bool defined;
    if (*ScrField_ambient_IsDefined(&defined, level_ent)) {
        const uint32_t table = address_of(script_object_or_empty(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 4)));
        ScriptString ambient;
        interior = ScriptString_Equals(static_cast<ScriptString*>(ScrField_ambient_GetCopy(pointer<uint8_t>(table + 0xc), &ambient)),
                                       pointer<const char>(0x00816a4c));  // "interior"
        ScriptString_Release(&ambient);
    }
    // both branches of the interior test pick the same aliases
    (void)interior;
    static const uint32_t kAliases[4] = {0x00816a34, 0x00816a1c, 0x00816a04, 0x008169ec};  // SHELL_EXPLO_MAINIMP_1..4
    const uint32_t alias = pick == 1 ? kAliases[0] : pick == 2 ? kAliases[1] : pick == 3 ? kAliases[2] : kAliases[3];
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(alias));
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0, 0, 0);  // GScr_PlaySound
    ScriptString_Release(&sound);
}

}  // namespace game
