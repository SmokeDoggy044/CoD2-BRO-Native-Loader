// Decompiles a list of functions to files for tools/lift.py.
// Script arguments: LIST OUTDIR   LIST = one hex address per line (more columns ignored).
// Per function: OUTDIR/<addr>.c (decompiler C) and OUTDIR/<addr>.sig (its own and every direct callee's
// prototype: address;name;convention;return type;param types joined by '|';varargs;custom storage 0/1;stack purge;
// the first line is the decompiler's own prototype of the function: convention = its model, purge = extrapop - 4),
// OUTDIR/<addr>.glob (name;address;data type of every global token) and OUTDIR/<addr>.str (address;text of every
// string literal token).
//@category CoD BRO
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.lang.PrototypeModel;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ExportDecompile extends GhidraScript {
    private DecompInterface d;
    private final Map<Function, String> inferred = new HashMap<>();

    // Callees without a set signature: the decompiler's own prototype of them (decompiled once).
    private String calleeSig(Function f) {
        if (f.getSignatureSource() != ghidra.program.model.symbol.SourceType.DEFAULT) return sig(f);
        return inferred.computeIfAbsent(f, g -> {
            DecompileResults r = d.decompileFunction(g, 60, monitor);
            return r.decompileCompleted() ? own(g, r.getHighFunction().getFunctionPrototype()) : sig(g);
        });
    }

    private static String own(Function f, FunctionPrototype fp) {
        StringBuilder own = new StringBuilder();
        for (int i = 0; i < fp.getNumParams(); i++) {
            HighSymbol q = fp.getParam(i);
            if (own.length() > 0) own.append('|');
            own.append(q.getDataType().getDisplayName()).append(' ').append(q.getName()).append('@')
               .append(q.getStorage().toString());
        }
        return String.format("%08x;%s;%s;%s;%s;%d;%d;%d", f.getEntryPoint().getOffset(), f.getName(),
            fp.getModelName(), fp.getReturnType().getDisplayName(), own, fp.isVarArg() ? 1 : 0, 1,
            fp.getExtraPop() == PrototypeModel.UNKNOWN_EXTRAPOP ? -1 : fp.getExtraPop() - 4);
    }

    private static String sig(Function f) {
        StringBuilder p = new StringBuilder();
        for (Parameter q : f.getParameters()) {
            if (p.length() > 0) p.append('|');
            p.append(q.getDataType().getDisplayName()).append(' ').append(q.getName())
             .append('@').append(q.getVariableStorage().toString());
        }
        return String.format("%08x;%s;%s;%s;%s;%d;%d;%d", f.getEntryPoint().getOffset(), f.getName(),
                             f.getCallingConventionName(), f.getReturnType().getDisplayName(), p,
                             f.hasVarArgs() ? 1 : 0, f.hasCustomVariableStorage() ? 1 : 0, f.getStackPurgeSize());
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        List<String> lines = Files.readAllLines(Paths.get(args[0]));
        File out = new File(args[1]);
        out.mkdirs();
        d = new DecompInterface();
        d.openProgram(currentProgram);
        int n = 0;
        for (String line : lines) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String hex = line.split("[\\s,]")[0];
            Function f = getFunctionAt(toAddr(Long.parseLong(hex, 16)));
            if (f == null) { println("no function at " + hex); continue; }
            DecompileResults r = d.decompileFunction(f, 120, monitor);
            if (!r.decompileCompleted()) { println("failed " + hex + ": " + r.getErrorMessage()); continue; }
            String name = String.format("%08x", f.getEntryPoint().getOffset());
            Files.writeString(new File(out, name + ".c").toPath(), r.getDecompiledFunction().getC());
            HighFunction hf = r.getHighFunction();
            StringBuilder s = new StringBuilder(own(f, hf.getFunctionPrototype())).append('\n');
            StringBuilder glob = new StringBuilder(), str = new StringBuilder();
            Set<String> seen = new HashSet<>();
            List<ClangNode> tokens = new ArrayList<>();
            r.getCCodeMarkup().flatten(tokens);
            for (ClangNode node : tokens) {
                if (!(node instanceof ClangVariableToken)) continue;
                ClangVariableToken t = (ClangVariableToken) node;
                String text = t.getText();
                Varnode v = t.getVarnode();
                HighVariable hv = t.getHighVariable();
                if (text.startsWith("\"") && v != null && v.isConstant()) {
                    if (seen.add("s" + v.getOffset())) str.append(String.format("%08x;%s%n", v.getOffset(), text));
                } else if (hv instanceof HighGlobal && hv.getRepresentative() != null) {
                    Varnode rep = hv.getRepresentative();
                    if (rep.getAddress().isMemoryAddress() && seen.add("g" + text))
                        glob.append(String.format("%s;%08x;%s%n", text, rep.getOffset(), hv.getDataType().getDisplayName()));
                } else if (v != null && v.isAddress() && seen.add("g" + text) && text.matches("(DAT|PTR|LAB|s|u)_\\w+")) {
                    glob.append(String.format("%s;%08x;%s%n", text, v.getOffset(), "undefined"));
                }
            }
            Files.writeString(new File(out, name + ".glob").toPath(), glob.toString());
            Files.writeString(new File(out, name + ".str").toPath(), str.toString());
            for (Function c : f.getCalledFunctions(monitor)) s.append(calleeSig(c)).append('\n');
            Files.writeString(new File(out, name + ".sig").toPath(), s.toString());
            n++;
        }
        d.dispose();
        println("exported " + n + " functions to " + out);
    }
}
