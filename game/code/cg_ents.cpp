#include "cg_ents.h"

#include "cg_view.h"
#include "cgame.h"
#include "dobj.h"
#include "qcommon.h"
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

}  // namespace game
