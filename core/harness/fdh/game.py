"""Launch, watch and kill the game. VM-only.

Crash = process exit, a hang > HANG_S (window stops answering messages), or
any top-level dialog (#32770) owned by the game process -- a FATAL ERROR box
looks "non-black" to a screenshot check, so dialogs are detected explicitly.
"""
import ctypes
import ctypes.wintypes as wt
import hashlib
import os
import subprocess
import time

import pymem

user32 = ctypes.windll.user32
WINDOW_TITLE = "FDNY Firefighter"
EXE_NAME = "FDNYFirefighter.exe"
HANG_S = 10.0
SMTO_ABORTIFHUNG = 0x0002

_EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def _window_pid(hwnd):
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


def _class_name(hwnd):
    buf = ctypes.create_unicode_buffer(64)
    user32.GetClassNameW(hwnd, buf, 64)
    return buf.value


def _title(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(hwnd, buf, 256)
    return buf.value


def windows_of(pid):
    found = []

    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd) and _window_pid(hwnd) == pid:
            found.append(hwnd)
        return True

    user32.EnumWindows(_EnumProc(cb), 0)
    return found


def dialog_text(hwnd):
    """Title + static text of a #32770 dialog, for the crash report."""
    parts = [_title(hwnd)]

    def cb(child, _):
        if _class_name(child) == "Static" and _title(child):
            parts.append(_title(child))
        return True

    user32.EnumChildWindows(hwnd, _EnumProc(cb), 0)
    return " | ".join(p for p in parts if p)


class Game:
    def __init__(self, game_dir):
        self.dir = game_dir
        self.exe = os.path.join(game_dir, EXE_NAME)
        self.core_log = os.path.join(game_dir, "FDNYCore.log")
        self.proc = None
        self.hwnd = 0
        self.pm = None

    def launch(self):
        self.hwnd = 0  # a stale handle from a previous launch would read as "hung"
        self.proc = subprocess.Popen([self.exe], cwd=self.dir)
        self.started = time.time()
        return self.proc.pid

    def wait_window(self, timeout=30):
        t0 = time.time()
        while time.time() - t0 < timeout:
            crash = self.check_crash()
            if crash:
                return crash
            for h in windows_of(self.proc.pid):
                if _title(h) == WINDOW_TITLE:
                    self.hwnd = h
                    return None
            time.sleep(0.2)
        return f"no game window within {timeout}s"

    def wait_ready(self, timeout=30, line="FDNYCore ready"):
        """Wait for the core's ready line in a log written after launch."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            crash = self.check_crash()
            if crash:
                return crash
            try:
                if os.path.getmtime(self.core_log) >= self.started - 1:
                    with open(self.core_log, encoding="utf-8", errors="replace") as f:
                        if line in f.read():
                            return None
            except OSError:
                pass
            time.sleep(0.2)
        return f"no '{line}' in FDNYCore.log within {timeout}s"

    def check_crash(self):
        """None if healthy, else a one-line reason."""
        if self.proc.poll() is not None:
            return f"process exited (code {self.proc.returncode})"
        for h in windows_of(self.proc.pid):
            if _class_name(h) == "#32770":
                return f"dialog: {dialog_text(h)}"
        if self.hwnd:
            res = wt.DWORD()
            ok = user32.SendMessageTimeoutW(self.hwnd, 0, 0, 0, SMTO_ABORTIFHUNG,
                                            int(HANG_S * 1000), ctypes.byref(res))
            if not ok:
                return f"window hung > {HANG_S:.0f}s"
        return None

    def attach(self):
        self.pm = pymem.Pymem(self.proc.pid)
        bases = {}
        for m in self.pm.list_modules():
            n = m.name.lower()
            if n == EXE_NAME.lower():
                bases["exe"] = m.lpBaseOfDll
            elif n == "genesis.dll":
                bases["genesis"] = m.lpBaseOfDll
        return bases

    def client_rect(self):
        r = wt.RECT()
        user32.GetClientRect(self.hwnd, ctypes.byref(r))
        pt = wt.POINT(0, 0)
        user32.ClientToScreen(self.hwnd, ctypes.byref(pt))
        return pt.x, pt.y, r.right - r.left, r.bottom - r.top

    def kill(self):
        if self.pm:
            try:
                self.pm.close_process()
            except Exception:
                pass
            self.pm = None
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=10)
