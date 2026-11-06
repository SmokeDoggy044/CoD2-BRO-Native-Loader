// Applies our C prototypes (tools/protos.py output) to the functions and types the script-interface slots.
// Script arguments: DIR [apply]   (functions.csv column storage: explicit storage from an asm shim)
// Script arguments: DIR [apply]   DIR = build/protos; without "apply" it only counts (dry run).
// Storage is set explicitly (custom storage) from the convention: cdecl/stdcall all on the stack, thiscall
// first in ECX, fastcall first two in ECX/EDX, regparm(N) the first N integer-class ones in EAX/EDX/ECX.
// Our struct names become placeholder structs in /bro (by-value ones at their real size) unless a struct of
// that name exists already (any category; by value only if the size matches).
//@category CoD BRO
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.util.*;

public class ApplyPrototypes extends GhidraScript {
    private final Map<String, DataType> structs = new HashMap<>();
    private DataTypeManager dtm;

    private DataType scalar(String n) {
        switch (n) {
            case "uint32_t": case "uintptr_t": case "size_t": case "unsigned": return UnsignedIntegerDataType.dataType;
            case "int32_t": case "int": return IntegerDataType.dataType;
            case "uint8_t": return ByteDataType.dataType;
            case "int8_t": return SignedByteDataType.dataType;
            case "uint16_t": return UnsignedShortDataType.dataType;
            case "int16_t": return ShortDataType.dataType;
            case "float": return FloatDataType.dataType;
            case "double": return DoubleDataType.dataType;
            case "bool": return BooleanDataType.dataType;
            case "char": return CharDataType.dataType;
            case "void": return VoidDataType.dataType;
            case "uint64_t": return UnsignedLongLongDataType.dataType;
            case "int64_t": return LongLongDataType.dataType;
            case "long double": return Float10DataType.dataType;
        }
        return null;
    }

    private DataType type(String t) {
        int stars = 0;
        while (t.endsWith("*")) { stars++; t = t.substring(0, t.length() - 1); }
        DataType dt = scalar(t);
        if (dt == null) dt = structs.get(t);
        if (dt == null) throw new IllegalArgumentException("unknown type " + t);
        for (int i = 0; i < stars; i++) dt = new PointerDataType(dt, 4, dtm);
        return dt;
    }

    private DataType struct(String name, int size) {
        List<DataType> found = new ArrayList<>();
        dtm.findDataTypes(name, found);
        for (DataType d : found) {
            if (!(d instanceof Structure)) continue;
            if (size <= 0 || d.getLength() == size) return d;
        }
        StructureDataType s = new StructureDataType(new CategoryPath("/bro"), name, Math.max(size, 0), dtm);
        return dtm.addDataType(s, DataTypeConflictHandler.KEEP_HANDLER);
    }

