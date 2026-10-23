/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: what Videodrome.ino calls in the synth (vd_core.c) and what the synth calls back. */
#pragma once
#include <stdint.h>
#if defined(__has_include) && __has_include("clib/u8g2.h")
#include "clib/u8g2.h"                  /* (the Arduino library) */
#else
#include "u8g2.h"                       /* (the host test: U8g2's C core) */
#endif

enum { VB_PLAY, VB_SHIFT, VB_PAGE, VB_TRACK, VB_MUTE, VB_OCTDN, VB_OCTUP };   /* (vd_ui.h's button bits) */
typedef struct {
    int32_t enc;                 /* detents since the last frame */
    uint8_t push, push_long;     /* the encoder's push: a short press, a long one (600 ms) */
    uint8_t pressed;             /* buttons pressed since the last frame (bit VB_*) */
    uint8_t held;                /* buttons down now */
    int16_t pot[2];              /* 0..4095, -1: not moved past its deadband since the last frame */
} vd_ctl_t;

void vd_ui_attach(u8g2_t *u, uint32_t mhz);
void vd_boot(const char *done, const char *res, const char *next);
void vd_init(void);
void vd_audio_fill(int32_t *lr, uint32_t frames);
int vd_tick(uint32_t ms);
void vd_midi_in(uint8_t b);
void vd_core1_poll(void);
uint32_t vd_frame(const vd_ctl_t *c);

/* the board's side (Videodrome.ino) */
uint32_t vd_store_size(void);
const uint8_t *vd_store_base(void);
int vd_flash_erase(uint32_t off);
int vd_flash_prog(uint32_t off, const uint8_t *page);
int vd_midi_tx_done(void);
void vd_midi_tx_put(uint8_t b);
