// @category KisakBlack
// Headless arguments: output.jsonl [decompile-rva]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import com.google.gson.Gson;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ExportInventory extends GhidraScript {
    private final Gson gson = new Gson();
    private long rva(Address address) { return address.subtract(currentProgram.getImageBase()); }
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("Expected output path");
        DecompInterface decompiler = null;
        long selected = args.length > 1 ? Long.decode(args[1]) : -1;
        if (selected >= 0) { decompiler = new DecompInterface(); decompiler.openProgram(currentProgram); }
        try (PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(args[0]), StandardCharsets.UTF_8))) {
            Map<String,Object> header = new LinkedHashMap<>();
            header.put("record", "metadata"); header.put("schema", 1);
            header.put("sha256", currentProgram.getExecutableSHA256());
            header.put("image_base", currentProgram.getImageBase().getOffset());
            header.put("language", currentProgram.getLanguageID().toString());
            out.println(gson.toJson(header));
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function f = functions.next(); Map<String,Object> row = new LinkedHashMap<>();
                row.put("record", "function"); row.put("rva", rva(f.getEntryPoint()));
                row.put("name", f.getName()); row.put("signature", f.getPrototypeString(true, false));
                row.put("calling_convention", f.getCallingConventionName());
                row.put("thunk", f.isThunk());
                List<Object> ranges = new ArrayList<>();
                for (AddressRange range : f.getBody().getAddressRanges()) {
                    Map<String,Object> r = new LinkedHashMap<>(); r.put("rva", rva(range.getMinAddress()));
                    r.put("size", range.getLength()); ranges.add(r);
                }
                row.put("ranges", ranges);
                Set<Long> callees = new TreeSet<>(), callers = new TreeSet<>(), dataRefs = new TreeSet<>();
                Set<String> strings = new TreeSet<>();
                Set<String> imports = new TreeSet<>();
                for (Function c : f.getCalledFunctions(monitor)) {
                    if (c.getEntryPoint().isMemoryAddress()) callees.add(rva(c.getEntryPoint()));
                    else imports.add(c.getName(true));
                }
                for (Function c : f.getCallingFunctions(monitor)) {
                    if (c.getEntryPoint().isMemoryAddress()) callers.add(rva(c.getEntryPoint()));
                }
                row.put("imports", imports);
                InstructionIterator instructions = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (instructions.hasNext()) {
                    Instruction instruction = instructions.next();
                    for (Reference ref : instruction.getReferencesFrom()) {
                        if (!ref.getReferenceType().isData() || !ref.getToAddress().isMemoryAddress()) continue;
                        dataRefs.add(rva(ref.getToAddress()));
                        Data data = currentProgram.getListing().getDefinedDataAt(ref.getToAddress());
                        if (data != null && data.hasStringValue()) strings.add(String.valueOf(data.getValue()));
                    }
                }
                row.put("callees", callees); row.put("callers", callers);
                row.put("data_refs", dataRefs); row.put("strings", strings);
                if (decompiler != null && f.getBody().contains(currentProgram.getImageBase().add(selected))) {
                    var result = decompiler.decompileFunction(f, 60, monitor);
                    row.put("decompilation", result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
                    List<String> disassembly = new ArrayList<>();
                    instructions = currentProgram.getListing().getInstructions(f.getBody(), true);
                    while (instructions.hasNext()) { Instruction i = instructions.next(); disassembly.add(i.getAddress() + " " + i); }
                    row.put("disassembly", disassembly);
                }
                out.println(gson.toJson(row));
            }
            if (monitor.isCancelled()) throw new IOException("Analysis cancelled; incomplete inventory");
            out.println("{\"record\":\"complete\"}");
        } finally { if (decompiler != null) decompiler.dispose(); }
    }
}
