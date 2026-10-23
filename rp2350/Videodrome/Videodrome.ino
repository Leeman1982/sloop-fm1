// SPDX-License-Identifier: GPL-3.0-only
// ============================================================================
//  VIDEODROME (RP2350) — the FM-1 / SMK-37 Videodrome firmware's synth on the xtro board.
//
//  The synth is the FM-1's, its sources unchanged (fw/, tools/rp2350_sync.py), built as one C translation unit
//  (vd_core.c). This file is the board: the I2S DAC, the OLED, xtro's panel, MIDI on its DIN pins, the flash, and the
//  two cores. Who runs what, as on the FM-1:
//    core 0  the I2S DMA interrupt renders each half buffer (vd_audio_fill: the FM-1's audio interrupt)
//            a 1 kHz timer: the millisecond clock, MIDI in and out, the click out (vd_tick)
//            loop(): the panel, the OLED, the FM-1's per-frame work and its flash writes when quiet (vd_frame)
//    core 1  loop1(): its share of every render block (vd_core1_poll: the FM-1's second core)
//
//  Arduino IDE, arduino-pico (Earle Philhower): Board "Generic RP2350" (or Raspberry Pi Pico 2), Flash Size 16 MB with
//  an FS of at least 1 MB (e.g. "16MB (Sketch: 14MB, FS: 2MB)": the projects, sections, user presets and samples live
//  there, in the FM-1's own format), CPU Speed 250 MHz, USB Stack "Pico SDK". Libraries: U8g2. See rp2350/README.md.
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <I2S.h>
#include <SerialPIO.h>
#include <hardware/flash.h>
#include <hardware/irq.h>
#include <pico/time.h>
#include "config.h"
#include "controls.h"

extern "C" {
#include "vd_api.h"
}

static U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
static I2S i2s(OUTPUT);
static SerialPIO midi(PIN_MIDI_TX, PIN_MIDI_RX, 64);
static Controls ctl;
static volatile bool core1_go;
// core 1 on its own 8 KB stack (from the heap): scratch X, its default, stays below core 0's stack, which grows into
// it: 8 KB for core 0, where the audio interrupt renders on top of whatever loop() is doing (the deepest render
// measured 1.4 KB: the PHYS engine, rp2350/README.md)
bool core1_separate_stack = true;

// ─── the flash partition: the FM-1's storage offsets, 1 MB of them, at the start of the sketch's FS ─────────────────
extern "C" uint8_t _FS_start, _FS_end;
static uint32_t fs_off() { return (uint32_t)(&_FS_start) - XIP_BASE; }
extern "C" uint32_t vd_store_size(void) { return (uint32_t)(&_FS_end - &_FS_start); }
extern "C" const uint8_t *vd_store_base(void) { return &_FS_start; }
// erase / program with core 1 parked and interrupts off (the XIP flash is away while it works)
extern "C" int vd_flash_erase(uint32_t off)
{
    if (off + FLASH_SECTOR_SIZE > vd_store_size())
        return -8;
    rp2040.idleOtherCore();
    noInterrupts();
    flash_range_erase(fs_off() + off, FLASH_SECTOR_SIZE);
    interrupts();
    rp2040.resumeOtherCore();
    return 0;
}
extern "C" int vd_flash_prog(uint32_t off, const uint8_t *page)
{
    if ((off & (FLASH_PAGE_SIZE - 1)) || off + FLASH_PAGE_SIZE > vd_store_size())
        return -8;
    rp2040.idleOtherCore();
    noInterrupts();
    flash_range_program(fs_off() + off, page, FLASH_PAGE_SIZE);
    interrupts();
    rp2040.resumeOtherCore();
    return 0;
}

// ─── MIDI OUT (GP1): a byte whenever the PIO UART takes one ──────────────────────────────────────────────────────────
extern "C" int vd_midi_tx_done(void) { return midi.availableForWrite() > 0; }
extern "C" void vd_midi_tx_put(uint8_t b) { midi.write(b); }

// ─── audio: each free DMA buffer, a half buffer rendered (core 0, the DMA interrupt) ────────────────────────────────
static int32_t abuf[2 * VD_HALF_FRAMES];
static void on_i2s()
{
    while (i2s.availableForWrite() >= (int)sizeof abuf) {
        vd_audio_fill(abuf, VD_HALF_FRAMES);
        i2s.write((const uint8_t *)abuf, sizeof abuf);
    }
}

