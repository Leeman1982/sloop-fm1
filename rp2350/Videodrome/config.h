#pragma once
// ============================================================================
//  VIDEODROME (RP2350) — config.h
//  Board : Raspberry Pi Pico 2 / RP2350, 16 MB flash (arduino-pico core)
//  Audio : the FM-1 / SMK-37 Videodrome firmware's synth (the six component models, SLOOP's twelve engines, the drums,
//          the sequencer, the song chain, the effects), 44.1 kHz -> I2S -> PCM5102A
//  Disp  : 128x64 OLED (SSD1309) on hardware I2C
//  Input : 1x rotary encoder w/ push + 5x momentary switches + 2x performance switches (GP2/GP3) + 2x pots (GP26/27)
//
//  The xtro board, pin for pin: leeman1982/xtro's config.h (the CS-80 / New York Ripper board). Nothing below the
//  hardware headings is a free choice; the audio constants are the FM-1 firmware's (firmware/src/core.h).
// ============================================================================
#include <Arduino.h>

// ─── I2S audio out -> PCM5102 ───────────────────────────────────────────────
// BCK<-GP15  LRCK<-GP16 (BCK + 1)  DIN<-GP17  SCK->GND (internal PLL)  FLT/DEMP/FMT->GND  XSMT->3V3
#define PIN_I2S_BCLK   15
#define PIN_I2S_DOUT   17

// ─── 128x64 OLED over hardware I2C (SSD1309, U8g2) ──────────────────────────
#define PIN_OLED_SDA    4
#define PIN_OLED_SCL    5
#define OLED_ADDR      0x3C
#define OLED_I2C_HZ    1000000     // 400000 is the safe fallback

// ─── Rotary encoder (EC11 w/ push) ──────────────────────────────────────────
#define PIN_ENC_A       6
#define PIN_ENC_B       7
#define PIN_ENC_SW      8
#define ENC_TICKS_PER_DETENT 4

// ─── 5 momentary switches (active-low to GND, internal pull-ups) ────────────
#define PIN_BTN_PLAY    9
#define PIN_BTN_SHIFT  10
#define PIN_BTN_PAGE   11
#define PIN_BTN_TRACK  12
#define PIN_BTN_MUTE   13
#define DEBOUNCE_MS    15
#define LONG_PRESS_MS  600

#define PIN_CLICK_OUT  14      // metronome / gate out (the sequencer's click: high for 5 ms a beat while it plays)

// ─── 2 performance momentary switches ───────────────────────────────────────
#define PIN_BTN_OCT_DOWN 2
#define PIN_BTN_OCT_UP   3

// ─── 2 analog pots (GP26/27 = ADC0/ADC1): the selected track's CUTOFF and RESONANCE ─
#define PIN_POT_CUTOFF     26
#define PIN_POT_RESONANCE  27
#define POT_ADC_BITS       12
#define POT_ADC_MAX        4095.0f
#define POT_DEADBAND       (1.0f / 400.0f)

// ─── MIDI: OUT on GP1, IN on GP0, via PIO UART ──────────────────────────────
#define PIN_MIDI_TX     1
#define PIN_MIDI_RX     0
#define MIDI_BAUD   31250

// ─── CPU clock ──────────────────────────────────────────────────────────────
// Tools -> CPU Speed. 250 MHz is the target: the heaviest model (JP-8) at the full 8 voices with the drums and the
// effects measured ~97,000 instructions a 32-sample block on a Cortex-M33 (rp2350/README.md), ~70 % of one core at
// 250 MHz before the second core takes its share. 150 MHz boots and plays; the FM-1's overload guard sheds voices.
// Do NOT re-clock at runtime (USB and core 1 are up before setup()).

// ─── Audio (the FM-1 firmware's) ────────────────────────────────────────────
#define VD_SAMPLE_RATE  44100      // FS (core.h): the tables are made for it
#define VD_HALF_FRAMES  256        // HALF_FRAMES (core.h): a render pass, 5.8 ms
#define VD_I2S_BUFFERS  4          // DMA buffers of a half each: ~23 ms of cushion
