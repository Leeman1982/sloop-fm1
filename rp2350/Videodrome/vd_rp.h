/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: the running instrument, the parts of the FM-1's main.c and audio.c this board needs, and
 * the functions Videodrome.ino calls (extern "C"). Who runs what:
 *   core 0, the I2S DMA interrupt   vd_audio_fill: a half buffer rendered in CTL blocks (audio.c fm1_alnk0_irq)
 *   core 0, a 1 kHz timer           vd_tick: the millisecond clock, MIDI out (one message whole or none, a byte as
 *                                   the line takes it: smk_midi_out.h, the SMK-37's TRS out), MIDI in (vd_midi_in)
 *   core 0, loop()                  vd_frame: the UI, autosave, the sections written when quiet (main.c's loop)
 *   core 1, loop1()                 vd_core1_poll: its share of the render (vd_cpu1.h)
 * The MIDI rings, the transport and the voices are the FM-1's (seq.c, usb.c's rings, voice.c), unchanged. */

/* ---- the audio block (audio.c): the mix, the scope tap, the overload guard (two late halves in a row: a voice is
 * shed, voice.c shed_voice), the CPU figure the SYSTEM screen shows ---- */
#define VD_OUT_SHIFT 15                         /* Q15 -> the 32-bit I2S word, -6 dBFS ceiling (audio.c: 24-bit << 7) */
static volatile uint8_t shed_req;
static uint8_t shed_over;
static volatile uint32_t audio_halves, audio_max_us, audio_late;

#ifdef __cplusplus
extern "C" {
#endif

void vd_audio_fill(int32_t *lr, uint32_t frames)
{
    uint32_t b, i, t0 = vd_time_us(), us, half_us = frames * 1000000u / FS;
    if (shed_req) {
        shed_req = 0;
        shed_voice();
    }
    for (b = 0; b + CTL <= frames; b += CTL) {
        int32_t *o = lr + 2u * b;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            if (i & 1u) {
                scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];
                scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
            }
            o[2u * i] = (int32_t)((uint32_t)o[2u * i] << VD_OUT_SHIFT);
            o[2u * i + 1u] = (int32_t)((uint32_t)o[2u * i + 1u] << VD_OUT_SHIFT);
        }
    }
    audio_halves++;
    cpu1_load_half(half_us);
    us = vd_time_us() - t0;
    if (us > audio_max_us)
        audio_max_us = us;
    if (us > half_us)
        audio_late++;
    shed_over = (uint8_t)(shed_over << 1 | (us * 100u > half_us * 85u));
    if ((shed_over & 3u) == 3u)
        shed_req = 1;
    song.cpu_q8 = (song.cpu_q8 * 15u + (us * 256u) / (half_us ? half_us : 1u)) / 16u;
}

/* ---- MIDI ---- */
/* a byte from xtro's MIDI IN (GP0) into midi_uart.c's parser. OCT- / OCT+ (song.octave: on the FM-1, its own keys'
 * octave) transpose a keyboard on MIDI IN here, as xtro's switches do: a note-on is moved by the octave as it comes,
 * and its note-off by what its note-on was moved (the octave may change while it is held: no stuck note). The drums'
 * channel (GLO > MIDI) is played as it comes: its notes are sounds, not pitches */
static uint8_t vm_st, vm_have, vm_note;
static int8_t vm_shift[16][128];
void vd_midi_in(uint8_t b)
{
    if (b >= 0xF8u) {                           /* realtime: through, the state kept */
        um_byte(b);
        return;
    }
    if (b & 0x80u) {
        vm_st = b < 0xF0u ? b : 0;
        vm_have = 0;
        um_byte(b);
        return;
    }
    if ((vm_st & 0xE0u) != 0x80u) {             /* not a note on / off */
        um_byte(b);
        return;
    }
    if (!vm_have) {                             /* the note: held until its velocity says on or off */
        vm_note = b;
        vm_have = 1;
        return;
    }
    vm_have = 0;
    {
        uint32_t ch = vm_st & 15u, on = (vm_st & 0xF0u) == 0x90u && b;
        int32_t sh;
        if (on) {
            sh = (int32_t)ch + 1 == song.g[G_DRCH] ? 0 : 12 * song.octave;
            while (vm_note + sh > 127)
                sh -= 12;
            while (vm_note + sh < 0)
                sh += 12;
            vm_shift[ch][vm_note] = (int8_t)sh;
        } else {
            sh = vm_shift[ch][vm_note];
        }
        um_byte((uint32_t)(vm_note + sh));
        um_byte(b);
    }
}
/* the 1 kHz tick (core 0): the clock, MIDI out a few bytes at the line's pace. -> GP14's level: xtro's click / gate out,
 * high for 5 ms on every beat while the sequencer plays (seq.c clk_beat, the beat its metronome counts) */
