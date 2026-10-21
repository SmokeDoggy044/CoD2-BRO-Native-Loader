// Creates or updates the structs of tools/data/structs.txt in category /bro and
// types the globals, parameters and return values it lists. A struct is rebuilt
// from the file each run (fields not in the file go back to undefined), so the
// file is the only place to edit layouts. Report: one line per problem.
// Args: STRUCTS_TXT (the repository root is two folders up from it)
//@category CodBro

import java.io.File;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

public class ApplyStructs extends GhidraScript {
    static final CategoryPath CATEGORY = new CategoryPath("/bro");
    DataTypeManager dtm;
    Map<String, Structure> structs = new LinkedHashMap<>();
    int problems = 0;

    @Override
    public void run() throws Exception {
        File spec = new File(getScriptArgs()[0]).getAbsoluteFile();
        File root = spec.getParentFile().getParentFile().getParentFile();
        dtm = currentProgram.getDataTypeManager();
        List<String[]> lines = new ArrayList<>();
        for (String raw : Files.readAllLines(spec.toPath())) {
            String line = raw.replaceAll("#.*", "").trim();
            if (!line.isEmpty()) lines.add(line.split("\\s+"));
        }
        // Pass 1: every struct exists (empty, right size) so fields can point at any of them.
        for (String[] t : lines) {
            if (!t[0].equals("struct")) continue;
            int size = Integer.decode(t[2]);
            Structure s = (Structure) dtm.getDataType(CATEGORY, t[1]);
            if (s == null) {
                s = (Structure) dtm.addDataType(new StructureDataType(CATEGORY, t[1], size, dtm),
                        DataTypeConflictHandler.REPLACE_HANDLER);
            } else {
                s.deleteAll();
                s.setLength(size);
            }
            structs.put(t[1], s);
        }
        // Pass 2: fields, then the typed globals, parameters and returns.
        Structure current = null;
        int fields = 0, typed = 0;
        for (String[] t : lines) {
            try {
                switch (t[0]) {
                    case "struct":
                        current = structs.get(t[1]);
                        break;
                    case "global":
                        typed += typeGlobal(t[1], type(t[2]));
                        break;
                    case "globals":
                        DataType dt = type(t[2]);
                        List<String> rows = Files.readAllLines(new File(root, t[1]).toPath());
                        for (String row : rows.subList(1, rows.size()))
                            typed += typeGlobal(row.split(",")[0], dt);
                        break;
                    case "param":
                        typed += t[2].matches("\\d+") ? typeParam(t[1], Integer.parseInt(t[2]), type(t[3]))
                                : typeRegisterParam(t[1], t[2], type(t[3]));
                        break;
                    case "return":
                        Function f = function(t[1]);
                        if (f != null) {
                            f.setReturnType(type(t[2]), SourceType.USER_DEFINED);
                            typed++;
                        }
                        break;
                    default:
                        int offset = Integer.decode(t[0]);
                        DataType ft = type(t[2]);
                        current.replaceAtOffset(offset, ft, ft.getLength(), t[1], null);
                        fields++;
                }
            } catch (Exception e) {
                problem(String.join(" ", t) + ": " + e.getMessage());
            }
        }
        println("ApplyStructs: " + structs.size() + " structs, " + fields + " fields, " + typed + " typed, "
                + problems + " problems");
    }

    void problem(String text) {
        problems++;
        println("  " + text);
    }

    DataType type(String spec) throws Exception {
        String base = spec.replaceAll("[\\*\\[].*", "");
        DataType dt = structs.containsKey(base) ? structs.get(base) : builtin(base);
        if (dt == null) throw new Exception("unknown type " + base);
        for (String rest = spec.substring(base.length()); !rest.isEmpty(); ) {
            if (rest.startsWith("*")) {
                dt = new PointerDataType(dt, dtm);
                rest = rest.substring(1);
            } else {
                int close = rest.indexOf(']');
                int n = Integer.decode(rest.substring(1, close));
                dt = new ArrayDataType(dt, n, dt.getLength(), dtm);
                rest = rest.substring(close + 1);
            }
        }
        return dt;
    }

