/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: the FM-1's flash offsets (its storage objects, user presets and samples, 0..1 MB) read
 * through the RP2350's XIP window, at the start of the sketch's FS partition (vd_port.h vd_store_xip). Replaces
 * firmware/hal/fm1_xip.h in the sketch's copy (tools/rp2350_sync.py). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

static const uint8_t *vd_store_xip(uint32_t off);   /* vd_port.h */
FM1_INLINE const uint8_t *fm1_xip_ptr(uint32_t off) { return vd_store_xip(off); }
