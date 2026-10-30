#!/usr/bin/env python3
"""The title's shader programs -> HLSL files of our own (docs/own-renderer.md, step R3).

Input: a --dump-shaders directory (the loader records every vertex program and pixel shader
definition the renderer's shader slots get: vs_KEY.bin, ps_KEY.bin, shaders.csv). Output:
render/shaders/vs_KEY.hlsl and ps_KEY.hlsl (KEY: the title data address naming the program),
plus manifest.csv. The build compiles them with vkd3d-compiler (render/CMakeLists.txt).

Translation follows render/d3d9_shader.cpp (the Xbox path's translation to D3D9 tokens, checked
in game), written as HLSL: NV2A vertex program microcode (MAC + ILU pairs) and register combiners.
What that translation took from the draw-time state comes from reserved constants instead
(gfx_device.h): the fog mode (vertex c194.z) and alpha kill per texture stage (pixel c27).

usage: tools/shader_hlsl.py DUMP_DIR [OUT_DIR]   (OUT_DIR default: render/shaders)
"""

import csv
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

COMMON = """\
// Shared by the translated shaders (tools/shader_hlsl.py; generated, do not edit).
// Constant registers: see render/gfx_device.h. Every reserved register is declared, so the
// compiler's own literal constants land above them.

#ifdef VERTEX_SHADER
float4 c[192] : register(c0);              // the NV2A's constant slots
float4 screen_to_clip[2] : register(c192);  // clip.xyz = screen.xyz * [0].xyz + [1].xyz
float4 fog_params : register(c194);         // bias, scale, mode (0 off, 1 linear, 2 exp, 3 exp2)

struct VsOut {
    float4 pos : POSITION;
    float4 d0 : COLOR0;
    float4 d1 : COLOR1;
    float4 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
    float4 t2 : TEXCOORD2;
    float4 t3 : TEXCOORD3;
    float fog : FOG;
};

// ILU EXP and LOG, like vs_1_1's expp/logp.
float4 nv_exp(float s) { return float4(exp2(floor(s)), frac(s), exp2(s), 1); }
float4 nv_log(float s) {
    const float l = log2(abs(s));
    const float e = floor(l);
    return float4(e, abs(s) * exp2(-e), l, 1);
}
// RCC: the reciprocal clamped to +-1.884467e19.
float4 nv_rcc(float s) { return clamp(1 / s, -1.884467e19, 1.884467e19); }
float4 nv_dst(float4 a, float4 b) { return float4(1, a.y * b.y, a.z, b.w); }

// Screen space (title pixels, depth buffer units) -> clip space: w' = w, or 1 where w is 0.
VsOut nv_epilogue(float4 pos, float4 d0, float4 d1, float4 t0, float4 t1, float4 t2, float4 t3, float fog) {
    VsOut o;
    const float w = pos.w == 0 ? 1 : pos.w;
    o.pos = float4((pos.xyz * screen_to_clip[0].xyz + screen_to_clip[1].xyz) * w, w);
    o.d0 = d0;
    o.d1 = d1;
    o.t0 = t0;
    o.t1 = t1;
    o.t2 = t2;
    o.t3 = t3;
    // The fog factor from the distance (xemu's formulas; D3D picks the parameters).
    const float d = fog * fog_params.y;
    if (fog_params.z == 1)
        o.fog = fog_params.x + d - 1;
    else if (fog_params.z == 2)
        o.fog = fog_params.x + exp2(16 * d) - 1.5;
    else if (fog_params.z == 3)
        o.fog = fog_params.x + exp2(-32 * d * d) - 1.5;
    else
        o.fog = 1;
    return o;
}
#endif

#ifdef PIXEL_SHADER
float4 c[18] : register(c0);              // c0-c7 each stage's C0, c8-c15 C1, c16 c17 the final C0 C1
float4 texcoord_scale[8] : register(c18);  // per texture slot: texel -> normalized (linear textures)
float4 fog_color : register(c26);
float4 alpha_kill : register(c27);          // per stage (x-w): 1 = discard where its texture's alpha is 0

// Combiner input mappings (PS_INPUTMAPPING).
#define unsigned_identity(x) max(x, 0)
#define unsigned_invert(x) (1 - saturate(x))
#define expand_normal(x) (2 * max(x, 0) - 1)
#define expand_negate(x) (-2 * max(x, 0) + 1)
#define halfbias_normal(x) (max(x, 0) - 0.5)
#define halfbias_negate(x) (-max(x, 0) + 0.5)
#endif
"""

