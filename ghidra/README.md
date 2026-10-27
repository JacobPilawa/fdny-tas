# Ghidra export (labels, no game bytes)

Ghidra's own XML exporter, run with **Memory Contents off**. Each file holds a
program's memory map, functions, symbols/names, comments, data types, equates,
references, bookmarks and program trees, but none of the binary's bytes. That makes
it safe to commit, and readable and diffable on GitHub.

| File | Program |
|---|---|
| `FDNYFirefighter.exe.xml` | the game executable |
| `Genesis.dll.xml` | the Genesis3D engine |
| `D3DDrv.dll.xml` | the D3D driver (raw binary import) |

**Regenerate** after Ghidra work: run `ghidra/scripts/ExportProgramBackup.java`
for each program, with args `<this folder> -`. Either use GhidrAssistMCP's
`scripts` run with `program_name`, or `analyzeHeadless` with `-postScript`. Then
commit the result.

**Restore on another machine:**
1. Import the original binary from your own game install into a new Ghidra project
   (same name, same loader; D3DDrv.dll as Raw Binary x86:LE:32).
2. Run File > Add To Program, select this XML, and keep the default options.

This restores names, comments, types and functions. Analysis state that the XML
format doesn't carry is not restored. For an exact copy you need a `.gzf` (Ghidra Zip
File) or a project archive. Both contain the game's bytes, so they stay out of this
repo.
