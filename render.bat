@echo off
REM Runs a DI guitar WAV through the OpenPocket engine (in Docker). Usage: render.bat in.wav [out.wav] [preset 1..52]
REM The WAV must be inside this folder (or a subfolder).
setlocal
cd /d "%~dp0"
if "%~1"=="" ( echo usage: render.bat in.wav [out.wav] [preset 1..52] & pause & exit /b 1 )
set OUT=%~2
if "%OUT%"=="" set OUT=%~n1_OpenPocket.wav
set PRESET=%~3
if "%PRESET%"=="" set PRESET=1
docker run --rm --platform linux/amd64 -v "%cd%":/src -w /src openpocket-build bash engine/render.sh "%~1" "%OUT%" %PRESET%
pause
