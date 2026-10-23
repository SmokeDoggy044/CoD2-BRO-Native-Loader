#include "cg_ents.h"

#include "cg_view.h"
#include "cgame.h"
#include "dobj.h"
#include "fx.h"
#include "g_utils.h"
#include "qcommon.h"
#include "script_string.h"
#include "title_data.h"
#include "tr_main.h"

// Calls `function` on the current stack with all six registers from `regs` and `count`
// stack words (args[0] pushed last, popped again here); returns its EAX.
PLATFORM_ASM_ENTRY(cg_call_regs,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 16(%ebp), %edx\n"
                   "movl 20(%ebp), %ecx\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "pushl (%edx,%ecx,4)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 4(%eax), %ebx\n"
                   "movl 8(%eax), %ecx\n"
                   "movl 12(%eax), %edx\n"
                   "movl 16(%eax), %esi\n"
                   "movl 20(%eax), %edi\n"
                   "movl 0(%eax), %eax\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

namespace game {

namespace {

uint32_t addr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
int32_t cg_time() { return title_global<int32_t>(0x0097253c); }
float g(uint32_t address) { return title_global<float>(address); }

template <typename T>
T& at(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

int32_t title_rand() { return title_function<int32_t XB_CDECL()>(0x001efcc0)(); }

// The title CRT's __CIsqrt of an x87 value, stored as float.
float title_ci_sqrt_ext(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// CG_EntityEvent (0x00289790: event EAX, cent ECX, EBX 1 for player-state events) and
// CG_EntityPreEvent (0x0028a5e0: event EAX, cent ECX).
void entity_event(uint32_t function, uint32_t event, uint8_t* cent, uint32_t ebx) {
    TitleRegs regs{};
    regs.eax = event;
    regs.ecx = addr(cent);
    regs.ebx = ebx;
    regs.esi = addr(cent);
    cg_call_regs(function, &regs, nullptr, 0);
}

// The entity's event queue: sequence +0xb0, events +0xb4 and parms +0xb8 (4 bytes each);
// `seen` (+0x420 events, +0x424 pre-events) is how far it was processed. Event entities
// (type above 0x11) fire their type - 0x11 once.
void check_events(uint8_t* cent, uint32_t seen_offset, uint32_t function, bool keep_ebx, uint32_t ebx) {
    const uint8_t type = cent[0];
    int32_t& seen = at<int32_t>(cent, seen_offset);
    if (type > 0x11) {
        if (seen) return;
        seen = 1;
        entity_event(function, static_cast<uint32_t>(type) - 0x11, cent, keep_ebx ? ebx : 0);
        return;
    }
    const int32_t sequence = at<int32_t>(cent, 0xb0);
    if (sequence == 0) {
        seen = 0;
        return;
    }
    if (static_cast<int32_t>(static_cast<uint32_t>(sequence) - static_cast<uint32_t>(seen)) < 0) seen = 0;
    if (static_cast<int32_t>(static_cast<uint32_t>(sequence) - static_cast<uint32_t>(seen)) > 4) seen = sequence - 4;
    int32_t i = seen;
    if (i < sequence) {
        const uint8_t parm = cent[4];
        // CG_CheckEntityPreEvents keeps the saved parm in BL.
        const uint32_t call_ebx = keep_ebx ? (ebx & ~0xffu) | parm : 0;
        do {
            const uint32_t event = cent[0xb4 + (i & 3)];
            cent[4] = cent[0xb8 + (i & 3)];
            entity_event(function, event, cent, call_ebx);
            ++i;
        } while (i != at<int32_t>(cent, 0xb0));
        cent[4] = parm;
    }
    seen = at<int32_t>(cent, 0xb0);
}

}  // namespace

void XB_CDECL CG_AddLocalEntities() {
    title_global<float>(0x00972f70) = 0.0f;
    uint32_t* const sentinel = &title_global<uint32_t>(0x0095f748);
    uint32_t* le = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(sentinel[0]));
    if (le == sentinel) return;
    uint32_t* next;
    do {
        next = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(le[0]));
        if (cg_time() >= static_cast<int32_t>(le[3])) {
            // Expired (end time +0xc): unlinked onto the free list.
            if (!next) CG_Error(&title_global<char>(0x00839910));
            reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(le[0]))[1] = le[1];
            reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(le[1]))[0] = le[0];
            le[1] = title_global<uint32_t>(0x0094c9d8);
            title_global<uint32_t>(0x0094c9d8) = addr(le);
        } else {
            const int16_t type = at<int16_t>(le, 8);
            switch (type) {
            case 0: R_AddRefEntityToScene(reinterpret_cast<uint8_t*>(le) + 0x3c, -1); break;
            case 1: CG_AddNearCulledLocalEntity(le); break;
            case 2: CG_AddTracerLocalEntity(reinterpret_cast<uint8_t*>(le)); break;
            default: CG_Error(&title_global<char>(0x00839900), static_cast<int32_t>(type)); break;
            }
        }
        le = next;
    } while (next != sentinel);
}

void XB_REGS1 XB_CDECL CG_AddTracerLocalEntity(uint8_t* le) {
    float length, width;
    if (at<int16_t>(le, 0xa) == 0x20) {
        length = g(0x0083ed04);
        width = g(0x00830f60);
    } else {
        length = g(0x0094ca78);
        width = g(0x0094bb78);
        const float epsilon = g(0x0083f02c);
        if (epsilon > length) return;
        if (epsilon > width) return;
    }
    alignas(16) float origin[4];
    alignas(16) float dir[4];
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(cg_time());
    regs.edi = addr(le + 0x10);
    regs.ecx = addr(origin);
    cg_call_regs(0x002d62c0, &regs, nullptr, 0);  // BG_EvaluateTrajectory
    regs = TitleRegs{};
    regs.esi = addr(le + 0x28);
    regs.edi = addr(dir);
    cg_call_regs(0x002975c0, &regs, nullptr, 0);  // VectorNormalize2
    alignas(16) float end[4];
    end[0] = dir[0] * length + origin[0];
    end[1] = dir[1] * length + origin[1];
    end[2] = dir[2] * length + origin[2];
    CG_Tracer(0, origin, end, width);
}

void XB_REGS3 XB_CDECL CG_SpawnTracer(const float* start, uint32_t, const float* end, int32_t weapon) {
    const float s0 = start[0], s1 = start[1], s2 = start[2];
    const float e0 = end[0], e1 = end[1], e2 = end[2];
    float d0 = s0 - e0, d1 = s1 - e1, d2 = s2 - e2;
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];  // bg_weaponDefs
    const float length = title_ci_sqrt_ext(static_cast<long double>((d2 * d2 + d1 * d1) + d0 * d0));
    if (!(length == g(0x0083eafc))) {
        const float inverse = g(0x0083eb20) / length;
        d0 = inverse * d0;
        d1 = inverse * d1;
        d2 = inverse * d2;
    }
    // The tracer starts its own length short of the start, moving towards the end.
    const float back = 0.0f - g(0x0094ca78);
    const float p0 = (back * d0 + s0) - e0;
    const float p2 = (back * d2 + s2) - e2;
    const float p1 = (back * d1 + s1) - e1;
    using ext = long double;
    const float distance =
        title_ci_sqrt_ext(static_cast<ext>(p2) * static_cast<ext>(p2) + static_cast<ext>(p1) * static_cast<ext>(p1) +
                          static_cast<ext>(p0) * static_cast<ext>(p0));
    uint8_t* le = reinterpret_cast<uint8_t*>(CG_AllocLocalEntity());
    at<int16_t>(le, 8) = 2;
    const int32_t delay = title_rand() % 0x21;
    const float speed = g(0x0094a330);
    const ext travel = static_cast<ext>(distance) * static_cast<ext>(g(0x0083ec74)) / static_cast<ext>(speed);
    const int32_t start_time = delay + cg_time();
    at<int32_t>(le, 0xc) = start_time - static_cast<int32_t>(static_cast<int64_t>(travel));
    if (def) {
        const int32_t kind = *reinterpret_cast<const int32_t*>(def + 0xa8);
        if (kind == 3 || kind == 4) at<int16_t>(le, 0xa) = 0x20;
    }
    at<float>(le, 0x1c) = e0;
    at<float>(le, 0x20) = e1;
    at<float>(le, 0x24) = e2;
    at<int32_t>(le, 0x14) = start_time;
    at<int32_t>(le, 0x10) = 2;
    at<float>(le, 0x28) = g(0x0094a330) * d0;
    at<float>(le, 0x2c) = g(0x0094a330) * d1;
    at<float>(le, 0x30) = g(0x0094a330) * d2;
}

PLATFORM_ASM_ENTRY(CG_MaybeSpawnTracer_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_MaybeSpawnTracer) "addl $12, %esp\n"
                   "ret")

void XB_CDECL CG_MaybeSpawnTracer(int32_t weapon, const float* end, const float* start) {
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];  // bg_weaponDefs
    float chance = g(0x0094a8e0);
    if (def) {
        const int32_t kind = *reinterpret_cast<const int32_t*>(def + 0xa8);
        if (kind == 3 || kind == 4) chance = g(0x0083ed28);
    }
    const int32_t roll = title_rand() & 0x1f;
    if (chance * g(0x0083eeac) > static_cast<float>(roll)) CG_SpawnTracer(start, 0, end, weapon);
}

