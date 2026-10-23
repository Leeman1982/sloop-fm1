#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 firmware's sources into the RP2350 sketch (rp2350/Videodrome/fw/), so the Arduino IDE builds the same synth:
the Arduino IDE builds only what is inside the sketch's folder. fw/src: firmware/src (the synth core and the logic the
OLED UI drives), fw/hal: the headers usb.c, eng_sample.c and midi_uart.c include (two of them the RP2350's own, rp2350/port/hal), and in
fw/src too the tables tools/build.py generates (build/gen).
Nothing is changed on the way: the RP2350's own code is rp2350/Videodrome/*.
  python3 tools/rp2350_sync.py           copy (build/gen must exist: ./build.sh, or the host tests, made it)
  python3 tools/rp2350_sync.py --check   exit 1 when the sketch's copy is not the firmware's"""
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "rp2350" / "Videodrome" / "fw"
SRC = ["libc.c", "core.h", "cpu1.h", "dsp.c", "engines.c", "eng_analog.c", "eng_digital.c", "eng_phase.c",
       "eng_lofi.c", "eng_sample.c", "eng_formant.c", "eng_trio.c", "eng_drawbar.c", "eng_grain.c", "eng_fm6.c",
       "fm6_core.c", "fm6_bank.c", "eng_phys.c", "phys_dsp.c", "phys_symp.c", "eng_noise.c", "eng_slice.c",
       "eng_vd_moog.c", "eng_vd_p5.c", "eng_vd_jp8.c", "eng_vd_obx.c", "eng_vd_ms20.c", "eng_vd_cs80.c", "vd_parts.c",
       "vd_tables.h", "preset_trim.h", "scratch.h", "drums.c", "drum_synth.c", "params.c", "voice.c", "slicer.c",
       "fx.c", "punch.c", "usb.c", "midi_uart.c", "arranger.h", "arranger.c", "arranger_scene.c", "seq.c", "ui.c",
       "storage.c", "upreset.c", "project.c", "bootguard.h", "uac_fir.h", "panel.c", "gfx.c",
       "ui_song.c", "ui_studio.c", "icons.c", "ui_draw.c", "ui_vis.c", "ui_layers.c", "ui_menu.c", "ui_input.c", "smk_midi_out.h"]
HAL = ["fm1_cc.h", "fm1_flash_map.h"]                     # (usb.c: its MIDI rings; the controller is never started)
PORT_HAL = ["fm1_xip.h", "fm1_uart.h", "fm1_usb.h"]                # the RP2350's own (rp2350/port/hal): flash window, no UART1
GEN = ["felucca_tables.h", "felucca_samples.h", "felucca_drumkits.h", "felucca_fm6.h", "felucca_font.h", "felucca_icons.h"]


def pairs():
    for n in SRC:
        yield ROOT / "firmware" / "src" / n, FW / "src" / n
    for n in HAL:
        yield ROOT / "firmware" / "hal" / n, FW / "hal" / n
    for n in PORT_HAL:
        yield ROOT / "rp2350" / "port" / "hal" / n, FW / "hal" / n
    for n in GEN:                                     # (next to the sources: the IDE has no include path for them)
        yield ROOT / "build" / "gen" / n, FW / "src" / n


def main(argv):
    check = "--check" in argv
    stale = []
    for a, b in pairs():
        if not a.exists():
            raise SystemExit(f"rp2350_sync: {a.relative_to(ROOT)} is missing" +
                             (" (build/gen: run ./build.sh or tests/run_tests.sh first)" if "gen" in a.parts else ""))
        if not b.exists() or b.read_bytes() != a.read_bytes():
            stale.append(str(b.relative_to(ROOT)))
            if not check:
                b.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(a, b)
    known = {b for _, b in pairs()}
    extra = [p for p in FW.rglob("*") if p.is_file() and p not in known]
    if check:
        if stale or extra:
            print("rp2350_sync: the sketch's fw/ is not the firmware's: " +
                  ", ".join(stale + [f"{p.relative_to(ROOT)} (not the firmware's)" for p in extra]))
            return 1
        print(f"rp2350_sync: fw/ is the firmware's ({len(known)} files)")
        return 0
    for p in extra:
        p.unlink()
    print(f"rp2350_sync: {len(stale)} copied, {len(extra)} removed, {len(known)} in fw/")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