    DataType builtin(String name) {
        switch (name) {
            case "pointer": return new PointerDataType(dtm);
            case "int": return ghidra.program.model.data.IntegerDataType.dataType;
            case "uint": return ghidra.program.model.data.UnsignedIntegerDataType.dataType;
            case "short": return ghidra.program.model.data.ShortDataType.dataType;
            case "ushort": return ghidra.program.model.data.UnsignedShortDataType.dataType;
            case "char": return ghidra.program.model.data.CharDataType.dataType;
            case "uchar": return ghidra.program.model.data.UnsignedCharDataType.dataType;
            case "float": return ghidra.program.model.data.FloatDataType.dataType;
            default: return dtm.getDataType("/" + name);
        }
    }

    Address resolve(String what) {
        try {
            if (what.matches("(0x)?[0-9a-fA-F]{6,8}")) return toAddr(Long.parseLong(what.replace("0x", ""), 16));
        } catch (Exception e) {
            // a name
        }
        for (Symbol s : currentProgram.getSymbolTable().getGlobalSymbols(what))
            return s.getAddress();
        return null;
    }

    Function function(String what) {
        Address at = resolve(what);
        Function f = at == null ? null : getFunctionAt(at);
        if (f == null) problem(what + ": no such function");
        return f;
    }

    int typeGlobal(String what, DataType dt) throws Exception {
        Address at = resolve(what);
        if (at == null) {
            problem(what + ": no such global");
            return 0;
        }
        if (getFunctionContaining(at) != null || getInstructionAt(at) != null) {
            problem(what + ": is code");
            return 0;
        }
        Address last = at.add(dt.getLength() - 1);
        currentProgram.getListing().clearCodeUnits(at, last, false);
        createData(at, dt);
        return 1;
    }

    int typeParam(String what, int index, DataType dt) throws Exception {
        Function f = function(what);
        if (f == null) return 0;
        if (f.getParameterCount() <= index) { // commit the decompiler's parameters first
            DecompInterface d = new DecompInterface();
            d.openProgram(currentProgram);
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            if (r.getHighFunction() != null)
                HighFunctionDBUtil.commitParamsToDatabase(r.getHighFunction(), true,
                        HighFunctionDBUtil.ReturnCommitOption.NO_COMMIT, SourceType.ANALYSIS);
            d.dispose();
        }
        if (f.getParameterCount() <= index) {
            problem(what + " " + index + ": only " + f.getParameterCount() + " stack parameters (register argument?)");
            return 0;
        }
        Parameter p = f.getParameter(index);
        p.setDataType(dt, SourceType.USER_DEFINED);
        return 1;
    }

    // A register argument: switches the function to custom storage, keeps its other
    // parameters where they are and types (or adds) the one in REGISTER.
    int typeRegisterParam(String what, String register, DataType dt) throws Exception {
        Function f = function(what);
        if (f == null) return 0;
        Register reg = currentProgram.getRegister(register.toUpperCase());
        if (reg == null) {
            problem(what + ": no register " + register);
            return 0;
        }
        List<Variable> params = new ArrayList<>();
        boolean found = false;
        for (Parameter p : f.getParameters()) {
            if (p.isRegisterVariable() && p.getRegister().equals(reg)) {
                params.add(new ParameterImpl(p.getName(), dt, reg, currentProgram, SourceType.USER_DEFINED));
                found = true;
            } else {
                params.add(new ParameterImpl(p, currentProgram));
            }
        }
        if (!found) params.add(0, new ParameterImpl("ent", dt, reg, currentProgram, SourceType.USER_DEFINED));
        f.updateFunction(f.getCallingConventionName(), f.getReturn(), params,
                Function.FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
        return 1;
    }
}
