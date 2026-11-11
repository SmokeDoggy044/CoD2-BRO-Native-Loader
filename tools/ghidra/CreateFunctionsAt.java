// Creates a function at every address in a list file (one hex address per line, '#' comments), for gap functions
// the own-engine-host check found (--text-clear). Disassembles first when needed; an address inside another
// function's body cuts that function's body there first (a function that swallowed its neighbour).
// Args: LIST [dry]. Prints one line per address: created / exists / cut+created / FAILED.
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;

public class CreateFunctionsAt extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        boolean dry = args.length > 1;
        int created = 0, exists = 0, cut = 0, failed = 0;
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            Address at = toAddr(Long.parseLong(line.split("\\s+")[0], 16));
            if (getFunctionAt(at) != null) { exists++; continue; }
            Function outer = getFunctionContaining(at);
            String how = "created";
            if (dry) { println(at + (outer != null ? " inside " + outer.getName() : " free")); continue; }
            if (outer != null) {
                AddressSet body = new AddressSet(outer.getBody());
                body.delete(new AddressSet(at, outer.getBody().getMaxAddress()));
                outer.setBody(body);
                how = "cut " + outer.getName() + " + created";
                cut++;
            }
            if (getInstructionAt(at) == null) new DisassembleCommand(at, null, true).applyTo(currentProgram, monitor);
            if (new CreateFunctionCmd(at).applyTo(currentProgram, monitor) && getFunctionAt(at) != null) {
                created++;
                Function f = getFunctionAt(at);
                println(at + " " + how + " " + f.getName() + " " + f.getBody().getNumAddresses());
            } else {
                failed++;
                println(at + " FAILED");
            }
        }
        println("created " + created + " (cut " + cut + "), existed " + exists + ", failed " + failed);
    }
}
