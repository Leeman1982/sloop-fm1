/* SPDX-License-Identifier: GPL-3.0-only */
/* MS-20: the Korg MS-20 (1978-83, the early KORG35 filter board), after xtro's ms20 engine (GPLv3), at component level.
 *
 *   VCO 1: triangle, saw, pulse (its width: LFO / ENV > SHP, the MS-20's PW knob) or white noise. VCO 2: saw, square,
 *   a narrow pulse (10 %) or RING: VCO 1 times VCO 2's square (the ring modulator on VCO 2's wave switch). PITCH:
 *   VCO 2's interval, +-1 octave as the knob. MIX: VCO 1 / VCO 2 levels.
 *   -> the HIGH-PASS then the LOW-PASS, two separate KORG35 Sallen-Key sections in series, each with its own PEAK:
 *   two RC stages, the voltage between them bootstrapped from the section's output through a pair of diodes. The diodes
 *   ARE the MS-20: the resonance feedback is clipped (tanh), so past PEAK ~ 2 both sections scream and self-oscillate
 *   at a level the diodes set, grit and all, where a ladder would sing a clean sine. The high-pass is taken after its
 *   first stage, as Korg's panel says: -6 dB/oct below a resonant peak; the low-pass after its second: -12 dB/oct.
 *   -> the VCA. EG 2 drives the cutoffs (ENV > FLT, both sections) and the VCA, as the patch panel normals it: one
 *   contour for both, so the filter and the loudness move together (ADSR; the EG's HOLD is not a knob here). EG 1, the
 *   pitch, is SLOOP's ENV > PIT. No keyboard tracking on the filters: the MS-20 has none until you patch it.
 * EDIT: VCO {VCO1, VCO2, PITCH, MIX}, KORG35 {HPF, HPK, LPF, LPK} */
static const char *const N_VDS_1[] = {"TRI", "SAW", "PULSE", "NOISE"};
static const char *const N_VDS_2[] = {"SAW", "SQUARE", "PULSE", "RING"};

static void vds_note_on(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MS20);
    if (!s)
        return;
    if (!s->card) {
        vd_card(s, t, v, ENGI_MS20, 4, 80);
        s->ph[0] = noise32(&s->nz);
        s->ph[1] = noise32(&s->nz);
    }
    vd_trigger(s, v);
}

static int vds_done(track_t *t, voice_t *v)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MS20);
    const uint32_t *ek = vd_ek(t);
    if (!s)
        return 1;
    vd_contour(&s->aenv, v->gate, (int32_t)t->p[P_SUS] << 17, ek[0], ek[1], ek[2], VDA_CEM);
    s->fenv = s->aenv;                                    /* EG 2: one contour, the cutoffs and the VCA */
    vd_drift(s, 4);
    return !s->aenv.st;
}

static int32_t vds_amp(track_t *t, voice_t *v, int32_t env)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MS20);
    return s ? vd_amp_of(s, env) : 0;
}

static void vds_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    vd_voice_t *s = vd_voice_of(t, v, ENGI_MS20);
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)p[P_E0] & 3u, w2 = (uint32_t)p[P_E1] & 3u, i, inc1, inc2, ph1, ph2, pw;
    int32_t fe, l1v = (127 - p[P_E3]) * 258, l2v = p[P_E3] * 258;
    int32_t nst;
    vd_k35c_t CH, CL;
    vd_k35_t KH, KL;
    if (!s)
        return;
    fe = ((s->fenv.lvl >> 9) * p[P_ED_FLT]) >> 7;
    inc1 = vd_detune(m->inc, s->tol_pitch[0] + s->drift);
    inc2 = vd_detune(vd_inc_at(m, p[P_E2] * 16), s->tol_pitch[1] + s->drift);
    pw = (uint32_t)clamp(128 + ((m->shape - (64 << 8)) >> 8), 13, 243) << 24;
    /* PEAK: K 0 .. 2.3, Q12 (2: the section self-oscillates) */
    vd_k35_coef(&CH, (p[P_E4] << 8) + m->cutoff + fe + s->tol_cut, p[P_E5] * 74);
    vd_k35_coef(&CL, (p[P_E6] << 8) + m->cutoff + fe + s->tol_cut, p[P_E7] * 74);
    ph1 = s->ph[0];
    ph2 = s->ph[1];
    KH.s1 = s->f[0], KH.s2 = s->f[1], KL.s1 = s->f[2], KL.s2 = s->f[3];
    nst = s->nz;
    for (i = 0; i < n; i++) {
        int32_t o1, o2, x;
        switch (w1) {
        case 0: o1 = osc_tri(ph1); break;
        case 2: o1 = osc_pulse(ph1, inc1, pw); break;
        case 3: o1 = (int32_t)(noise32(&nst) >> 16) - 32768; break;
        default: o1 = osc_saw(ph1, inc1); break;
        }
        switch (w2) {
        case 1: o2 = osc_pulse(ph2, inc2, 0x80000000u); break;
        case 2: o2 = osc_pulse(ph2, inc2, 0x1999999Au); break;
        case 3: o2 = (o1 * (osc_pulse(ph2, inc2, 0x80000000u) << 1)) >> 15; break;   /* RING */
        default: o2 = osc_saw(ph2, inc2); break;
        }
        ph1 += inc1;
        ph2 += inc2;
        x = (mulq15(o1, l1v) + mulq15(o2, l2v)) >> 1;
        x = vd_k35_lp(&CL, &KL, vd_k35_hp(&CH, &KH, x));
        out[i] += voice_amp(vd_dc(&s->f[11], x + (x >> 1)), m, i);
    }
    s->ph[0] = ph1;
    s->ph[1] = ph2;
    s->f[0] = KH.s1, s->f[1] = KH.s2, s->f[2] = KL.s1, s->f[3] = KL.s2;
    s->nz = nst;
}

