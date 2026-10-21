"""Runs one YAML test live and writes runs/<date>-<test>/result.json.

Steps (one key per list item):
  wait_s: 2.5                       sleep (frame-accurate waits arrive with item 3)
  key: esc | {key: w, hold: 1.5}    tap / hold a key (scan code)
  type: "fd_fov 90"                 type text as scan codes
  click: [x, y]                     left click at client coords
  focus: true                       bring the game window to the foreground
  screenshot: name                  checkpoint image (+ non-black luma recorded)
  assert_mem: {name, equals|not_equals|min|max, tolerance}
  sample_mem: {name, as: label}     record a value without asserting (for comparisons)
  assert_log: {contains: "text"}    FDNYCore.log must contain text
  console: "fd_fov 90"              open the game console, type, Enter, close (item 4+)
"""
import datetime
import json
import os
import re
import shutil
import time

import yaml

from . import addrtable, capture, game, inputs, pipe

CORE_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
RUNS_DIR = os.path.join(CORE_DIR, "runs")
SETUPS_DIR = os.path.join(CORE_DIR, "harness", "setups")
CORE_ASI = "FDNYCore.asi"


def core_version():
    """core/VERSION: the single source build.ps1 compiles into the core."""
    with open(os.path.join(CORE_DIR, "VERSION"), encoding="ascii") as f:
        return f.read().strip()


def deploy_core(game_dir, config):
    """config: 'Debug' | 'Release' | None (core removed for baseline runs)."""
    dst = os.path.join(game_dir, CORE_ASI)
    if config is None:
        if os.path.exists(dst):
            os.replace(dst, dst + ".off")  # ASI loader only loads *.asi
        return
    shutil.copy2(os.path.join(CORE_DIR, "build", config, CORE_ASI), dst)


