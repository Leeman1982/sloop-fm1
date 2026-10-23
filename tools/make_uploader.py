#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VIDEODROME-UPLOADER.html: one file that flashes Videodrome from Chrome or Edge, nothing to install. It is
web/uploader.html with web/fm1pkg.js + web/fm1ota.js inlined and every package of releases/ embedded (base64).
  python3 tools/make_uploader.py [releases/] [VIDEODROME-UPLOADER.html]"""
import base64
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))
from make_site import product_of, strip_module  # noqa: E402

PKG_RE = re.compile(r"videodrome-(\d+)\.(\d+)-(fm1|smk37|smk37-elite|smk37-mke)\.fwsc")
PRODUCT_RE = {"fm1": r"FM-1_9\d\d", "smk37": r"SMK-37 Pro_9\d\d", "smk37-elite": r"SMK-37 Elite_9\d\d",
              "smk37-mke": r"MKE-P37_9\d\d"}


def packages(folder):
    """the newest package for each board, checked: its identity the one its name says, the Felucca loader in it"""
    best = {}
    for p in sorted(Path(folder).glob("*.fwsc")):
        m = PKG_RE.fullmatch(p.name)
        if not m:
            continue
        ver, board = (int(m[1]), int(m[2])), m[3]
        if board not in best or ver > best[board][0]:
            best[board] = (ver, p)
    out = []
    for board, (ver, p) in sorted(best.items()):
        raw = p.read_bytes()
        product = product_of(raw)
        if not re.fullmatch(PRODUCT_RE[board], product):
            raise SystemExit(f"{p.name}: identity {product!r} is not a {board} package")
        if b"FELUCCA-LOADER-1" not in raw:
            raise SystemExit(f"{p.name}: no Felucca update loader in it")
        out.append({"name": p.name, "board": board, "product": product, "version": "%d.%d" % ver,
                    "b64": base64.b64encode(raw).decode()})
    return out


def build(folder, out):
    pk = packages(folder)
    if not pk:
        raise SystemExit(f"{folder}: no videodrome-X.Y-<board>.fwsc in it")
    html = (ROOT / "web" / "uploader.html").read_text(encoding="utf-8")
    for mark in ("/*LIB*/", "/*PACKAGES*/", "/*VERSION*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"uploader.html must contain {mark} once; update make_uploader.py")
    lib = strip_module((ROOT / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((ROOT / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    # one classic script: a page name equal to a library one replaces it (a page fail() once broke every error)
    top = r"^(?:async\s+)?(?:function|const|let|class)\s+([A-Za-z_$][\w$]*)"
    clash = set(re.findall(top, lib, re.M)) & set(re.findall(top, html.split("/*LIB*/")[1], re.M))
    if clash:
        raise SystemExit(f"uploader.html redefines {sorted(clash)} of fm1pkg.js / fm1ota.js: rename them in the page")
    version = max((p["version"] for p in pk), key=lambda v: tuple(map(int, v.split("."))))
    html = html.replace("/*LIB*/", lib).replace("/*VERSION*/", json.dumps(version)) \
               .replace("/*PACKAGES*/", json.dumps(pk))
    Path(out).write_text(html, encoding="utf-8")
    return pk


if __name__ == "__main__":
    folder = sys.argv[1] if len(sys.argv) > 1 else ROOT / "releases"
    out = sys.argv[2] if len(sys.argv) > 2 else ROOT / "VIDEODROME-UPLOADER.html"
    for p in build(folder, out):
        print(f"  {p['name']:<36} {p['product']}")
    print(f"{out}: {Path(out).stat().st_size // 1024} KB")
