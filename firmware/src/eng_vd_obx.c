/* SPDX-License-Identifier: GPL-3.0-only */
/* OB-X: the Oberheim OB-X (1979-81), after xtro's obx engine (GPLv3), at component level.
 *
 *   discrete voice cards, two VCOs each: saw, pulse or both; VCO-2 can be hard-synced to VCO-1 (SYN SAW, SYN PUL: the
 *   OB-X's sync direction) and CROSS MOD runs VCO-2 into VCO-1's frequency at audio rate. V2 PIT: VCO-2's interval.
 *   -> the SEM state-variable filter, 12 dB/oct, the voice card's own (the OB-Xa moved to the CEM3320; the OB-X kept
 *   Tom Oberheim's SEM circuit): two CA3080 OTA integrators in a loop, low-pass out. A trapezoidal SVF for the loop,
 *   and the OTAs' limited output current as a soft limit on the band-pass integrator: at high RES the SEM swells and
 *   rings but its damping never reaches zero (k >= 0.12): a whistle, not a sine of its own.
 *   -> the VCA, two ADSR contours (filter, amp).
 *   SLOP: the cards. An OB-X was eight hand-built boards: their VCO trims, filter capacitors and VCA gains differ, and
 *   the auto-tune trims only the pitch. 0 = eight identical cards (no real OB-X), 127 = a tired one: up to +-16 cents
 *   and a third of an octave of cutoff between voices; the slow drift grows with it.
 * EDIT: VCO {VCO1, VCO2, V2 PIT, PW}, SEM {CUT, RES, XMOD, SLOP} */
static const char *const N_VDO_1[] = {"SAW", "PULSE", "S+P"};
static const char *const N_VDO_2[] = {"SAW", "PULSE", "S+P", "SYN SAW", "SYN PUL"};

static void vdo_note_on(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_OBX);
    if (!s)
        return;
    if (!s->card) {
        vd_card(s, t, v, ENGI_OBX, 100, 128);             /* the full spread, scaled by SLOP at render */
        s->ph[0] = noise32(&s->nz);
        s->ph[1] = noise32(&s->nz);
    }
    vd_trigger(s, v);
}

static int vdo_done(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_OBX);
    const uint32_t *ek = vd_ek(t);
    int32_t sus = (int32_t)t->p[P_SUS] << 17;
    if (!s)
        return 1;
    vd_contour(&s->aenv, v->gate, sus, ek[0], ek[1], ek[2], VDA_CEM);
    vd_contour(&s->fenv, v->gate, sus, ek[0], ek[1], ek[2], VDA_CEM);
    vd_drift(s, 2 + t->p[P_E7] / 16);
    return !s->aenv.st;
}

static int32_t vdo_amp(track_t *t, voice_t *v, int32_t env)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_OBX);
    return s ? vd_amp_of(s, env) : 0;
}

/* the OTA's limited current: linear while small, bounded near +-2 */
static inline int32_t vdo_ota(int32_t x) { return vd_tanh(x >> 1) << 1; }

/* the SEM: a trapezoidal SVF (tsvf's Q13 coefficients, dsp.c) whose band-pass integrator is an OTA's limited
 * current; the low-pass out */
typedef vd_svf_t vdo_coef_t;
static void vdo_coefs(vdo_coef_t *c, int32_t u, int32_t res) { vd_svf_coef(c, u, 8192 - res * 7700 / 127); }   /* 2 .. 0.12 */
static inline __attribute__((always_inline)) int32_t vdo_sem(const vdo_coef_t *c, int32_t x, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = (x << 8) - *ic2;                       /* (Q23 integrators, as vd_svf) */
    int32_t v1 = (int32_t)(((int64_t)c->a1 * *ic1 + (int64_t)c->a2 * v3 + (1 << 27)) >> 28);
    int32_t v2 = *ic2 + (int32_t)(((int64_t)c->a2 * *ic1 + (int64_t)c->a3 * v3 + (1 << 27)) >> 28);
    v1 = vdo_ota(v1 >> 8) << 8;                          /* the OTA's limited current */
    *ic1 = clamp(2 * v1 - *ic1, -(1 << 26), 1 << 26);
    *ic2 = clamp(2 * v2 - *ic2, -(1 << 26), 1 << 26);
    return v2 >> 8;
}