# --- Vertex programs ------------------------------------------------------------------------

MAC_OPS = ["NOP", "MOV", "MUL", "ADD", "MAD", "DP3", "DPH", "DP4", "DST", "MIN", "MAX", "SLT", "SGE", "ARL"]
ILU_OPS = ["NOP", "MOV", "RCP", "RCC", "RSQ", "EXP", "LOG", "LIT"]
COMPONENTS = "xyzw"

# Output addresses -> the variable kept until the epilogue.
OUTPUTS = {0: "R12", 3: "oD0", 4: "oD1", 5: "oFog", 6: "oPts", 7: "oB0", 8: "oB1",
           9: "oT0", 10: "oT1", 11: "oT2", 12: "oT3"}


class VsInstruction:
    def __init__(self, words):
        self.words = words

    def get(self, dword, shift, bits):
        return (self.words[dword] >> shift) & ((1 << bits) - 1)


def nv_mask(mask):
    """NV2A write masks are x=8 y=4 z=2 w=1."""
    return "".join(c for bit, c in zip((8, 4, 2, 1), COMPONENTS) if mask & bit)


class Operand:
    """A source operand: its text, and the temporary it reads (or None) with its swizzle."""

    def __init__(self, ins, mux, temp, negate, fields, inputs):
        self.temp, self.fields = None, fields
        if mux == 1:
            base, self.temp = f"R{temp}", temp
        elif mux == 2:
            v = ins.get(1, 9, 4)
            inputs.add(v)
            base = f"v{v}"
        elif mux == 3:
            index = ins.get(1, 13, 8)
            base = f"c[a0 + {index}]" if ins.get(3, 1, 1) else f"c[{index}]"
        else:
            base, self.fields = "float4(0, 0, 0, 0)", (0, 0, 0, 0)
        text = "".join(COMPONENTS[f] for f in self.fields)
        self.text = base + ("" if text == "xyzw" else "." + text)
        if negate:
            self.text = "-" + self.text

    def __str__(self):
        return self.text

    def reads(self, positions):
        """The temporary's components read at these operand positions."""
        return {COMPONENTS[self.fields[p]] for p in positions}


def operands(ins, inputs):
    a = Operand(ins, ins.get(2, 26, 2), ins.get(2, 28, 4), ins.get(1, 8, 1),
                (ins.get(1, 6, 2), ins.get(1, 4, 2), ins.get(1, 2, 2), ins.get(1, 0, 2)), inputs)
    b = Operand(ins, ins.get(2, 11, 2), ins.get(2, 13, 4), ins.get(2, 25, 1),
                (ins.get(2, 23, 2), ins.get(2, 21, 2), ins.get(2, 19, 2), ins.get(2, 17, 2)), inputs)
    c = Operand(ins, ins.get(3, 28, 2), ins.get(2, 0, 2) << 2 | ins.get(3, 30, 2), ins.get(2, 10, 1),
                (ins.get(2, 8, 2), ins.get(2, 6, 2), ins.get(2, 4, 2), ins.get(2, 2, 2)), inputs)
    return a, b, c


def positions(mask):
    return {COMPONENTS.index(ch) for ch in mask}


