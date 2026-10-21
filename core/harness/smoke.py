"""Smoke launch: start the game, wait for its window, screenshot it, kill it.

VM-only. Usage: python -I smoke.py <game_dir> <out_dir> [wait_s]
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

import mss
from PIL import Image, ImageStat

WINDOW_TITLE = "FDNY Firefighter"
user32 = ctypes.windll.user32


def window_rect(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def main():
    game_dir, out_dir = sys.argv[1], sys.argv[2]
    wait_s = float(sys.argv[3]) if len(sys.argv) > 3 else 25
    os.makedirs(out_dir, exist_ok=True)
    proc = subprocess.Popen([os.path.join(game_dir, "FDNYFirefighter.exe")], cwd=game_dir)
    hwnd, t0 = 0, time.time()
    while time.time() - t0 < wait_s:
        if proc.poll() is not None:
            print(f"EXITED code={proc.returncode} after {time.time() - t0:.1f}s")
            return 1
        hwnd = hwnd or user32.FindWindowA(None, WINDOW_TITLE.encode())
        time.sleep(0.5)
    if not hwnd:
        print("NO WINDOW")
    else:
        l, t, r, b = window_rect(hwnd)
        print(f"window rect {l},{t},{r},{b} ({r - l}x{b - t})")
        with mss.mss() as s:
            shot = s.grab({"left": l, "top": t, "width": r - l, "height": b - t})
        img = Image.frombytes("RGB", shot.size, shot.rgb)
        path = os.path.join(out_dir, "smoke.png")
        img.save(path)
        mean = sum(ImageStat.Stat(img).mean) / 3
        print(f"screenshot {path} mean_luma={mean:.1f} {'NON-BLACK' if mean > 8 else 'BLACK'}")
    proc.kill()
    proc.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
