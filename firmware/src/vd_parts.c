/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME: the parts bin of the six component models (eng_vd_*.c), included from engines.c.
 *
 * The six engines are built the way the instruments were: from the circuits, a stage at a time, not from a generic
 * "VCO -> VCF -> VCA" template with a different label. What is shared here is the physics the circuits share:
 *
 *   vd_tanh        the transfer curve of a bipolar differential pair (a ladder rung, an OTA's input stage, a diode
 *                  pair), tanh over -4..4 (x Q15, 1.0 = 2 Vt across the pair), linearly interpolated
 *   vd_gexp/gtan   a capacitor's charging rate at a cutoff, from the knob's own scale (SLOOP's 30 Hz .. 16 kHz,
 *                  extended): the impulse-invariant one-pole gain (the explicit nonlinear stages) and the trapezoidal
 *                  integrator's tan() gain (the state-variable and Sallen-Key cores)
 *   vd_card        what a voice card is apart from its schematic: the tolerances it left the factory with (its
 *                  VCOs' trim, its filter capacitor, its VCA gain) and the slow thermal drift of an exponential
 *                  converter, a random walk per card. A Prophet-5 rev 3 has less of it than an OB-X; none has none
 *   vd_contour     the contour generators as RC circuits: the attack charges a capacitor toward a target above the
 *                  peak (the comparator stops it at 1.0: the CEM3310 / Model D / Roland curve, concave, not a ramp);
 *                  decay and release discharge it. Per control tick (CTL samples), with SLOOP's ATK / DEC / REL
 *                  times (and ENV SYNC: esync_k, voice.c)
 *
 * State: the part's engine arena (engines.c eng_arena_of), one vd_voice_t a voice. The engines set .ownenv: their own
 * contours are the voice's amplitude and end it (vd_done), as on the real instruments: a Model D has no release time
 * of its own, a CS-80's filter contour starts from its INITIAL LEVEL, an MS-20's holds before it lets go.
 * Fixed point throughout (the FM-1 has no FPU): audio Q15 (1.0 = 32768) with headroom in int32. */
#include "vd_tables.h"

static uint32_t esync_k[NTRK][3];        /* (voice.c: this block's ATK step, DEC and REL coefficients of each part) */

/* tanh(x / 32768) * 32767 for x anywhere: the table holds -4..4, beyond it the pair is fully switched. Odd by
 * construction (|x| looked up, the sign put back): a differential pair is symmetric, its model must be too */
static inline int32_t vd_tanh(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i, y;
    a = a > 131071 ? 131071 : a;                         /* (the table's last point: fully switched) */
    i = (a >> 9) + 256;
    y = VD_TANH[i] + (((VD_TANH[i + 1] - VD_TANH[i]) * (a & 511)) >> 9);
    return x < 0 ? -y : y;
}

/* the cutoff u (SLOOP cutoff units: knob value << 8, any modulation added) as a table position, clamped */
static inline uint32_t vd_upos(int32_t u, uint32_t *f)
{
    u = clamp(u, VD_U_LO, VD_U_HI - 1) - VD_U_LO;
    *f = (uint32_t)u & 127u;
    return (uint32_t)u >> 7;
}
static inline int32_t vd_gexp(int32_t u)               /* Q15, 1 - e^(-2 pi fc / FS) */
{
    uint32_t f, i = vd_upos(u, &f);
    int32_t a = VD_GEXP[i];
    return a + (((VD_GEXP[i + 1] - a) * (int32_t)f) >> 7);
}
static inline int32_t vd_gtan(int32_t u)               /* Q16, tan(pi fc / FS) */
{
    uint32_t f, i = vd_upos(u, &f);
    int32_t a = (int32_t)VD_GTAN[i];
    return a + ((((int32_t)VD_GTAN[i + 1] - a) * (int32_t)f) >> 7);
}

/* a capacitor's charge in one sample: G (Q15, vd_gexp) times a difference up to +-2^18 (two bits dropped from it so
 * the product stays in 32 bits) */
static inline int32_t vd_step(int32_t g, int32_t d) { return (g * (d >> 2)) >> 13; }

/* a * b for b up to +-2^18 and a up to 2^15 (two bits dropped from b) */
static inline int32_t vd_mul(int32_t a, int32_t b) { return (a * (b >> 2) + 4096) >> 13; }

/* the output coupling capacitor every one of these boards has (~7 Hz): pulse widths and hard sync leave DC that the
 * real instrument never let out */
static inline int32_t vd_dc(int32_t *z, int32_t x)
{
    *z += ((x << 8) - *z) >> 10;                         /* (the capacitor in Q8: a Q0 one, floored by the shift, settles
                                                          * up to ~500 below the mean and leaves that as DC) */
    return x - (*z >> 8);
}

/* ------------------------------------------- the state-variable cores --- */
/* A trapezoidal SVF (Simper) in Q28 coefficients and 64-bit products: the pi32v2 multiplies 32 x 32 -> 64 in one
 * instruction, so the low cutoffs keep their precision (tsvf's Q13, dsp.c, rounds a3 to 0 below ~50 Hz: a closed
 * CS-80 filter would freeze). k: the damping, Q12. The coefficients once a tick (a 64-bit divide) */
typedef struct { int32_t a1, a2, a3, k; } vd_svf_t;
static void vd_svf_coef(vd_svf_t *c, int32_t u, int32_t k12)
{
    int64_t g = vd_gtan(u), den = 65536 + ((g * (g + ((int64_t)k12 << 4))) >> 16);   /* Q16, 1 .. ~54 */
    /* 1 / (1 + g (g + k)), Q28: 2^32 / den in 32 bits (the pi32v2 has no 64-bit divide), then << 12 */
    c->a1 = (int32_t)((0xFFFFFFFFu / (uint32_t)den) << 12);
    c->a2 = (int32_t)((g * c->a1) >> 16);
    c->a3 = (int32_t)((g * c->a2) >> 16);
    c->k = k12;
}
/* one step: returns the band-pass v1, *lp the low-pass; the high-pass is in - k v1 - lp. The integrators keep 8 more
 * bits than the signal (Q23): at a low cutoff a sample's change is a fraction of a Q15 step, and a Q15 state would
 * stick there (a dead band of ~140 at 50 Hz) */
static inline __attribute__((always_inline)) int32_t vd_svf(const vd_svf_t *c, int32_t in, int32_t *ic1, int32_t *ic2,
                                                            int32_t *lp)
{
    int32_t v3 = (in << 8) - *ic2;
    int32_t v1 = (int32_t)(((int64_t)c->a1 * *ic1 + (int64_t)c->a2 * v3 + (1 << 27)) >> 28);
    int32_t v2 = *ic2 + (int32_t)(((int64_t)c->a2 * *ic1 + (int64_t)c->a3 * v3 + (1 << 27)) >> 28);
    *ic1 = clamp(2 * v1 - *ic1, -(1 << 26), 1 << 26);
    *ic2 = clamp(2 * v2 - *ic2, -(1 << 26), 1 << 26);
    *lp = v2 >> 8;
    return v1 >> 8;
}

/* ------------------------------------------------- the four-pole cascades --- */
/* The Model D ladder, the CEM3320 and the IR3109 are all four integrators with the 4th fed back inverted. Solved
 * zero-delay (Zavalishin's TPT cascade): the feedback is found for this very sample from the integrators' states
 * (linearised), so the resonance sits where the circuit's does at every cutoff, self-oscillating from k = 4 (an
 * explicit loop, one sample late, needs k 4.1 at 100 Hz and over 12 at 6 kHz). Then the cells run with their own
 * nonlinearity, which is what tells the three apart and bounds the oscillation:
 *   VDZ_LADDER  a transistor rung: the capacitor charged by tanh(in) - tanh(out) (Huovilainen)
 *   VDZ_CEM     a CEM3320 cell: tanh at the cell's input, the capacitor after it linear
 *   VDZ_OTA     an IR3109 OTA: tanh(in - out), the OTA's own transfer (it slews rather than clips) */
enum { VDZ_LADDER, VDZ_CEM, VDZ_OTA };
typedef struct { int32_t G, ex, e[4]; } vd_zdf4_t;
typedef struct { int32_t s[4]; } vd_cas4_t;            /* the integrators' TPT states, Q23 (8 bits under the signal) */
/* The linear solve, folded into five coefficients once a tick: with b_i = (1 - G) s_i the 4th output is
 *   y4 = (G^4 x + G^3 b_0 + G^2 b_1 + G b_2 + b_3) / (1 + k G^4)
 * and the input after the feedback in = x - k y4 = ex x - sum(e_i s_i), all in Q28 (64-bit products, one
 * instruction each on the pi32v2) */
static void vd_zdf4_coef(vd_zdf4_t *c, int32_t u, int32_t k12)
{
    uint32_t g = (uint32_t)vd_gtan(u);                   /* Q16, up to ~6.3 at the 0.45 FS clamp */
    int64_t G = (int64_t)((g << 13) / ((65536u + g) >> 2)) << 13;   /* g / (1 + g), Q28 */
    int64_t one = (int64_t)1 << 28, omg = one - G, G2 = (G * G) >> 28, G3 = (G2 * G) >> 28, G4 = (G3 * G) >> 28;
    int64_t kq = (int64_t)k12 << 16, den = one + ((kq * G4) >> 28), inv, kinv;
    inv = (int64_t)(0xFFFFFFFFu / (uint32_t)(den >> 14)) << 10;    /* 1 / (1 + k G^4), Q28 (no 64-bit divide) */
    kinv = (kq * inv) >> 28;
    c->G = (int32_t)G;                                   /* the cells' gain, Q28 */
    c->ex = (int32_t)(one - ((kinv * G4) >> 28));
    c->e[0] = (int32_t)((((kinv * G3) >> 28) * omg) >> 28);
    c->e[1] = (int32_t)((((kinv * G2) >> 28) * omg) >> 28);
    c->e[2] = (int32_t)((((kinv * G) >> 28) * omg) >> 28);
    c->e[3] = (int32_t)((kinv * omg) >> 28);
}
/* one sample, x Q15; returns the 4th integrator (Q15), *y2 the 2nd (the IR3109's 12 dB tap). The capacitors keep 8
 * bits under the signal: at a low cutoff a sample moves them by a fraction of a Q15 step, and Q15 capacitors would
 * stick there (a dead band, a DC limit cycle no real capacitor has); the cells' nonlinearity sees the Q15 voltage */
static inline __attribute__((always_inline)) int32_t vd_cas4(const vd_zdf4_t *c, vd_cas4_t *q, int32_t x, uint32_t kind,
                                                             int32_t *y2)
{
    int32_t in = (int32_t)(((int64_t)c->ex * (x << 8) - (int64_t)c->e[0] * q->s[0] - (int64_t)c->e[1] * q->s[1] -
                            (int64_t)c->e[2] * q->s[2] - (int64_t)c->e[3] * q->s[3] + (1 << 27)) >> 28), j;
    for (j = 0; j < 4; j++) {
        int32_t st = q->s[j], v, y;
        if (kind == VDZ_LADDER)
            v = (int32_t)(((int64_t)c->G * (vd_tanh(in >> 8) - vd_tanh(st >> 8)) + (1 << 19)) >> 20);
        else if (kind == VDZ_CEM)
            v = (int32_t)(((int64_t)c->G * ((vd_tanh(in >> 8) << 8) - st) + (1 << 27)) >> 28);
        else
            v = (int32_t)(((int64_t)c->G * vd_tanh((in - st) >> 8) + (1 << 19)) >> 20);
        y = st + v;
        q->s[j] = y + v;                                 /* (bounded by the cell: it charges toward a bounded target) */
        in = y;
        if (j == 1)
            *y2 = y >> 8;
    }
    return in >> 8;
}

/* ------------------------------------------------- the KORG35 sections --- */
/* Two TPT one-poles, the voltage between them bootstrapped through the diode pair (tanh) by K (Q12): solved
 * zero-delay as above, self-oscillating from K = 2 at every cutoff, the diodes setting the scream's level */
typedef struct { int32_t G, GK, inv, k; } vd_k35c_t;
typedef struct { int32_t s1, s2; } vd_k35_t;
static void vd_k35_coef(vd_k35c_t *c, int32_t u, int32_t k12)
{
    uint32_t g = (uint32_t)vd_gtan(u);
    int32_t G = (int32_t)((g << 13) / ((65536u + g) >> 2));
    int32_t gk = (((G * (32768 - G)) >> 15) * k12) >> 12;   /* K G (1 - G), Q15 */
    c->G = G;
    c->GK = (G * k12) >> 12;                             /* K G, Q15 (up to ~2.3) */
    c->inv = (int32_t)((1u << 29) / (uint32_t)clamp(32768 - gk, 4096, 32768));   /* Q14 */
    c->k = k12;
}
/* a * b >> sh, rounded, through the 64-bit product */
#define VD_M(a, b, sh) ((int32_t)(((int64_t)(a) * (b) + ((int64_t)1 << ((sh) - 1))) >> (sh)))

/* low-pass: RC - RC, the 2nd stage's out (-12 dB/oct). The capacitors in Q23 (8 bits under the signal's Q15: no
 * dead band at a low cutoff), the diodes on the Q15 voltage */
static inline __attribute__((always_inline)) int32_t vd_k35_lp(const vd_k35c_t *c, vd_k35_t *f, int32_t x)
{
    int32_t omg = 32768 - c->G, b1 = VD_M(omg, f->s1, 15), b2 = VD_M(omg, f->s2, 15), l1, l2, v, y1, y2;
    x <<= 8;
    l1 = VD_M(c->G, x, 15) - VD_M(c->GK, b2, 15) + b1;
    l1 = VD_M(l1, c->inv, 14);
    l2 = VD_M(c->G, l1, 15) + b2;
    x += vd_tanh((c->k * ((l1 - l2) >> 10)) >> 10) << 8; /* the diodes */
    v = VD_M(c->G, x - f->s1, 15);
    y1 = f->s1 + v;
    f->s1 = clamp(y1 + v, -(1 << 26), 1 << 26);
    v = VD_M(c->G, y1 - f->s2, 15);
    y2 = f->s2 + v;
    f->s2 = clamp(y2 + v, -(1 << 26), 1 << 26);
    return y2 >> 8;
}
/* high-pass: CR - CR, the 2nd stage's low-pass part fed back; the 1st stage's out (-6 dB/oct below the peak) */
static inline __attribute__((always_inline)) int32_t vd_k35_hp(const vd_k35c_t *c, vd_k35_t *f, int32_t x)
{
    int32_t omg = 32768 - c->G, b1 = VD_M(omg, f->s1, 15), b2 = VD_M(omg, f->s2, 15), u, z1, z2, v, h1;
    x <<= 8;
    u = x + VD_M(c->k, b2 - VD_M(c->G, b1, 15), 12);
    u = VD_M(u, c->inv, 14);
    z1 = VD_M(c->G, u, 15) + b1;
    z2 = VD_M(c->G, u - z1, 15) + b2;
    u = x + (vd_tanh((c->k * (z2 >> 10)) >> 10) << 8);   /* the diodes */
    v = VD_M(c->G, u - f->s1, 15);
    z1 = f->s1 + v;
    f->s1 = clamp(z1 + v, -(1 << 26), 1 << 26);
    h1 = u - z1;
    v = VD_M(c->G, h1 - f->s2, 15);
    f->s2 = clamp(f->s2 + 2 * v, -(1 << 26), 1 << 26);
    return h1 >> 8;
}

/* the voice board's output stage: the VCA's sample (voice_amp: s within +-65535) and the board's makeup, Q4 (16 =
 * x1). After voice_amp, so its 32-bit product never sees the gain */
#define VD_OUT(s, m, i, g16) ((voice_amp((s), (m), (i)) * (g16)) >> 4)

/* keyboard tracking: 1/16 semitones from middle C (pitch16 60 * 16) as cutoff units, Q8 of the amount
 * (256 = full: an octave of the key moves the cutoff an octave, 3589 units) */
#define VD_KTRK(pitch16, amt256) ((((pitch16) - 960) * 4785 >> 8) * (amt256) >> 8)

/* ---------------------------------------------------------------- the state --- */
typedef struct {
    int32_t lvl;                         /* Q24, 1.0 = 1 << 24 */
    uint8_t st;                          /* 0 idle, 1 attack, 2 decay / sustain, 3 release, 4 hold (MS-20) */
    uint8_t hold;                        /* MS-20: ticks of HOLD left */
} vd_env_t;

typedef struct {
    vd_env_t aenv, fenv;                 /* the loudness contour (the VCA) and the filter's */
    int32_t f[12];                       /* filter capacitors / integrators (the engine's own layout) */
    int32_t aux[8];                      /* the engine's own (CS-80: the reset glitch, the fixed pole; OB-X: S&H) */
    uint32_t ph[4];                      /* VCO cores (the voice's own ph[] holds three, a CS-80 needs four) */
    int32_t nz;                          /* the noise diode */
    int32_t drift, dtgt;                 /* the thermal walk of the card's expo converters, 1/4096 of the frequency */
    int16_t tol_pitch[2];                /* the card's VCO trims, 1/4096 of the frequency */
    int16_t tol_cut;                     /* the card's filter capacitor: cutoff units */
    int16_t tol_gain;                    /* the card's VCA: Q12 around 4096 */
    uint8_t card;                        /* 1 once the card was made (its tolerances drawn) */
    uint8_t vel;                         /* the note's velocity (CS-80: initial touch) */
    uint16_t walk;                       /* ticks to the next drift target */
} vd_voice_t;

typedef struct {
    vd_voice_t v[NVOICE];
} vd_part_t;

static vd_part_t *vd_part_of(const track_t *t, uint32_t eng) { return (vd_part_t *)eng_arena_of(t, eng); }
static inline vd_voice_t *vd_voice_of(const track_t *t, const voice_t *v, uint32_t eng)
{
    vd_part_t *P = vd_part_of(t, eng);
    return P ? &P->v[(uint32_t)(v - t->v) % NVOICE] : 0;
}

/* --------------------------------------------------------------- the cards --- */
/* a voice card's tolerances, drawn once (a hash of the part and the slot: the same card plays the same every power-on,
 * as a real instrument's cards do). spread_ct: the VCO trim spread in cents, cut: the filter cap's (cutoff units) */
static void vd_card(vd_voice_t *s, const track_t *t, const voice_t *v, uint32_t eng, int32_t spread_ct, int32_t cut)
{
    uint32_t h = 0x9E3779B9u * (uint32_t)((t - trk) * 16 + (v - t->v) + 1) ^ (eng * 0x85EBCA6Bu);
    int32_t r;
    if (s->card)
        return;
    s->card = 1;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    s->nz = (int32_t)(h | 1u);
    r = (int32_t)(h & 255u) - 128;                          /* +-1 */
    s->tol_pitch[0] = (int16_t)(r * spread_ct * 237 / 12800);   /* 1 ct = 2.37 / 4096 */
    r = (int32_t)((h >> 8) & 255u) - 128;
    s->tol_pitch[1] = (int16_t)(r * spread_ct * 237 / 12800);
    r = (int32_t)((h >> 16) & 255u) - 128;
    s->tol_cut = (int16_t)(r * cut / 128);
    r = (int32_t)((h >> 24) & 255u) - 128;
    s->tol_gain = (int16_t)(4096 + r * 2);                  /* +-0.5 dB */
}

/* the expo converters' thermal walk, once a tick: a new target every ~0.4 .. 1.6 s, approached slowly. Returns the
 * drift in 1/4096 of the frequency (depth_ct cents at most) */
static int32_t vd_drift(vd_voice_t *s, int32_t depth_ct)
{
    if (!s->walk) {
        uint32_t r = noise32(&s->nz);
        s->dtgt = ((int32_t)(r & 1023u) - 512) * depth_ct * 237 / 51200;
        s->walk = (uint16_t)(550u + ((r >> 10) & 1663u));
    }
    s->walk--;
    s->drift += (s->dtgt - s->drift) >> 7;
    return s->drift;
}

/* a phase increment detuned by d (1/4096 of the frequency, as vmod_t fine) */
static inline uint32_t vd_detune(uint32_t inc, int32_t d) { return inc + (uint32_t)((int32_t)(inc >> 12) * d); }

/* an interval in 1/16 semitones above the note (any sign), from the pitch the voice plays (m->pitch16) with the same
 * residual fraction (m->fine) as the note */
static inline uint32_t vd_inc_at(const vmod_t *m, int32_t d16)
{
    uint32_t inc = pitch_inc((uint32_t)clamp(m->pitch16 + d16, 0, 2047));
    return m->fine ? vd_detune(inc, m->fine) : inc;
}

/* ----------------------------------------------------------- the contours --- */
enum { VDA_CEM, VDA_MOOG, VDA_LIN };   /* attack: the CEM3310's RC toward 1.5x (Prophet, OB-X, Roland's alike), the
                                        * Model D's toward ~1.3x (a rounder knee), or the CS-80's straight ramp */

/* one tick of a contour. gate: the key; sus Q24; ek: the part's ATK step (Q24 a tick, the linear ramp's) and DEC / REL
 * coefficients (Q16); rel_k: the release's own (the Model D passes DEC's: its DECAY switch); top: the attack's target
 * above 1.0 (VDA_*) */
static void vd_contour(vd_env_t *e, int gate, int32_t sus, uint32_t atk, uint32_t dec, uint32_t rel, uint32_t kind)
{
    if (!gate && (e->st == 1u || e->st == 2u))
        e->st = e->hold ? 4u : 3u;
    switch (e->st) {
    case 1:
        if (kind == VDA_LIN) {
            e->lvl += (int32_t)atk;
        } else {
            /* RC toward top: the time to 1.0 is the knob's (ln(top / (top - 1)) time constants): k = step x that /
             * 2^24, Q16 (top 1.5: ln 3 = 1.0986; 1.3: ln 4.33 = 1.466) */
            uint32_t k = (uint32_t)(((uint64_t)atk * (kind == VDA_MOOG ? 375u : 281u)) >> 16);
            int32_t top = kind == VDA_MOOG ? 21810381 : 25165824;
            if (k > 65535u)
                k = 65535u;
            e->lvl += mulq16(top - e->lvl, k);
        }
        if (e->lvl >= (1 << 24)) {
            e->lvl = 1 << 24;
            e->st = 2;
        }
        break;
    case 2:
        e->lvl += mulq16(sus - e->lvl, dec);
        break;
    case 4:                              /* MS-20: the level held, HOLD ticks, then the release */
        if (!--e->hold)
            e->st = 3;
        break;
    case 3:
        e->lvl -= mulq16(e->lvl, rel);
        if (e->lvl < (1 << 12)) {
            e->lvl = 0;
            e->st = 0;
        }
        break;
    default:
        e->lvl = 0;
        break;
    }
}

/* the note starts: both contours attack from where they are (an analogue contour is a capacitor: a retrigger charges
 * it from its present voltage, it does not jump to 0) */
static void vd_trigger(vd_voice_t *s, const voice_t *v)
{
    s->aenv.st = s->fenv.st = 1;
    s->aenv.hold = s->fenv.hold = 0;
    s->vel = v->vel;
}

/* ENV SYNC aware times of the part: ek[0] the attack's step, ek[1] decay, ek[2] release */
static inline const uint32_t *vd_ek(const track_t *t) { return esync_k[(uint32_t)(t - trk) % NTRK]; }

/* the loudness: the VCA contour, the card's gain and the key velocity (the MIDI convention every SLOOP engine keeps:
 * the sequencer's accents and step velocities. The 1978-83 keyboards had none but the CS-80; documented) */
static int32_t vd_amp_of(const vd_voice_t *s, int32_t env)
{
    int32_t a = s->aenv.lvl >> 9;                         /* Q15 */
    a = (a * s->tol_gain) >> 12;
    a = mulq15(a, s->vel * 258);
    return env >= 32767 ? a : mulq15(a, env);             /* (a voice given up for another part fades: env) */
}
