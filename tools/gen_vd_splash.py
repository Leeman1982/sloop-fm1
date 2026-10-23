#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VIDEODROME boot splash: an early-80s cable channel coming on. SMPTE colour bars, the wordmark in the hot orange of a
1983 title card arriving bent (each scanline pushed sideways, a red and a cyan ghost either side, as a misconverged CRT
does), the channel ident in character-generator type (Terminus, the screen's own bitmap font, scaled in blocks as a
Chyron would), and a band of VHS tracking noise. Drawn on a 240 x 188 raster with every other line dimmed.

Same output as tools/gen_logo.py (the C side, splash.c, is unchanged): 16 colours, RLE runs of (run - 1) << 4 | colour.
Deterministic, and only the repository's own font: no system fonts, no randomness that changes run to run.

  tools/gen_vd_splash.py OUT.h            firmware boot splash
  tools/gen_vd_splash.py --png OUT.png    the same picture at 2x (README, the editor)"""
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BDF = ROOT / "assets" / "fonts" / "ter-u16b.bdf"
W, H = 240, 188

PAL = [
    (0, 0, 0),          # 0 black
    (235, 235, 235),    # 1 white
    (180, 180, 180),    # 2 75 % grey (the bars' white)
    (190, 190, 0),      # 3 yellow
    (0, 190, 190),      # 4 cyan
    (0, 190, 0),        # 5 green
    (190, 0, 190),      # 6 magenta
    (190, 0, 0),        # 7 red
    (0, 0, 190),        # 8 blue
    (255, 104, 24),     # 9 the title card's orange
    (150, 52, 8),       # 10 orange on a dimmed scanline
    (200, 20, 40),      # 11 the red ghost
    (0, 150, 170),      # 12 the cyan ghost
    (90, 90, 96),       # 13 static grey
    (4, 6, 30),         # 14 the raster's dark line (the dead-air blue of CH 83, a shade under its background)
    (255, 200, 120),    # 15 the hot core of a letter
]


def bdf_glyphs(path):
    """{char: (width, rows of bit strings)} from a BDF (8 x 16 Terminus)"""
    out, cur, rows, enc, bbx = {}, None, None, None, None
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("ENCODING"):
            enc = int(line.split()[1])
        elif line.startswith("BBX"):
            bbx = [int(v) for v in line.split()[1:]]
        elif line == "BITMAP":
            rows = []
        elif line == "ENDCHAR":
            if enc is not None and 32 <= enc < 127 and rows is not None:
                out[chr(enc)] = (bbx[0], [bin(int(r, 16))[2:].zfill(len(r) * 4)[:bbx[0]] for r in rows])
            rows, enc = None, None
        elif rows is not None:
            rows.append(line.strip())
    return out


def text(img, glyphs, s, x0, y0, sx, sy, col):
    """block-scaled text, as a character generator draws it"""
    x = x0
    for ch in s:
        w, rows = glyphs.get(ch, glyphs[" "])
        for r, bits in enumerate(rows):
            for c, b in enumerate(bits):
                if b == "1":
                    for yy in range(sy):
                        for xx in range(sx):
                            px, py = x + c * sx + xx, y0 + r * sy + yy
                            if 0 <= px < W and 0 <= py < H:
                                img[py][px] = col
        x += w * sx
    return x


def text_w(glyphs, s, sx):
    return sum(glyphs.get(ch, glyphs[" "])[0] for ch in s) * sx


def picture():
    g = bdf_glyphs(BDF)
    img = [[0] * W for _ in range(H)]
    # SMPTE bars (75 %): grey, yellow, cyan, green, magenta, red, blue
    bars = [2, 3, 4, 5, 6, 7, 8]
    for y in range(0, 34):
        for x in range(W):
            img[y][x] = bars[min(6, x * 7 // W)]
    for y in range(34, 40):                               # the castellations under them: blue, black, magenta ...
        for x in range(W):
            img[y][x] = [8, 0, 6, 0, 4, 0, 2][min(6, x * 7 // W)]
    # the wordmark: Terminus x2 wide, x3 tall (a condensed title face), each scanline bent sideways (the title arriving
    # distorted), drawn bold (each column doubled)
    word = "VIDEODROME"
    sx, sy = 2, 3
    wm = [[0] * W for _ in range(H)]
    x0 = (W - text_w(g, word, sx)) // 2
    y0 = 52
    text(wm, g, word, x0, y0, sx, sy, 9)
    text(wm, g, word, x0 + 1, y0, sx, sy, 9)
    for y in range(y0, y0 + 16 * sy):
        bend = int(round(2.0 * math.sin((y - y0) / 15.0) + (5 if 79 <= y < 82 else 0)))   # a wobble, one tear
        row = wm[y]
        for x in range(W):
            if row[x]:
                for dx, col in ((-1, 11), (1, 12)):       # misconverged guns: a red ghost left, a cyan one right
                    xx = x + bend + dx
                    if 0 <= xx < W and not img[y][xx]:
                        img[y][xx] = col
        for x in range(W):
            if row[x]:
                xx = x + bend
                if 0 <= xx < W:
                    img[y][xx] = 15 if (y - y0) % 6 == 2 else 9   # (a brighter line through the letters)
    # the channel ident, character-generator type
    for s, y, sxx, col in (("CIVIC-TV", 110, 2, 1), ("CHANNEL 83  PUBLIC ACCESS", 146, 1, 2),
                           ("LONG LIVE THE NEW FLESH", 164, 1, 13)):
        text(img, g, s, (W - text_w(g, s, sxx)) // 2, y, sxx, 2 if sxx == 2 else 1, col)
    # VHS tracking: a band of streaks at the bottom (horizontal runs: they compress, and it is what a bad tracking
    # setting does: lines torn sideways, not snow)
    seed = 0x83C1
    for y in (104, 105, 182, 183, 184, 186):
        x = 0
        while x < W:
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            run = 4 + (seed >> 16) % 28
            col = (13, 2, 0, 14, 1)[(seed >> 8) % 5]
            for xx in range(x, min(W, x + run)):
                if not img[y][xx]:
                    img[y][xx] = col
            x += run
    # the raster: every other line dimmed (the dark between a CRT's scanlines)
    for y in range(1, H, 2):
        for x in range(W):
            c = img[y][x]
            img[y][x] = 14 if c == 0 else 10 if c == 9 else c
    return img


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def header(path):
    px = [c for row in picture() for c in row]
    rle = bytearray()
    i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 16:
            j += 1
        rle.append(((j - i - 1) << 4) | px[i])
        i = j
    L = ["/* generated by tools/gen_vd_splash.py: the VIDEODROME boot splash */", "#pragma once", "#include <stdint.h>",
         f"#define SLOOP_SPLASH_W {W}", f"#define SLOOP_SPLASH_H {H}",
         "static const uint16_t SLOOP_SPLASH_PAL[16] = {" + ", ".join(f"0x{rgb565(c):04X}" for c in PAL) + "};",
         f"static const uint8_t SLOOP_SPLASH_RLE[{len(rle)}] = {{"]
    for k in range(0, len(rle), 24):
        L.append("    " + ", ".join(str(b) for b in rle[k:k + 24]) + ",")
    L.append("};")
    Path(path).write_text("\n".join(L) + "\n")
    print(f"logo: VIDEODROME splash {W}x{H}, {len(rle)} B RLE -> {path}")


def png(path, k=2):
    from PIL import Image
    img = picture()
    im = Image.new("RGB", (W, H))
    im.putdata([PAL[c] for row in img for c in row])
    im.resize((W * k, H * k), Image.NEAREST).save(path)
    print(f"logo: VIDEODROME splash -> {path}")


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--png":
        png(sys.argv[2])
    else:
        header(sys.argv[1] if len(sys.argv) > 1 else "build/gen/sloop_logo.h")
