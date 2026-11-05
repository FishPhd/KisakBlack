// @category KisakBlack
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import com.google.gson.Gson;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
public class ExportMultiplayerFunctions extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs();
        Set<String> wanted=new HashSet<>();
        for(int n=1;n<a.length;n++) wanted.addAll(Arrays.asList(a[n].split(",")));
        List<Object> rows=new ArrayList<>();
        var fs=currentProgram.getFunctionManager().getFunctions(true);
        while(fs.hasNext()) {
            var f=fs.next(); if(!wanted.contains(f.getName(true))) continue;
            List<Object> instructions=new ArrayList<>();
            var it=currentProgram.getListing().getInstructions(f.getBody(),true);
            while(it.hasNext()) {
                var i=it.next(); StringBuilder hex=new StringBuilder();
                for(byte b:i.getBytes()) hex.append(String.format("%02x",b&255));
                instructions.add(Map.of("address",i.getAddress().getOffset(),"hex",hex.toString(),"text",i.toString()));
            }
            rows.add(Map.of("name",f.getName(true),"entry",f.getEntryPoint().getOffset(),"instructions",instructions));
        }
        if(rows.size()!=wanted.size()) throw new IllegalStateException("Missing/duplicate scoped symbols: "+rows.size()+" of "+wanted.size());
        try(var out=new PrintWriter(new OutputStreamWriter(new FileOutputStream(a[0]),StandardCharsets.UTF_8))) {
            out.println(new Gson().toJson(Map.of("binary_sha256",currentProgram.getExecutableSHA256(),"functions",rows)));
        }
    }
}
