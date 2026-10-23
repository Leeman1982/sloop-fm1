@echo off
rem VIDEODROME uploader (Windows): double-click. The first run makes a private Python environment next to this file
rem (.uploader-venv) with the two MIDI modules the uploader needs (mido, python-rtmidi); later runs just open it.
rem python-rtmidi ships ready-made only for Python 3.12 and older, so 3.12 / 3.11 / 3.10 are tried first. When there
rem is no Python or the setup fails, the browser uploader (VIDEODROME-UPLOADER.html: Chrome or Edge) opens instead.
title VIDEODROME uploader
cd /d "%~dp0"
if exist ".uploader-venv\Scripts\python.exe" goto modules
set PY=
for %%v in (3.12 3.11 3.10) do if not defined PY py -%%v -c "" >nul 2>nul && set PY=py -%%v
if not defined PY where py >nul 2>nul && set PY=py -3
if not defined PY where python >nul 2>nul && set PY=python
if not defined PY (
  echo Python 3 is not installed ^(https://www.python.org/downloads/ : take 3.12, tick "Add python.exe to PATH"^).
  goto browser
)
echo Setting up the uploader once with %PY% ^(a private Python environment, the MIDI modules^)...
%PY% -m venv .uploader-venv || goto fail
:modules
".uploader-venv\Scripts\python.exe" -c "import mido, rtmidi" 2>nul || (
  ".uploader-venv\Scripts\python.exe" -m pip install --disable-pip-version-check -q mido python-rtmidi || goto fail
)
start "" ".uploader-venv\Scripts\pythonw.exe" tools\videodrome_uploader.py %*
exit /b 0
:fail
rmdir /s /q .uploader-venv 2>nul
echo.
echo The setup failed ^(see above: the internet connection, or Python 3.13 or newer: install 3.12^).
:browser
echo Opening the browser uploader instead ^(VIDEODROME-UPLOADER.html: use Chrome or Edge^).
start "" "VIDEODROME-UPLOADER.html"
pause
exit /b 1
