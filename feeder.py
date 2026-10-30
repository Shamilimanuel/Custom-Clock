"""Feeds the Windhawk mod: writes the raw values to %APPDATA%\\CustomClock\\data.txt.

One "key=value" per line. The mod decides how they are shown (see its Settings tab).
Run it with pythonw.exe (no window) or python.exe (to see errors).
Reuses the data collectors from main.py. No network port is opened.
"""
import os
import threading
import time

import main  # importing does not start the widget; that only happens in main.py's __main__

OUT_FILE = main.DATA_DIR / "data.txt"


def snapshot():
    """Current values as a dict of text. Newlines are removed so each stays on one line."""
    state = main.state
    gpu = "n/a" if state["gpu"] is None else str(state["gpu"])
    values = {
        "cpu": str(state["cpu"]),
        "gpu": gpu,
        "ram": str(state["ram"]),
        "media": state["media"],
        "resin": state["resin"],
        "resin_time": state["resin_sub"],
    }
    return {key: str(value).replace("\n", " ").replace("\r", " ") for key, value in values.items()}


def write_atomically(values):
    """Write to a temp file first, then swap it in, so the mod never reads half a file."""
    text = "".join(f"{key}={value}\n" for key, value in values.items())
    temp = OUT_FILE.with_suffix(".tmp")
    temp.write_text(text, encoding="utf-8")
    os.replace(temp, OUT_FILE)


def another_copy_is_running():
    """Windows lets only one program own a named mutex, so a second copy finds it taken."""
    import ctypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateMutexW.restype = ctypes.c_void_p
    another_copy_is_running.lock = kernel32.CreateMutexW(None, False, "Local\\CustomClockFeeder")  # keep it alive
    return ctypes.get_last_error() == 183  # ERROR_ALREADY_EXISTS


if __name__ == "__main__":
    if another_copy_is_running():
        raise SystemExit  # already running (for example it started with Windows)
    main.DATA_DIR.mkdir(parents=True, exist_ok=True)
    for target in (main.poll_system, main.poll_gpu, main.poll_media, main.poll_resin):
        threading.Thread(target=target, daemon=True).start()
    while True:
        try:
            write_atomically(snapshot())
        except OSError:
            pass  # file briefly locked by another program: skip this second, try again at the next
        time.sleep(1)
