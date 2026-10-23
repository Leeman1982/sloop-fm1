/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: the synth as one translation unit, as felucca.c builds it on the FM-1. The FM-1's sources
 * (fw/src, unchanged: tools/rp2350_sync.py) in felucca.c's order, minus the FM-1's screen, keys, USB controller and
 * update loader; the RP2350's own parts around them: vd_port.h (time, interrupts, flash), vd_glue.h (what the logic
 * takes from the FM-1's main loop and screens), vd_flash.h (storage over the FS partition), the OLED UI (vd_ui.h,
 * xtro's panel) and vd_rp.h (core 1, the audio block, the MIDI pump, the boot, the functions Videodrome.ino calls). */
#include "vd_port.h"
#include "fw/src/felucca_tables.h"
#define memset felucca_memset              /* (libc.c's own memset / memcpy / memcmp: the RP2350 has the C library's) */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "fw/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
/* gfx.c for its palettes, theme and fonts (the settings' side record keeps the theme: the same bytes as on the FM-1),
 * never for a pixel: no canvas, no LCD */
#define CV_MAX 16u
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p) { (void)x, (void)y, (void)w, (void)h, (void)p; }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x, (void)y, (void)w, (void)h, (void)c; }
#include "fw/src/gfx.c"
#include "fw/src/core.h"
#include "fw/src/cpu1.h"
#include "fw/src/engines.c"
#include "fw/src/drums.c"
#include "fw/src/params.c"
#include "fw/src/voice.c"
#include "fw/src/slicer.c"
#include "fw/src/fx.c"
#include "fw/src/usb.c"
/* MIDI OUT on xtro's GP1: the SMK-37's TRS out (smk_midi_out.h, its UART macros pointed at the PIO UART) */
#define SMO_TX_DONE() vd_midi_tx_done()
#define SMO_TX_PUT(b) vd_midi_tx_put(b)
#include "fw/src/smk_midi_out.h"
#include "fw/src/midi_uart.c"
#include "fw/src/arranger.c"
#include "fw/src/seq.c"
#include "vd_cpu1.h"                       /* the second core, where felucca.c has cpu1.c */
#include "vd_glue.h"                       /* what panel.c / ui.c / project.c take from the FM-1's main loop and screens */
#include "fw/src/panel.c"
#include "fw/src/ui.c"
/* the FM-1's own UI, for its logic (edit_param, step_edit, tracks_edit, the song, the menus' settings ...): built as
 * felucca.c builds it, drawing into a canvas of nothing (CV_MAX above: cv_begin keeps no row), never asked to draw */
#include "fw/src/ui_song.c"
#include "fw/src/ui_studio.c"
#include "fw/src/icons.c"
#include "fw/src/ui_draw.c"
#include "fw/src/ui_vis.c"
#include "fw/src/ui_layers.c"
#include "fw/src/ui_menu.c"
#include "fw/src/ui_input.c"
#include "vd_flash.h"                      /* felucca.c's flash glue (st_read / st_erase / st_prog) over the partition */
#include "fw/src/storage.c"
#include "fw/src/upreset.c"
#include "fw/src/project.c"
#include "vd_ui.h"
#include "vd_rp.h"
