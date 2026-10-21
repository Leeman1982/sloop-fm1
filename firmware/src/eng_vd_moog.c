/* SPDX-License-Identifier: GPL-3.0-only */
/* MODEL D: the Minimoog Model D (1970-81), after xtro's minimoog engine (GPLv3), rebuilt at component level.
 *
 *   three VCOs, free-running (no key ever resets a Model D oscillator: each note starts wherever the cores are),
 *   their six shapes as the waveform switch gives them: triangle, the "shark tooth" (osc 1 / 2: the triangle-saw that
 *   rises over 3/4 of the cycle; osc 3, on the same switch position, a reverse saw), saw, square, the wide and the
 *   narrow rectangles (70 % / 15 %). Osc 3 sits an octave below (16'), detuned the other way: the classic bass stack.
 *   -> the MIXER, whose summing amp overdrives the filter input when the levels go past the middle: DRV is that
 *   overload, the "fat" of a Model D (Moog's own patch books turn the levels up on purpose)
 *   -> the 4-pole TRANSISTOR LADDER (Moog's 1966 patent): four rungs, each a differential pair charging its capacitor
 *   with tanh(in) - tanh(out) (Huovilainen's model of the rung: the transistors, not a generic one-pole), the 4th
 *   rung fed back inverted (EMPHASIS). The rungs bound the loop: past k ~ 4 it sings a sine on its own, with no input
 *   (SELF-OSC), and the passband sinks as the emphasis rises (1 / (1 + k)): the Moog bass loss, kept.
 *   -> the VCA. KEYBOARD CONTROL: the two filter switches (1/3, 2/3: both = full tracking).
 *   CONTOURS: attack / decay / sustain, no release knob: on key-up a Model D's contour falls at its DECAY rate if the
 *   DECAY switch is on, else at once (~10 ms). REL here is that switch: 0 OFF, else ON (its value unused).
 *   AMOUNT OF CONTOUR is positive only on the panel: ENV > FLT below 0 does nothing (as on the instrument).
 * EDIT: OSC {WAVE, DTN (osc 2 up / osc 3 down, cents), OSC3 (its level), NOIS}, FLT {CUT, EMPH, DRV, KBD} */
static const char *const N_VDM_WAVE[] = {"TRI", "SHARK", "SAW", "SQUARE", "WIDE", "NARROW"};
static const char *const N_VDM_KBD[] = {"OFF", "1/3", "2/3", "FULL"};
#define VDM_FAST_REL 22u                                  /* ENV_EXP index: ~10 ms, the DECAY switch OFF */

static inline int32_t vdm_wave(uint32_t w, uint32_t ph, uint32_t inc, int osc3)
{
    switch (w) {
    case 0:
        return osc_tri(ph);
    case 1: {
        int32_t x = (int32_t)(ph >> 16);
        if (osc3)                                         /* osc 3's switch position 2: REV SAW */
            return -osc_saw(ph, inc);
        return x < 49152 ? -32768 + ((x * 87381) >> 16) : 32767 - ((x - 49152) << 2);
    }
    case 3:
        return osc_pulse(ph, inc, 0x80000000u);
    case 4:
        return osc_pulse(ph, inc, 0xB3333333u);           /* 70 % */
    case 5:
        return osc_pulse(ph, inc, 0x26666666u);           /* 15 % */
    default:
        return osc_saw(ph, inc);
    }
}

static void vdm_note_on(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MODELD);
    if (!s)
        return;
    if (!s->card) {
        vd_card(s, t, v, ENGI_MODELD, 4, 96);
        s->ph[0] = noise32(&s->nz);                       /* the cores were running before the first key */
        s->ph[1] = noise32(&s->nz);
        s->ph[2] = noise32(&s->nz);
    }
    vd_trigger(s, v);
}