static void vdo_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_OBX);
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)p[P_E0] % 3u, w2 = (uint32_t)p[P_E1] % 5u, i, inc1, inc2, ph1, ph2, pw, ratio = 0;
    int32_t fe, u, ic1, ic2, xm = p[P_E6], slop = p[P_E7];
    vdo_coef_t C;
    int sync = w2 >= 3u;
    if (!s)
        return;
    fe = ((s->fenv.lvl >> 9) * p[P_ED_FLT]) >> 7;
    /* the card at this SLOP: its trims (drawn for 100 cents) scaled to 0 .. 16 cents, its capacitor to 0 .. 1/3 oct */
    inc1 = vd_detune(m->inc, s->tol_pitch[0] * slop * 16 / 12700 + s->drift);
    inc2 = vd_detune(vd_inc_at(m, p[P_E2] * 16), s->tol_pitch[1] * slop * 16 / 12700 + s->drift);
    if (sync && inc1 >> 8)
        ratio = inc2 / (inc1 >> 8);
    pw = (uint32_t)clamp(128 + (p[P_E3] - 64) + ((m->shape - (64 << 8)) >> 8), 13, 243) << 24;
    u = (p[P_E4] << 8) + m->cutoff + fe + VD_KTRK(m->pitch16, 128) + s->tol_cut * slop * 74 / 1000;
    vdo_coefs(&C, u, p[P_E5]);                           /* the SEM loop, once a tick */
    ph1 = s->ph[0];
    ph2 = s->ph[1];
    ic1 = s->f[0], ic2 = s->f[1];
    for (i = 0; i < n; i++) {
        int32_t a, b, x;
        uint32_t n1, i1 = inc1;
        switch (w2) {
        case 1: case 4: b = osc_pulse(ph2, inc2, pw); break;
        case 2: b = (osc_saw(ph2, inc2) + osc_pulse(ph2, inc2, pw)) >> 1; break;
        default: b = osc_saw(ph2, inc2); break;
        }
        if (xm)                                           /* CROSS MOD: VCO-2 into VCO-1 (linear FM) */
            i1 = (uint32_t)clamp((int32_t)inc1 + (int32_t)(inc1 >> 15) * ((b * xm) >> 7), 0, 0x7FFFFFFF);
        switch (w1) {
        case 1: a = osc_pulse(ph1, i1, pw); break;
        case 2: a = (osc_saw(ph1, i1) + osc_pulse(ph1, i1, pw)) >> 1; break;
        default: a = osc_saw(ph1, i1); break;
        }
        n1 = ph1 + i1;
        ph2 += inc2;
        if (sync && n1 < ph1)                             /* VCO-1 wrapped: VCO-2's core restarts */
            ph2 = (n1 >> 8) * ratio;
        ph1 = n1;
        x = (a + b) >> 2;
        out[i] += voice_amp(vd_dc(&s->f[11], vdo_sem(&C, x, &ic1, &ic2) << 1), m, i);
    }
    s->ph[0] = ph1;
    s->ph[1] = ph2;
    s->f[0] = ic1, s->f[1] = ic2;
}