static const preset_t VDS_PRESETS[] = {
    /* {VCO1, VCO2, PITCH, MIX, HPF, HPK, LPF, LPK}, {A D S R}, EG2 > cutoffs, mono */
    {"ACID BASS", {1, 0, 0, 0, 0, 0, 30, 100}, {0, 50, 20, 20}, 56, 1, FX(20, 0, 20, 6),
     XP(P_GLIDE + 1, 18, P_GLMODE + 1, 1, P_TRANS + 1, -12)},
    {"SCRM BASS", {1, 1, -12, 50, 20, 80, 34, 110}, {0, 60, 60, 20}, 50, 1, FX(30, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"SUB BASS", {0, 1, -12, 70, 0, 0, 26, 30}, {0, 70, 100, 20}, 20, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -12)},
    {"SCREAM LD", {1, 0, 7, 40, 40, 90, 70, 118}, {0, 70, 110, 40}, 24, 1, FX(10, 0, 30, 24), XP(P_GLIDE + 1, 26)},
    {"RING LEAD", {2, 3, 5, 80, 20, 40, 70, 60}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 30, 30)},
    {"HPF SCREAM", {1, 1, 0, 64, 60, 124, 100, 20}, {0, 70, 110, 40}, 20, 1, FX(0, 0, 30, 30)},
    {"SELF-OSC", {3, 0, 0, 0, 0, 0, 70, 127}, {0, 70, 110, 50}, 0, 1, FX(0, 0, 30, 40)},
    {"LASER PLUK", {1, 1, 12, 64, 30, 60, 60, 110}, {0, 30, 0, 20}, 63, 1, FX(0, 0, 34, 30)},
    {"SIREN", {0, 3, 7, 100, 10, 30, 60, 90}, {20, 70, 110, 60}, 20, 1, FX(0, 0, 34, 40),
     XP(P_LRATE + 1, 30, P_LD_PIT + 1, 24)},
    {"NOISE SWP", {3, 0, 0, 0, 0, 40, 20, 110}, {60, 100, 100, 80}, 63, 1, FX(0, 0, 30, 60)},
    {"METAL HIT", {2, 3, 11, 100, 40, 60, 80, 70}, {0, 50, 0, 50}, 30, 0, FX(10, 0, 30, 50)},
    {"BRASS STAB", {1, 0, 0, 64, 10, 20, 40, 50}, {0, 50, 30, 30}, 50, 0, FX(0, 20, 20, 30)},
    {"HAUNT PAD", {0, 1, 7, 50, 30, 50, 50, 60}, {80, 90, 110, 90}, 20, 0, FX(0, 50, 30, 70),
     XP(P_LRATE + 1, 16, P_LD_FLT + 1, 14)},
    {"FLESH GUN", {1, 3, -7, 90, 30, 100, 50, 120}, {0, 80, 40, 60}, 50, 1, FX(40, 0, 30, 50),
     XP(P_ED_PIT + 1, 40)},
    {"CATHODE", {2, 2, 12, 60, 50, 90, 90, 90}, {0, 70, 80, 50}, 30, 1, FX(20, 0, 30, 40),
     XP(P_LRATE + 1, 100, P_LD_SHP + 1, 50)},
    {"SWOOP LEAD", {1, 0, 0, 30, 0, 0, 60, 80}, {0, 70, 100, 40}, 20, 1, FX(0, 0, 30, 30), XP(P_ED_PIT + 1, 60)},
};

static const engine_t ENG_MS20 = {
    "MS-20", {"VCO", "KORG35"},
    {
        {"VCO1", F_ENUM, 0, 3, 1, N_VDS_1, 0},
        {"VCO2", F_ENUM, 0, 3, 0, N_VDS_2, 0},
        {"PITCH", F_SEMI, -12, 12, 0, 0, 0},
        {"MIX", F_PCT, 0, 127, 0, 0, 0},
        {"HPF", F_CUTOFF, 0, 127, 0, 0, 0},
        {"HPK", F_PCT, 0, 127, 0, 0, 0},
        {"LPF", F_CUTOFF, 0, 127, 70, 0, 0},
        {"LPK", F_PCT, 0, 127, 40, 0, 0},
    },
    VDS_PRESETS, sizeof(VDS_PRESETS) / sizeof(VDS_PRESETS[0]), 1, vds_note_on, vds_render,
    0x7BEF, {P_E6, P_E7, P_E4, P_E5}, 1, vds_amp, 0, 0, 1, vds_done,
};
