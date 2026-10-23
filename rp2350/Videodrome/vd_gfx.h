/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: the OLED's drawing vocabulary, xtro's "84" terminal skin (leeman1982/xtro ui_gfx.h, the
 * hx:: helpers) in C for the U8g2 C API: chamfered 1 px frames, > prompts, [CHIP] screen names, hatched banners for
 * what must not be missed, framed bars with an inset fill, a header rule solid under the readout and decaying to dots.
 * Everything 1 px lines on a 1-bit panel: few lit pixels. Geometry as xtro's (128 x 64; the 6x10 list rows, the 5x7
 * header and footer), so the two instruments read as one family. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__has_include)
#if __has_include("clib/u8g2.h")
#include "clib/u8g2.h"
#else
#include "u8g2.h"
#endif
#else
#include "u8g2.h"
#endif

#define VG_W 128
#define VG_H 64
#define VG_BAND_Y 11            /* the content band, under the header rule (y 9) */
#define VG_FOOT_Y 53            /* the footer rule */
#define VG_GUTTER_R (VG_W - 10)
#define VG_FLUSH_R (VG_W - 4)
#define VG_LABEL_X 8

static int vg_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* on / period dashes: (1, 2) a dotted hairline, (2, 3) torn film, (1, 1) solid */
static void vg_rule(u8g2_t *u, int x, int y, int w, int on, int period)
{
    int i;
    if (period <= on) {
        u8g2_DrawHLine(u, x, y, w);
        return;
    }
    for (i = 0; i < w; i += period)
        u8g2_DrawHLine(u, x + i, y, w - i < on ? w - i : on);
}
static void vg_vrule(u8g2_t *u, int x, int y, int h, int on, int period)
{
    int j;
    if (period <= on) {
        u8g2_DrawVLine(u, x, y, h);
        return;
    }
    for (j = 0; j < h; j += period)
        u8g2_DrawVLine(u, x, y + j, h - j < on ? h - j : on);
}
/* solid under the live readout, dots across the rest: the line that sets the instrument's tone */
static void vg_header_rule(u8g2_t *u, int solid_to)
{
    u8g2_DrawHLine(u, 0, 9, solid_to);
    vg_rule(u, solid_to, 9, VG_W - solid_to, 1, 2);
}
/* every box: a 1 px outline with its four corners knocked out */
static void vg_frame(u8g2_t *u, int x, int y, int w, int h)
{
    u8g2_DrawFrame(u, x, y, w, h);
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawPixel(u, x, y);
    u8g2_DrawPixel(u, x + w - 1, y);
    u8g2_DrawPixel(u, x, y + h - 1);
    u8g2_DrawPixel(u, x + w - 1, y + h - 1);
    u8g2_SetDrawColor(u, 1);
}
/* 50 % on a 1-bit panel: a checkerboard, a few dozen pixels at a time */
static void vg_hatch(u8g2_t *u, int x, int y, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = (x + y + j) & 1; i < w; i += 2)
            u8g2_DrawPixel(u, x + i, y + j);
}
/* the loud marker: a solid bar, hatched caps, the text knocked out; 7 px tall (the caller sets the font) */
#define VG_BANNER_H 7
static void vg_banner(u8g2_t *u, int x, int y, int w, const char *label, int cap)
{
    int i, j, tw, tx;
    u8g2_DrawBox(u, x, y, w, VG_BANNER_H);
    u8g2_SetDrawColor(u, 0);
    for (j = 0; j < VG_BANNER_H; j++)
        for (i = (x + y + j + 1) & 1; i < cap; i += 2) {
            u8g2_DrawPixel(u, x + i, y + j);
            u8g2_DrawPixel(u, x + w - 1 - i, y + j);
        }
    tw = u8g2_GetStrWidth(u, label);
    tx = x + (w - tw) / 2;
    if (tx < x + cap)
        tx = x + cap;
    u8g2_DrawStr(u, tx, y + VG_BANNER_H - 1, label);
    u8g2_SetDrawColor(u, 1);
}
/* text clipped to [.., x1): a long string is cut at its slot's edge */
static void vg_clipped(u8g2_t *u, int x, int y, const char *s, int x1)
{
    u8g2_SetClipWindow(u, 0, 0, (u8g2_uint_t)(x1 < 0 ? 0 : x1), VG_H);
    u8g2_DrawStr(u, x, y, s);
    u8g2_SetMaxClipWindow(u);
}
/* a screen's name in terminal brackets, [MENU]: quiet (the loud one is the banner); sets the 4x6 font; -> the x past it */
#define VG_CHIP_H 7
static int vg_chip_w(u8g2_t *u, const char *label) { return u8g2_GetStrWidth(u, label) + 6; }
static int vg_chip(u8g2_t *u, int x, int y, const char *label)
{
    int w;
    u8g2_SetFont(u, u8g2_font_4x6_tr);
    w = vg_chip_w(u, label);
    u8g2_DrawVLine(u, x, y, VG_CHIP_H);
    u8g2_DrawVLine(u, x + w - 1, y, VG_CHIP_H);
    u8g2_DrawHLine(u, x, y, 2);
    u8g2_DrawHLine(u, x + w - 2, y, 2);
    u8g2_DrawHLine(u, x, y + VG_CHIP_H - 1, 2);
    u8g2_DrawHLine(u, x + w - 2, y + VG_CHIP_H - 1, 2);
    u8g2_DrawStr(u, x + 4, y + VG_CHIP_H - 1, label);
    return x + w;
}
/* the framed bar: a chamfered outline, a pixel clear, the fill (3 tall); fill < 0: busy (hatched) */
#define VG_BAR_H 7
static void vg_bar(u8g2_t *u, int x, int y, int w, int fill)
{
    vg_frame(u, x, y, w, VG_BAR_H);
    if (fill < 0)
        vg_hatch(u, x + 2, y + 2, w - 4, 3);
    else if (fill > 0)
        u8g2_DrawBox(u, x + 2, y + 2, fill > w - 4 ? w - 4 : fill, 3);
}
static int vg_fill(int v, int max, int w) { return max <= 0 ? 0 : vg_clampi(v, 0, max) * w / max; }
static void vg_meter(u8g2_t *u, int x, int y, int w, int v, int max, int ticks)
{
    int i;
    vg_bar(u, x, y, w, vg_fill(v, max, w - 4));
    if (ticks)
        for (i = 1; i < 4; i++)
            u8g2_DrawPixel(u, x + i * (w - 1) / 4, y + VG_BAR_H);
}
/* a list row (6x10, y the row's top, 11 tall): the selected one a chamfered frame with a > prompt; while its value is
 * turned the prompt is a solid block (the terminal cursor). -> the value's left edge */
