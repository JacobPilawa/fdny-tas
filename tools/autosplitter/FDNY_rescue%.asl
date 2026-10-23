/*
 * ==============================================================================
 * FDNY FIREFIGHTER - RESCUE% AUTO SPLITTER (ALL RESCUE MAPS)       v2, 2026-10-23
 * ==============================================================================
 * Manual resets required: no automatic resetting (menus between levels are paused
 * time: after a win the timer waits for the next level's first gameplay frame).
 *
 * How it detects things (verified live on the game, core/evidence/autosplitter/):
 *
 *  STAGE DONE (oil rig A/B/C, boat A, subway A): touching the stage exit runs the
 *    script verb `finalexitloadmap <next>`, which stores the next map's name in the
 *    game session (GameSession+0x660, "pendingMap") on that exact frame and keeps it
 *    there. The load starts one frame later. The split fires when pendingMap changes
 *    to the next stage of the map being played. (The old script waited for mapName to
 *    change, which on a stage started from the menu only happens once the load has
 *    begun or finished: the late split.)
 *    Fallback: if pendingMap already held that name (a stage retried after an earlier
 *    exit), the split fires when the in-game load starts (gameState 1 -> 2) with
 *    pendingMap = next stage.
 *
 *  LEVEL WON (every map, including the last stage of boat / subway / oil rig): the
 *    mission result (PlayerRescueEnt+0x3438) goes 0 -> 1 on the frame the game declares
 *    the win (final exit touched, or every victim accounted for on exit-less maps).
 *    This replaces the eight per-map "end sound" slots: those are sound handles, they
 *    change during unrelated levels, and they fill in a frame or two after the win.
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
    int missionResult : 0x00103A78, 0x30, 0x3438;     // 0 playing, 1 won, 2 lost
    int totalVictims : 0x00103A78, 0x30, 0x3528;
    int rescuedVictims : 0x00103A78, 0x30, 0x352C;
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
    print("[FDNY] Rescue% auto splitter v2 loaded");
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
    // level won (every map): the mission result turns to 1
    if (current.gameState == 1 && old.missionResult == 0 && current.missionResult == 1)
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

// no reset block: manual resets (rescue% spans every level and its menus)
