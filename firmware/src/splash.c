/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME boot splash: CIVIC-TV coming on (tools/gen_vd_splash.py -> sloop_logo.h: 16 colours, RLE runs of
 * (run - 1) << 4 | colour), drawn into the canvas in bands of at most 120 rows. */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#ifndef FELUCCA_LOGO_TEXT       /* (2.7) 1: the boot logo as text, the name in the four track colours (5.4 KB of flash
                                 * less: tools/build.py builds so by itself when the flash is full) */
#define FELUCCA_LOGO_TEXT 0
#endif
#if FELUCCA_LOGO_TEXT
UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    static const char L[] = "VIDEODROME";
    uint32_t i, w = text_w(&FONT_L, L), k = w * 3u <= 232u ? 3u : w * 2u <= 232u ? 2u : 1u, h = 32u, off, y, x;
    int32_t px = 0;
    char c[2] = {0, 0};
    cv_begin(240, h * k, C_BLACK);
    for (i = 0; L[i]; i++) {                            /* small, at the top left: the letters in the tracks' colours */
        c[0] = L[i];
        px = cv_text(px, 0, &FONT_L, c, TE_COL_DEF[i & 3u]);
    }
    off = (240u - w * k) / 2u;
    for (y = h * k; y-- > 0;)                           /* then k times as big, in place (from the end: nothing read is
                                                         * overwritten before) */
        for (x = w * k; x-- > 0;)
            cv_px[y * 240u + off + x] = cv_px[(y / k) * 240u + x / k];
    for (y = 0; y < h * k; y++)                         /* (the small one's left-over columns) */
        for (x = 0; x < off; x++)
            cv_px[y * 240u + x] = swap16(C_BLACK);
    cv_blit(0, y0 + 60u);
}
#else
#include "sloop_logo.h"
UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    uint32_t r0;
    for (r0 = 0; r0 < SLOOP_SPLASH_H; r0 += 120u) {
        uint32_t rows = SLOOP_SPLASH_H - r0 > 120u ? 120u : SLOOP_SPLASH_H - r0;
        uint32_t lo = r0 * SLOOP_SPLASH_W, hi = (r0 + rows) * SLOOP_SPLASH_W, pos = 0, i;
        cv_begin(SLOOP_SPLASH_W, rows, C_BLACK);
        for (i = 0; i < sizeof SLOOP_SPLASH_RLE && pos < hi; i++) {
            uint32_t run = (SLOOP_SPLASH_RLE[i] >> 4) + 1u, c = SLOOP_SPLASH_RLE[i] & 15u, p;
            if (c && pos + run > lo)
                for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                    cv_px[p - lo] = swap16(SLOOP_SPLASH_PAL[c]);
            pos += run;
        }
        cv_blit((240u - SLOOP_SPLASH_W) / 2u, y0 + r0);
    }
}
#endif

/* VIDEODROME 1.1: the transmission log under the card, after xtro's boot console. Two lines: the stage that just
 * finished, closed out (OK, SKIP, or the signal ACQUIRED), and the one in progress under a block cursor; then a framed
 * bar that fills a step a stage. Once on the air the second line is the version. done 0: nothing finished yet */
#define BOOT_STAGES 7u
static uint8_t boot_n;
UI_SMALL static void boot_log(const char *done, const char *res, const char *next)
{
    int32_t x, rx, fill;
    cv_begin(240, 50, C_BLACK);
    if (done) {
        boot_n++;
        x = cv_text(4, 0, &FONT_S, "> ", C_DIM);
        x = cv_text(x, 0, &FONT_S, done, C_GRAY);
        rx = 236 - text_w(&FONT_S, res);
        for (x += 6; x < rx - 6; x += 4)                    /* the leader: dots to the result */
            cv_rect(x, 11, 1, 2, C_DIM);
        cv_text(rx, 0, &FONT_S, res, next ? C_HI : RGB(255, 44, 52));
    }
    if (next) {
        x = cv_text(4, 17, &FONT_S, "> ", C_DIM);
        x = cv_text(x, 17, &FONT_S, next, C_HI);
        cv_rect(x + 2, 18, 8, 14, C_WHITE);                 /* the block cursor */
    } else {
        cv_text(4, 17, &FONT_S, FELUCCA_VERSION, C_GRAY);
    }
    cv_rect(5, 40, 230, 1, C_DIM);                          /* the bar: a chamfered outline, a pixel clear, the fill */
    cv_rect(5, 48, 230, 1, C_DIM);
    cv_rect(4, 41, 1, 7, C_DIM);
    cv_rect(235, 41, 1, 7, C_DIM);
    fill = (int32_t)(boot_n >= BOOT_STAGES ? BOOT_STAGES : boot_n) * 228 / (int32_t)BOOT_STAGES;
    cv_rect(6, 42, fill, 5, next ? C_WHITE : RGB(255, 44, 52));
    cv_blit(0, 190);
}

/* the boot screen: CIVIC-TV's card, then the log (main.c fm1_main: a line a stage) */
UI_SMALL static void sloop_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sloop_logo_draw(0);
    boot_n = 0;
    boot_log(0, 0, "CARRIER");
}
