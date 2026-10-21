/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME: the six component models (firmware/src/eng_vd_*.c, vd_parts.c) on the host, the same sources as the
 * firmware. What the circuits are claimed to do, checked on the code that runs:
 *   - the filters, through their own step functions: the Model D ladder and the CEM3320 self-oscillate at full
 *     resonance, near their cutoff, and stay bounded (the tanh rungs / cells); the MS-20's KORG35 sections scream and
 *     self-oscillate (bounded by the diodes); the IR3109 whistles only at the very top; the SEM and the CS-80's
 *     IG00156 never self-oscillate; the ladder loses its low end as the emphasis rises; below the edge every one decays
 *   - the tables: vd_tanh odd and monotonic, the cutoff gains monotonic
 *   - every preset of the six: sounds, stays under full scale, no DC, and frees its voice after the key is let go
 *   - the contours: the Model D's DECAY switch (REL 0: a fast release; REL > 0: the decay's), the CS-80's VCF contour
 *     from INITIAL LEVEL to ATTACK LEVEL and back, a retrigger from the present level (no click to 0)
 *   build/host/vd_test        (tests/run_tests.sh builds and runs it) */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails, checks;
static void check(int ok, const char *what, double a, double b)
{
    checks++;
    if (!ok)
        fails++;
    printf("%-74s %s (%.1f, %.1f)\n", what, ok ? "ok" : "FAIL", a, b);
}

/* a filter kicked with a short burst, then silence: the last 0.25 s' peak and its zero-crossing frequency */
typedef int32_t (*fstep_t)(void *st, int32_t x, int32_t a, int32_t b);
static void ring(fstep_t f, void *st, int32_t a, int32_t b, double *peak, double *hz, double *maxabs)
{
    uint32_t i, n = FS * 2u, zc = 0;
    int32_t prev = 0, pk = 0, mx = 0;
    for (i = 0; i < n; i++) {
        int32_t y = f(st, i < 200u ? 12000 : 0, a, b);   /* a 4.5 ms pulse: it reaches a low cutoff too */
        int32_t ay = y < 0 ? -y : y;
        if (ay > mx)
            mx = ay;
        if (i >= n - FS / 4u) {
            if (ay > pk)
                pk = ay;
            if ((prev < 0) != (y < 0))
                zc++;
        }
        prev = y;
    }
    *peak = pk;
    *hz = zc * 2.0;                                          /* crossings in 0.25 s: 2 a cycle */
    *maxabs = mx;
}

static vd_zdf4_t zc;
static vd_k35c_t kc;
static int32_t f_cas(void *st, int32_t x, int32_t kind, int32_t b)
{
    int32_t y2;
    (void)b;
    return vd_cas4(&zc, st, x, (uint32_t)kind, &y2);
}
static int32_t f_k35lp(void *st, int32_t x, int32_t a, int32_t b) { (void)a, (void)b; return vd_k35_lp(&kc, st, x); }
static int32_t f_k35hp(void *st, int32_t x, int32_t a, int32_t b) { (void)a, (void)b; return vd_k35_hp(&kc, st, x); }
static vdo_coef_t sem_c;
static int32_t f_sem(void *st, int32_t x, int32_t a, int32_t b)
{
    int32_t *s = st;
    (void)a, (void)b;
    return vdo_sem(&sem_c, x, &s[0], &s[1]);
}
static vdc_svf_t cs_c;
static int32_t f_cs(void *st, int32_t x, int32_t a, int32_t b)
{
    int32_t *s = st, lp;
    (void)a, (void)b;
    vdc_svf(&cs_c, x, &s[0], &s[1], &lp);
    return lp;
}
static double hz_of(int32_t u) { return 30.0 * pow(16000.0 / 30.0, u / (127.0 * 256)); }

