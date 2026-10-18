// Names cvar_t* globals after their cvars, Quake 3 style: for every call to the
// function given as argument (Cvar_Get) whose first argument is a string and whose
// result is stored to a global right after the call ("MOV [global], EAX"), writes
// "address,name,data,high,evidence" to the CSV given as second argument. Globals
// that receive different cvars, and cvars stored to several globals, are left out.
// Read-only; apply the CSV with ApplyNames.java.
// Arguments: function address, output CSV path.
//@category CodBro

import java.io.File;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

public class CvarGlobals extends GhidraScript {
    private Memory memory;

    private String readString(Address addr) {
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 128; i++) {
                int b = memory.getByte(addr.add(i)) & 0xff;
                if (b == 0) break;
                if (!(Character.isLetterOrDigit(b) || b == '_')) return null;
                sb.append((char) b);
            }
        } catch (Exception e) {
            return null;
        }
        return sb.length() > 0 && !Character.isDigit(sb.charAt(0)) ? sb.toString() : null;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address target = toAddr(Long.parseLong(args[0].replace("0x", ""), 16));
        File out = new File(args[1]);
        memory = currentProgram.getMemory();
        Map<Address, String> nameOf = new TreeMap<>();
        Map<Address, String> where = new HashMap<>();
        Map<Address, Boolean> conflict = new HashMap<>();
        Map<String, Integer> uses = new HashMap<>();
        for (Reference ref : getReferencesTo(target)) {
            if (!ref.getReferenceType().isCall()) continue;
            Instruction call = getInstructionAt(ref.getFromAddress());
            if (call == null) continue;
            // the first argument is the last push before the call
            String cvar = null;
            Instruction insn = call.getPrevious();
            for (int i = 0; i < 6 && insn != null && cvar == null; i++, insn = insn.getPrevious()) {
                if (!insn.getMnemonicString().equalsIgnoreCase("PUSH")) continue;
                Scalar s = insn.getScalar(0);
                if (s != null) cvar = readString(toAddr(s.getUnsignedValue()));
                break;
            }
            if (cvar == null) continue;
            // the store may come after the next call's pushes (MSVC interleaves),
            // so scan forward until EAX is overwritten or control flow leaves
            Address global = null;
            Instruction next = call.getNext();
            for (int i = 0; i < 10 && next != null && global == null; i++, next = next.getNext()) {
                String m = next.getMnemonicString().toUpperCase();
                if (m.equals("CALL") || m.startsWith("J") || m.equals("RET")) break;
                if (m.equals("MOV") && next.getDefaultOperandRepresentation(1).equalsIgnoreCase("EAX")) {
                    for (Reference r : next.getReferencesFrom()) {
                        if (r.getReferenceType().isWrite() && r.getToAddress().isMemoryAddress()) global = r.getToAddress();
                    }
                    if (global == null) break; // stored to a local or through a register
                } else if (next.getNumOperands() > 0
                        && next.getDefaultOperandRepresentation(0).equalsIgnoreCase("EAX")
                        && !m.equals("PUSH") && !m.equals("CMP") && !m.equals("TEST")) {
                    break;
                }
            }
            if (global == null) continue;
            uses.merge(cvar, 1, Integer::sum);
            String old = nameOf.get(global);
            if (old != null && !old.equals(cvar)) conflict.put(global, true);
            nameOf.put(global, cvar);
            Function f = getFunctionContaining(call.getAddress());
            where.put(global, f != null ? f.getName() : call.getAddress().toString());
        }
        Map<String, Integer> globalsPerCvar = new HashMap<>();
        for (Map.Entry<Address, String> e : nameOf.entrySet()) {
            if (!conflict.containsKey(e.getKey())) globalsPerCvar.merge(e.getValue(), 1, Integer::sum);
        }
        int written = 0;
        try (PrintWriter w = new PrintWriter(out)) {
            w.println("address,name,kind,confidence,evidence");
            for (Map.Entry<Address, String> e : nameOf.entrySet()) {
                if (conflict.containsKey(e.getKey()) || globalsPerCvar.get(e.getValue()) != 1) continue;
                w.printf("%08x,%s,data,high,\"cvar_t*: Cvar_Get(\"\"%s\"\") in %s\"%n", e.getKey().getOffset(),
                        e.getValue(), e.getValue(), where.get(e.getKey()));
                written++;
            }
        }
        println("Wrote " + written + " cvar globals to " + out + " (" + conflict.size() + " conflicting globals left out)");
    }
}