PLATFORM_ASM_ENTRY(CG_CheckPlayerstateEvents_entry,
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_CheckPlayerstateEvents) "addl $16, %esp\n"
                   "ret")

void XB_CDECL CG_CheckPlayerstateEvents(uint32_t flags, uint32_t old_flags, const int32_t* ops, const int32_t* ps) {
    // Q3's CG_CheckPlayerstateEvents: {sequence, events[4], parms[4]} of the new (EDI) and
    // old (ECX) player state; a teleport toggle (flag 4) forgets the old ones.
    int32_t old_sequence;
    int32_t old_events[4];
    if ((flags ^ old_flags) & 4) {
        old_sequence = 0;
        old_events[0] = old_events[1] = old_events[2] = old_events[3] = 0;
    } else {
        old_sequence = ops[0];
        for (int k = 0; k < 4; ++k) old_events[k] = ops[1 + k];
    }
    uint8_t* cent = *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 4);
    for (int32_t i = ps[0] - 4; i != ps[0]; ++i) {
        if (static_cast<int32_t>(static_cast<uint32_t>(i) - static_cast<uint32_t>(old_sequence)) < 0) {
            if (static_cast<int32_t>(static_cast<uint32_t>(old_sequence) - static_cast<uint32_t>(i)) >= 4) continue;
            if (ps[1 + (i & 3)] == old_events[i & 3]) continue;
        }
        const int32_t event = ps[1 + (i & 3)];
        cent[4] = static_cast<uint8_t>(ps[5 + (i & 3)]);
        entity_event(0x00289790, static_cast<uint32_t>(event), cent, 1);
        ++title_global<int32_t>(0x00972ae4);
    }
}

PLATFORM_ASM_ENTRY(CG_CheckEvents_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_CheckEvents) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_CheckEvents(uint8_t* cent) { check_events(cent, 0x420, 0x00289790, false, 0); }

PLATFORM_ASM_ENTRY(CG_CheckEntityPreEvents_entry,
                   "pushl %ebx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_CheckEntityPreEvents) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CG_CheckEntityPreEvents(uint8_t* cent, uint32_t ebx) { check_events(cent, 0x424, 0x0028a5e0, true, ebx); }

namespace {

// CG_GetEntityRefEntity (0x002be200, cent in ESI): the cent's render record, allocated on
// first use.
uint8_t* entity_ref(uint8_t* cent) {
    TitleRegs regs{};
    regs.esi = addr(cent);
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(cg_call_regs(0x002be200, &regs, nullptr, 0)));
}

// The refEntity part (+0x80 of the render record) of the model entity types: origin (+0x3c)
// and old origin (+0x48) from the cent's lerp origin (+0x60), axis (+0x14) from its angles
// (+0x70; the left axis is 0 - right), flags +4, DObj +0x54, cent +0x58, +0 = 1.
uint8_t* fill_model_ref(uint8_t* cent, uint32_t obj) {
    uint8_t* ref = entity_ref(cent) + 0x80;
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ref, 0x3c + 4 * k) = at<uint32_t>(cent, 0x60 + 4 * k);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ref, 0x48 + 4 * k) = at<uint32_t>(cent, 0x60 + 4 * k);
    float* axis = &at<float>(ref, 0x14);
    uint32_t args[2] = {addr(cent + 0x70), addr(axis)};
    float right[3];
    AnglesToAxis(args, right, axis + 6);
    for (int k = 0; k < 3; ++k) axis[3 + k] = 0.0f - right[k];
    at<uint32_t>(ref, 0x54) = obj;
    at<uint32_t>(ref, 0x58) = addr(cent);
    at<uint32_t>(ref, 0) = 1;
    return ref;
}

// The lighting origin (+8) 32 units above the cent's origin.
void raised_lighting_origin(uint8_t* ref, const uint8_t* cent) {
    at<uint32_t>(ref, 8) = *reinterpret_cast<const uint32_t*>(cent + 0x60);
    at<uint32_t>(ref, 0xc) = *reinterpret_cast<const uint32_t*>(cent + 0x64);
    at<float>(ref, 0x10) = *reinterpret_cast<const float*>(cent + 0x68) + g(0x0083eeac);
}

bool hidden(const uint8_t* cent) { return static_cast<int8_t>(cent[0x10]) < 0; }

}  // namespace

void XB_CDECL CG_Vehicle(uint8_t* cent) {
    if (hidden(cent)) return;
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (!obj) return;
    uint8_t* ref = fill_model_ref(cent, obj);
    raised_lighting_origin(ref, cent);
    at<uint32_t>(ref, 4) = 0x80;
    const uint32_t flags = at<uint32_t>(cent, 0x10);
    if ((flags & 0x100000) && !(flags & 0x400)) at<uint32_t>(ref, 4) = 0x10;
    R_AddRefEntityToScene(ref, -1);
}

void XB_CDECL CG_Mover(uint8_t* cent) {
    if (hidden(cent)) return;
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (at<uint32_t>(cent, 0xac) != 0xffffff && !obj) return;
    uint8_t* ref = fill_model_ref(cent, obj);
    at<uint32_t>(ref, 4) = 0x40;
    R_AddRefEntityToScene(ref, -1);
}

void XB_CDECL CG_Missile(uint8_t* cent) {
    if (hidden(cent)) return;
    if (static_cast<int32_t>(cent[3]) > title_global<int32_t>(0x0095f4d4)) cent[3] = 0;  // bg_lastWeaponIndex
    const uint8_t* weapon = &title_global<uint8_t>(0x00975fa8 + cent[3] * 0xc4u);
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (!obj) return;
    uint8_t* ref = fill_model_ref(cent, obj);
    at<uint32_t>(ref, 4) = *reinterpret_cast<const uint32_t*>(weapon + 0xb4) | 0x40;
    R_AddRefEntityToScene(ref, -1);
}

void XB_CDECL CG_MG42(uint8_t* cent) {
    if (hidden(cent)) return;
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (!obj) return;
    uint8_t* ref = fill_model_ref(cent, obj);
    raised_lighting_origin(ref, cent);
    at<uint32_t>(ref, 4) = 0x80;
    R_AddRefEntityToScene(ref, -1);
}

PLATFORM_ASM_ENTRY(CG_SetLightingOrigin_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_SetLightingOrigin) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CG_SetLightingOrigin(uint8_t* cent, uint8_t* ref) {
    // With flag 0x8000 the lighting origin (render record +0x88) is kept from the first
    // frame the entity had its origin (+0x60) and copied to ref +8 (flag 0x80); else cleared.
    if (at<uint32_t>(cent, 0x10) & 0x8000) {
        const float zero = g(0x0083eafc);
        if (at<float>(entity_ref(cent), 0x88) == zero && at<float>(entity_ref(cent), 0x8c) == zero &&
            at<float>(entity_ref(cent), 0x90) == zero) {
            at<uint32_t>(entity_ref(cent), 0x88) = at<uint32_t>(cent, 0x60);
            at<uint32_t>(entity_ref(cent), 0x8c) = at<uint32_t>(cent, 0x64);
            at<uint32_t>(entity_ref(cent), 0x90) = at<uint32_t>(cent, 0x68);
        }
        at<uint32_t>(ref, 8) = at<uint32_t>(entity_ref(cent), 0x88);
        at<uint32_t>(ref, 0xc) = at<uint32_t>(entity_ref(cent), 0x8c);
        at<uint32_t>(ref, 0x10) = at<uint32_t>(entity_ref(cent), 0x90);
        at<uint32_t>(ref, 4) |= 0x80;
        return;
    }
    if (at<uint32_t>(cent, 0x250)) {
        at<float>(entity_ref(cent), 0x90) = 0.0f;
        at<float>(entity_ref(cent), 0x8c) = 0.0f;
        at<float>(entity_ref(cent), 0x88) = 0.0f;
    }
}

PLATFORM_ASM_ENTRY(CG_AdjustPositionForMover_entry,
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_AdjustPositionForMover) "addl $24, %esp\n"
                   "ret")

