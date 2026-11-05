#include "bro_scr_level_info.h"

#include "bro_scr_array.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

void call_ent(uint32_t interface_slot, uint32_t ent) { slot<void XB_CDECL(uint32_t)>(interface_slot)(ent); }
uint32_t spawnflags(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(ent); }  // ScrEnt_GetSpawnflags

using Getter = ScriptString* XB_THISCALL(const uint32_t*, ScriptString*);

bool string_is(Getter* get, const uint32_t* ent, uint32_t text) {
    ScriptString value;
    const bool equal = ScriptString_Equals(get(ent, &value), pointer<const char>(text));
    ScriptString_Release(&value);
    return equal;
}

const uint32_t kDelete = 0x00922540, kSetFlag400 = 0x00922568, kClearFlag400 = 0x00922564,
               kNotSolid = 0x009227c0, kSolid = 0x009227bc, kConnectPaths = 0x00922574;
const uint32_t kExploder = 0x008246fc, kChunk = 0x008246e0;  // "exploder", "exploderchunk"

}  // namespace

void XB_CDECL BroScr_SetupExploders(uint32_t ent) {
    const uint32_t number =
        slot<uint32_t XB_CDECL(uint32_t)>(0x009223c0)(*ScrField_script_exploder(ent));  // GScr_GetIntMapValue
    if (number != title_global<uint32_t>(0x00922ec0) && number != 0) {
        if (string_is(&ScrCall_ScrEnt_GetClassname, &ent, 0x00824708)) {  // "item_health"
            call_ent(kDelete, ent);
            return;
        }
        if (string_is(&ScrCall_ScrEnt_GetTargetname, &ent, kExploder)) {
            call_ent(kClearFlag400, ent);
            bool not_solid = false;
            ScriptString noteworthy;
            if (ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep) {
                ScriptString again;
                not_solid = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &again),
                                                pointer<const char>(0x008246f0));  // "notsolid"
                ScriptString_Release(&again);
            }
            ScriptString_Release(&noteworthy);
            call_ent(not_solid ? kNotSolid : kSolid, ent);
            if (spawnflags(ent) & 1) call_ent(kConnectPaths, ent);
            return;
        }
        bool chunk;
        {
            ScriptString first;
            chunk = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&ent, &first), pointer<const char>(kChunk));
            if (!chunk) {
                ScriptString second;
                chunk = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&ent, &second),
                                            pointer<const char>(0x008246c8));  // "exploderchunk visible"
                ScriptString_Release(&second);
            }
            ScriptString_Release(&first);
        }
        if (chunk) {
            ScriptString target;
            const bool has_target = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
            ScriptString_Release(&target);
            if (has_target) {
                uint32_t found;
                const ScriptString* const name = ScrCall_ScrEnt_GetTarget(&ent, &target);
                const uint32_t piece = *ScrCall_GScr_GetEntArray(
                    &found, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)), 0x0d786b4c, 0);
                found = piece;
                ScriptString_Release(&target);
                if (ScrCall_Scr_EntityHandleIsValid(&found) & 0xff) call_ent(kDelete, piece);
            }
            call_ent(kDelete, ent);
            return;
        }
        bool defined;
        if (*ScrField_script_fxid_IsDefined(&defined, ent)) return;
        if (spawnflags(ent) & 1) call_ent(kConnectPaths, ent);
        call_ent(kDelete, ent);
        return;
    }
    ScriptString targetname;
    ScrCall_ScrEnt_GetTargetname(&ent, &targetname);
    bool fx_model;
    {
        ScriptString model;
        fx_model = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x008246bc)) &&  // "xmodel/fx"
                   (!targetname.rep || !ScriptString_Equals(&targetname, pointer<const char>(kChunk)));
        ScriptString_Release(&model);
    }
    if (fx_model) {
        call_ent(kSetFlag400, ent);
    } else if (ScriptString_Equals(&targetname, pointer<const char>(kExploder)) ||
               ScriptString_Equals(&targetname, pointer<const char>(kChunk))) {
        call_ent(kSetFlag400, ent);
        call_ent(kNotSolid, ent);
        if (spawnflags(ent) & 1) call_ent(kConnectPaths, ent);
    }
    const auto list = []() { return pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x78); };
    bool stored = false;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list()[2]); ++i) {
        if (slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(*BroScr_ArrayElement(list(), i))) continue;  // Scr_EntityHandleIsValid
        *BroScr_ArrayElement(list(), i) = ent;
        stored = true;
        break;
    }
    if (!stored) DynArray_PushBack(reinterpret_cast<uint8_t*>(list()), &ent);
    ScriptString_Release(&targetname);
}

