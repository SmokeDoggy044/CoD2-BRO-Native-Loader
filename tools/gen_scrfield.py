#!/usr/bin/env python3
"""Generates the ScrField_* template instances (game/code/scr_field.cpp).

The title has hundreds of field accessors that are one function per field hash. Each
shape (SHAPES below) is one template in scr_field.cpp; this script finds the members
of each shape in the function list, checks that each really has the reference
function's disassembly (tools/shapes.py: immediates masked) with one hash, and writes
  game/code/scr_field_instances.h    extern template lines + ScrField_<name> pointers
  game/code/scr_field_instances.inc  the explicit instantiations
  game/code/functions.cpp            a kFunctions entry per member (missing ones only)
  tools/data/functions.csv           a zero row per member (missing ones only)

usage: tools/gen_scrfield.py LIST [--dry-run]
  LIST  lines "address state bytes name" (tools/progress.py style), e.g. wt/scr_field_list.txt
Rerun after adding a shape; existing entries are kept (their status may have moved on).
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402
import shapes  # noqa: E402

ROOT = fnpack.ROOT
CODE = ROOT / "game" / "code"
FUNCTIONS_CPP = CODE / "functions.cpp"
FUNCTIONS_CSV = fnpack.DATA / "functions.csv"

# One entry per shape: the template, the member whose disassembly defines the shape,
# extra template arguments after the hash, the signature, stack bytes, result kind,
# how many times the hash appears in the body, and "ecx" for an asm entry that hands
# the caller's ECX on as the last parameter, "esp" for one that moves the C frame below a
# gap and passes (entry ESP, ECX): the body runs its title call where the original does.
SHAPES = [
    dict(template="ScrField_Slot", ref="ScrField_dropped", extra=[0x00922ec0],
         ret="uint32_t*", params="uint32_t", stack=4, result="Eax", hashes=2),
    dict(template="ScrField_SlotIsDefined", ref="ScrField_dropped_IsDefined", extra=[0x00922ec0],
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrField_String", ref="ScrField_explode2",
         ret="ScriptString*", params="uint32_t", stack=4, result="Eax", hashes=1),
    dict(template="ScrField_StringIsDefined", ref="ScrField_explode2_IsDefined",
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
    # batch 2
    dict(template="ScrField_BoolSlot", ref="ScrField_h19738af8", entry="ecx",
         ret="uint32_t*", params="uint32_t, uint32_t", stack=4, result="Eax", hashes=2),
    dict(template="ScrField_BoolSlotIsDefined", ref="ScrField_h19738af8_IsDefined",
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrField_Slot", ref="ScrField_h25bf26c6", extra=[0x00922eac],  # float: a word copy
         ret="uint32_t*", params="uint32_t", stack=4, result="Eax", hashes=2),
    dict(template="ScrField_FloatSlotIsDefined", ref="ScrField_h25bf26c6_IsDefined",
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrField_Slot", ref="ScrField_hc511e266", extra=[0],  # starts as 0
         ret="uint32_t*", params="uint32_t", stack=4, result="Eax", hashes=2),
    dict(template="ScrField_SlotIsDefined", ref="ScrField_he0b632e1_IsDefined", extra=[0],
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrField_Slot", ref="ScrField_turn_type", extra=[0x00922eb4],
         ret="uint32_t*", params="uint32_t", stack=4, result="Eax", hashes=2),
    dict(template="ScrField_SlotIsDefined", ref="ScrField_turn_type_IsDefined", extra=[0x00922eb4],
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
    # batch 3
    dict(template="ScrField_HandleSlot", ref="ScrField_h4c9e9556",
         ret="uint32_t*", params="uint32_t", stack=4, result="Eax", hashes=2),
    dict(template="ScrField_HandleSlotIsDefined", ref="ScrField_h4c9e9556_IsDefined",
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrField_VectorSlot", ref="ScrField_lead_pos",
         ret="float*", params="uint32_t", stack=4, result="Eax", hashes=2),
    dict(template="ScrField_VectorSlotIsDefined", ref="ScrField_lead_pos_IsDefined",
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
] + [
    # array/struct fields: the getter and the copier are template arguments
    dict(template="ScrField_Get", ref=ref, extra=[getter],
         ret="void*", params="uint32_t", stack=4, result="Eax", hashes=1)
    for ref, getter in [("ScrField_followers", 0x0045cd50), ("ScrField_h4f7c0a56", 0x00602a40),
                        ("ScrField_h677e856b", 0x0017c280), ("ScrField_hc4813666", 0x0017c080),
                        ("ScrField_path_pos", 0x0017c400), ("ScrField_he337f297", 0x0017c6c0),
                        ("ScrField_gunners", 0x0017c660)]
] + [
    dict(template="ScrField_ArrayIsDefined", ref=ref, extra=[copier],
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1)
    for ref, copier in [("ScrField_followers_IsDefined", 0x0045cd00), ("ScrField_h4f7c0a56_IsDefined", 0x0017c2e0),
                        ("ScrField_h677e856b_IsDefined", 0x001e60d0), ("ScrField_path_pos_IsDefined", 0x0017c3b0),
                        ("ScrField_he337f297_IsDefined", 0x0017c720), ("ScrField_gunners_IsDefined", 0x0017c610)]
] + [
    # push 2: fixed immediates beside the hash ("imms": H = the hash)
    dict(template="ScrField_SlotIsNot", ref="ScrField_thread_killer_IsDefined", extra=[0, 0xfefefefe],
         imms=["H", "0xfefefefe", "0xfefefefe"], ret="bool*", params="bool*, uint32_t", stack=8, result="Eax"),
    dict(template="ScrField_SlotIsNot", ref="ScrField_path_IsDefined", extra=[0xffffffff, 0xffffffff],
         imms=["H", "0xffffffff", "0xffffffff", "0xffffffff"], ret="bool*", params="bool*, uint32_t", stack=8,
         result="Eax"),
    dict(template="ScrField_SlotIsSet", ref="ScrField_h05374cc6_IsDefined",
         imms=["H", "0xffffffff"], ret="bool*", params="bool*, uint32_t", stack=8, result="Eax"),
    dict(template="ScrField_SlotLiteral", ref="ScrField_path", extra=[0xffffffff],
         imms=["H", "0xffffffff", "H"], ret="uint32_t*", params="uint32_t", stack=4, result="Eax"),
    dict(template="ScrField_BoolSlotSet", ref="ScrField_can_shoot_low_2",
         ret="uint32_t*", params="uint32_t, uint32_t", stack=8, result="Eax", hashes=2),
    dict(template="ScrField_CopyOf", ref="ScrField_ambient_GetCopy", extra=[0xc, 0x0052bd10], conv="XB_THISCALL",
         imms=["H", "0xfffffff4"], ret="void*", params="uint8_t*, void*", stack=4, result="Eax"),
    dict(template="ScrField_CopyOf", ref="ScrField_he337f297_GetArrayCopy", extra=[0x10, 0x0017c4e0],
         conv="XB_THISCALL", imms=["H", "0xfffffff0"], ret="void*", params="uint8_t*, void*", stack=4,
         result="Eax"),
    dict(template="ScrField_Clear52", ref="ScrField_h1791f035_Clear", extra=[0x001e61f0],
         ret="void", params="uint32_t", stack=4, result="None", hashes=1),
    dict(template="ScrField_ArrayIsDefinedReleased", ref="ScrField_h053fe992_IsDefined",
         extra=[0x001e6020, 0x0045db80], ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1),
] + [
    dict(template="ScrField_Get", ref=ref, extra=[getter],
         ret="void*", params="uint32_t", stack=4, result="Eax", hashes=1)
    # ScrField_h12993a44 (getter 0x001e4a20) is plain C: its getter copies a never-set word of its frame
    for ref, getter in [("ScrField_h053fe992", 0x001e6070),
                        ("ScrField_fallback", 0x001e6250), ("ScrField_h1791f035", 0x001e61f0)]
] + [
    dict(template="ScrField_ArrayIsDefined", ref=ref, extra=[copier],
         ret="bool*", params="bool*, uint32_t", stack=8, result="Eax", hashes=1)
    for ref, copier in [("ScrField_fallback_IsDefined", 0x001e62b0), ("ScrField_h1791f035_IsDefined", 0x001e61a0)]
] + [
    # push 2: path / vehicle node fields
    dict(template="ScrNode_GetKey", ref="ScrVehNodeField_detourstart", extra=[0x00922980],
         ret="uint32_t*", params="uint32_t*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrNode_GetKey", ref="ScrPathNodeField_script_fallback", extra=[0x00922960],
         ret="uint32_t*", params="uint32_t*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrNode_KeyIsDefined", ref="ScrVehNodeField_detourstart_IsDefined", extra=[0x00922980],
         imms=["0xffffffff", "H"], ret="bool*", params="bool*, uint32_t", stack=8, result="Eax"),
    dict(template="ScrNode_HandleIsDefined", ref="ScrPathNodeField_script_noteworthy_IsDefined",
         extra=[0x00922224, 1], imms=["0xffffffff"], ret="bool*", params="bool*, uint32_t", stack=8, result="Eax"),
    dict(template="ScrNode_HandleIsDefined", ref="ScrVehNodeField_script_noteworthy_IsDefined",
         extra=[0x00922228, 0], imms=["0xffffffff"], ret="bool*", params="bool*, uint32_t", stack=8, result="Eax"),
    dict(template="ScrNode_StringKeyCopy", ref="ScrPathNodeField_script_waittill_Copy", extra=[0x00922958],
         conv="XB_THISCALL", imms=["0xffffffff", "0x0036f9c1", "H"], ret="ScriptString*",
         params="const uint32_t*, ScriptString*", stack=4, result="Eax"),
    dict(template="ScrNode_StringKeyCopy", ref="ScrVehNodeField_script_uniquename_Copy", extra=[0x00922978],
         conv="XB_THISCALL", imms=["0xffffffff", "0x0036f9c1", "H"], ret="ScriptString*",
         params="const uint32_t*, ScriptString*", stack=4, result="Eax"),
    dict(template="ScrNode_StringCopy", ref="ScrPathNodeField_animscript_Copy", extra=[0x00922aa8],
         conv="XB_THISCALL", imms=["0xffffffff", "0x0036f9c1"], ret="ScriptString*",
         params="const uint32_t*, ScriptString*", stack=4, result="Eax"),
    dict(template="ScrNode_StringCopy", ref="ScrPathNodeField_type_Copy", extra=[0x00922ad8],
         conv="XB_THISCALL", imms=["0xffffffff", "0x0036f9c1"], ret="ScriptString*",
         params="const uint32_t*, ScriptString*", stack=4, result="Eax"),
    dict(template="ScrNode_GetNodeKey", ref="ScrVehNodeField_endswitch", extra=[0x00922990],
         ret="uint32_t*", params="uint32_t*, uint32_t", stack=8, result="Eax", hashes=1),
    dict(template="ScrNode_NodeKeyIsDefined", ref="ScrVehNodeField_endswitch_IsDefined", extra=[0x00922990],
         imms=["0xffffffff", "H", "0xffffffff"], ret="bool*", params="bool*, uint32_t", stack=8, result="Eax"),
    dict(template="ScrNode_GetKeyOf", ref="ScrVehNodeField_detoured_2", extra=[0x00922980], conv="XB_THISCALL",
         ret="uint32_t*", params="const uint32_t*, uint32_t*", stack=4, result="Eax", hashes=1),
    dict(template="ScrNode_SetKeyOf", ref="ScrVehNodeField_hfefdb41b_Set", extra=[0x0092297c], conv="XB_THISCALL",
         ret="const uint32_t*", params="const uint32_t*, const uint32_t*", stack=4, result="Eax", hashes=1),
] + [
    dict(template="ScrNode_String", ref=ref, extra=[getter], imms=[], entry="esp",
         ret="ScriptString*", params="uint32_t, uint32_t", stack=8, result="Eax")
    for ref, getter in [("ScrPathNodeField_animscript", 0x00181170), ("ScrPathNodeField_script_chain", 0x001812d0),
                        ("ScrPathNodeField_script_door", 0x00181350),
                        ("ScrPathNodeField_script_noteworthy", 0x0011b9a0),
                        ("ScrPathNodeField_script_waittill", 0x00181250), ("ScrPathNodeField_target", 0x00119a80),
                        ("ScrPathNodeField_targetname", 0x0011cd30), ("ScrPathNodeField_type", 0x001811e0),
                        ("ScrVehNodeField_script_noteworthy", 0x00119c90),
                        ("ScrVehNodeField_script_uniquename", 0x001813d0), ("ScrVehNodeField_target", 0x00119be0),
                        ("ScrVehNodeField_targetname", 0x00119b70)]
] + [
    dict(template="ScrNode_Vector", ref=ref, extra=[slot], imms=[],
         ret="float*", params="float*, uint32_t", stack=8, result="Eax")
    for ref, slot in [("ScrPathNodeField_angles", 0x00922ac0), ("ScrPathNodeField_origin", 0x00922ab8),
                      ("ScrVehNodeField_angles", 0x00922af8), ("ScrVehNodeField_origin", 0x00922af0)]
] + [
    dict(template="ScrNode_FloatKey", ref=ref, extra=[slot], hashes=1,
         ret="float*", params="float*, uint32_t", stack=8, result="Eax")
    for ref, slot in [("ScrPathNodeField_script_delay", 0x00922968), ("ScrVehNodeField_script_delay", 0x00922988)]
] + [
    dict(template="ScrNode_Float", ref=ref, extra=[slot], imms=[],
         ret="float*", params="float*, uint32_t", stack=8, result="Eax")
    for ref, slot in [("ScrVehNodeField_speed", 0x00922b00), ("ScrVehNodeField_lookahead", 0x00922b08)]
] + [
    dict(template="ScrNode_StringNonEmpty", ref=ref, extra=[copier], imms=[], entry="esp",
         ret="bool", params="uint32_t, uint32_t", stack=0, result="Al")
    for ref, copier in [("ScrPathNodeField_script_waittill_NonEmpty", 0x00181250),
                        ("ScrPathNodeField_script_door_IsSet", 0x00181350),
                        ("ScrVehNodeField_script_uniquename_IsNonEmpty", 0x001813d0)]
] + [
    dict(template=template, ref=ref, extra=[offset], imms=[], ret=ret, params=params, stack=stack, result=result)
    for ref, template, offset, ret, params, stack, result in [
        (f"ScrHudElem_{kind}_{offset:02x}", f"ScrHudElem_{kind}Word", offset, "uint32_t",
         "uint32_t, uint32_t" if kind == "Set" else "uint32_t", 8 if kind == "Set" else 4, "Eax")
        for offset in (0x04, 0x08, 0x14, 0x18) for kind in ("Set", "Get")
    ] + [
        ("ScrHudElem_SetFloat_6c", "ScrHudElem_SetWord", 0x6c, "uint32_t", "uint32_t, uint32_t", 8, "Eax"),
        ("ScrHudElem_SetFloat_0c", "ScrHudElem_SetWord", 0x0c, "uint32_t", "uint32_t, uint32_t", 8, "Eax"),
        ("ScrHudElem_GetFloat_6c", "ScrHudElem_GetFloat", 0x6c, "float", "uint32_t", 4, "St0"),
        ("ScrHudElem_GetFloat_0c", "ScrHudElem_GetFloat", 0x0c, "float", "uint32_t", 4, "St0"),
    ] + [
        (f"ScrHudElem_{kind}{name}", f"ScrHudElem_{kind}Byte", offset,
         "uint32_t" if kind == "Set" else "uint8_t", "uint32_t, uint32_t" if kind == "Set" else "uint32_t",
         8 if kind == "Set" else 4, "Eax" if kind == "Set" else "Al")
        for name, offset in (("Alpha", 0x1f), ("ColorByte0", 0x1c), ("ColorByte1", 0x1d), ("ColorByte2", 0x1e))
        for kind in ("Set", "Get")
    ]
]


def instance(shape, value):
    head = [] if value is None else [value]  # None: a shape without a hash (built-in fields)
    args = ", ".join(f"{v:#010x}u" for v in head + shape.get("extra", []))
    return f"{shape['template']}<{args}>"


HEADER = """#pragma once

