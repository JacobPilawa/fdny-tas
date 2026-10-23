/*
 * ==============================================================================
 * FDNY FIREFIGHTER - IL AUTO SPLITTER (ALL RESCUE MAPS)            v2.1, 2026-10-23
 * ==============================================================================
 * Resets any time gameState is 0 (game in menu). INTENDED FOR IL RUNS ONLY.
 *
 * How it detects things (verified live on the game, core/evidence/autosplitter/):
 *
 *  STAGE DONE (oil rig A/B/C, boat A, subway A): touching the stage exit runs the
 *    script verb `finalexitloadmap <next>`, which stores the next map's name in the
 *    game session (GameSession+0x660, "pendingMap") on that exact frame and keeps it
 *    there. The load starts one frame later. The split fires when pendingMap changes
 *    to the next stage of the map being played. 
 *    Fallback: if pendingMap already held that name (a stage retried after an earlier
 *    exit), the split fires when the in-game load starts (gameState 1 -> 2) with
 *    pendingMap = next stage.
 *
 *  LEVEL WON (every map, including the last stage of boat / subway / oil rig): the
 *    level's end sound starts (its slot goes 0 -> nonzero), the v1 method, unchanged.
 *    Checked against the game's own win flag (PlayerRescueEnt+0x3438 0 -> 1): on every
 *    level the sound follows the win by 0-2 game frames (0-32 ms on the VM), and on its
 *    own map the slot stays 0 until the win. The slots do change during other levels,
 *    so each is only read on its own map.
 *
 *  LOADS: gameState 2 = loading, 3 = level start; gameplay resumes at 3 -> 1. After a
 *    win the timer stays paused until the next level is playing.
 * ==============================================================================
 */

state("FDNYFirefighter")
{
    int gameState : 0x00102AAC, 0x6F0;
    string16 mapName : 0x00102AAC, 0x688, 0x0, 0xC;
    string16 pendingMap : 0x00102AAC, 0x660, 0xC;     // next map of the last in-game level change
    int totalVictims : 0x00103A78, 0x30, 0x3528;
    int rescuedVictims : 0x00103A78, 0x30, 0x352C;

    // Single-stage map completion sounds
    int bankLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0x158, 0x0;
    int barLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0xFC, 0x0;
    int gasStationLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0xF0, 0x0;
    int militaryBaseLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0xC8, 0x0;
    int trainingLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0x104, 0x0;

    // Multi-stage final completion sounds
    int boatLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0xBC, 0x0;
    int oilRigDLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0xC4, 0x0;
    int subwayLevelEndSoundPlayed : 0x00103A78, 0x8, 0x0, 0x144, 0x0;
}

startup
{
    // the stage that follows each staged map (finalexitloadmap targets in the .bsp scripts)
    vars.nextStage = new Dictionary<string, string> {
        { "oil_rig_a", "oil_rig_b" }, { "oil_rig_b", "oil_rig_c" }, { "oil_rig_c", "oil_rig_d" },
        { "boat_a", "boat_b" }, { "subway_a", "subway_b" },
    };
    vars.levelMap = "";       // the map being played (set when gameplay starts)
    vars.inLevel = false;     // playing since the last 3 -> 1, no menu since
    vars.stageSplit = false;  // this stage already split
    vars.levelDone = false;   // won: paused until the next level is playing
    print("[FDNY] IL auto splitter");
}

init
{
    vars.levelMap = "";
    vars.inLevel = false;
    vars.stageSplit = false;
    vars.levelDone = false;
}

update
{
    vars.map = (current.mapName ?? "").ToLower().Trim();
    vars.pending = (current.pendingMap ?? "").ToLower().Trim();
    vars.oldPending = (old.pendingMap ?? "").ToLower().Trim();

    // gameplay (re)starts: a level or stage finished loading
    if (old.gameState == 3 && current.gameState == 1)
    {
        vars.levelMap = vars.map;
        vars.inLevel = true;
        vars.stageSplit = false;
        vars.levelDone = false;
        print(String.Format("[FDNY] playing '{0}'", vars.levelMap));
    }
    if (current.gameState == 0)
        vars.inLevel = false;
}

start
{
    // the first gameplay frame of a level
    if (old.gameState == 3 && current.gameState == 1)
    {
        print(String.Format("[FDNY START] '{0}'", vars.map));
        return true;
    }
    return false;
}

split
{
    string next;
    bool staged = vars.nextStage.TryGetValue((string)vars.levelMap, out next);

    // stage exit touched: pendingMap became the next stage
    if (staged && !vars.stageSplit && vars.pending == next && vars.pending != vars.oldPending)
    {
        print(String.Format("[FDNY SPLIT] stage '{0}' -> '{1}' (exit touched)", vars.levelMap, next));
        vars.stageSplit = true;
        return true;
    }
    // fallback: the in-game load of the next stage starts (pendingMap already held its name)
    if (staged && !vars.stageSplit && vars.inLevel && old.gameState == 1 && current.gameState == 2 &&
        vars.pending == next)
    {
        print(String.Format("[FDNY SPLIT] stage '{0}' -> '{1}' (load start)", vars.levelMap, next));
        vars.stageSplit = true;
        return true;
    }
    // level won (every map): that map's end sound starts
    string m = vars.levelMap;
    bool won =
        (m.Contains("training") && old.trainingLevelEndSoundPlayed == 0 && current.trainingLevelEndSoundPlayed != 0) ||
        (m.Contains("bank") && old.bankLevelEndSoundPlayed == 0 && current.bankLevelEndSoundPlayed != 0) ||
        (m.Contains("bar_r") && old.barLevelEndSoundPlayed == 0 && current.barLevelEndSoundPlayed != 0) ||
        (m.Contains("gas_r") && old.gasStationLevelEndSoundPlayed == 0 && current.gasStationLevelEndSoundPlayed != 0) ||
        (m.Contains("mbase") && old.militaryBaseLevelEndSoundPlayed == 0 && current.militaryBaseLevelEndSoundPlayed != 0) ||
        (m.Contains("boat_b") && old.boatLevelEndSoundPlayed == 0 && current.boatLevelEndSoundPlayed != 0) ||
        (m.Contains("subway_b") && old.subwayLevelEndSoundPlayed == 0 && current.subwayLevelEndSoundPlayed != 0) ||
        (m.Contains("oil_rig_d") && old.oilRigDLevelEndSoundPlayed == 0 && current.oilRigDLevelEndSoundPlayed != 0);
    if (current.gameState == 1 && vars.inLevel && !vars.levelDone && won)
    {
        print(String.Format("[FDNY SPLIT] '{0}' won", vars.levelMap));
        vars.levelDone = true;
        return true;
    }
    return false;
}

isLoading
{
    return vars.levelDone || current.gameState == 2 || current.gameState == 3;
}

reset
{
    if (current.gameState == 0)
    {
        print("[FDNY RESET] returned to menu");
        return true;
    }
    return false;
}
