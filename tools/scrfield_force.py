#!/usr/bin/env python3
"""--force specs that test the generated ScrField_* accessors on real entities (most fields belong to levels
the compare runs never reach): one spec per member taking (entity) or (out, entity), called on entity
index I = (address >> 4) % 24 while that entity has a script object (+0x428), out in the dead stack.

usage: tools/scrfield_force.py [--untested] [--count N] > specs.txt
  then: compare_batch.py NAMES... --only --every 1 --force SPEC ... (one --force per line)
Handle of entity I: I | generation [0x0097a96c + I*8] << 12 (GScr_GetEntScriptObject's check).
2026-11-04: 718 specs, all four missions: 717 matched, ScrField_h12993a44 found a stale-stack word.

Node fields (ScrPathNodeField_* / ScrVehNodeField_*): path node handle 0x200 + n (block 0 of g_pathNodeBlocks
0x0095f4f4, node n; guard: the block's node array +0x10), vehicle node handle n (g_vehNodes 0x0095c228 pointer n;
guard: that pointer), n = (address >> 4) % 2. One-off accessors (ONE_OFF below): field-table `this` pointers are
the script object (entity +0x428) plus the member's offset (Scr_GetEntObjectOrEmpty callers), the field callbacks
get the actor (entity +0x240) or the entity, the script-memory frees a block start of the heap's first pool.
"""
import argparse
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENTITY = {"ScrField_Slot", "ScrField_String", "ScrField_BoolSlot", "ScrField_HandleSlot", "ScrField_VectorSlot",
          "ScrField_Get", "ScrField_SlotLiteral", "ScrField_Clear52"}
OUT = {"ScrField_SlotIsDefined", "ScrField_StringIsDefined", "ScrField_BoolSlotIsDefined",
       "ScrField_FloatSlotIsDefined", "ScrField_HandleSlotIsDefined", "ScrField_VectorSlotIsDefined",
       "ScrField_ArrayIsDefined", "ScrField_SlotIsNot", "ScrField_SlotIsSet", "ScrField_ArrayIsDefinedReleased"}


NODE_OUT = {"ScrNode_GetKey", "ScrNode_KeyIsDefined", "ScrNode_GetNodeKey", "ScrNode_NodeKeyIsDefined",
            "ScrNode_HandleIsDefined", "ScrNode_String", "ScrNode_Vector", "ScrNode_FloatKey", "ScrNode_Float"}
NODE_SELF_OUT = {"ScrNode_GetKeyOf", "ScrNode_StringKeyCopy", "ScrNode_StringCopy"}


def node(name, va):
    n = (int(va, 16) >> 4) % 2
    if name.startswith("ScrPathNodeField_"):
        return f"{0x200 + n:#x}", "?[[[0x95f4f4]+8]+0x10]"
    return f"{n}", f"?[0x95c228+{n}*4]"


def entity(i):
    return f"{i}+[0x97a96c+{i}*8]*0x1000", f"[0x97a968+{i}*8]"


