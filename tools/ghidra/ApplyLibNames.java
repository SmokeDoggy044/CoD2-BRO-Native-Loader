// Applies library names from a CSV (address,namespace,name,... with a header line, made by
// libmatch.py + libnames.py): creates the function where Ghidra has none, renames only
// default FUN_ names, and puts the function in the library's namespace (XAPILIB, UIX, ...)
// like the XBE loader's own library names. Script argument: CSV path.
//@category XboxPort

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

public class ApplyLibNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        File csv = new File(getScriptArgs()[0]);
        SymbolTable st = currentProgram.getSymbolTable();
        int renamed = 0, created = 0, kept = 0, failed = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(csv))) {
            String line = r.readLine();
            while ((line = r.readLine()) != null) {
                String[] f = line.split(",");
                if (f.length < 3) continue;
                Address a = toAddr(Long.parseLong(f[0], 16));
                Function fn = getFunctionAt(a);
                if (fn == null) {
                    disassemble(a);
                    fn = createFunction(a, null);
                    if (fn == null) { failed++; continue; }
                    created++;
                }
                if (!fn.getName().startsWith("FUN_")) { kept++; continue; }
                Namespace ns = st.getNamespace(f[1], currentProgram.getGlobalNamespace());
                if (ns == null) ns = st.createNameSpace(currentProgram.getGlobalNamespace(), f[1], SourceType.ANALYSIS);
                try {
                    fn.setParentNamespace(ns);
                    fn.setName(f[2], SourceType.ANALYSIS);
                    renamed++;
                } catch (Exception e) {
                    println("failed " + f[0] + " " + f[2] + ": " + e.getMessage());
                    failed++;
                }
            }
        }
        println("Renamed " + renamed + " (" + created + " functions created), kept " + kept + ", failed " + failed);
    }
}
