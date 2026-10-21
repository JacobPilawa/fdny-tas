# environment/scripts/init_research_db.ps1
#
# Creates the Phase 3 research datastore ($ResearchDbPath) by applying
# research_schema.sql with the installed sqlite3 CLI. Idempotent -- the
# schema uses CREATE TABLE IF NOT EXISTS, so re-running is safe and won't
# touch existing rows. Also seeds the initial research_tasks queue from
# RESEARCH.md's strategy on first creation only.

. (Join-Path $PSScriptRoot '..\config.ps1')

if (-not (Test-Path $SqliteExe)) {
    throw "sqlite3 not found at $SqliteExe. Run get_sqlite.ps1 + install_sqlite.ps1 first."
}
if (-not (Test-Path $ResearchSchema)) {
    throw "Schema file not found: $ResearchSchema"
}

New-Item -ItemType Directory -Force -Path $ResearchDir | Out-Null
$fresh = -not (Test-Path $ResearchDbPath)

& $SqliteExe $ResearchDbPath ".read $($ResearchSchema -replace '\\','/')"
if ($LASTEXITCODE -ne 0) { throw "sqlite3 schema apply failed ($LASTEXITCODE)" }

if ($fresh) {
    $seed = @"
INSERT INTO research_tasks (title, subsystem, priority, notes) VALUES
 ('Apply Genesis3D struct types from open-source headers', 'engine', 1, 'geVec3d/geXForm3d/geWorld/geActor/geEntity/geMotion/gePath/geCamera/geBitmap'),
 ('Map 54 assert .cpp strings -> function clusters', 'all', 1, 'xref each source-path string; name cluster by class/file'),
 ('Label bootstrap + frame loop', 'main', 1, 'WinMain -> per-frame update/render call tree'),
 ('Label entity base classes + Genesis3D entity registration', 'sharedents', 2, 'shared vtable/userdata shape for *Ent.cpp'),
 ('Label rescue subsystem', 'rescue', 2, 'RescueBot first, then victim/fire/cam/player rescue entities'),
 ('Label hose subsystem', 'hose', 3, 'PlayerHoseEnt + HoseGfx'),
 ('Label script VM + BoundsTrigger', 'script', 3, 'scriptengine/parser/trigger/actor + sharedents BoundsTriggerEnt'),
 ('Resolve where player movement is integrated', 'main', 2, 'no movement.cpp string; check evergame/PlayerManager/GameControls/ROC'),
 ('Identify what "ROC" is', 'ROC', 3, 'own resource parser + AI entity model + message mapping'),
 ('Label ROC / basic / sound / BluelAPI remainder', 'all', 4, 'by file cluster');
INSERT INTO events (kind, summary) VALUES ('session', 'research.db created; task queue seeded from RESEARCH.md strategy');
"@
    $seed | & $SqliteExe $ResearchDbPath
    if ($LASTEXITCODE -ne 0) { throw "sqlite3 seed failed ($LASTEXITCODE)" }
    Write-Host "Created $ResearchDbPath (schema + seeded task queue)."
} else {
    Write-Host "Schema re-applied to existing $ResearchDbPath (no data touched)."
}

$counts = & $SqliteExe $ResearchDbPath @"
SELECT 'symbols='||count(*) FROM symbols
UNION ALL SELECT 'hypotheses='||count(*) FROM hypotheses
UNION ALL SELECT 'evidence='||count(*) FROM evidence
UNION ALL SELECT 'tasks='||count(*) FROM research_tasks
UNION ALL SELECT 'events='||count(*) FROM events;
"@
$counts | ForEach-Object { Write-Host "  $_" }
