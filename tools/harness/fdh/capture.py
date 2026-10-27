"""Screenshots (mss) and whole-run video (ffmpeg gdigrab). VM-only."""
import os
import subprocess

import mss
from PIL import Image, ImageStat

FFMPEG = os.environ.get("FDH_FFMPEG", "")


def find_ffmpeg():
    if FFMPEG and os.path.exists(FFMPEG):
        return FFMPEG
    root = os.path.join(os.path.dirname(__file__), "..", "..", "..", "environment", "local", "tools", "ffmpeg")
    for dirpath, _, files in os.walk(root):
        if "ffmpeg.exe" in files:
            return os.path.join(dirpath, "ffmpeg.exe")
    return None


def screenshot(rect, path):
    """rect = (x, y, w, h) in screen coords. Returns (path, mean luma)."""
    x, y, w, h = rect
    with mss.MSS() as s:
        shot = s.grab({"left": x, "top": y, "width": w, "height": h})
    img = Image.frombytes("RGB", shot.size, shot.rgb)
    img.save(path)
    return path, sum(ImageStat.Stat(img).mean) / 3


class Recorder:
    def __init__(self, path, rect, fps=30):
        self.path, self.rect, self.fps = path, rect, fps
        self.proc = None

    def start(self):
        ff = find_ffmpeg()
        if not ff:
            return "ffmpeg not installed (environment/scripts/get_ffmpeg.ps1)"
        x, y, w, h = self.rect
        w, h = w - w % 2, h - h % 2  # yuv420p needs even dimensions
        self.proc = subprocess.Popen(
            [ff, "-hide_banner", "-loglevel", "error", "-y", "-f", "gdigrab",
             "-framerate", str(self.fps), "-offset_x", str(x), "-offset_y", str(y),
             "-video_size", f"{w}x{h}", "-draw_mouse", "1", "-i", "desktop",
             "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", self.path],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW)
        return None

    def stop(self):
        if not self.proc:
            return
        try:
            self.proc.communicate(b"q", timeout=15)  # graceful stop finalizes the mp4
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.proc = None
