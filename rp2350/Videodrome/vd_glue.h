/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: what panel.c, ui.c and project.c take from the FM-1's main loop, screens and key matrix,
 * for xtro's panel. The logic is the FM-1's; its screens are not built (the OLED UI, vd_ui.h, draws instead), so the
 * calls into them are kept as the FM-1's main loop would see them: a message is kept for the OLED to show (ui.c
 * ui_say writes ui.msg), a screen redraw request is a flag. */
static uint8_t flash_ok;                         /* the partition is there (persist_boot: fl_jedec_ram) */
#define VIS_STYLES 12u                           /* (the visualiser's styles: a setting kept, never drawn here) */
#define VIS_LAST_NAMES "TEST CARD"
#define VIS_N_STR "/12"
/* the FM-1's matrix: 14 buttons, 7 encoders (panel.c checks a stored layout against them; the layout is the FM-1's
 * default, never asked for: xtro's panel is read directly) */
#define FM1_NBTN 14u
#define FM1_NENC 7u
#define FM1_NCOL 16u
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static uint32_t fm1_input_edges(int x) { (void)x; return 0; }
static uint32_t fm1_input_note_edges(void) { return 0; }
static void fm1_wdt_feed(void) {}
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];   /* (the mix, for the OLED's scope: vd_rp.h fills it) */
static uint32_t scope_w;
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
static const int8_t FM1_KEYMAP[5][16];           /* (the FM-1's key matrix: none here) */
#define BRAND_SAIL_X 172
