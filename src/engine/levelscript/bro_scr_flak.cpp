#include "bro_scr_flak.h"

#include <cstring>

#include "bro_scr_array.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

// A cdecl title call with two stack words returning on the x87 (left in ST0); *ecx_out = the ECX
// the callee leaves (a bool-field accessor called next builds a missing field's word from it).
extern "C" PLATFORM_HIDDEN long double XB_CDECL bro_flak_call2_st0_ecx(uint32_t function, uint32_t a, uint32_t b,
                                                                       uint32_t* ecx_out);
PLATFORM_ASM_ENTRY(bro_flak_call2_st0_ecx,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "call *12(%esp)\n"
                   "addl $8, %esp\n"
                   "movl 16(%esp), %edx\n"
                   "movl %ecx, (%edx)\n"
                   "ret")

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t bits_of(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

const uint32_t* origin_of(uint32_t* buffer, uint32_t ent) {
    return slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
}

ScriptKeyTable* table_of(uint32_t ent) { return BroScr_ScriptObjectOrEmpty(ent); }

uint32_t* field_slot(uint32_t ent, uint32_t key, uint32_t missing) {
    ScriptKeyTable* const table = table_of(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, key)) return value;
    return ScriptKeyTable_Set(table, key, missing);
}

uint32_t undefined_lane() { return title_global<uint32_t>(0x00922eb8); }

// Scr_AnglesToForward of the angles in `angles` into `forward` (set to the undefined lane first).
void angles_to_forward(uint32_t* forward, const uint32_t* angles) {
    forward[0] = forward[1] = forward[2] = undefined_lane();
    slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, angles);
}

// The forward vector of `gun`'s "tag_flash" (GScr_GetTagAngles, then Scr_AnglesToForward).
void flash_forward(uint32_t gun, uint32_t* forward) {
    ScriptString tag;
    ScriptString_FromCString(&tag, pointer<const char>(0x008389c0));  // "tag_flash"
    uint32_t angles[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x00922620)(gun, angles, &tag);  // GScr_GetTagAngles
    const uint32_t copy[3] = {angles[0], angles[1], angles[2]};
    ScriptString_Release(&tag);
    angles_to_forward(forward, copy);
}

// Scr_VectorNormalize(in) into `out` (set to the undefined lane first).
void normalize(uint32_t* out, const uint32_t* in) {
    out[0] = out[1] = out[2] = undefined_lane();
    slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922280)(out, in);
}

long double random_float_range(uint32_t low, uint32_t high) {
    return slot<long double XB_CDECL(uint32_t, uint32_t)>(0x00922238)(low, high);  // Scr_RandomFloatRange
}

// A flak 88 crew member's role: its string field 0x062bea65 = `role`, and the five anim words of its
// anim-set field 0xc4813666 (+0 pre-turn idle, +4 turn, +8 fire, +0x14 idle, +0x18 twitch) from the
// anim-handle globals given; each field is looked up again per write, as the title does.
inline void set_crew_anims(uint32_t ent, uint32_t role, uint32_t preturn, uint32_t turn, uint32_t fire, uint32_t idle,
                    uint32_t twitch) {
    ScriptString_AssignCString(ScrField_h062bea65(ent), pointer<const char>(role));
    static_cast<uint32_t*>(ScrField_hc4813666(ent))[0] = title_global<uint32_t>(preturn);
    static_cast<uint32_t*>(ScrField_hc4813666(ent))[1] = title_global<uint32_t>(turn);
    static_cast<uint32_t*>(ScrField_hc4813666(ent))[2] = title_global<uint32_t>(fire);
    static_cast<uint32_t*>(ScrField_hc4813666(ent))[5] = title_global<uint32_t>(idle);
    static_cast<uint32_t*>(ScrField_hc4813666(ent))[6] = title_global<uint32_t>(twitch);
}

// The anti-air sets (0x00140fc0 and its dead twin 0x001413b0): the passer keeps its anti-tank turn.
inline void crew_anims_antiair(uint32_t ent, uint32_t role) {
    switch (role) {
    case 0:
        set_crew_anims(ent, 0x007ff368, 0x008cd368, 0x008cd374, 0x008cd350, 0x008cd35c, 0x008cd380);  // "passer"
        break;
    case 1:
        set_crew_anims(ent, 0x007ff37c, 0x008cd314, 0x008cd320, 0x008cd2e4, 0x008cd2fc, 0x008cd338);  // "loader"
        break;
    case 2:
        set_crew_anims(ent, 0x007ff390, 0x008cd260, 0x008cd26c, 0x008cd230, 0x008cd248, 0x008cd284);  // "ejecter"
        break;
    case 3:
        set_crew_anims(ent, 0x00824960, 0x008cd2b4, 0x008cd2c0, 0x008cd29c, 0x008cd2a8, 0x008cd2d8);  // "leader"
        break;
    }
}

}  // namespace

