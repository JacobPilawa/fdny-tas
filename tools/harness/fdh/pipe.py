"""Client for the core's Debug-only test pipe (\\\\.\\pipe\\FDNYCore)."""
import time

PIPE = r"\\.\pipe\FDNYCore"


def call(cmd, retries=20):
    """Send one request line, return the response line."""
    last = None
    for _ in range(retries):
        try:
            with open(PIPE, "r+b", buffering=0) as p:
                p.write(cmd.encode("ascii"))
                return p.read(8192).decode("ascii", "replace")
        except OSError as e:  # server busy between clients
            last = e
            time.sleep(0.05)
    raise OSError(f"test pipe unavailable: {last}")


def snapshot():
    """The core's state snapshot as a dict of strings."""
    return dict(kv.split("=", 1) for kv in call("snapshot").split() if "=" in kv)


def stats():
    """Parse 'begin=.. end=.. events=.. levels=.. toggles=.. frame=on' into a dict."""
    out = {}
    for kv in call("stats").split():
        k, v = kv.split("=", 1)
        out[k] = int(v) if v.lstrip("-").isdigit() else v
    return out