static const preset_t VDO_PRESETS[] = {
    /* {VCO1, VCO2, V2 PIT, PW, CUT, RES, XMOD, SLOP}, {A D S R}, ENV > FLT, mono */
    {"OBX BRASS", {0, 0, 0, 64, 36, 30, 0, 50}, {20, 70, 100, 40}, 46, 0, FX(0, 30, 10, 30)},
    {"JUMP STAB", {0, 0, 0, 64, 40, 20, 0, 50}, {0, 60, 70, 34}, 40, 0, FX(0, 40, 20, 36)},
    {"1-FINGER", {0, 0, 7, 64, 40, 30, 0, 50}, {10, 70, 96, 40}, 40, 0, FX(0, 30, 20, 40)},
    {"PUNCH BASS", {1, 0, -12, 50, 28, 40, 0, 40}, {0, 50, 60, 20}, 50, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"FAT BASS", {0, 0, 0, 64, 30, 30, 0, 80}, {0, 60, 90, 20}, 40, 1, FX(0, 0, 0, 4),
     XP(P_VOICE + 1, 3, P_DETUNE + 1, 30, P_TRANS + 1, -12)},
    {"SYNC LEAD", {0, 3, 12, 64, 70, 30, 0, 40}, {0, 70, 110, 40}, 30, 1, FX(0, 0, 30, 24), XP(P_GLIDE + 1, 20)},
    {"XMOD LEAD", {0, 1, 7, 40, 64, 40, 50, 40}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 30, 24)},
    {"UNISON LD", {0, 0, 0, 64, 70, 30, 0, 90}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 30, 24),
     XP(P_VOICE + 1, 3, P_DETUNE + 1, 40, P_GLIDE + 1, 24)},
    {"STRING PAD", {1, 1, 0, 40, 60, 10, 0, 60}, {70, 80, 112, 80}, 8, 0, FX(0, 60, 16, 60),
     XP(P_LRATE + 1, 40, P_LD_SHP + 1, 36)},
    {"WARM PAD", {0, 0, 0, 64, 46, 10, 0, 70}, {80, 90, 112, 90}, 10, 0, FX(0, 60, 20, 66)},
    {"HORROR STB", {0, 2, 6, 30, 50, 70, 30, 100}, {0, 80, 40, 70}, 40, 0, FX(20, 30, 30, 70)},
    {"HAUNTED", {1, 0, 1, 20, 40, 50, 0, 127}, {60, 90, 100, 90}, 20, 0, FX(0, 50, 30, 80),
     XP(P_LRATE + 1, 14, P_LD_PIT + 1, 2)},
    {"VIDEODROME", {2, 3, 7, 50, 34, 64, 40, 90}, {30, 90, 90, 80}, 50, 0, FX(16, 40, 30, 70),
     XP(P_LRATE + 1, 22, P_LD_FLT + 1, 12)},
    {"BIANCA", {0, 1, 12, 40, 56, 20, 0, 60}, {40, 90, 100, 80}, 16, 0, FX(0, 50, 26, 66)},
    {"CHORD STAB", {0, 0, 0, 64, 44, 20, 0, 50}, {0, 40, 0, 30}, 56, 0, FX(0, 30, 30, 40), XP(P_CHORD + 1, 1)},
    {"BELL PLUCK", {1, 0, 24, 20, 60, 30, 70, 40}, {0, 70, 0, 60}, 30, 0, FX(0, 20, 30, 50)},
};

static const engine_t ENG_OBX = {
    "OB-X", {"VCO", "SEM"},
    {
        {"VCO1", F_ENUM, 0, 2, 0, N_VDO_1, 0},
        {"VCO2", F_ENUM, 0, 4, 0, N_VDO_2, 0},
        {"V2 PIT", F_SEMI, -24, 24, 0, 0, 0},
        {"PW", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 60, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"XMOD", F_PCT, 0, 127, 0, 0, 0},
        {"SLOP", F_PCT, 0, 127, 50, 0, 0},
    },
    VDO_PRESETS, sizeof(VDO_PRESETS) / sizeof(VDO_PRESETS[0]), 1, vdo_note_on, vdo_render,
    0x07FF, {P_E4, P_E5, P_E6, P_E7}, 0, vdo_amp, 0, 0, 1, vdo_done,
};
