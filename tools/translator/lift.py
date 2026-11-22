#!/usr/bin/env python3
"""lift.py: translate level functions (x86-32) to C over a register struct (translator step 2: integer core).
usage: build/translator_venv/bin/python tools/translator/lift.py [LEVEL] [--class intleaf] [--only ADDR,...]
                                                                  [--out DIR]
Writes DIR/<level>_xl.cpp (default build/translator/gen): one C function per x86 function plus its entry stub and a
game::Function row table (translated_<level>()). Functions using anything the lifter does not handle yet are left out
(listed with the reason). Runtime: src/levels/xlat_runtime.h.

Flags are lazy: an instruction that sets flags some reader needs stores its operands (fa, fb) and result (fr); the
reader computes its condition from those. A reader reached by producers of one kind gets that kind's expression;
mixed producers store a kind number (fk) and use the generic xl_cond().
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cfg  # noqa: E402  (sets the cwd and sys.path like the other tools)
import capstone  # noqa: E402
from capstone import x86  # noqa: E402
import fnpack  # noqa: E402

R32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
R16 = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx", "sp": "esp", "bp": "ebp", "si": "esi", "di": "edi"}
R8L = {"al": "eax", "cl": "ecx", "dl": "edx", "bl": "ebx"}
R8H = {"ah": "eax", "ch": "ecx", "dh": "edx", "bh": "ebx"}
MASK = {1: "0xffu", 2: "0xffffu", 4: "0xffffffffu"}
CC = {"o": "o", "no": "no", "b": "b", "ae": "ae", "e": "e", "ne": "ne", "be": "be", "a": "a", "s": "s", "ns": "ns",
      "p": "p", "np": "np", "l": "l", "ge": "ge", "le": "le", "g": "g"}
# Flag-setting kinds (XL_K_* in xlat_runtime.h) and which instructions set them.
KIND = {"add": "ADD", "sub": "SUB", "cmp": "SUB", "neg": "NEG", "and": "LOGIC", "or": "LOGIC", "xor": "LOGIC",
        "test": "LOGIC", "inc": "INC", "dec": "DEC", "shl": "SHL", "shr": "SHR", "sar": "SAR", "imul": "MUL",
        "mul": "MUL", "adc": "ADC", "sbb": "SBB", "bt": "BT", "ucomiss": "RAW", "comiss": "RAW",
        "fcompi": "RAW", "fcomip": "RAW", "fcomi": "RAW", "fucompi": "RAW", "fucomip": "RAW", "fucomi": "RAW"}
# Flags each condition reads (an INC/DEC producer cannot answer CF).
CC_FLAGS = {"o": "o", "no": "o", "b": "c", "ae": "c", "e": "z", "ne": "z", "be": "cz", "a": "cz", "s": "s", "ns": "s",
            "p": "p", "np": "p", "l": "so", "ge": "so", "le": "zso", "g": "zso"}


NAMES = {}  # every function's name by address (main())


class Unsupported(Exception):
    pass


SS_ARITH = {"addss": "add", "subss": "sub", "mulss": "mul", "divss": "div", "minss": "min", "maxss": "max"}
PS_ARITH = {"addps": "add", "subps": "sub", "mulps": "mul", "divps": "div", "minps": "min", "maxps": "max",
            "andps": "and", "andnps": "andnot", "orps": "or", "xorps": "xor", "unpcklps": "unpacklo",
            "unpckhps": "unpackhi", "movlhps": "movelh", "movhlps": "movehl"}
SS_UNARY = {"sqrtss": "sqrt", "rsqrtss": "rsqrt", "rcpss": "rcp"}
CMP_PRED = ["eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"]


def is_xmm(ins, op):
    return op.type == x86.X86_OP_REG and ins.reg_name(op.reg).startswith("xmm")


class Lifter:
    def __init__(self, fn, all_starts, translated, image, lo, hi):
        self.fn, self.starts, self.translated = fn, all_starts, translated
        self.image, self.lo, self.hi = image, lo, hi
        self.callees = set()
        self.lines = []
        self.uses_fk = False

    # ---- operands --------------------------------------------------------------------------------------------
    def reg_read(self, ins, reg):
        n = ins.reg_name(reg)
        if n in R32:
            return n
        if n in R16:
            return f"({R16[n]} & 0xffffu)"
        if n in R8L:
            return f"({R8L[n]} & 0xffu)"
        if n in R8H:
            return f"(({R8H[n]} >> 8) & 0xffu)"
        raise Unsupported(f"register {n}")

    def reg_write(self, ins, reg, v):
        n = ins.reg_name(reg)
        if n in R32:
            return f"{n} = {v};"
        if n in R16:
            return f"{R16[n]} = ({R16[n]} & 0xffff0000u) | (({v}) & 0xffffu);"
        if n in R8L:
            return f"{R8L[n]} = ({R8L[n]} & 0xffffff00u) | (({v}) & 0xffu);"
        if n in R8H:
            return f"{R8H[n]} = ({R8H[n]} & 0xffff00ffu) | ((({v}) & 0xffu) << 8);"
        raise Unsupported(f"register {n}")

    def addr(self, ins, op):
        m = op.mem
        if m.segment != 0 and ins.reg_name(m.segment) != "fs":
            raise Unsupported("segment")
        parts = []
        if m.base:
            parts.append(self.reg_read(ins, m.base))
        if m.index:
            idx = self.reg_read(ins, m.index)
            parts.append(idx if m.scale == 1 else f"{idx} * {m.scale}u")
        if m.disp or not parts:
            parts.append(f"0x{m.disp & 0xffffffff:x}u")
        return "(uint32_t)(" + " + ".join(parts) + ")"

    def read(self, ins, op, size=None):
        size = size or op.size
        if op.type == x86.X86_OP_REG:
            return self.reg_read(ins, op.reg)
        if op.type == x86.X86_OP_IMM:
            return f"0x{op.imm & ((1 << (8 * size)) - 1):x}u"
        if op.type == x86.X86_OP_MEM:
            if op.mem.segment != 0:
                if size != 4:
                    raise Unsupported("fs access not 32-bit")
                return f"XL_FS_R32({self.addr(ins, op)})"
            return f"XL_R{8 * size}({self.addr(ins, op)})"
        raise Unsupported("operand type")

    def write(self, ins, op, v):
        if op.type == x86.X86_OP_REG:
            return self.reg_write(ins, op.reg, v)
        if op.type == x86.X86_OP_MEM:
            if op.mem.segment != 0:
                if op.size != 4:
                    raise Unsupported("fs access not 32-bit")
                return f"XL_FS_W32({self.addr(ins, op)}, {v});"
            return f"XL_W{8 * op.size}({self.addr(ins, op)}, {v});"
        raise Unsupported("write operand")

    # ---- flags -----------------------------------------------------------------------------------------------
    def set_flags(self, ins, kind, a, b, r, size):
        """Stores what readers of this instruction's flags need (only if some reader is reached by it)."""
        if ins.address not in self.needed:
            return ""
        k = f"XL_K_{kind}{8 * size}"
        out = f"fa = {a}; fb = {b}; fr = {r};"
        if self.mixed_needed.get(ins.address):
            out += f" fk = {k};"
        return out

    def cond(self, ins, cc):
        prods = self.reaching.get(ins.address, set())
        if not prods or None in prods:
            raise Unsupported(f"flags of {ins.mnemonic} not set in the function")
        kinds = {self.kind_of[p] for p in prods}
        for k in kinds:
            if k.startswith(("INC", "DEC")) and "c" in CC_FLAGS[cc]:
                raise Unsupported("CF after inc/dec")
            if k.startswith(("MUL", "BT")) and set(CC_FLAGS[cc]) - set("co"):
                raise Unsupported(f"{cc} after {k}")
        if len(kinds) == 1:
            return f"XL_CC_{cc}(XL_K_{kinds.pop()}, fa, fb, fr, fc)"
        self.uses_fk = True
        return f"XL_CC_{cc}(fk, fa, fb, fr, fc)"

    # ---- dataflow: which flag producers reach each reader ----------------------------------------------------
    def flow(self):
        fn = self.fn
        order = sorted(fn.ins)
        succ = {}
        for a in order:
            ins = fn.ins[a]
            m = ins.mnemonic
            nxt = a + ins.size
            s = []
            if m in cfg.JCC:
                s = [ins.operands[0].imm, nxt]
            elif m == "jmp":
                if ins.operands[0].type == x86.X86_OP_IMM:
                    t = ins.operands[0].imm
                    s = [t] if t in fn.ins else []
                else:  # a switch: its table's cases (the flags and the x87 depth flow into them)
                    s = sorted({c for c in (cfg.switch_table(self.image, ins, self.lo, self.hi) or []) if c in fn.ins})
            elif m in ("ret", "int3"):
                s = []
            else:
                s = [nxt] if nxt in fn.ins else []
            succ[a] = s
        self.succ = succ
        self.kind_of = {}
        for a in order:
            ins = fn.ins[a]
            k = KIND.get(ins.mnemonic)
            if k:
                size = ins.operands[0].size if ins.operands else 4
                if ins.mnemonic in ("imul", "mul") and len(ins.operands) == 1:
                    size = ins.operands[0].size
                if k == "RAW":
                    size = 4
                self.kind_of[a] = f"{k}{8 * size}"
        # state at entry of each instruction: set of producer addresses (None = from outside)
        state = {fn.start: {None}}
        work = [fn.start]
        while work:
            a = work.pop()
            ins = fn.ins[a]
            cur = state[a]
            if a in self.kind_of:
                out = {a}
            elif writes_flags_unknown(ins):
                out = {None}
            else:
                out = cur
            for s in succ[a]:
                old = state.get(s)
                new = out if old is None else old | out
                if old != new:
                    state[s] = new
                    work.append(s)
        self.reaching = {}
        for a in order:
            if reads_flags(fn.ins[a]):
                self.reaching[a] = state.get(a, set())
        self.needed = set()
        self.mixed_needed = {}
        for a, prods in self.reaching.items():
            kinds = {self.kind_of.get(p) for p in prods}
            for p in prods:
                if p is not None:
                    self.needed.add(p)
                    if len(kinds) > 1:
                        self.mixed_needed[p] = True

    # ---- instructions ----------------------------------------------------------------------------------------
    def emit(self, s):
        self.lines.append("    " + s)

    def lift_ins(self, ins):
        m = ins.mnemonic
        ops = ins.operands
        e = self.emit
        size = ops[0].size if ops else 4
        msk = MASK.get(size)
        if m == "nop":
            return
        if m == "mov":
            e(self.write(ins, ops[0], self.read(ins, ops[1])))
        elif m in ("movzx", "movsx"):
            v = self.read(ins, ops[1])
            if m == "movsx":
                v = f"(uint32_t)(int32_t)(int{8 * ops[1].size}_t)({v})"
            e(self.write(ins, ops[0], v))
        elif m == "lea":
            e(self.reg_write(ins, ops[0].reg, self.addr(ins, ops[1])))
        elif m in ("add", "sub", "and", "or", "xor", "cmp", "test"):
            a, b = self.read(ins, ops[0]), self.read(ins, ops[1], size)
            sym = {"add": "+", "sub": "-", "cmp": "-", "and": "&", "test": "&", "or": "|", "xor": "^"}[m]
            if m == "xor" and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG and ops[0].reg == ops[1].reg:
                a = b = "0u"
            body = f"uint32_t a = {a}, b = {b}, r = (a {sym} b) & {msk};"
            if m not in ("cmp", "test"):
                body += " " + self.write(ins, ops[0], "r")
            e("{ " + body + " " + self.set_flags(ins, KIND[m], "a", "b", "r", size) + " }")
        elif m in ("adc", "sbb"):
            cf = self.carry(ins)
            a, b = self.read(ins, ops[0]), self.read(ins, ops[1], size)
            sym = "+" if m == "adc" else "-"
            body = (f"uint32_t a = {a}, b = {b}, c = {cf}, r = (a {sym} b {sym} c) & {msk}; "
                    + self.write(ins, ops[0], "r"))
            fl = self.set_flags(ins, KIND[m], "a", "b", "r", size)
            if fl:
                fl += " fc = c;"
                self.uses_fc = True
            e("{ " + body + " " + fl + " }")
        elif m in ("inc", "dec"):
            a = self.read(ins, ops[0])
            sym = "+" if m == "inc" else "-"
            e("{ " + f"uint32_t a = {a}, r = (a {sym} 1u) & {msk}; " + self.write(ins, ops[0], "r") + " "
              + self.set_flags(ins, KIND[m], "a", "1u", "r", size) + " }")
        elif m == "neg":
            a = self.read(ins, ops[0])
            e("{ " + f"uint32_t a = {a}, r = (0u - a) & {msk}; " + self.write(ins, ops[0], "r") + " "
              + self.set_flags(ins, "NEG", "a", "0u", "r", size) + " }")
        elif m == "not":
            e(self.write(ins, ops[0], f"(~{self.read(ins, ops[0])}) & {msk}"))
        elif m in ("shl", "shr", "sar", "rol", "ror"):
            a = self.read(ins, ops[0])
            cnt = self.read(ins, ops[1], 1) if len(ops) > 1 else "1u"
            bits = 8 * size
            if m == "shl":
                r = f"(a << n) & {msk}"
            elif m == "shr":
                r = "a >> n"
            elif m == "sar":
                r = f"(uint32_t)((int32_t)((int{bits}_t)a) >> n) & {msk}"
            elif m == "rol":
                r = f"((a << (n % {bits})) | (a >> (({bits} - n % {bits}) % {bits}))) & {msk}"
            else:
                r = f"((a >> (n % {bits})) | (a << (({bits} - n % {bits}) % {bits}))) & {msk}"
            body = f"uint32_t a = {a}, n = {cnt} & 31u;"
            if bits < 32 and m == "sar":
                body = f"uint32_t a = {a}, n = {cnt} & 31u; if (n > {bits - 1}) n = {bits - 1};"
            fl = ""
            if m in ("shl", "shr", "sar"):
                fl = self.set_flags(ins, KIND[m], "a", "n", "r", size)
                if fl:
                    fl = "if (n) { " + fl + " }"
            elif ins.address in self.needed:
                raise Unsupported("flags of rol/ror")
            e("{ " + body + f" uint32_t r = n ? ({r}) : a; " + self.write(ins, ops[0], "r") + " " + fl + " }")
        elif m in ("shld", "shrd"):
            if ins.address in self.needed:
                raise Unsupported(f"flags of {m}")
            a, b = self.read(ins, ops[0]), self.read(ins, ops[1])
            cnt = self.read(ins, ops[2], 1)
            if size != 4:
                raise Unsupported(f"16-bit {m}")
            r = "(a << n) | (b >> (32 - n))" if m == "shld" else "(a >> n) | (b << (32 - n))"
            e("{ " + f"uint32_t a = {a}, b = {b}, n = {cnt} & 31u; uint32_t r = n ? ({r}) : a; "
              + self.write(ins, ops[0], "r") + " }")
        elif m == "imul":
            if len(ops) == 1:
                self.mul1(ins, signed=True)
            else:
                if len(ops) == 2:
                    a, b = self.read(ins, ops[0]), self.read(ins, ops[1])
                else:
                    a, b = self.read(ins, ops[1]), self.read(ins, ops[2], size)
                bits = 8 * size
                body = (f"int64_t p = (int64_t)(int{bits}_t)({a}) * (int64_t)(int{bits}_t)({b}); "
                        f"uint32_t r = (uint32_t)p & {msk}; " + self.write(ins, ops[0], "r"))
                fl = self.set_flags(ins, "MUL", f"(uint32_t)(p != (int64_t)(int{bits}_t)r)", "0u", "r", size)
                e("{ " + body + " " + fl + " }")
        elif m == "mul":
            self.mul1(ins, signed=False)
        elif m in ("div", "idiv"):
            if size != 4:
                raise Unsupported(f"{size * 8}-bit {m}")
            d = self.read(ins, ops[0])
            if m == "div":
                e("{ " + f"uint64_t n = ((uint64_t)edx << 32) | eax; uint32_t d = {d}; "
                  "uint64_t q = n / d; if (q >> 32) xl_divide_error(); eax = (uint32_t)q; edx = (uint32_t)(n % d); }")
            else:
                e("{ " + f"int64_t n = (int64_t)(((uint64_t)edx << 32) | eax); int32_t d = (int32_t){d}; "
                  "if (d == 0 || (n == INT64_MIN && d == -1)) xl_divide_error(); int64_t q = n / d; "
                  "if (q != (int32_t)q) xl_divide_error(); eax = (uint32_t)q; edx = (uint32_t)(n % d); }")
        elif m == "cdq":
            e("edx = (eax & 0x80000000u) ? 0xffffffffu : 0u;")
        elif m == "cwde":
            e("eax = (uint32_t)(int32_t)(int16_t)eax;")
        elif m == "xchg":
            a, b = self.read(ins, ops[0]), self.read(ins, ops[1])
            e("{ " + f"uint32_t a = {a}, b = {b}; " + self.write(ins, ops[0], "b") + " " + self.write(ins, ops[1], "a")
              + " }")
        elif m == "bswap":
            r = ins.reg_name(ops[0].reg)
            e(f"{r} = __builtin_bswap32({r});")
        elif m == "bt":
            a = self.read(ins, ops[0])
            if ops[0].type == x86.X86_OP_MEM and ops[1].type == x86.X86_OP_REG:
                raise Unsupported("bt mem, reg (bit string)")
            e("{ " + f"uint32_t r = ({a} >> ({self.read(ins, ops[1], 1)} & {8 * size - 1}u)) & 1u; "
              + self.set_flags(ins, "BT", "0u", "0u", "r", size) + " }")
        elif m == "push":
            v = self.read(ins, ops[0], 4) if ops[0].type != x86.X86_OP_IMM else f"0x{ops[0].imm & 0xffffffff:x}u"
            if ops[0].size != 4:
                raise Unsupported("16-bit push")
            e("{ " + f"uint32_t v = {v}; esp -= 4u; XL_W32(esp, v);" + " }")
        elif m == "pop":
            if ops[0].type == x86.X86_OP_REG and ins.reg_name(ops[0].reg) == "esp":
                e("esp = XL_R32(esp);")
            else:
                e("{ uint32_t v = XL_R32(esp); esp += 4u; " + self.write(ins, ops[0], "v") + " }")
        elif m == "leave":
            e("esp = ebp; ebp = XL_R32(esp); esp += 4u;")
        elif m.startswith("set") and m[3:] in CC:
            e(self.write(ins, ops[0], f"({self.cond(ins, m[3:])} ? 1u : 0u)"))
        elif m.startswith("cmov") and m[4:] in CC:
            e(f"if ({self.cond(ins, m[4:])}) " + "{ " + self.write(ins, ops[0], self.read(ins, ops[1])) + " }")
        elif m == "lahf":
            prods = self.reaching.get(ins.address, set())
            if not prods or None in prods:
                raise Unsupported("flags of lahf not set in the function")
            kinds = {self.kind_of[p] for p in prods}
            if any(k.startswith(("INC", "DEC", "MUL", "BT")) for k in kinds):
                raise Unsupported("lahf after inc/dec/mul/bt")
            k = f"XL_K_{kinds.pop()}" if len(kinds) == 1 else "fk"
            if k == "fk":
                self.uses_fk = True
            e(f"eax = (eax & 0xffff00ffu) | (::xl::XL_LAHF({k}, fa, fb, fr, fc) << 8);")
        elif self.sse(ins):
            pass
        elif self.x87(ins):
            pass
        elif m in ("movsb", "movsw", "movsd", "stosb", "stosw", "stosd", "rep movsb", "rep movsw", "rep movsd",
                   "rep stosb", "rep stosw", "rep stosd"):
            self.string(ins)
        else:
            raise Unsupported(f"mnemonic {m}")

    # ---- SSE (scalar and packed single; the title targets a Pentium III: no SSE2) ------------------------------
    def x_src(self, ins, op, scalar):
        if is_xmm(ins, op):
            return ins.reg_name(op.reg)
        if op.type == x86.X86_OP_MEM and op.mem.segment == 0:
            return f"::xl::{'ld_ss' if scalar else 'ld_ps'}({self.addr(ins, op)})"
        raise Unsupported(f"xmm operand of {ins.mnemonic}")

    def sse(self, ins):
        m, ops, e = ins.mnemonic, ins.operands, self.emit
        if m not in cfg.SSE and not (m.startswith("cmp") and m.endswith(("ss", "ps"))) and m != "movmskps":
            return False
        if m == "movsd" and not any(is_xmm(ins, o) for o in ops):
            return False  # the string op
        d = ins.reg_name(ops[0].reg) if ops and is_xmm(ins, ops[0]) else None
        if m in ("movss", "movaps", "movups"):
            scalar = m == "movss"
            if d and is_xmm(ins, ops[1]):
                s_ = ins.reg_name(ops[1].reg)
                e(f"{d} = _mm_move_ss({d}, {s_});" if scalar else f"{d} = {s_};")
            elif d:
                e(f"{d} = {self.x_src(ins, ops[1], scalar)};")
            else:
                if not is_xmm(ins, ops[1]) or ops[0].type != x86.X86_OP_MEM or ops[0].mem.segment:
                    raise Unsupported(f"{m} form")
                e(f"::xl::{'st_ss' if scalar else 'st_ps'}({self.addr(ins, ops[0])}, {ins.reg_name(ops[1].reg)});")
        elif m in SS_ARITH:
            e(f"{d} = _mm_{SS_ARITH[m]}_ss({d}, {self.x_src(ins, ops[1], True)});")
        elif m in PS_ARITH:
            e(f"{d} = _mm_{PS_ARITH[m]}_ps({d}, {self.x_src(ins, ops[1], False)});")
        elif m in SS_UNARY:
            e(f"{d} = _mm_move_ss({d}, _mm_{SS_UNARY[m]}_ss({self.x_src(ins, ops[1], True)}));")
        elif m == "cvtsi2ss":
            e(f"{d} = _mm_cvtsi32_ss({d}, (int){self.read(ins, ops[1], 4)});")
        elif m in ("cvttss2si", "cvtss2si"):
            fn = "_mm_cvttss_si32" if m == "cvttss2si" else "_mm_cvtss_si32"
            e(self.reg_write(ins, ops[0].reg, f"(uint32_t){fn}({self.x_src(ins, ops[1], True)})"))
        elif m in ("ucomiss", "comiss"):
            fl = self.set_flags(ins, "RAW", "0u", "0u", f"::xl::eflags_after_{m}({d}, {self.x_src(ins, ops[1], True)})", 4)
            e("{ " + (fl or f"(void)::xl::eflags_after_{m}({d}, {self.x_src(ins, ops[1], True)});") + " }")
        elif m == "shufps":
            e(f"{d} = _mm_shuffle_ps({d}, {self.x_src(ins, ops[1], False)}, 0x{ops[2].imm & 0xff:x});")
        elif m in ("movlps", "movhps"):
            half = "l" if m == "movlps" else "h"
            if d:
                e(f"{d} = _mm_load{half}_pi({d}, reinterpret_cast<const __m64*>(uintptr_t{{{self.addr(ins, ops[1])}}}));")
            else:
                e(f"_mm_store{half}_pi(reinterpret_cast<__m64*>(uintptr_t{{{self.addr(ins, ops[0])}}}), "
                  f"{ins.reg_name(ops[1].reg)});")
        elif m == "movmskps":
            e(self.reg_write(ins, ops[0].reg, f"(uint32_t)_mm_movemask_ps({ins.reg_name(ops[1].reg)})"))
        elif m.startswith("cmp") and m.endswith(("ss", "ps")):
            kind = m[-2:]
            pred = m[3:-2]
            if not pred:
                pred = CMP_PRED[ops[2].imm & 7]
            if pred not in CMP_PRED:
                raise Unsupported(f"compare {m}")
            e(f"{d} = _mm_cmp{pred}_{kind}({d}, {self.x_src(ins, ops[1], kind == 'ss')});")
        else:
            raise Unsupported(f"mnemonic {m}")
        return True

    # ---- x87 -------------------------------------------------------------------------------------------------
    # The stack depth before each instruction is static (compiler code: equal at every join, empty at calls); st(i)
    # at depth d is the local f<d-1-i>. A float return travels through the Cpu (XL_FRETURN / XL_FRESULT).
    def x87_effect(self, ins, d):
        m = ins.mnemonic
        if m == "call" and self.is_ftol(ins):
            if d < 1:
                raise Unsupported("_ftol without a value")
            return -1
        if m == "call":
            if d:
                raise Unsupported("x87 values live across a call")
            return 1 if ins.address in self.call_fret else 0
        if m in ("jmp",) and ins.operands[0].type == x86.X86_OP_IMM and ins.operands[0].imm not in self.fn.ins and d:
            raise Unsupported("x87 values live across a tail call")
        if not is_x87(ins):
            return 0
        if m in ("fld", "fild", "fld1", "fldz"):
            return 1
        if m in ("fstp", "fistp", "faddp", "fsubp", "fsubrp", "fmulp", "fdivp", "fdivrp", "fcompi", "fcomip",
                 "fucompi", "fucomip"):
            return -1
        if m in ("fst", "fadd", "fsub", "fsubr", "fmul", "fdiv", "fdivr", "fxch", "fabs", "fchs", "fcomi", "fucomi"):
            return 0
        raise Unsupported(f"mnemonic {m}")

    def is_ftol(self, ins):
        """The CRT's _ftol: st(0) popped, truncated to a 64-bit integer in edx:eax."""
        op = ins.operands[0]
        return op.type == x86.X86_OP_IMM and NAMES.get(op.imm) in ("_ftol", "_ftol2")

    def x87_depths(self):
        fn = self.fn
        self.fdepth, self.call_fret, self.ret_float = {}, set(), False
        self.uses_x87 = any(is_x87(i) for i in fn.ins.values())
        if not self.uses_x87:
            return
        # A call returns a float the caller uses when the x87 code after it reaches below what it pushed itself
        # (e.g. fld constant; fcomip st(1): the call's result is st(1)).
        for a, ins in fn.ins.items():
            if ins.mnemonic != "call":
                continue
            n, d, steps = a + ins.size, 0, 0
            while n in fn.ins and steps < 64:
                steps += 1
                j = fn.ins[n]
                if j.mnemonic == "jmp" and j.operands[0].type == x86.X86_OP_IMM and j.operands[0].imm in fn.ins:
                    n = j.operands[0].imm  # merged code: follow the jump
                    continue
                if is_x87(j):
                    if x87_reach(j) > d:
                        self.call_fret.add(a)
                        break
                    d += self.x87_effect(j, d)
                elif j.mnemonic == "call" and self.is_ftol(j):  # _ftol takes st(0)
                    if d < 1:
                        self.call_fret.add(a)
                        break
                    d -= 1
                elif j.mnemonic in ("call", "ret", "retn", "jmp", "int3") or j.mnemonic in cfg.JCC:
                    break
                n += j.size
        depth = {fn.start: 0}
        work = [fn.start]
        rets = set()
        while work:
            a = work.pop()
            ins = self.fn.ins[a]
            d = depth[a]
            if ins.mnemonic in ("ret", "retn"):
                rets.add(d)
            d2 = d + self.x87_effect(ins, d)
            if not 0 <= d2 <= 8:
                raise Unsupported("x87 stack under/overflow")
            for t in self.succ[a]:
                if t in depth:
                    if depth[t] != d2:
                        raise Unsupported("x87 stack depth differs at a join")
                else:
                    depth[t] = d2
                    work.append(t)
        if not rets <= {0, 1} or len(rets) > 1:
            raise Unsupported("x87 stack at return")
        self.ret_float = rets == {1}
        self.fdepth = depth

    def st(self, ins, i):
        d = self.fdepth[ins.address]
        if not 0 <= i < d:
            raise Unsupported("x87 register outside the stack")
        return f"f{d - 1 - i}"

    def st_index(self, ins, op):
        n = ins.reg_name(op.reg)
        if not n.startswith("st"):
            raise Unsupported(f"x87 operand {n}")
        return int(n[3]) if n.startswith("st(") else 0

    def fmem(self, ins, op):
        """A memory operand as a long double (float/double, or int for fi* forms)."""
        a = self.addr(ins, op)
        if op.mem.segment:
            raise Unsupported("x87 fs operand")
        if ins.mnemonic.startswith("fi"):
            return {2: f"(long double)(int16_t)XL_R16({a})", 4: f"(long double)(int32_t)XL_R32({a})"}.get(op.size) \
                or self._unsup("fild size")
        return {4: f"::xl::ld_f32({a})", 8: f"::xl::ld_f64({a})"}.get(op.size) or self._unsup("x87 operand size")

    def _unsup(self, why):
        raise Unsupported(why)

    def x87(self, ins):
        if not is_x87(ins):
            return False
        m, ops, e = ins.mnemonic, ins.operands, self.emit
        d = self.fdepth[ins.address]
        if m in ("fld", "fild"):
            if ops[0].type == x86.X86_OP_MEM:
                e(f"f{d} = {self.fmem(ins, ops[0])};")
            else:
                e(f"f{d} = {self.st(ins, self.st_index(ins, ops[0]))};")
        elif m in ("fld1", "fldz"):
            e(f"f{d} = {'1.0L' if m == 'fld1' else '0.0L'};")
        elif m in ("fst", "fstp"):
            top = self.st(ins, 0)
            if ops[0].type == x86.X86_OP_MEM:
                if op_size(ops[0]) == 4:
                    e(f"::xl::st_f32({self.addr(ins, ops[0])}, {top});")
                elif op_size(ops[0]) == 8:
                    e(f"::xl::st_f64({self.addr(ins, ops[0])}, {top});")
                else:
                    raise Unsupported("fst size")
            else:
                i = self.st_index(ins, ops[0])
                if i:
                    e(f"{self.st(ins, i)} = {top};")
        elif m in ("fadd", "fsub", "fsubr", "fmul", "fdiv", "fdivr", "faddp", "fsubp", "fsubrp", "fmulp", "fdivp",
                   "fdivrp"):
            base = m[:-1] if m.endswith("p") and m != "fsubp" and m != "fdivp" else m
            base = {"fsubp": "fsub", "fdivp": "fdiv"}.get(m, base)
            if len(ops) == 1 and ops[0].type == x86.X86_OP_MEM:
                dst, src = self.st(ins, 0), self.fmem(ins, ops[0])
            elif m.endswith("p") and len(ops) <= 1:  # f*p st(i): st(i) = st(i) op st(0), then pop
                dst, src = self.st(ins, self.st_index(ins, ops[0]) if ops else 1), self.st(ins, 0)
            elif len(ops) == 1:
                dst, src = self.st(ins, 0), self.st(ins, self.st_index(ins, ops[0]))
            else:
                dst = self.st(ins, self.st_index(ins, ops[0]))
                src = self.st(ins, self.st_index(ins, ops[1]))
            expr = {"fadd": f"{dst} + {src}", "fsub": f"{dst} - {src}", "fsubr": f"{src} - {dst}",
                    "fmul": f"{dst} * {src}", "fdiv": f"{dst} / {src}", "fdivr": f"{src} / {dst}"}[base]
            e(f"{dst} = {expr};")
        elif m in ("fcompi", "fcomip", "fucompi", "fucomip", "fcomi", "fucomi"):
            other = self.st(ins, self.st_index(ins, ops[-1]))
            r = f"::xl::eflags_after_fcomi({self.st(ins, 0)}, {other})"
            fl = self.set_flags(ins, "RAW", "0u", "0u", r, 4)
            e("{ " + (fl or f"(void){r};") + " }")
        elif m == "fxch":
            # capstone may list st(0) among the operands: the other register is the one swapped with (default st(1))
            other = max([self.st_index(ins, o) for o in ops if o.type == x86.X86_OP_REG] + [0]) or 1
            a_, b_ = self.st(ins, 0), self.st(ins, other)
            e(f"{{ long double t = {a_}; {a_} = {b_}; {b_} = t; }}")
        elif m == "fabs":
            e(f"{self.st(ins, 0)} = __builtin_fabsl({self.st(ins, 0)});")
        elif m == "fchs":
            e(f"{self.st(ins, 0)} = -{self.st(ins, 0)};")
        else:
            raise Unsupported(f"mnemonic {m}")
        return True

    def call(self, target, nxt):
        if target in self.translated:
            self.callees.add(target)
            self.emit(self.xs + f"XL_CALL_XL(0x{target:08x}, 0x{nxt:x}u);" + self.xr)
        else:
            self.emit(self.xs + f"XL_CALL_NATIVE(0x{target:x}u, 0x{nxt:x}u);" + self.xr)

    def tail(self, target):
        if target in self.translated:
            self.callees.add(target)
            self.emit(self.xs + f"XL_TAIL_XL(0x{target:08x});")
        else:
            self.emit(self.xs + f"XL_TAIL_NATIVE(0x{target:x}u);")

    def carry(self, ins):
        prods = self.reaching.get(ins.address, set())
        if not prods or None in prods:
            raise Unsupported(f"CF of {ins.mnemonic} not set in the function")
        kinds = {self.kind_of[p] for p in prods}
        if any(k.startswith(("INC", "DEC")) for k in kinds):
            raise Unsupported("CF after inc/dec")
        if len(kinds) == 1:
            return f"(uint32_t)XL_CC_b(XL_K_{kinds.pop()}, fa, fb, fr, fc)"
        self.uses_fk = True
        return "(uint32_t)XL_CC_b(fk, fa, fb, fr, fc)"

    def mul1(self, ins, signed):
        op = ins.operands[0]
        size = op.size
        if size != 4:
            raise Unsupported(f"{8 * size}-bit one-operand mul")
        v = self.read(ins, op)
        if signed:
            body = (f"int64_t p = (int64_t)(int32_t)eax * (int64_t)(int32_t){v}; eax = (uint32_t)p; "
                    "edx = (uint32_t)((uint64_t)p >> 32);")
            fl = self.set_flags(ins, "MUL", "(uint32_t)(p != (int64_t)(int32_t)eax)", "0u", "eax", 4)
        else:
            body = f"uint64_t p = (uint64_t)eax * {v}; eax = (uint32_t)p; edx = (uint32_t)(p >> 32);"
            fl = self.set_flags(ins, "MUL", "(uint32_t)(edx != 0u)", "0u", "eax", 4)
        self.emit("{ " + body + " " + fl + " }")

    def string(self, ins):
        m = ins.mnemonic
        rep = m.startswith("rep ")
        base = m[4:] if rep else m
        n = {"b": 1, "w": 2, "d": 4}[base[-1]]
        if base.startswith("movs"):
            one = f"XL_W{8 * n}(edi, XL_R{8 * n}(esi)); esi += {n}u; edi += {n}u;"
        else:
            src = {1: "eax & 0xffu", 2: "eax & 0xffffu", 4: "eax"}[n]
            one = f"XL_W{8 * n}(edi, {src}); edi += {n}u;"
        # The title runs with DF = 0 (the harness checks it); string ops go up.
        self.emit(("for (; ecx; --ecx) { " + one + " }") if rep else one)

    # ---- function --------------------------------------------------------------------------------------------
    def lift(self):
        fn = self.fn
        self.uses_fc = False
        self.uses_xmm = any(op.type == x86.X86_OP_REG and ins.reg_name(op.reg).startswith("xmm")
                            for ins in fn.ins.values() for op in ins.operands)
        self.xs = "XL_SAVE_X(); " if self.uses_xmm else ""
        self.xr = " XL_RELOAD_X();" if self.uses_xmm else ""
        self.flow()
        self.x87_depths()
        blocks = cfg.blocks(fn)
        targets = set()
        for a, insns in blocks:
            last = insns[-1]
            if last.mnemonic in cfg.JCC or last.mnemonic == "jmp":
                targets.add(last.operands[0].imm)
        body = []
        self.lines = body
        if self.uses_x87:
            body.append("    long double f0 = 0, f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f6 = 0, f7 = 0;")
        prev_end = None
        for a, insns in blocks:
            if prev_end is not None and prev_end != a:
                pass  # previous block ended in jmp/ret/jcc+fallthrough handled below
            body.append(f"L_{a:08x}:")
            for ins in insns:
                m = ins.mnemonic
                if m in cfg.JCC:
                    self.emit(f"if ({self.cond(ins, m[1:])}) goto L_{ins.operands[0].imm:08x};")
                    nxt = ins.address + ins.size
                    if not any(b[0] == nxt for b in blocks):
                        raise Unsupported("jcc fall-through outside the function")
                    targets.add(nxt)
                    if ins.operands[0].imm not in fn.ins:
                        raise Unsupported("conditional tail jump")
                elif m in cfg.JCXZ:
                    raise Unsupported(m)
                elif m == "jmp":
                    op = ins.operands[0]
                    if op.type == x86.X86_OP_IMM:
                        if op.imm in fn.ins:
                            self.emit(f"goto L_{op.imm:08x};")
                        else:
                            self.tail(op.imm)
                    else:
                        cases = cfg.switch_table(self.image, ins, self.lo, self.hi)
                        t = self.read(ins, op, 4)
                        if cases:
                            arms = " ".join(f"case 0x{c:x}u: goto L_{c:08x};" for c in sorted(set(cases)))
                            self.emit("switch (" + t + ") { " + arms + " default: ::xl::xl_trap(); }")
                            targets.update(cases)
                        else:
                            self.emit("{ uint32_t t = " + t + "; " + self.xs + "XL_TAIL_DYN(t); }")
                elif m == "call":
                    op = ins.operands[0]
                    nxt = ins.address + ins.size
                    if self.uses_x87 and self.is_ftol(ins):
                        # _ftol inline: C's conversion truncates (as _ftol sets the rounding control to do)
                        self.emit(f"{{ const int64_t v = (int64_t){self.st(ins, 0)}; eax = (uint32_t)v; "
                                  "edx = (uint32_t)((uint64_t)v >> 32); }")
                        continue
                    if ins.address in self.call_fret:
                        self.emit("c->fret = 0u;")
                    if op.type == x86.X86_OP_IMM:
                        self.call(op.imm, nxt)
                    else:
                        self.emit("{ uint32_t t = " + self.read(ins, op, 4) + "; " + self.xs + f"XL_CALL_DYN(t, 0x{nxt:x}u);"
                                  + self.xr + " }")
                    if ins.address in self.call_fret:
                        self.emit("XL_FRESULT(f0);")
                elif m in ("ret", "retn"):
                    n = ins.operands[0].imm if ins.operands else 0
                    if self.ret_float:
                        self.emit("XL_FRETURN(f0);")
                    self.emit(self.xs + f"XL_RETURN({n}u);")
                elif m == "int3":
                    self.emit("::xl::xl_trap();")
                else:
                    self.lift_ins(ins)
            last = insns[-1]
            nxt = last.address + last.size
            if last.mnemonic not in ("jmp", "ret", "retn", "int3") and last.mnemonic not in cfg.JCC:
                if nxt in fn.ins:  # falls through into the next leader
                    self.emit(f"goto L_{nxt:08x};")
                    targets.add(nxt)
                else:  # falls into the next function: a tail call
                    self.tail(nxt)
            elif last.mnemonic in cfg.JCC:
                self.emit(f"goto L_{nxt:08x};")
        # drop labels nobody jumps to (entry kept)
        out = []
        for line in body:
            if line.startswith("L_") and int(line[2:10], 16) not in targets and int(line[2:10], 16) != fn.start:
                continue
            out.append(line)
        return out


