// @category KisakBlack
// Headless arguments: output.BinExport inventory.jsonl
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import com.google.security.binexport.BinExport2Builder;
import com.google.gson.Gson;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ExportBinaryDiff extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("Expected BinExport and inventory paths");
        // Export first: the upstream builder can discover additional functions.
        var builder = new BinExport2Builder(currentProgram, currentProgram.getMemory());
        builder.setPrependNamespace(true);
        var proto = builder.build();
        monitor.checkCancelled();
        try (var stream = new FileOutputStream(args[0])) { proto.writeTo(stream); }
        Gson gson = new Gson();
        AddressSet decoded = new AddressSet();
        long executableBytes = 0;
        for (var block : currentProgram.getMemory().getBlocks()) {
            if (block.isExecute() && block.isInitialized()) executableBytes += block.getSize();
        }
        int count = 0;
        try (var out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(args[1]), StandardCharsets.UTF_8))) {
            out.println(gson.toJson(Map.of("record", "metadata", "schema", 1,
                "sha256", currentProgram.getExecutableSHA256(),
                "image_base", currentProgram.getImageBase().getOffset(),
                "language", currentProgram.getLanguageID().toString(), "executable_bytes", executableBytes)));
            var functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                monitor.checkCancelled();
                Function f = functions.next();
                if (f.isExternal() || !f.getEntryPoint().isMemoryAddress()) continue;
                int instructions = 0;
                long bytes = 0;
                var it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction i = it.next(); instructions++; bytes += i.getLength();
                    decoded.add(i.getMinAddress(), i.getMaxAddress());
                }
                Map<String,Object> row = new LinkedHashMap<>();
                row.put("record", "function"); row.put("address", f.getEntryPoint().getOffset());
                row.put("name", f.getName(true)); row.put("signature", f.getPrototypeString(true, false));
                row.put("bytes", bytes); row.put("instructions", instructions); row.put("thunk", f.isThunk());
                row.put("symbol_source", f.getSymbol().getSource().toString());
                out.println(gson.toJson(row)); count++;
            }
            out.println(gson.toJson(Map.of("record", "complete", "functions", count,
                "unique_decoded_bytes", decoded.getNumAddresses())));
            if (out.checkError()) throw new IOException("Inventory write failed");
        }
        println("KISAK_BINARY_DIFF_EXPORT_COMPLETE");
    }
}
