#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# VIDEODROME on the RP2350: its checks, from the repository's root (needs build/gen: ./build.sh or tests/run_tests.sh).
#   1. the sketch's fw/ is the firmware's (tools/rp2350_sync.py --check)
#   2. rp2350/test/rp2350_test.c: the sketch's synth and UI on the host, against U8g2's own C core, under ASan (and the
#      divide-by-zero check of tests/run_tests.sh: the FM-1 code shifts signed values on purpose)
#      (U8G2_CLIB: its src/clib; default: the Arduino library's, ~/Arduino/libraries/U8g2/src/clib)
#   3. with arduino-cli and the arduino-pico core: the sketch built for the RP2350 (250 MHz, 16 MB, FS 2 MB) and at
#      150 MHz, its ELF checked (tools/rp2350_check_elf.py)
#   rp2350/test/run.sh [OUT_DIR]          (the screens as OUT_DIR/*.pbm)
set -e
cd "$(dirname "$0")/../.."
OUT="${1:-build/rp2350}"
mkdir -p "$OUT"
python3 tools/rp2350_sync.py --check
U8G2_CLIB="${U8G2_CLIB:-$HOME/Arduino/libraries/U8g2/src/clib}"
if [ -f "$U8G2_CLIB/u8g2.h" ]; then
    if [ ! -f "$OUT/libu8g2.a" ]; then
        mkdir -p "$OUT/u8g2"
        for f in "$U8G2_CLIB"/*.c; do cc -O1 -w -c -I"$U8G2_CLIB" "$f" -o "$OUT/u8g2/$(basename "$f" .c).o"; done
        ar rcs "$OUT/libu8g2.a" "$OUT"/u8g2/*.o
    fi
    cc -std=gnu11 -O1 -g -w -fsanitize=address,integer-divide-by-zero -fno-sanitize-recover=integer-divide-by-zero \
        -I"$U8G2_CLIB" \
        rp2350/test/rp2350_test.c "$OUT/libu8g2.a" -lm -o "$OUT/rp2350_test"
    "$OUT/rp2350_test" "$OUT"
else
    echo "rp2350: no U8g2 C core ($U8G2_CLIB): host test skipped"
fi
CLI="${ARDUINO_CLI:-$(command -v arduino-cli || echo "$HOME/tools/arduino-cli")}"
if [ -x "$CLI" ] && "$CLI" core list 2>/dev/null | grep -q "^rp2040:rp2040"; then
    for MHZ in 250 150; do
        "$CLI" compile --fqbn "rp2040:rp2040:generic_rp2350:flash=16777216_2097152,freq=$MHZ,opt=Optimize2" \
            --build-path "$OUT/build-$MHZ" rp2350/Videodrome > "$OUT/build-$MHZ.log" 2>&1 ||
            { cat "$OUT/build-$MHZ.log"; exit 1; }
        grep -E "Sketch uses|Global variables" "$OUT/build-$MHZ.log" | sed "s/^/rp2350 ($MHZ MHz): /"
    done
    BIN="$(dirname "$(find "$HOME/.arduino15/packages/rp2040/tools/pqt-gcc" -name arm-none-eabi-objdump | head -1)")"
    python3 tools/rp2350_check_elf.py "$OUT/build-250/Videodrome.ino.elf" "$BIN/arm-none-eabi-objdump" "$BIN/arm-none-eabi-nm"
else
    echo "rp2350: no arduino-cli with the rp2040 core: the RP2350 build skipped"
fi
echo "rp2350: all checks passed"