def translate_vs_instruction(ins, inputs, notes):
    """(name for the comment, HLSL lines, temporaries' components read, components written)."""
    mac, ilu = ins.get(1, 21, 4), ins.get(1, 25, 3)
    a, b, c = operands(ins, inputs)

    # Destinations: (variable, mask) per unit.
    out_r = ins.get(3, 20, 4)
    mac_dests, ilu_dests = [], []
    mac_mask = nv_mask(ins.get(3, 24, 4))
    if mac not in (0, 13) and mac_mask:
        mac_dests.append((f"R{out_r}", mac_mask))
    ilu_mask = nv_mask(ins.get(3, 16, 4))
    if ilu and ilu_mask:  # with both units active the ILU's temporary destination is always R1
        ilu_dests.append((f"R{1 if mac else out_r}", ilu_mask))
    o_mask = nv_mask(ins.get(3, 12, 4))
    if o_mask:
        if ins.get(3, 11, 1) == 0:
            notes.add("writes a constant register: not supported")
        elif ins.get(3, 3, 8) in OUTPUTS:
            (ilu_dests if ins.get(3, 2, 1) else mac_dests).append((OUTPUTS[ins.get(3, 3, 8)], o_mask))

    # What each unit reads (operand positions), for the initialisation of temporaries.
    reads = []
    mac_positions = set().union(*(positions(m) for _, m in mac_dests)) if mac_dests else set()
    if mac in (1, 2, 3, 4, 9, 10, 11, 12):
        for operand in {1: (a,), 2: (a, b), 3: (a, c), 4: (a, b, c)}.get(mac, (a, b)):
            reads.append((operand, mac_positions))
    elif mac in (5, 7):
        reads += [(a, {0, 1, 2, 3} if mac == 7 else {0, 1, 2}), (b, {0, 1, 2, 3} if mac == 7 else {0, 1, 2})]
    elif mac == 6:
        reads += [(a, {0, 1, 2}), (b, {0, 1, 2, 3})]
    elif mac == 8:
        reads += [(a, {1, 2}), (b, {1, 3})]
    elif mac == 13:
        reads.append((a, {0}))
    if ilu == 1:
        reads.append((c, set().union(*(positions(m) for _, m in ilu_dests)) if ilu_dests else set()))
    elif ilu == 7:
        reads.append((c, {0, 1, 3}))
    elif ilu:
        reads.append((c, {0}))
    temp_reads = [(f"R{o.temp}", o.reads(p)) for o, p in reads if o.temp is not None and p]

    mac_expr = {
        1: f"{a}", 2: f"{a} * {b}", 3: f"{a} + {c}", 4: f"{a} * {b} + {c}",
        5: f"dot(({a}).xyz, ({b}).xyz)", 6: f"dot(({a}).xyz, ({b}).xyz) + ({b}).w", 7: f"dot({a}, {b})",
        8: f"nv_dst({a}, {b})", 9: f"min({a}, {b})", 10: f"max({a}, {b})",
        11: f"float4({a} < {b})", 12: f"float4({a} >= {b})",
    }
    ilu_expr = {
        1: f"{c}", 2: f"1 / ({c}).x", 3: f"nv_rcc(({c}).x)", 4: f"rsqrt(abs(({c}).x))",
        5: f"nv_exp(({c}).x)", 6: f"nv_log(({c}).x)", 7: f"lit(({c}).x, ({c}).y, ({c}).w)",
    }
    if mac and mac != 13 and mac not in mac_expr:
        notes.add(f"unknown MAC op {mac}")
    lines = []
    if mac == 13:
        lines.append(f"a0 = (int)floor(({a}).x);")
    both = bool(mac_dests) and bool(ilu_dests)
    for unit, expr, dests in (("mac", mac_expr.get(mac), mac_dests), ("ilu", ilu_expr.get(ilu), ilu_dests)):
        if not expr or not dests:
            continue
        if len(dests) == 1 and not both:
            var, mask = dests[0]
            lines.append(f"{var}.{mask} = ((float4)({expr})).{mask};")
        else:
            # Both units read their sources before either writes.
            lines.append(f"{unit} = {expr};")
    for unit, dests in (("mac", mac_dests), ("ilu", ilu_dests)):
        if both or len(dests) > 1:
            lines += [f"{var}.{mask} = {unit}.{mask};" for var, mask in dests]

    parts = [MAC_OPS[mac] if mac < len(MAC_OPS) else str(mac)] if mac else []
    if ilu:
        parts.append(ILU_OPS[ilu])
    return " + ".join(parts) or "NOP", lines, temp_reads, mac_dests + ilu_dests


EPILOGUE_READS = {"R12": "xyzw", "oD0": "xyzw", "oD1": "xyzw", "oT0": "xyzw", "oT1": "xyzw", "oT2": "xyzw",
                  "oT3": "xyzw", "oFog": "x"}


