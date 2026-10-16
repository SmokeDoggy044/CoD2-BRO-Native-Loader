// Xbox shaders -> D3D9 shader model 3 bytecode. See d3d9_shader.h.
//
// Token formats: D3D9 driver documentation ("Direct3D Shader Codes"). NV2A vertex
// program fields: xemu (hw/xbox/nv2a/vsh.c). Register combiners: XDK D3DPIXELSHADERDEF
// and NVIDIA's NV_register_combiners.

#include "d3d9_shader.h"

#include <atomic>
#include <cstring>
#include <initializer_list>
#include <mutex>

#include "log.h"

namespace render::d3d9 {

namespace {

// --- Bytecode ---------------------------------------------------------------------------

enum RegisterType : uint32_t {
    kTemp = 0, kInput = 1, kConst = 2, kAddr = 3, kOutput = 6, kColorOut = 8, kSampler = 10,
};

enum Opcode : uint32_t {
    kMov = 1, kAdd = 2, kMad = 4, kMul = 5, kRcp = 6, kRsq = 7, kDp3 = 8, kDp4 = 9, kMin = 10, kMax = 11,
    kSlt = 12, kSge = 13, kExp = 14, kLog = 15, kLit = 16, kDst = 17, kLrp = 18, kFrc = 19, kDcl = 31,
    kAbs = 35, kMova = 46, kTexkill = 65, kTexld = 66, kDef = 81, kCmp = 88, kEnd = 0xFFFF,
};

enum Usage : uint32_t { kUsagePosition = 0, kUsageTexcoord = 5, kUsageColor = 10, kUsageFog = 11 };

constexpr uint32_t kVs30 = 0xFFFE0300;
constexpr uint32_t kPs30 = 0xFFFF0300;
constexpr uint32_t kTexldProject = 1u << 16;

// Write masks.
constexpr uint32_t kX = 1, kY = 2, kZ = 4, kW = 8, kXyz = 7, kAll = 15;

constexpr uint8_t swizzle(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    return static_cast<uint8_t>(x | y << 2 | z << 4 | w << 6);
}
constexpr uint8_t kIdentity = swizzle(0, 1, 2, 3);
constexpr uint8_t replicate(uint32_t component) { return static_cast<uint8_t>(component * 0x55); }

uint32_t register_bits(uint32_t type, uint32_t index) {
    return 0x80000000u | (type & 7) << 28 | ((type >> 3) & 3) << 11 | (index & 0x7FF);
}

struct Dst {
    uint32_t token;
};

struct Src {
    uint32_t token;
    bool relative = false;  // indexed by a0.x (vertex shader constants)

    Src operator-() const {
        Src s = *this;
        s.token ^= 1u << 24;  // D3DSPSM_NEG
        return s;
    }
    // Composes a swizzle on top of the current one.
    Src swz(uint8_t sw) const {
        Src s = *this;
        const uint32_t old = (token >> 16) & 0xFF;
        uint32_t composed = 0;
        for (uint32_t i = 0; i < 4; ++i) composed |= ((old >> (((sw >> (i * 2)) & 3) * 2)) & 3) << (i * 2);
        s.token = (token & ~0x00FF0000u) | composed << 16;
        return s;
    }
    Src x() const { return swz(replicate(0)); }
    Src y() const { return swz(replicate(1)); }
    Src z() const { return swz(replicate(2)); }
    Src w() const { return swz(replicate(3)); }
    // The first component, replicated (scalar operations).
    Src first() const { return swz(replicate(0)); }
};

Dst dst(uint32_t type, uint32_t index, uint32_t mask = kAll, bool saturate = false) {
    return {register_bits(type, index) | mask << 16 | (saturate ? 1u << 20 : 0)};
}
Dst sat(Dst d) { return {d.token | 1u << 20}; }
Src src(uint32_t type, uint32_t index, uint8_t sw = kIdentity) { return {register_bits(type, index) | uint32_t{sw} << 16}; }

Dst r_(uint32_t index, uint32_t mask = kAll) { return dst(kTemp, index, mask); }
Src r(uint32_t index) { return src(kTemp, index); }
Src c(uint32_t index) { return src(kConst, index); }

class Assembler {
public:
    explicit Assembler(uint32_t version) { code_.push_back(version); }