namespace {

// One level's start-up loadout in BroScr_SetupLevelInfo (the compiled per-level if/else chain as a
// table, in the original's compare order). Weapons are given in order; `to_level_ent` is the index of
// the one given to the level entity at +0x7f8 instead of the player (gela's mg30cal), or -1.
struct LevelLoadout {
    uint32_t name;
    bool skip;        // credits: nothing else happens
    bool levelshot;   // sets cg_deadscreen_levelname / cg_victoryscreen_levelname
    bool done;        // sets the level byte +0x7f4
    uint32_t give[3];
    int8_t to_level_ent;
    uint32_t switch_to;
};

const uint32_t kGarand = 0x007f88c0, kThompson = 0x007f88cc, kFrag = 0x00839978, kMg30 = 0x0080d0d4,
               kCarbine = 0x0081f5b0, kBar = 0x0081f5bc, kSniper = 0x008244f4, kBazooka = 0x0081a91c;

const LevelLoadout kLoadouts[] = {
    {0x0082453c, false, true, false, {}, -1, 0},                                         // testmap
    {0x008325d0, false, true, true, {kThompson, kGarand, kFrag}, -1, kThompson},         // prologue
    {0x0082bc60, false, true, true, {}, -1, 0},                                          // oran
    {0x0082bc58, false, true, true, {}, -1, 0},                                          // tunisia
    {0x0082bc48, false, true, true, {kGarand, kFrag}, -1, kGarand},                      // kasserine_pass
    {0x0082bc3c, false, true, true, {kGarand, kFrag}, -1, kGarand},                      // kasserine
    {0x0082cc0c, false, true, true, {}, -1, 0},                                          // liberator
    {0x0082bc34, false, true, true, {kGarand, kFrag, kMg30}, 2, kMg30},                  // gela
    {0x0082bc28, false, true, true, {kCarbine, kFrag}, -1, kCarbine},                    // piano_lupo
    {0x0082bc20, false, true, true, {kThompson, kGarand, kFrag}, -1, kThompson},         // troina
    {0x0082bcc0, false, true, true, {}, -1, 0},                                          // omaha_beach
    {0x0082bcb8, false, true, true, {kGarand, kFrag, kThompson}, -1, kGarand},           // mons1
    {0x0082bcac, false, true, true, {kBar, kFrag, kCarbine}, -1, kBar},                  // eilendorf
    {0x0082bca0, false, true, true, {kThompson, kFrag, kCarbine}, -1, kThompson},        // buchholz
    {0x0082bc90, false, true, true, {kThompson, kFrag, kCarbine}, -1, kThompson},        // dragons_teeth
    {0x00824504, false, false, false, {kSniper, kBazooka, kFrag}, -1, kSniper},          // james1
    {0x0082c528, true, false, false, {}, -1, 0},                                         // credits
};
const LevelLoadout kDefaultLoadout = {0, false, false, false, {kGarand, kFrag}, -1, kGarand};

uint8_t* level() { return pointer<uint8_t>(title_global<uint32_t>(0x00922ee0)); }
uint32_t player() { return title_global<uint32_t>(0x00922eec); }

// Builds the weapon name, then calls the slot on the player (or the level entity at +0x7f8).
void weapon_call(uint32_t interface_slot, uint32_t weapon, bool level_ent = false) {
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(weapon));
    const uint32_t ent = level_ent ? *reinterpret_cast<uint32_t*>(level() + 0x7f8) : player();
    slot<void XB_CDECL(uint32_t, ScriptString*)>(interface_slot)(ent, &name);
    ScriptString_Release(&name);
}

}  // namespace