void XB_CDECL CG_AdjustPositionForMover(float* out, const float* in, uint32_t mover, int32_t from_time,
                                        int32_t to_time, float* delta_angles) {
    // Q3's CG_AdjustPositionForMover: `in` moved along with the mover entity (handle: index
    // in the low 12 bits of 0x0097a968 {cent, generation}; types 4 and 7) from from_time to
    // to_time.
    if (delta_angles) {
        delta_angles[2] = 0.0f;
        delta_angles[1] = 0.0f;
        delta_angles[0] = 0.0f;
    }
    const uint32_t index = mover & 0xfff;
    uint8_t* cent = nullptr;
    if (index < 0x540 && (mover >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        cent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!cent || (cent[0] != 4 && cent[0] != 7)) {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        return;
    }
    alignas(16) float old_origin[4], old_angles[4], origin[4], angles[4];
    const auto evaluate = [](int32_t time, uint8_t* trajectory, float* result) {
        TitleRegs regs{};
        regs.eax = static_cast<uint32_t>(time);
        regs.edi = addr(trajectory);
        regs.ecx = addr(result);
        cg_call_regs(0x002d62c0, &regs, nullptr, 0);  // BG_EvaluateTrajectory
    };
    evaluate(from_time, cent + 0x14, old_origin);
    evaluate(from_time, cent + 0x38, old_angles);
    evaluate(to_time, cent + 0x14, origin);
    evaluate(to_time, cent + 0x38, angles);
    const float d0 = origin[0] - old_origin[0], d1 = origin[1] - old_origin[1], d2 = origin[2] - old_origin[2];
    const float a0 = angles[0] - old_angles[0], a1 = angles[1] - old_angles[1], a2 = angles[2] - old_angles[2];
    out[0] = in[0] + d0;
    out[1] = in[1] + d1;
    out[2] = in[2] + d2;
    if (delta_angles) {
        delta_angles[0] = a0;
        delta_angles[1] = a1;
        delta_angles[2] = a2;
    }
}

int32_t XB_CDECL CG_PointContents(const float* point, int32_t pass_entity, uint32_t mask) {
    // Q3's CG_PointContents: the world's contents or'ed with those of every solid entity
    // (cg_solidEntities, handles into 0x0097a968) other than pass_entity (+0x224) that is a
    // brush model (+0xac 0xffffff, clip model +0xdc).
    TitleRegs regs{};
    regs.eax = addr(point);
    regs.ecx = 0;
    uint32_t contents = cg_call_regs(0x002cc480, &regs, nullptr, 0);  // CM_PointContents
    for (int32_t i = 0; i < title_global<int32_t>(0x0094af98); ++i) {
        const uint32_t handle = title_global<uint32_t>(0x00974810 + static_cast<uint32_t>(i) * 4);
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) continue;
        uint8_t* cent = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (!cent || at<int32_t>(cent, 0x224) == pass_entity || at<uint32_t>(cent, 0xac) != 0xffffff) continue;
        const uint32_t model = at<uint32_t>(cent, 0xdc);
        if (!model) continue;
        TitleRegs t{};
        t.eax = addr(cent + 0x60);
        t.edx = addr(cent + 0x70);
        t.ecx = static_cast<uint32_t>(pass_entity);
        t.ebx = model;
        const uint32_t args[1] = {addr(point)};
        contents |= cg_call_regs(0x002db500, &t, args, 1);  // CM_TransformedPointContents
    }
    return static_cast<int32_t>(contents & mask);
}

void XB_REGS1 XB_CDECL CG_FireWeaponEmpty(const uint8_t* cent, int32_t event) {
    // The weapon's (+3) empty-click sound (def +8, kind 4 for event 0xa6, else 3) for
    // non-event entities with +0x23c set.
    if (!*reinterpret_cast<const uint32_t*>(cent + 0x23c) || cent[0] >= 0x11 || !cent[3]) return;
    const int32_t weapon = cent[3];
    if (weapon > title_global<int32_t>(0x0095f4d4)) {
        CG_Error(&title_global<char>(0x00838764));
        return;
    }
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];
    const uint32_t sound = *reinterpret_cast<const uint32_t*>(def + 8);
    title_function<void XB_CDECL(uint32_t, uint32_t)>(0x002a62d0)(sound, event == 0xa6 ? 4 : 3);  // FXCmd_PlayWeaponSound
}

uint8_t* XB_REGS2 XB_CDECL RefEntity_Clear(uint32_t, uint8_t* record) {
    // A cent's render record (0x100 bytes) reset: header and the refEntity part (+0x80, 0x60
    // bytes) zeroed, scale +0xf4 = 1, lighting origin +0xe0 zero, bytes +0xee/+0xef 0xff.
    at<uint32_t>(record, 0) = 0;
    at<uint32_t>(record, 4) = 0;
    at<uint32_t>(record, 8) = 0;
    at<float>(record, 0xc) = 0.0f;
    for (uint32_t k = 0; k < 0x18; ++k) at<uint32_t>(record, 0x80 + 4 * k) = 0;
    record[0xed] &= 0xfd;
    record[0xee] = 0xff;
    record[0xef] = 0xff;
    at<uint16_t>(record, 0xf8) = 0;
    at<uint32_t>(record, 0xfc) = 0;
    at<float>(record, 0xf4) = g(0x0083eb20);
    at<float>(record, 0xe0) = 0.0f;
    at<float>(record, 0xe4) = 0.0f;
    at<float>(record, 0xe8) = 0.0f;
    return record;
}

PLATFORM_ASM_ENTRY(CG_GetEntityRefEntity_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_GetEntityRefEntity) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL CG_GetEntityRefEntity(uint8_t* cent) {
    // The render record (+0x250), allocated (0x100 bytes, aligned 16) on first use from the
    // memory context of the cent's client (+0x220, 0..0x62: 0x0095f638 +0x38), else the heap.
    uint8_t*& record = at<uint8_t*>(cent, 0x250);
    if (record) return record;
    const int32_t client = at<int32_t>(cent, 0x220);
    uint8_t* memory = nullptr;
    if (client != -1) {
        uint32_t context = 0;
        if (client >= 0 && client < 0x63)
            context = *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f638) + 0x38 + client * 4);
        memory = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x0028c0f0)(context, 0x10,
                                                                                            0x100);  // MemContext_Alloc_r
    }
    if (!memory) memory = title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x10, 0x100);  // Heap_AllocAligned
    if (!memory) {
        record = nullptr;
        return record;
    }
    record = RefEntity_Clear(0, memory);
    return record;
}

PLATFORM_ASM_ENTRY(CG_DObjGetTagMatrix_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_DObjGetTagMatrix) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL CG_DObjGetTagMatrix(uint32_t tag, DObj* obj, float* out) {
    // The tag's bone matrix (hash in EAX) times the view placement (axis 0x00972fe4, origin
    // 0x00972fd8) into out (4x4); 0 when the DObj has no such tag. Creates the skeleton.
    const int32_t index = DObj_GetTagIndex(obj, tag);
    if (index < 0) return 0;
    if (!obj->bone_matrices) {
        title_function<void* XB_CDECL(uint32_t, const char*, const char*, uint32_t)>(0x00042c20)(  // Z_MallocDebug
            obj->bone_count * 0x40u + 0x30, &title_global<char>(0x0083534c), &title_global<char>(0x00835354), 0x21c);
        DObj_CreateSkeleton(obj);
    }
    const uint8_t* bones = obj->bone_matrices ? obj->bone_matrices + obj->matrix_offset * 0x40u + 0x30 : nullptr;
    alignas(16) float placement[12];
    for (int k = 0; k < 9; ++k) placement[k] = title_global<float>(0x00972fe4 + 4 * k);
    for (int k = 0; k < 3; ++k) placement[9 + k] = title_global<float>(0x00972fd8 + 4 * k);
    MatrixMultiply43To44(placement, out, reinterpret_cast<const float*>(bones + index * 0x40));
    return 1;
}

void XB_CDECL CG_ScriptMover(uint8_t* cent) {
    if (hidden(cent)) return;
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (at<uint32_t>(cent, 0xac) != 0xffffff && !obj) return;
    uint8_t* ref = fill_model_ref(cent, obj);
    at<uint32_t>(ref, 4) = 0x40;
    if (at<uint32_t>(cent, 0xac) != 0xffffff) CG_SetLightingOrigin(cent, ref);
    R_AddRefEntityToScene(ref, -1);
}

void XB_CDECL CG_General(uint8_t* cent) {
    if (hidden(cent)) return;
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (!obj) return;
    uint8_t* ref = fill_model_ref(cent, obj);
    CG_SetLightingOrigin(cent, ref);
    R_AddRefEntityToScene(ref, -1);
}

void XB_CDECL CG_CalcEntityLerpPositions(uint8_t* cent) {
    // Q3's CG_CalcEntityLerpPositions: origin (+0x60) from the position trajectory (+0x14;
    // type 1 = the next snapshot's origin +0x140) moved with its mover unless it is the
    // player's cent; angles (+0x70) from +0x38, type 1 = lerped from +0x44 to +0x50 by
    // cg.frameInterpolation the short way round.
    const auto evaluate = [](int32_t time, uint8_t* trajectory, float* result) {
        TitleRegs regs{};
        regs.eax = static_cast<uint32_t>(time);
        regs.edi = addr(trajectory);
        regs.ecx = addr(result);
        cg_call_regs(0x002d62c0, &regs, nullptr, 0);  // BG_EvaluateTrajectory
    };
    float* origin = &at<float>(cent, 0x60);
    if (at<int32_t>(cent, 0x14) == 1) {
        for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(cent, 0x60 + 4 * k) = at<uint32_t>(cent, 0x140 + 4 * k);
    } else {
        evaluate(cg_time(), cent + 0x14, origin);
        if (cent != *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 4))
            CG_AdjustPositionForMover(origin, origin, at<uint32_t>(cent, 0xc),
                                      *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x00971aa8) + 4), cg_time(),
                                      nullptr);
    }
    if (at<int32_t>(cent, 0x38) != 1) {
        evaluate(cg_time(), cent + 0x38, &at<float>(cent, 0x70));
        return;
    }
    const float half_turn = g(0x0083eb60), turn = g(0x0083ebcc), minus_half = g(0x0083ebd0);
    const float f = g(0x00972530);
    for (uint32_t k = 0; k < 3; ++k) {
        float next = at<float>(cent, 0x50 + 4 * k);
        const float prev = at<float>(cent, 0x44 + 4 * k);
        if (next - prev > half_turn) next = next - turn;
        if (minus_half > next - prev) next = next + turn;
        at<float>(cent, 0x70 + 4 * k) = (next - prev) * f + prev;
    }
}

