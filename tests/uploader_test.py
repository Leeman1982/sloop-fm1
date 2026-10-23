#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/videodrome_uploader.py against a simulated keyboard (no hardware, no mido, no window): the package it picks
for each model, what it refuses before anything is sent, a whole install, an install cut off and finished, the
terminal version and --list. The simulated FM-1 is tests/install_test.py's (its helpers, not its tests).
  python3 tests/uploader_test.py"""
import io
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402
import videodrome_uploader as V  # noqa: E402

# install_test.py's FakeFM1 and package(): its source up to its tests
src = (ROOT / "tests" / "install_test.py").read_text()
helpers = {"__name__": "install_helpers", "__file__": str(ROOT / "tests" / "install_test.py")}
exec(compile(src[:src.index("# ------------------------------------------------------------------- tests ---")],
             "install_test.py", "exec"), helpers)
FakeFM1, package = helpers["FakeFM1"], helpers["package"]
I.DELAY.update(open=0, start=0.01, reply=0, loader=0.01, reboot=0.01, retry=0.02, poll=0.05, hs=0.1,
               idle_check=0.4, idle_write=0.4, wait_loader=2, wait_reboot=2)

failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<74} {'ok' if cond else 'FAIL'}")
    failed += not cond


TMP = Path(tempfile.mkdtemp(prefix="vd-uploader-"))
REL = TMP / "releases"
REL.mkdir()
PRODUCTS = {"fm1": "FM-1_900", "smk37": "SMK-37 Pro_900", "smk37-elite": "SMK-37 Elite_900", "smk37-mke": "MKE-P37_900"}
for board, product in PRODUCTS.items():
    (REL / f"videodrome-1.0-{board}.fwsc").write_bytes(package(product))
(REL / "videodrome-0.9-fm1.fwsc").write_bytes(package("FM-1_900", seed=3))   # an older one: never picked
(REL / "notes.txt").write_text("not a package")

# ---- the packages
rows = V.releases(REL)
ok(len(rows) == 5 and rows[0][1] == (1, 0) and {b for _, _, b in rows} == set(PRODUCTS),
   "releases(): the five packages, newest first, the text file left out")
ok(V.package_for("FM-1_015", REL).name == "videodrome-1.0-fm1.fwsc", "FM-1 (stock): the FM-1 package, the newest")
ok(V.package_for("ota-FM-1_900", REL).name == "videodrome-1.0-fm1.fwsc", "FM-1 in update mode: the FM-1 package")
ok(V.package_for("SMK-37 Pro_016", REL).name == "videodrome-1.0-smk37.fwsc", "SMK-37 Pro: the Pro package")
ok(V.package_for("SMK-37 Elite_900", REL).name == "videodrome-1.0-smk37-elite.fwsc", "SMK-37 Elite: its own name's package")
ok(V.package_for("MKE-P37_015", REL).name == "videodrome-1.0-smk37-mke.fwsc", "MKE-P37: its own name's package")
ok(V.package_for("STARRYKEY-37 PLAY_016", REL) is None, "a keyboard there is no package for: none (never a guess)")
ok(V.package_for("FM-1_015", TMP / "nowhere") is None, "no releases folder: none")

# ---- a whole install on an FM-1 running the stock firmware
img = I.deinterleave(package("FM-1_900"))[0]
dev = FakeFM1(img, identity="FM-1_015")
u = V.Uploader(dev, folder=REL)
ident = u.find()
prod = u.choose()
u.check()
steps = []
got = u.install(lambda k, x=None: steps.append(k))
ok(ident == "FM-1_015" and prod == "FM-1_900" and got == "FM-1_900" and dev.bad == 0 and dev.upgrades == 2
   and "write" in steps and steps[-1] == "done", "FM-1: found, its package picked, checked, written, verified")
ok("official" in V.describe("FM-1_015") and "Felucca" in V.describe("FM-1_900") and "update mode" in V.describe("ota-FM-1_900"),
   "describe(): stock, Felucca family, update mode")

# ---- refusals, before anything is sent
dev = FakeFM1(img, identity="SMK-37 Pro_016", name="SMK-37 Pro")
u = V.Uploader(dev, folder=REL)
u.find()
u.choose(REL / "videodrome-1.0-fm1.fwsc")
try:
    u.check()
    refused = None
except I.InstallError as e:
    refused = e.code
ok(refused == "model" and dev.upgrades == 0, "an FM-1 package on an SMK-37: refused, nothing sent")
u.close()
dev = FakeFM1(img, identity="SMK-37 Pro_016", name="SMK-37 Pro", after_write="SMK-37 Pro_900",
              loader=("ota-SMK-37 Pro_900", "Felucca Update"))
u = V.Uploader(dev, folder=REL)
u.find()
ok(u.choose() == "SMK-37 Pro_900", "SMK-37 Pro: its package picked by itself")
dev.image = I.deinterleave(package("SMK-37 Pro_900"))[0]
u.check()
ok(u.install() == "SMK-37 Pro_900" and dev.bad == 0, "SMK-37 Pro: written and verified")
dev = FakeFM1(img, identity="ota-SLOWTHING_900", name="Felucca Update")
u = V.Uploader(dev, folder=REL)
u.find()
try:
    u.choose()
    refused = None
except I.InstallError as e:
    refused = e.code
ok(refused == "badpkg", "a model with no package: a clear error, nothing sent")
bad = TMP / "videodrome-1.0-broken.fwsc"
bad.write_bytes(package("FM-1_900", damage=True))
why = ""
try:
    V.Uploader(FakeFM1(img), folder=REL).choose(bad)
    refused = None
except I.InstallError as e:
    refused, why = e.code, str(e)
ok(refused == "badpkg" and "damaged" in why, "a damaged package: refused before anything")

# ---- an install cut off in step 2, then finished
dev = FakeFM1(img, identity="FM-1_015", unplug_after=8)
u = V.Uploader(dev, folder=REL)
u.find()
u.choose()
try:
    u.install()
    cut = None
except I.InstallError as e:
    cut = e.code
dev.unplug_after = None
dev.boot("ota-FM-1_900", "Felucca Update")          # replugged: the keyboard waits in its update mode
u2 = V.Uploader(dev, folder=REL)
ident = u2.find()
u2.choose()
ok(cut == "lost" and ident == "ota-FM-1_900" and u2.in_update_mode and u2.install() == "FM-1_900",
   "cut off while writing, replugged: found in update mode, the write finished")
ok(all(k in V.HINTS for k in ("notfound", "lost", "stopped", "noloader", "model", "badpkg")),
   "every error a person can meet has a next step")

# ---- not found
try:
    V.Uploader(FakeFM1(img, name="Some Synth"), folder=REL).find()   # (a port not named as the FM-1 family: not probed)
    nf = None
except I.InstallError as e:
    nf = e.code
ok(nf == "notfound", "no keyboard: not found (other gear is never probed)")

class NoMidi(FakeFM1):
    def output_names(self):
        raise RuntimeError("MidiInAlsa::initialize: error creating ALSA sequencer client object")


try:
    V.Uploader(NoMidi(img), folder=REL).find()
    nm = None
except I.InstallError as e:
    nm = e.code
ok(nm == "nomidi" and "nomidi" in V.HINTS, "the computer's MIDI system fails to open: a clear error, not a crash")

# ---- the terminal version and --list
V.RELEASES = REL                                     # (the module's default folder)
out, err = io.StringIO(), io.StringIO()
old, sys.stderr = sys.stderr, err
try:
    rc = V.main(["--cli", "--yes"], backend=FakeFM1(img, identity="FM-1_015"), out=out)
    rc_no = V.main(["--cli"], backend=FakeFM1(img, identity="FM-1_015"), out=out, ask=lambda _p: False)
    rc_nf = V.main(["--cli", "--yes"], backend=FakeFM1(img, name="Other"), out=out)
finally:
    sys.stderr = old
o = out.getvalue()
ok(rc == 0 and "installed: FM-1_900" in o, "--cli --yes: found, picked, written")
ok(rc_no == 1 and "cancelled" in o, "--cli: answering no writes nothing")
ok(rc_nf == 3 and "USB data cable" in err.getvalue(), "--cli: not found, exit 3 and what to do")
lst = io.StringIO()
V.main(["--list"], out=lst)
ok(lst.getvalue().count(".fwsc") == 5 and "SMK-37 Elite" in lst.getvalue(), "--list: the packages and their keyboards")

# ---- the real releases folder: one package for each of the four names
real = V.releases(ROOT / "releases")
ok({b for _, _, b in real} >= set(PRODUCTS) and all(I.product_of(p.read_bytes()) == PRODUCTS[b] for p, _, b in real
                                                   if _ == real[0][1]),
   "releases/: a package for each keyboard, its identity the one its name says")

print(f"UPLOADER TESTS FAILED ({failed})" if failed else "uploader tests passed")
sys.exit(1 if failed else 0)
