#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""web/vd_skin.css (VIDEODROME 1.1's CH 83 skin) into the pages that wear it, at the end of each page's own <style>,
between its markers: the editor (web/editor.html), the installer (web/index_pkg.html), the uploader (web/uploader.html).
The pages stay single files that run from file:// (no stylesheet next to them).
  python3 tools/vd_skin.py           write it into the pages
  python3 tools/vd_skin.py --check   exit 1 when a page's copy is not the file's"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIN = ROOT / "web" / "vd_skin.css"
PAGES = [ROOT / "web" / n for n in ("editor.html", "index_pkg.html", "uploader.html")]
BEGIN, END = "/* VD-SKIN */", "/* /VD-SKIN */"


def skinned(html, css):
    """html with css between the markers (added before the first </style> when there are none)"""
    block = f"{BEGIN}\n{css.rstrip()}\n  {END}\n"
    if BEGIN in html:
        return re.sub(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n", lambda _m: block, html, count=1, flags=re.S)
    i = html.index("</style>")
    return html[:i] + "  " + block + html[i:]


def main(argv):
    css = SKIN.read_text(encoding="utf-8")
    stale = []
    for p in PAGES:
        html = p.read_text(encoding="utf-8")
        new = skinned(html, css)
        if new != html:
            stale.append(p.name)
            if "--check" not in argv:
                p.write_text(new, encoding="utf-8")
    if "--check" in argv:
        if stale:
            print("vd_skin: not the skin of web/vd_skin.css: " + ", ".join(stale) + " (run tools/vd_skin.py)")
            return 1
        print("vd_skin: the three pages wear web/vd_skin.css")
        return 0
    print("vd_skin: " + (", ".join(stale) + " updated" if stale else "nothing to change"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