int vd_tick(uint32_t ms)
{
    static uint32_t beat_last = 0xFFFFFFFFu, gate_until;
    uint32_t i;
    fm1_ms = ms;
    if (song.playing && clk_beat != beat_last) {
        beat_last = clk_beat;
        gate_until = ms + 5u;
    } else if (!song.playing) {
        beat_last = 0xFFFFFFFFu;
    }
    while (mo_r != mo_w) {                      /* usb.c's out ring (seq.c's notes, clock): into the byte ring */
        smo_put(midi_out_q[mo_r % MQ]);
        mo_r++;
    }
    for (i = 0; i < 4u; i++)                    /* (31250 baud: ~3 bytes a millisecond) */
        smo_tick();
    return (int32_t)(gate_until - ms) > 0;
}

/* ---- core 1 ---- */
void vd_core1_poll(void) { vd_c1_poll(); }

/* ---- the boot (main.c fm1_main, felucca_init), a line of the log a stage ---- */
static void felucca_init(void)                  /* main.c's, as it is */
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.sel = 0;
    song.master_q12 = 2048;
    autosave_resume();
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];
    song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4], song.g[G_BEND] = midi_set[5];
    song.g[G_FXCH] = midi_set[6];
    song.g[G_DRMAP] = midi_set[7];
    song.g[G_CLKOUT] = midi_set[9];
    song.g[G_RMCH] = midi_set[8];
    song.g[G_FXFADE] = fxfade;
    layers_init();
    go_home();
    ui.force = 1;
}
void vd_ui_attach(u8g2_t *u, uint32_t mhz)
{
    vu.u = u;
    vu.mhz = mhz;
    memset(vu.shown, 0xA5, sizeof vu.shown);   /* (every row sent once) */
}
void vd_boot(const char *done, const char *res, const char *next) { vd_boot_log(done, res, next); }
/* the synth up (core 1 already in loop1(): the self-test); the I2S starts after (Videodrome.ino) */
void vd_init(void)
{
    vd_boot_log(0, 0, "CARRIER");
    vd_store_ok = vd_store_size() >= VD_STORE_SPAN;
    persist_boot();                             /* the settings, the song, the sample slots (project.c) */
    settings_init();
    panel_init();
    vd_boot_log("STORAGE", flash_ok ? "OK" : "RAM", "ENGINES");
    felucca_init();
    song.master_q12 = 2896;                     /* (no MASTER pot on xtro's panel: -3 dB from full; MIXER > MASTER) */
    vd_boot_log("ENGINES", "OK", "SECOND CORE");
    cpu1_boot();
    vd_boot_log("SECOND CORE", cpu1_state == CPU1_OK ? "OK" : "SKIP", "AUDIO");
    usb.config = 1;                             /* (the DIN jack is this board's MIDI port: the out ring fills as on an
                                                 * FM-1 with a computer; usb.c's controller itself is never started) */
    uart_midi_init();
}
/* the main loop's frame (main.c): the FM-1's per-frame duties, then the OLED. -> rows of the panel still to send */
uint32_t vd_frame(const vd_ctl_t *c)
{
    while (fm1_ms - vu.t_timer >= 16u) {        /* the FM-1's frame timers run at ~60 Hz (ui_timers) */
        vu.t_timer += 16u;
        ui_timers();
    }
    pc_poll();                                  /* MIDI program change: the PRESETS list (ui.c) */
    ml_poll();                                  /* MIDI LEARN */
    fm6_poll();
    midi_chan_fix();
    vd_input(c);
    autosave_tick();                            /* the working project into flash, when quiet (project.c) */
    sections_flush();                           /* live sections, the song */
    if (!vu.u)
        return 0;
    vd_draw();
    return vd_ui_push(2);
}

#ifdef __cplusplus
}
#endif