    void op(uint32_t opcode, Dst d, std::initializer_list<Src> sources, uint32_t controls = 0) {
        uint32_t length = 1;
        for (const Src& s : sources) length += s.relative ? 2 : 1;
        code_.push_back(opcode | controls | length << 24);
        code_.push_back(d.token);
        for (const Src& s : sources) {
            code_.push_back(s.token | (s.relative ? 1u << 13 : 0));
            if (s.relative) code_.push_back(register_bits(kAddr, 0) | uint32_t{replicate(0)} << 16);
        }
    }
    void mov(Dst d, Src a) { op(kMov, d, {a}); }
    void add(Dst d, Src a, Src b) { op(kAdd, d, {a, b}); }
    void mul(Dst d, Src a, Src b) { op(kMul, d, {a, b}); }
    void mad(Dst d, Src a, Src b, Src c_) { op(kMad, d, {a, b, c_}); }
    void max(Dst d, Src a, Src b) { op(kMax, d, {a, b}); }
    void min(Dst d, Src a, Src b) { op(kMin, d, {a, b}); }

    void dcl(uint32_t declaration, Dst d) {
        code_.push_back(kDcl | 2u << 24);
        code_.push_back(declaration);
        code_.push_back(d.token);
    }
    void dcl_usage(uint32_t usage, uint32_t index, Dst d) { dcl(0x80000000u | usage | index << 16, d); }
    void dcl_sampler(uint32_t texture_type, uint32_t index) {
        dcl(0x80000000u | texture_type << 27, dst(kSampler, index));
    }

    void def(uint32_t index, float x, float y, float z, float w) {
        code_.push_back(kDef | 5u << 24);
        code_.push_back(dst(kConst, index).token);
        for (float f : {x, y, z, w}) {
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            code_.push_back(bits);
        }
    }

    void texkill(Dst d) {
        code_.push_back(kTexkill | 1u << 24);
        code_.push_back(d.token);
    }

