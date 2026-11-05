@echo off
REM Runs a DI guitar WAV through the OpenPocket engine, through tools\op.mjs. Usage: render.bat in.wav [out.wav] [preset 1..52]
REM The WAV must be inside this folder (or a subfolder).
setlocal
cd /d "%~dp0"
where node >nul 2>&1 || ( echo Node.js 18 or newer is needed: install it from https://nodejs.org, then run this again. & pause & exit /b 1 )
node tools\op.mjs render %*
pause