def is_x87(ins):
    return ins.mnemonic.startswith("f") and ins.mnemonic not in ("femms",)


def x87_reach(ins):
    """How many stack values the instruction reads (st(0) is 1)."""
    m = ins.mnemonic
    if m in ("fld1", "fldz") or (m in ("fld", "fild") and ins.operands[0].type == x86.X86_OP_MEM):
        return 0
    deep = [int(ins.reg_name(o.reg)[3]) + 1 for o in ins.operands
            if o.type == x86.X86_OP_REG and ins.reg_name(o.reg).startswith("st(")]
    if m in ("fld", "fxch") and not deep:
        return 2 if m == "fxch" else 0
    return max([1] + deep)


def op_size(op):
    return op.size


def reads_flags(ins):
    m = ins.mnemonic
    return (m in cfg.JCC or m in cfg.SETCC or m in cfg.CMOV or m in ("adc", "sbb", "lahf"))


def writes_flags_unknown(ins):
    """Instructions that change flags in a way we do not model (a reader after them is unsupported)."""
    return ins.mnemonic in ("rol", "ror", "shld", "shrd", "div", "idiv", "popfd", "sahf", "call")


HEADER = """// Generated by tools/translator/lift.py from default.xbe - do not edit.
// Translated level code ({level}): each function behaves as the original x86 at its address.
#include "xlat_runtime.h"

#pragma GCC diagnostic ignored "-Wunused-label"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

namespace xl {{
"""


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("level", nargs="?", default="PROLOGUE")
    ap.add_argument("--class", dest="classes", default=",".join(cfg.RANK))
    ap.add_argument("--only")
    ap.add_argument("--skip", help="addresses left to the original code (bisecting)")
    ap.add_argument("--out", default="build/translator/gen")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    functions, _ = fnpack.load_names()
    NAMES.update(functions)
    image = fnpack.Image()
    sec = next(s for s in image.sections if s[0] == args.level)
    lo, hi = sec[1], sec[1] + max(sec[2], sec[4])
    starts = {a: n for a, n in functions.items() if lo <= a < hi}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    only = {int(x, 16) for x in args.only.split(",")} if args.only else None
    classes = set(args.classes.split(","))
    candidates = []
    for a in sorted(starts):
        if only is not None and a not in only:
            continue
        if args.skip and a in {int(x, 16) for x in args.skip.split(",")}:
            continue
        fn = cfg.Fn(a, starts[a])
        cfg.decode(fn, image, md, lo, hi, starts)
        if only is None and fn.cls not in classes:
            continue
        candidates.append(fn)
    # Pass 1 finds what lifts; pass 2 emits calls between those as C calls (a call never makes a lift fail).
    translated = {fn.start for fn in candidates}
    for _ in range(2):
        lifted, skipped = [], []
        for fn in candidates:
            try:
                lf = Lifter(fn, starts, translated, image, lo, hi)
                lifted.append((fn, lf.lift(), lf))
            except Unsupported as why:
                skipped.append((fn, str(why)))
        translated = {fn.start for fn, _, _ in lifted}
    level = args.level.lower()
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, f"{level}_xl.cpp")
    with open(path, "w") as f:
        f.write(HEADER.format(level=args.level))
        for fn, _, _ in lifted:
            f.write(f"XL_DECLARE(0x{fn.start:08x})\n")
        for index, (fn, body, lf) in enumerate(lifted):
            f.write(f"\n// {fn.name} ({len(fn.ins)} instructions)\n")
            f.write(f"XL_FUNCTION(0x{fn.start:08x}) {{\n    XL_LOAD();\n" + ("    XL_LOAD_X();\n" if lf.uses_xmm else ""))
            f.write("    uint32_t fa = 0, fb = 0, fr = 0, fc = 0" + (", fk = 0" if lf.uses_fk else "") + ";\n")
            f.write("\n".join(body) + "\n}\n")
            f.write(f"XL_ENTRY(0x{fn.start:08x}, {index})\n")
        f.write("\nconst Entry kTable[] = {\n")
        for fn, _, _ in lifted:
            f.write(f"    {{0x{fn.start:08x}u, xl_fn_0x{fn.start:08x}}},\n")
        f.write("};\n[[maybe_unused]] const int kRegistered = (register_table(kTable, sizeof kTable / sizeof *kTable), 0);\n")
        f.write("\n}  // namespace xl\n\nnamespace game {\n\n")
        f.write(f"extern const Function kTranslated_{level}[];\nextern const size_t kTranslated_{level}_count;\n")
        f.write(f"extern const Function kTranslated_{level}[] = {{\n")
        for fn, _, _ in lifted:
            f.write(f'    {{"xl_{fn.name}", 0x{fn.start:08x}, 0, XL_CODE(0x{fn.start:08x}), {fn.arg_pop or 0}, '
                    "Result::Eax, Status::Compare},\n")
        f.write("};\n")
        f.write(f"extern const size_t kTranslated_{level}_count = {len(lifted)};\n\n}}  // namespace game\n")
    print(f"{args.level}: {len(lifted)} lifted, {len(skipped)} skipped -> {path}")
    reasons = collections.Counter(why for _, why in skipped)
    for why, n in reasons.most_common():
        print(f"  skipped {n:4d}: {why}")
    if args.verbose:
        for fn, why in skipped:
            print(f"  skip {fn.start:#010x} {fn.name}: {why}")


if __name__ == "__main__":
    main()
