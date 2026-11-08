#!/usr/bin/env python3
"""C for the level sections' generated thread plumbing, from the disassembly (a symbolic pass).

The levels' compiler emitted, per bound argument list, a thread-start wrapper and a functor class
(constructor, Invoke, destructor). Their bodies differ in sizes, offsets and the order of copies, so
masked-hash twins (tools/near_twins.py) do not cover them; this pass evaluates each body over symbolic
values (arguments, the functor/object, its fields) and emits it with the engine's helpers:

  Scr_StartThread_*  push SIZE; call [ScrIface_Alloc]; field stores; Scr_CreateThread
                     -> start_thread(args, SIZE, VTABLE, fields, N)  (g_script.cpp; unhookable: push imm8 +
                        indirect call in the first 5 bytes -> C kept, set_aside "unhookable")
                     SEH frame + constructor (a[first], &a[first+1], a[first+2]...)
                     -> create_thread(args, construct_functor(args, SIZE, CTOR, first, count))  (registered)
  *_Ctor             thiscall stores into the object -> put/word/byte_at/construct (scr_class.cpp)
  *_Invoke           builds the thread function's by-value argument block and calls [this+4]
                     -> build_NAME + call_body(self, BYTES, ...) (scr_class.cpp; registered, kept original:
                        script thread switch, as the engine's Invokes)

Rows whose body leaves the pattern are skipped with the reason (--why).
Output: src/engine/game/g_script_gen.inc, src/engine/script/scr_class_gen.inc (included at the end of their .cpp),
declarations in src/engine/levels/levels_twins.h, kFunctions entries, functions.csv zero rows, set_aside.csv rows.

usage: tools/gen_functors.py [--module prologue] [--dry-run] [--why] [--only NAME,...]
Rerun-safe: rows already in kFunctions, set_aside.csv or defined in src/engine are skipped.
"""
import argparse
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402
import engine_src

ROOT = fnpack.ROOT
CODE = engine_src.ENGINE
FUNCTIONS_CPP = engine_src.FUNCTIONS_CPP
FUNCTIONS_CSV = fnpack.DATA / "functions.csv"
SET_ASIDE = fnpack.DATA / "set_aside.csv"
HEADER = engine_src.path("levels_twins.h")
ENTRY = re.compile(r'^    \{"(\w+)", (0x[0-9a-fA-F]+),', re.M)

ALLOC_SLOT, CREATE_SLOT = 0x922928, 0x92216c
REGS = ("eax", "ecx", "edx", "ebx", "esi", "edi", "ebp")
LOW8 = {"al": "eax", "cl": "ecx", "dl": "edx", "bl": "ebx"}


class Skip(Exception):
    pass


# Symbolic values: ("arg", k) the k-th stack word at entry (k=0: return address+4), ("this",) the
# thiscall object, ("obj",) the allocated functor, ("stk", off) the address entry_esp+off,
# ("ptr", base, disp) = base + disp, ("load", addr_value, size) a load, ("imm", n), ("ret",) a call result,
# None unknown.
def add(v, d):
    if d == 0:
        return v
    if v and v[0] == "stk":
        return ("stk", v[1] + d)
    if v and v[0] == "ptr":
        return ("ptr", v[1], v[2] + d)
    if v and v[0] in ("this", "obj", "load", "arg", "ret"):
        return ("ptr", v, d)
    raise Skip(f"pointer arithmetic on {v}")


def base_disp(v):
    if v and v[0] == "ptr":
        return v[1], v[2]
    return v, 0