static int vdm_done(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MODELD);
    const uint32_t *ek = vd_ek(t);
    const int16_t *p = t->p;
    uint32_t rel;
    if (!s)
        return 1;
    rel = p[P_REL] ? ek[1] : ENV_EXP[VDM_FAST_REL];
    vd_contour(&s->aenv, v->gate, (int32_t)p[P_SUS] << 17, ek[0], ek[1], rel, VDA_MOOG);
    vd_contour(&s->fenv, v->gate, (int32_t)p[P_SUS] << 17, ek[0], ek[1], rel, VDA_MOOG);
    vd_drift(s, 5);
    return !s->aenv.st;
}

static int32_t vdm_amp(track_t *t, voice_t *v, int32_t env)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MODELD);
    return s ? vd_amp_of(s, env) : 0;
}

static void vdm_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint16_t KBD[4] = {0, 85, 171, 256};
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MODELD);
    const int16_t *p = t->p;
    uint32_t w = (uint32_t)p[P_E0] % 6u, i, inc1, inc2, inc3, ph1, ph2, ph3;
    int32_t det, l3 = p[P_E2] * 258, ln = p[P_E3] * 200, drive, u, fe;
    int32_t nst, y2;
    vd_zdf4_t Z;
    vd_cas4_t L;
    if (!s)
        return;
    /* the cores: the card's trims and its thermal walk, osc 2 DTN cents up, osc 3 an octave down and half of it down */
    det = p[P_E1] * 237 / 100;
    inc1 = vd_detune(m->inc, s->tol_pitch[0] + s->drift);
    inc2 = vd_detune(m->inc, s->tol_pitch[1] + s->drift + det);
    inc3 = vd_detune(m->inc >> 1, -(s->drift >> 1) - det / 2 - s->tol_pitch[0]);
    /* the ladder's cutoff: the knob, the LFO (m->cutoff: no ADSR on an own-contour engine), the contour (positive
     * only), the keyboard switches and the card's capacitor */
    fe = p[P_ED_FLT] > 0 ? ((s->fenv.lvl >> 9) * p[P_ED_FLT]) >> 7 : 0;
    u = (p[P_E4] << 8) + m->cutoff + fe + VD_KTRK(m->pitch16, KBD[p[P_E7] & 3]) + s->tol_cut;
    vd_zdf4_coef(&Z, u, p[P_E5] * 161);                 /* EMPHASIS: k 0 .. 5.0 (it sings from ~4, EMPH 104) */
    drive = 2048 + p[P_E6] * 96;                          /* the mixer into the ladder: x0.5 .. x3.5, Q12 */
    ph1 = s->ph[0];
    ph2 = s->ph[1];
    ph3 = s->ph[2];
    memcpy(&L, s->f, sizeof L);
    nst = s->nz;
    for (i = 0; i < n; i++) {
        int32_t x = (vdm_wave(w, ph1, inc1, 0) + vdm_wave(w, ph2, inc2, 0)) >> 1, y;
        if (l3)
            x += mulq15(vdm_wave(w, ph3, inc3, 1), l3) >> 1;
        if (ln)
            x += mulq15((int32_t)(noise32(&nst) >> 16) - 32768, ln) >> 1;
        ph1 += inc1;
        ph2 += inc2;
        ph3 += inc3;
        y = vd_cas4(&Z, &L, (x * drive) >> 12, VDZ_LADDER, &y2);
        out[i] += VD_OUT(vd_dc(&s->f[11], y + (y >> 1)), m, i, 40);
    }
    s->ph[0] = ph1;
    s->ph[1] = ph2;
    s->ph[2] = ph3;
    memcpy(s->f, &L, sizeof L);
    s->nz = nst;
}

