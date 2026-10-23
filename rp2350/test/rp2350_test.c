/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350, on the host: rp2350/Videodrome/vd_core.c as the sketch builds it (the FM-1's sources from
 * fw/, the OLED UI, the runtime), against U8g2's real C core on a 128 x 64 full buffer (so the screens are the panel's
 * pixels), a RAM stand-in for the 2 MB FS partition, and xtro's controls as vd_frame gets them. Checks the boot log, the
 * audio (a note from MIDI IN sounds, the drums play, MIDI out and the click out follow the clock), every screen and
 * what its controls do (through the FM-1's own functions), storage (a section stored, the unit "restarted", loaded
 * back), then a fuzz of random controls and MIDI. Writes each screen as DIR/NAME.pbm.
 *   cc -I U8G2_CLIB rp2350/test/rp2350_test.c U8G2 sources -lm; ./a.out OUT_DIR       (rp2350/test/run.sh) */
#define VD_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "u8g2.h"
static uint32_t host_us;                                 /* the test's clock: it moves when the test says */
static uint32_t vd_time_us(void) { return host_us; }
#include "../Videodrome/vd_core.c"

/* ---- the board's side ---- */
#define STORE (2u << 20)
static uint8_t store[STORE];
static uint32_t erases, progs;
uint32_t vd_store_size(void) { return STORE; }
const uint8_t *vd_store_base(void) { return store; }
int vd_flash_erase(uint32_t off) { if (off & 4095u || off + 4096u > STORE) return -8; memset(store + off, 0xFF, 4096); erases++; return 0; }
int vd_flash_prog(uint32_t off, const uint8_t *p)
{
    uint32_t i;
    if (off & 255u || off + 256u > STORE)
        return -8;
    for (i = 0; i < 256u; i++)
        store[off + i] &= p[i];                          /* (NOR flash: bits only go from 1 to 0) */
    progs++;
    return 0;
}
static uint8_t mtx[4096];
static uint32_t mtx_n;
int vd_midi_tx_done(void) { return 1; }
void vd_midi_tx_put(uint8_t b) { if (mtx_n < sizeof mtx) mtx[mtx_n++] = b; }

static uint8_t u8x8_noop_byte(u8x8_t *u, uint8_t m, uint8_t a, void *p) { (void)u, (void)m, (void)a, (void)p; return 1; }
static uint8_t u8x8_noop_gpio(u8x8_t *u, uint8_t m, uint8_t a, void *p) { (void)u, (void)m, (void)a, (void)p; return 1; }

static int fails;
static void ok(int c, const char *what) { printf("rp2350: %-70s %s\n", what, c ? "ok" : "FAIL"); fails += !c; }
static const char *outdir;
static u8g2_t U;
static void pbm(const char *name)
{
    char path[512];
    FILE *f;
    uint8_t *b = u8g2_GetBufferPtr(&U);
    int x, y;
    if (!outdir)
        return;
    snprintf(path, sizeof path, "%s/%s.pbm", outdir, name);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputs((b[(y / 8) * 128 + x] >> (y & 7)) & 1 ? "1 " : "0 ", f);
        fputc('\n', f);
    }
    fclose(f);
}
static int lit(void)
{
    uint8_t *b = u8g2_GetBufferPtr(&U);
    int i, n = 0;
    for (i = 0; i < 1024; i++)
        n += __builtin_popcount(b[i]);
    return n;
}
/* the instrument running for ms milliseconds: the 1 kHz tick, the audio a half buffer at a time (5.8 ms), a frame
 * of the UI every 16 ms; -> the loudest sample */