class Machine:
    def __init__(self, image, va, size):
        self.rows = fnpack.disassemble(image, va, size)
        self.at = {r[0]: i for i, r in enumerate(self.rows)}
        self.reg = {r: None for r in REGS}
        self.reg["esp"] = ("stk", 0)
        self.reg["ecx"] = ("this",)
        self.mem = {}  # stack offset -> (value, size)
        self.events = []  # ("store", addr, value, size) / ("call", target, ecx, regs, esp, mem, pops)
        self.calls = 0
        self.tick, self.mseq = 0, {}  # store order: stack offset -> tick of its last write

    def operand(self, text, size=4):
        """Value of a register / immediate / memory operand."""
        text = text.strip()
        if text in self.reg:
            return self.reg[text]
        if text in LOW8:  # a byte register: the byte that was loaded into it (or the low byte of the word)
            v = self.reg[LOW8[text]]
            return v[1] if v and v[0] in ("zx8", "lo8set") else v
        if re.fullmatch(r"0x[0-9a-f]+|\d+", text):
            return ("imm", int(text, 0))
        addr = self.address(text)
        if addr and addr[0] == "stk":
            if addr[1] >= 4:
                if size == 4 and addr[1] % 4 == 0 and addr[1] not in self.mem:
                    return ("arg", addr[1] // 4 - 1)
            if addr[1] in self.mem:
                value, stored = self.mem[addr[1]]
                if stored == size:
                    return value
            raise Skip(f"stack read {text}")
        return ("load", addr, size)

    def address(self, text):
        m = re.fullmatch(r"(?:(?:DWORD|BYTE|WORD) PTR )?(?:ds:)?\[?([^\]]*)\]?", text)
        inner = m.group(1)
        if re.fullmatch(r"0x[0-9a-f]+", inner):
            return ("imm", int(inner, 16))
        m = re.fullmatch(r"(\w+)(?:([+-])(0x[0-9a-f]+))?", inner)
        if not m or m.group(1) not in self.reg:
            raise Skip(f"address {text}")
        d = int(m.group(3), 16) * (-1 if m.group(2) == "-" else 1) if m.group(3) else 0
        return add(self.reg[m.group(1)], d)

    def store(self, dest, value, size):
        addr = self.address(dest)
        if addr and addr[0] == "stk":
            self.mem[addr[1]] = (value, size)
            self.tick += 1
            self.mseq[addr[1]] = self.tick
            for o in list(self.mem):
                if o != addr[1] and addr[1] < o < addr[1] + size:
                    del self.mem[o]
        else:
            self.events.append(("store", addr, value, size))

    def push(self, value):
        self.reg["esp"] = add(self.reg["esp"], -4)
        self.mem[self.reg["esp"][1]] = (value, 4)
        self.tick += 1
        self.mseq[self.reg["esp"][1]] = self.tick

    def run(self, follow_nonnull=True):
        """Straight-line evaluation; `test r,r; je` after an allocation follows the non-null path."""
        i, steps = 0, 0
        while i < len(self.rows):
            steps += 1
            if steps > 400:
                raise Skip("loop")
            va, _, t = self.rows[i]
            i += 1
            mn, _, rest = t.partition(" ")
            ops = fnpack.split_operands(t)[1] if rest else []
            if t.startswith(("mov eax,fs:", "mov DWORD PTR fs:", "mov fs:", "mov ecx,DWORD PTR fs:")):
                if t.startswith("mov ecx"):
                    self.reg["ecx"] = None
                continue
            if mn == "push":
                size = 4
                v = self.operand(ops[0], size)
                self.push(v)
            elif mn == "pop":
                off = self.reg["esp"][1]
                self.reg[ops[0]] = self.mem.get(off, (None, 4))[0]
                self.reg["esp"] = add(self.reg["esp"], 4)
            elif mn in ("mov", "movzx"):
                dst, src = ops
                size = 1 if ("BYTE PTR" in dst or dst in LOW8 or "BYTE PTR" in src) else 4
                if "[" in dst or "ds:" in dst:
                    self.store(dst, self.operand(src, size), size)
                elif dst in LOW8:
                    full = LOW8[dst]
                    old = self.reg[full]
                    v = self.operand(src, 1)
                    self.reg[full] = ("zx8", v) if old == ("imm", 0) else ("lo8set", v)
                else:
                    v = self.operand(src, 1 if mn == "movzx" else 4)
                    if mn == "movzx":
                        v = ("zx8", v)
                    self.reg[dst] = v
            elif mn == "lea":
                self.reg[ops[0]] = self.address(ops[1])
            elif mn == "xor" and ops[0] == ops[1]:
                self.reg[ops[0]] = ("imm", 0)
            elif mn in ("add", "sub") and ops[0] == "esp":
                n = int(ops[1], 16)
                self.reg["esp"] = add(self.reg["esp"], n if mn == "add" else -n)
            elif mn in ("add", "sub", "inc", "dec") and ops[0] in self.reg:
                if mn in ("inc", "dec"):
                    self.reg[ops[0]] = None
                elif re.fullmatch(r"0x[0-9a-f]+", ops[1]):
                    n = int(ops[1], 16)
                    self.reg[ops[0]] = add(self.reg[ops[0]], n if mn == "add" else -n)
                else:
                    raise Skip(t)
            elif mn in ("test", "cmp"):
                self.last_test = t
            elif mn in ("je", "jne"):
                target = int(ops[0], 16)
                if not getattr(self, "last_test", "").startswith("test eax,eax") or not follow_nonnull:
                    raise Skip(f"branch {t} after {getattr(self, 'last_test', '')}")
                if mn == "jne":
                    i = self.at[target]
            elif mn == "jmp":
                i = self.at.get(int(ops[0], 16), None)
                if i is None:
                    raise Skip(t)
            elif mn == "call":
                target = ops[0]
                m = re.fullmatch(r"DWORD PTR ds:(0x[0-9a-f]+)", target)
                if m:
                    tv = ("slot", int(m.group(1), 16))
                elif re.fullmatch(r"0x[0-9a-f]+", target):
                    tv = ("fn", int(target, 16))
                else:
                    tv = ("ind", self.address(target))
                esp = self.reg["esp"][1]
                pops = ret_pop(tv[1]) if tv[0] == "fn" else 0  # slot and [this+4] callees are cdecl
                self.tick += 1
                self.events.append(("call", tv, self.reg["ecx"], dict(self.reg), esp, dict(self.mem), pops, self.tick))
                self.calls += 1
                for r in ("eax", "ecx", "edx"):
                    self.reg[r] = None
                self.reg["eax"] = ("ret", self.calls)
                self.reg["esp"] = add(self.reg["esp"], pops)
            elif mn == "ret":
                self.ret = int(ops[0], 16) if ops else 0
                self.ret_eax = self.reg["eax"]
                return
            else:
                raise Skip(f"instruction {t}")
        raise Skip("no ret")


SIZES, POPS = {}, {}


def ret_pop(va, depth=0):
    """Bytes a callee pops (its ret N; a tail jump: the target's)."""
    if va not in POPS:
        rows = fnpack.disassemble(IMAGE, va, SIZES.get(va, 0x200))
        POPS[va] = None
        for _, _, t in rows:
            if t.startswith("ret"):
                POPS[va] = int(t.split()[1], 16) if " " in t else 0
                break
            m = re.fullmatch(r"jmp (0x[0-9a-f]+)", t)
            if m and not va <= int(m.group(1), 16) < va + SIZES.get(va, 0x200) and depth < 4:
                POPS[va] = ret_pop(int(m.group(1), 16), depth + 1)
                break
        if POPS[va] is None:
            raise Skip(f"callee {va:#x}: no ret")
    return POPS[va]


def arg_words(mem, esp, count):
    return [mem.get(esp + 4 * k, (None, 4))[0] for k in range(count)]


def gen_start_thread(image, va, size, name):
    rows = fnpack.disassemble(image, va, size)
    if len(rows) < 2:
        raise Skip("short")
    m = Machine(image, va, size)
    m.reg["ecx"] = None
    m.run()
    calls = [e for e in m.events if e[0] == "call"]
    if not calls or calls[0][1] != ("slot", ALLOC_SLOT):
        raise Skip("no ScrIface_Alloc first")
    first = calls[0]
    fsize = first[5].get(first[4], (None,))[0]
    if not fsize or fsize[0] != "imm":
        raise Skip("alloc size")
    last = calls[-1]
    if last[1] != ("slot", CREATE_SLOT):
        raise Skip(f"last call {last[1]}")
    words = arg_words(last[5], last[4], 6)
    functor = ("ret", 1)
    ctor_calls = [e for e in calls[1:-1]]
    if ctor_calls:
        if len(ctor_calls) != 1 or ctor_calls[0][1][0] != "fn" or ctor_calls[0][2] != functor:
            raise Skip("constructor call shape")
        functor = ("ret", 2)
    if words != [("arg", 1), ("arg", 2), ("arg", 3), ("arg", 5), functor, ("arg", 0)]:
        raise Skip(f"Scr_CreateThread arguments {words}")
    if m.ret != 0 or m.ret_eax != ("ret", len(calls)):
        raise Skip("result")
    hookable = not (rows[0][2].startswith("push 0x") and rows[0][1] == 2 and rows[1][2].startswith("call DWORD PTR"))
    if ctor_calls:
        c = ctor_calls[0]
        ctor = c[1][1]
        mem, esp = c[5], c[4]
        n = c[6] // 4
        got = arg_words(mem, esp, n)
        # pushed words of the constructor: the stack above its arguments is the frame (locals): count
        # the words that are this function's arguments
        k0 = got[0][1] if got and got[0] and got[0][0] == "arg" else None
        if k0 is None:
            raise Skip("constructor first argument")
        count = 1
        want = [("arg", k0), ("stk", 4 * (k0 + 2))]
        while count < len(got):
            exp = want[count] if count < 2 else ("arg", k0 + count)
            if got[count] != exp:
                break
            count += 1
        if count < 2 or count != n:
            raise Skip(f"constructor arguments {got}")
        stack = 4 * (k0 + count)
        body = (f"    return create_thread(args, construct_functor(args, {fsize[1]:#x}, {ctor:#010x}, {k0}, {count}));"
                f"  // {NAMES.get(ctor, f'{ctor:#010x}')}")
        return dict(kind="start", body=body, stack=stack, hookable=hookable)
    # inline functor: {vtable, a4, a5, fields from *a6, *a7, ...}
    stores = [e for e in m.events if e[0] == "store"]
    vtable, fields, seen = None, [], set()
    for _, addr, value, sz in stores:
        b, d = base_disp(addr)
        if b != ("ret", 1):
            raise Skip(f"store to {addr}")
        if d == 0 and value and value[0] == "imm":
            vtable = value[1]
        elif d == 4 and value == ("arg", 4):
            pass
        elif d == 8 and value == ("arg", 5):
            pass
        elif value and value[0] == "load":
            sb, sd = base_disp(value[1])
            if not (sb and sb[0] == "arg" and sb[1] >= 6):
                raise Skip(f"field source {value}")
            fields.append((sb[1], sd, d, sz))
        else:
            raise Skip(f"stored value {value}")
    if vtable is None:
        raise Skip("no vtable store")
    # group per argument: contiguous copies of one pointed-to value
    groups = {}
    for k, sd, d, sz in fields:
        groups.setdefault(k, []).append((sd, d, sz))
    out = []
    for i, k in enumerate(sorted(groups)):
        if k != 6 + i:
            raise Skip("field arguments not consecutive")
        parts = sorted(groups[k])
        start = parts[0][1] - parts[0][0]
        total = 0
        for sd, d, sz in parts:
            if d - sd != start or sd != total:
                raise Skip("field copy layout")
            total += sz
        out.append((start, total))
    stack = 4 * (6 + len(out))
    if not out:
        stack = 24
    fl = ", ".join(f"{{{o:#x}, {s}}}" for o, s in out)
    body = ""
    if out:
        body += f"    static const FunctorField fields[] = {{{fl}}};\n"
        body += f"    return start_thread(args, {fsize[1]:#x}, {vtable:#010x}, fields, {len(out)});"
    else:
        body += f"    return start_thread(args, {fsize[1]:#x}, {vtable:#010x}, nullptr, 0);"
    return dict(kind="start", body=body, stack=stack, hookable=hookable)


def value_c(v, self_name="self"):
    """C expression for a value in a constructor (arguments a1..: stack word k = a{k+1})."""
    if v is None:
        raise Skip("unknown value")
    if v[0] == "arg":
        return f"a{v[1] + 1}"
    if v[0] == "imm":
        return f"{v[1]:#x}"
    raise Skip(f"value {v}")


def gen_ctor(image, va, size, name):
    m = Machine(image, va, size)
    m.run(follow_nonnull=False)
    if m.ret_eax != ("this",):
        raise Skip("result is not this")
    nargs = m.ret // 4
    lines = []
    for e in m.events:
        if e[0] == "store":
            _, addr, value, sz = e
            b, d = base_disp(addr)
            if b != ("this",):
                raise Skip(f"store to {addr}")
            if sz == 1:
                if value and value[0] == "load" and value[2] == 1 and base_disp(value[1])[0][0] == "arg" \
                        and base_disp(value[1])[1] == 0:
                    lines.append(f"    self[{d:#x}] = byte_at({value_c(base_disp(value[1])[0])});")
                else:
                    raise Skip(f"byte store {value}")
            elif value and value[0] == "load":
                sb, sd = base_disp(value[1])
                lines.append(f"    put(self, {d:#x}, word({value_c(sb)}, {sd:#x}));" if sd else
                             f"    put(self, {d:#x}, word({value_c(sb)}, 0));")
            else:
                c = value_c(value)
                lines.append(f"    put(self, {d:#x}, {c if value[0] == 'arg' else '0x' + format(value[1], 'x')});"
                             if value[0] != "imm" or value[1] else f"    put(self, {d:#x}, 0);")
        else:
            _, tv, ecx, regs, esp, mem, _, _ = e
            if tv[0] != "fn":
                raise Skip(f"call {tv}")
            b, d = base_disp(ecx)
            if b != ("this",):
                raise Skip("member constructor on another object")
            arg = mem.get(esp, (None,))[0]
            lines.append(f"    construct({tv[1]:#010x}, self + {d:#x}, {value_c(arg)});"
                         f"  // {NAMES.get(tv[1], hex(tv[1]))}")
    params = "".join(f", uint32_t a{k + 1}" for k in range(nargs))
    head = f"uint8_t* XB_THISCALL {name}(uint8_t* self{params})"
    body = "\n".join(lines) + "\n    return self;"
    return dict(kind="ctor", head=head, body=body, stack=m.ret)


def gen_invoke(image, va, size, name):
    m = Machine(image, va, size)
    m.run(follow_nonnull=False)
    calls = [e for e in m.events if e[0] == "call"]
    body_call = calls[-1] if calls else None
    if not body_call or body_call[1] != ("ind", ("ptr", ("this",), 4)):
        raise Skip("no call [this+4]")
    if m.ret != 0:
        raise Skip("ret N")
    # the block: from esp at the call up to the add esp after it
    rows = m.rows
    idx = max(i for i, r in enumerate(rows) if r[2].startswith("call "))
    pop = None
    for r in rows[idx + 1:idx + 4]:
        mm = re.fullmatch(r"add esp,(0x[0-9a-f]+)", r[2])
        if mm:
            pop = int(mm.group(1), 16)
            break
        if re.fullmatch(r"pop e[a-d]x", r[2]):  # a one-word block
            pop = 4
            break
    if pop is None:
        raise Skip("no add esp after the body call")
    esp, mem, regs = body_call[4], body_call[5], body_call[3]
    # copy constructors into the block and the words stored there, in the original's order (a word stored
    # before a constructor runs on its member - DynArray's zeroed {data, ?, size} - stays before it)
    items, ctors = [], {}
    for e in calls[:-1]:
        _, tv, ecx, cregs, cesp, cmem, _, tick = e
        if tv[0] != "fn" or not ecx or ecx[0] != "stk" or not (esp <= ecx[1] < esp + pop):
            raise Skip(f"call {tv} on {ecx}")
        src = cmem.get(cesp, (None,))[0]
        sb, sd = base_disp(src)
        if sb != ("this",):
            raise Skip("copy source")
        o = ecx[1] - esp
        items.append((tick, f"    construct_arg({tv[1]:#010x}, args + {o:#x}, address(self + {sd:#x}));"
                            f"  // {NAMES.get(tv[1], hex(tv[1]))}"))
        ctors[o] = tick
    for o in range(0, pop, 4):
        tick = m.mseq.get(esp + o)
        v, sz = mem.get(esp + o, (None, 4))
        if o in ctors and (tick is None or tick < ctors[o]) and not (v and v[0] == "imm"):
            continue  # the word a copy constructor fills (reserved by a push; a stored constant stays)
        if tick is None:
            if any(c < o for c in ctors):
                continue  # inside a constructed member wider than one word
            raise Skip(f"block word {o:#x} unknown")
        if v and v[0] == "load" and base_disp(v[1])[0] == ("this",):
            line = f"    put(args, {o:#x}, word_at(self, {base_disp(v[1])[1]:#x}));"
        elif v and v[0] == "zx8" and v[1] and v[1][0] == "load" and base_disp(v[1][1])[0] == ("this",):
            line = f"    put(args, {o:#x}, byte_of(self, {base_disp(v[1][1])[1]:#x}));"
        elif v and v[0] == "imm":
            line = f"    put(args, {o:#x}, {v[1]:#x}u);"
        else:
            raise Skip(f"block word {o:#x} = {v}")
        items.append((tick, line))
    lines = [line for _, line in sorted(items)]

    def reg_c(v):
        if v is None or v[0] == "ret":
            return "0;  // whatever the copy constructor left"
        if v[0] == "stk":
            return f"address(args + {v[1] - esp:#x});"
        if v[0] == "this":
            return "address(self + 0x0);"
        if v[0] == "ptr" and v[1] == ("this",):
            return f"address(self + {v[2]:#x});"
        if v[0] == "load" and base_disp(v[1])[0] == ("this",):
            return f"word_at(self, {base_disp(v[1])[1]:#x});"
        if v[0] == "zx8" and v[1] and v[1][0] == "load" and base_disp(v[1][1])[0] == ("this",):
            return f"byte_of(self, {base_disp(v[1][1])[1]:#x});"
        if v[0] == "imm":
            return f"{v[1]:#x};"
        raise Skip(f"register value {v}")

    for r in ("eax", "ecx", "edx"):
        lines.append(f"    regs->{r} = {reg_c(regs[r])}")
    build = (f"static void build_{name}(uint8_t* args, uint8_t* self, BodyRegs* regs) {{\n" + "\n".join(lines)
             + "\n}\n\n")
    return dict(kind="invoke", build=build, bytes=pop, stack=0)


NAMES = {}
IMAGE = None


def main():
    global NAMES, IMAGE
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--module", default="prologue")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--why", action="store_true")
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    NAMES = fnpack.load_names()[0]
    image = IMAGE = fnpack.Image()
    rows = list(csv.DictReader(open(ROOT / "build" / "all_body_hashes.csv")))
    SIZES.update({int(r["address"], 16): int(r["size"]) for r in rows})
    text = FUNCTIONS_CPP.read_text()
    have = {int(m.group(2), 16) for m in ENTRY.finditer(text)}
    aside = {int(l.split(",", 1)[0], 16) for l in open(SET_ASIDE).read().split("\n")[1:] if l}
    defined = set()
    for p in engine_src.files("*.cpp"):
        defined.update(re.findall(r"\bXB_\w+\s+(\w+)\(", p.read_text()))
    for p in engine_src.files("*.inc"):
        defined.update(re.findall(r"\bXB_\w+\s+(\w+)\(", p.read_text()))
    only = set(filter(None, args.only.split(",")))
    made, skipped = [], []
    for r in rows:
        a, name = int(r["address"], 16), r["name"]
        if r["section"] != "levels" or r["module"] != args.module or a in have or a in aside or name in defined:
            continue
        if only and name not in only:
            continue
        gen = (gen_start_thread if name.startswith("Scr_StartThread_") else
               gen_ctor if re.match(r"AeThreadFunctor\d+_\w+_Ctor$", name) else
               gen_invoke if re.match(r"AeThreadFunctor\d+_\w+_Invoke$", name) else None)
        if not gen:
            continue
        try:
            got = gen(image, a, int(r["size"]), name)
        except Skip as e:
            skipped.append((a, name, str(e)))
            continue
        made.append((a, name, got))
        print(f"{a:08x} {name:55} {got['kind']}" + ("" if got.get("hookable", True) else " (unhookable)"))
    if args.why:
        for a, name, why in skipped:
            print(f"skip {a:08x} {name}: {why}")
    print(f"{len(made)} generated in {args.module}, {len(skipped)} skipped")
    if args.dry_run or not made:
        return

    incs = {"g_script": [], "scr_class": []}
    decls, entries, csv_rows, aside_rows = [], [], [], []
    for a, name, g in made:
        if g["kind"] == "start":
            incs["g_script"].append(f"// {a:#010x}\nextern \"C\" PLATFORM_HIDDEN uint32_t XB_CDECL {name}(const uint32_t* args);\n"
                                    f"SCRIPT_ARGS_ENTRY({name})\nuint32_t XB_CDECL {name}(const uint32_t* args) {{\n"
                                    f"{g['body']}\n}}\n")
            decls.append(f'extern "C" void {name}_entry(void);')
            if g["hookable"]:
                entries.append((a, f'    {{"{name}", {a:#010x}, 0, code(&{name}_entry), {g["stack"]}, Result::Eax, '
                                   f'Status::Compare}},  // tools/gen_functors.py'))
            else:
                aside_rows.append(f'{a:08x},{name},unhookable,"push imm8 + indirect call in the first 5 bytes; '
                                  f'C kept (tools/gen_functors.py)"')
                continue
        elif g["kind"] == "ctor":
            incs["scr_class"].append(f"// {a:#010x}\n{g['head']} {{\n{g['body']}\n}}\n")
            decls.append(g["head"] + ";")
            entries.append((a, f'    {{"{name}", {a:#010x}, 0, code(&{name}), {g["stack"]}, Result::Eax, '
                               f'Status::Compare}},  // tools/gen_functors.py'))
        else:
            incs["scr_class"].append(f"// {a:#010x}\n{g['build']}void XB_THISCALL {name}(uint8_t* self) "
                                     f"{{ call_body(self, {g['bytes']:#x}, &build_{name}); }}\n")
            decls.append(f"void XB_THISCALL {name}(uint8_t* self);")
            entries.append((a, f'    {{"{name}", {a:#010x}, 0, code(&{name}), 0, Result::None, Status::Compare}},  '
                               f'// tools/gen_functors.py; kept original: script thread switch (as the engine\'s Invokes)'))
        csv_rows.append(f'{a:08x},{name},compare,0,0,0,0,0,0,""')

    for stem, items in incs.items():
        if not items:
            continue
        inc = engine_src.path(f"{stem}_gen.inc")
        cpp = engine_src.path(f"{stem}.cpp")
        if not inc.exists():
            inc.write_text("// Generated by tools/gen_functors.py - level-section thread plumbing (symbolic pass over the\n"
                           f"// disassembly). Included at the end of {cpp.name} (inside namespace game).\n\n")
            src = cpp.read_text()
            at = src.rstrip().rfind("}  // namespace game")
            cpp.write_text(src[:at] + f'#include "{inc.name}"\n\n' + src[at:])
        with open(inc, "a") as f:
            f.write(("\n" if inc.read_text().endswith("}\n") else "") + "\n".join(items))
    h = HEADER.read_text()
    h = h.replace("\n}  // namespace game", "".join(f"\n{d}" for d in decls) + "\n\n}  // namespace game", 1)
    HEADER.write_text(h)
    lines = text.split("\n")
    for a, line in entries:
        idx = [(i, int(mm.group(1), 16)) for i, l in enumerate(lines)
               for mm in [re.match(r'\s+\{"\w+", 0x([0-9a-fA-F]+),', l)] if mm]
        at = next((i for i, x in idx if x > a), idx[-1][0] + 1)
        lines.insert(at, line)
    FUNCTIONS_CPP.write_text("\n".join(lines))
    csv_lines = FUNCTIONS_CSV.read_text().split("\n")
    head, body = csv_lines[0], [l for l in csv_lines[1:] if l]
    have_csv = {l.split(",", 1)[0] for l in body}
    body = sorted(body + [l for l in csv_rows if l.split(",", 1)[0] not in have_csv], key=lambda l: l.split(",", 1)[0])
    FUNCTIONS_CSV.write_text("\n".join([head] + body) + "\n")
    if aside_rows:
        with open(SET_ASIDE, "a") as f:  # appended: the file keeps its own (unsorted) order
            f.write("\n".join(aside_rows) + "\n")


if __name__ == "__main__":
    main()
