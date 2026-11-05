@echo off
REM Makes fm1-300.bin (OpenPocket + your original firmware) for the SD-card recovery, in Docker.
REM Usage: drag your fm1-1.bin dump onto this .bat, or put it here as fm1-1.bin. Run build.bat first.
setlocal
cd /d "%~dp0"
set DUMP=%~1
if "%DUMP%"=="" set DUMP=fm1-1.bin
docker run --rm --platform linux/amd64 -v "%cd%":/src -w /src openpocket-build python3 tools/pocketx_appimage.py "%DUMP%" firmware/openpocket.bin fm1-300.bin
pause
