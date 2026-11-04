#!/usr/bin/env python3
"""--force specs that test the generated ScrField_* accessors on real entities (most fields belong to levels
the compare runs never reach): one spec per member taking (entity) or (out, entity), called on entity
index I = (address >> 4) % 24 while that entity has a script object (+0x428), out in the dead stack.

usage: tools/scrfield_force.py [--untested] [--count N] > specs.txt
  then: compare_batch.py NAMES... --only --every 1 --force SPEC ... (one --force per line)
Handle of entity I: I | generation [0x0097a96c + I*8] << 12 (GScr_GetEntScriptObject's check).
2026-11-04: 718 specs, all four missions: 717 matched, ScrField_h12993a44 found a stale-stack word.
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


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--untested", action="store_true", help="only members with no test in functions.csv")
    parser.add_argument("--count", type=int, default=2, help="calls per spec")
    args = parser.parse_args()
    text = (ROOT / "game/code/scr_field_instances.h").read_text()
    rows = {r["name"]: r for r in csv.DictReader(open(ROOT / "tools/data/functions.csv"))}
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
