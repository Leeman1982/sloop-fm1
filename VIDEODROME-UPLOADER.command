#!/bin/sh
# VIDEODROME uploader (macOS): double-click in the Finder. The first run makes a private Python environment next to
# this file (.uploader-venv) with the two MIDI modules (mido, python-rtmidi); later runs just open the window.
# Needs Python 3 with Tk: the python.org installer has it (Homebrew's: brew install python-tk).
cd "$(dirname "$0")" || exit 1
exec sh ./videodrome-uploader.sh "$@"
