/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: the second core, as cpu1.c has it on the FM-1 (where felucca.c includes cpu1.c). The
 * render (fx.c mix_units) hands core 1 its share of the units each block through cpu1.h's cpu1_run / cpu1_wait; here
 * the handover is a pair of counters: core 0 publishes a job and counts it in c1.go, core 1's loop1() (Videodrome.ino,
 * vd_core1_poll) runs it and counts it in c1.done. One job at a time, as the FM-1's. A job core 1 does not finish in
 * VD_C1_TIMEOUT_US holds it for good (one core from then on: cpu1_state CPU1_FAILED), as cpu1.c does. Proved at
 * boot with cpu1.c's self-test (the same LCG). HOME menu > SYSTEM > 2ND CORE: cpu1_off (panel.c), as on the FM-1. */
#define FELUCCA_CPU1 1
enum { CPU1_OFF, CPU1_NOSTART, CPU1_FAILED, CPU1_OK };
static uint8_t cpu1_state = CPU1_OFF;
static uint8_t cpu1_off;                                 /* (panel.c: the menu's 2ND CORE, read from the settings) */
static uint32_t cpu1_timeouts;
#define VD_C1_TIMEOUT_US 3000u                           /* (a block's units take well under a millisecond) */
static struct {
    volatile cpu1_job_t fn;
    volatile uint32_t arg, go, done;
} c1;
static volatile uint32_t cpu1_test_out;

static int cpu1_on(void) { return cpu1_state == CPU1_OK && !cpu1_off; }
static uint32_t cpu1_ticks(void) { return fm1_ticks(); }
static int cpu1_run(cpu1_job_t fn, uint32_t arg)
{
    if (cpu1_state != CPU1_OK)
        return 0;
    c1.fn = fn;
    c1.arg = arg;
    __sync_synchronize();                                /* the job before its count (core 1 reads in that order) */
    c1.go = c1.go + 1u;
    return 1;
}
static void vd_c1_poll(void);
static int cpu1_wait(void)
{
    uint32_t t0 = vd_time_us();
#if VD_HOST
    vd_c1_poll();                                        /* (the host test: core 1's job runs here, as cpu1_host.c's
                                                          * CORES=1: deterministic) */
#endif
    while (c1.done != c1.go)
        if (vd_time_us() - t0 > VD_C1_TIMEOUT_US) {
            cpu1_timeouts++;
            cpu1_state = CPU1_FAILED;                    /* held: never handed a job again */
            return -1;
        }
    __sync_synchronize();                                /* (what the job wrote, before the caller reads it) */
    return 0;
}
/* core 1's side: loop1() calls it over and over */
static void vd_c1_poll(void)
{
    uint32_t g = c1.go;
    if (g == c1.done)
        return;
    __sync_synchronize();
    c1.fn(c1.arg);
    __sync_synchronize();
    c1.done = g;
}

static uint32_t cpu1_lcg(uint32_t x)                     /* cpu1.c's self-test job: 64 steps of an LCG */
{
    uint32_t i;
    for (i = 0; i < 64u; i++)
        x = x * 1664525u + 1013904223u;
    return x;
}
static void cpu1_selftest(uint32_t arg) { cpu1_test_out = cpu1_lcg(arg); }
/* at boot, core 1 already in loop1(): one job, its answer checked */
static void cpu1_boot(void)
{
    cpu1_units = cpu1_off ? 0 : -1;
    cpu1_state = CPU1_OK;                                /* (for the test job only, until it answers) */
    cpu1_test_out = 0;
    if (!cpu1_run(cpu1_selftest, 20261010u) || cpu1_wait() != 0 || cpu1_test_out != cpu1_lcg(20261010u)) {
        cpu1_state = cpu1_state == CPU1_FAILED ? CPU1_NOSTART : CPU1_FAILED;
        return;
    }
    cpu1_state = CPU1_OK;
}
static const char *cpu1_label(void) { return cpu1_state != CPU1_OK ? "1 CORE" : cpu1_units ? "2 CORES" : "C1 OFF"; }
#define cpu1_cores cpu1_label
static void cpu1_supplies(char *b) { str_cpy(b, "RP2350", 12); }   /* (the FM-1's ABOUT shows its two supplies) */