def translate_vertex_program(blob, key, users):
    count = struct.unpack_from("<H", blob, 2)[0]
    notes, inputs = set(), set()
    body = []
    written = {}      # variable -> components written so far
    unset_reads = {}  # variable -> components read before any write (they start as on the NV2A)
    used = set()
    for i in range(count):
        words = struct.unpack_from("<4I", blob, 4 + i * 16)
        name, lines, reads, writes = translate_vs_instruction(VsInstruction(words), inputs, notes)
        for var, comps in reads:
            used.add(var)
            unset_reads.setdefault(var, set()).update(comps - written.get(var, set()))
        for var, mask in writes:
            used.add(var)
            written.setdefault(var, set()).update(mask)
        body.append(f"    // {i}: {name}")
        body.extend("    " + line for line in lines)
        if words[3] & 1:  # the final instruction
            break
    for var, comps in EPILOGUE_READS.items():
        used.add(var)
        unset_reads.setdefault(var, set()).update(set(comps) - written.get(var, set()))

    # Temporaries start as 0, outputs as (0, 0, 0, 1); only what is read before it is written.
    declarations = []
    for var in sorted(used, key=lambda v: (v[0] != "R", len(v), v)):
        comps = "".join(ch for ch in COMPONENTS if ch in unset_reads.get(var, set()))
        start = [0, 0, 0, 0] if var.startswith("R") else [0, 0, 0, 1]
        if not comps:
            declarations.append(f"    float4 {var};")
        elif comps == "xyzw":
            declarations.append(f"    float4 {var} = float4({', '.join(map(str, start))});")
        else:
            values = ", ".join(str(start[COMPONENTS.index(ch)]) for ch in comps)
            declarations.append(f"    float4 {var};")
            declarations.append(f"    {var}.{comps} = float{len(comps)}({values})" + ";" if len(comps) > 1
                                else f"    {var}.{comps} = {values};")
    if any(line.lstrip().startswith("mac =") for line in body):
        declarations.append("    float4 mac;")
    if any(line.lstrip().startswith("ilu =") for line in body):
        declarations.append("    float4 ilu;")
    if any("a0" in line for line in body):
        declarations.append("    int a0 = 0;")

    params = ", ".join(f"float4 v{v} : TEXCOORD{v}" for v in sorted(inputs))
    out = [f"// Vertex program {key} ({count} instructions), translated from NV2A microcode by",
           "// tools/shader_hlsl.py; generated, do not edit.", f"// Used by: {users}."]
    out += [f"// Note: {n}." for n in sorted(notes)]
    out += ["", "#define VERTEX_SHADER", '#include "nv2a.hlsli"', "", f"VsOut main({params}) {{"]
    out += declarations + [""] + body
    out += ["", "    return nv_epilogue(R12, oD0, oD1, oT0, oT1, oT2, oT3, oFog.x);", "}", ""]
    return "\n".join(out)


# --- Register combiners ---------------------------------------------------------------------

DEF_FIELDS = ["alpha_inputs"] * 8 + ["final_abcd", "final_efg"] + ["constant0"] * 8 + ["constant1"] * 8 + \
    ["alpha_outputs"] * 8 + ["rgb_inputs"] * 8 + ["compare_mode", "final_constant0", "final_constant1"] + \
    ["rgb_outputs"] * 8 + ["combiner_count", "texture_modes", "dot_mapping", "input_texture", "c0_mapping",
                           "c1_mapping", "final_constants_mapping"]


def parse_def(blob):
    words = struct.unpack_from("<60I", blob)
    d = {}
    for name, word in zip(DEF_FIELDS, words):
        d.setdefault(name, []).append(word)
    return {k: (v if len(v) > 1 else v[0]) for k, v in d.items()}


MODE_NONE, MODE_PROJECT2D, MODE_PROJECT3D, MODE_CUBEMAP, MODE_PASSTHRU = 0, 1, 2, 3, 4
MODE_DEPENDENT_AR, MODE_DEPENDENT_GB = 15, 16
MAPPINGS = ["unsigned_identity", "unsigned_invert", "expand_normal", "expand_negate", "halfbias_normal",
            "halfbias_negate", "", "-"]


def texture_mode(d, stage):
    return (d["texture_modes"] >> (stage * 5)) & 0x1F


def input_stage(d, stage):
    return {2: (d["input_texture"] >> 16) & 1, 3: (d["input_texture"] >> 20) & 3}.get(stage, 0)


def combiner_register(reg, stage, unique_c0, unique_c1):
    final = stage >= 8
    if reg == 1:
        return "c[16]" if final else f"c[{stage if unique_c0 else 0}]"
    if reg == 2:
        return "c[17]" if final else f"c[{8 + (stage if unique_c1 else 0)}]"
    names = {3: "FOG", 4: "V0", 5: "V1", 8: "T0", 9: "T1", 10: "T2", 11: "T3", 12: "R0", 13: "R1"}
    if reg in names:
        return names[reg]
    if final and reg == 14:
        return "V1R0"
    if final and reg == 15:
        return "EF"
    return "float4(0, 0, 0, 0)"


