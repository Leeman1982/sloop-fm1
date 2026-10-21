/* SPDX-License-Identifier: GPL-3.0-only */
/* P-5: the Sequential Circuits Prophet-5 rev 3 (1980-84), after xtro's prophet5 engine (GPLv3), at component level.
 *
 *   five voice cards (five voices here too: the engine caps the part at 5), each two CEM3340 VCOs: OSC A saw and pulse at once
 *   (the panel has level-free switches, not a selector: S+P plays both), OSC B saw, triangle or pulse, and the sync:
 *   A's core reset each time B's wraps (the comparator's reset line, a real hard sync: SYN SAW, SYN S+P). PW: both
 *   pulse comparators' threshold (LFO / ENV > SHP move it).
 *   POLY-MOD, the Prophet's own: the FILTER ENVELOPE into OSC A's frequency (PM ENV: the sync sweep) and OSC B into the
 *   filter's cutoff at audio rate (PM OSB: the growl), always on, never through the wheel.
 *   -> the CEM3320 four-pole: Curtis' variable-gain cells, each a current-mode integrator whose input pair saturates
 *   (tanh at the cell's input, a linear capacitor after it: the rev 3 sound, rounder than a ladder, which saturates
 *   both ends of every rung). The resonance VCA feeds the 4th cell back inverted; it self-oscillates past k ~ 4 and,
 *   with no gain compensation on the rev 3 board, the low end thins as RES rises (kept). KEYBOARD: half (the knob at
 *   noon, the usual patch).
 *   -> the CEM3360 VCA, the CEM3310 contours (ADSR both, the attack an RC charge toward 1.5x: concave, as measured on
 *   the chip). The rev 3's cards kept close by auto-tune: +-3 cents and a small drift.
 * EDIT: OSC {OSC A, OSC B, B PIT (semitones), PW}, VCF {CUT, RES, PM ENV, PM OSB} */
static const char *const N_VDP_A[] = {"SAW", "PULSE", "S+P", "SYN SAW", "SYN S+P"};
static const char *const N_VDP_B[] = {"SAW", "TRI", "PULSE", "S+P", "OFF"};

static void vdp_note_on(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_P5);
    if (!s)
        return;
    if (!s->card) {
        vd_card(s, t, v, ENGI_P5, 3, 64);
        s->ph[0] = noise32(&s->nz);
        s->ph[1] = noise32(&s->nz);
    }
    vd_trigger(s, v);
}

static int vdp_done(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_P5);
    const uint32_t *ek = vd_ek(t);
    int32_t sus = (int32_t)t->p[P_SUS] << 17;
    if (!s)
        return 1;
    vd_contour(&s->aenv, v->gate, sus, ek[0], ek[1], ek[2], VDA_CEM);
    vd_contour(&s->fenv, v->gate, sus, ek[0], ek[1], ek[2], VDA_CEM);
    vd_drift(s, 3);
    return !s->aenv.st;
}

static int32_t vdp_amp(track_t *t, voice_t *v, int32_t env)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_P5);
    return s ? vd_amp_of(s, env) : 0;
}

static void vdp_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_P5);
    const int16_t *p = t->p;
    uint32_t wa = (uint32_t)p[P_E0] % 5u, wb = (uint32_t)p[P_E1] % 5u, i, inca, incb, pha, phb, pw, ratio = 0;
    int32_t fe, u, k, pmo = p[P_E7], d16, y2;
    vd_zdf4_t Z;
    vd_cas4_t C;
    int sync = wa >= 3u;
    if (!s)
        return;
    fe = ((s->fenv.lvl >> 9) * p[P_ED_FLT]) >> 7;          /* the filter contour, cutoff units */
    /* OSC B: B PIT semitones; OSC A: the note, plus POLY-MOD's filter contour (up to 4 octaves) */
    incb = vd_detune(vd_inc_at(m, p[P_E2] * 16), s->tol_pitch[1] + s->drift);
    d16 = ((s->fenv.lvl >> 9) * p[P_E6] * 3) >> 14;       /* 0 .. 48 semitones in 1/16 */
    inca = vd_detune(d16 ? vd_inc_at(m, d16) : m->inc, s->tol_pitch[0] + s->drift);
    if (sync && incb >> 8)
        ratio = inca / (incb >> 8);                       /* A's increment per B's, Q8 (once a tick) */
    pw = (uint32_t)clamp(128 + (p[P_E3] - 64) + ((m->shape - (64 << 8)) >> 8), 13, 243) << 24;   /* 5 % .. 95 % */
    u = (p[P_E4] << 8) + m->cutoff + fe + VD_KTRK(m->pitch16, 128) + s->tol_cut;
    k = p[P_E5] * 161;                                    /* RES: 0 .. 5.0, Q12 (it sings from ~4) */
    vd_zdf4_coef(&Z, u, k);
    pha = s->ph[0];
    phb = s->ph[1];
    memcpy(&C, s->f, sizeof C);
    for (i = 0; i < n; i++) {
        int32_t a, b, x;
        uint32_t nb = phb + incb;
        /* OSC B first: its wrap resets A's core (sync) */
        switch (wb) {
        case 1: b = osc_tri(phb); break;
        case 2: b = osc_pulse(phb, incb, pw); break;
        case 3: b = (osc_saw(phb, incb) + osc_pulse(phb, incb, pw)) >> 1; break;
        case 4: b = 0; break;
        default: b = osc_saw(phb, incb); break;
        }
        switch (wa) {
        case 1: a = osc_pulse(pha, inca, pw); break;
        case 2: case 4: a = (osc_saw(pha, inca) + osc_pulse(pha, inca, pw)) >> 1; break;
        default: a = osc_saw(pha, inca); break;
        }
        pha += inca;
        if (sync && nb < phb)                             /* B wrapped: A's core restarts where B is now */
            pha = (nb >> 8) * ratio;
        phb = nb;
        x = (a + b) >> 1;
        if (pmo) {                                        /* POLY-MOD OSC B -> the cutoff, at audio rate */
            int32_t ob = wb == 4u ? osc_saw(phb, incb) : b;
            Z.G = vd_gexp(u + ((ob * pmo) >> 9)) << 13;   /* (+-2.3 octaves at 127): the cells' gain (Q28) moved at audio
                                                            * rate, the feedback solve stays the tick's */
        }
        out[i] += VD_OUT(vd_dc(&s->f[11], vd_cas4(&Z, &C, (x * 3) >> 2, VDZ_CEM, &y2) << 1), m, i, 40);
    }
    s->ph[0] = pha;
    s->ph[1] = phb;
    memcpy(s->f, &C, sizeof C);
}

