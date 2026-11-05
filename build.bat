@echo off
REM Builds OpenPocket in Docker (the same container as make build on macOS and Linux): see docs\BUILDING.md.
REM  -> firmware\openpocket.bin, docs\firmware\*.fwsc + manifest.json
setlocal
cd /d "%~dp0"
docker build --platform linux/amd64 -t openpocket-build . || goto fail
docker run --rm --platform linux/amd64 -v "%cd%":/src -w /src openpocket-build sh firmware/build.sh || goto fail
echo.
echo SUCCESS: see docs\firmware\ and firmware\openpocket.bin
if not "%~1"=="nopause" pause
exit /b 0
:fail
echo.
echo BUILD FAILED: read the messages above. Is Docker Desktop running? See docs\BUILDING.md.
if not "%~1"=="nopause" pause
exit /b 1
