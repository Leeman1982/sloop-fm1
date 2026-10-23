#!/bin/sh
# VIDEODROME uploader (Linux, macOS): sh videodrome-uploader.sh [--cli] [--package FILE.fwsc] [--port NAME]
# The first run makes a private Python environment here (.uploader-venv) with mido and python-rtmidi, then runs
# tools/videodrome_uploader.py (a window; --cli in the terminal). python-rtmidi ships ready-made only for
# Python 3.12 and older, so 3.12 / 3.11 / 3.10 are tried first. On Linux the window needs Tk (Debian / Ubuntu:
# sudo apt install python3-tk python3-venv); without it the terminal version runs. When the setup fails, the
# browser uploader (VIDEODROME-UPLOADER.html: Chrome or Edge, nothing to install) is opened instead.
cd "$(dirname "$0")" || exit 1
browser() {
    echo "$1"
    echo "Opening the browser uploader instead (VIDEODROME-UPLOADER.html: use Chrome or Edge)."
    if command -v open >/dev/null 2>&1 && [ "$(uname)" = Darwin ]; then open VIDEODROME-UPLOADER.html
    elif command -v xdg-open >/dev/null 2>&1; then xdg-open VIDEODROME-UPLOADER.html
    else echo "Open $(pwd)/VIDEODROME-UPLOADER.html in Chrome or Edge."; fi
    exit 1
}
VENV=.uploader-venv
if [ ! -x "$VENV/bin/python" ]; then
    PY=
    for p in python3.12 python3.11 python3.10 python3 python; do
        command -v "$p" >/dev/null 2>&1 && { PY=$p; break; }
    done
    [ -n "$PY" ] || browser "Python 3 is not installed."
    echo "Setting up the uploader once with $PY (a private Python environment, the MIDI modules)..."
    "$PY" -m venv "$VENV" || { rm -rf "$VENV"; browser "Could not make $VENV (Debian / Ubuntu: sudo apt install python3-venv)."; }
fi
"$VENV/bin/python" -c "import mido, rtmidi" 2>/dev/null ||
    "$VENV/bin/python" -m pip install --disable-pip-version-check -q mido python-rtmidi ||
    { rm -rf "$VENV"; browser "Could not install mido / python-rtmidi (internet? Python 3.13 or newer: install 3.12; Linux: sudo apt install libasound2-dev)."; }
exec "$VENV/bin/python" tools/videodrome_uploader.py "$@"