    std::vector<uint32_t> finish() {
        code_.push_back(kEnd);
        return std::move(code_);
    }

private:
    std::vector<uint32_t> code_;
};

void warn_once(std::once_flag& once, const char* message) {
    std::call_once(once, [message] { LOG_WARN("render", "%s", message); });
}

// --- Vertex programs ----------------------------------------------------------------------

// Temporaries: r0..r11 = NV2A R0..R11, r12 = R12 (oPos).
constexpr uint32_t kMacResult = 13, kIluResult = 14, kScratch = 15;
constexpr uint32_t kOutD0 = 20, kOutD1 = 21, kOutT0 = 22, kOutFog = 26, kOutPts = 27, kOutB0 = 28, kOutB1 = 29;
constexpr uint32_t kEpilogue = 30, kEpilogueW = 31;
// Constants after the NV2A slots and kVsScreenToClip.
constexpr uint32_t kVsDefaults = 200;  // (0, 0, 0, 1)
constexpr uint32_t kVsRccLimits = 201;
constexpr uint32_t kVsFogHelpers = 202;  // (16, -32, -1, -1.5)

// The output registers.
constexpr uint32_t kOPos = 0, kOD0 = 1, kOD1 = 2, kOT0 = 3, kOFog = 7;

// An instruction's fields (xemu's table: dword, shift, bits).
struct Instruction {
    const uint32_t* words;
    uint32_t get(uint32_t dword, uint32_t shift, uint32_t bits) const {
        return (words[dword] >> shift) & ((1u << bits) - 1);
    }
};

// NV2A write masks are x=8 y=4 z=2 w=1.
uint32_t d3d_mask(uint32_t nv_mask) {
    return (nv_mask & 8 ? kX : 0) | (nv_mask & 4 ? kY : 0) | (nv_mask & 2 ? kZ : 0) | (nv_mask & 1 ? kW : 0);
}

enum Mux : uint32_t { kMuxR = 1, kMuxV = 2, kMuxC = 3 };

Src vs_operand(const Instruction& in, uint32_t mux, uint32_t temp, bool negate, uint8_t sw, uint32_t attribute_mask) {
    Src s;
    switch (mux) {
        case kMuxR: s = r(temp); break;
        case kMuxV: {
            const uint32_t v = in.get(1, 9, 4);
            s = attribute_mask & (1u << v) ? src(kInput, v) : c(kVsDefaults);
            break;
        }
        case kMuxC:
            s = c(in.get(1, 13, 8));
            s.relative = in.get(3, 1, 1) != 0;
            break;
        default: s = c(kVsDefaults).x(); break;
    }
    s = s.swz(sw);
    return negate ? -s : s;
}

Src operand_a(const Instruction& in, uint32_t mask) {
    return vs_operand(in, in.get(2, 26, 2), in.get(2, 28, 4), in.get(1, 8, 1),
                      swizzle(in.get(1, 6, 2), in.get(1, 4, 2), in.get(1, 2, 2), in.get(1, 0, 2)), mask);
}
Src operand_b(const Instruction& in, uint32_t mask) {
    return vs_operand(in, in.get(2, 11, 2), in.get(2, 13, 4), in.get(2, 25, 1),
                      swizzle(in.get(2, 23, 2), in.get(2, 21, 2), in.get(2, 19, 2), in.get(2, 17, 2)), mask);
}
Src operand_c(const Instruction& in, uint32_t mask) {
    return vs_operand(in, in.get(3, 28, 2), in.get(2, 0, 2) << 2 | in.get(3, 30, 2), in.get(2, 10, 1),
                      swizzle(in.get(2, 8, 2), in.get(2, 6, 2), in.get(2, 4, 2), in.get(2, 2, 2)), mask);
}

enum MacOp : uint32_t {
    kMacNop, kMacMov, kMacMul, kMacAdd, kMacMad, kMacDp3, kMacDph, kMacDp4, kMacDst, kMacMin, kMacMax,
    kMacSlt, kMacSge, kMacArl,
};
enum IluOp : uint32_t { kIluNop, kIluMov, kIluRcp, kIluRcc, kIluRsq, kIluExp, kIluLog, kIluLit };

// The temporary an output address is kept in until the epilogue (0 = not kept).
uint32_t output_temp(uint32_t address) {
    switch (address) {
        case 0: return 12;
        case 3: return kOutD0;
        case 4: return kOutD1;
        case 5: return kOutFog;
        case 6: return kOutPts;
        case 7: return kOutB0;
        case 8: return kOutB1;
        case 9: case 10: case 11: case 12: return kOutT0 + (address - 9);
        default: return 0;
    }
}

void translate_instruction(Assembler& a, const Instruction& in, uint32_t attribute_mask) {
    static std::once_flag const_write_once;
    const uint32_t mac = in.get(1, 21, 4);
    const uint32_t ilu = in.get(1, 25, 3);
    const Src A = operand_a(in, attribute_mask), B = operand_b(in, attribute_mask), C = operand_c(in, attribute_mask);

    // Both units read their sources before either writes: results go to scratch first.
    const Dst mac_out = r_(kMacResult);
    switch (mac) {
        case kMacNop: break;
        case kMacMov: a.mov(mac_out, A); break;
        case kMacMul: a.mul(mac_out, A, B); break;
        case kMacAdd: a.add(mac_out, A, C); break;
        case kMacMad: a.mad(mac_out, A, B, C); break;
        case kMacDp3: a.op(kDp3, mac_out, {A, B}); break;
        case kMacDph:
            a.op(kDp3, r_(kScratch), {A, B});
            a.add(mac_out, r(kScratch), B.w());
            break;
        case kMacDp4: a.op(kDp4, mac_out, {A, B}); break;
        case kMacDst: a.op(kDst, mac_out, {A, B}); break;
        case kMacMin: a.min(mac_out, A, B); break;
        case kMacMax: a.max(mac_out, A, B); break;
        case kMacSlt: a.op(kSlt, mac_out, {A, B}); break;
        case kMacSge: a.op(kSge, mac_out, {A, B}); break;
        case kMacArl:  // a0.x = floor(A.x); mova rounds, so floor first
            a.op(kFrc, r_(kScratch, kX), {A.x()});
            a.add(r_(kScratch, kX), A.x(), -r(kScratch).x());
            a.op(kMova, dst(kAddr, 0, kX), {r(kScratch).x()});
            break;
        default: LOG_WARN("render", "vertex program: unknown MAC op %u", mac); break;
    }
    const Dst ilu_out = r_(kIluResult);
    switch (ilu) {
        case kIluNop: break;
        case kIluMov: a.mov(ilu_out, C); break;
        case kIluRcp: a.op(kRcp, ilu_out, {C.first()}); break;
        case kIluRcc:  // rcp clamped to +-1.884467e19
            a.op(kRcp, ilu_out, {C.first()});
            a.min(ilu_out, r(kIluResult), c(kVsRccLimits).x());
            a.max(ilu_out, r(kIluResult), c(kVsRccLimits).y());
            break;
        case kIluRsq: a.op(kRsq, ilu_out, {C.first()}); break;
        case kIluExp:  // like vs_1_1 expp: (2^floor(s), frac(s), 2^s, 1); titles use .y as a phase
            a.op(kFrc, r_(kScratch, kX), {C.first()});
            a.add(r_(kScratch, kY), C.first(), -r(kScratch).x());
            a.op(kExp, r_(kIluResult, kX), {r(kScratch).y()});
            a.mov(r_(kIluResult, kY), r(kScratch).x());
            a.op(kExp, r_(kIluResult, kZ), {C.first()});
            a.mov(r_(kIluResult, kW), c(kVsDefaults).w());
            break;
        case kIluLog:  // like vs_1_1 logp: (exponent, mantissa in [1, 2), log2|s|, 1)
            a.op(kAbs, r_(kScratch, kX), {C.first()});
            a.op(kLog, r_(kIluResult, kZ), {r(kScratch).x()});
            a.op(kFrc, r_(kScratch, kY), {r(kIluResult).z()});
            a.add(r_(kIluResult, kX), r(kIluResult).z(), -r(kScratch).y());
            a.op(kExp, r_(kScratch, kZ), {-r(kIluResult).x()});
            a.mul(r_(kIluResult, kY), r(kScratch).x(), r(kScratch).z());
            a.mov(r_(kIluResult, kW), c(kVsDefaults).w());
            break;
        case kIluLit: a.op(kLit, ilu_out, {C}); break;
        default: break;
    }

    const uint32_t out_r = in.get(3, 20, 4);
    const uint32_t mac_mask = d3d_mask(in.get(3, 24, 4));
    if (mac != kMacNop && mac != kMacArl && mac_mask) a.mov(r_(out_r, mac_mask), r(kMacResult));
    const uint32_t ilu_mask = d3d_mask(in.get(3, 16, 4));
    // With both units active the ILU's temporary destination is always R1.
    if (ilu != kIluNop && ilu_mask) a.mov(r_(mac != kMacNop ? 1 : out_r, ilu_mask), r(kIluResult));

    const uint32_t o_mask = d3d_mask(in.get(3, 12, 4));
    if (!o_mask) return;
    const bool from_ilu = in.get(3, 2, 1) != 0;
    if (in.get(3, 11, 1) == 0) {  // ORB = 0: writes a constant ("context") register
        warn_once(const_write_once, "vertex program writes constants: not supported");
        return;
    }
    if (const uint32_t temp = output_temp(in.get(3, 3, 8)))
        a.mov(r_(temp, o_mask), r(from_ilu ? kIluResult : kMacResult));
}

}  // namespace

std::vector<uint32_t> translate_vertex_program(const uint32_t (*instructions)[4], uint32_t count,
                                               uint32_t attribute_mask, FogMode fog) {
    Assembler a(kVs30);
    for (uint32_t v = 0; v < 16; ++v)
        if (attribute_mask & (1u << v)) a.dcl_usage(kUsageTexcoord, v, dst(kInput, v));
    a.dcl_usage(kUsagePosition, 0, dst(kOutput, kOPos));
    a.dcl_usage(kUsageColor, 0, dst(kOutput, kOD0));
    a.dcl_usage(kUsageColor, 1, dst(kOutput, kOD1));
    for (uint32_t t = 0; t < 4; ++t) a.dcl_usage(kUsageTexcoord, t, dst(kOutput, kOT0 + t));
    a.dcl_usage(kUsageFog, 0, dst(kOutput, kOFog, kX));
    a.def(kVsDefaults, 0.0f, 0.0f, 0.0f, 1.0f);
    a.def(kVsRccLimits, 1.884467e19f, -1.884467e19f, 0.0f, 0.0f);
    a.def(kVsFogHelpers, 16.0f, -32.0f, -1.0f, -1.5f);

    for (uint32_t t = 0; t <= 12; ++t) a.mov(r_(t), c(kVsDefaults).x());
    for (uint32_t t = kOutD0; t <= kOutB1; ++t) a.mov(r_(t), c(kVsDefaults));

    for (uint32_t i = 0; i < count; ++i) translate_instruction(a, Instruction{instructions[i]}, attribute_mask);

    // Screen space -> clip space: w' = w, or 1 where w is 0; xyz = ndc * w'.
    const Src pos = r(12);
    a.op(kAbs, r_(kEpilogueW, kX), {pos.w()});
    a.op(kSge, r_(kEpilogueW, kX), {-r(kEpilogueW).x(), c(kVsDefaults).x()});
    a.add(r_(kEpilogueW, kX), pos.w(), r(kEpilogueW).x());
    a.mad(r_(kEpilogue, kX | kY), pos, c(kVsScreenToClip), c(kVsScreenToClip + 1));
    a.mul(r_(kEpilogue, kZ), pos, c(kVsScreenToClip));
    a.mul(r_(kEpilogue, kXyz), r(kEpilogue), r(kEpilogueW).x());
    a.mov(r_(kEpilogue, kW), r(kEpilogueW).x());
    a.mov(dst(kOutput, kOPos), r(kEpilogue));
    a.mov(dst(kOutput, kOD0), r(kOutD0));
    a.mov(dst(kOutput, kOD1), r(kOutD1));
    for (uint32_t t = 0; t < 4; ++t) a.mov(dst(kOutput, kOT0 + t), r(kOutT0 + t));
    // Fog factor from the distance in oFog.x (xemu's formulas; D3D picks the params).
    const Src distance = r(kOutFog).x(), bias = c(kVsFogParams).x(), scale = c(kVsFogParams).y();
    const Src helpers = c(kVsFogHelpers);
    const Dst factor = r_(kEpilogue, kX);
    const Src f = r(kEpilogue).x();
    switch (fog) {
        case FogMode::Off: a.mov(factor, c(kVsDefaults).w()); break;
        case FogMode::Linear:  // bias + d * scale - 1
            a.mad(factor, distance, scale, bias);
            a.add(factor, f, helpers.z());
            break;
        case FogMode::Exp:  // bias + 2^(16 * d * scale) - 1.5
            a.mul(factor, distance, scale);
            a.mul(factor, f, helpers.x());
            a.op(kExp, factor, {f});
            a.add(factor, f, bias);
            a.add(factor, f, helpers.w());
            break;
        case FogMode::Exp2:  // bias + 2^(-32 * (d * scale)^2) - 1.5
            a.mul(factor, distance, scale);
            a.mul(factor, f, f);
            a.mul(factor, f, helpers.y());
            a.op(kExp, factor, {f});
            a.add(factor, f, bias);
            a.add(factor, f, helpers.w());
            break;
    }
    a.mov(dst(kOutput, kOFog, kX), f);
    return a.finish();
}

// --- Register combiners ------------------------------------------------------------------

namespace {

// Temporaries: r0 R0, r1 R1, r2..r5 T0..T3, r6 V0, r7 V1, r8.. scratch.
constexpr uint32_t kR0 = 0, kR1 = 1, kT0 = 2, kV0 = 6, kV1 = 7;
constexpr uint32_t kInA = 8, kInB = 9, kInC = 10, kInD = 11, kAb = 12, kCd = 13, kSum = 14, kMuxSel = 15;
constexpr uint32_t kEfProd = 16, kV1R0Sum = 17, kFinal = 18, kFinalD = 19, kFinalG = 20, kFog = 21;
constexpr uint32_t kFogInput = 6;  // v6: the fog factor
constexpr uint32_t kPsHelpers = 22;   // (0, 1, 0.5, 2)
constexpr uint32_t kPsHelpers2 = 23;  // (-1, 1/512, -0.5, -2)

// PS_REGISTER_*
enum CombinerRegister : uint32_t {
    kRegZero = 0, kRegC0 = 1, kRegC1 = 2, kRegFog = 3, kRegV0 = 4, kRegV1 = 5, kRegT0 = 8, kRegR0 = 12,
    kRegR1 = 13, kRegV1R0Sum = 14, kRegEfProd = 15,
};

struct CombinerContext {
    uint32_t stage;  // general combiner stage, or 8 for the final combiner
    bool unique_c0, unique_c1;
};

// A combiner register as a source (rgb, or alpha with its blue/alpha channel select).
Src combiner_register(uint32_t reg, const CombinerContext& ctx) {
    const bool final = ctx.stage >= 8;
    switch (reg) {
        case kRegC0: return c(final ? kPsFinalC0 : kPsStageC0 + (ctx.unique_c0 ? ctx.stage : 0));
        case kRegC1: return c(final ? kPsFinalC1 : kPsStageC1 + (ctx.unique_c1 ? ctx.stage : 0));
        case kRegFog: return r(kFog);
        case kRegV0: return r(kV0);
        case kRegV1: return r(kV1);
        case 8: case 9: case 10: case 11: return r(kT0 + (reg - 8));
        case kRegR0: return r(kR0);
        case kRegR1: return r(kR1);
        case kRegV1R0Sum: if (final) return r(kV1R0Sum); break;
        case kRegEfProd: if (final) return r(kEfProd); break;
        default: break;
    }
    return c(kPsHelpers).x();  // zero
}

// Computes an input variable (8 bits: register, channel, mapping) into `temp`'s rgb
// (xyz) or alpha (w).
void combiner_input(Assembler& a, uint32_t input, bool alpha_portion, uint32_t temp, const CombinerContext& ctx) {
    const bool alpha_channel = (input & 0x10) != 0;
    Src s = combiner_register(input & 0xF, ctx);
    if (alpha_portion)
        s = alpha_channel ? s.w() : s.z();
    else if (alpha_channel)
        s = s.w();
    const uint32_t mask = alpha_portion ? kW : kXyz;
    const Dst d = r_(temp, mask);
    const Src t = r(temp);
    const Src zero = c(kPsHelpers).x(), one = c(kPsHelpers).y(), half = c(kPsHelpers).z(), two = c(kPsHelpers).w();
    switch (input >> 5) {
        case 0: a.max(d, s, zero); break;  // UNSIGNED_IDENTITY: max(0, x)
        case 1:                            // UNSIGNED_INVERT: 1 - clamp(x)
            a.mov(sat(d), s);
            a.add(d, one, -t);
            break;
        case 2:  // EXPAND_NORMAL: 2 * max(0, x) - 1
            a.max(d, s, zero);
            a.mad(d, t, two, -one);
            break;
        case 3:  // EXPAND_NEGATE: -2 * max(0, x) + 1
            a.max(d, s, zero);
            a.mad(d, t, -two, one);
            break;
        case 4:  // HALFBIAS_NORMAL: max(0, x) - 0.5
            a.max(d, s, zero);
            a.add(d, t, -half);
            break;
        case 5:  // HALFBIAS_NEGATE: -max(0, x) + 0.5
            a.max(d, s, zero);
            a.add(d, -t, half);
            break;
        case 6: a.mov(d, s); break;   // SIGNED_IDENTITY
        default: a.mov(d, -s); break;  // SIGNED_NEGATE
    }
}

// PS_COMBINEROUTPUT mapping (bias, shift) and the general combiners' [-1, 1] range.
void combiner_output_mapping(Assembler& a, uint32_t temp, uint32_t mask, uint32_t outputs) {
    const Dst d = r_(temp, mask);
    const Src t = r(temp);
    if (outputs & (1u << 15)) a.add(d, t, c(kPsHelpers2).z());  // BIAS: - 0.5
    switch ((outputs >> 16) & 3) {
        case 1: a.mul(d, t, c(kPsHelpers).w()); break;  // * 2
        case 2: a.add(d, t, t), a.add(d, t, t); break;    // * 4
        case 3: a.mul(d, t, c(kPsHelpers).z()); break;   // / 2
        default: break;
    }
    a.max(d, t, c(kPsHelpers2).x());
    a.min(d, t, c(kPsHelpers).y());
}

// The temporary a combiner output register number lands in (0 = discard).
uint32_t combiner_dest(uint32_t reg) {
    switch (reg) {
        case kRegV0: return kV0;
        case kRegV1: return kV1;
        case 8: case 9: case 10: case 11: return kT0 + (reg - 8);
        case kRegR0: return kR0;
        case kRegR1: return kR1;
        default: return ~0u;
    }
}

void general_combiner_portion(Assembler& a, uint32_t inputs, uint32_t outputs, bool alpha) {
    const uint32_t mask = alpha ? kW : kXyz;
    const bool ab_dot = !alpha && (outputs & (1u << 13));
    const bool cd_dot = !alpha && (outputs & (1u << 12));
    a.op(ab_dot ? kDp3 : kMul, r_(kAb, mask), {r(kInA), r(kInB)});
    a.op(cd_dot ? kDp3 : kMul, r_(kCd, mask), {r(kInC), r(kInD)});
    if (outputs & (1u << 14))  // MUX: R0.alpha >= 0.5 ? CD : AB
        a.op(kCmp, r_(kSum, mask), {r(kMuxSel).x(), r(kCd), r(kAb)});
    else
        a.add(r_(kSum, mask), r(kAb), r(kCd));
    (void)inputs;
    for (uint32_t t : {kAb, kCd, kSum}) combiner_output_mapping(a, t, mask, outputs);
}

void write_portion(Assembler& a, uint32_t outputs, bool alpha) {
    const uint32_t mask = alpha ? kW : kXyz;
    const uint32_t regs[3] = {(outputs >> 4) & 0xF, outputs & 0xF, (outputs >> 8) & 0xF};  // AB, CD, SUM
    const uint32_t temps[3] = {kAb, kCd, kSum};
    for (int i = 0; i < 3; ++i)
        if (const uint32_t d = combiner_dest(regs[i]); d != ~0u) a.mov(r_(d, mask), r(temps[i]));
}

}  // namespace

std::vector<uint32_t> translate_pixel_shader(const PixelShaderKey& key) {
    static std::atomic<uint32_t> warned_modes{0};
    const XbPixelShaderDef& def = key.def;
    Assembler a(kPs30);
    a.def(kPsHelpers, 0.0f, 1.0f, 0.5f, 2.0f);
    a.def(kPsHelpers2, -1.0f, 1.0f / 512.0f, -0.5f, -2.0f);
    a.dcl_usage(kUsageColor, 0, dst(kInput, 0));
    a.dcl_usage(kUsageColor, 1, dst(kInput, 1));
    for (uint32_t t = 0; t < 4; ++t) a.dcl_usage(kUsageTexcoord, t, dst(kInput, 2 + t));
    for (uint32_t stage = 0; stage < 4; ++stage) {
        switch (texture_mode(def, stage)) {
            case kModeProject2D:
            case kModeDependentAR:
            case kModeDependentGB: a.dcl_sampler(2, stage); break;
            case kModeCubemap: a.dcl_sampler(3, stage); break;
            case kModeProject3D: a.dcl_sampler(4, stage); break;
            default: break;
        }
    }

    a.dcl_usage(kUsageFog, 0, dst(kInput, kFogInput, kX));
    a.mov(r_(kV0), src(kInput, 0));
    // FOG: rgb = fog colour, alpha = the fog factor.
    a.mov(r_(kFog, kXyz), c(kPsFogColor));
    a.mov(sat(r_(kFog, kW)), src(kInput, kFogInput).x());
    a.mov(r_(kV1), src(kInput, 1));
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const Src coord = src(kInput, 2 + stage);
        const Dst t = r_(kT0 + stage);
        switch (texture_mode(def, stage)) {
            case kModeNone: a.mov(t, c(kPsHelpers).x()); break;
            case kModeProject2D:
                a.mul(r_(kInA), coord, c(kPsTexcoordScale + stage));
                a.op(kTexld, t, {r(kInA), src(kSampler, stage)}, kTexldProject);
                break;
            case kModeCubemap:
            case kModeProject3D: a.op(kTexld, t, {coord, src(kSampler, stage)}); break;
            case kModePassthru: a.mov(sat(t), coord); break;
            case kModeDependentAR:  // coordinates = an earlier stage's (alpha, red)
            case kModeDependentGB: {  // or (green, blue)
                const Src in = r(kT0 + input_stage(def, stage));
                const uint8_t sw = texture_mode(def, stage) == kModeDependentAR ? swizzle(3, 0, 0, 0) : swizzle(1, 2, 2, 2);
                a.mov(r_(kInA), in.swz(sw));
                a.op(kTexld, t, {r(kInA), src(kSampler, stage)});
                break;
            }
            default:
                if (!(warned_modes.fetch_or(1u << texture_mode(def, stage)) & (1u << texture_mode(def, stage))))
                    LOG_WARN("render", "pixel shader texture mode %u not supported yet (reads zero)",
                             texture_mode(def, stage));
                a.mov(t, c(kPsHelpers).x());
                break;
        }
        if (key.alpha_kill_mask & (1u << stage)) {
            a.add(r_(kInA), r(kT0 + stage).w(), -c(kPsHelpers2).y());
            a.texkill(r_(kInA));
        }
    }
    // R0 starts as (0, 0, 0, T0.alpha), R1 as zero.
    a.mov(r_(kR0), c(kPsHelpers).x());
    a.mov(r_(kR0, kW), r(kT0).w());
    a.mov(r_(kR1), c(kPsHelpers).x());