static int32_t run(uint32_t ms, const vd_ctl_t *c)
{
    static uint32_t now_ms, audio_due, frame_due;
    static int32_t lr[2 * 256];
    int32_t peak = 0;
    uint32_t end = now_ms + ms, i;
    vd_ctl_t idle = {0};
    idle.pot[0] = idle.pot[1] = -1;
    while (now_ms < end) {
        now_ms++;
        host_us = now_ms * 1000u;
        vd_tick(now_ms);
        while (audio_due <= now_ms * 1000u) {
            vd_audio_fill(lr, 256);
            for (i = 0; i < 512u; i++) {
                int32_t v = lr[i] >> 15;
                if (v < 0)
                    v = -v;
                if (v > peak)
                    peak = v;
            }
            audio_due += 256u * 1000000u / 44100u;
        }
        if (now_ms >= frame_due) {
            frame_due = now_ms + 16u;
            vd_frame(c ? c : &idle);
            c = 0;
        }
    }
    return peak;
}
static vd_ctl_t ctl_none(void) { vd_ctl_t c = {0}; c.pot[0] = c.pot[1] = -1; return c; }
static void press(uint32_t b, uint32_t held)
{
    vd_ctl_t c = ctl_none();
    c.pressed = (uint8_t)(1u << b);
    c.held = (uint8_t)held;
    run(20, &c);
}
static void turn(int32_t d, uint32_t held)
{
    vd_ctl_t c = ctl_none();
    c.enc = d;
    c.held = (uint8_t)held;
    run(20, &c);
}
static void push(int lng, uint32_t held)
{
    vd_ctl_t c = ctl_none();
    c.push = (uint8_t)!lng;
    c.push_long = (uint8_t)lng;
    c.held = (uint8_t)held;
    run(20, &c);
}
static void midi(const uint8_t *b, uint32_t n) { uint32_t i; for (i = 0; i < n; i++) vd_midi_in(b[i]); }
static void go_screen(uint32_t s)
{
    uint32_t k;
    for (k = 0; k < VS_COUNT && vu.scr != s; k++)
        press(VB_PAGE, 0);
}
#define SH (1u << VB_SHIFT)

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : 0;
    memset(store, 0xFF, sizeof store);
    u8g2_Setup_ssd1309_i2c_128x64_noname0_f(&U, U8G2_R0, u8x8_noop_byte, u8x8_noop_gpio);
    u8g2_InitDisplay(&U);

    /* ---- the boot: the log, a stage a line, the bar to the end, on the air */
    vd_ui_attach(&U, 250);
    vd_init();
    pbm("boot-log");
    ok(vd_boot_n == 3 && !strcmp(vd_boot_prev, "SECOND CORE") && !strcmp(vd_boot_res, "OK") && flash_ok,
       "boot: CARRIER .. SECOND CORE logged, the store found, core 1 answered");
    vd_boot("AUDIO", "OK", "MIDI");
    vd_boot("MIDI", "OK", "SIGNAL");
    vd_boot("SIGNAL", "ACQUIRED", 0);
    pbm("boot-air");
    ok(vd_boot_n == 6 && lit() > 400, "boot: SIGNAL ACQUIRED, the wordmark and the bar drawn");
    ok(cpu1_state == CPU1_OK && cpu1_units != 0, "the second core: its self-test answered, the render shares units with it");

    /* ---- sound: a note from MIDI IN on the selected track; the drums; MIDI out; the click */
    run(200, 0);
    {
        static const uint8_t on[3] = {0x90, 60, 110}, off[3] = {0x80, 60, 0};
        int32_t quiet = run(100, 0), loud;
        midi(on, 3);
        loud = run(300, 0);
        midi(off, 3);
        ok(quiet < 50 && loud > 2000, "MIDI IN: a note on channel 1 plays the selected track (silent before)");
        ok(last_note == 60, "MIDI IN: the note is the step entry's (seq.c last_note)");
    }
    {   /* OCT+: a keyboard on MIDI IN an octave up; its note-off follows its note-on even after OCT- */
        static const uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0};
        press(VB_OCTUP, 0);
        midi(on, 3);
        run(30, 0);
        ok(song.octave == 1 && last_note == 72, "OCT+: MIDI IN notes an octave up (as xtro's switches)");
        press(VB_OCTDN, 0);
        midi(off, 3);
        run(400, 0);
        {
            uint32_t k, held = 0;
            for (k = 0; k < NVOICE; k++)
                held += TSEL->v[k].active && TSEL->v[k].stage < 3u;
            ok(song.octave == 0 && held == 0, "OCT-: the note held through it still ends (its note-off moved as it was)");
        }
    }
    run(1500, 0);

    /* ---- the screens: each drawn, each with its own content */
    {
        static const char *const N[VS_COUNT] = {"scr-pattern", "scr-sound", "scr-presets", "scr-mixer", "scr-song", "scr-system", "scr-help"};
        int seen[VS_COUNT] = {0};
        for (i = 0; i < VS_COUNT; i++) {
            run(40, 0);
            pbm(N[vu.scr]);
            seen[vu.scr] = lit();
            press(VB_PAGE, 0);
        }
        ok(vu.scr == VS_PATTERN, "PAGE: the seven screens round, back to PATTERN");
        for (i = 0; i < VS_COUNT; i++)
            if (seen[i] < 120) {
                printf("  (screen %s: %d pixels)\n", N[i], seen[i]);
                break;
            }
        ok(i == VS_COUNT, "every screen draws its header, content and footer");
        press(VB_PAGE, SH);
        ok(vu.scr == VS_HELP, "SHIFT+PAGE: the screen before");
        press(VB_PAGE, 0);
    }

    /* ---- PATTERN: a step set from the cursor with the last note played, cleared again; SHIFT+turn its note */
    {
        track_t *t = TSEL;
        uint8_t n0 = last_note;
        turn(3, 0);
        ok(ui.cursor == 3, "PATTERN: the encoder moves the cursor");
        push(0, 0);
        ok(step_on(&t->step[3]) && t->step[3].note[0] == n0, "PATTERN: push sets the step, with the last note from MIDI IN");
        turn(2, SH);
        ok(t->step[3].note[0] == n0 + 2, "PATTERN: SHIFT+turn moves the step's note");
        pbm("pattern-step");
        push(0, 0);
        ok(!step_on(&t->step[3]), "PATTERN: push again clears it");
        press(VB_OCTDN, SH);
        ok(step_on(&t->step[3]), "SHIFT+OCT-: undo (the step back)");
        press(VB_OCTUP, SH);
        ok(!step_on(&t->step[3]), "SHIFT+OCT+: redo");
    }
    /* ---- the drums: TRACK to track 4, a hit of the picked sound */
    {
        press(VB_TRACK, 0);
        press(VB_TRACK, 0);
        press(VB_TRACK, 0);
        ok(song.sel == 3 && is_drum(TSEL), "TRACK: to the drum track");
        turn(-30, 0);
        turn(4, 0);
        push(0, 0);
        ok(dstep_has(&TSEL->dstep[4], drum_lane), "PATTERN, drums: push sets the picked sound's hit");
        turn(1, SH);
        push(0, 0);
        ok(dstep_mask(&TSEL->dstep[4]) && dstep_has(&TSEL->dstep[4], drum_lane), "PATTERN, drums: SHIFT+turn picks another sound, a second hit");
        pbm("pattern-drums");
        press(VB_TRACK, 0);
        ok(song.sel == 0, "TRACK: round to track 1");
    }
    /* ---- SOUND: the list, an edit through edit_param, a page jump */
    {
        int16_t before;
        uint32_t r, pg, k;
        int16_t *vp;
        go_screen(VS_SOUND);
        vd_rows_build();
        ok(vd_rows_n > 40, "SOUND: the track's pages as one list (EDIT, ENV, LFO, FX, ARP, SEL, PATTERN, GLOBAL, TOOLS)");
        r = vd_rows[vu.sound_row];
        pg = r >> 2;
        k = r & 3u;
        page_desc(&PAGES[pg], k, &vp);
        before = *vp;
        push(0, 0);
        ok(vu.edit, "SOUND: push edits the row");
        turn(5, 0);
        pbm("sound-edit");
        ok(*vp != before, "SOUND: turning edits the value (edit_param on the row's page)");
        push(0, 0);
        turn(1, SH);
        ok(vd_rows[vu.sound_row] >> 2 != pg, "SOUND: SHIFT+turn jumps to the next page");
    }
    /* ---- PRESETS: turn loads the next, SHIFT+turn the next engine */
    {
        uint32_t total, before;
        uint8_t eng = TSEL->eng_req;
        go_screen(VS_PRESETS);
        before = preset_pos(&total);
        turn(1, 0);
        ok(preset_pos(&total) != before, "PRESETS: turning loads the next sound (preset_go)");
        turn(1, SH);
        ok(TSEL->eng_req != eng, "PRESETS: SHIFT+turn the next engine (select_engine)");
        pbm("presets");
    }
    /* ---- MIXER: the level of a track; mute and solo; MASTER */
    {
        int16_t lv = trk[1].p[P_LEVEL];
        uint32_t m = song.master_q12;
        go_screen(VS_MIXER);
        turn(1, 0);
        push(0, 0);
        turn(-10, 0);
        ok(trk[1].p[P_LEVEL] == lv - 10, "MIXER: push, turn: track 2's level");
        push(0, 0);
        press(VB_MUTE, 0);
        ok(trk[1].p[P_MUTE] != 0, "MIXER: MUTE mutes the cursor's track");
        press(VB_MUTE, SH);
        ok((song.solo >> 1) & 1u, "MIXER: SHIFT+MUTE solos it");
        pbm("mixer");
        press(VB_MUTE, 0);
        press(VB_MUTE, SH);
        turn(10, 0);
        push(0, 0);
        turn(-4, 0);
        ok(vu.mix_sel == NTRK && song.master_q12 == m - 256u, "MIXER: MASTER, the fifth column");
        push(0, 0);
    }
    /* ---- SONG: a section stored, the chain edited, song mode */
    {
        go_screen(VS_SONG);
        turn(-20, 0);
        push(0, SH);
        ok(project_used(0) || live_sec == 0, "SONG: SHIFT+push stores the loop as section A");
        turn(4, 0);
        push(0, 0);
        turn(1, 0);
        turn(4, SH);
        push(0, 0);
        ok(arrangement.entry[0].scene == 1 && arrangement.entry[0].bars == 8, "SONG: a chain step's section and bars");
        pbm("song");
    }
    /* ---- PLAY / STOP, SHIFT+PLAY records; the click and MIDI clock follow the sequencer */
    {
        uint32_t clicks = 0, t0;
        press(VB_PLAY, 0);
        run(50, 0);
        ok(song.playing, "PLAY starts");
        song.g[G_CLKOUT] = 1;                            /* GLO > MIDI 3 > CLOCK OUT */
        mtx_n = 0;
        for (t0 = 0; t0 < 2000u; t0++) {
            static int was;
            int g;
            host_us += 1000u;
            g = vd_tick(host_us / 1000u + 100000u);
            clicks += g && !was;
            was = g;
            if (t0 % 6u == 0u) {
                static int32_t lr[512];
                vd_audio_fill(lr, 256);
            }
        }
        {
            uint32_t f8 = 0;
            for (i = 0; i < mtx_n; i++)
                f8 += mtx[i] == 0xF8u;
            ok(f8 > 20u, "MIDI OUT (GP1): the clock, 24 a beat, while it plays");
        }
        ok(clicks >= 2u, "GP14: the click out, a pulse a beat");
        press(VB_PLAY, 0);
        run(50, 0);
        ok(!song.playing, "PLAY again stops");
    }
    /* ---- the pots: the engine's own CUT / RES, as xtro's pots reach the live engine's */
    {
        vd_ctl_t c = ctl_none();
        uint32_t cut = vd_pot_param(TSEL, 0);
        c.pot[0] = 0;
        run(20, &c);
        c = ctl_none();
        c.pot[0] = 4095;
        run(20, &c);
        ok(cut != 0xFFu && TSEL->p[cut] == track_desc(TSEL, cut)->max, "CUTOFF pot: the selected track's cutoff, all the way");
    }
    /* ---- storage: the working project into the partition, read back after a restart */
    {
        uint32_t e0 = erases;
        song.g[G_BPM] = 133;
        transport_req = 0;
        song.playing = 0;
        run(30000, 0);
        ok(erases > e0, "storage: the project written into the partition when quiet (autosave)");
        vd_init();                                       /* (a restart: the RAM's state from the flash) */
        ok(song.g[G_BPM] == 133, "storage: after a restart, the project as it was left (the FM-1's format)");
    }
    /* ---- fuzz: random controls and MIDI for 20000 frames: the screens stay on the panel, nothing hangs */
    {
        uint32_t seed = 83u, n;
        for (n = 0; n < 20000u; n++) {
            vd_ctl_t c = ctl_none();
            uint8_t m[3];
            seed = seed * 1103515245u + 12345u;
            c.enc = (int32_t)((seed >> 8) % 7u) - 3;
            c.pressed = (uint8_t)((seed >> 16) % 13u == 0u ? 1u << ((seed >> 20) % 7u) : 0u);
            c.held = (uint8_t)((seed >> 24) & 1u ? SH : 0u);
            c.push = (uint8_t)((seed >> 12) % 11u == 0u);
            c.push_long = (uint8_t)((seed >> 4) % 97u == 0u);
            c.pot[0] = (seed >> 3) % 17u == 0u ? (int16_t)((seed >> 5) & 4095u) : -1;
            m[0] = (uint8_t)(0x80u | ((seed >> 9) & 0x1Fu));
            m[1] = (uint8_t)((seed >> 14) & 127u);
            m[2] = (uint8_t)((seed >> 21) & 127u);
            if ((seed >> 7) % 5u == 0u)
                midi(m, 3);
            if (c.pressed & (1u << VB_PLAY) && (seed & 3u))
                c.pressed = 0;                           /* (mostly not the transport) */
            run(16, &c);
        }
        ok(1, "fuzz: 20000 frames of random controls and MIDI");
    }
    printf("%s\n", fails ? "RP2350 TESTS FAILED" : "rp2350 tests passed");
    return fails ? 1 : 0;
}