static void filters(void)
{
    double pk, hz, mx;
    int32_t st[12];
    int32_t u = 80 << 8;                                     /* ~1.4 kHz */
    char b[128];
    /* the ladder, the CEM3320, the IR3109: self-oscillation from k = 4 at every cutoff */
    {
        static const char *const NM[3] = {"MODEL D ladder", "P-5 CEM3320", "JP-8 IR3109"};
        static const int32_t US[4] = {30 << 8, 80 << 8, 110 << 8, 125 << 8};
        uint32_t kind, j;
        for (kind = 0; kind < 3u; kind++) {
            int sing = 1, quiet = 1, near = 1;
            double mxx = 0;
            for (j = 0; j < 4u; j++) {
                memset(st, 0, sizeof st);
                vd_zdf4_coef(&zc, US[j], kind == VDZ_OTA ? 127 * 134 : 127 * 161);
                ring(f_cas, st, (int32_t)kind, 0, &pk, &hz, &mx);
                sing &= pk > (kind == VDZ_OTA ? 500 : 2000);   /* (the IR3109: a whistle) */
                near &= hz > hz_of(US[j]) * 0.6 && hz < hz_of(US[j]) * 1.4;
                if (mx > mxx)
                    mxx = mx;
                memset(st, 0, sizeof st);
                vd_zdf4_coef(&zc, US[j], 3 * 4096);          /* k 3: below the edge */
                ring(f_cas, st, (int32_t)kind, 0, &pk, &hz, &mx);
                quiet &= pk < 50;
            }
            snprintf(b, sizeof b, "%s: full resonance self-oscillates at 4 cutoffs, at the cutoff", NM[kind]);
            check(sing && near, b, sing, near);
            snprintf(b, sizeof b, "%s: bounded by its cells; k 3 rings out", NM[kind]);
            check(mxx < 70000 && quiet, b, mxx, quiet);
        }
        memset(st, 0, sizeof st);                             /* the IR3109 just under its top: polite */
        vd_zdf4_coef(&zc, u, 115 * 134);
        ring(f_cas, st, VDZ_OTA, 0, &pk, &hz, &mx);
        check(pk < 50, "JP-8 IR3109: RES 115 rings out (Roland's resonance stops just past the edge)", pk, 50);
    }
    {   /* the bass loss: a low tone through k 0 and k 3.4 */
        double r[2];
        int j;
        for (j = 0; j < 2; j++) {
            uint32_t i, ph = 0;
            int32_t pkk = 0;
            memset(st, 0, sizeof st);
            vd_zdf4_coef(&zc, 100 << 8, j ? 100 * 139 : 0);
            for (i = 0; i < FS; i++, ph += pitch_inc(36 * 16)) {
                int32_t y2, y = vd_cas4(&zc, (vd_cas4_t *)st, osc_sine(ph) >> 3, VDZ_LADDER, &y2);
                if (i > FS / 2u && (y < 0 ? -y : y) > pkk)
                    pkk = y < 0 ? -y : y;
            }
            r[j] = pkk;
        }
        check(r[1] < r[0] * 0.5, "MODEL D ladder: the emphasis thins the bass (1 / (1 + k))", r[0], r[1]);
    }
    /* the SEM */
    memset(st, 0, sizeof st);
    vdo_coefs(&sem_c, u, 127);
    ring(f_sem, st, 0, 0, &pk, &hz, &mx);
    check(pk < 50, "OB-X SEM: RES 127 never self-oscillates (damping >= 0.12)", pk, 50);
    /* the KORG35 sections: self-oscillation from K = 2 at every cutoff, the diodes bound it */
    {
        static const int32_t US[4] = {30 << 8, 80 << 8, 110 << 8, 125 << 8};
        uint32_t j, hp;
        for (hp = 0; hp < 2u; hp++) {
            int sing = 1, quiet = 1;
            double mxx = 0;
            for (j = 0; j < 4u; j++) {
                memset(st, 0, sizeof st);
                vd_k35_coef(&kc, US[j], 127 * 74);
                ring(hp ? f_k35hp : f_k35lp, st, 0, 0, &pk, &hz, &mx);
                sing &= pk > 2000;
                if (mx > mxx)
                    mxx = mx;
                memset(st, 0, sizeof st);
                vd_k35_coef(&kc, US[j], 80 * 74);
                ring(hp ? f_k35hp : f_k35lp, st, 0, 0, &pk, &hz, &mx);
                quiet &= pk < 50;
            }
            snprintf(b, sizeof b, "MS-20 KORG35 %s: PEAK 127 screams at 4 cutoffs, bounded; PEAK 80 rings out",
                     hp ? "HPF" : "LPF");
            check(sing && quiet && mxx < 90000, b, sing + quiet, mxx);
        }
    }
    /* the IG00156 at every cutoff */
    {
        int32_t uu, worst = 0;
        for (uu = 10 << 8; uu <= 127 << 8; uu += 8 << 8) {
            memset(st, 0, sizeof st);
            vdc_coef(&cs_c, uu, 127);
            ring(f_cs, st, 0, 0, &pk, &hz, &mx);
            if (pk > worst)
                worst = (int32_t)pk;
        }
        check(worst < 50, "CS-80 IG00156: RES 127 cannot self-oscillate at any cutoff", worst, 50);
    }
}

