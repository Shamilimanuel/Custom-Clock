"""Custom Clock: time, system stats, media and Genshin resin in one small widget.

- Borderless, always on top, sits in the taskbar at the bottom right.
- Double-click = refresh resin now. Right-click = menu (refresh / quit).
- Cookies live in %APPDATA%\\CustomClock:
    %APPDATA%\\CustomClock\\cookies.json
- No web server, nothing listens on your network.
"""
import asyncio
import ctypes
import ctypes.wintypes
import json
import os
import subprocess
import threading
import time
import tkinter as tk
from datetime import datetime
from pathlib import Path

import psutil
import genshin

# Make Windows give us real pixels (otherwise positions are off on scaled displays).
ctypes.windll.shcore.SetProcessDpiAwareness(1)

# ---------- settings (change these to experiment) ----------
DATA_DIR = Path(os.environ.get("APPDATA", Path.home())) / "CustomClock"
COOKIES_FILE = DATA_DIR / "cookies.json"
RESIN_REFRESH_SECONDS = 300
MEDIA_MAX_LENGTH = 80  # the mod shortens the final line itself (see its Settings tab)

BG = "#06141a"
CYAN = "#8ff3ff"
SOFT = "#e6fbfb"
DIM = "#6fa9b3"

NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)

# Shared data. Background threads write here, the UI thread only reads.
state = {
    "cpu": 0,
    "ram": 0,
    "gpu": None,
    "media": "No media",
    "resin": "Resin ...",
    "resin_sub": "loading",
}
resin_now = threading.Event()


# ---------- system stats ----------
def poll_system():
    while True:
        state["cpu"] = round(psutil.cpu_percent(interval=1))
        state["ram"] = round(psutil.virtual_memory().percent)


def read_gpu():
    """NVIDIA first (fast). Otherwise Windows performance counters (any GPU)."""
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=3, creationflags=NO_WINDOW,
        )
        return int(out.stdout.strip().splitlines()[0])
    except Exception:
        pass
    try:
        command = (
            "(Get-Counter '\\GPU Engine(*engtype_3D)\\Utilization Percentage')"
            ".CounterSamples | Measure-Object -Property CookedValue -Sum "
            "| Select-Object -ExpandProperty Sum"
        )
        out = subprocess.run(
            ["powershell", "-NoProfile", "-Command", command],
            capture_output=True, text=True, timeout=8, creationflags=NO_WINDOW,
        )
        return min(100, round(float(out.stdout.strip().replace(",", "."))))
    except Exception:
        return None


def poll_gpu():
    while True:
        state["gpu"] = read_gpu()
        time.sleep(5)


# ---------- media ----------
def shorten(text, limit):
    return text if len(text) <= limit else text[: limit - 1] + "…"


async def media_loop():
    try:
        from winrt.windows.media.control import (
            GlobalSystemMediaTransportControlsSessionManager as Manager,
            GlobalSystemMediaTransportControlsSessionPlaybackStatus as Status,
        )
    except ImportError:
        state["media"] = "Media: pip install winrt packages"
        return

    while True:
        try:
            manager = await Manager.request_async()
            session = manager.get_current_session()
            if session is None:
                state["media"] = "No media"
            else:
                info = await session.try_get_media_properties_async()
                text = f"{info.artist} - {info.title}" if info.artist else info.title
                playing = session.get_playback_info().playback_status == Status.PLAYING
                state["media"] = shorten(text, MEDIA_MAX_LENGTH) + ("" if playing else "  (paused)")
        except Exception as error:
            print("Media error:", error)
            state["media"] = "No media"
        await asyncio.sleep(3)


def poll_media():
    asyncio.run(media_loop())


# ---------- resin ----------
def format_time(td):
    total = int(td.total_seconds())
    if total <= 0:
        return "full"
    hours, rest = divmod(total, 3600)
    return f"full in {hours}h {rest // 60:02d}m"


async def fetch_notes():
    # utf-8-sig also accepts files that start with a BOM (Notepad and PowerShell can add one)
    cookies = json.loads(COOKIES_FILE.read_text(encoding="utf-8-sig"))
    client = genshin.Client(cookies, game=genshin.Game.GENSHIN)
    return await client.get_genshin_notes()


