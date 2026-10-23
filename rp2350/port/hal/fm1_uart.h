/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: no FM-1 UART1. MIDI IN is xtro's (GP0, a PIO UART): its bytes go to midi_uart.c's parser
 * (um_byte) from the 1 kHz MIDI tick (Videodrome.ino); the FM-1's DMA ring stays empty. Replaces firmware/hal/fm1_uart.h
 * in the sketch's copy (tools/rp2350_sync.py). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

FM1_INLINE void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len) { (void)ring; (void)len; }
FM1_INLINE uint32_t fm1_uart1_rx_take(void) { return 0; }