    const uint32_t stages = def.combiner_count & 0xFF;
    const uint32_t count_flags = def.combiner_count >> 8;
    for (uint32_t stage = 0; stage < stages && stage < 8; ++stage) {
        const CombinerContext ctx{stage, (count_flags & 0x10) != 0, (count_flags & 0x100) != 0};
        const uint32_t in_vars[2] = {def.rgb_inputs[stage], def.alpha_inputs[stage]};
        for (int portion = 0; portion < 2; ++portion) {
            const uint32_t v = in_vars[portion];
            combiner_input(a, v >> 24, portion == 1, kInA, ctx);
            combiner_input(a, (v >> 16) & 0xFF, portion == 1, kInB, ctx);
            combiner_input(a, (v >> 8) & 0xFF, portion == 1, kInC, ctx);
            combiner_input(a, v & 0xFF, portion == 1, kInD, ctx);
        }
        a.add(r_(kMuxSel, kX), r(kR0).w(), c(kPsHelpers2).z());  // R0.alpha - 0.5 (MSB select)
        general_combiner_portion(a, def.rgb_inputs[stage], def.rgb_outputs[stage], false);
        general_combiner_portion(a, def.alpha_inputs[stage], def.alpha_outputs[stage], true);
        write_portion(a, def.rgb_outputs[stage], false);
        write_portion(a, def.alpha_outputs[stage], true);
        // Blue to alpha: the dot product result also goes to the destination's alpha.
        if (def.rgb_outputs[stage] & (1u << 19))
            if (const uint32_t d = combiner_dest((def.rgb_outputs[stage] >> 4) & 0xF); d != ~0u)
                a.mov(r_(d, kW), r(kAb).z());
        if (def.rgb_outputs[stage] & (1u << 18))
            if (const uint32_t d = combiner_dest(def.rgb_outputs[stage] & 0xF); d != ~0u) a.mov(r_(d, kW), r(kCd).z());
    }