class Run:
    def __init__(self, test_path, game_dir, core_config, tag=""):
        with open(test_path, encoding="utf-8") as f:
            self.test = yaml.safe_load(f)
        self.game_dir = game_dir
        self.core_config = core_config
        stamp = datetime.datetime.now().strftime("%Y-%m-%d-%H%M%S")
        suffix = f"-{tag}" if tag else ""
        self.out = os.path.join(RUNS_DIR, f"{stamp}-{self.test['name']}{suffix}")
        os.makedirs(self.out, exist_ok=True)
        self.results, self.samples = [], {}
        self.g = game.Game(game_dir)
        self.resolver = None

    def record(self, kind, ok, detail):
        self.results.append({"step": kind, "pass": bool(ok), "detail": detail})
        print(f"  [{'PASS' if ok else 'FAIL'}] {kind}: {detail}")
        return ok

    # --- steps -------------------------------------------------------------
    def client_point(self, x, y):
        cx, cy, _, _ = self.g.client_rect()
        return cx + x, cy + y

    def step(self, s):
        (kind, arg), = s.items()
        if kind == "wait_s":
            time.sleep(float(arg))
        elif kind == "focus":
            self.record("focus", inputs.focus(self.g.hwnd), "game window foreground")
        elif kind == "key":
            if isinstance(arg, dict):
                inputs.key_down(arg["key"])
                time.sleep(float(arg.get("hold", 0.05)))
                inputs.key_up(arg["key"])
            else:
                inputs.tap(arg)
        elif kind == "type":
            inputs.type_text(str(arg))
        elif kind == "click":
            inputs.click(*self.client_point(*arg))
        elif kind == "mouse_rel":
            inputs.mouse_move_rel(*arg)
        elif kind == "lclick":
            inputs.mouse_click_here()
        elif kind == "gclick":
            # Game-cursor click: the menus track their own cursor from DirectInput
            # deltas (1:1, clamped to the screen), so slam to the top-left corner,
            # then move by the target coords.
            inputs.mouse_move_rel(-3000, -3000)
            time.sleep(0.15)
            inputs.mouse_move_rel(*arg)
            time.sleep(0.25)
            inputs.mouse_click_here()
        elif kind == "screenshot":
            path, luma = capture.screenshot(self.g.client_rect(), os.path.join(self.out, f"{arg}.png"))
            self.samples[f"screenshot.{arg}.luma"] = round(luma, 1)
        elif kind == "assert_screen_nonblack":
            path, luma = capture.screenshot(self.g.client_rect(), os.path.join(self.out, f"{arg}.png"))
            self.record("assert_screen_nonblack", luma > 8, f"{arg} mean luma {luma:.1f}")
        elif kind == "repeat":
            for _ in range(int(arg["times"])):
                for sub in arg["steps"]:
                    if self.g.check_crash():
                        return
                    self.step(sub)
        elif kind == "pipe":
            self.samples.setdefault("pipe", []).append([arg, pipe.call(arg)])
        elif kind == "assert_frame_hooks":
            self.assert_frame_hooks(float(arg.get("seconds", 5)), int(arg.get("tolerance", 2)))
        elif kind in ("assert_same_pos", "assert_moved"):
            x, z = self.resolver.read("player.pos_x"), self.resolver.read("player.pos_z")
            x0, z0 = self.samples[arg["x"]], self.samples[arg["z"]]
            d = ((x - x0) ** 2 + (z - z0) ** 2) ** 0.5
            if kind == "assert_same_pos":
                self.record(kind, d <= float(arg["tolerance"]),
                            f"player moved {d:.1f} u (must be <= {arg['tolerance']})")
            else:
                self.record(kind, d >= float(arg["min"]), f"player moved {d:.1f} u (must be >= {arg['min']})")
        elif kind == "assert_hooks_idle":
            # Hook group switched off: its counters must not move while the game renders.
            group = arg.get("group", "frame")
            self.record("hook_off", pipe.call(f"hook {group} off") == "ok", f"{group} disabled")
            s0 = pipe.stats()
            time.sleep(float(arg.get("seconds", 2)))
            s1 = pipe.stats()
            d = {k: s1[k] - s0[k] for k in ("begin", "end", "events")}
            self.record("hooks_idle_while_off", all(v == 0 for v in d.values()),
                        f"{group} off for {arg.get('seconds', 2)}s: deltas {d}")
            self.record("hook_on", pipe.call(f"hook {group} on") == "ok", f"{group} re-enabled")
        elif kind == "toggle_hooks":
            fails = 0
            for _ in range(int(arg.get("times", 50))):
                fails += pipe.call(f"hook {arg['group']} off") != "ok"
                time.sleep(float(arg.get("gap_s", 0.05)))
                fails += pipe.call(f"hook {arg['group']} on") != "ok"
                time.sleep(float(arg.get("gap_s", 0.05)))
            self.record("toggle_hooks", fails == 0,
                        f"{arg.get('times', 50)}x off/on '{arg['group']}', {fails} failed command(s)")
        elif kind == "sample_addr":
            # Resolved address of each named entry (autosplitter-safety check).
            for name in arg:
                a = self.resolver.address(name)
                self.samples[f"addr.{name}"] = hex(a) if a is not None else None
        elif kind == "sample_pos":
            self.samples[f"pos.{arg}"] = [round(self.resolver.read(f"player.pos_{c}"), 2) for c in "xyz"]
            self.samples[f"t.{arg}"] = round(time.time() - self.g.started, 2)
        elif kind in ("assert_mem", "sample_mem"):
            val = self.resolver.read(arg["name"])
            if kind == "sample_mem":
                key = arg.get("as", arg["name"])
                if arg.get("append"):
                    self.samples.setdefault(key, []).append(val)
                else:
                    self.samples[key] = val
                return
            self.assert_value(arg, val)
        elif kind == "assert_log":
            with open(self.g.core_log, encoding="utf-8", errors="replace") as f:
                text = f.read()
            needle = arg["contains"].replace("{core_version}", core_version())
            self.record("assert_log", needle in text, f"contains {needle!r}")
        elif kind == "console":
            inputs.type_text("`")  # backtick opens the game console in typing mode (R3b)
            time.sleep(0.2)
            inputs.type_text(str(arg))
            inputs.tap("enter")
            time.sleep(0.1)
            inputs.type_text("`")
        else:
            raise ValueError(f"unknown step {kind}")

    def assert_frame_hooks(self, seconds, tolerance):
        """Independent frame count: g_gameClock's published elapsed-ms changes
        once per GameSession::RunFrame, and in a level each RunFrame renders
        one BeginFrame/EndFrame pair. Poll it fast and count changes, then
        compare with the core's hook counters over the same window."""
        s0 = pipe.stats()
        last, ticks = self.resolver.read("clock.elapsed_ms"), 0
        t_end = time.perf_counter() + seconds
        while time.perf_counter() < t_end:
            v = self.resolver.read("clock.elapsed_ms")
            if v != last:
                ticks += 1
                last = v
        s1 = pipe.stats()
        db, de, dv = (s1[k] - s0[k] for k in ("begin", "end", "events"))
        self.samples.setdefault("frame_windows", []).append(
            {"seconds": seconds, "clock_ticks": ticks, "begin": db, "end": de, "events": dv})
        self.record("frame_hooks_once_per_frame",
                    ticks > 0 and abs(db - ticks) <= tolerance and abs(de - ticks) <= tolerance
                    and abs(db - de) <= 1 and abs(dv - de) <= 1,
                    f"{seconds}s: clock ticks={ticks} BeginFrame={db} EndFrame={de} frame_end events={dv} "
                    f"(tolerance {tolerance} for window-edge races)")

    def assert_value(self, a, val):
        name, tol = a["name"], float(a.get("tolerance", 0))
        if val is None:
            return self.record("assert_mem", False, f"{name} unresolved (null pointer in chain)")
        if "equals" in a:
            exp = a["equals"]
            ok = abs(val - exp) <= tol if isinstance(val, (int, float)) and not isinstance(exp, str) else val == exp
            return self.record("assert_mem", ok, f"{name}={val!r} expected {exp!r} +/-{tol}")
        if "not_equals" in a:
            return self.record("assert_mem", val != a["not_equals"], f"{name}={val!r} != {a['not_equals']!r}")
        ok = ("min" not in a or val >= a["min"]) and ("max" not in a or val <= a["max"])
        return self.record("assert_mem", ok, f"{name}={val!r} in [{a.get('min')}, {a.get('max')}]")

    # --- lifecycle ---------------------------------------------------------
    def steps_with_setup(self):
        steps = []
        setup = self.test.get("setup")
        if setup:
            with open(os.path.join(SETUPS_DIR, f"{setup}.yaml"), encoding="utf-8") as f:
                steps += yaml.safe_load(f)["steps"]
        return steps + self.test.get("steps", [])

    def place_pre_files(self):
        """pre_files: [{src: <path rel. to core/>, dst: <name in game dir>}] copied
        before launch and removed after the run (e.g. Debug address-table overrides)."""
        placed = []
        for f in self.test.get("pre_files", []):
            dst = os.path.join(self.game_dir, f["dst"])
            backup = dst + ".fdh-backup"
            if os.path.exists(dst):  # a real game file: restore it afterwards
                shutil.copy2(dst, backup)
            shutil.copy2(os.path.join(CORE_DIR, f["src"]), dst)
            placed.append((dst, backup))
        return placed

    def execute(self):
        deploy_core(self.game_dir, self.core_config)
        placed = self.place_pre_files()
        try:
            return self._execute()
        finally:
            for dst, backup in placed:
                try:
                    if os.path.exists(backup):
                        os.replace(backup, dst)
                    else:
                        os.remove(dst)
                except OSError:
                    pass

    def _execute(self):
        expect = self.test.get("expect", {})
        crash = None
        rec = None
        print(f"== {self.test['name']} (core={self.core_config}) -> {self.out}")
        try:
            self.g.launch()
            crash = self.g.wait_window()
            if not crash:
                # Screenshots/video grab the screen region, and input goes to the
                # foreground window: the game must be on top.
                time.sleep(1.0)
                self.record("focus", inputs.focus(self.g.hwnd), "game window foreground at launch")
            if not crash and self.core_config:
                crash = self.g.wait_ready()
                self.record("core_ready", crash is None, crash or "ready line seen")
            if not crash:
                bases = self.g.attach()
                self.samples["module.exe"] = hex(bases.get("exe", 0))
                self.samples["module.genesis"] = hex(bases.get("genesis", 0))
                exe_sha = game.sha256(self.g.exe)
                gen_sha = game.sha256(os.path.join(self.game_dir, "Genesis.dll"))
                vname, version = addrtable.select_version(addrtable.load(), exe_sha, gen_sha)
                self.record("address_table_version", version is not None, vname or "no matching exe+Genesis hash")
                if version:
                    self.resolver = addrtable.Resolver(self.g.pm, version, bases)
                if self.test.get("record", True):
                    rec = capture.Recorder(os.path.join(self.out, "run.mp4"), self.g.client_rect())
                    err = rec.start()
                    if err:
                        self.record("record", False, err)
                        rec = None
                for s in self.steps_with_setup():
                    crash = self.g.check_crash()
                    if crash:
                        break
                    self.step(s)
                crash = crash or self.g.check_crash()
        except Exception as e:  # harness bug or unreadable memory: fail the run, keep evidence
            crash = crash or f"harness exception: {e!r}"
        finally:
            if rec:
                rec.stop()
            try:
                if self.g.hwnd and not crash:
                    capture.screenshot(self.g.client_rect(), os.path.join(self.out, "final.png"))
            except Exception:
                pass
            self.g.kill()

        if expect.get("no_crash", True):
            self.record("no_crash", crash is None, crash or "no crash, hang or dialog")
        if self.core_config:
            errors = self.collect_log()
            limit = expect.get("log_errors", 0)
            self.record("log_errors", len(errors) <= limit, f"{len(errors)} ERROR line(s), limit {limit}")
        passed = all(r["pass"] for r in self.results)
        result = {"test": self.test["name"], "core": self.core_config, "pass": passed,
                  "crash": crash, "assertions": self.results, "samples": self.samples,
                  "video": os.path.join(self.out, "run.mp4") if rec else None}
        with open(os.path.join(self.out, "result.json"), "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2)
        print(f"== {'PASS' if passed else 'FAIL'} {self.test['name']}")
        return result

    def collect_log(self):
        try:
            shutil.copy2(self.g.core_log, os.path.join(self.out, "FDNYCore.log"))
            with open(self.g.core_log, encoding="utf-8", errors="replace") as f:
                return [l for l in f if re.search(r"\] \[ERROR\] ", l)]
        except OSError:
            return ["FDNYCore.log missing"]