# What each mapping makes of a zero register.
ZERO_MAPPED = ["0", "1", "-1", "1", "-0.5", "0.5", "0", "0"]


def combiner_input(value, alpha_portion, stage, unique_c0, unique_c1):
    """One input variable (8 bits: register, channel, mapping) as an expression of the portion's
    width: rgb portions .xyz (or .www for the alpha channel), alpha portions .w (or .z: blue)."""
    reg = combiner_register(value & 0xF, stage, unique_c0, unique_c1)
    if reg.startswith("float4(0"):
        return ZERO_MAPPED[value >> 5]
    alpha_channel = value & 0x10
    if alpha_portion:
        reg += ".w" if alpha_channel else ".z"
    else:
        reg += ".www" if alpha_channel else ".xyz"
    mapping = MAPPINGS[value >> 5]
    if mapping == "":
        return reg
    if mapping == "-":
        return f"-{reg}"
    return f"{mapping}({reg})"


def product(x, y, dot):
    if dot:
        return "0" if "0" in (x, y) else f"dot({x}, {y})"
    if x == "0" or y == "0":
        return "0"
    if x == "1":
        return y
    if y == "1":
        return x
    return f"{x} * {y}"


DEST_NAMES = {4: "V0", 5: "V1", 8: "T0", 9: "T1", 10: "T2", 11: "T3", 12: "R0", 13: "R1"}


def output_mapping(outputs):
    """PS_COMBINEROUTPUT's bias and shift, then the general combiners' [-1, 1] range."""
    bias = " - 0.5" if outputs & (1 << 15) else ""
    shift = {1: " * 2", 2: " * 4", 3: " / 2"}.get((outputs >> 16) & 3, "")
    return bias, shift


def general_stage(d, stage, lines):
    """One general combiner stage: AB, CD and their sum (or MUX) per portion, every result
    computed before any is written (the hardware reads the stage's inputs first)."""
    count_flags = d["combiner_count"] >> 8
    unique_c0, unique_c1 = bool(count_flags & 0x10), bool(count_flags & 0x100)
    computed, writes = [], []
    for portion, mask in ((0, "xyz"), (1, "w")):
        inputs = d["rgb_inputs"][stage] if portion == 0 else d["alpha_inputs"][stage]
        outputs = d["rgb_outputs"][stage] if portion == 0 else d["alpha_outputs"][stage]
        a, b, c, dd = (combiner_input((inputs >> shift) & 0xFF, portion == 1, stage, unique_c0, unique_c1)
                       for shift in (24, 16, 8, 0))
        dests = {"ab": (outputs >> 4) & 0xF, "cd": outputs & 0xF, "sum": (outputs >> 8) & 0xF}
        used = {t for t, reg in dests.items() if reg in DEST_NAMES}
        # Blue to alpha: the dot product also goes to the destination's alpha.
        if portion == 0 and outputs & (1 << 19) and dests["ab"] in DEST_NAMES:
            used.add("ab")
        if portion == 0 and outputs & (1 << 18) and dests["cd"] in DEST_NAMES:
            used.add("cd")
        if not used:
            continue
        ab = product(a, b, portion == 0 and outputs & (1 << 13))
        cd = product(c, dd, portion == 0 and outputs & (1 << 12))
        if outputs & (1 << 14):
            total = f"R0.w >= 0.5 ? {cd} : {ab}"  # MUX: by R0's alpha at the stage's start
        else:
            total = ab if cd == "0" else cd if ab == "0" else f"{ab} + {cd}"
        bias, shift = output_mapping(outputs)
        names = {"ab": f"ab.{mask}", "cd": f"cd.{mask}", "sum": f"sum.{mask}"}
        for t, expr in (("ab", ab), ("cd", cd), ("sum", total)):
            if t in used:
                computed.append(f"    {names[t]} = clamp(({expr}){bias}{shift}, -1, 1);")
        for t in ("ab", "cd", "sum"):
            if t in used and dests[t] in DEST_NAMES:
                writes.append(f"    {DEST_NAMES[dests[t]]}.{mask} = {names[t]};")
        if portion == 0 and outputs & (1 << 19) and dests["ab"] in DEST_NAMES:
            writes.append(f"    {DEST_NAMES[dests['ab']]}.w = ab.z;")
        if portion == 0 and outputs & (1 << 18) and dests["cd"] in DEST_NAMES:
            writes.append(f"    {DEST_NAMES[dests['cd']]}.w = cd.z;")
    lines.append(f"    // Combiner stage {stage}.")
    lines += computed + writes