static int vg_list_row(u8g2_t *u, int y, int sel, const char *label, const char *value, int gutter, int edit)
{
    int right = gutter ? VG_GUTTER_R : VG_FLUSH_R, vx;
    if (sel) {
        vg_frame(u, 0, y, right + 4, 11);
        if (edit) {
            u8g2_DrawBox(u, 1, y + 1, 6, 9);
            u8g2_SetDrawColor(u, 0);
            u8g2_DrawGlyph(u, 1, y + 9, '>');
            u8g2_SetDrawColor(u, 1);
        } else {
            u8g2_DrawGlyph(u, 1, y + 8, '>');
        }
    }
    if (!value) {
        vg_clipped(u, VG_LABEL_X, y + 8, label, right);
        return right;
    }
    vx = right - u8g2_GetStrWidth(u, value);
    vg_clipped(u, VG_LABEL_X, y + 8, label, vx - 2);
    u8g2_DrawStr(u, vx, y + 8, value);
    return vx;
}
static void vg_scrollbar(u8g2_t *u, int top, int h, int first, int visible, int total)
{
    int x = VG_W - 2, th, span, ty;
    if (total <= visible)
        return;
    vg_vrule(u, x, top, h, 1, 3);
    th = h * visible / total;
    if (th < 4)
        th = 4;
    span = total - visible;
    ty = top + (h - th) * vg_clampi(first, 0, span) / span;
    u8g2_DrawBox(u, x - 1, ty, 2, th);
}
/* `> text` in the current font; -> the x past the text (a cursor hangs there) */
static int vg_prompt(u8g2_t *u, int x, int baseline, const char *text)
{
    int adv = u8g2_GetGlyphWidth(u, '>');
    u8g2_DrawGlyph(u, x, baseline, '>');
    u8g2_DrawStr(u, x + adv + 2, baseline, text);
    return x + adv + 2 + u8g2_GetStrWidth(u, text);
}
static void vg_block_cursor(u8g2_t *u, int x, int baseline) { u8g2_DrawBox(u, x + 1, baseline - 5, 3, 5); }

/* the footer: rule, [CHIP], the hint, the EDIT banner, or a toast in its place */
#define VG_EDIT_W 24
static void vg_footer(u8g2_t *u, const char *chip, const char *hint, int editing, const char *toast)
{
    int ty = VG_FOOT_Y + 3, limit = editing ? VG_W - VG_EDIT_W - 4 : VG_W, x = 2;
    u8g2_DrawHLine(u, 0, VG_FOOT_Y, VG_W);
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    if (toast && *toast) {
        int bw = u8g2_GetStrWidth(u, toast) + 14;
        if (bw > VG_W - 4)
            bw = VG_W - 4;
        vg_banner(u, (VG_W - bw) / 2, ty, bw, toast, 4);
        return;
    }
    if (chip && *chip) {
        int cw;
        u8g2_SetFont(u, u8g2_font_4x6_tr);
        cw = vg_chip_w(u, chip);
        u8g2_SetFont(u, u8g2_font_5x7_tr);
        if (!hint || !*hint || 2 + cw + 3 + u8g2_GetStrWidth(u, hint) <= limit)
            x = vg_chip(u, 2, ty, chip) + 3;
    }
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    if (hint && *hint)
        vg_clipped(u, x, VG_FOOT_Y + 9, hint, limit);
    if (editing) {
        u8g2_SetFont(u, u8g2_font_4x6_tr);
        vg_banner(u, VG_W - VG_EDIT_W - 2, ty, VG_EDIT_W, "EDIT", 3);
    }
}
