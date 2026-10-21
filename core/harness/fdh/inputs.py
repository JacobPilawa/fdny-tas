"""Scan-code keyboard / mouse input via SendInput, the way a player types.

The game reads DirectInput (scan codes) AND window messages, so keys are sent
with KEYEVENTF_SCANCODE; Windows synthesizes the WM_KEYDOWN/WM_CHAR side.
Input goes to the foreground window: the game must have focus.
"""
import ctypes
import ctypes.wintypes as wt
import time

user32 = ctypes.windll.user32

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP, KEYEVENTF_SCANCODE = 0x1, 0x2, 0x8
MOUSEEVENTF_MOVE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x1, 0x2, 0x4
MOUSEEVENTF_ABSOLUTE = 0x8000


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class _U(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT), ("pad", ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _U)]


# DIK scan codes for the keys tests use; (scan, extended).
SCAN = {
    "esc": (0x01, False), "enter": (0x1C, False), "tab": (0x0F, False),
    "space": (0x39, False), "backspace": (0x0E, False), "grave": (0x29, False),
    "lshift": (0x2A, False), "lctrl": (0x1D, False), "alt": (0x38, False),
    "up": (0x48, True), "down": (0x50, True), "left": (0x4B, True), "right": (0x4D, True),
    "minus": (0x0C, False), "equals": (0x0D, False), "period": (0x34, False),
    "slash": (0x35, False), "semicolon": (0x27, False), "quote": (0x28, False),
    "comma": (0x33, False), "f11": (0x57, False), "f12": (0x58, False),
}
for i in range(10):
    SCAN[f"f{i + 1}"] = (0x3B + i, False)
for i, c in enumerate("1234567890"):
    SCAN[c] = (0x02 + i, False)
for row, start in (("qwertyuiop", 0x10), ("asdfghjkl", 0x1E), ("zxcvbnm", 0x2C)):
    for i, c in enumerate(row):
        SCAN[c] = (start + i, False)

SHIFTED = {"_": "minus", "~": "grave", ":": "semicolon", '"': "quote", "+": "equals"}
UNSHIFTED = {"`": "grave", "-": "minus", "=": "equals", ".": "period", "/": "slash",
             ";": "semicolon", "'": "quote", ",": "comma"}


def _send(*inputs):
    arr = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), arr, ctypes.sizeof(INPUT))


def _key(scan, ext, up):
    flags = KEYEVENTF_SCANCODE | (KEYEVENTF_EXTENDEDKEY if ext else 0) | (KEYEVENTF_KEYUP if up else 0)
    return INPUT(INPUT_KEYBOARD, _U(ki=KEYBDINPUT(0, scan, flags, 0, 0)))


def key_down(name):
    _send(_key(*SCAN[name], False))


def key_up(name):
    _send(_key(*SCAN[name], True))


def tap(name, hold=0.05):
    key_down(name)
    time.sleep(hold)
    key_up(name)
    time.sleep(0.03)


def type_text(text, per_key=0.04):
    for ch in text:
        if ch == " ":
            tap("space", per_key)
        elif ch in SHIFTED or ch.isupper():
            key_down("lshift")
            tap(SHIFTED.get(ch, ch.lower()), per_key)
            key_up("lshift")
        else:
            tap(UNSHIFTED.get(ch, ch), per_key)


def click(x, y):
    """Left click at screen coords (absolute, normalized to the primary screen)."""
    sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
    ax, ay = int(x * 65535 / (sw - 1)), int(y * 65535 / (sh - 1))
    move = INPUT(INPUT_MOUSE, _U(mi=MOUSEINPUT(ax, ay, 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, 0, 0)))
    down = INPUT(INPUT_MOUSE, _U(mi=MOUSEINPUT(ax, ay, 0, MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE, 0, 0)))
    up = INPUT(INPUT_MOUSE, _U(mi=MOUSEINPUT(ax, ay, 0, MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE, 0, 0)))
    _send(move)
    time.sleep(0.05)
    _send(down)
    time.sleep(0.05)
    _send(up)


def mouse_move_rel(dx, dy, steps=10):
    """Relative motion, which DirectInput's mouse device sees as deltas."""
    for i in range(steps):
        sx = dx * (i + 1) // steps - dx * i // steps
        sy = dy * (i + 1) // steps - dy * i // steps
        _send(INPUT(INPUT_MOUSE, _U(mi=MOUSEINPUT(sx, sy, 0, MOUSEEVENTF_MOVE, 0, 0))))
        time.sleep(0.02)


def mouse_click_here():
    _send(INPUT(INPUT_MOUSE, _U(mi=MOUSEINPUT(0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0, 0))))
    time.sleep(0.08)
    _send(INPUT(INPUT_MOUSE, _U(mi=MOUSEINPUT(0, 0, 0, MOUSEEVENTF_LEFTUP, 0, 0))))


def alt_tab():
    """A real Alt+Tab, as a player presses it (switches to the previous window)."""
    key_down("alt")
    time.sleep(0.05)
    tap("tab", 0.05)
    time.sleep(0.05)
    key_up("alt")


def cursor_pos():
    pt = wt.POINT()
    user32.GetCursorPos(ctypes.byref(pt))
    return pt.x, pt.y


def set_cursor(x, y):
    user32.SetCursorPos(int(x), int(y))


def foreground():
    return user32.GetForegroundWindow()


def focus(hwnd):
    user32.SetForegroundWindow(hwnd)
    return user32.GetForegroundWindow() == hwnd
