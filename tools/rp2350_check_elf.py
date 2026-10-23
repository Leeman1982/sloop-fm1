#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The RP2350 build's ELF (arduino-cli's Videodrome.ino.elf), against what the port relies on:
  - the FM-1 sources' own RAM sections (.pool, .lowram) lie inside [__data_start__, __data_end__) with their images
    right after .data's in flash, so the start-up copy (pico-sdk crt0: __etext -> __data_start__ .. __data_end__) brings
    them in zeroed, as fm1_cstart zeroes them on the FM-1. Placed anywhere else they would start as garbage;
  - .noinit lies in [__bss_end__, __end__), which Videodrome.ino clears in setup();
  - no FM-1 RAM-code section (.ram_text, .c1_text) is left over: nothing of the FM-1's must run from its RAM here;
  - the heap left for core 1's stack (8 KB), the I2S buffers and the C library: 64 KB or more.
  tools/rp2350_check_elf.py ELF [OBJDUMP NM]"""
import subprocess
import sys

elf = sys.argv[1]
objdump = sys.argv[2] if len(sys.argv) > 2 else "arm-none-eabi-objdump"
nm = sys.argv[3] if len(sys.argv) > 3 else "arm-none-eabi-nm"
secs = {}
for line in subprocess.run([objdump, "-h", elf], capture_output=True, text=True, check=True).stdout.splitlines():
    p = line.split()
    if len(p) >= 6 and p[0].isdigit():
        secs[p[1]] = (int(p[2], 16), int(p[3], 16), int(p[4], 16))      # size, vma, lma
sym = {}
for line in subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        sym[p[2]] = int(p[0], 16)
bad = []
d0, d1 = sym["__data_start__"], sym["__data_end__"]
data_lma = secs[".data"][2] - secs[".data"][1]                          # lma - vma of .data: the copy's offset
for s in (".pool", ".lowram"):
    if s not in secs:
        continue
    size, vma, lma = secs[s]
    if not (d0 <= vma and vma + size <= d1):
        bad.append(f"{s} at {vma:#x}..{vma + size:#x} is outside the start-up copy {d0:#x}..{d1:#x}")
    elif lma - vma != data_lma:
        bad.append(f"{s}'s image is not where the copy reads it (lma - vma {lma - vma:#x}, .data's {data_lma:#x})")
if ".noinit" in secs:
    size, vma, _ = secs[".noinit"]
    if not (sym["__bss_end__"] <= vma and vma + size <= sym["__end__"]):
        bad.append(f".noinit at {vma:#x} is not in [__bss_end__, __end__): setup() would not clear it")
for s in (".ram_text", ".c1_text"):
    if s in secs and secs[s][0]:
        bad.append(f"{s} ({secs[s][0]} B): FM-1 RAM code in the RP2350 build")
heap = sym["__HeapLimit"] - sym["__end__"]
if heap < 64 * 1024:
    bad.append(f"heap {heap // 1024} KB < 64 KB")
ram = sym["__end__"] - 0x20000000
print(f"rp2350 elf: RAM {ram // 1024} KB static (.pool {secs.get('.pool', (0,))[0] // 1024} KB), heap {heap // 1024} KB; "
      f"flash {(secs['.data'][2] + secs['.data'][0] - 0x10000000) // 1024} KB" + ("" if bad else "; ok"))
for b in bad:
    print("rp2350 elf: " + b)
sys.exit(1 if bad else 0)
