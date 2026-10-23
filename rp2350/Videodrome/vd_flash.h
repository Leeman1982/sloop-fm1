/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: felucca.c's flash glue for storage.c, upreset.c and project.c, over the sketch's FS
 * partition (vd_port.h). The FM-1's offsets and its allow-list are kept (fw/hal/fm1_flash_map.h: FL_STORE_OK), so the
 * objects land where the FM-1 puts them, 1 MB of offsets at the partition's start, in the same format. */
#include "fw/hal/fm1_flash_map.h"

static uint32_t irq_save(void) { fm1_irq_off(); return 0; }
static void irq_restore(uint32_t f) { (void)f; fm1_irq_on(); }
#define FL_FAR(fn) (fn)
static uint32_t fl_jedec_ram(void) { return vd_store_ok ? 0x856014u : 0u; }   /* (persist_boot: "the part is there") */
static void fl_plain_window_init(void) {}

static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (!vd_store_ok || off > VD_STORE_SPAN || n > VD_STORE_SPAN - off)
        return -1;
    memcpy(dst, vd_store_base() + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (!vd_store_ok || !FL_STORE_OK(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return vd_flash_erase(off);
}
/* n bytes at off: whole 256-byte pages, 0xFF where nothing is written (programming 0xFF leaves a byte as it is) */
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    static uint8_t page[256];
    const uint8_t *s = (const uint8_t *)src;
    if (!vd_store_ok || !FL_STORE_OK(off, n))
        return -8;
    while (n) {
        uint32_t base = off & ~255u, at = off - base, k = 256u - at < n ? 256u - at : n;
        int rc;
        memset(page, 0xFF, sizeof page);
        memcpy(page + at, s, k);
        rc = vd_flash_prog(base, page);
        if (rc)
            return rc;
        if (memcmp(vd_store_base() + off, s, k))
            return -9;                                  /* (read back: what was written is there) */
        off += k;
        s += k;
        n -= k;
    }
    return 0;
}
#define ST_XIP_PTR(off) fm1_xip_ptr(off)
