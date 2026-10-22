// Backs up the current program with Ghidra's own exporters (no custom format):
//   <xmlDir>/<program>.xml  XML exporter with the memory-contents option OFF, so the file
//                           holds names, functions, comments, types and references but no
//                           game bytes (safe to commit). Restore: import the original
//                           binary, then File > Add To Program > this XML.
//   <gzfDir>/<program>.gzf  Ghidra Zip File, exact and complete (contains the binary's
//                           bytes: keep it out of the repo).
// Args: <xmlDir> <gzfDir>   (either may be "-" to skip)
// Run once per program (GhidrAssistMCP scripts run with program_name, or headless).
//@category FDNY

import java.io.File;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.Option;
import ghidra.app.util.exporter.GzfExporter;
import ghidra.app.util.exporter.XmlExporter;

public class ExportProgramBackup extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("usage: ExportProgramBackup <xmlDir|-> <gzfDir|->");
            return;
        }
        String name = currentProgram.getName();

        if (!args[0].equals("-")) {
            File dir = new File(args[0]);
            dir.mkdirs();
            File out = new File(dir, name + ".xml");
            XmlExporter xml = new XmlExporter();
            List<Option> opts = xml.getOptions(() -> currentProgram);
            StringBuilder seen = new StringBuilder();
            for (Option o : opts) {
                String n = o.getName();
                if (n.toLowerCase().contains("memory contents") || n.toLowerCase().contains("bytes")) {
                    o.setValue(Boolean.FALSE);
                }
                seen.append(n).append('=').append(o.getValue()).append("; ");
            }
            xml.setOptions(opts);
            boolean ok = xml.export(out, currentProgram, null, monitor);
            println("XML " + (ok ? "OK " : "FAILED ") + out + " (" + out.length() + " bytes) options: " + seen);
            File bytes = new File(dir, name + ".bytes");
            if (bytes.exists()) {
                println("WARNING: " + bytes + " was written (memory contents still on)");
            }
        }

        if (!args[1].equals("-")) {
            File dir = new File(args[1]);
            dir.mkdirs();
            File out = new File(dir, name + ".gzf");
            if (out.exists()) {
                out.delete();
            }
            String how = "GzfExporter";
            boolean ok;
            try {
                ok = new GzfExporter().export(out, currentProgram, null, monitor);
            }
            catch (Exception e) {
                println("GzfExporter threw: " + e);
                ok = false;
            }
            if (!ok || out.length() == 0) {
                // GzfExporter refuses a program with unsaved changes; packing the saved
                // project file is what it does underneath.
                if (out.exists()) {
                    out.delete();
                }
                how = "DomainFile.packFile (last saved version" +
                    (currentProgram.isChanged() ? "; the open program has UNSAVED changes" : "") + ")";
                try {
                    currentProgram.getDomainFile().packFile(out, monitor);
                    ok = out.length() > 0;
                }
                catch (Exception e) {
                    println("packFile threw: " + e);
                    ok = false;
                }
            }
            println("GZF " + (ok ? "OK " : "FAILED ") + out + " (" + out.length() + " bytes) via " + how);
        }
    }
}
