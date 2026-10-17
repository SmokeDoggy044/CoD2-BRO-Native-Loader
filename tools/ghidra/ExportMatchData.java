// Writes per-function data for matching engine functions against the Quake 3 / RTCW
// source (tools/q3match): one tab-separated line per function with address, section,
// size, current name, the C strings it references (in instruction order) and its
// direct call targets (in instruction order), each with the strings referenced since
// the previous call (usually its arguments; function pointers as &address). Then
// "@pair" lines: a data dword pointing to a string followed within 3 dwords by a
// pointer to a function ({"name", handler} tables). Read-only.
// Optional script argument: output path.
//@category CodBro

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;

public class ExportMatchData extends GhidraScript {
    private Memory memory;

    // XBE sections are all marked executable in Ghidra; these hold data.
    private static boolean isData(MemoryBlock block) {
        if (block == null) return false;
        String n = block.getName();
        return n.equals(".rdata") || n.equals(".data") || n.equals(".data1") || n.equals("XON_RD");
    }

    // A printable C string of at least 2 characters at addr, or null.
    private String readString(Address addr) {
        if (!isData(memory.getBlock(addr))) {
            return null;
        }
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 512; i++) {
                int b = memory.getByte(addr.add(i)) & 0xff;
                if (b == 0) break;
                if (b == '\n') { sb.append("\\n"); continue; }
                if (b == '\t') { sb.append("\\t"); continue; }
                if (b == '\r') { sb.append("\\r"); continue; }
                if (b < 0x20 || b > 0x7e) return null;
                if (b == '\\') { sb.append("\\\\"); continue; }
                sb.append((char) b);
            }
        } catch (Exception e) {
            return null;
        }
        return sb.length() >= 2 ? sb.toString() : null;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = args.length > 0 ? new File(args[0]) : askFile("Match data", "Save");
        memory = currentProgram.getMemory();
        int count = 0;
        int pairs = 0;
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            w.println("# address\tsection\tsize\tname\tstrings (\\x1f-separated)\tcallees (\\x1d-separated, each address[=strings \\x1e-separated])");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                Function f = functions.next();
                if (f.isExternal() || f.isThunk()) continue;
                MemoryBlock block = memory.getBlock(f.getEntryPoint());
                List<String> strings = new ArrayList<>();
                List<String> callees = new ArrayList<>();
                List<String> pending = new ArrayList<>();
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction insn = it.next();
                    for (Reference ref : insn.getReferencesFrom()) {
                        Address to = ref.getToAddress();
                        RefType type = ref.getReferenceType();
                        if (type.isCall()) {
                            Function callee = getFunctionAt(to);
                            if (callee != null && callee.isThunk()) callee = callee.getThunkedFunction(true);
                            String entry = String.format("%08x", (callee != null ? callee.getEntryPoint() : to).getOffset());
                            if (!pending.isEmpty()) entry += "=" + String.join("\u001e", pending);
                            callees.add(entry);
                            pending.clear();
                        } else if (type.isData() && to.isMemoryAddress() && (getFunctionAt(to) != null
                                || memory.getBlock(to) != null && memory.getBlock(to).getName().equals(".text"))) {
                            // a code pointer argument (Cmd_AddCommand("x", handler)); handlers
                            // only referenced this way often have no Ghidra function yet
                            pending.add(String.format("&%08x", to.getOffset()));
                        } else if (type.isData() && to.isMemoryAddress()) {
                            String s = readString(to);
                            if (s != null) {
                                strings.add(s);
                                pending.add(s);
                            }
                        }
                    }
                }
                w.printf("%08x\t%s\t%d\t%s\t%s\t%s%n", f.getEntryPoint().getOffset(),
                        block != null ? block.getName() : "?", f.getBody().getNumAddresses(),
                        f.getName(true).replace(' ', '_'), String.join("\u001f", strings),
                        String.join("\u001d", callees));
                count++;
            }
            for (MemoryBlock b : memory.getBlocks()) {
                if (!isData(b)) continue;
                Address end = b.getEnd();
                for (Address a = b.getStart(); a.add(4).compareTo(end) < 0; a = a.add(4)) {
                    Address sp = toAddr(memory.getInt(a) & 0xffffffffL);
                    if (memory.getBlock(sp) == null) continue;
                    String s = readString(sp);
                    if (s == null) continue;
                    for (int k = 1; k <= 3 && a.add(4 * k + 3).compareTo(end) <= 0; k++) {
                        Address fp = toAddr(memory.getInt(a.add(4 * k)) & 0xffffffffL);
                        Function fn = memory.getBlock(fp) != null ? getFunctionAt(fp) : null;
                        if (fn != null) {
                            w.printf("@pair\t%08x\t%s\t%08x\t%d%n", a.getOffset(), s, fp.getOffset(), k);
                            pairs++;
                            break;
                        }
                    }
                }
            }
        }
        println("Wrote " + count + " functions and " + pairs + " string/function pairs to " + out);
    }
}