// ─── the 1 kHz tick (core 0): the clock, MIDI, the click out ─────────────────────────────────────────────────────────
static repeating_timer_t tick_timer;
static bool on_tick(repeating_timer_t *)
{
    while (midi.available() > 0)
        vd_midi_in((uint8_t)midi.read());
    digitalWrite(PIN_CLICK_OUT, vd_tick(millis()) ? HIGH : LOW);
    return true;
}

// The FM-1 sources' own sections: .pool and .lowram land between .data and .bss, their (zero) images right after
// .data's in flash, so the start-up copy brings them in zeroed (as fm1_cstart zeroes them on the FM-1). .noinit lands
// after .bss, which nothing clears: cleared here, first, so a power-on is the same every time.
extern "C" char __bss_end__, __end__;   // (declared so by the core: RP2040Support.h)

void setup()
{
    memset(&__bss_end__, 0, (size_t)(&__end__ - &__bss_end__));
    Wire.setSDA(PIN_OLED_SDA);
    Wire.setSCL(PIN_OLED_SCL);
    oled.setI2CAddress(OLED_ADDR << 1);
    oled.setBusClock(OLED_I2C_HZ);
    oled.begin();                                   // the panel first: the boot log names every stage after it
    vd_ui_attach(oled.getU8g2(), (uint32_t)(rp2040.f_cpu() / 1000000u));
    pinMode(PIN_CLICK_OUT, OUTPUT);
    digitalWrite(PIN_CLICK_OUT, LOW);
    ctl.begin();
    core1_go = true;                                // (core 1's loop takes jobs from here: the self-test in vd_init)
    vd_init();                                      // storage, the engines, the second core
    i2s.setBCLK(PIN_I2S_BCLK);
    i2s.setDATA(PIN_I2S_DOUT);
    i2s.setBitsPerSample(32);
    i2s.setBuffers(VD_I2S_BUFFERS, 2 * VD_HALF_FRAMES);
    i2s.onTransmit(on_i2s);
    bool audio = i2s.begin(VD_SAMPLE_RATE);
    // The render is long (up to ~5 ms of a 5.8 ms buffer): its DMA interrupt goes below everything else, so the PIO
    // UART's receive interrupt (a byte every 320 us; its FIFO holds 4), the 1 kHz tick and USB nest into it, as the
    // FM-1's TIMER5 outranks its audio interrupt (main.c timer5_start). What the tick shares with the render are the
    // FM-1's single-producer / single-consumer MIDI rings.
    irq_set_priority(DMA_IRQ_0, PICO_LOWEST_IRQ_PRIORITY);
    vd_boot("AUDIO", audio ? "OK" : "FAIL", "MIDI");
    midi.begin(MIDI_BAUD);
    add_repeating_timer_ms(-1, on_tick, nullptr, &tick_timer);
    vd_boot("MIDI", "OK", "SIGNAL");
    delay(450);
    vd_boot("SIGNAL", "ACQUIRED", nullptr);         // on the air
    delay(700);
}

void loop()
{
    vd_ctl_t c = {};
    ctl.update();
    c.enc = ctl.encoder.getDelta();
    c.push = ctl.encoder.wasPressed();
    c.push_long = ctl.encoder.wasLongPress();
    c.pressed = (uint8_t)((ctl.play.wasDown() ? 1u << VB_PLAY : 0u) | (ctl.page.wasPressed() ? 1u << VB_PAGE : 0u) |
                          (ctl.track.wasPressed() ? 1u << VB_TRACK : 0u) | (ctl.mute.wasPressed() ? 1u << VB_MUTE : 0u) |
                          (ctl.octDown.wasPressed() ? 1u << VB_OCTDN : 0u) | (ctl.octUp.wasPressed() ? 1u << VB_OCTUP : 0u));
    ctl.shift.wasPressed();                         // (SHIFT is held, never pressed on its own)
    c.held = (uint8_t)((ctl.shift.isHeld() ? 1u << VB_SHIFT : 0u) | (ctl.octDown.isHeld() ? 1u << VB_OCTDN : 0u) |
                       (ctl.octUp.isHeld() ? 1u << VB_OCTUP : 0u));
    c.pot[0] = ctl.cutoffPot.changed() ? (int16_t)(ctl.cutoffPot.value() * 4095.0f) : -1;
    c.pot[1] = ctl.resonancePot.changed() ? (int16_t)(ctl.resonancePot.value() * 4095.0f) : -1;
    vd_frame(&c);
}

// ─── core 1: its share of the render ─────────────────────────────────────────────────────────────────────────────────
void setup1()
{
    while (!core1_go)
        ;
}

void loop1()
{
    vd_core1_poll();
}
