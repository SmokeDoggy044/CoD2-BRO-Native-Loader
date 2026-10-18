// Applies names from a CSV (address,name,... with a header line, e.g.
// tools/data/q3_name_proposals.csv or manual_names.csv). Creates the function first
// where Ghidra has none (command handlers only referenced as pointers), and only
// renames functions that still have a default FUN_ name. Rows whose third column
// is "data" name a global instead (only over a default DAT_/label name).
// Script argument: CSV path.
//@category CodBro

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File csv = args.length > 0 ? new File(args[0]) : askFile("Names CSV", "Apply");
        int renamed = 0, created = 0, skipped = 0, labels = 0;
        SymbolTable symbols = currentProgram.getSymbolTable();
        try (BufferedReader r = new BufferedReader(new FileReader(csv))) {
            String line = r.readLine(); // header
            while ((line = r.readLine()) != null) {
                String[] cols = line.split(",", 4);
                if (cols.length < 2) continue;
                Address addr = toAddr(Long.parseLong(cols[0], 16));
                String name = cols[1];
                if (cols.length > 2 && cols[2].equals("data")) {
                    Symbol sym = symbols.getPrimarySymbol(addr);
                    if (sym != null && sym.getSource() != SourceType.DEFAULT
                            && sym.getSource() != SourceType.ANALYSIS) {
                        println("kept label " + sym.getName() + " at " + addr + " (proposed " + name + ")");
                        skipped++;
                        continue;
                    }
                    Symbol label = symbols.createLabel(addr, name, SourceType.USER_DEFINED);
                    label.setPrimary();
                    labels++;
                    continue;
                }
                Function f = getFunctionAt(addr);
                if (f == null) {
                    if (getInstructionAt(addr) == null) disassemble(addr);
                    f = createFunction(addr, null);
                    if (f == null) {
                        println("could not create a function at " + addr + " for " + name);
                        skipped++;
                        continue;
                    }
                    created++;
                }
                if (!f.getName().startsWith("FUN_")) {
                    println("kept " + f.getName() + " at " + addr + " (proposed " + name + ")");
                    skipped++;
                    continue;
                }
                f.setName(name, SourceType.USER_DEFINED);
                renamed++;
            }
        }
        println("Renamed " + renamed + " (" + created + " functions created), " + labels
                + " globals named, skipped " + skipped);
    }
}
