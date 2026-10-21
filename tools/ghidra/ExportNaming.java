// Exports what tools/namepack.py needs, one pass over the functions in a range:
//   OUTDIR/decomp/<address>.c     the decompiled C (names as of the export;
//                                 namepack substitutes current names)
//   OUTDIR/meta/<START>.tsv       address, size, callees, strings, data references
//   OUTDIR/fields/<START>.tsv     address, struct, offset, access (r/w/a), count:
//                                 every field access through a /bro struct type
//                                 (pointer or typed global; tools/data/structs.txt)
// Run it again for a range after renames or struct changes. Run over everything in
// chunks (each call has a timeout): 00011000-00100000, ... up to the end of .text.
// Args: START END OUTDIR
//@category CodBro

import java.io.File;
import java.io.PrintWriter;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;

public class ExportNaming extends GhidraScript {
    static final String CATEGORY = "/bro";

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Address start = toAddr(a[0]), end = toAddr(a[1]);
        File out = new File(a[2]);
        File decompDir = new File(out, "decomp"), metaDir = new File(out, "meta"), fieldDir = new File(out, "fields");
        decompDir.mkdirs();
        metaDir.mkdirs();
        fieldDir.mkdirs();
        ReferenceManager refs = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decomp.setOptions(options);
        decomp.openProgram(currentProgram);
        int count = 0, failed = 0;
        String tag = a[0].toLowerCase();
        try (PrintWriter meta = new PrintWriter(new File(metaDir, tag + ".tsv"));
             PrintWriter fields = new PrintWriter(new File(fieldDir, tag + ".tsv"))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(new AddressSet(start, end), true)) {
                if (monitor.isCancelled()) break;
                if (f.isThunk() || f.isExternal()) continue;
                String addr = String.format("%08x", f.getEntryPoint().getOffset());
                Set<String> callees = new LinkedHashSet<>(), strings = new LinkedHashSet<>(), data = new LinkedHashSet<>();
                for (Function c : f.getCalledFunctions(monitor))
                    callees.add(String.format("%08x", c.getEntryPoint().getOffset()));
                for (Address ad : f.getBody().getAddresses(true)) {
                    for (Reference r : refs.getReferencesFrom(ad)) {
                        if (r.getReferenceType().isFlow() || !r.isMemoryReference()
                                || !currentProgram.getMemory().contains(r.getToAddress())) continue;
                        Data d = listing.getDataContaining(r.getToAddress());
                        if (d != null && d.hasStringValue()) {
                            strings.add(String.valueOf(d.getValue()).replaceAll("[\\t\\n\\r\\x1f]", " "));
                        } else if (getFunctionAt(r.getToAddress()) == null) {
                            data.add(String.format("%08x", r.getToAddress().getOffset()));
                        }
                    }
                }
                meta.printf("%s\t%d\t%s\t%s\t%s%n", addr, f.getBody().getNumAddresses(), String.join(",", callees),
                        String.join("\u001f", strings), String.join(",", data));

                DecompileResults res = decomp.decompileFunction(f, 60, monitor);
                if (res.getDecompiledFunction() == null) {
                    failed++;
                    continue;
                }
                try (PrintWriter c = new PrintWriter(new File(decompDir, addr + ".c"))) {
                    c.print(res.getDecompiledFunction().getC());
                }
                HighFunction hf = res.getHighFunction();
                if (hf != null) {
                    Map<String, Integer> seen = new TreeMap<>();
                    Iterator<PcodeOpAST> ops = hf.getPcodeOps();
                    while (ops.hasNext()) {
                        PcodeOpAST op = ops.next();
                        if (op.getOpcode() != PcodeOp.PTRSUB) continue;
                        String key = fieldKey(listing, op);
                        if (key != null) seen.merge(key + "\t" + access(op.getOutput()), 1, Integer::sum);
                    }
                    for (Map.Entry<String, Integer> e : seen.entrySet())
                        fields.printf("%s\t%s\t%d%n", addr, e.getKey(), e.getValue());
                }
                count++;
                if (count % 500 == 0) println("ExportNaming: " + count + " (" + addr + ")");
            }
        }
        decomp.dispose();
        println("ExportNaming: " + count + " functions, " + failed + " without a decompile, to " + out);
    }

    // "struct<TAB>0xoffset" for a PTRSUB into a /bro struct, else null.
    static String fieldKey(Listing listing, PcodeOp op) {
        Varnode base = op.getInput(0), off = op.getInput(1);
        if (!off.isConstant()) return null;
        if (base.isConstant() && base.getOffset() == 0) { // typed global: PTRSUB(0, address)
            Address at = off.getAddress().getNewAddress(off.getOffset());
            Data d = listing.getDataContaining(at);
            if (d == null) return null;
            DataType dt = strip(d.getDataType());
            if (!(dt instanceof Structure) || !isOurs(dt)) return null;
            return dt.getName() + "\t0x" + Long.toHexString(at.subtract(d.getAddress()));
        }
        if (base.getHigh() == null) return null;
        DataType dt = strip(base.getHigh().getDataType());
        if (!(dt instanceof Pointer)) return null;
        DataType target = strip(((Pointer) dt).getDataType());
        if (!(target instanceof Structure) || !isOurs(target)) return null;
        return target.getName() + "\t0x" + Long.toHexString(off.getOffset());
    }

    // Follows casts and copies (an undefined field is read through a cast pointer).
    static String uses(Varnode out, int depth) {
        if (out == null || depth > 4) return "a";
        String kind = "";
        Iterator<PcodeOp> it = out.getDescendants();
        while (it.hasNext()) {
            PcodeOp d = it.next();
            int code = d.getOpcode();
            if (code == PcodeOp.LOAD && d.getInput(1) == out) kind += "r";
            else if (code == PcodeOp.STORE && d.getInput(1) == out) kind += "w";
            else if (code == PcodeOp.CAST || code == PcodeOp.COPY) kind += uses(d.getOutput(), depth + 1);
            else kind += "a";
        }
        return kind;
    }

    static DataType strip(DataType dt) {
        while (dt instanceof TypeDef) dt = ((TypeDef) dt).getBaseDataType();
        return dt;
    }

    static boolean isOurs(DataType dt) {
        return dt.getCategoryPath().getPath().startsWith(CATEGORY);
    }

    // r: loaded from, w: stored to, a: address used otherwise (passed on, nested field)
    static String access(Varnode out) {
        String kind = uses(out, 0);
        if (kind.contains("w")) return "w";
        if (kind.contains("r")) return "r";
        return "a";
    }
}