PLATFORM_ASM_ENTRY(CG_PlayWeaponFireSound_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_PlayWeaponFireSound) "addl $12, %esp\n"
                   "ret")

void XB_CDECL CG_PlayWeaponFireSound(int32_t weapon, uint32_t alt, uint32_t source) {
    // The weapon's fire sound (def +8; kind 1, or 2 without alt) and FXCmd_Type9(2), both
    // with EAX = the source in ESI.
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];
    TitleRegs regs{};
    regs.eax = source;
    regs.esi = source;
    const uint32_t sound_args[2] = {*reinterpret_cast<const uint32_t*>(def + 8), alt ? 1u : 2u};
    cg_call_regs(0x002a62d0, &regs, sound_args, 2);  // FXCmd_PlayWeaponSound
    const uint32_t kind[1] = {2};
    cg_call_regs(0x002a5bb0, &regs, kind, 1);  // FXCmd_Type9
}

void XB_REGS1 XB_CDECL CG_Portal(uint8_t* cent) {
    // A portal surface (refEntity type 0xb): origin +0x60, old origin +0x80, axis[0] the
    // byte direction +4 (bytedirs 0x008d52d8, 162 entries), axis[1] = -perpendicular, axis[2]
    // = axis[0] x axis[1].
    uint8_t* ref = entity_ref(cent) + 0x80;
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ref, 0x3c + 4 * k) = at<uint32_t>(cent, 0x60 + 4 * k);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ref, 0x48 + 4 * k) = at<uint32_t>(cent, 0x80 + 4 * k);
    float* a0 = &at<float>(ref, 0x14);
    float* a1 = &at<float>(ref, 0x20);
    const uint32_t dir = cent[4];
    if (dir < 0xa2) {
        for (uint32_t k = 0; k < 3; ++k)
            reinterpret_cast<uint32_t*>(a0)[k] = title_global<uint32_t>(0x008d52d8 + dir * 12 + 4 * k);
    } else {
        a0[0] = 0.0f;
        a0[1] = 0.0f;
        a0[2] = 0.0f;
    }
    PerpendicularVector(a0, a1);
    a1[0] = 0.0f - a1[0];
    a1[2] = 0.0f - a1[2];
    a1[1] = 0.0f - a1[1];
    float* a2 = &at<float>(ref, 0x2c);
    a2[0] = a1[2] * a0[1] - a1[1] * a0[2];
    a2[1] = a1[0] * a0[2] - a1[2] * a0[0];
    a2[2] = a1[1] * a0[0] - a1[0] * a0[1];
    at<uint32_t>(ref, 0) = 0xb;
    R_AddRefEntityToScene(ref, -1);
}

PLATFORM_ASM_ENTRY(CG_UpdateFlashSound_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_UpdateFlashSound) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_UpdateFlashSound(uint8_t* cent) {
    // While the cent's flash timer (+0x41c) runs: counted down by the frametime, the
    // "tag_flash" placement looked up (the player's view model 0x00975fa8 + weapon * 0xc4, or
    // the cent's DObj through 0x00284d00; else its trajectory) into a local nothing reads,
    // then the weapon's sound kind 0xb (still running) or 0xc (just ended).
    uint8_t* weapon_info = &title_global<uint8_t>(0x00975fa8 + cent[3] * 0xc4u);
    uint32_t hash;
    if (!(title_global<uint8_t>(0x0098f1c8) & 1)) {
        title_global<uint32_t>(0x0098f1c8) |= 1;
        hash = Com_HashKeyNoCase(0, &title_global<char>(0x008389c0));
        title_global<uint32_t>(0x0098f1c4) = hash;
    } else {
        hash = title_global<uint32_t>(0x0098f1c4);
    }
    int32_t& timer = at<int32_t>(cent, 0x41c);
    if (timer <= 0) return;
    timer -= title_global<int32_t>(0x00972534);
    alignas(16) float placement[16];
    uint32_t found = 0;
    const int32_t client = *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x00971aa8) + 0x8c);
    if (at<int32_t>(cent, 0x224) == client) {
        DObj* obj = *reinterpret_cast<DObj**>(weapon_info);
        if (obj) found = CG_DObjGetTagMatrix(hash, obj, placement);
    } else if (at<uint32_t>(cent, 0x22c)) {
        TitleRegs regs{};
        regs.eax = hash;
        regs.ecx = at<uint32_t>(cent, 0x22c);
        const uint32_t args[2] = {addr(cent), addr(placement)};
        found = cg_call_regs(0x00284d00, &regs, args, 2);
    }
    if (!found) {
        alignas(16) float origin[4];
        TitleRegs regs{};
        regs.eax = static_cast<uint32_t>(cg_time());
        regs.edi = addr(cent + 0x14);
        regs.ecx = addr(origin);
        cg_call_regs(0x002d62c0, &regs, nullptr, 0);  // BG_EvaluateTrajectory
    }
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[cent[3]];
    TitleRegs regs{};
    regs.eax = addr(cent);
    regs.esi = addr(cent);
    const uint32_t args[2] = {*reinterpret_cast<const uint32_t*>(def + 8), timer > 0 ? 0xbu : 0xcu};
    cg_call_regs(0x002a62d0, &regs, args, 2);  // FXCmd_PlayWeaponSound
}

void XB_CDECL CG_Actor(uint8_t* cent) {
    // An actor model: as CG_Vehicle (lit 32 units above the origin, flag 0x80, | 0x20 with
    // entity flag 0x100), then its flash sound.
    if (hidden(cent)) return;
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (!obj) return;
    uint8_t* ref = fill_model_ref(cent, obj);
    raised_lighting_origin(ref, cent);
    at<uint32_t>(ref, 4) = 0x80;
    if (at<uint32_t>(cent, 0x10) & 0x100) at<uint32_t>(ref, 4) |= 0x20;
    R_AddRefEntityToScene(ref, -1);
    CG_UpdateFlashSound(cent);
}

