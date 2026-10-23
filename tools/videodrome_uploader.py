#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VIDEODROME uploader: flash VIDEODROME onto an M-VAVE FM-1 or SMK-37 (Pro, Elite, MKE-P37) over USB.

A desktop window (Tk, in Python itself) over tools/fm1_install.py, the same tested update as the browser installer:
it finds the keyboard on its USB-MIDI port, picks the package for that model from releases/ (or the one you choose),
refuses a package for another keyboard before anything is sent, writes it (step 1: the running firmware checks the
package and starts the update loader; step 2: the loader writes it), and checks the identity the keyboard reports
once it restarts. An update that was cut off (cable pulled, computer asleep) is finished: the keyboard waits in its
update mode, and the next run writes it again.

  videodrome_uploader.py                    the window
  videodrome_uploader.py --cli [--yes]      the same in a terminal
  videodrome_uploader.py --list             the packages it knows and the keyboards they are for
  options: --package FILE.fwsc (instead of releases/), --port NAME (part of a MIDI port's name)

Needs Python 3.8+ with mido and python-rtmidi (pip install mido python-rtmidi); the launchers next to this repo's
README (VIDEODROME-UPLOADER.bat on Windows, VIDEODROME-UPLOADER.command on macOS, videodrome-uploader.sh on Linux)
install them when they are missing.

Before the first install: save a backup (the editor: Projects > Backup). On an SMK-37 read SMK37.md first."""
import argparse
import queue
import re
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fm1_install as I  # noqa: E402

ROOT = HERE.parent
RELEASES = ROOT / "releases"
PKG_RE = re.compile(r"videodrome-(\d+)\.(\d+)-(fm1|smk37|smk37-elite|smk37-mke)\.fwsc$", re.I)
# a keyboard's model (its identity before "_") -> the package's board name in releases/
BOARD_OF_MODEL = {"FM-1": "fm1", "SMK-37 Pro": "smk37", "SMK-37 Elite": "smk37-elite", "MKE-P37": "smk37-mke"}
MODEL_OF_BOARD = {v: k for k, v in BOARD_OF_MODEL.items()}
HINTS = {   # what to do next, after each kind of error (fm1_install.InstallError codes)
    "nomidi": "The computer's MIDI system did not open. Linux: sudo modprobe snd-seq (and be in the audio group); "
              "elsewhere restart the computer. Or use VIDEODROME-UPLOADER.html in Chrome or Edge.",
    "notfound": "Plug the keyboard in with a USB data cable (not a charge-only one), switch it on, and close every "
                "other program that uses MIDI (the browser editor, a DAW, M-UPGRADE). Then Find again.",
    "lost": "The USB connection was lost. Plug it back in and press Install again: the keyboard waits in its update "
            "mode until the write is finished.",
    "stopped": "The keyboard stopped answering. Press Install again; if it still stops, replug the cable first.",
    "noloader": "The update mode did not appear. Replug the USB cable and press Install again.",
    "noreturn": "Written, but the keyboard did not come back: switch it off and on.",
    "mismatch": "Written, but the keyboard reports another firmware. Switch it off and on, then Find to check.",
    "model": "This package is for another keyboard. Choose the package for this one.",
    "board": "This package is laid out for another keyboard: nothing was written.",
    "foreign": "The keyboard is in another firmware's update mode: finish that update with its own updater "
               "(M-VAVE's M-UPGRADE), then come back.",
    "badpkg": "The package file is damaged or not a VIDEODROME / SLOOP package: download it again.",
    "usage": "Install the two Python modules: pip install mido python-rtmidi (the launchers do it for you).",
}


# ------------------------------------------------------------------- the packages ---

def releases(folder=None):
    """-> [(path, (major, minor), board)], newest first (folder: releases/ by default)"""
    folder = RELEASES if folder is None else folder
    out = []
    for p in sorted(Path(folder).glob("*.fwsc")) if Path(folder).is_dir() else []:
        m = PKG_RE.search(p.name)
        if m:
            out.append((p, (int(m.group(1)), int(m.group(2))), m.group(3).lower()))
    return sorted(out, key=lambda r: r[1], reverse=True)


def package_for(identity, folder=None):
    """the newest package in folder for the keyboard that reports identity (running or in update mode), or None"""
    board = BOARD_OF_MODEL.get(I.model_of(identity))
    for p, _, b in releases(folder):
        if b == board:
            return p
    return None


# ------------------------------------------------------------------- the core ---

class Uploader:
    """the steps, without any window: find, choose, check, install (the window and --cli drive it)"""

    def __init__(self, backend, port=None, folder=None):
        self.up = I.Updater(backend, port)
        self.folder = RELEASES if folder is None else folder
        self.dev = None
        self.package = None                  # (path, product, image)

    def close(self):
        if self.dev:
            self.dev.link.close()
            self.dev = None

    def find(self):
        """the keyboard: -> its identity text, or raises InstallError("notfound")"""
        self.close()
        try:
            self.dev = self.up.find()
        except Exception as e:                       # the computer's MIDI system itself (ALSA, CoreMIDI, WinMM)
            raise I.InstallError("nomidi", f"the computer's MIDI system did not open: {e}") from e
        if not self.dev:
            raise I.not_found(self.up)
        return self.dev.id.text

    @property
    def in_update_mode(self):
        return bool(self.dev and self.dev.id.loader)

    def choose(self, path=None):
        """the package: path, or the newest one in releases/ for the keyboard found; -> its identity text"""
        if path is None:
            if not self.dev:
                raise I.InstallError("notfound", "find the keyboard first")
            path = package_for(self.dev.id.text, self.folder)
            if path is None:
                raise I.InstallError("badpkg", f"no VIDEODROME package for {I.model_of(self.dev.id.text)} in "
                                               f"{self.folder}: choose one")
        product, image = I.load_package(str(path), False)
        self.package = (Path(path), product, image)
        return product

    def check(self):
        """nothing is sent: the package fits the keyboard? raises InstallError if not"""
        if not self.dev or not self.package:
            raise I.InstallError("usage", "find the keyboard and choose a package first")
        _, product, image = self.package
        refusal = I.board_refusal(I.model_of(self.dev.id.text), image, False)
        if refusal:
            raise refusal
        if not I.same_keyboard(self.dev.id.text, product):
            raise I.InstallError("model", f"the keyboard is {I.model_of(self.dev.id.text)}, the package is for "
                                          f"{I.model_of(product)}")

    def install(self, step=lambda *a: None):
        """the write; the keyboard is found again first (the port opened by find() may have gone stale while the
        window waited). -> the identity the keyboard reports after its restart"""
        path, product, image = self.package
        self.find()
        self.check()
        dev, self.dev = self.dev, None       # (install() closes its links)
        return self.up.install(dev, image, product, step)


def describe(identity):
    """'FM-1, running VIDEODROME / SLOOP (FM-1_900)' for a person"""
    model = I.model_of(identity)
    if identity.lower().startswith("ota-"):
        return f"{model}, in update mode (an update was not finished: Install finishes it)"
    num = identity.split("_")[-1]
    what = "the official M-VAVE firmware" if not num.startswith("9") else "a Felucca-family firmware (SLOOP / VIDEODROME)"
    return f"{model}, running {what} ({identity})"


# ------------------------------------------------------------------- the terminal ---

def run_cli(a, backend, out=sys.stdout, ask=I.ask_tty):
    u = Uploader(backend, a.port)
    try:
        ident = u.find()
        print(f"keyboard: {describe(ident)}  [port {u.dev.name}]", file=out)
        product = u.choose(a.package)
        print(f"package:  {u.package[0].name}  ({product})", file=out)
        u.check()
        if not a.yes and not ask("Install VIDEODROME? Saved a backup? Do not unplug while writing. [y/N] "):
            print("cancelled: nothing was written", file=out)
            return 1
        prog = I.Progress(out)
        try:
            got = u.install(prog)
        finally:
            prog.end()
        print(f"installed: {got}", file=out)
        return 0
    except I.InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        if e.code in HINTS:
            print(HINTS[e.code], file=sys.stderr)
        return I.EXIT.get(e.code, 1)
    finally:
        u.close()


# ------------------------------------------------------------------- the window ---

def run_gui(a, backend):
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    # CIVIC-TV, CH 83 (VIDEODROME 1.1): the firmware's CH 83 palette and the pages' skin (web/vd_skin.css): dead-air
    # blue, CG white, the chyron's yellow; the wordmark in the title card's orange
    BG, PANEL, ORANGE, GHOST_R, GHOST_C, OSD, FG, DIM = ("#06082a", "#0b1038", "#ff6818", "#c81428", "#0096aa",
                                                         "#ffdc28", "#f0f0e8", "#7c94ce")
    BARS = ["#b4b4b4", "#bebe00", "#00bebe", "#00be00", "#be00be", "#be0000", "#0000be"]
    mono = ("Menlo", 11) if sys.platform == "darwin" else ("Consolas", 10) if sys.platform == "win32" else ("DejaVu Sans Mono", 10)

    root = tk.Tk()
    root.title("VIDEODROME uploader")
    root.configure(bg=BG)
    root.minsize(560, 520)
    root.geometry("620x600")
    events = queue.Queue()                  # (kind, value) from the worker thread
    u = Uploader(backend, a.port)
    state = {"busy": False, "writing": False, "by_hand": False}

    bars = tk.Canvas(root, height=18, bg=BG, highlightthickness=0)
    bars.pack(fill="x")
    bars.bind("<Configure>", lambda e: [bars.delete("all")] + [bars.create_rectangle(
        k * e.width / 7, 0, (k + 1) * e.width / 7, 18, fill=c, width=0) for k, c in enumerate(BARS)])
    head = tk.Canvas(root, height=64, bg=BG, highlightthickness=0)
    head.pack(fill="x", padx=16, pady=(10, 0))
    for dx, col in ((-2, GHOST_R), (2, GHOST_C), (0, ORANGE)):   # the misconverged wordmark
        head.create_text(4 + dx, 30, text="VIDEODROME", anchor="w", fill=col, font=(mono[0], 30, "bold"))
    head.create_text(10_000, 30, text="CH 83  PUBLIC ACCESS", anchor="e", fill=OSD, font=(mono[0], 14, "bold"), tags="osd")
    head.bind("<Configure>", lambda e: head.coords("osd", e.width - 4, 30))
    tk.Label(root, text="uploader for the M-VAVE FM-1 and SMK-37", bg=BG, fg=DIM, font=mono).pack(anchor="w", padx=20)

    box = tk.Frame(root, bg=PANEL, padx=14, pady=10)
    box.pack(fill="x", padx=16, pady=10)

    def row(r, label):
        tk.Label(box, text=label, bg=PANEL, fg=DIM, font=mono, anchor="w", width=10).grid(row=r, column=0, sticky="w")
        v = tk.StringVar(value="-")
        tk.Label(box, textvariable=v, bg=PANEL, fg=FG, font=mono, anchor="w", justify="left", wraplength=380).grid(
            row=r, column=1, sticky="w")
        return v
    v_dev, v_pkg = row(0, "keyboard"), row(1, "package")
    box.columnconfigure(1, weight=1)

    btns = tk.Frame(root, bg=BG)
    btns.pack(fill="x", padx=16)

    def button(text, cmd, primary=False):
        b = tk.Button(btns, text=text, command=cmd, font=mono, relief="flat", padx=12, pady=6,
                      bg=ORANGE if primary else "#141c4e", fg="#000000" if primary else FG,
                      activebackground=FG, activeforeground="#000000", disabledforeground="#2a3474")
        b.pack(side="left", padx=(0, 8))
        return b
    b_find = button("Find keyboard", lambda: work(do_find))
    b_pick = button("Choose package…", lambda: pick())
    b_go = button("Install", lambda: confirm(), primary=True)

    backed = tk.BooleanVar(value=False)
    tk.Checkbutton(root, text="I saved a backup (editor: Projects > Backup)", variable=backed, bg=BG, fg=FG,
                   selectcolor=BG, activebackground=BG, activeforeground=FG, font=mono,
                   command=lambda: refresh()).pack(anchor="w", padx=16, pady=(8, 0))

    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass
    style.configure("vd.Horizontal.TProgressbar", troughcolor=PANEL, background=ORANGE, bordercolor=BG,
                    lightcolor=ORANGE, darkcolor=ORANGE)
    bar = ttk.Progressbar(root, style="vd.Horizontal.TProgressbar", maximum=100)
    bar.pack(fill="x", padx=16, pady=(10, 4))
    v_status = tk.StringVar(value="Plug the keyboard in by USB, then Find keyboard.")
    tk.Label(root, textvariable=v_status, bg=BG, fg=OSD, font=mono, anchor="w", justify="left", wraplength=520).pack(
        fill="x", padx=16)
    log = tk.Text(root, height=10, width=60, bg=PANEL, fg=DIM, insertbackground=FG, font=mono, relief="flat", wrap="word")
    log.pack(fill="both", expand=True, padx=16, pady=(6, 16))

    def say(s, status=True):
        log.insert("end", time.strftime("%H:%M:%S  ") + s + "\n")
        log.see("end")
        if status:
            v_status.set(s)

    def refresh():
        busy = state["busy"]
        b_find.config(state="disabled" if busy else "normal")
        b_pick.config(state="disabled" if busy else "normal")
        ok = u.dev is not None and u.package is not None and backed.get() and not busy
        b_go.config(state="normal" if ok else "disabled", bg=ORANGE if ok else "#0b1038")

    def work(fn, *args):
        """fn in a thread (USB waits must not freeze the window); its events come back through the queue"""
        if state["busy"]:
            return
        state["busy"] = True
        refresh()

        def body():
            try:
                fn(*args)
            except I.InstallError as e:
                events.put(("error", e))
            except Exception as e:      # (a MIDI driver's own error: shown, not a crash)
                events.put(("error", I.InstallError("other", f"{type(e).__name__}: {e}")))
            events.put(("idle", None))
        threading.Thread(target=body, daemon=True).start()

    def do_find():
        events.put(("say", "looking for the keyboard on the USB-MIDI ports…"))
        ident = u.find()
        events.put(("device", (ident, u.dev.name)))
        if not state["by_hand"]:                         # (a package chosen by hand is kept; else the keyboard's own)
            events.put(("package", u.choose(a.package)))
            state["by_hand"] = bool(a.package)
        u.check()
        events.put(("say", "ready: tick the backup box and press Install" if not backed.get() else "ready: press Install"))

    def do_install():
        state["writing"] = True

        def step(k, x=None):
            events.put(("step", (k, x)))
        got = u.install(step)
        events.put(("done", got))

    def pick():
        f = filedialog.askopenfilename(title="VIDEODROME package", initialdir=str(RELEASES if RELEASES.is_dir() else ROOT),
                                       filetypes=[("Felucca package", "*.fwsc"), ("all files", "*")])
        if not f:
            return
        try:
            product = u.choose(f)
            state["by_hand"] = True
            v_pkg.set(f"{Path(f).name}\n{product}")
            say(f"package chosen: {Path(f).name} ({product})")
            if u.dev:
                u.check()
        except I.InstallError as e:
            show_error(e)
        refresh()

    def confirm():
        if not (u.dev and u.package):
            return
        msg = (f"Write {u.package[0].name} to the {I.model_of(u.dev.id.text)}?\n\n"
               "It takes about two minutes. Do not unplug the USB cable or let the computer sleep until it says done "
               "(if it is cut off anyway, the keyboard waits in its update mode: press Install again).")
        if messagebox.askokcancel("VIDEODROME uploader", msg, icon="warning"):
            bar.config(mode="indeterminate")
            bar.start(12)
            work(do_install)

    def show_error(e):
        say(f"error: {e}")
        hint = HINTS.get(e.code)
        if hint:
            say(hint, status=False)
        messagebox.showerror("VIDEODROME uploader", f"{e}\n\n{hint or ''}".strip())

    def pump():
        try:
            while True:
                kind, val = events.get_nowait()
                if kind == "say":
                    say(val)
                elif kind == "device":
                    ident, port = val
                    v_dev.set(describe(ident))
                    say(f"found: {ident} on port {port}")
                elif kind == "package":
                    v_pkg.set(f"{u.package[0].name}\n{val}")
                    say(f"package: {u.package[0].name} ({val})", status=False)
                elif kind == "step":
                    k, x = val
                    if k == "check":
                        v_status.set(f"step 1 of 2: the keyboard checks the package ({x} reads)")
                    elif k == "write":
                        if str(bar.cget("mode")) != "determinate":
                            bar.stop()
                            bar.config(mode="determinate")
                        bar["value"] = x
                        v_status.set(f"step 2 of 2: writing {x} %  -  do not unplug")
                    else:
                        say({"start": f"starting the update on {x}", "loader": "the keyboard restarts into its update mode…",
                             "reboot": "written: the keyboard restarts…", "done": f"the keyboard runs {x}"}.get(k, k))
                elif kind == "done":
                    state["writing"] = False
                    bar.stop()
                    bar.config(mode="determinate")
                    bar["value"] = 100
                    v_dev.set(describe(val))
                    say(f"done: VIDEODROME is installed ({val}). Long live the new flesh.")
                    messagebox.showinfo("VIDEODROME uploader", f"Done: the keyboard runs {val}.")
                elif kind == "error":
                    state["writing"] = False
                    bar.stop()
                    bar.config(mode="determinate")
                    bar["value"] = 0
                    show_error(val)
                elif kind == "idle":
                    state["busy"] = False
                    refresh()
        except queue.Empty:
            pass
        root.after(80, pump)

    def on_close():
        if state["writing"] and not messagebox.askyesno(
                "VIDEODROME uploader", "The keyboard is being written. Closing now leaves it in its update mode "
                "(press Install again later to finish). Close anyway?", icon="warning"):
            return
        u.close()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    say(f"packages in {RELEASES}: " + (", ".join(p.name for p, _, _ in releases()) or "none"), status=False)
    refresh()
    root.after(80, pump)
    root.after(300, lambda: work(do_find))    # look straight away: most of the time the keyboard is plugged in
    root.mainloop()
    return 0


# ------------------------------------------------------------------- main ---

def main(argv=None, backend=None, out=sys.stdout, ask=I.ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli", action="store_true", help="in the terminal, no window")
    ap.add_argument("--yes", action="store_true", help="--cli: do not ask before writing")
    ap.add_argument("--list", action="store_true", help="the packages in releases/ and their keyboards")
    ap.add_argument("--package", metavar="FILE", help="this .fwsc instead of the newest in releases/ for the keyboard")
    ap.add_argument("--port", metavar="NAME", help="a MIDI port whose name contains NAME")
    a = ap.parse_args(argv)
    if a.list:
        rows = releases()
        for p, (mj, mn), b in rows:
            print(f"{p.name:<36} {mj}.{mn}  {MODEL_OF_BOARD[b]}", file=out)
        if not rows:
            print(f"no packages in {RELEASES}", file=out)   # (the module's, read now)
        return 0
    if backend is None:
        try:
            backend = I.MidoBackend()
        except ImportError:
            print("error: needs mido and python-rtmidi: pip install mido python-rtmidi", file=sys.stderr)
            return 2
    if a.cli:
        return run_cli(a, backend, out, ask)
    try:
        return run_gui(a, backend)
    except Exception as e:                  # no Tk in this Python (some Linux distributions ship it apart), or no
        if not isinstance(e, ImportError) and type(e).__name__ != "TclError":   # display to open a window on
            raise
        print(f"no window ({e}; on Linux: sudo apt install python3-tk): the terminal version follows", file=sys.stderr)
        return run_cli(a, backend, out, ask)


if __name__ == "__main__":
    sys.exit(main())
