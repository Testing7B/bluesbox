@echo off
REM Builds OpenPocket in Docker through tools\op.mjs (the same as make build on macOS and Linux): see docs\BUILDING.md.
REM  -> firmware\openpocket.bin, docs\firmware\*.fwsc + manifest.json
setlocal
cd /d "%~dp0"
where node >nul 2>&1 || ( echo Node.js 18 or newer is needed: install it from https://nodejs.org, then run this again. & goto fail )
node tools\op.mjs build || goto fail
echo.
echo SUCCESS: see docs\firmware\ and firmware\openpocket.bin
if not "%~1"=="nopause" pause
exit /b 0
:fail
echo.
echo BUILD FAILED: read the messages above. See docs\BUILDING.md.
if not "%~1"=="nopause" pause
exit /b 1