// The trace functions run their title callees at the original's ESP with their locals at
// the original's addresses (the collision code keeps pointers into the trace work, 30% of
// the tests differed otherwise): each entry moves the C frame below a gap and hands the C
// version its entry ESP; the original frame is ((entry ESP - 4) & ~15) - frame size.
PLATFORM_ASM_ENTRY(CG_ClipMoveToEntities_entry,
                   "movl %esp, %edx\n"
                   "subl $0x10000, %esp\n"
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl 32(%edx)\n"
                   "pushl 28(%edx)\n"
                   "pushl 24(%edx)\n"
                   "pushl 20(%edx)\n"
                   "pushl 16(%edx)\n"
                   "pushl 12(%edx)\n"
                   "pushl 8(%edx)\n"
                   "pushl 4(%edx)\n" PLATFORM_ASM_CALL(CG_ClipMoveToEntities) "addl $36, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_ClipMoveToEntities(const float* start, const float* mins, const float* maxs, const float* end,
                                    int32_t skip, uint32_t mask, uint32_t capsule, uint8_t* tr, uint32_t entry_esp) {
    // Q3's CG_ClipMoveToEntities over cg_solidEntities: brush models (+0xac 0xffffff, clip
    // model +0xdc) at their trajectories' position/angles at 0x00972544, other solids as a
    // temporary box from the packed size +0xac (x/y radius, z down - 1, z up - 32; contents
    // by type: 1 = 0x2000000, 0xb = 0x4000, 0xd = 0x4000000, else 1) at their origin; the
    // nearest hit (or a start inside) goes into tr with the entity number (+0x30).
    const uint32_t frame = (((entry_esp - 4) & ~15u) - 0xa0);
    uint8_t* trace = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(frame + 0x60));
    float* origin = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x20));
    float* angles = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x30));
    float* box_mins = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x40));
    float* box_maxs = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x50));
    at<uint32_t>(trace, 0x30) = 0;
    at<uint32_t>(trace, 0x34) = 0;
    for (int32_t i = 0; i < title_global<int32_t>(0x0094af98); ++i) {
        const uint32_t handle = title_global<uint32_t>(0x00974810 + static_cast<uint32_t>(i) * 4);
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) continue;
        uint8_t* cent = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (!cent || at<int32_t>(cent, 0x224) == skip) continue;
        uint32_t model;
        if (at<uint32_t>(cent, 0xac) == 0xffffff) {
            model = at<uint32_t>(cent, 0xdc);
            TitleRegs regs{};
            regs.eax = static_cast<uint32_t>(title_global<int32_t>(0x00972544));
            regs.edi = addr(cent + 0x38);
            regs.ecx = addr(angles);
            title_call_at_regs(frame, 0x002d62c0, &regs, 0, nullptr);  // BG_EvaluateTrajectory
            regs.eax = static_cast<uint32_t>(title_global<int32_t>(0x00972544));
            regs.edi = addr(cent + 0x14);
            regs.ecx = addr(origin);
            title_call_at_regs(frame, 0x002d62c0, &regs, 0, nullptr);
        } else {
            const uint32_t packed = at<uint32_t>(cent, 0xac);
            const float radius = static_cast<float>(static_cast<int32_t>(packed & 0xff));
            box_mins[0] = box_mins[1] = 0.0f - radius;
            box_maxs[0] = box_maxs[1] = radius;
            box_mins[2] = 0.0f - static_cast<float>(static_cast<int32_t>((packed >> 8) & 0xff) - 1);
            box_maxs[2] = static_cast<float>(static_cast<int32_t>((packed >> 16) & 0xff) - 0x20);
            uint32_t contents;
            switch (cent[0]) {
            case 1: contents = 0x2000000; break;
            case 0xb: contents = 0x4000; break;
            case 0xd: contents = 0x4000000; break;
            default: contents = 1; break;
            }
            if (!(mask & contents)) continue;
            TitleRegs regs{};
            regs.eax = addr(box_maxs);
            regs.edx = (cent[0x10] & 0x10) ? 1 : 0;
            const uint32_t args[2] = {addr(box_mins), contents};
            model = title_call_at_regs(frame, 0x002ccc50, &regs, 2, args);  // CM_TempBoxModel
            origin[0] = at<float>(cent, 0x60);
            origin[1] = at<float>(cent, 0x64);
            origin[2] = at<float>(cent, 0x68);
            angles[0] = angles[1] = angles[2] = 0.0f;
        }
        at<float>(trace, 0x20) = g(0x0083eb20);
        TitleRegs regs{};
        regs.eax = addr(origin);
        regs.ecx = addr(maxs);
        const uint32_t args[8] = {addr(trace), addr(start), addr(end), addr(mins), model, mask, addr(angles), capsule ? 1u : 0u};
        title_call_at_regs(frame, 0x002e70c0, &regs, 8, args);  // CM_TransformedBoxTrace
        if (trace[0x3c] || at<float>(tr, 0x20) > at<float>(trace, 0x20)) {
            at<int32_t>(trace, 0x30) = at<int32_t>(cent, 0x224);
            for (uint32_t k = 0; k < 0x40; k += 4) at<uint32_t>(tr, k) = at<uint32_t>(trace, k);
        } else if (trace[0x3d]) {
            tr[0x3d] = 1;
        }
        if (tr[0x3c]) return;
    }
}

namespace {

// CG_Trace / CG_TracePoint: the world trace (CM_BoxTrace 0x002e5030: maxs in ECX), its entity
// number (+0x30) the world's when it hit, then the solid entities (CG_ClipMoveToEntities at
// its title address: in tests the original runs on both sides).
void trace_world_and_entities(uint8_t* result, const float* start, const float* mins, const float* maxs,
                              const float* end, int32_t skip, uint32_t mask, uint32_t capsule, uint32_t entry_esp) {
    const uint32_t frame = (((entry_esp - 4) & ~15u) - 0x50);
    uint8_t* trace = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(frame + 0x10));
    at<uint32_t>(trace, 0x30) = 0;
    at<uint32_t>(trace, 0x34) = 0;
    TitleRegs regs{};
    regs.ecx = addr(maxs);
    const uint32_t box_args[7] = {addr(trace), addr(start), addr(end), addr(mins), 0, mask, 0};
    title_call_at_regs(frame, 0x002e5030, &regs, 7, box_args);
    at<int32_t>(trace, 0x30) =
        at<float>(trace, 0x20) == 1.0f ? 0 : at<int32_t>(*reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 8), 0x224);
    const TitleRegs none{};
    const uint32_t clip_args[8] = {addr(start), addr(mins), addr(maxs), addr(end), static_cast<uint32_t>(skip), mask, capsule,
                                   addr(trace)};
    title_call_at_regs(frame, 0x00286950, &none, 8, clip_args);  // CG_ClipMoveToEntities
    for (uint32_t k = 0; k < 0x40; k += 4) at<uint32_t>(result, k) = at<uint32_t>(trace, k);
}

}  // namespace

PLATFORM_ASM_ENTRY(CG_Trace_entry,
                   "movl %esp, %edx\n"
                   "subl $0x10000, %esp\n"
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl 28(%edx)\n"
                   "pushl 24(%edx)\n"
                   "pushl 20(%edx)\n"
                   "pushl 16(%edx)\n"
                   "pushl 12(%edx)\n"
                   "pushl 8(%edx)\n"
                   "pushl 4(%edx)\n" PLATFORM_ASM_CALL(CG_Trace) "addl $32, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_Trace(uint8_t* result, const float* start, const float* mins, const float* maxs, const float* end,
                       int32_t skip, uint32_t mask, uint32_t entry_esp) {
    trace_world_and_entities(result, start, mins, maxs, end, skip, mask, 1, entry_esp);
}

PLATFORM_ASM_ENTRY(CG_TracePoint_entry,
                   "movl %esp, %edx\n"
                   "subl $0x10000, %esp\n"
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl 20(%edx)\n"
                   "pushl 16(%edx)\n"
                   "pushl 12(%edx)\n"
                   "pushl 8(%edx)\n"
                   "pushl 4(%edx)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_TracePoint) "addl $32, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_TracePoint(uint32_t mask, const float* end, uint8_t* result, const float* start, const float* mins,
                            const float* maxs, int32_t skip, uint32_t entry_esp) {
    trace_world_and_entities(result, start, mins, maxs, end, skip, mask, 0, entry_esp);
}

void XB_REGS3 XB_CDECL CG_DamageFeedback(int32_t yaw_byte, uint32_t, int32_t pitch_byte, int32_t damage) {
    // Q3's CG_DamageFeedback: a view kick (0x00972f80 pitch, 0x00972f84 roll) from the damage
    // direction (bytes / 255 * 360, 255/255 = straight ahead) against the view axis
    // (0x00972bc0..), kick = damage * 0x0094b938 clamped to 5..0x0094a4f0; the oldest of the
    // 8 damage indicators (0x00972f00, {time, 0x0094bffc, yaw +-10 random}) refreshed; then a
    // rumble (both motors: strength 0.4..1, fade 0.12..0.4 by the damage) while 0x0095f5c4.
    const float amount = static_cast<float>(damage);
    float kick = amount * g(0x0094b938);
    if (g(0x0083ece8) > kick) {
        kick = g(0x0083ece8);
    } else if (kick > g(0x0094a4f0)) {
        kick = g(0x0094a4f0);
    }
    if (yaw_byte == 0xff && pitch_byte == yaw_byte) {
        title_global<float>(0x00972f84) = 0.0f;
        title_global<float>(0x00972f80) = 0.0f - kick;
    } else {
        const float full = g(0x0083eb8c), turn = g(0x0083ebcc), to_radians = g(0x0083edd8);
        const float pitch = static_cast<float>(pitch_byte) / full * turn;
        const float yaw = static_cast<float>(yaw_byte) / full * turn;
        float sy, cy, sp, cp;
        FastSinCos(&sy, 0, &cy, yaw * to_radians);
        FastSinCos(&sp, 0, &cp, pitch * to_radians);
        const float x = 0.0f - cp * cy;
        const float y = 0.0f - cp * sy;
        const float z = 0.0f - (0.0f - sp);
        title_global<float>(0x00972f84) =
            ((g(0x00972bd4) * z + g(0x00972bd0) * y) + g(0x00972bcc) * x) * kick;
        title_global<float>(0x00972f80) =
            0.0f - ((g(0x00972bc8) * z + g(0x00972bc4) * y) + g(0x00972bc0) * x) * kick;
        uint32_t oldest = 0;
        for (uint32_t k = 1; k < 8; ++k)
            if (title_global<int32_t>(0x00972f00 + k * 12) < title_global<int32_t>(0x00972f00 + oldest * 12)) oldest = k;
        uint8_t* slot = &title_global<uint8_t>(0x00972f00 + oldest * 12);
        at<int32_t>(slot, 0) = *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x00971aa8) + 4);
        at<uint32_t>(slot, 4) = title_global<uint32_t>(0x0094bffc);
        const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
        const float jitter = (static_cast<float>(r) / g(0x0083ebc8) - g(0x0083eb24)) * g(0x0083eb5c) + yaw;
        at<float>(slot, 8) = static_cast<float>(static_cast<int32_t>(jitter * g(0x0083ebd8)) & 0xffff) * g(0x0083ebd4);
    }
    title_global<int32_t>(0x00972f7c) = cg_time() + 500;
    title_global<int32_t>(0x00972f60) = *reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x00971aa8) + 4);
    const float high = g(0x0083ec44);
    float strength;
    const float a = amount * g(0x00830670);
    if (high > a)
        strength = high;
    else if (a > g(0x0083eb20))
        strength = g(0x0083eb20);
    else
        strength = a;
    float fade;
    const float b = amount * g(0x0083eb88);
    if (g(0x0083f068) > b)
        fade = g(0x0083f068);
    else if (b > high)
        fade = high;
    else
        fade = b;
    // The rumble description: two motors {flag byte, start, strength, hold, fade, fade in,
    // name, byte}; only the low bytes of the byte fields are written.
    alignas(4) uint8_t desc[0x40] = {};
    for (uint32_t m = 0; m < 2; ++m)
        ScriptString_FromRep(reinterpret_cast<ScriptString*>(desc + 0x18 + m * 0x20), nullptr);
    for (uint32_t m = 0; m < 2; ++m) {
        uint8_t* motor = desc + m * 0x20;
        at<float>(motor, 4) = 0.0f;
        at<float>(motor, 0xc) = 0.0f;
        at<float>(motor, 0x14) = 0.0f;
        motor[0] = 0;
        motor[0x1c] = 0;
        at<float>(motor, 8) = strength;
        at<float>(motor, 0x10) = fade;
    }
    const uint32_t manager = title_global<uint32_t>(0x0095f5c4);
    if (manager) {
        uint32_t id;
        ShakeRumble_Start(desc, reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(manager)), &id, g(0x0083eb20));
    }
    for (int m = 1; m >= 0; --m)
        title_function<void XB_THISCALL(void*)>(0x0011d640)(desc + 0x18 + m * 0x20);  // ScriptString_Release
}