def translate_pixel_shader(blob, key, users):
    d = parse_def(blob)
    notes = set()
    lines = []
    samplers = []
    for stage in range(4):
        mode = texture_mode(d, stage)
        kind = {MODE_PROJECT2D: "sampler2D", MODE_DEPENDENT_AR: "sampler2D", MODE_DEPENDENT_GB: "sampler2D",
                MODE_CUBEMAP: "samplerCUBE", MODE_PROJECT3D: "sampler3D"}.get(mode)
        if kind:
            samplers.append(f"{kind} s{stage} : register(s{stage});")
    lines += ["    float4 V0 = v0, V1 = v1;", "    float4 FOG = float4(fog_color.rgb, saturate(fog));",
              "    float4 T0 = 0, T1 = 0, T2 = 0, T3 = 0;"]
    for stage in range(4):
        mode = texture_mode(d, stage)
        t, coord = f"T{stage}", f"t{stage}"
        if mode == MODE_NONE:
            pass  # T stays 0 (alpha kill below still applies, as on the Xbox path)
        elif mode == MODE_PROJECT2D:
            lines.append(f"    {t} = tex2Dproj(s{stage}, {coord} * texcoord_scale[{stage}]);")
        elif mode == MODE_CUBEMAP:
            lines.append(f"    {t} = texCUBE(s{stage}, {coord}.xyz);")
        elif mode == MODE_PROJECT3D:
            lines.append(f"    {t} = tex3D(s{stage}, {coord}.xyz);")
        elif mode == MODE_PASSTHRU:
            lines.append(f"    {t} = saturate({coord});")
        elif mode in (MODE_DEPENDENT_AR, MODE_DEPENDENT_GB):
            source = f"T{input_stage(d, stage)}"
            sw = "wx" if mode == MODE_DEPENDENT_AR else "yz"
            lines.append(f"    {t} = tex2D(s{stage}, {source}.{sw});  // an earlier stage's "
                         f"{'alpha, red' if sw == 'wx' else 'green, blue'}")
        else:
            notes.add(f"texture mode {mode} (stage {stage}) is not translated: reads zero")
        lines.append(f"    if (alpha_kill.{COMPONENTS[stage]} != 0) clip({t}.w - 1.0 / 512);")
    lines += ["    // R0 starts as (0, 0, 0, T0.alpha), R1 as zero.", "    float4 R0 = float4(0, 0, 0, T0.w), R1 = 0;",
              "    float4 ab = 0, cd = 0, sum = 0;"]
    stages = d["combiner_count"] & 0xFF
    for stage in range(min(stages, 8)):
        general_stage(d, stage, lines)

    abcd, efg = d["final_abcd"], d["final_efg"]
    if abcd == 0 and efg == 0:
        lines.append("    return saturate(R0);")
    else:
        lines.append("    // Final combiner: rgb = A*B + (1-A)*C + D, alpha = G; E*F and V1+R0 are inputs too.")
        fa, fb, fc, fd = (combiner_input((abcd >> shift) & 0xFF, False, 8, False, False) for shift in (24, 16, 8, 0))
        fg = combiner_input((efg >> 8) & 0xFF, True, 8, False, False)
        used = " ".join((fa, fb, fc, fd, fg))
        if "EF" in used:
            e = combiner_input(efg >> 24, False, 8, False, False)
            f = combiner_input((efg >> 16) & 0xFF, False, 8, False, False)
            lines.append(f"    const float4 EF = float4({product(e, f, False)}, 0);")
        if "V1R0" in used:
            r0 = "(1 - R0.xyz)" if efg & 0x20 else "R0.xyz"
            v1 = "(1 - V1.xyz)" if efg & 0x40 else "V1.xyz"
            total = f"{r0} + {v1}"
            lines.append(f"    const float4 V1R0 = float4({'saturate(' + total + ')' if efg & 0x80 else total}, 0);")
        # A*B + (1-A)*C + D, folded where A is 0 or 1.
        if fa == "0":
            rgb = fc
        elif fa == "1":
            rgb = fb
        else:
            rgb = f"lerp({fc}, {fb}, {fa})"
        if fd != "0":
            rgb = f"{rgb} + {fd}"
        lines.append(f"    return saturate(float4({rgb}, {fg}));")

    out = [f"// Pixel shader {key} ({stages} combiner stages), translated from its register combiner",
           "// setup (D3DPIXELSHADERDEF) by tools/shader_hlsl.py; generated, do not edit.", f"// Used by: {users}."]
    out += [f"// Note: {n}." for n in sorted(notes)]
    out += ["", "#define PIXEL_SHADER", '#include "nv2a.hlsli"', ""] + samplers
    out += ["", "float4 main(float4 v0 : COLOR0, float4 v1 : COLOR1, float4 t0 : TEXCOORD0, float4 t1 : TEXCOORD1,",
            "            float4 t2 : TEXCOORD2, float4 t3 : TEXCOORD3, float fog : FOG) : COLOR0 {"]
    out += lines + ["}", ""]
    return "\n".join(out)