// Generated by tools/gen_scrfield.py - rerun it instead of editing this file.
// Included by scr_field.h.

namespace game {
"""


# Sizes the list (the Ghidra function bodies) has short: the bodies run to the ret.
SIZES = {"ScrField_h17138232": 35, "ScrField_ha3d681e3_IsDefined": 87}


def load_list(path):
    rows = []
    for line in open(path):
        p = line.split()
        if len(p) >= 4:
            rows.append((int(p[0], 16), SIZES.get(p[3], int(p[2])), p[3]))
    return rows


def members(image, rows):
    """[(shape, [(va, name, hash)])] with every member checked against its reference."""
    by_name = {name: (va, size) for va, size, name in rows}
    refs = {}
    for i, shape in enumerate(SHAPES):
        va, size = by_name[shape["ref"]]
        key = shapes.shape(image, va, size)[0]
        if key in refs:
            sys.exit(f"{shape['ref']}: same shape as {SHAPES[refs[key]]['ref']}")
        refs[key] = i
    found = [(shape, []) for shape in SHAPES]
    for va, size, name in rows:
        key, imms = shapes.shape(image, va, size)
        i = refs.get(key)
        if i is None:
            continue
        pattern = SHAPES[i].get("imms", ["H"] * SHAPES[i].get("hashes", 0))
        fixed_ok = len(imms) == len(pattern) and all(
            p == "H" or int(p, 16) == int(h, 16) for p, h in zip(pattern, imms))
        values = {int(h, 16) for p, h in zip(pattern, imms) if p == "H"}
        hashless = "H" not in pattern
        if not fixed_ok or len(values) != (0 if hashless else 1):
            sys.exit(f"{name} {va:08x}: shape {SHAPES[i]['ref']} but immediates {imms}")
        value = None if hashless else values.pop()
        m = re.fullmatch(r"ScrField_h([0-9a-f]{8})(_\w+)?", name)
        if m and int(m.group(1), 16) != value:
            sys.exit(f"{name} {va:08x}: name and hash {value:#010x} differ")
        found[i][1].append((va, name, value))
    return found


def code_name(shape, name):
    return f"&{name}_entry" if shape.get("entry") else name


def write_sources(found, dry_run):
    decls, defs, names, entries, seen = [], [], [], [], set()
    for shape, items in found:
        for value in sorted({v for _, _, v in items}):
            spec = f"{shape['ret']} {shape.get('conv', 'XB_CDECL')} {instance(shape, value)}({shape['params']});"
            if spec in seen:
                continue
            seen.add(spec)
            decls.append(f"extern template {spec}")
            defs.append(f"template {spec}")
    flat = sorted((va, n, v, shape) for shape, items in found for va, n, v in items)
    for va, name, value, shape in flat:
        names.append(f"inline constexpr auto* {name} = &{instance(shape, value)};  // {va:#010x}")
        if shape.get("entry"):
            decls.append(f'extern "C" void {name}_entry(void);')
            macro = "SCR_FIELD_ECX_ENTRY" if shape["entry"] == "ecx" else "SCR_FIELD_ESP_ENTRY"
            entries.append(f"{macro}({name}, {instance(shape, value)})")
    header = HEADER + "\n" + "\n".join(decls) + "\n\n" + "\n".join(names) + "\n\n}  // namespace game\n"
    inc = ("// Generated by tools/gen_scrfield.py - rerun it instead of editing this file.\n\n"
           "namespace game {\n\n" + "\n".join(defs) + "\n\n}  // namespace game\n")
    if entries:
        inc += "\n" + "\n".join(entries) + "\n"
    if not dry_run:
        (CODE / "scr_field_instances.h").write_text(header)
        (CODE / "scr_field_instances.inc").write_text(inc)


def register(found, dry_run):
    text = FUNCTIONS_CPP.read_text()
    known = set(re.findall(r'\{"(\w+)", 0x', text))
    lines = text.split("\n")
    added = 0
    for shape, items in found:
        for va, name, _ in items:
            if name in known:
                # keep the entry point in step with the shape (an asm entry added later)
                want = f"code({code_name(shape, name)})"
                for i, l in enumerate(lines):
                    if l.startswith(f'    {{"{name}", ') and want not in l:
                        lines[i] = re.sub(r"code\([^)]*\)", want, l)
                        added += 1
                continue
            line = (f'    {{"{name}", {va:#010x}, 0, code({code_name(shape, name)}), {shape["stack"]}, '
                    f'Result::{shape["result"]}, Status::Compare}},')
            rows = [(i, int(m.group(1), 16)) for i, l in enumerate(lines)
                    for m in [re.match(r'\s+\{"\w+", 0x([0-9a-fA-F]+),', l)] if m]
            at = next((i for i, a in rows if a > va), rows[-1][0] + 1)
            lines.insert(at, line)
            added += 1
    if not dry_run:
        FUNCTIONS_CPP.write_text("\n".join(lines))
    return added


def add_csv_rows(found, dry_run):
    lines = FUNCTIONS_CSV.read_text().split("\n")
    head, body = lines[0], [l for l in lines[1:] if l]
    have = {l.split(",", 1)[0] for l in body}
    new = [f'{va:08x},{name},compare,0,0,0,0,0,0,""'
           for _, items in found for va, name, _ in items if f"{va:08x}" not in have]
    if not dry_run and new:
        body = sorted(body + new, key=lambda l: l.split(",", 1)[0])
        FUNCTIONS_CSV.write_text("\n".join([head] + body) + "\n")
    return len(new)


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("list")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    image = fnpack.Image()
    found = members(image, load_list(args.list))
    for shape, items in found:
        print(f"{shape['ref']} -> {instance(shape, 0).replace('0x00000000u', 'H', 1)}: {len(items)} members, "
              f"{len({v for _, _, v in items})} hashes")
    write_sources(found, args.dry_run)
    print(f"kFunctions: {register(found, args.dry_run)} added; "
          f"functions.csv: {add_csv_rows(found, args.dry_run)} rows added")


if __name__ == "__main__":
    main()
