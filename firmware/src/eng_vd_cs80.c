/* SPDX-License-Identifier: GPL-3.0-only */
/* CS-80: the Yamaha CS-80 (1977-80), after xtro's cs80 engine (GPLv3), at component level.
 *
 *   two complete channels a voice, I and II, both sounding every note (the pairing IS the CS-80, not a layer mode).
 *   Each: a VCO saw core whose reset comparator fires a ~30 us one-shot at every wrap (left un-band-limited: a real
 *   discontinuity, the CS-80's brightness), the WAVE SHAPE CONVERTER (saw, pulse after the comparator, sine shaped
 *   from the core, or saw + pulse), channel II at II PIT and the panel's DETUNE (+6 cents).
 *   -> the IG00156 filter pair: a resonant 12 dB HIGH-PASS then a resonant 12 dB LOW-PASS, both the CS damping law:
 *   resonance comes from less damping, never from feedback around the loop, and the damping's floor rises as the filter
 *   opens, so it cannot self-oscillate and grows smoother the brighter it is (where a ladder would scream)
 *   -> the fixed single pole at ~7.6 kHz the voice board puts on everything -> the VCA (ADSR).
 *   The VCF contour is the CS-80's own: INITIAL LEVEL, ATTACK LEVEL, attack, decay, release. It sits at IL when the key
 *   goes down (IL below 0: under the panel setting), travels to AL (ENV > FLT) in the attack time, falls back to the
 *   panel setting (0) in the decay and returns to IL in the release: the "blown" Vangelis brass is IL < 0 < AL.
 *   INITIAL TOUCH: the velocity opens both filters (brilliance) and sets the level, the one keyboard of the six that
 *   had it (and polyphonic aftertouch, which SLOOP has no source for).
 * EDIT: VCO {I WAVE, II WAVE, II PIT, BAL}, IG00156 {CUT, RES, HPF, IL} */
static const char *const N_VDC_W[] = {"SAW", "PULSE", "S+P", "SINE"};
#define VDC_POLE 21661                                    /* 1 - e^(-2 pi 7600 / 44100), Q15 */
#define VDC_DETUNE 14                                     /* channel II's DETUNE: +6 cents, 1/4096 */

static void vdc_note_on(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_CS80);
    if (!s)
        return;
    if (!s->card) {
        vd_card(s, t, v, ENGI_CS80, 3, 64);
        s->ph[0] = noise32(&s->nz);
        s->ph[1] = noise32(&s->nz);
    }
    if (!s->fenv.st && !s->aenv.st)
        s->aux[2] = t->p[P_E7] * 254 * 256;              /* the VCF contour starts at IL (Q8 cutoff units) */
    vd_trigger(s, v);
}

/* the VCF contour: IL -> AL (attack, an RC toward 1.5x of the way, stopped at AL) -> 0 (decay) ... -> IL (release) */
static void vdc_vcf(vd_voice_t *s, int gate, int32_t il, int32_t al, const uint32_t *ek)
{
    int32_t *x = &s->aux[2];
    vd_env_t *e = &s->fenv;
    if (!gate && (e->st == 1u || e->st == 2u))
        e->st = 3;
    switch (e->st) {
    case 1: {
        uint32_t k = (uint32_t)(((uint64_t)ek[0] * 281u) >> 16);
        int32_t top = il + ((al - il) * 3 >> 1);
        if (k > 65535u)
            k = 65535u;
        *x += mulq16(top - *x, k);
        if (al >= il ? *x >= al : *x <= al) {
            *x = al;
            e->st = 2;
        }
        break;
    }
    case 2:
        *x -= mulq16(*x, ek[1]);
        break;
    case 3:
        *x += mulq16(il - *x, ek[2]);
        if (!s->aenv.st)
            e->st = 0;
        break;
    default:
        break;
    }
}

static int vdc_done(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_CS80);
    const uint32_t *ek = vd_ek(t);
    const int16_t *p = t->p;
    if (!s)
        return 1;
    vd_contour(&s->aenv, v->gate, (int32_t)p[P_SUS] << 17, ek[0], ek[1], ek[2], VDA_CEM);
    vdc_vcf(s, v->gate, p[P_E7] * 254 * 256, p[P_ED_FLT] * 254 * 256, ek);
    vd_drift(s, 4);
    return !s->aenv.st;
}

static int32_t vdc_amp(track_t *t, voice_t *v, int32_t env)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_CS80);
    return s ? vd_amp_of(s, env) : 0;
}

/* the CS damping law: k from RES, never below a floor that rises with the cutoff (no self-oscillation; smoother as
 * it opens). The SVF itself: vd_svf (vd_parts.c) */