void XB_CDECL CG_TransitionSnapshot() {
    // Q3's CG_TransitionSnapshot: the next snapshot becomes current, the player's cent
    // (handle +0x8c) and every snapshot entity (0x0097d370, count 0x0097d36c) marked (+0x418)
    // with their events checked; with 0x0094b66c the player-state transition (damage
    // feedback when the damage event count +0xfc changed, ammo, player-state events).
    uint8_t* old = title_global<uint8_t*>(0x00971aa8);
    uint8_t* snap = title_global<uint8_t*>(0x00971aac);
    title_global<uint8_t*>(0x00971aa8) = snap;
    const uint32_t handle = at<uint32_t>(snap, 0x8c);
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    uint8_t* player = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!player) return;
    at<uint32_t>(player, 0x418) = 1;
    uint8_t** entities = &title_global<uint8_t*>(0x0097d370);
    uint8_t** last = entities + title_global<int32_t>(0x0097d36c);
    for (uint8_t** e = entities; e != last; ++e) {
        if (!*e) continue;
        at<uint32_t>(*e, 0x418) = 1;
        CG_CheckEvents(*e);
    }
    if (!title_global<uint32_t>(0x0094b66c)) return;
    snap = title_global<uint8_t*>(0x00971aa8);
    const uint8_t* ps = snap + 0xd4;
    if (at<int32_t>(snap, 0xfc) != at<int32_t>(old, 0xfc) && *reinterpret_cast<const int32_t*>(ps + 0x34))
        CG_DamageFeedback(*reinterpret_cast<const int32_t*>(ps + 0x2c), 0, *reinterpret_cast<const int32_t*>(ps + 0x30),
                          *reinterpret_cast<const int32_t*>(ps + 0x34));
    CG_CheckAmmo();
    CG_CheckPlayerstateEvents(at<uint32_t>(snap, 0xd0), at<uint32_t>(old, 0xd0), reinterpret_cast<const int32_t*>(old + 0xd4),
                              reinterpret_cast<const int32_t*>(ps));
}

PLATFORM_ASM_ENTRY(CG_Item_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CG_Item) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_Item(uint8_t* cent) {
    // An item (index +6, < 0x88): registered first (0x00949a98, 8 bytes each), then its model
    // at the origin with the left-handed axis (AnglesToAxisLeft 0x002a4610) and +0x38 = 1
    // for items of kind 1 (0x0096bd10 + index * 0x34, +0x24).
    if (at<uint16_t>(cent, 6) >= 0x88) CG_Error(&title_global<char>(0x00839a20), static_cast<int32_t>(at<uint16_t>(cent, 6)));
    if (hidden(cent)) return;
    const uint32_t index = at<uint16_t>(cent, 6);
    const uint8_t* item = &title_global<uint8_t>(0x0096bd10 + index * 0x34);
    if (!title_global<uint8_t>(0x00949a98 + index * 8)) {
        title_function<void XB_CDECL(uint32_t)>(0x00285d50)(at<uint16_t>(cent, 6));  // CG_RegisterItemVisuals
        return;
    }
    const uint32_t obj = at<uint32_t>(cent, 0x22c);
    if (!obj) return;
    uint8_t* ref = entity_ref(cent) + 0x80;
    at<float>(ref, 0x38) = 0.0f;
    TitleRegs regs{};
    regs.eax = addr(cent + 0x70);
    const uint32_t args[1] = {addr(ref + 0x14)};
    cg_call_regs(0x002a4610, &regs, args, 1);  // AnglesToAxisLeft
    if (*reinterpret_cast<const int32_t*>(item + 0x24) == 1) at<float>(ref, 0x38) = g(0x0083eb20);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ref, 0x3c + 4 * k) = at<uint32_t>(cent, 0x60 + 4 * k);
    for (uint32_t k = 0; k < 3; ++k) at<uint32_t>(ref, 0x48 + 4 * k) = at<uint32_t>(cent, 0x60 + 4 * k);
    at<uint32_t>(ref, 0x54) = obj;
    at<uint32_t>(ref, 0x58) = addr(cent);
    at<uint32_t>(ref, 0) = 1;
    R_AddRefEntityToScene(ref, -1);
}

void XB_REGS1 XB_CDECL CG_AddCEntity(uint8_t* cent) {
    // Q3's CG_AddCEntity: by entity type (+0).
    switch (cent[0]) {
    case 0: CG_General(cent); break;
    case 2: CG_Item(cent); break;
    case 3: CG_Missile(cent); break;
    case 4: CG_Mover(cent); break;
    case 5: CG_Portal(cent); break;
    case 7: CG_ScriptMover(cent); break;
    case 10: CG_MG42(cent); break;
    case 11:
    case 13: CG_Actor(cent); break;
    case 14:
    case 15: CG_Vehicle(cent); break;
    case 1:
    case 6:
    case 8:
    case 9:
    case 12: break;
    default: CG_Error(&title_global<char>(0x008399c4), static_cast<int32_t>(cent[0])); break;
    }
}

void XB_CDECL CG_AddPacketEntities() {
    // Q3's CG_AddPacketEntities: the frame counter 0x0093fc28 (never 0), the three auto-rotate
    // axes (yaw from cg.time over 4095/2048/1024 ms; 0x00972af8/b28/b58 angles, axis +0xc,
    // left = 0 - right), then the player's cent and every snapshot entity whose render record
    // was used this frame (+0xfc == 0x00985414), each lerped and added (types below 0x11).
    uint32_t& frame = title_global<uint32_t>(0x0093fc28);
    if (++frame == 0) frame = 1;
    const int32_t now = cg_time();
    title_global<float>(0x00972afc) = static_cast<float>((now & 0xfff) * 0x168) / g(0x0083f244);
    title_global<float>(0x00972b2c) = static_cast<float>((now & 0x7ff) * 0x168) / g(0x0083eb7c);
    const float third = static_cast<float>((now & 0x3ff) * 0x168) / g(0x0083eff8);
    static const uint32_t kZeroed[6] = {0x009730a0, 0x00972af8, 0x00972b00, 0x00972b28, 0x00972b30, 0x00972b58};
    for (uint32_t k = 0; k < 6; ++k) title_global<float>(kZeroed[k]) = 0.0f;
    title_global<float>(0x00972b5c) = third;
    title_global<float>(0x00972b60) = 0.0f;
    for (uint32_t base = 0x00972af8; base <= 0x00972b58; base += 0x30) {
        uint32_t args[2] = {base, base + 0xc};
        float right[3];
        AnglesToAxis(args, right, &title_global<float>(base + 0x24));
        for (uint32_t k = 0; k < 3; ++k) title_global<float>(base + 0x18 + 4 * k) = 0.0f - right[k];
    }
    uint8_t* player = *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 4);
    if (player[0] < 0x11) {
        CG_CalcEntityLerpPositions(player);
        CG_AddCEntity(player);
    }
    uint8_t** entities = &title_global<uint8_t*>(0x0097d370);
    uint8_t** last = entities + title_global<int32_t>(0x0097d36c);
    for (uint8_t** e = entities; e != last; ++e) {
        uint8_t* cent = *e;
        if (!cent) continue;
        const uint8_t* record = at<uint8_t*>(cent, 0x250);
        if (!record || *reinterpret_cast<const uint32_t*>(record + 0xfc) != title_global<uint32_t>(0x00985414)) continue;
        if (cent[0] >= 0x11) continue;
        CG_CalcEntityLerpPositions(cent);
        CG_AddCEntity(cent);
    }
}