def poll_resin():
    while True:
        try:
            # A hung request (sleep, network change) would freeze resin forever, so give up after 30 s.
            notes = asyncio.run(asyncio.wait_for(fetch_notes(), timeout=30))
            state["resin"] = f"Resin {notes.current_resin}/{notes.max_resin}"
            state["resin_sub"] = format_time(notes.remaining_resin_recovery_time)
        except FileNotFoundError:
            state["resin"] = "Resin: no cookies.json"
            state["resin_sub"] = str(DATA_DIR)
        except Exception as error:
            print("Resin error:", type(error).__name__)
            state["resin"] = "Resin error"
            state["resin_sub"] = (str(error) or type(error).__name__)[:40]
        resin_now.wait(RESIN_REFRESH_SECONDS)
        resin_now.clear()


# ---------- window ----------
LINE2_MODES = ("media", "stats", "resin")   # what line 2 cycles through
LINE2_CYCLE_SECONDS = 4
LINE_WIDTH = 34                              # characters per line (fixed so the widget never jumps)
TASKBAR_MARGIN_RIGHT = 8                     # pixels from the right screen edge


def get_taskbar_height(default=48):
    """Height of the Windows taskbar in pixels. Works even when it is set to auto-hide."""
    rect = ctypes.wintypes.RECT()
    hwnd = ctypes.windll.user32.FindWindowW("Shell_TrayWnd", None)
    if hwnd and ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        return max(rect.bottom - rect.top, default)
    return default


class ClockWidget:
    def __init__(self):
        self.root = tk.Tk()
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)
        self.root.configure(bg=BG)

        self.line1 = self.make_label(CYAN)
        self.line2 = self.make_label(SOFT)

        self.menu = tk.Menu(self.root, tearoff=0)
        self.menu.add_command(label="Refresh resin now", command=resin_now.set)
        self.menu.add_command(label="Quit", command=self.root.destroy)
        for widget in (self.root, self.line1, self.line2):
            widget.bind("<Double-Button-1>", lambda e: resin_now.set())
            widget.bind("<Button-3>", lambda e: self.menu.tk_popup(e.x_root, e.y_root))

        self.place_in_taskbar()
        self.tick()

    def make_label(self, color):
        label = tk.Label(self.root, text="", bg=BG, fg=color, font=("Segoe UI", 9),
                         width=LINE_WIDTH, anchor="e", padx=8)
        label.pack(fill="x")
        return label

    def place_in_taskbar(self):
        """Bottom right, vertically centred inside the taskbar strip."""
        self.root.update_idletasks()
        w, h = self.root.winfo_reqwidth(), self.root.winfo_reqheight()
        screen_w, screen_h = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        taskbar_h = max(get_taskbar_height(), h)
        x = screen_w - w - TASKBAR_MARGIN_RIGHT
        y = screen_h - taskbar_h + (taskbar_h - h) // 2
        self.root.geometry(f"+{x}+{y}")

    def line2_text(self):
        mode = LINE2_MODES[int(time.time() // LINE2_CYCLE_SECONDS) % len(LINE2_MODES)]
        if mode == "media":
            text = "♪ " + state["media"]
        elif mode == "stats":
            gpu = "n/a" if state["gpu"] is None else f"{state['gpu']}%"
            text = f"CPU {state['cpu']}%  GPU {gpu}  RAM {state['ram']}%"
        else:
            sub = state["resin_sub"]
            text = f"{state['resin']} · {sub}" if sub.startswith("full") else state["resin"]
        return shorten(text, LINE_WIDTH)

    def tick(self):
        now = datetime.now()
        # %#d = day without leading zero on Windows
        self.line1.config(text=now.strftime("%a, %b %#d %Y | %H:%M:%S"))
        self.line2.config(text=self.line2_text())
        self.root.lift()  # stay above the taskbar
        self.root.after(250, self.tick)


if __name__ == "__main__":
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    for target in (poll_system, poll_gpu, poll_media, poll_resin):
        threading.Thread(target=target, daemon=True).start()
    ClockWidget().root.mainloop()
