@echo off
REM  Runs your DI guitar recording through the OpenPocket engine and writes a processed WAV.
REM  Usage: drag a WAV onto this .bat, OR:
REM     render.bat  input.wav  [output.wav]  [preset 1..42]
REM  Preset list: docs\SOUNDS-AND-CONTROLS.md (e.g. 6 = BRIT JANGLE, 16 = GROOVE METAL)
setlocal
if "%~1"=="" (
  echo Drag a DI guitar WAV file onto this .bat, or pass it as an argument.
  echo   render.bat  my_guitar.wav  output.wav  1
  pause & exit /b 1
)
set IN=%~1
set OUT=%~2
if "%OUT%"=="" set OUT=%~dpn1_OpenPocket.wav
set PRESET=%~3
if "%PRESET%"=="" set PRESET=1
REM WSL paths
for /f "usebackq delims=" %%p in (`wsl wslpath -a "%IN%"`) do set WIN=%%p
for /f "usebackq delims=" %%p in (`wsl wslpath -a "%OUT%"`) do set WOUT=%%p
wsl --cd "%~dp0engine" -- bash render.sh "%WIN%" "%WOUT%" %PRESET%
echo.
echo Done: %OUT%
pause
