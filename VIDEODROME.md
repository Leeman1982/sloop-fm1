# VIDEODROME

**CIVIC-TV, channel 83.** A firmware for the M-VAVE **FM-1** and **SMK-37** (Pro, Elite, MKE-P37): six classic
synthesizers from [xtro](https://github.com/leeman1982/xtro), rebuilt a circuit stage at a time, on top of
[SLOOP](https://github.com/isod89/sloop-fm1) 2.8's live groovebox (four tracks, the drum machine, the sequencer,
song mode, the punch-in effects, USB audio, MIDI), with the look of an early-80s cable channel on a tired set.

> **Status: untested on hardware.** Everything here builds, and runs in the host test suites (the DSP, the UI pages,
> the editor, the installer). None of it has been played on an FM-1 or an SMK-37 yet. Make a backup first (the
> editor's Projects → Backup; on an SMK-37 a full flash backup, see [SMK37.md](SMK37.md)), and report what you find.

---

## Contents

1. [The six models](#the-six-models)
2. [What "component level" means here](#what-component-level-means-here)
3. [Controls of each model](#controls-of-each-model)
4. [The sounds](#the-sounds)
5. [The look](#the-look)
6. [Songs: the chain builder](#songs-the-chain-builder)
7. [Install](#install)
8. [Build](#build)
9. [Where it differs from the real instruments](#where-it-differs-from-the-real-instruments)
10. [What it cost](#what-it-cost)
11. [Credits and licence](#credits-and-licence)

---

## The six models

Picked from xtro's seventeen for 1983: the synths on the records and the scores of the years the film was made, and
six filters that sound nothing alike.

| Engine | Instrument | The circuit modelled | Voices |
|---|---|---|---|
| **MODEL D** | Minimoog Model D (1970–81) | three free-running VCOs, the mixer overdrive, the **transistor ladder** | 1 (it was monophonic) |
| **P-5** | Sequential Prophet-5 rev 3 (1980–84) | CEM3340 VCOs, hard sync, **poly-mod**, the **CEM3320** four-pole | 5 |
| **JP-8** | Roland Jupiter-8 (1981–84) | VCO-1 cross-mod and sync, the passive HPF, the **IR3109** OTA four-pole | 8 |
| **OB-X** | Oberheim OB-X (1979–81) | sync, cross-mod, the **SEM state-variable** filter, the voice cards' spread | 8 |
| **MS-20** | Korg MS-20 (1978–83, KORG35 board) | **KORG35 Sallen-Key** high-pass into low-pass, diodes in each loop | 1 (monophonic) |
| **CS-80** | Yamaha CS-80 (1977–80) | two channels a voice, reset-glitch VCO cores, the **IG00156** pair, IL/AL contour | 8 |

They are engines 12–17, after SLOOP's twelve (ANALOG … NOISE), which all stay: SLOOP's projects, user presets and
favourites load as they are. The **PRESETS** list shows the six models first.

## What "component level" means here

Every engine is built from the parts its instrument was built from (`firmware/src/vd_parts.c`, the engines in
`firmware/src/eng_vd_*.c`), in fixed point (the FM-1's processor has no FPU):

- **Three different filter cells, not one filter with three names.** The Moog ladder's rung charges its capacitor
  with *tanh(in) − tanh(out)* (a transistor pair at each end, Huovilainen's model); a CEM3320 cell saturates at its
  input and integrates linearly; an IR3109 OTA charges with *tanh(in − out)* (it slews rather than clips). Same
  four-pole topology, different sound when driven — that is the difference between a Minimoog, a Prophet and a
  Jupiter.
- **Solved zero-delay** (Zavalishin's TPT): the feedback is found for the sample being computed, so the resonance
  sits where the circuit's does at every cutoff. The ladder and the CEM3320 self-oscillate from k = 4, at the cutoff,
  from 130 Hz to 14 kHz (an explicit one-sample-late loop needed k 4.1 at 100 Hz and over 12 at 6 kHz). The IR3109
  only whistles at the very top of RES. The KORG35 sections scream from K = 2, the diodes bounding the scream. The
  SEM and the CS-80's IG00156 never self-oscillate (their damping has a floor), as on the instruments.
- **The Moog bass loss is kept**: the ladder's passband falls as 1 / (1 + k), so emphasis thins the low end.
- **The capacitors keep 8 bits under the signal**: at a low cutoff a sample moves a capacitor by a fraction of the
  signal's step, and without those bits the filter would freeze in a dead band (a DC limit cycle no capacitor has).
- **Voice cards**: each voice gets the tolerances it left the factory with — its VCO trims, its filter capacitor, its
  VCA gain — and a slow thermal drift of its expo converters (a random walk). A Prophet rev 3 has little (auto-tune),
  an OB-X a lot: its **SLOP** knob sets how tired the eight cards are.
- **Contour generators as RC circuits**: an attack charges a capacitor toward a target above the peak and a comparator
  stops it at 1.0 (the CEM3310's concave curve, not a straight ramp); a retrigger charges from the present level.
  Each model's own: the Model D has no release knob (its DECAY switch), the CS-80's filter contour starts from its
  INITIAL LEVEL, the MS-20 drives the cutoffs and the VCA from one contour (its patch panel's normalling).
- **The output coupling capacitor** every one of these boards has: pulse widths and hard sync leave no DC.

`tests/vd_test.c` checks these claims on the code that runs: the resonance and its bounds at four cutoffs for every
filter, the bass loss, the contours, and every one of the 96 presets (sound, bounded, no DC, the voice frees).

## Controls of each model

Each model has two EDIT pages of four knobs (the first two names below), plus SLOOP's common pages: **ENV** (ATK, DEC,
SUS, REL: the VCA's contour, and ENV > FLT the filter contour's amount), **LFO**, **ARP**, **FX**, **VOICE** (poly,
mono, legato, unison, glide). The filter contour runs on the same times as the VCA's.

| Engine | Page 1 | Page 2 |
|---|---|---|
| MODEL D | **WAVE** (tri, shark tooth, saw, square, wide, narrow; osc 3 a reverse saw), **DTN** (osc 2 up / osc 3 down, cents), **OSC3** (its level, 16'), **NOIS** | **CUT**, **EMPH**, **DRV** (the mixer overdrive), **KBD** (off, 1/3, 2/3, full). REL is the DECAY switch: 0 off, else on. ENV > FLT is positive only (the panel's AMOUNT OF CONTOUR) |
| P-5 | **OSC A** (saw, pulse, both, synced saw, synced both), **OSC B** (saw, tri, pulse, both, off), **B PIT** (semitones), **PW** | **CUT**, **RES**, **PM ENV** (poly-mod: filter contour → osc A frequency), **PM OSB** (poly-mod: osc B → cutoff, at audio rate) |
| JP-8 | **VCO1** (tri, saw, pulse, square, synced saw, synced square), **VCO2** (sine, saw, pulse, noise), **V2 PIT**, **XMOD** (VCO-2 → VCO-1 frequency) | **CUT**, **RES**, **HPF**, **SLOPE** (12 / 24 dB) |
| OB-X | **VCO1** (saw, pulse, both), **VCO2** (saw, pulse, both, synced saw, synced pulse), **V2 PIT**, **PW** | **CUT**, **RES**, **XMOD**, **SLOP** (the cards' spread) |
| MS-20 | **VCO1** (tri, saw, pulse, noise), **VCO2** (saw, square, narrow pulse, ring), **PITCH** (±1 octave), **MIX** | **HPF**, **HPK** (its peak), **LPF**, **LPK** |
| CS-80 | **I WAVE**, **II WAVE** (saw, pulse, both, sine), **II PIT**, **BAL** (I / II) | **CUT**, **RES**, **HPF**, **IL** (the filter contour's initial level; ENV > FLT is its attack level) |

## The sounds

Sixteen a model, 96 in all, level-matched with SLOOP's own tool (`tools/level_presets.py`, BS.1770 loudness). Some
from xtro's banks, some for the film:

- **MODEL D**: MOOG BASS, SUB BASS, ACID BASS, FUNK BASS, GROWL BASS, MOOG LEAD, TB LEAD, FIFTH LEAD, SCREAM, MOOG
  BRASS, SOLO SAX, SELF-OSC, NOISE SWP, LONG LIVE, MAX RENN, SEQ PLUCK
- **P-5**: P5 BRASS, SYNC LEAD, POLY BASS, DEEP BASS, GROWL BASS, P5 PAD, GLASS PAD, PWM STRING, P5 STAB, E.PIANO, CLAV
  FUNK, BELL PLUCK, CIVIC TV, NEW FLESH, RESO LEAD, FILT SWEEP
- **JP-8**: JP8 BRASS, LUSH PAD, STRING PAD, XMOD BELL, XMOD LEAD, SYNC LEAD, JP8 BASS, XMOD BASS, BRIGHT PAD, POLY STAB,
  GLASS PLUK, ARENA ARP, NICKI, SPECTACLE, NOISE SWP, 24DB LEAD
- **OB-X**: OBX BRASS, JUMP STAB, 1-FINGER, PUNCH BASS, FAT BASS, SYNC LEAD, XMOD LEAD, UNISON LD, STRING PAD, WARM
  PAD, HORROR STB, HAUNTED, VIDEODROME, BIANCA, CHORD STAB, BELL PLUCK
- **MS-20**: ACID BASS, SCRM BASS, SUB BASS, SCREAM LD, RING LEAD, HPF SCREAM, SELF-OSC, LASER PLUK, SIREN, NOISE SWP,
  METAL HIT, BRASS STAB, HAUNT PAD, FLESH GUN, CATHODE, SWOOP LEAD
- **CS-80**: BLADE BRSS, TEARS RAIN, CS STRINGS, CS BRASS, YAMAHA PAD, SINE ORGAN, BELL, CS BASS, CS LEAD, HARPSICH,
  HUMAN VOX, RANNOCH, SIGNAL, TV STATIC, WARM BRASS, GLIDE LEAD

SLOOP's 153 sounds follow them in the list.

## The look

Researched from the film (1983) and the cable television it is about: the title card arrives bright orange, *bent
and distorted*, out of static; the station is CIVIC-TV, channel 83, cable 12, modelled on Toronto's CITY-TV; the
screens are character-generator type, colour bars and a set's green channel digits.

- **CIVIC 83**, the default palette of a fresh unit (MENU → SCREEN → COLOR; SLOOP's five palettes and your own theme
  are still there): five steps of phosphor in the title card's orange, and a **CRT raster** — every other line of
  the screen dimmed, through text and all. The other palettes and your theme have no raster.
- **The tracks** in the colour bars' cyan, magenta and yellow, the drums in the title's orange (also the SMK-37's pads).
- **The boot screen**: colour bars, the wordmark coming in bent with a red and a cyan ghost (misconverged guns),
  CIVIC-TV CHANNEL 83 CABLE 12, LONG LIVE THE NEW FLESH, VHS tracking streaks (`tools/gen_vd_splash.py`).
- **TEST CARD**, the visualiser's 12th style (HOME on the TRACKS screen, SELECT): the bars torn sideways by the
  master's wave like a VCR losing its tracking, the tracks' bars flaring on their notes, PLEASE STAND BY, CH 83.
- **The editor** in the same colours, the wordmark, *ch 83*, and the raster over the page (the **crt** switch under
  the pages turns it off; the browser remembers).

## Songs: the chain builder

A song is SLOOP's: up to **16 entries**, each one of the four sections **A–D** (SAVE → SECTION on the device) for
**1–64 bars**, played once or looped (song mode: SAVE + key 13). The editor's **Song** page now builds it the easy way:

- **Tap A, B, C or D** to add that section at the end, for the bars chosen next to them.
- **Or type it**: `A4 B8 C8x2 B8 D4` — a section, its bars (left out: the last ones given), `x` and a count to repeat.
  Separators can be spaces, commas, `>` or `-`. What it cannot read is named.
- **Or start from a song shape**: loop, verse/chorus, build-up, minimal, drone, film score, broadcast.
- **The timeline** shows the blocks as long as their bars (tap one to take it out), with the song's length in bars
  and in minutes at the device's tempo.
- **Send to the FM-1** writes it (stop the song first). The table below still edits each entry.

Nothing changed on the device side: the same song order (`arranger.h`), so a song made here plays on SLOOP too.

## Install

**From the browser** (Chrome or Edge, desktop or Android): the site in `docs/` — `docs/webapp/installer/` for the
FM-1, `docs/smk37/webapp/installer/` for the SMK-37 — on this fork's GitHub Pages once enabled (Settings → Pages →
branch, `/docs`). The installer finds the device by its handshake and refuses a package for another board.

**The packages** are in `releases/`:

| File | For |
|---|---|
| `videodrome-1.0-fm1.fwsc` | M-VAVE FM-1 |
| `videodrome-1.0-smk37.fwsc` | M-Vave SMK-37 Pro |
| `videodrome-1.0-smk37-elite.fwsc` | SMK-37 Elite (the same keyboard under its own model name) |
| `videodrome-1.0-smk37-mke.fwsc` | MKE-P37 (likewise) |

Videodrome uses SLOOP's flash layout and package identity: going from SLOOP to Videodrome and back keeps your
projects, presets, samples and settings. On the way back a track on one of the six models opens on a SLOOP engine
(the engine number taken modulo SLOOP's twelve), and SLOOP resets its screen settings once (it has no palette 5).

**The SMK-37** build is the community board layer by genkigenki (`fm1-to-smk37`, the reference port of SLOOP),
merged here into SLOOP 2.8. Read [SMK37.md](SMK37.md) first: a full flash backup, the protected flash ranges, the way
back to the M-Vave firmware.

## Build

As SLOOP's ([BUILDING.md](BUILDING.md)): the JieLi toolchain, three files of the AC79 SDK, then

```
./build.sh                    # FM-1:   build/felucca.fwsc
BOARD=smk37 ./build.sh        # SMK-37: build/sloop-smk37.fwsc  (MODEL=elite / mke for the other two names)
tests/run_tests.sh            # the host tests (tests/vd_test.c among them)
```

## Where it differs from the real instruments

Said plainly, so nothing here is mistaken for more than it is:

- **Velocity reaches the VCA on all six.** Only the CS-80 had a velocity-sensing keyboard (its initial touch, which here
  also opens its filters). The others follow the MIDI convention every SLOOP engine keeps, so the sequencer's accents
  and step velocities work.
- **The filter contour shares the VCA's times** (SLOOP's ENV page has one set of A/D/S/R; ENV > FLT is the amount). The
  real instruments have a second set of knobs.
- **Eight knobs a model**: not every panel control is here. Not modelled: the Model D's osc 3 as a modulation
  source and its pink noise, the OB-X's sample & hold and chord memory, the JP-8's VCO-2 low-frequency range, the
  MS-20's EG2 HOLD time, its ESP and its patch panel, the CS-80's ribbon, polyphonic aftertouch, sub-oscillator and
  ring modulator.
- **Hard sync is not band-limited**: high synced notes alias where the analogue circuit would not.
- **Uncertain details**: the JP-8's sync direction (VCO-1 reset by VCO-2) and its 12 dB mode (output from the second
  integrator, the resonance from the fourth) are my reading, not checked against a schematic. The MS-20's RING is
  VCO 1 times VCO 2's square.
- **The CS-80 is not 8 × 2 × everything**: its fixed 7.6 kHz pole runs once on the two channels' mix (it is linear, so
  the sound is the same), and its high-pass is left out at its lowest setting (it passes everything a voice makes
  there). Its pulse width comes from SLOOP's LFO / ENV > SHP rather than a PWM LFO per channel.

## What it cost

- **Flash**: the FM-1's app slot is 581,564 bytes and SLOOP 2.8 used 567,308. The six models and their presets did not
  fit, so two of SLOOP's sample sets were thinned to their middle zone (**FLUTE** and **VIBES**, −43 KB; each still
  plays, stretched over more keys; their set numbers did not change). Now: FM-1 ~551,600 bytes, SMK-37 ~576,200 of 619,520.
- **CPU** (instructions counted on the host under callgrind, so a guide, not a measurement on the device), per
  voice against SLOOP's ANALOG: MODEL D 2.7× (three VCOs and five tanh rungs, but one voice), JP-8 2.2×, MS-20 2.0×
  (one voice), P-5 1.9× (five voices at most), CS-80 1.5× (both channels), OB-X 1.0×; SLOOP's heaviest, PHYS, is 1.3×.
  The voice budget is SLOOP's eight across all tracks, so the worst case is eight JP-8 voices: 1.6× eight PHYS ones.
  SLOOP's owner measured a typical pattern at 31 % of one core (23 % with the second core on). **Not yet measured on
  the device.**

## Credits and licence

- **The models**: xtro (GPLv3), by its author — the six engines' architecture, their behaviour and many of the presets.
- **SLOOP** by isod89 (GPL-3.0): the groovebox, the sequencer, the editor and the installer this is built on.
- **Felucca** by Leo Kuroshita (@kurogedelic), Hügelton Instruments (GPL-3.0): the firmware SLOOP is based on.
- **The SMK-37 board layer** by M. Geisler (genkigenki, makkuro), from `genkigenki/sloop-fm1` (GPL-3.0).
- The filter models after the published work of Antti Huovilainen (the transistor ladder), Vadim Zavalishin (*The Art
  of VA Filter Design*: the TPT cascades and the KORG35) and Andrew Simper (the trapezoidal SVF).
- Font: Terminus (OFL). Samples: Versilian Studios and Sonic Pi (CC0).

GPL-3.0, no warranty. Minimoog, Model D, Prophet-5, Jupiter-8, OB-X, MS-20, CS-80, M-VAVE, FM-1 and SMK-37 are
trademarks of their respective owners; *Videodrome* (1983) is David Cronenberg's film. This firmware is a tribute
and is not affiliated with any of them.
