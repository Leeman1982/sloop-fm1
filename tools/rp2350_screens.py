#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The RP2350 build's screens as one picture (rp2350/screens.png): the host test's frames (rp2350/test/run.sh writes
OUT/*.pbm, the panel's own pixels through U8g2) at 4x, lit pixels in xtro's amber, as xtro's design/preview.png.
  tools/rp2350_screens.py OUT_DIR OUT.png NAME..."""
import sys
from PIL import Image, ImageDraw

S, COLS = 4, 3


def load(d, n):
    t = open(f"{d}/{n}.pbm").read().split()
    w, h, px = int(t[1]), int(t[2]), t[3:]
    im = Image.new("RGB", (w, h))
    im.putdata([(255, 176, 0) if p == "1" else (11, 8, 5) for p in px])
    return im.resize((w * S, h * S), Image.NEAREST)


def main(d, out, names):
    ims = [load(d, n) for n in names]
    rows = (len(ims) + COLS - 1) // COLS
    sh = Image.new("RGB", (COLS * (128 * S + 10), rows * (64 * S + 22)), (24, 24, 24))
    dr = ImageDraw.Draw(sh)
    for i, (im, n) in enumerate(zip(ims, names)):
        x, y = (i % COLS) * (128 * S + 10), (i // COLS) * (64 * S + 22)
        sh.paste(im, (x, y + 18))
        dr.text((x + 2, y + 3), n, fill=(200, 200, 200))
    sh.save(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
