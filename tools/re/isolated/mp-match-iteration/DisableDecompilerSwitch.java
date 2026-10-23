// @category KisakBlack
// Iteration analysis only. The recorded whole-EXE score still uses full analysis.
import ghidra.app.script.GhidraScript;

public class DisableDecompilerSwitch extends GhidraScript {
    public void run() throws Exception {
        setAnalysisOption(currentProgram, "Decompiler Switch Analysis", "false");
        println("KISAK_QUICK_ANALYSIS");
    }
}