// AddLeanToPosition (0x002d9cc0): position in ESI, lean in XMM1, cdecl (yaw, a, b).
extern "C" PLATFORM_HIDDEN void XB_CDECL cg_add_lean(float* position, float lean, float yaw, float a, float b);
PLATFORM_ASM_ENTRY(cg_add_lean,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "movss 12(%esp), %xmm1\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "movl $0x002d9cc0, %eax\n"
                   "call *%eax\n"
                   "addl $12, %esp\n"
                   "popl %esi\n"
                   "ret")

PLATFORM_ASM_ENTRY(CG_GetEntityEyePosition_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(CG_GetEntityEyePosition) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL CG_GetEntityEyePosition(float* out, uint32_t handle, const char* tag) {
    // The player's eye (snapshot origin +0x10 plus view height +0xbc, leaned by +0x5c), else
    // the entity's tag (hash cached in 0x0098ebd4) from its DObj's world matrix, else its
    // origin (+0x20) raised by the stance's eye height (0x00929b6c / 0x009354b4 / 0x009341c4
    // for flags 0x40 / 0x20 / neither) when it is a living entity. Always 1.
    if (handle == at<uint32_t>(title_global<uint8_t*>(0x00971aa8), 0x8c)) {
        reinterpret_cast<uint32_t*>(out)[0] = at<uint32_t>(title_global<uint8_t*>(0x00971aa8), 0x10);
        reinterpret_cast<uint32_t*>(out)[1] = at<uint32_t>(title_global<uint8_t*>(0x00971aa8), 0x14);
        out[2] = at<float>(title_global<uint8_t*>(0x00971aa8), 0x18);
        out[2] = at<float>(title_global<uint8_t*>(0x00971aa8), 0xbc) + out[2];
        cg_add_lean(out, at<float>(title_global<uint8_t*>(0x00971aa8), 0x5c), g(0x00972bf4), 16.0f, 20.0f);
        return 1;
    }
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 1;
    uint8_t* cent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!cent) return 1;
    if (!(title_global<uint8_t>(0x0098ebd8) & 1)) {
        title_global<uint32_t>(0x0098ebd8) |= 1;
        title_global<uint32_t>(0x0098ebd4) = Com_HashKeyNoCase(0, tag);
    }
    DObj* obj = at<DObj*>(cent, 0x22c);
    if (obj) {
        const int32_t bone = DObj_GetTagIndex(obj, title_global<uint32_t>(0x0098ebd4));
        if (bone >= 0) {
            alignas(16) float m[16];
            title_function<void XB_CDECL(uint8_t*, int32_t, float*)>(0x002e3780)(cent, bone, m);  // G_DObjGetWorldTagMatrix
            out[0] = m[12];
            out[1] = m[13];
            out[2] = m[14];
            return 1;
        }
    }
    reinterpret_cast<uint32_t*>(out)[0] = at<uint32_t>(cent, 0x20);
    reinterpret_cast<uint32_t*>(out)[1] = at<uint32_t>(cent, 0x24);
    const float z = at<float>(cent, 0x28);
    out[2] = z;
    const uint32_t handle_copy = handle;
    const uint8_t* ent = G_EntHandleToEnt(&handle_copy);
    if (!*reinterpret_cast<const uint32_t*>(ent + 0x23c)) return 1;
    const uint32_t flags = at<uint32_t>(cent, 0x10);
    const int32_t height = (flags & 0x40)   ? title_global<int32_t>(0x00929b6c)
                           : (flags & 0x20) ? title_global<int32_t>(0x009354b4)
                                            : title_global<int32_t>(0x009341c4);
    out[2] = static_cast<float>(height) + z;
    return 1;
}

PLATFORM_ASM_ENTRY(CG_BulletWhizBy_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_BulletWhizBy) "addl $12, %esp\n"
                   "ret")

void XB_CDECL CG_BulletWhizBy(const float* end, const float* start, uint32_t handle) {
    // A whiz-by sound ("WhizBySound" on the shooter) when the bullet from start to end passes
    // the view origin within 140 units, at least 64 units down the path and before its end.
    const float d2 = end[2] - start[2], d1 = end[1] - start[1], d0 = end[0] - start[0];
    const float length = title_ci_sqrt_ext(static_cast<long double>((d2 * d2 + d1 * d1) + d0 * d0));
    float n0, n1, n2;
    if (!(length == g(0x0083eafc))) {
        const float inverse = g(0x0083eb20) / length;
        n0 = inverse * d0;
        n2 = inverse * d2;
        n1 = inverse * d1;
    } else {
        n0 = n1 = n2 = 0.0f;
    }
    const float v0 = g(0x00972bb0), v1 = g(0x00972bb4), v2 = g(0x00972bb8);
    const float along_end = (n2 * d2 + n1 * d1) + n0 * d0;
    const float along = ((v2 - start[2]) * n2 + (v1 - start[1]) * n1) + (v0 - start[0]) * n0;
    if (g(0x00830c1c) > along) return;
    if (along - g(0x0083f1f4) > along_end) return;
    const float q2 = (along * n2 + start[2]) - v2;
    const float q1 = (along * n1 + start[1]) - v1;
    const float q0 = (along * n0 + start[0]) - v0;
    const float distance = title_ci_sqrt_ext(static_cast<long double>((q2 * q2 + q1 * q1) + q0 * q0));
    if (distance > g(0x0083edcc)) return;
    const uint32_t handle_copy = handle;
    TitleRegs regs{};
    regs.ecx = addr(G_EntHandleToEnt(&handle_copy));
    const uint32_t args[4] = {0x00838720, 0, 0xffffffff, 0};
    cg_call_regs(0x002a6990, &regs, args, 4);  // FXCmd_PlaySoundAlias
}

void XB_REGS1 XB_CDECL CG_BulletTracerAndWhizBy(const char* tag, uint32_t handle, const float* end, int32_t weapon) {
    // From the shooter's eye (CG_GetEntityEyePosition of tag): a tracer by chance (as
    // CG_MaybeSpawnTracer, in 100) and the whiz-by, unless the shooter is the player.
    if (0.0f >= g(0x0094a8e0)) return;
    alignas(16) float eye[4];
    if (!CG_GetEntityEyePosition(eye, handle, tag)) return;
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    uint8_t* cent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!cent) return;
    if (cent != *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 4)) {
        const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];
        float chance = g(0x0094a8e0);
        if (def) {
            const int32_t kind = *reinterpret_cast<const int32_t*>(def + 0xa8);
            if (kind == 3 || kind == 4) chance = g(0x0083ed28);
        }
        const int32_t roll = title_rand() % 100;
        if (chance * g(0x0083eb90) > static_cast<float>(roll)) {
            alignas(16) float from[4] = {eye[0], eye[1], eye[2], eye[0]};
            CG_SpawnTracer(end, 0, from, weapon);
        }
    }
    if (cent != *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 4)) CG_BulletWhizBy(end, eye, handle);
}

// FXCmd_Type5AtTransform copies 12 words of the transform, the original writes 9: the
// last 3 are whatever its stack held. The entry keeps the region near the entry ESP
// untouched (C frame below a gap) and the C version builds the transform at the original's
// address, (entry ESP - 4) & ~15 less 0x30.
PLATFORM_ASM_ENTRY(CG_Bullet_entry,
                   "movl %esp, %edx\n"
                   "subl $0x1000, %esp\n"
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl 8(%edx)\n"
                   "pushl 4(%edx)\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_Bullet) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_Bullet(const uint32_t* start, uint8_t* cent, int32_t weapon, const uint32_t* end, uint32_t extra,
                       uint32_t entry_esp) {
    // The impact effect (FXCmd_Type5AtTransform with the weapon kind +0xa8 and {end, start,
    // extra}), then CG_BulletTracerAndWhizBy from the shooter's flash tag ("tag_guncoax" for
    // a vehicle's coaxial weapon, else the turret record's tag name 0x008da6e0[+0x188]).
    uint32_t* transform = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(((entry_esp - 4) & ~15u) - 0x30));
    for (int k = 0; k < 4; ++k) transform[k] = end[k];
    for (int k = 0; k < 4; ++k) transform[4 + k] = start[k];
    transform[8] = extra;
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[weapon];
    TitleRegs regs{};
    regs.eax = addr(cent);
    regs.esi = addr(cent);
    regs.edi = static_cast<uint32_t>(weapon);
    const uint32_t args[2] = {*reinterpret_cast<const uint32_t*>(def + 0xa8), addr(transform)};
    cg_call_regs(0x002a61f0, &regs, args, 2);  // FXCmd_Type5AtTransform
    const uint8_t* turret = at<uint8_t*>(cent, 0x248);
    const char* tag = title_global<const char*>(0x008da6e0);
    if (turret) {
        if (*reinterpret_cast<const int32_t*>(turret + 0x1a4) == weapon)
            tag = &title_global<char>(0x00838714);
        else
            tag = title_global<const char*>(0x008da6e0 + *reinterpret_cast<const uint32_t*>(turret + 0x188) * 4);
    }
    CG_BulletTracerAndWhizBy(tag, at<uint32_t>(cent, 0x224), reinterpret_cast<const float*>(end), weapon);
}

