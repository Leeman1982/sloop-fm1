/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350 (xtro's board): what the FM-1 firmware's sources expect of their hardware, made of the
 * RP2350's. The sources themselves are the FM-1's, unchanged (fw/, tools/rp2350_sync.py); this file comes first in
 * vd_core.c and stands in for felucca.c's top, main.c's TIMER5 and cpu1.c.
 *
 *   time        fm1_ms (core.h) is kept by vd_tick() (the 1 kHz MIDI tick, Videodrome.ino); fm1_ticks() at 24 a us
 *   interrupts  fm1_irq_off / on: the audio IRQ and the tick are core 0's, as on the FM-1, so the UI's critical
 *               sections are what they were (nesting counted)
 *   core 1      cpu1.h's API over a spin handshake: the render hands core 1 its units (fx.c units_split), core 1's
 *               loop1() runs them (vd_core1_poll)
 *   flash       storage.c's st_read / st_erase / st_prog on the FM-1's offsets 0..1 MB, at the start of the sketch's
 *               FS partition (Tools > Flash Size: an FS of 1 MB or more); the C++ side erases and programs with
 *               core 1 parked and interrupts off (vd_flash_erase / vd_flash_prog, Videodrome.ino)
 * Everything here is static: the synth is one translation unit, as felucca.c is on the FM-1. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef VD_HOST
#define VD_HOST 0                        /* 1: the host test (tests/rp2350_test.c): the same file on the Mac / Linux */
#endif

/* ---- the build: the FM-1 firmware's switches, as felucca.c sets them, for this board ---- */
#define BOARD_SMK37 0
#define FELUCCA_CDC 0                    /* no FM-1 USB controller here: usb.c is built for its MIDI rings only */
#define FELUCCA_UAC 0
#define FELUCCA_UART 1                   /* MIDI IN: midi_uart.c's parser, fed from xtro's GP0 */
#define FELUCCA_ARRANGER 1
#define FELUCCA_OTA 0                    /* firmware updates are the RP2350's own (BOOTSEL, a .uf2) */
#define FELUCCA_FLASH 1                  /* storage.c over the FS partition */
#define FELUCCA_ICONS 0
#define FELUCCA_VERSION "VIDEODROME 2.8 RP2350"   /* (the editor reads the first number pair as the firmware: SLOOP's) */
#define UI_SMALL

/* felucca.c's words (ui.c, ABOUT): this board's */
#define BRAND_NAME "VIDEODROME"
#define BRAND_BASED "BASED ON SLOOP + FELUCCA"
#define BRAND_URL "GITHUB: LEEMAN1982/SLOOP-FM1"

/* ---- the FM-1's input state, as the sources read it (fm1_input.h): the buttons are xtro's, read by the OLED UI;
 * the FM-1's key matrix is MIDI IN here ---- */
static struct { volatile uint32_t notes, buttons; volatile int32_t wheel[2]; } fm1_in;
static volatile uint32_t fm1_remote_btn;

/* interrupts: core 0's (the audio IRQ, the MIDI tick), nesting counted */
#if VD_HOST
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint32_t vd_time_us(void);
#else
#include <hardware/sync.h>                       /* save_and_disable_interrupts / restore_interrupts (pico-sdk) */
#include <pico/time.h>
static uint32_t vd_irq_depth, vd_irq_saved;
static void fm1_irq_off(void)
{
    uint32_t s = save_and_disable_interrupts();
    if (!vd_irq_depth++)
        vd_irq_saved = s;
}
static void fm1_irq_on(void)
{
    if (vd_irq_depth && !--vd_irq_depth)
        restore_interrupts(vd_irq_saved);
}
static uint32_t vd_time_us(void) { return (uint32_t)time_us_64(); }
#endif
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return vd_time_us() * FM1_TICKS_PER_US; }
static void fm1_delay_ms(uint32_t ms)
{
    uint32_t t0 = vd_time_us();
    while (vd_time_us() - t0 < ms * 1000u)
        ;
}

/* the C++ side (Videodrome.ino; the host test has its own) */
#ifdef __cplusplus
extern "C" {
#endif
uint32_t vd_store_size(void);                          /* bytes of the FS partition (0: none) */
const uint8_t *vd_store_base(void);                    /* its first byte, read through XIP */
int vd_flash_erase(uint32_t off);                      /* 4 KB at off (from the partition's start): 0 = done */
int vd_flash_prog(uint32_t off, const uint8_t *page);  /* 256 B at off (256-aligned): 0 = done */
int vd_midi_tx_done(void);                             /* MIDI OUT (GP1) takes another byte now */
void vd_midi_tx_put(uint8_t b);
#ifdef __cplusplus
}
#endif

/* the FM-1's 1 MB of offsets in the partition (storage.c, upreset.c, the sample slots) */
#define VD_STORE_SPAN 0x100000u
static uint8_t vd_store_ok;                            /* the partition is there and big enough (vd_init) */
static const uint8_t VD_ERASED[256] = {
#define F8 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
#define F64 F8, F8, F8, F8, F8, F8, F8, F8
    F64, F64, F64, F64
#undef F64
#undef F8
};
static const uint8_t *vd_store_xip(uint32_t off)
{
    return vd_store_ok && off < VD_STORE_SPAN ? vd_store_base() + off : VD_ERASED;
}
