@echo off
REM Makes fm1-300.bin (OpenPocket + your original firmware) for the SD-card recovery, through tools\op.mjs.
REM Usage: drag your fm1-1.bin dump onto this .bat, or put it here as fm1-1.bin. Run build.bat first.
setlocal
cd /d "%~dp0"
where node >nul 2>&1 || ( echo Node.js 18 or newer is needed: install it from https://nodejs.org, then run this again. & pause & exit /b 1 )
node tools\op.mjs appimage %*
pause