# --- Who uses which slot ----------------------------------------------------------------------

def slot_owners():
    """Shader slot address -> the C function that fills it (game/code), for the comments."""
    owners, drawn = {}, {}
    for path in sorted((ROOT / "game/code").glob("*.cpp")):
        name = None
        for line in path.read_text(errors="replace").splitlines():
            m = re.match(r"(?:void|uint\w*|int\w*|\w+\*?)\s+XB_\w+\s+(\w+)\(", line)
            if m:
                name = m.group(1)
            if not name:
                continue
            m = re.search(r"init_(vertex|pixel)_shaders\(0x([0-9a-f]+), 0x[0-9a-f]+, (\d+)", line)
            if m:
                step = 8 if m.group(1) == "vertex" else 4
                for i in range(int(m.group(3))):
                    owners.setdefault(int(m.group(2), 16) + i * step, f"{name}[{i}]")
            for m in re.finditer(r"ngl(?:Vertex|Pixel)Shader_Init\(&title_global<uint32_t>\(0x([0-9a-f]+)\)", line):
                owners.setdefault(int(m.group(1), 16), name)
            # The assemblers: pixel (slot, source), vertex (declaration, slot, ...).
            m = re.search(r"title_function<Assemble(Pixel|Vertex)>\(0x[0-9a-f]+\)\(0x([0-9a-f]+), 0x([0-9a-f]+)", line)
            if m:
                slot = m.group(2) if m.group(1) == "Pixel" else m.group(3)
                owners.setdefault(int(slot, 16), f"{name} (assembled at boot)")
            m = re.search(r"init_op_shaders\(0x([0-9a-f]+), 0x[0-9a-f]+, 0x[0-9a-f]+, 0x([0-9a-f]+)", line)
            if m:
                for slot in m.groups():
                    owners.setdefault(int(slot, 16), name)
            # Otherwise the draw code that selects the slot.
            for m in re.finditer(r"(?:shader_ref\(|title_global<NglShaderRef>\()0x([0-9a-f]+)\)", line):
                drawn.setdefault(int(m.group(1), 16), f"drawn by {name}")
    for slot, text in drawn.items():
        owners.setdefault(slot, text)
    return owners


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    dump = Path(sys.argv[1])
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "render/shaders"
    out_dir.mkdir(parents=True, exist_ok=True)
    owners = slot_owners()
    users = {}  # (kind, key) -> set of owner names
    for row in csv.reader((dump / "shaders.csv").open()):
        kind, key, slot = row[0], row[1], int(row[2], 16)
        users.setdefault((kind, key), set()).add(owners.get(slot, f"slot {slot:08x}"))

    (out_dir / "nv2a.hlsli").write_text(COMMON)
    manifest = []
    for (kind, key), names in sorted(users.items()):
        prefix = "vs" if kind == "v" else "ps"
        blob = (dump / f"{prefix}_{key}.bin").read_bytes()
        who = ", ".join(sorted(names))
        text = translate_vertex_program(blob, key, who) if kind == "v" else translate_pixel_shader(blob, key, who)
        (out_dir / f"{prefix}_{key}.hlsl").write_text(text)
        manifest.append((prefix, key, who))
    with (out_dir / "manifest.csv").open("w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["stage", "key", "used_by"])
        w.writerows(manifest)
    print(f"{len(manifest)} shaders -> {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
