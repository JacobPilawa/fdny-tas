// AslHost: runs a LiveSplit .asl with LiveSplit's own ASL engine (LiveSplit.Core.dll +
// Components\LiveSplit.ScriptableAutoSplit.dll), without the LiveSplit window, and logs
// what the timer does: start, split, reset, and every Game Time pause/resume, each with
// the Game Time, the high-resolution clock and the game's own clock.
//
// It drives the script the way the ASL component does: parse, startup, then Update() at
// the script's refresh rate (60 Hz unless the script sets refreshRate).
//
//   AslHost.exe <script.asl> <out.log> <seconds> [segments]
//
// The clock column is seconds of the Windows performance counter, the same clock as
// Python's time.perf_counter() in events.py, so the two logs line up.
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using LiveSplit.ASL;
using LiveSplit.Model;
using LiveSplit.Model.Comparisons;
using LiveSplit.Options;

static class AslHost
{
    [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint ms);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int n, out IntPtr read);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);

    const int GameClockAddr = 0x502AB8 + 0x44;  // the game clock's elapsed ms (one tick per GameSession::RunFrame)

    static StreamWriter log;
    static IntPtr game = IntPtr.Zero;
    static int gamePid;
    static LiveSplitState state;

    static double Now() { return Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency; }

    static string Clock()
    {
        if (game == IntPtr.Zero)
        {
            var p = Process.GetProcessesByName("FDNYFirefighter");
            if (p.Length == 0) return "None";
            gamePid = p[0].Id;
            game = OpenProcess(0x0010, false, gamePid);  // PROCESS_VM_READ
            if (game == IntPtr.Zero) return "None";
        }
        var buf = new byte[4];
        IntPtr read;
        if (!ReadProcessMemory(game, new IntPtr(GameClockAddr), buf, 4, out read))
        {
            CloseHandle(game);
            game = IntPtr.Zero;  // the game exited: reopen next time
            return "None";
        }
        return BitConverter.ToUInt32(buf, 0).ToString();
    }

    static string GameTime()
    {
        var t = state.CurrentTime.GameTime;
        return t.HasValue ? t.Value.TotalSeconds.ToString("F4") : "None";
    }

    static void Log(string msg)
    {
        log.WriteLine(string.Format("{0,12:F4} clock={1} {2}", Now(), Clock(), msg));
        log.Flush();
    }

    static int Main(string[] args)
    {
        // LiveSplit keeps the ASL component and its parser (Irony) in Components\
        string components = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Components");
        AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
        {
            string dll = Path.Combine(components, new System.Reflection.AssemblyName(e.Name).Name + ".dll");
            return File.Exists(dll) ? System.Reflection.Assembly.LoadFrom(dll) : null;
        };
        // the ASL compiler references LiveSplit.Core.dll by file name: run from the LiveSplit folder
        // (paths on the command line are made absolute first)
        for (int i = 0; i < Math.Min(2, args.Length); i++)
            args[i] = Path.GetFullPath(args[i]);
        Environment.CurrentDirectory = AppDomain.CurrentDomain.BaseDirectory;
        return Host(args);
    }

    // separate from Main: the LiveSplit types load when this is compiled, after the resolver is set
    static int Host(string[] args)
    {
        if (args.Length < 3)
        {
            Console.Error.WriteLine("AslHost.exe <script.asl> <out.log> <seconds> [segments]");
            return 2;
        }
        string asl = args[0];
        double seconds = double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture);
        int segments = args.Length > 3 ? int.Parse(args[3]) : 30;
        log = new StreamWriter(args[1], false, new System.Text.UTF8Encoding(false));
        timeBeginPeriod(1);

        var run = new Run(new StandardComparisonGeneratorsFactory());
        for (int i = 0; i < segments; i++)
            run.AddSegment("segment " + (i + 1));
        state = new LiveSplitState(run, null, null, null, new Settings());
        state.CurrentTimingMethod = TimingMethod.GameTime;
        state.OnStart += (s, e) => Log("TIMER start game=" + GameTime());
        state.OnSplit += (s, e) => Log(string.Format("TIMER split {0} game={1}", state.CurrentSplitIndex, GameTime()));
        state.OnReset += (s, e) => Log("TIMER reset");

        ASLScript script;
        try
        {
            script = ASLParser.Parse(File.ReadAllText(asl));
            script.RunStartup(state);
        }
        catch (Exception e)
        {
            Log("HOST script failed to load: " + e.ToString().Replace("\r", " ").Replace("\n", " "));
            log.Close();
            return 3;
        }
        Log(string.Format("HOST loaded {0}, {1} segments, refresh {2} Hz", Path.GetFileName(asl), segments, script.RefreshRate));

        double t0 = Now(), next = t0;
        bool? paused = null;
        TimerPhase phase = state.CurrentPhase;
        long ticks = 0;
        while (Now() - t0 < seconds)
        {
            try
            {
                script.Update(state);
            }
            catch (Exception e)
            {
                Log("HOST update error: " + e.Message.Replace("\r", " ").Replace("\n", " "));
            }
            ticks++;
            bool p = state.IsGameTimePaused;
            if (state.CurrentPhase == TimerPhase.Running && paused != p)
            {
                Log((p ? "TIMER paused" : "TIMER running") + " game=" + GameTime());
                paused = p;
            }
            if (state.CurrentPhase != phase)
            {
                if (state.CurrentPhase != TimerPhase.Running) paused = null;
                phase = state.CurrentPhase;
            }
            next += 1.0 / Math.Max(1.0, script.RefreshRate);
            double wait = next - Now();
            if (wait > 0) Thread.Sleep((int)(wait * 1000));
            else next = Now();
        }
        Log(string.Format("HOST done, {0} ticks, phase {1}, split index {2}, game={3}", ticks, state.CurrentPhase,
            state.CurrentSplitIndex, GameTime()));
        log.Close();
        return 0;
    }
}