float* XB_CDECL BroScr_FlakPickLeadPos(float* out, uint32_t gun, uint32_t target) {
    uint32_t buffer[3];
    const uint32_t* o = origin_of(buffer, target);
    const uint32_t target_origin[3] = {o[0], o[1], o[2]};
    const uint32_t* const a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, target);  // ScrEnt_GetAngles
    const uint32_t angles[3] = {a[0], a[1], a[2]};
    uint32_t forward[3];
    angles_to_forward(forward, angles);
    const float speed = slot<float XB_CDECL(uint32_t)>(0x00922b38)(target);
    if (speed == title_global<float>(0x00922eac)) {
        out[0] = 0.0f;
        out[2] = 0.0f;
        out[1] = 0.0f;
        return out;
    }
    const float vx = platform_mulss(as_float(forward[0]), speed);
    const float vy = platform_mulss(as_float(forward[1]), speed);
    const float vz = platform_mulss(as_float(forward[2]), speed);
    o = origin_of(buffer, gun);
    const uint32_t gun_origin[3] = {o[0], o[1], o[2]};
    const int32_t shell_speed = static_cast<int32_t>(*field_slot(gun, 0x93c3cf6c, title_global<uint32_t>(0x00922ec0)));
    const long double distance =
        slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(gun_origin, target_origin);  // Scr_Distance
    const float time = static_cast<float>(distance / static_cast<long double>(shell_speed));
    const float r1 = static_cast<float>(random_float_range(0, 0x42800000));  // 0..64
    const float r2 = static_cast<float>(random_float_range(0, 0x42800000));
    const float spread = title_global<float>(0x007e5bf0);  // 64
    const float sx = platform_mulss(r1 - spread, time);
    const float sy = platform_mulss(r2 - spread, time);
    const long double r3 = random_float_range(0, 0x42800000);
    const float sz = static_cast<float>((r3 - static_cast<long double>(spread)) * static_cast<long double>(time));
    o = origin_of(buffer, target);
    const float lx = platform_addss(platform_addss(as_float(o[0]), vx), sx);
    const float ly = platform_addss(platform_addss(as_float(o[1]), vy), sy);
    const float lz = platform_addss(platform_addss(as_float(o[2]), vz), sz);
    float* const lead = ScrField_lead_pos(gun);
    lead[0] = lx;
    lead[1] = ly;
    lead[2] = lz;
    out[0] = platform_addss(sx, vx);
    out[1] = platform_addss(sy, vy);
    out[2] = static_cast<float>(static_cast<long double>(sz) + static_cast<long double>(vz));
    return out;
}

uint8_t* XB_CDECL BroScr_FlakCanShootLow(uint8_t* out, uint32_t gun) {
    uint32_t buffer[3];
    const uint32_t* o = origin_of(buffer, gun);
    const float ox = as_float(o[0]), oy = as_float(o[1]), oz = as_float(o[2]);
    const float* const lead = ScrField_lead_pos(gun);
    const float dz = lead[2] - oz;
    const float dy = lead[1] - oy;
    const float dx = lead[0] - ox;
    const uint32_t direction[3] = {bits_of(dx), bits_of(dy), bits_of(dz)};
    uint32_t normal[3];
    normalize(normal, direction);
    const uint32_t normal_copy[3] = {normal[0], normal[1], normal[2]};
    uint32_t forward[3];
    flash_forward(gun, forward);
    const uint32_t forward_copy[3] = {forward[0], forward[1], forward[2]};
    uint32_t ecx = 0;
    const long double dot = bro_flak_call2_st0_ecx(title_global<uint32_t>(0x0092227c), address_of(normal_copy),
                                                   address_of(forward_copy), &ecx);  // Scr_VectorDot
    if (!(dot > static_cast<long double>(title_global<float>(0x008245e0)))) {  // 0.94
        *out = 0;
        return out;
    }
    if (*reinterpret_cast<const uint8_t*>(ScrField_can_shoot_low(gun, ecx))) {
        *out = 1;
        return out;
    }
    const float* const lead_again = ScrField_lead_pos(gun);
    o = origin_of(buffer, gun);
    const long double height = static_cast<long double>(lead_again[2]) - static_cast<long double>(as_float(o[2]));
    const int32_t whole = static_cast<int32_t>(static_cast<int64_t>(height));  // _ftol
    const float threshold = title_global<float>(0x008cd170);
    *out = static_cast<float>(whole) > threshold ? 1 : 0;
    return out;
}