static const preset_t VDM_PRESETS[] = {
    /* {WAVE, DTN, OSC3, NOIS, CUT, EMPH, DRV, KBD}, {A D S R(0: DECAY switch off)}, contour amount, mono */
    {"MOOG BASS", {2, 7, 90, 0, 30, 60, 70, 2}, {0, 62, 70, 0}, 52, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"SUB BASS", {0, 0, 110, 0, 22, 30, 50, 1}, {0, 70, 100, 0}, 30, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    {"ACID BASS", {3, 0, 0, 0, 30, 118, 60, 2}, {0, 52, 0, 0}, 63, 1, FX(18, 0, 18, 6),
     XP(P_GLIDE + 1, 20, P_GLMODE + 1, 1, P_TRANS + 1, -12)},
    {"FUNK BASS", {3, 9, 70, 0, 42, 80, 64, 2}, {0, 46, 50, 0}, 50, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"GROWL BASS", {2, 18, 64, 0, 24, 100, 100, 1}, {0, 70, 80, 1}, 45, 1, FX(10, 0, 0, 6), XP(P_TRANS + 1, -24)},
    {"MOOG LEAD", {2, 12, 0, 0, 84, 90, 70, 3}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 30, 24),
     XP(P_GLIDE + 1, 34, P_LD_PIT + 1, 2, P_LRATE + 1, 82, P_LFADE + 1, 60)},
    {"TB LEAD", {3, 0, 0, 0, 55, 110, 60, 3}, {0, 60, 70, 30}, 45, 1, FX(0, 0, 28, 20), XP(P_GLIDE + 1, 14)},
    {"FIFTH LEAD", {2, 127, 0, 0, 75, 60, 60, 3}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 26, 24)},
    {"SCREAM", {2, 6, 0, 0, 70, 124, 110, 3}, {0, 70, 110, 40}, 24, 1, FX(20, 0, 30, 26), XP(P_GLIDE + 1, 26)},
    {"MOOG BRASS", {2, 10, 50, 0, 40, 30, 80, 2}, {30, 70, 100, 40}, 50, 1, FX(0, 0, 10, 26)},
    {"SOLO SAX", {5, 4, 0, 10, 52, 40, 76, 3}, {16, 60, 100, 30}, 36, 1, FX(0, 0, 22, 30),
     XP(P_LD_PIT + 1, 2, P_LRATE + 1, 80, P_LFADE + 1, 70, P_GLIDE + 1, 20)},
    {"SELF-OSC", {0, 0, 0, 0, 80, 127, 0, 3}, {0, 70, 110, 50}, 0, 1, FX(0, 0, 30, 40)},
    {"NOISE SWP", {0, 0, 0, 127, 10, 100, 60, 0}, {60, 100, 100, 60}, 63, 1, FX(0, 0, 30, 60)},
    {"LONG LIVE", {1, 9, 80, 0, 26, 74, 96, 1}, {0, 86, 90, 1}, 40, 1, FX(12, 0, 16, 20), XP(P_TRANS + 1, -12)},
    {"MAX RENN", {2, 5, 100, 0, 34, 50, 88, 2}, {2, 60, 80, 1}, 34, 1, FX(0, 0, 12, 10),
     XP(P_GLIDE + 1, 30, P_GLMODE + 1, 1, P_TRANS + 1, -12)},
    {"SEQ PLUCK", {2, 7, 0, 0, 30, 70, 60, 2}, {0, 40, 0, 1}, 58, 1, FX(0, 0, 30, 20)},
};

static const engine_t ENG_MODELD = {
    "MODEL D", {"OSC", "LADDER"},
    {
        {"WAVE", F_ENUM, 0, 5, 2, N_VDM_WAVE, 0},
        {"DTN", F_INT, 0, 127, 8, 0, "ct"},
        {"OSC3", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 50, 0, 0},
        {"EMPH", F_PCT, 0, 127, 40, 0, 0},
        {"DRV", F_PCT, 0, 127, 64, 0, 0},
        {"KBD", F_ENUM, 0, 3, 2, N_VDM_KBD, 0},
    },
    VDM_PRESETS, sizeof(VDM_PRESETS) / sizeof(VDM_PRESETS[0]), 1, vdm_note_on, vdm_render,
    0xFD20, {P_E4, P_E5, P_E6, P_DEC}, 1, vdm_amp, 0, 0, 1, vdm_done,
};