static void tables(void)
{
    int32_t x, last = -40000, bad = 0, u, g0 = -1, bad2 = 0;
    for (x = -140000; x <= 140000; x += 97) {
        int32_t y = vd_tanh(x);
        if (y < last || vd_tanh(-x) != -y)
            bad++;
        last = y;
    }
    check(!bad, "vd_tanh: monotonic and odd over -4.3 .. 4.3", bad, 0);
    for (u = VD_U_LO; u < VD_U_HI; u += 64) {
        if (vd_gexp(u) < g0)
            bad2++;
        g0 = vd_gexp(u);
    }
    check(!bad2 && vd_gexp(VD_U_HI) <= 32767, "vd_gexp: monotonic, Q15", bad2, vd_gexp(VD_U_HI));
}

/* the part-1 setup for a preset of engine e (as hostsim's main) */
static void setup(uint32_t e, uint32_t pi)
{
    uint32_t i;
    track_t *t = &trk[0];
    const preset_t *p = &ENGINES[e]->presets[pi];
    memset(t->v, 0, sizeof t->v);
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (i = 0; i < P_E0; i++) t->p[i] = TP[i].def;
    t->eng_req = t->engine = (uint8_t)e;
    for (i = 0; i < 8; i++) t->p[P_E0 + i] = p->e[i];
    t->p[P_ATK] = p->env[0], t->p[P_DEC] = p->env[1], t->p[P_SUS] = p->env[2], t->p[P_REL] = p->env[3];
    t->p[P_ED_FLT] = p->fenv;
    preset_extras(t->p, p);
    t->p[P_VOICE] = V_POLY;
    t->p[P_GLIDE] = 0;
    eng_arena_own[0] = 0;                                  /* a fresh arena: the cards are drawn again */
}
/* hold note for hold s, then render until all voices are free (or cap s): peak, DC, the time to free */
static void play(uint32_t note, uint32_t vel, double hold, double cap, int32_t *peak, double *dc, double *free_s)
{
    track_t *t = &trk[0];
    uint32_t f, nh = (uint32_t)(hold * FS), nc = (uint32_t)(cap * FS), i;
    double sum = 0;
    int32_t pk = 0, b[CTL];
    *free_s = -1;
    trk_note_on(t, note, vel);
    for (f = 0; f < nh + nc; f += CTL) {
        uint32_t live = 0;
        if (f == nh - nh % CTL)
            trk_note_off(t, note);
        track_render(t, b, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = b[i] < 0 ? -b[i] : b[i];
            if (a > pk)
                pk = a;
            if (f < nh)
                sum += b[i];
        }
        for (i = 0; i < NVOICE; i++)
            live += t->v[i].active;
        if (f > nh && !live) {
            *free_s = (double)(f - nh) / FS;
            break;
        }
    }
    *peak = pk;
    *dc = sum / nh;
}