    const Dst out = sat(dst(kColorOut, 0));
    if (def.final_inputs_abcd == 0 && def.final_inputs_efg == 0) {
        a.mov(out, r(kR0));
        return a.finish();
    }
    // Final combiner: rgb = A*B + (1-A)*C + D, alpha = G; E*F and V1+R0 are extra inputs.
    const CombinerContext ctx{8, false, false};
    const uint32_t abcd = def.final_inputs_abcd, efg = def.final_inputs_efg;
    combiner_input(a, efg >> 24, false, kInA, ctx);
    combiner_input(a, (efg >> 16) & 0xFF, false, kInB, ctx);
    a.mul(r_(kEfProd, kXyz), r(kInA), r(kInB));
    const bool complement_v1 = efg & 0x40, complement_r0 = efg & 0x20, clamp_sum = efg & 0x80;
    if (complement_r0)
        a.add(r_(kV1R0Sum, kXyz), c(kPsHelpers).y(), -r(kR0));
    else
        a.mov(r_(kV1R0Sum, kXyz), r(kR0));
    if (complement_v1)
        a.add(r_(kInC, kXyz), c(kPsHelpers).y(), -r(kV1));
    else
        a.mov(r_(kInC, kXyz), r(kV1));
    a.add(clamp_sum ? sat(r_(kV1R0Sum, kXyz)) : r_(kV1R0Sum, kXyz), r(kV1R0Sum), r(kInC));

    combiner_input(a, abcd >> 24, false, kInA, ctx);
    combiner_input(a, (abcd >> 16) & 0xFF, false, kInB, ctx);
    combiner_input(a, (abcd >> 8) & 0xFF, false, kInC, ctx);
    combiner_input(a, abcd & 0xFF, false, kFinalD, ctx);
    combiner_input(a, (efg >> 8) & 0xFF, true, kFinalG, ctx);
    a.op(kLrp, r_(kFinal, kXyz), {r(kInA), r(kInB), r(kInC)});
    a.add(r_(kFinal, kXyz), r(kFinal), r(kFinalD));
    a.mov(r_(kFinal, kW), r(kFinalG).w());
    a.mov(out, r(kFinal));
    return a.finish();
}

std::vector<uint32_t> solid_color_pixel_shader(float r, float g, float b, float a) {
    Assembler as(kPs30);
    as.def(kPsHelpers, r, g, b, a);
    as.mov(dst(kColorOut, 0), c(kPsHelpers));
    return as.finish();
}

}  // namespace render::d3d9