typedef vd_svf_t vdc_svf_t;
static void vdc_coef(vdc_svf_t *c, int32_t u, int32_t res)
{
    int32_t floor_k = 1100 + clamp(u, 0, 140 << 8) / 16, k = 8192 - res * 6400 / 127;   /* 2.0 .. 0.44, Q12 */
    vd_svf_coef(c, u, k < floor_k ? floor_k : k);
}
/* one step: *lp the low-pass, returns the high-pass (in - k bp - lp) */
static inline __attribute__((always_inline)) int32_t vdc_svf(const vdc_svf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, int32_t *lp)
{
    int32_t v1 = vd_svf(c, in, ic1, ic2, lp);
    return in - ((c->k * v1) >> 12) - *lp;
}

/* the wave shape converter's output from a saw core (ph, inc), the pulse width pw */
static inline int32_t vdc_wsc(uint32_t w, uint32_t ph, uint32_t inc, uint32_t pw)
{
    switch (w) {
    case 1: return osc_pulse(ph, inc, pw);
    case 2: return (osc_saw(ph, inc) + osc_pulse(ph, inc, pw)) >> 1;
    case 3: return osc_sine(ph);
    default: return osc_saw(ph, inc);
    }
}

static void vdc_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_CS80);
    const int16_t *p = t->p;
    uint32_t wa = (uint32_t)p[P_E0] & 3u, wb = (uint32_t)p[P_E1] & 3u, i, inca, incb, pha, phb, pw;
    int32_t u, uh, bri, la = (127 - p[P_E3]) * 258, lb = p[P_E3] * 258, ga, gb;
    int32_t h1a, h2a, l1a, l2a, h1b, h2b, l1b, l2b, pa, hpf_on = p[P_E6] > 0;
    vdc_svf_t hp, lp;
    if (!s)
        return;
    bri = (s->vel - 64) * 40;                             /* INITIAL TOUCH: brilliance */
    inca = vd_detune(m->inc, s->tol_pitch[0] + s->drift);
    incb = vd_detune(vd_inc_at(m, p[P_E2] * 16), s->tol_pitch[1] + s->drift + VDC_DETUNE);
    pw = (uint32_t)clamp(128 + ((m->shape - (64 << 8)) >> 8), 13, 243) << 24;
    u = (p[P_E4] << 8) + m->cutoff + (s->aux[2] >> 8) + VD_KTRK(m->pitch16, 128) + bri + s->tol_cut;
    uh = (p[P_E6] << 8) + m->cutoff + (s->aux[2] >> 8) + VD_KTRK(m->pitch16, 128) + bri;
    vdc_coef(&lp, u, p[P_E5]);
    vdc_coef(&hp, uh, 40);                                /* the HPF's own resonance: a mild peak */
    pha = s->ph[0];
    phb = s->ph[1];
    h1a = s->f[0], h2a = s->f[1], l1a = s->f[2], l2a = s->f[3];
    h1b = s->f[4], h2b = s->f[5], l1b = s->f[6], l2b = s->f[7];
    pa = s->f[8];
    ga = s->aux[0], gb = s->aux[1];
    for (i = 0; i < n; i++) {
        int32_t a, b, d;
        uint32_t na = pha + inca, nb = phb + incb;
        /* the cores and their reset one-shots: ~1.3 samples of +0.45, the part past this sample carried to the next */
        a = vdc_wsc(wa, pha, inca, pw) + ga;
        b = vdc_wsc(wb, phb, incb, pw) + gb;
        ga = na < pha && wa != 3u ? 14746 : ga > 4600 ? 4600 : 0;
        gb = nb < phb && wb != 3u ? 14746 : gb > 4600 ? 4600 : 0;
        pha = na;
        phb = nb;
        /* each channel: the IG00156 high-pass (at its lowest setting it passes everything a voice makes: left out),
         * then its low-pass */
        a >>= 1;
        b >>= 1;
        if (hpf_on) {
            a = vdc_svf(&hp, a, &h1a, &h2a, &d);
            b = vdc_svf(&hp, b, &h1b, &h2b, &d);
        }
        vdc_svf(&lp, a, &l1a, &l2a, &a);
        vdc_svf(&lp, b, &l1b, &l2b, &b);
        /* the fixed ~7.6 kHz pole, the same on both channels (linear: on their mix, one pole for two) */
        pa += vd_step(VDC_POLE, mulq15(a, la) + mulq15(b, lb) - pa);
        out[i] += voice_amp(vd_dc(&s->f[11], pa), m, i);
    }
    s->ph[0] = pha;
    s->ph[1] = phb;
    s->f[0] = h1a, s->f[1] = h2a, s->f[2] = l1a, s->f[3] = l2a;
    s->f[4] = h1b, s->f[5] = h2b, s->f[6] = l1b, s->f[7] = l2b;
    s->f[8] = pa;
    s->aux[0] = ga, s->aux[1] = gb;
}