static const preset_t VDP_PRESETS[] = {
    /* {OSC A, OSC B, B PIT, PW, CUT, RES, PM ENV, PM OSB}, {A D S R}, ENV > FLT, mono */
    {"P5 BRASS", {0, 0, 0, 64, 38, 20, 0, 0}, {28, 70, 96, 40}, 40, 0, FX(0, 20, 10, 30)},
    {"SYNC LEAD", {3, 4, 0, 64, 70, 30, 70, 0}, {0, 70, 100, 40}, 20, 1, FX(0, 0, 30, 24), XP(P_GLIDE + 1, 24)},
    {"POLY BASS", {0, 2, -12, 50, 30, 50, 0, 0}, {0, 56, 60, 30}, 46, 0, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"DEEP BASS", {2, 0, -12, 64, 22, 30, 0, 0}, {0, 66, 90, 20}, 30, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"GROWL BASS", {0, 0, -12, 64, 26, 60, 0, 40}, {0, 60, 80, 20}, 40, 1, FX(10, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"P5 PAD", {0, 0, 0, 64, 50, 10, 0, 0}, {80, 90, 110, 90}, 12, 0, FX(0, 50, 20, 60),
     XP(P_LRATE + 1, 26, P_LD_PIT + 1, 1)},
    {"GLASS PAD", {1, 1, 12, 40, 70, 40, 0, 0}, {70, 90, 100, 90}, 10, 0, FX(0, 50, 30, 70),
     XP(P_LRATE + 1, 40, P_LD_SHP + 1, 24)},
    {"PWM STRING", {1, 2, 0, 50, 64, 10, 0, 0}, {60, 80, 110, 80}, 8, 0, FX(0, 60, 20, 60),
     XP(P_LRATE + 1, 46, P_LD_SHP + 1, 40)},
    {"P5 STAB", {0, 0, 7, 64, 36, 40, 0, 0}, {0, 50, 20, 30}, 50, 0, FX(0, 20, 26, 30)},
    {"E.PIANO", {1, 1, 12, 20, 40, 10, 0, 0}, {0, 80, 40, 50}, 30, 0, FX(0, 30, 10, 30)},
    {"CLAV FUNK", {1, 4, 0, 24, 46, 30, 0, 0}, {0, 44, 0, 20}, 56, 0, FX(0, 0, 10, 10)},
    {"BELL PLUCK", {0, 1, 19, 64, 60, 20, 30, 50}, {0, 70, 0, 60}, 30, 0, FX(0, 20, 30, 50)},
    {"CIVIC TV", {4, 2, 7, 30, 44, 54, 56, 20}, {0, 80, 60, 50}, 36, 0, FX(0, 30, 28, 40)},
    {"NEW FLESH", {3, 0, -5, 64, 30, 70, 90, 60}, {40, 90, 90, 70}, 40, 0, FX(16, 30, 30, 60),
     XP(P_LRATE + 1, 20, P_LD_FLT + 1, 16)},
    {"RESO LEAD", {0, 4, 0, 64, 60, 112, 0, 0}, {0, 70, 110, 40}, 30, 1, FX(0, 0, 30, 24), XP(P_GLIDE + 1, 20)},
    {"FILT SWEEP", {0, 0, 7, 64, 14, 90, 0, 0}, {90, 100, 110, 80}, 63, 0, FX(0, 40, 30, 60)},
};

static const engine_t ENG_P5 = {
    "P-5", {"OSC", "CEM3320"},
    {
        {"OSC A", F_ENUM, 0, 4, 0, N_VDP_A, 0},
        {"OSC B", F_ENUM, 0, 4, 0, N_VDP_B, 0},
        {"B PIT", F_SEMI, -24, 24, 0, 0, 0},
        {"PW", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 60, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"PM ENV", F_PCT, 0, 127, 0, 0, 0},
        {"PM OSB", F_PCT, 0, 127, 0, 0, 0},
    },
    VDP_PRESETS, sizeof(VDP_PRESETS) / sizeof(VDP_PRESETS[0]), 1, vdp_note_on, vdp_render,
    0xFFE0, {P_E4, P_E5, P_E6, P_ATK}, 5, vdp_amp, 0, 0, 1, vdp_done,
};
