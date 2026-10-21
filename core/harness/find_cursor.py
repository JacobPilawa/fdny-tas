"""Locate the game's yellow arrow cursor in screenshots (top-left of its bbox).

python -I find_cursor.py a.png [b.png ...]
"""
import sys

from PIL import Image


def find(path):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    px = img.load()
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r > 200 and g > 180 and b < 60:  # cursor yellow
                xs.append(x)
                ys.append(y)
    return (min(xs), min(ys), len(xs)) if xs else None


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(p, find(p))
