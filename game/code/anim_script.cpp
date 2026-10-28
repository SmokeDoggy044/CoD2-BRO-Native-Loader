#include "anim_script.h"

#include <cstring>

#include "g_utils.h"
#include "nal_anim.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t addr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
template <typename T>
T* ptr(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t& word(uint32_t address) { return *ptr<uint32_t>(address); }
float& flt(uint32_t address) { return *ptr<float>(address); }

// The first level of detail a model has (models +0x24.., no bound as in the original).
uint32_t first_lod(uint32_t model) {
    uint32_t i = 0;
    while (!word(model + 0x24 + i * 4)) ++i;
    return word(model + 0x24 + i * 4);
}

// AnimScript_Update's frame: `s` is the original's ESP after its three pushes. Each call
// runs at that ESP (less the words still pushed) with the original's registers, and the
// original's return address is put back where the call left ours, so the dead stack
// below looks as the original leaves it.
struct Frame {
    uint32_t s;
    uint32_t call(uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count, const uint32_t* args,
                  uint32_t outstanding = 0) const {
        const uint32_t top = s - outstanding * 4;
        const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
        word(top - count * 4 - 4) = ret;
        return result;
    }
};

}  // namespace

PLATFORM_ASM_ENTRY(AnimWeights_SetAll_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(AnimWeights_SetAll) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL AnimWeights_SetAll(uint8_t* weights, float value) {
    int32_t i = 0;
    while (i < *reinterpret_cast<const int32_t*>(*reinterpret_cast<uint8_t* const*>(weights + 4) + 0x7c)) {
        (*reinterpret_cast<float* const*>(weights + 8))[i] = value;
        ++i;
    }
    return i;
}

uint8_t* XB_REGS1 XB_CDECL NAL_AsGenericClip(uint8_t* clip) {
    if (clip && *reinterpret_cast<const uint32_t*>(clip) == title_global<uint32_t>(0x00903fdc)) return clip;
    return nullptr;
}

namespace {

float* weight_ref(const uint32_t* ref, const uint8_t* weights, uint32_t dummy) {
    if (!ref[0]) return ptr<float>(dummy);
    const int32_t index = *reinterpret_cast<const int32_t*>(ptr<uint8_t>(ref[1]) + 0x24) + static_cast<int32_t>(ref[2]);
    return *reinterpret_cast<float* const*>(weights + 8) + index;
}

}  // namespace

float* XB_REGS1 XB_STDCALL AnimWeightRef_GetA(const uint32_t* ref, const uint8_t* weights) {
    return weight_ref(ref, weights, 0x0098ad20);
}

float* XB_REGS1 XB_STDCALL AnimWeightRef_GetB(const uint32_t* ref, const uint8_t* weights) {
    return weight_ref(ref, weights, 0x0098ad1c);
}

PLATFORM_ASM_ENTRY(AnimScript_Update_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl %ecx\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(AnimScript_Update) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret $0x14")

void XB_CDECL AnimScript_Update(uint32_t entry, uint8_t* self, uint8_t* track, uint32_t time, uint32_t previous,
                                float elapsed, uint32_t, uint32_t ebx, uint32_t esi, uint32_t edi) {
    // push ebp; and esp,-16; sub esp,0x124; push ebx/esi/edi.
    const Frame f{((entry - 4) & ~15u) - 0x130};
    const uint32_t state = f.s + 0x20;    // AnimState
    const uint32_t record = f.s + 0x90;   // root {quaternion, position}; the "camera" name first
    const uint32_t temp = f.s + 0x80;     // the blend's AnimState, later the angles
    const uint32_t local = f.s + 0x40;    // 4x4
    const uint32_t product = f.s + 0xb0;  // 4x4 / axis / 4x3
    const uint32_t root = f.s + 0xf0;     // 4x4
    const uint32_t angles = f.s + 0x80;
    const uint32_t self_a = addr(self);
    const uint32_t track_a = addr(track);
    const uint32_t generic = title_global<uint32_t>(0x00903fdc);
    const uint8_t saved = title_global<uint8_t>(0x0095f5d0);
    title_global<uint8_t>(0x0095f5d1) = 0;
    title_global<uint8_t>(0x0095f5d0) = 0;
    const uint32_t vtable = word(track_a);
    const uint32_t sample = word(vtable + 4);
    float time_f;
    float previous_f;
    std::memcpy(&time_f, &time, 4);  // std::bit_cast is an out-of-line call in -O0 builds
    std::memcpy(&previous_f, &previous, 4);

    if (self[0x28] & 1) {
        // The camera: the skeleton named "camera" from the stream, its root into 0x00960040.
        const uint32_t name_arg[] = {0x00832a58};
        f.call(0x0001e195, 0x00011060, TitleRegs{saved, ebx, record, 0, esi, edi}, 1, name_arg);  // HashStr_Init
        const uint32_t zone = word(title_global<uint32_t>(0x0095f638) + 0x2c);
        const uint32_t streams = title_global<uint32_t>(0x0095f630);
        const uint32_t find_args[] = {streams, zone, record, self_a & ~0xffu};
        const uint32_t found = f.call(0x0001e1bf, 0x002901f0, TitleRegs{zone & ~0xffu, ebx, record, streams, esi, edi}, 4,
                                      find_args);  // Stream_FindSkel
        const uint32_t clip = found && word(found) == generic ? found : 0;
        const uint32_t init_args[] = {clip, 0};
        f.call(0x0001e1df, 0x00087ad0, TitleRegs{found, ebx, state, 0, clip, edi}, 2, init_args);  // NAL_AnimState_Init
        const uint32_t sample_args[] = {time, previous, state, clip + 0xc8, 0};
        f.call(0x0001e1fd, sample, TitleRegs{time, ebx, track_a, vtable, clip + 0xc8, edi}, 5, sample_args);
        title_global<uint8_t>(0x00935538) = 1;
        const uint32_t root_args[] = {state, 0x00960040};
        f.call(0x0001e217, 0x00086d30, TitleRegs{0, ebx, word(state), 0, clip + 0xc8, edi}, 2,
               root_args);  // NAL_GenericAnim_GetRootTransform
        f.call(0x0001eb17, 0x00086da0, TitleRegs{0, ebx, state, 0, clip + 0xc8, edi}, 0, nullptr);  // NAL_AnimState_Destroy
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = saved;
        return;
    }

    // The entity (handle: index in the low 12 bits, generation above). The early returns
    // leave 0x0095f5d0 cleared, as in the original.
    const uint32_t handle = *reinterpret_cast<const uint32_t*>(self + 0x2c);
    const uint32_t index = handle & 0xfff;
    if (static_cast<int32_t>(index) >= 0x540) return;
    if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    const uint32_t ent = title_global<uint32_t>(0x0097a968 + index * 8);
    if (!ent) return;
    const uint32_t dobj = word(ent + 0x22c);
    uint32_t bones = 0;
    uint32_t clip;
    if (dobj) {
        bones = word(dobj + 0x20);
        if (!bones) return;
        clip = word(word(first_lod(word(dobj + 0x38)) + 8) + 0x38);
        if (!clip || word(clip) != generic) {
            // The model's skeleton becomes the track clip's.
            word(word(first_lod(word(dobj + 0x38)) + 8) + 0x38) = word(word(track_a + 0x10) + 0x30);
            clip = addr(NAL_AsGenericClip(ptr<uint8_t>(word(word(first_lod(word(dobj + 0x38)) + 8) + 0x38))));
            word(f.s - 4) = 0x0001e2f0;
        }
    } else {
        clip = word(word(track_a + 0x10) + 0x30);
        if (clip && word(clip) != generic) clip = 0;
    }
    const uint32_t from_args[] = {clip + 0xc8, 1};
    f.call(0x0001e31e, 0x00087bb0, TitleRegs{clip, clip + 0xc8, state, 0, ent, dobj}, 2,
           from_args);  // NAL_AnimState_InitFrom

    if (!dobj) {
        const uint32_t sample_args[] = {time, previous, state, clip + 0xc8, 0};
        f.call(0x0001e625, sample, TitleRegs{time, clip + 0xc8, track_a, vtable, ent, 0}, 5, sample_args);
    } else {
        word(dobj + 0x98) = 0xffffffff;
        uint32_t reg_ebx = 0;  // EBX as the original's loop leaves it
        const uint8_t* const d = ptr<uint8_t>(dobj);
        for (int32_t i = 0; i < d[0x2e]; ++i) {
            if (i != 0) {
                const uint8_t parent = d[0x58 + i];
                const uint32_t attach = parent == 0xff ? 0 : bones + parent * 0x40 + 0x30;
                const uint32_t matrices = bones + d[0x60 + i] * 0x40 + 0x30;
                const uint32_t model = word(dobj + 0x38 + i * 4);
                const uint32_t calc_args[] = {model, matrices, attach};
                f.call(0x0001e37e, 0x0023df00, TitleRegs{static_cast<uint32_t>(i), reg_ebx, matrices, model, ent, dobj}, 3,
                       calc_args);  // XModel_CalcSkeleton
                continue;
            }
            word(state + 4) = 0;
            const uint32_t sample_args[] = {time, previous, state, clip + 0xc8, 0};
            f.call(0x0001e3ac, sample, TitleRegs{time, 0, track_a, vtable, ent, dobj}, 5, sample_args);
            const uint32_t masked = word(ent + 0x2a4) & 0x7bffffff;
            word(ent + 0x2a4) = masked;
            bool off = true;
            const uint32_t weights = word(self_a + 0xc8);
            if (word(dobj + 0x68) && weights) {
                reg_ebx = self_a;
                // Blended in over +0xcc seconds, out over the last +0xd0 seconds of the
                // animation (+4: +4 bit 1 = no blend out, +0x3c its length).
                const float one = title_global<float>(0x0083eb20);
                float factor = 0.0f;
                const float in = flt(self_a + 0xcc);
                bool have = false;
                if (in > elapsed) {
                    factor = elapsed / in;
                    have = true;
                } else {
                    const uint32_t anim = word(self_a + 4);
                    const float out = flt(self_a + 0xd0);
                    if (!((word(anim + 4) >> 1) & 1) && !(out == 0.0f) && elapsed > flt(anim + 0x3c) - out) {
                        factor = (flt(anim + 0x3c) - elapsed) / out;
                        have = true;
                    }
                }
                if (have) {
                    off = false;
                    bool apply;
                    if (0.0f > factor) {
                        factor = 0.0f;
                        apply = true;
                    } else if (factor > one) {
                        apply = false;
                    } else if (factor == one) {
                        apply = false;
                    } else {
                        apply = true;
                    }
                    if (apply) {
                        const uint32_t temp_args[] = {clip, 0};
                        f.call(0x0001e496, 0x00087ad0, TitleRegs{weights, self_a, temp, 0, ent, dobj}, 2,
                               temp_args);  // NAL_AnimState_Init
                        AnimWeights_SetAll(ptr<uint8_t>(word(self_a + 0xc8)), factor);
                        word(f.s - 4) = 0x0001e4a7;
                        word(f.s - 8) = ent;  // its push esi
                        if (word(self_a + 0xa8)) {
                            const uint32_t w = word(self_a + 0xc8);
                            const uint32_t ref_args[] = {w};
                            flt(f.call(0x0001e4c3, 0x00023150, TitleRegs{self_a + 0xa8, self_a, word(self_a + 0xa8), w, ent, dobj},
                                       1, ref_args)) = one;
                        }
                        if (word(self_a + 0xb8)) {
                            const uint32_t w = word(self_a + 0xc8);
                            const uint32_t ref_args[] = {w};
                            flt(f.call(0x0001e4eb, 0x00023180, TitleRegs{self_a + 0xb8, self_a, w, 0, ent, dobj}, 1,
                                       ref_args)) = one;
                        }
                        const uint32_t blend_args[] = {temp, word(dobj + 0x68), state};
                        f.call(0x0001e513, 0x000879a0, TitleRegs{word(dobj + 0x68), self_a, word(self_a + 0xc8), state, ent, dobj},
                               3, blend_args);  // NAL_AnimState_BlendWeighted
                        const uint32_t copy_args[] = {temp};
                        f.call(0x0001e524, 0x00086e50, TitleRegs{0, self_a, state, temp, ent, dobj}, 1,
                               copy_args);  // NAL_AnimState_Copy
                        f.call(0x0001e530, 0x00086da0, TitleRegs{0, self_a, temp, 0, ent, dobj}, 0,
                               nullptr);  // NAL_AnimState_Destroy
                    }
                }
            }
            if (off) word(ent + 0x2a4) = masked | 0x84000000;
            // The pose, when the model has the bones the state's range needs.
            const uint32_t parts = word(first_lod(word(dobj + 0x38)) + 8);
            const uint32_t count = parts ? word(parts + 0x10) : 0;
            reg_ebx = word(word(state) + 0x64);
            const uint32_t range = word(state + 4);
            if (count >= word(reg_ebx + range * 20)) {
                const uint8_t parent = d[0x58];
                const uint32_t attach = parent == 0xff ? 0 : bones + parent * 0x40 + 0x30;
                const uint32_t matrices = bones + d[0x60] * 0x40 + 0x30;
                const uint32_t pose_args[] = {state, matrices, range};
                f.call(0x0001e5d5, 0x000886f0, TitleRegs{state, attach, word(state), matrices, ent, dobj}, 3,
                       pose_args);  // NAL_GenericAnim_EvaluatePose
                reg_ebx = attach;
                if (attach) {
                    const uint32_t model = word(dobj + 0x38);
                    const uint32_t transform_args[] = {model, matrices, attach};
                    f.call(0x0001e5f3, 0x00242540, TitleRegs{matrices, attach, model, bones, ent, dobj}, 3,
                           transform_args);  // XModel_TransformBones
                }
            }
        }
    }

    // Root motion moves the entity.
    const uint32_t root_args[] = {state, record};
    f.call(0x0001e63d, 0x00086d30, TitleRegs{record, 0, word(state), 0, ent, dobj}, 2,
           root_args);  // NAL_GenericAnim_GetRootTransform
    const float one = title_global<float>(0x0083eb20);
    float* const m = ptr<float>(local);
    float* const p = ptr<float>(product);
    float* const r = ptr<float>(root);
    float origin[3];
    if ((word(word(track_a + 0x10) + 0x34) >> 1) & 1) {
        // The clip has root motion: it is the entity's placement.
        f.call(0x0001e9ad, 0x0001cbe0, TitleRegs{root, 0, record, 0, ent, dobj}, 0, nullptr);  // QuatTrans_ToMat44
        origin[0] = flt(record + 0x10);
        origin[1] = flt(record + 0x14);
        origin[2] = flt(record + 0x18);
        f.call(0x0001e9ed, 0x002a26a0, TitleRegs{angles, 0, root, 0, ent, dobj}, 0, nullptr);  // MatrixToAngles
    } else if (const uint32_t link = word(ent + 0x3cc)) {
        // Tag-linked: the root moves the link's local matrix (4x3 at +0x10).
        f.call(0x0001e672, 0x0001cbe0, TitleRegs{root, 0, record, 0, ent, dobj}, 0, nullptr);  // QuatTrans_ToMat44
        m[11] = 0.0f;
        m[7] = 0.0f;
        m[3] = 0.0f;
        m[15] = one;
        const int rows[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        for (int k = 0; k < 12; ++k) m[rows[k]] = flt(link + 0x10 + k * 4);
        const uint32_t mul_args[] = {product, root};
        f.call(0x0001e73b, 0x0001c5b0, TitleRegs{product, 0, local, root, ent, link}, 2,
               mul_args);  // Mat44_MultiplyInPlace
        for (int k = 0; k < 12; ++k) flt(word(ent + 0x3cc) + 0x10 + k * 4) = m[rows[k]];
        const uint32_t parent_args[] = {local};
        const uint32_t current = word(ent + 0x3cc);
        f.call(0x0001e80d, 0x002df3e0, TitleRegs{ent, 0, current, current, ent, link}, 1,
               parent_args);  // G_GetTagParentMatrix
        const uint32_t multiply_args[] = {word(ent + 0x3cc) + 0x10};
        f.call(0x0001e827, 0x002954a0, TitleRegs{local, 0, word(ent + 0x3cc) + 0x10, product, ent, link}, 1, multiply_args,
               1);  // MatrixMultiply43
        origin[0] = p[9];
        origin[1] = p[10];
        origin[2] = p[11];
        f.call(0x0001e865, 0x002a27f0, TitleRegs{angles, 0, product, product, ent, link}, 0, nullptr);  // AxisToAngles
    } else {
        // The delta applied to the entity's current axis and origin.
        f.call(0x0001e86f, 0x0001cbe0, TitleRegs{root, 0, record, 0, ent, dobj}, 0, nullptr);  // QuatTrans_ToMat44
        const uint32_t axis_args[] = {product};
        f.call(0x0001e882, 0x002a4610, TitleRegs{ent + 0x150, 0, 0, product, ent, dobj}, 1,
               axis_args);  // AnglesToAxisLeft
        m[11] = 0.0f;
        m[7] = 0.0f;
        m[3] = 0.0f;
        m[15] = one;
        m[0] = p[0];
        m[1] = p[1];
        m[2] = p[2];
        m[4] = p[3];
        m[5] = p[4];
        m[6] = p[5];
        m[8] = p[6];
        m[9] = p[7];
        m[10] = p[8];
        m[12] = flt(ent + 0x140);
        m[13] = flt(ent + 0x144);
        m[14] = flt(ent + 0x148);
        const uint32_t mul_args[] = {product, local};
        f.call(0x0001e978, 0x0001c5b0, TitleRegs{local, 0, root, 0, ent, dobj}, 2, mul_args);  // Mat44_MultiplyInPlace
        origin[0] = r[12];
        origin[1] = r[13];
        origin[2] = r[14];
        f.call(0x0001e9ed, 0x002a26a0, TitleRegs{angles, 0, root, 0, ent, dobj}, 0, nullptr);  // MatrixToAngles
    }
    const float* const a = ptr<float>(angles);
    const float ax = a[0], ay = a[1], az = a[2];
    flt(ent + 0x20) = origin[0];
    flt(ent + 0x24) = origin[1];
    flt(ent + 0x28) = origin[2];
    word(ent + 0x14) = 0;
    word(ent + 0x18) = 0;
    word(ent + 0x1c) = 0;
    flt(ent + 0x34) = 0.0f;
    flt(ent + 0x30) = 0.0f;
    flt(ent + 0x2c) = 0.0f;
    flt(ent + 0x140) = origin[0];
    flt(ent + 0x144) = origin[1];
    flt(ent + 0x148) = origin[2];
    flt(ent + 0x44) = ax;
    flt(ent + 0x48) = ay;
    flt(ent + 0x4c) = az;
    word(ent + 0x38) = 0;
    word(ent + 0x3c) = 0;
    word(ent + 0x40) = 0;
    flt(ent + 0x58) = 0.0f;
    flt(ent + 0x54) = 0.0f;
    flt(ent + 0x50) = 0.0f;
    flt(ent + 0x150) = ax;
    flt(ent + 0x154) = ay;
    flt(ent + 0x158) = az;
    f.call(0x0001eaa2, 0x002d7fe0, TitleRegs{angles, 0, 0, 0, ent, ent}, 0, nullptr);  // SV_LinkEntity
    if (!((word(word(track_a + 0x10) + 0x34) >> 1) & 1)) {
        // Entities linked to this one follow it.
        uint32_t linked = word(ent + 0x3d0);
        while (linked) {
            const uint32_t next = word(word(linked + 0x3cc) + 4);
            f.call(0x0001eace, 0x002e6490, TitleRegs{linked, 0, 0, word(linked + 0x3cc), ent, next}, 0,
                   nullptr);  // G_GeneralLink
            linked = next;
        }
    }
    // Notetracks {notify, time, ?} passed between the previous and the current time.
    uint32_t note = word(self_a + 0x30) + 4;
    while (word(note)) {
        const float at = flt(note + 4);
        if (time_f >= at && at >= previous_f) {
            const uint32_t notify_args[] = {word(note)};
            f.call(0x0001eb04, 0x002b75c0, TitleRegs{f.s - 4, 0, word(note), 0, ent, note}, 1, notify_args);  // Scr_Notify
        }
        note += 12;
    }
    f.call(0x0001eb17, 0x00086da0, TitleRegs{0, 0, state, 0, ent, note}, 0, nullptr);  // NAL_AnimState_Destroy
    title_global<uint8_t>(0x0095f5d1) = 0;
    title_global<uint8_t>(0x0095f5d0) = saved;
}

}  // namespace game

namespace game {

namespace {

// NAL_Clip_FindPosOriChannel / NAL_Clip_FindFloat3Channel: the first channel on bone
// `bone` named `name` whose object's slot 0 gives the class `kind`; out = {clip, group,
// channel in group}; out[1..3] start as the title's never-set locals (read by the entries).
void find_typed_channel(uint8_t* generic, uint32_t* out, const uint32_t* bone, const uint32_t* name, uint32_t kind,
                        uint32_t stale1, uint32_t stale2, uint32_t stale3) {
    out[0] = 0;
    out[1] = stale1;
    out[2] = stale2;
    out[3] = stale3;
    int32_t channel = 0;
    for (int32_t g = 0; g < static_cast<int32_t>(word(addr(generic) + 0x84)); ++g) {
        for (int32_t k = 0; k < static_cast<int32_t>(word(word(addr(generic) + 0x88) + g * 0x30 + 0x28)); ++k, ++channel) {
            const uint32_t* record = ptr<uint32_t>(word(addr(generic) + 0x80) + channel * 0x28);
            int32_t i = 0;
            while (i < 8 && name[i] == record[i]) ++i;
            if (i < 8) continue;
            const uint32_t* bone_name = ptr<uint32_t>(word(addr(generic) + 0x74) + record[8] * 0x30);
            i = 0;
            while (i < 8 && bone[i] == bone_name[i]) ++i;
            if (i < 8) continue;
            const uint32_t object = word(word(addr(generic) + 0x88) + g * 0x30 + 0x20);
            if (title_function<uint32_t XB_THISCALL(uint32_t)>(word(word(object)))(object) != kind) continue;
            out[0] = addr(generic);
            out[1] = word(addr(generic) + 0x88) + g * 0x30;
            out[2] = static_cast<uint32_t>(k);
            return;
        }
    }
}

// A title channel finder (generic clip in ESI, callee pops 12) by address.
extern "C" PLATFORM_HIDDEN void XB_CDECL anim_find_channel_call(uint32_t function, uint8_t* generic, uint32_t* out,
                                                               const uint32_t* bone, const uint32_t* name);
PLATFORM_ASM_ENTRY(anim_find_channel_call,
                   "pushl %esi\n"
                   "movl 12(%esp), %esi\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "movl 20(%esp), %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

uint32_t* hash_str(uint32_t* out, uint32_t text) {
    return title_function<uint32_t* XB_THISCALL(uint32_t*, uint32_t)>(0x00011060)(out, text);  // HashStr_Init
}

}  // namespace

// (generic clip in ESI; out, bone name, channel name; callee pops 12).
PLATFORM_ASM_ENTRY(NAL_Clip_FindPosOriChannel_entry,
                   "pushl -4(%esp)\n"
                   "pushl -4(%esp)\n"
                   "pushl -4(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(NAL_Clip_FindPosOriChannel) "addl $28, %esp\n"
                   "ret $12")

void XB_CDECL NAL_Clip_FindPosOriChannel(uint8_t* generic, uint32_t* out, const uint32_t* bone, const uint32_t* name, uint32_t stale1,
                                         uint32_t stale2, uint32_t stale3) {
    find_typed_channel(generic, out, bone, name, 0x00909824, stale1, stale2, stale3);
}

PLATFORM_ASM_ENTRY(NAL_Clip_FindFloat3Channel_entry,
                   "pushl -4(%esp)\n"
                   "pushl -4(%esp)\n"
                   "pushl -4(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl 24(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(NAL_Clip_FindFloat3Channel) "addl $28, %esp\n"
                   "ret $12")

void XB_CDECL NAL_Clip_FindFloat3Channel(uint8_t* generic, uint32_t* out, const uint32_t* bone, const uint32_t* name, uint32_t stale1,
                                         uint32_t stale2, uint32_t stale3) {
    find_typed_channel(generic, out, bone, name, 0x00909820, stale1, stale2, stale3);
}

// (weights in ESI, generic clip in EAX).
PLATFORM_ASM_ENTRY(AnimWeights_Construct_entry,
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(AnimWeights_Construct) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL AnimWeights_Construct(uint8_t* weights, uint8_t* generic) {
    // vtable 0x00832758, the clip and one zeroed float per channel (+0x7c).
    word(addr(weights) + 4) = addr(generic);
    word(addr(weights)) = 0x00832758;
    float* values = static_cast<float*>(Mem_AlignedAlloc(word(addr(generic) + 0x7c) * 4, 4, 0));
    word(addr(weights) + 8) = addr(values);
    const uint32_t count = word(word(addr(weights) + 4) + 0x7c);
    for (uint32_t i = 0; i < count; ++i) word(addr(values) + i * 4) = 0;
    return weights;
}

// (this in EBX, kind in EAX, name {HashStr} in ECX, blend-in time in XMM0; blend-out time;
// callee pops 4).
PLATFORM_ASM_ENTRY(AnimScript_Construct_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(AnimScript_Construct) "addl $20, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL AnimScript_Construct(uint8_t* self, uint32_t kind, const uint32_t* name, float blend_in, uint32_t blend_out) {
    // A script on the entity whose +0x288 matches the name's hash (handle +0x2c, flag
    // 0x40000000 set on it): its clip's root channel ("Trajectory" on "fakeroot" or
    // "tag_origin", +0xa8), the pelvis position channel (+0xb8) and channel weights
    // (+0xc8). The name "camera" is the camera kind (+0x28 bit 0).
    const uint32_t s = addr(self);
    word(s) = 0x00832624;
    word(s + 4) = kind;
    for (uint32_t i = 0; i < 8; ++i) word(s + 8 + i * 4) = name[i];
    word(s + 0x28) = 0;
    word(s + 0x2c) = 0;
    word(s + 0x30) = 0;
    word(s + 0x40) = 0;
    word(s + 0xa8) = 0;
    word(s + 0xb8) = 0;
    flt(s + 0xcc) = blend_in;
    word(s + 0xc8) = 0;
    word(s + 0xd0) = blend_out;
    uint32_t camera[8];
    const uint32_t* hash = hash_str(camera, 0x00832a58);  // "camera"
    uint32_t i = 0;
    while (i < 8 && name[i] == hash[i]) ++i;
    if (i == 8) {
        word(s + 0x28) |= 1;
        word(s + 0x2c) = 0;
        return self;
    }
    const uint32_t key = Com_HashKeyNoCase(0, ptr<const char>(s + 0xc));
    uint8_t* ent = G_FindEntityByIntField(ptr<uint8_t>(0x0097a8c0), 0x288, static_cast<int32_t>(key));
    if (!ent) return self;
    word(s + 0x2c) = word(addr(ent) + 0x224);
    if (const uint32_t model = word(addr(ent) + 0x22c)) {
        const uint32_t lods = word(model + 0x38);
        uint32_t k = 0;
        while (!word(lods + 0x24 + k * 4)) ++k;
        const uint32_t clip = word(word(word(lods + 0x24 + k * 4) + 8) + 0x38);
        uint8_t* generic = clip && word(clip) == title_global<uint32_t>(0x00903fdc) ? ptr<uint8_t>(clip) : nullptr;
        if (word(word(addr(ent) + 0x22c) + 0x68)) {
            uint32_t a[8];
            uint32_t b[8];
            hash_str(a, 0x00832a4c);  // "Trajectory"
            hash_str(b, 0x00832b10);  // "fakeroot"
            anim_find_channel_call(0x00022eb0, generic, ptr<uint32_t>(s + 0xa8), b, a);
            if (!word(s + 0xa8)) {
                hash_str(b, 0x00832a4c);  // "Trajectory"
                hash_str(a, 0x00832b04);  // "tag_origin"
                anim_find_channel_call(0x00022eb0, generic, ptr<uint32_t>(s + 0xa8), a, b);
            }
            hash_str(b, 0x00832a40);  // "Position"
            hash_str(a, 0x00832a30);  // "Bip01 Pelvis"
            anim_find_channel_call(0x00023000, generic, ptr<uint32_t>(s + 0xb8), a, b);
            uint8_t* memory = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(0xc);  // Z_Malloc
            word(s + 0xc8) = memory ? addr(AnimWeights_Construct(memory, generic)) : 0;
        }
    }
    word(addr(ent) + 0x2a4) |= 0x40000000;
    return self;
}

void XB_THISCALL AnimScript_Destroy(uint8_t* self) {
    // The weights deleted, the entity's script flags cleared (handle +0x2c), the
    // notetracks (+0x30) released, then the script itself freed.
    const uint32_t s = addr(self);
    if (const uint32_t weights = word(s + 0xc8))
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(word(word(weights) + 4))(weights, 1);
    const uint32_t handle = word(s + 0x2c);
    const uint32_t slot = handle & 0xfff;
    if (slot < 0x540 && (handle >> 12) == word(0x0097a96c + slot * 8))
        if (const uint32_t ent = word(0x0097a968 + slot * 8)) word(ent + 0x2a4) &= 0x3bffffffu;
    if (const uint32_t notes = word(s + 0x30)) {
        const int32_t count = *ptr<int32_t>(notes - 4);
        for (int32_t k = count - 1; k >= 0; --k)
            title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(notes + k * 12);  // ScriptString_Release
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(notes - 4);               // Z_Free
    }
    word(s + 0x30) = 0;
    title_function<void XB_CDECL(uint32_t)>(0x000428d0)(s);  // Z_Free
}

}  // namespace game