PLATFORM_ASM_ENTRY(CG_SetViewHandsModel_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(CG_SetViewHandsModel) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_SetViewHandsModel(const char* name) {
    // Every weapon (1..bg_lastWeaponIndex) whose view-model hands name (0x009760d4 + (weapon
    // - 1) * 0xc4) differs from `name` gets it (0x00283120(weapon, name)).
    for (int32_t weapon = 1; weapon <= title_global<int32_t>(0x0095f4d4); ++weapon) {
        const char* current = &title_global<char>(0x009760d4 + static_cast<uint32_t>(weapon - 1) * 0xc4);
        int32_t i = 0;
        while (current[i] == name[i] && current[i]) ++i;
        if (current[i] != name[i])
            title_function<void XB_CDECL(int32_t, const char*)>(0x00283120)(weapon, name);
    }
}

PLATFORM_ASM_ENTRY(CG_SetNextSnap_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_SetNextSnap) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_SetNextSnap(uint8_t* snap) {
    // Q3's CG_SetNextSnap: cg.nextSnap, the frame counter, cg.frameInterpolation (0 when the
    // snapshots share a time or the result is negative), the server commands up to +0x538,
    // the player state copied into the current snapshot (unless the player's cent was seen
    // and lacks flag 8), the view hands model (+0xa8 +0x48), the solid list and the
    // entities' pre-events.
    title_global<uint8_t*>(0x00971aac) = snap;
    uint32_t& frame = title_global<uint32_t>(0x0093fc28);
    if (++frame == 0) frame = 1;
    const int32_t previous = at<int32_t>(title_global<uint8_t*>(0x00971aa8), 4);
    const int32_t delta = at<int32_t>(snap, 4) - previous;
    float& interpolation = title_global<float>(0x00972530);
    if (delta == 0) {
        interpolation = 0.0f;
    } else {
        interpolation = static_cast<float>(cg_time() - previous) / static_cast<float>(delta);
        if (0.0f > interpolation) interpolation = 0.0f;
    }
    CG_ExecuteNewServerCommands(at<int32_t>(snap, 0x538));
    const uint32_t handle = at<uint32_t>(snap, 0x8c);
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8)) {
        const uint8_t* cent = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (cent && (!at<uint32_t>(const_cast<uint8_t*>(cent), 0x418) || (cent[0x10] & 8))) {
            uint8_t* current = title_global<uint8_t*>(0x00971aa8);
            for (uint32_t k = 0; k < 0x148 * 4; k += 4) at<uint32_t>(current, 0x10 + k) = at<uint32_t>(snap, 0x10 + k);
            title_global<float>(0x00972ae0) = 0.0f;
            title_global<float>(0x00972adc) = 0.0f;
            title_global<float>(0x00972ad8) = 0.0f;
        }
    }
    const uint8_t* info = at<uint8_t*>(snap, 0xa8);
    if (info) {
        const char* hands = *reinterpret_cast<const char* const*>(info + 0x48);
        if (hands && *hands) CG_SetViewHandsModel(hands);
    }
    CG_BuildSolidList();
    uint8_t** entities = &title_global<uint8_t*>(0x0097d370);
    uint8_t** last = entities + title_global<int32_t>(0x0097d36c);
    for (uint8_t** e = entities; e != last; ++e)
        if (*e) CG_CheckEntityPreEvents(*e, addr(last));
}

uint8_t* XB_CDECL CG_ReadNextSnapshot() {
    // Q3's CG_ReadNextSnapshot: the next snapshot the client has (CL_GetSnapshot 0x002ac510,
    // number in ECX, buffer in EDX) into whichever of the two buffers (0x00971ab0/0x00971ff0)
    // cg.snap is not; null when none is left. With no weapon yet (0x00947fa4) the snapshot's
    // weapon (+0x90) is requested through the vmCvars 0x00947f98 / 0x0094c230.
    int32_t& processed = title_global<int32_t>(0x00971a98);
    const int32_t latest = title_global<int32_t>(0x00971a9c);
    if (latest > processed + 1000) CG_Printf(&title_global<char>(0x00838ac4), latest, processed);
    const auto va_int = [](int32_t v) {
        return title_function<const char* XB_CDECL(const char*, int32_t)>(0x002a93d0)(&title_global<char>(0x0082ded4), v);
    };
    const auto set_vm = [](uint32_t vm, const char* value) {
        TitleRegs regs{};
        regs.eax = addr(value);
        const uint32_t args[1] = {vm};
        cg_call_regs(0x0029fe40, &regs, args, 1);  // Cvar_SetByVmIndex
    };
    while (processed < title_global<int32_t>(0x00971a9c)) {
        uint8_t* dest = title_global<uint32_t>(0x00971aa8) == 0x00971ab0 ? &title_global<uint8_t>(0x00971ff0)
                                                                           : &title_global<uint8_t>(0x00971ab0);
        ++processed;
        TitleRegs regs{};
        regs.ecx = static_cast<uint32_t>(processed);
        regs.edx = addr(dest);
        if (!cg_call_regs(0x002ac510, &regs, nullptr, 0)) continue;
        const uint8_t* snap = title_global<uint8_t*>(0x00971aa8);
        if (snap && !title_global<int32_t>(0x00947fa4) && *reinterpret_cast<const int32_t*>(snap + 0x90)) {
            set_vm(0x00947f98, va_int(*reinterpret_cast<const int32_t*>(snap + 0x90)));
            set_vm(0x0094c230, va_int(cg_time()));
        }
        return dest;
    }
    return nullptr;
}

namespace {

int32_t wrapped_sub(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

}  // namespace

// A callee stores a stack address (0x100006a8 differed by the C frame's depth in 4 of 128
// tests): the entry moves the C frame below a gap, the calls run at the original's ESP
// (entry - 4: its one push) through their title addresses, and the first snapshot's tail
// jump to CG_TransitionSnapshot is returned to the entry, which jumps there at the entry ESP.
PLATFORM_ASM_ENTRY(CG_ProcessSnapshots_entry,
                   "movl %esp, %edx\n"
                   "subl $0x10000, %esp\n"
                   "pushl %edx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_ProcessSnapshots) "addl $4, %esp\n"
                   "popl %esp\n"
                   "testl %eax, %eax\n"
                   "jz 1f\n"
                   "jmp *%eax\n"
                   "1:\n"
                   "ret")

uint32_t XB_CDECL CG_ProcessSnapshots(uint32_t entry_esp) {
    // Q3's CG_ProcessSnapshots: the latest snapshot number (0x0096016c; going down is an
    // error), the first snapshot set up (0x0027e2f0), then snapshots read and transitioned
    // until cg.time lies between the current and the next one; cg.frameInterpolation.
    title_global<uint32_t>(0x00971aa0) = title_global<uint32_t>(0x00960168);
    const int32_t n = title_global<int32_t>(0x0096016c);
    int32_t& latest = title_global<int32_t>(0x00971a9c);
    if (n != latest) {
        if (n < latest) CG_Error(&title_global<char>(0x00838a94));
        latest = n;
    }
    const uint32_t top = entry_esp - 4;
    const auto call = [top](uint32_t function, uint32_t value) {
        TitleRegs regs{};
        regs.eax = value;
        regs.esi = value;
        return title_call_at_regs(top, function, &regs, 0, nullptr);
    };
    uint8_t*& snap = title_global<uint8_t*>(0x00971aa8);
    uint8_t*& next = title_global<uint8_t*>(0x00971aac);
    if (!snap) {
        const uint32_t first = call(0x0027c4f0, 0);  // CG_ReadNextSnapshot
        call(0x0027e2f0, first);                     // CG_SetInitialSnapshot
        call(0x0028aaa0, first);                     // CG_SetNextSnap
        return 0x0028abd0;                           // CG_TransitionSnapshot (tail jump)
    }
    const int32_t delta = at<int32_t>(next, 4) - at<int32_t>(snap, 4);
    float& interpolation = title_global<float>(0x00972530);
    if (delta == 0) {
        interpolation = 0.0f;
    } else {
        interpolation = static_cast<float>(cg_time() - at<int32_t>(snap, 4)) / static_cast<float>(delta);
        if (0.0f > interpolation) interpolation = 0.0f;
    }
    for (;;) {
        if (next == snap) {
            uint8_t* s = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(call(0x0027c4f0, 0)));
            if (!s) return 0;
            if (wrapped_sub(at<int32_t>(s, 4), at<int32_t>(snap, 4)) < 0) CG_Error(&title_global<char>(0x00838a64));
            call(0x0028aaa0, addr(s));  // CG_SetNextSnap
        }
        if (wrapped_sub(cg_time(), at<int32_t>(snap, 4)) >= 0 && wrapped_sub(cg_time(), at<int32_t>(next, 4)) < 0) return 0;
        call(0x0028abd0, 0);  // CG_TransitionSnapshot
    }
}

}  // namespace game