def one_off():
    """(name, call, guard) for the plain accessors; several entity indices where an actor is needed."""
    specs = []
    for i in (1, 2, 3):
        handle, ent = entity(i)
        table = f"[{ent}+0x428]"
        specs += [(f"ScrField_hits_Set", f"ecx={table}+0x14,&1,5", table),
                  (f"ScrField_hits", f"ecx={table}+0x14", table),
                  (f"ScrField_onfire", f"ecx={table}+0x14", table),
                  (f"ScrField_ambient_GetCopy", f"ecx={table}+0xc,&0-0x400", table),
                  (f"ScrField_he337f297_GetArrayCopy", f"ecx={table}+0x10,&0-0x400", table),
                  (f"ScrField_targets_groupname_2", f"{handle},0", table),
                  (f"ScrField_mgturret_2", handle, table),
                  (f"ScrField_follow_ent", f"{handle},0x42c80000,0+[0x97a96c]*0x1000", table),
                  (f"ScrField_SetByte", f"{ent},0x29c,&3,1", ent),
                  (f"ScrIface_Free", f"[{table}+8]", f"[{table}+8]")]
    # entities 1-23 have no actor (+0x240) in missions 1/4/5/6: spread over the AI's indices
    for i in range(24, 400, 7):
        actor = f"[{entity(i)[1]}+0x240]"
        specs += [(f"ScrField_GetWeaponName", f"{actor},0x131c,&0-0x400", actor),
                  (f"ScrField_GetMsAsSeconds", f"{actor},{0x1308 if i % 2 else 0x7ec:#x},&0-0x400", actor),
                  # name {rep}: rep 0, or "mp40" (text at rep + 6, words 4-6)
                  (f"ScrField_SetWeaponIndex", f"{actor},0x131c,&3,{'0' if i % 3 == 0 else '&4,0,0x706d0000,0x3034'}",
                   actor)]
    specs += [("ScrIface_Malloc", "0x20,4", "[0x95f4a4]"), ("ScrIface_Malloc", "0x3000,0x10", "[0x95f4a4]"),
              ("ScrIface_Free", "[[[0x95f4a4]+4]+0x18]", "[[[0x95f4a4]+4]+0x18]"),
              ("ScrIface_SmallFree", "[[[0x947700]+4]+0x18]", "[[[0x947700]+4]+0x18]"),
              ("ScrIface_SmallFree", "0", "[0x947700]")]
    return specs


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--untested", action="store_true", help="only members with no test in functions.csv")
    parser.add_argument("--count", type=int, default=2, help="calls per spec")
    parser.add_argument("--group", choices=["entity", "node", "oneoff"], default="entity")
    args = parser.parse_args()
    text = (ROOT / "game/code/scr_field_instances.h").read_text()
    rows = {r["name"]: r for r in csv.DictReader(open(ROOT / "tools/data/functions.csv"))}
    def untested(name):
        return name not in rows or int(rows[name]["tested"]) == 0

    if args.group == "oneoff":
        for name, call, guard in one_off():
            if not args.untested or untested(name):
                print(f"{name}({call})@5*{args.count}?{guard}")
        return
    if args.group == "node":
        for name, template, va in re.findall(r"auto\* (\w+) = &(\w+)<[^>]*>;  // 0x([0-9a-f]+)", text) + [
                ("ScrVehNodeField_script_uniquename_IsDefined", "ScrNode_HandleIsDefined", "00198d80")]:
            if not name.startswith(("ScrPathNodeField_", "ScrVehNodeField_")) or (args.untested and not untested(name)):
                continue
            handle, guard = node(name, va)
            if template in NODE_OUT:
                call = f"&0-0x400,{handle}"
            elif template in NODE_SELF_OUT:
                call = f"ecx=&1,&0-0x400,{handle}"
            elif template == "ScrNode_SetKeyOf":
                call = f"ecx=&2,&1,1,{handle}"
            elif template == "ScrNode_StringNonEmpty":
                call = f"ecx=&0,{handle}"
            else:
                continue
            print(f"{name}({call})@5*{args.count}{guard}")
        return
    for name, template, va in re.findall(r"auto\* (\w+) = &(\w+)<[^>]*>;  // 0x([0-9a-f]+)", text):
        if args.untested and name in rows and int(rows[name]["tested"]) > 0:
            continue
        i = (int(va, 16) >> 4) % 24
        handle = f"{i}+[0x97a96c+{i}*8]*0x1000"
        if template in ENTITY:
            call = handle
        elif template in OUT:
            call = f"&0-0x400,{handle}"
        elif template == "ScrField_BoolSlotSet":
            call = f"{handle},1"
        else:
            continue
        print(f"{name}({call})@5*{args.count}?[[0x97a968+{i}*8]+0x428]")


if __name__ == "__main__":
    main()
