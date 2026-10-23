/* SPDX-License-Identifier: GPL-3.0-only */
/* JP-8: the Roland Jupiter-8 (1981-84), after xtro's jupiter8 engine (GPLv3), at component level.
 *
 *   two true VCOs a voice (before Roland's DCOs): VCO-1 triangle, saw, pulse or square; VCO-2 sine, saw, pulse or
 *   noise; the VCO MODULATION section works on VCO-1: CROSS MOD (VCO-2 into VCO-1's frequency at audio rate, linear
 *   FM: the clangorous JP-8 sound) and SYNC (VCO-1's core reset by VCO-2: SYN SAW, SYN SQR). V2 PIT: VCO-2's range and
 *   tune as semitones.
 *   -> the HPF, a passive 6 dB/oct high-pass ahead of the VCF (no resonance: the JP-8's "thin the pad" slider)
 *   -> the IR3109: four OTA integrators. An OTA charges its capacitor with a current tanh(in - out) of the pair's
 *   input difference (the OTA's own transfer, not the ladder's difference of two tanh, not the CEM's saturating input:
 *   it slews instead of clipping, the JP-8's smooth bite). SLOPE takes the output from the second integrator (12 dB/oct) or the
 *   fourth (24); the resonance always comes back from the fourth. Roland's resonance path stops just past the edge: a whistle at the very top, never a scream
 *   (k 0 .. 4.05, and the input level compensated by half of what the feedback removes).
 *   -> the VCA (BA662), ENV-1 on the filter, ENV-2 on the VCA (ADSR, Roland's RC attack). The JP-8's cards are trimmed
 *   at the factory and drift a little (+-2 cents).
 * EDIT: VCO {VCO1, VCO2, V2 PIT, XMOD}, VCF {CUT, RES, HPF, SLOPE} */
static const char *const N_VDJ_1[] = {"TRI", "SAW", "PULSE", "SQUARE", "SYN SAW", "SYN SQR"};
static const char *const N_VDJ_2[] = {"SINE", "SAW", "PULSE", "NOISE"};
static const char *const N_VDJ_SLOPE[] = {"12DB", "24DB"};

static void vdj_note_on(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_JP8);
    if (!s)
        return;
    if (!s->card) {
        vd_card(s, t, v, ENGI_JP8, 2, 48);
        s->ph[0] = noise32(&s->nz);
        s->ph[1] = noise32(&s->nz);
    }
    vd_trigger(s, v);
}

static int vdj_done(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_JP8);
    const uint32_t *ek = vd_ek(t);
    int32_t sus = (int32_t)t->p[P_SUS] << 17;
    if (!s)
        return 1;
    vd_contour(&s->aenv, v->gate, sus, ek[0], ek[1], ek[2], VDA_CEM);
    vd_contour(&s->fenv, v->gate, sus, ek[0], ek[1], ek[2], VDA_CEM);
    vd_drift(s, 2);
    return !s->aenv.st;
}

static int32_t vdj_amp(track_t *t, voice_t *v, int32_t env)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_JP8);
    return s ? vd_amp_of(s, env) : 0;
}

static void vdj_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_JP8);
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)p[P_E0] % 6u, w2 = (uint32_t)p[P_E1] & 3u, i, inc1, inc2, ph1, ph2, pw, ratio = 0;
    int32_t fe, u, Gh, k, comp, xm = p[P_E3], hz, nst, slope24 = p[P_E7] & 1, y2;
    vd_zdf4_t Z;
    vd_cas4_t O;
    int sync = w1 >= 4u;
    if (!s)
        return;
    fe = ((s->fenv.lvl >> 9) * p[P_ED_FLT]) >> 7;
    inc1 = vd_detune(m->inc, s->tol_pitch[0] + s->drift);
    inc2 = vd_detune(vd_inc_at(m, p[P_E2] * 16), s->tol_pitch[1] + s->drift);
    if (sync && inc2 >> 8)
        ratio = inc1 / (inc2 >> 8);
    pw = (uint32_t)clamp(128 + ((m->shape - (64 << 8)) >> 8), 13, 243) << 24;
    u = (p[P_E4] << 8) + m->cutoff + fe + VD_KTRK(m->pitch16, 160) + s->tol_cut;
    Gh = p[P_E6] ? vd_gexp(p[P_E6] * 200 - (20 << 8)) : 0;   /* the HPF slider: off .. ~2.5 kHz */
    k = p[P_E5] * 134;                                    /* RES: 0 .. 4.16, Q12: it whistles only at the top */
    vd_zdf4_coef(&Z, u, k);
    comp = 4096 + (k >> 1);                               /* the input lifted by half of what the feedback takes */
    ph1 = s->ph[0];
    ph2 = s->ph[1];
    memcpy(&O, s->f, sizeof O);
    hz = s->f[4];
    nst = s->nz;
    for (i = 0; i < n; i++) {
        int32_t a, b, x;
        uint32_t n2 = ph2 + inc2, i1 = inc1;
        switch (w2) {
        case 1: b = osc_saw(ph2, inc2); break;
        case 2: b = osc_pulse(ph2, inc2, pw); break;
        case 3: b = (int32_t)(noise32(&nst) >> 16) - 32768; break;
        default: b = osc_sine(ph2); break;
        }
        if (xm)                                           /* CROSS MOD: VCO-2 into VCO-1's frequency (linear FM) */
            i1 = (uint32_t)clamp((int32_t)inc1 + (int32_t)(inc1 >> 15) * ((b * xm) >> 7), 0, 0x7FFFFFFF);
        switch (w1) {
        case 0: a = osc_tri(ph1); break;
        case 2: a = osc_pulse(ph1, i1, pw); break;
        case 3: case 5: a = osc_pulse(ph1, i1, 0x80000000u); break;
        default: a = osc_saw(ph1, i1); break;
        }
        ph1 += i1;
        if (sync && n2 < ph2)                             /* VCO-2 wrapped: VCO-1's core restarts */
            ph1 = (n2 >> 8) * ratio;
        ph2 = n2;
        x = (a + b) >> 1;
        if (Gh) {                                         /* the HPF: x minus its own low-passed self */
            hz += (Gh * (x - hz)) >> 15;
            x -= hz;
        }
        x = vd_cas4(&Z, &O, (x * comp) >> 12, VDZ_OTA, &y2);   /* the 4th fed back; SLOPE picks the tap */
        x = slope24 ? x : y2;
        out[i] += VD_OUT(vd_dc(&s->f[11], (x * 3) >> 1), m, i, 40);
    }
    s->ph[0] = ph1;
    s->ph[1] = ph2;
    memcpy(s->f, &O, sizeof O);
    s->f[4] = hz;
    s->nz = nst;
}