static const preset_t VDC_PRESETS[] = {
    /* {I WAVE, II WAVE, II PIT, BAL, CUT, RES, HPF, IL}, {A D S R}, AL (ENV > FLT), mono */
    {"BLADE BRSS", {0, 0, 0, 64, 34, 40, 0, -20}, {16, 80, 100, 50}, 40, 0, FX(0, 40, 20, 60)},
    {"TEARS RAIN", {1, 3, 12, 50, 46, 30, 10, -10}, {40, 90, 100, 90}, 24, 0, FX(0, 60, 30, 80),
     XP(P_LRATE + 1, 30, P_LD_SHP + 1, 30)},
    {"CS STRINGS", {1, 1, 0, 64, 56, 10, 20, 0}, {60, 80, 112, 80}, 10, 0, FX(0, 60, 16, 60),
     XP(P_LRATE + 1, 44, P_LD_SHP + 1, 40)},
    {"CS BRASS", {0, 1, 0, 60, 38, 50, 0, -24}, {10, 70, 96, 40}, 44, 0, FX(0, 30, 10, 40)},
    {"YAMAHA PAD", {0, 3, -12, 40, 50, 20, 0, 0}, {80, 90, 112, 90}, 10, 0, FX(0, 60, 20, 70)},
    {"SINE ORGAN", {3, 3, 12, 50, 100, 0, 0, 0}, {4, 60, 120, 20}, 0, 0, FX(0, 40, 0, 30)},
    {"BELL", {3, 0, 19, 30, 70, 20, 30, 0}, {0, 80, 0, 70}, 30, 0, FX(0, 30, 30, 60)},
    {"CS BASS", {0, 1, -12, 50, 28, 50, 0, -10}, {0, 60, 70, 20}, 40, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"CS LEAD", {2, 0, 0, 64, 60, 60, 0, -10}, {0, 70, 110, 40}, 30, 1, FX(0, 0, 30, 30),
     XP(P_GLIDE + 1, 30, P_LD_PIT + 1, 2, P_LRATE + 1, 80, P_LFADE + 1, 60)},
    {"HARPSICH", {1, 1, 12, 50, 70, 20, 40, 0}, {0, 60, 0, 50}, 30, 0, FX(0, 20, 10, 30)},
    {"HUMAN VOX", {2, 3, 7, 70, 40, 70, 30, -10}, {40, 80, 100, 70}, 20, 0, FX(0, 50, 20, 70),
     XP(P_LRATE + 1, 60, P_LD_PIT + 1, 1)},
    {"RANNOCH", {0, 0, 0, 64, 30, 50, 10, -40}, {50, 100, 90, 90}, 50, 0, FX(0, 50, 30, 80)},
    {"SIGNAL", {1, 2, 5, 60, 44, 60, 20, -30}, {30, 90, 80, 90}, 40, 0, FX(10, 50, 40, 80),
     XP(P_LRATE + 1, 12, P_LD_FLT + 1, 10)},
    {"TV STATIC", {0, 1, 1, 64, 80, 30, 60, 20}, {0, 50, 60, 40}, -20, 0, FX(20, 20, 30, 50)},
    {"WARM BRASS", {0, 0, -12, 40, 32, 30, 0, -16}, {20, 80, 100, 50}, 36, 0, FX(0, 40, 16, 60)},
    {"GLIDE LEAD", {0, 0, 7, 64, 56, 40, 0, 0}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 30, 30), XP(P_GLIDE + 1, 50)},
};

static const engine_t ENG_CS80 = {
    "CS-80", {"VCO", "IG00156"},
    {
        {"I WAVE", F_ENUM, 0, 3, 0, N_VDC_W, 0},
        {"II WAVE", F_ENUM, 0, 3, 0, N_VDC_W, 0},
        {"II PIT", F_SEMI, -24, 24, 0, 0, 0},
        {"BAL", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 50, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"HPF", F_CUTOFF, 0, 127, 0, 0, 0},
        {"IL", F_BIPCT, -64, 63, 0, 0, 0},
    },
    VDC_PRESETS, sizeof(VDC_PRESETS) / sizeof(VDC_PRESETS[0]), 1, vdc_note_on, vdc_render,
    0xF81F, {P_E4, P_E5, P_E7, P_ATK}, 0, vdc_amp, 0, 0, 1, vdc_done,
};