void XB_CDECL BroScr_SetupLevelInfo(uint32_t /*passed_on*/) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(0x00824544, 0x0080da58);  // GScr_SetCvar(cg_victoryscreen_menu, "off")
    level()[0x7f4] = 0;
    ScriptString* const campaign = reinterpret_cast<ScriptString*>(level() + 0x64);
    if (!campaign->rep) ScriptString_AssignCString(campaign, pointer<const char>(0x008236b8));  // "american"
    const LevelLoadout* loadout = &kDefaultLoadout;
    for (const LevelLoadout& row : kLoadouts) {
        if (ScriptString_Equals(reinterpret_cast<ScriptString*>(level() + 0x1c4), pointer<const char>(row.name))) {
            loadout = &row;
            break;
        }
    }
    if (loadout->skip) return;
    if (loadout->levelshot) {
        const uint32_t kLevelshot = 0x0082450c;  // "levelshots/level_name_text/hud@bastogne1.tga"
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(0x00838e50, kLevelshot);  // cg_deadscreen_levelname
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(0x00838e18, kLevelshot);  // cg_victoryscreen_levelname
    }
    BroScr_SetAmericanScreens();  // the original passes its own argument on; the callee ignores it
    ScriptString_AssignCString(reinterpret_cast<ScriptString*>(level() + 0x64), pointer<const char>(0x008236b8));
    slot<void XB_CDECL(uint32_t, float)>(0x00922554)(player(), 1.0f);  // GScr_SetNormalHealth
    for (int i = 0; i < 3 && loadout->give[i]; ++i)
        weapon_call(0x009227c4, loadout->give[i], i == loadout->to_level_ent);  // GScr_PlayerGiveWeapon
    if (loadout->switch_to) weapon_call(0x009227e0, loadout->switch_to);  // GScr_PlayerSwitchToWeapon
    if (loadout->done) level()[0x7f4] = 1;
}

void XB_CDECL BroScr_InitWeaponNames() {
    // m1carbine, m1garand, thompson, bar, mg30cal, springfield, colt, kar98, gewehr43, mp40, mp44, mg34,
    // kar98_sniper, p38, beretta34, beretta_smg, breda_lmg, mas36, mas1935, mas_smg, carcano,
    // chatellerault, fraggrenade, stielhandgranate
    static const uint32_t kNames[24] = {
        0x0081f5b0, 0x007f88c0, 0x007f88cc, 0x0081f5bc, 0x0080d0d4, 0x00824818, 0x00824810, 0x00824808,
        0x008247fc, 0x008247f4, 0x008247ec, 0x008247e4, 0x008244f4, 0x00800b00, 0x008247d8, 0x008247cc,
        0x008247c0, 0x008247b8, 0x008247b0, 0x008247a8, 0x008247a0, 0x00824790, 0x00839978, 0x0082466c,
    };
    using Reserve = void XB_THISCALL(ScrStringArray*, uint32_t, uint32_t);
    Reserve* const reserve = title_function<Reserve>(0x0045c2e0);  // DynArray_ReserveString
    ScrStringArray names = {};
    for (uint32_t k = 0; k < 24; ++k) {
        if (names.count <= k) reserve(&names, k + 1, k + 4);
        ScriptString_AssignCString(&names.data[k], pointer<const char>(kNames[k]));
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(names.count); ++i) {
        if (i >= names.count) reserve(&names, i + 1, i + 4);
        using HasWeapon = bool XB_CDECL(uint32_t, const ScriptString*);
        if (!slot<HasWeapon>(0x009227d8)(player(), &names.data[i])) continue;  // GScr_PlayerHasWeapon
        if (i >= names.count) reserve(&names, i + 1, i + 4);
        slot<void XB_CDECL(uint32_t, const ScriptString*)>(0x009227e8)(player(), &names.data[i]);  // GScr_PlayerGiveMaxAmmoByAmmoType
    }
    if (names.data) {
        for (ScriptString* it = names.data; it != names.data + names.count; ++it) ScriptString_Release(it);
        Scr_Free(names.data);
    }
}

}  // namespace game