    private static boolean integerClass(DataType dt) {
        return dt instanceof Pointer || (dt.getLength() <= 4 && !(dt instanceof FloatDataType) && !(dt instanceof Structure));
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File dir = new File(args[0]);
        boolean apply = args.length > 1 && args[1].equals("apply");
        dtm = currentProgram.getDataTypeManager();
        Register eax = currentProgram.getRegister("EAX"), ecx = currentProgram.getRegister("ECX"),
                 edx = currentProgram.getRegister("EDX"), st0 = currentProgram.getRegister("ST0");

        for (String[] r : csv(new File(dir, "structs.csv"))) structs.put(r[0], struct(r[0], Integer.parseInt(r[1])));

        int done = 0, noFunction = 0, userDefined = 0, failed = 0, slotsTyped = 0;
        Map<Long, FunctionDefinitionDataType> defs = new HashMap<>();
        for (String[] r : csv(new File(dir, "functions.csv"))) {
            Address a = toAddr(Long.parseLong(r[0], 16));
            Function f = getFunctionAt(a);
            try {
                String cc = r[2];
                int regs = Integer.parseInt(r[3]);
                DataType ret = type(r[4]);
                String[] ps = r[5].isEmpty() ? new String[0] : r[5].split("\\|", -1);
                String[] names = r.length > 7 && !r[7].isEmpty() ? r[7].split("\\|", -1) : new String[0];
                List<ParameterImpl> params = new ArrayList<>();
                List<ParameterDefinition> pdefs = new ArrayList<>();
                String[] shim = r.length > 8 && !r[8].isEmpty() ? r[8].split(";")[0].split("\\|", -1) : null;
                int purge = shim != null ? Integer.parseInt(r[8].split(";purge:")[1]) : -1;
                int stack = 4, used = 0;
                Register[] regOrder = {eax, edx, ecx};
                for (int i = 0; i < ps.length; i++) {
                    DataType dt = type(ps[i]);
                    String name = i < names.length && !names[i].isEmpty() ? names[i] : "param_" + (i + 1);
                    pdefs.add(new ParameterDefinitionImpl(name, dt, null));
                    VariableStorage st;
                    if (shim != null) {
                        String w = shim[i];
                        st = w.startsWith("stack:") ? new VariableStorage(currentProgram, Integer.parseInt(w.substring(6)), dt.getLength())
                                                    : new VariableStorage(currentProgram, currentProgram.getRegister(w.toUpperCase()));
                    }
                    else if (cc.equals("thiscall") && i == 0) st = new VariableStorage(currentProgram, ecx);
                    else if (cc.equals("fastcall") && used < 2 && integerClass(dt))
                        st = new VariableStorage(currentProgram, used++ == 0 ? ecx : edx);
                    else if (regs > 0 && used < regs && integerClass(dt))
                        st = new VariableStorage(currentProgram, regOrder[used++]);
                    else {
                        int len = Math.max(4, (dt.getLength() + 3) & ~3);
                        st = new VariableStorage(currentProgram, stack, dt.getLength());
                        stack += len;
                    }
                    params.add(new ParameterImpl(name, dt, st, currentProgram));
                }
                VariableStorage rs;
                if (ret instanceof VoidDataType) rs = VariableStorage.VOID_STORAGE;
                else if (ret instanceof FloatDataType || ret instanceof DoubleDataType || ret instanceof Float10DataType) rs = new VariableStorage(currentProgram, st0);
                else if (ret.getLength() == 8) rs = new VariableStorage(currentProgram, eax, edx);
                else rs = new VariableStorage(currentProgram, eax);
                FunctionDefinitionDataType def = new FunctionDefinitionDataType(r[1], dtm);
                def.setReturnType(ret);
                def.setArguments(pdefs.toArray(new ParameterDefinition[0]));
                def.setVarArgs(r[6].equals("1"));
                def.setCallingConvention("__" + (regs > 0 && cc.equals("cdecl") ? "cdecl" : cc));
                defs.put(Long.parseLong(r[0], 16), def);
                if (f == null) { noFunction++; continue; }
                if (f.getSignatureSource() == SourceType.USER_DEFINED) userDefined++;
                if (apply) {
                    String ccName = "__" + cc;
                    f.updateFunction(ccName, new ReturnParameterImpl(ret, rs, currentProgram), params,
                                     FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
                    f.setVarArgs(r[6].equals("1"));
                    if (purge >= 0) f.setStackPurgeSize(purge);
                }
                done++;
            } catch (Exception e) {
                failed++;
                if (failed <= 20) println("failed " + r[0] + " " + r[1] + ": " + e.getMessage());
            }
        }

        for (String[] r : csv(new File(dir, "slots.csv"))) {
            FunctionDefinitionDataType def = defs.get(Long.parseLong(r[1], 16));
            if (def == null) continue;
            if (apply) {
                Address a = toAddr(Long.parseLong(r[0], 16));
                DataType fd = dtm.addDataType(def, DataTypeConflictHandler.REPLACE_HANDLER);
                clearListing(a, a.add(3));
                createData(a, new PointerDataType(fd, 4, dtm));
            }
            slotsTyped++;
        }
        println((apply ? "applied " : "would apply ") + done + " prototypes (" + userDefined +
                " replaced a user-defined signature), " + noFunction + " rows without a function, " + failed +
                " failed; slots typed " + slotsTyped);
    }

    private static List<String[]> csv(File f) throws IOException {
        List<String[]> out = new ArrayList<>();
        try (BufferedReader in = new BufferedReader(new FileReader(f))) {
            String line = in.readLine();
            while ((line = in.readLine()) != null) if (!line.isEmpty()) out.add(line.split(",", -1));
        }
        return out;
    }
}