uint32_t* XB_CDECL BroScr_FlakFindTargets(uint32_t* out, uint32_t gun) {
    DynArray group = {};
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    const ScriptString* const name = title_function<GetString>(0x00458da0)(table_of(gun), 0x46861906);  // ScriptKeyTable_GetString
    Scr_GetEntArray(address_of(name), 0x2d36ed4e, reinterpret_cast<uint8_t*>(&group), 0);
    const uint32_t* const range_a = field_slot(gun, 0x93c3cf6c, title_global<uint32_t>(0x00922ec0));
    const uint32_t* const range_b = field_slot(gun, 0x93c3cf6c, title_global<uint32_t>(0x00922ec0));
    const float range_squared = static_cast<float>(static_cast<int32_t>(*range_b * *range_a));
    uint32_t forward[3];
    flash_forward(gun, forward);
    const uint32_t forward_copy[3] = {forward[0], forward[1], forward[2]};
    float best = title_global<float>(0x0083efc8);  // -90
    uint32_t found = 0;
    const auto element = [&](uint32_t i) { return BroScr_ArrayElement(reinterpret_cast<uint32_t*>(&group), i); };
    uint32_t buffer[3];
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(group.size); ++i) {
        if (!slot<int32_t XB_CDECL(uint32_t)>(0x00922210)(*element(i))) continue;  // GScr_IsAlive
        const uint32_t* o = origin_of(buffer, *element(i));
        const uint32_t member_origin[3] = {o[0], o[1], o[2]};
        o = origin_of(buffer, gun);
        const uint32_t gun_origin[3] = {o[0], o[1], o[2]};
        const float distance_squared =
            slot<float XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(gun_origin, member_origin);  // Scr_DistanceSquared
        if (!(range_squared > distance_squared)) continue;
        o = origin_of(buffer, gun);
        const float gx = as_float(o[0]), gy = as_float(o[1]), gz = as_float(o[2]);
        o = origin_of(buffer, *element(i));
        const float vx = as_float(o[0]) - gx;
        const float vy = as_float(o[1]) - gy;
        const float vz = as_float(o[2]) - gz;
        const uint32_t direction[3] = {bits_of(vx), bits_of(vy), bits_of(vz)};
        uint32_t normal[3];
        normalize(normal, direction);
        const uint32_t normal_copy[3] = {normal[0], normal[1], normal[2]};
        const float dot = slot<float XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092227c)(forward_copy, normal_copy);  // Scr_VectorDot
        if (!(dot > best)) continue;
        if (*reinterpret_cast<const uint8_t*>(ScrField_can_shoot_low(gun, gun))) {
            found = *element(i);
            best = dot;
            continue;
        }
        const uint32_t* const member = element(i);
        o = origin_of(buffer, gun);
        const float gun_z = as_float(o[2]);
        uint32_t member_buffer[3];
        const float height = as_float(ScrCall_ScrEnt_GetOrigin(member, member_buffer)[2]) - gun_z;
        const int32_t whole = static_cast<int32_t>(height);  // cvttss2si
        if (!(static_cast<float>(whole) > title_global<float>(0x008cd170))) continue;
        using At = uint32_t* XB_THISCALL(DynArray*, uint32_t);
        found = *title_function<At>(0x0045cb70)(&group, i);  // DynArray_At
        best = dot;
    }
    *out = found;
    if (group.data) Scr_Free(group.data);
    return out;
}

void XB_CDECL BroScr_RandomRotateStep(uint32_t ent, float step) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    bool defined;
    if (!*ScrField_pitch_IsDefined(&defined, ent)) {
        uint32_t buffer[3];
        const uint32_t* const angles = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
        const uint32_t pitch = angles[0];
        *ScrField_pitch(ent) = pitch;
        slot<float XB_CDECL(uint32_t)>(0x00922230)(0x40a00000);  // Scr_RandomFloat(5), result unused
    }
    const float limit = title_global<float>(0x0083ee48);  // 80
    const float current = as_float(*field_slot(ent, 0x07355c08, title_global<uint32_t>(0x00922eac)));
    if (!(limit > current)) return;
    const float delta = platform_mulss(step, title_global<float>(0x0083ec84));  // 40
    const float pitch = as_float(*ScrField_pitch(ent));
    *ScrField_pitch(ent) = bits_of(platform_addss(delta, pitch));
    if (as_float(*ScrField_pitch(ent)) > limit) *ScrField_pitch(ent) = bits_of(limit);
}

void XB_CDECL BroScr_Flak88CrewAnimsAntitank(uint32_t ent, uint32_t role) {
    switch (role) {
    case 0:
        set_crew_anims(ent, 0x007ff368, 0x008cd368, 0x008cd374, 0x008cd350, 0x008cd35c, 0x008cd380);  // "passer"
        break;
    case 1:
        set_crew_anims(ent, 0x007ff37c, 0x008cd314, 0x008cd32c, 0x008cd2f0, 0x008cd308, 0x008cd344);  // "loader"
        break;
    case 2:
        set_crew_anims(ent, 0x007ff390, 0x008cd260, 0x008cd278, 0x008cd23c, 0x008cd254, 0x008cd290);  // "ejecter"
        break;
    case 3:
        set_crew_anims(ent, 0x00824960, 0x008cd2b4, 0x008cd2cc, 0x008cd29c, 0x008cd2a8, 0x008cd2d8);  // "leader"
        break;
    }
}

void XB_CDECL BroScr_Flak88CrewAnimsAntiair(uint32_t ent, uint32_t role) { crew_anims_antiair(ent, role); }

void XB_CDECL BroScr_Flak88CrewAnimsAntiair_2(uint32_t ent, uint32_t role) { crew_anims_antiair(ent, role); }

}  // namespace game