static const preset_t VDJ_PRESETS[] = {
    /* {VCO1, VCO2, V2 PIT, XMOD, CUT, RES, HPF, SLOPE}, {A D S R}, ENV-1 > VCF, mono */
    {"JP8 BRASS", {1, 1, 0, 0, 40, 20, 0, 1}, {24, 70, 96, 40}, 40, 0, FX(0, 30, 10, 36)},
    {"LUSH PAD", {1, 1, 0, 0, 56, 10, 20, 1}, {80, 90, 112, 90}, 10, 0, FX(0, 60, 20, 66),
     XP(P_LRATE + 1, 28, P_LD_PIT + 1, 1)},
    {"STRING PAD", {2, 1, 12, 0, 60, 6, 40, 0}, {70, 80, 112, 80}, 8, 0, FX(0, 60, 16, 60),
     XP(P_LRATE + 1, 44, P_LD_SHP + 1, 36)},
    {"XMOD BELL", {0, 0, 19, 70, 80, 10, 30, 1}, {0, 80, 0, 70}, 20, 0, FX(0, 30, 30, 60)},
    {"XMOD LEAD", {1, 0, 7, 40, 70, 30, 0, 1}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 30, 24), XP(P_GLIDE + 1, 20)},
    {"SYNC LEAD", {4, 1, 12, 0, 74, 30, 0, 1}, {0, 70, 110, 40}, 30, 1, FX(0, 0, 30, 24),
     XP(P_LRATE + 1, 30, P_LD_PIT + 1, 0)},
    {"JP8 BASS", {1, 2, -12, 0, 30, 40, 0, 1}, {0, 56, 70, 20}, 46, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"XMOD BASS", {1, 0, -12, 50, 34, 30, 0, 1}, {0, 56, 70, 20}, 40, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"BRIGHT PAD", {3, 1, 7, 0, 80, 20, 50, 0}, {60, 80, 110, 80}, 10, 0, FX(0, 50, 30, 60)},
    {"POLY STAB", {1, 1, 0, 0, 40, 30, 0, 1}, {0, 46, 10, 30}, 52, 0, FX(0, 30, 28, 40)},
    {"GLASS PLUK", {2, 0, 12, 20, 60, 30, 40, 0}, {0, 60, 0, 60}, 40, 0, FX(0, 30, 30, 50)},
    {"ARENA ARP", {1, 1, 0, 0, 46, 40, 0, 1}, {0, 70, 50, 50}, 40, 0, FX(0, 40, 34, 50), ARP(1, 2, 2, 50)},
    {"NICKI", {2, 1, 12, 10, 52, 30, 30, 1}, {50, 90, 100, 90}, 24, 0, FX(0, 50, 30, 70),
     XP(P_LRATE + 1, 18, P_LD_FLT + 1, 10)},
    {"SPECTACLE", {0, 0, 31, 90, 60, 50, 40, 1}, {10, 90, 40, 80}, 30, 0, FX(10, 40, 40, 70)},
    {"NOISE SWP", {0, 3, 0, 0, 20, 60, 0, 1}, {90, 100, 110, 80}, 60, 0, FX(0, 30, 30, 70)},
    {"24DB LEAD", {3, 1, 0, 0, 60, 60, 0, 1}, {0, 70, 110, 40}, 30, 1, FX(0, 0, 30, 24), XP(P_GLIDE + 1, 20)},
};

static const engine_t ENG_JP8 = {
    "JP-8", {"VCO", "IR3109"},
    {
        {"VCO1", F_ENUM, 0, 5, 1, N_VDJ_1, 0},
        {"VCO2", F_ENUM, 0, 3, 1, N_VDJ_2, 0},
        {"V2 PIT", F_SEMI, -24, 24, 0, 0, 0},
        {"XMOD", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 60, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"HPF", F_PCT, 0, 127, 0, 0, 0},
        {"SLOPE", F_ENUM, 0, 1, 1, N_VDJ_SLOPE, 0},
    },
    VDJ_PRESETS, sizeof(VDJ_PRESETS) / sizeof(VDJ_PRESETS[0]), 1, vdj_note_on, vdj_render,
    0xF800, {P_E4, P_E5, P_E3, P_REL}, 0, vdj_amp, 0, 0, 1, vdj_done,
};
