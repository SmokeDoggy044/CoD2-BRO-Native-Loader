// Data inside the code sections, for tools/text_clear_keep.py (own engine host 0.3). Writes three files to the
// directory given as the script argument (default build/naming of the decomp repo, as an absolute path):
//   code_section_instructions.txt  "begin end"          runs of disassembled instructions
//   code_section_data.txt           "address length section type"  defined data
//   code_section_data_refs.txt      "address section from"  non-instruction addresses with a data reference
// Read-only. Through MCP: run_script_inline with this file's contents and the output directory as args.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ExportCodeSectionData extends GhidraScript {
    static final String[] SECTIONS = {".text", "XONLINE", "XNET", "D3DX", "DSOUND", "D3D", "XGRPH", "XPP"};

    public void run() throws Exception {
        String dir = getScriptArgs().length > 0 ? getScriptArgs()[0]
                : "/mnt/ms1/Projects/Decomps/Xbox/cod_bro/decomp/build/naming";
        Listing listing = currentProgram.getListing();
        ReferenceManager references = currentProgram.getReferenceManager();
        try (PrintWriter instructions = new PrintWriter(new FileWriter(dir + "/code_section_instructions.txt"));
             PrintWriter data = new PrintWriter(new FileWriter(dir + "/code_section_data.txt"));
             PrintWriter refs = new PrintWriter(new FileWriter(dir + "/code_section_data_refs.txt"))) {
            for (String name : SECTIONS) {
                MemoryBlock block = currentProgram.getMemory().getBlock(name);
                if (block == null) continue;
                AddressSet set = new AddressSet(block.getStart(), block.getEnd());
                long start = -1, end = -1;
                InstructionIterator it = listing.getInstructions(set, true);
                while (it.hasNext()) {
                    Instruction instruction = it.next();
                    long address = instruction.getAddress().getOffset();
                    if (address != end) {
                        if (start >= 0) instructions.println(String.format("%08x %08x", start, end));
                        start = address;
                    }
                    end = address + instruction.getLength();
                }
                if (start >= 0) instructions.println(String.format("%08x %08x", start, end));
                DataIterator defined = listing.getDefinedData(set, true);
                while (defined.hasNext()) {
                    Data item = defined.next();
                    data.println(String.format("%08x %d %s %s", item.getAddress().getOffset(), item.getLength(), name,
                                               item.getDataType().getName().replace(' ', '_')));
                }
                AddressIterator targets = references.getReferenceDestinationIterator(set, true);
                while (targets.hasNext()) {
                    Address target = targets.next();
                    if (listing.getInstructionContaining(target) != null) continue;
                    for (Reference reference : references.getReferencesTo(target)) {
                        if (reference.getReferenceType().isFlow()) continue;
                        refs.println(String.format("%08x %s %s", target.getOffset(), name, reference.getFromAddress()));
                        break;
                    }
                }
            }
        }
        println("written to " + dir);
    }
}