static void presets(void)
{
    uint32_t e, pi;
    for (e = ENGI_VD0; e < ENGI_VD0 + NVD; e++) {
        uint32_t quiet = 0, loud = 0, hang = 0, dcbad = 0;
        char b[128];
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            int32_t pk;
            double dc, fr;
            setup(e, pi);
            play(48, 100, 1.0, 12.0, &pk, &dc, &fr);
            if (pk < 300) {
                quiet++;
                printf("   quiet: %s %s peak %d\n", ENGINES[e]->name, ENGINES[e]->presets[pi].name, pk);
            }
            if (pk >= 32767 * 3)                            /* (a part's sum before the mix; the master limits) */
                loud++;
            if (fr < 0) {
                hang++;
                printf("   hangs: %s %s\n", ENGINES[e]->name, ENGINES[e]->presets[pi].name);
            }
            if (dc > 2500 || dc < -2500) {
                dcbad++;
                printf("   DC: %s %s %.0f\n", ENGINES[e]->name, ENGINES[e]->presets[pi].name, dc);
            }
        }
        snprintf(b, sizeof b, "%s: %u presets sound, bounded, no DC, the voice frees", ENGINES[e]->name,
                 ENGINES[e]->npresets);
        check(!quiet && !loud && !hang && !dcbad, b, quiet + loud, hang + dcbad);
    }
}

static voice_t *live_voice(track_t *t)                 /* the voice the note went to */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return &t->v[i];
    return &t->v[0];
}
static void contours(void)
{
    int32_t pk;
    double dc, fr0, fr1;
    track_t *t = &trk[0];
    /* the Model D's DECAY switch */
    setup(ENGI_MODELD, 0);
    t->p[P_SUS] = 127, t->p[P_DEC] = 90, t->p[P_REL] = 0;
    play(48, 100, 0.5, 12.0, &pk, &dc, &fr0);
    setup(ENGI_MODELD, 0);
    t->p[P_SUS] = 127, t->p[P_DEC] = 90, t->p[P_REL] = 1;
    play(48, 100, 0.5, 12.0, &pk, &dc, &fr1);
    check(fr0 >= 0 && fr0 < 0.06 && fr1 > 0.3, "MODEL D: DECAY switch OFF lets go at once, ON at the decay's rate", fr0, fr1);
    /* the CS-80's VCF contour: IL at the key, toward AL, back toward IL on release */
    {
        vd_voice_t *s;
        int32_t at0, atk, atrel;
        uint32_t f;
        int32_t b[CTL];
        setup(ENGI_CS80, 0);
        t->p[P_E7] = -40, t->p[P_ED_FLT] = 50, t->p[P_ATK] = 70, t->p[P_DEC] = 127, t->p[P_SUS] = 127;
        t->p[P_REL] = 60;
        trk_note_on(t, 48, 100);
        s = vd_voice_of(t, live_voice(t), ENGI_CS80);
        at0 = s->aux[2] >> 8;
        printf("   CS-80 VCF contour: IL %d\n", at0);
        for (f = 0; f < FS; f += CTL)
            track_render(t, b, CTL);
        atk = s->aux[2] >> 8;
        printf("   CS-80 VCF contour: after the attack %d (AL %d)\n", atk, 50 * 254);
        trk_note_off(t, 48);
        for (f = 0; f < FS * 3u; f += CTL)
            track_render(t, b, CTL);
        atrel = s->aux[2] >> 8;
        check(at0 == -40 * 254 && atk > 30 * 254 && atk <= 50 * 254 && atrel < 0,
              "CS-80 VCF contour: IL at the key, AL, its decay toward 0, back toward IL", at0, atk);
    }
    /* a retrigger charges from the present level */
    {
        vd_voice_t *s;
        int32_t before;
        uint32_t f;
        int32_t b[CTL];
        setup(ENGI_P5, 0);
        t->p[P_VOICE] = V_MONO;
        trk_note_on(t, 48, 100);
        for (f = 0; f < FS / 2u; f += CTL)
            track_render(t, b, CTL);
        s = vd_voice_of(t, live_voice(t), ENGI_P5);
        before = s->aenv.lvl;
        trk_note_off(t, 48);
        track_render(t, b, CTL);
        trk_note_on(t, 50, 100);
        check(s->aenv.lvl > before / 2 && s->aenv.st == 1, "P-5: a retrigger attacks from the present level", before,
              s->aenv.lvl);
    }
}

int main(void)
{
    host_tracks_init();
    tables();
    filters();
    presets();
    contours();
    printf("vd_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
