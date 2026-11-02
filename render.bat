@echo off
REM  Passe ta guitare DI dans le moteur OpenPocket et sort un WAV traite.
REM  Usage : glisse un WAV sur ce .bat, OU :
REM     render.bat  entree.wav  [sortie.wav]  [preset 1..42]
REM  Liste des presets : docs\SONS-ET-COMMANDES.md (ex. 6 = BRIT JANGLE, 16 = GROOVE METAL)
setlocal
if "%~1"=="" (
  echo Glisse un fichier WAV de guitare DI sur ce .bat, ou passe-le en argument.
  echo   render.bat  ma_guitare.wav  sortie.wav  1
  pause & exit /b 1
)
set IN=%~1
set OUT=%~2
if "%OUT%"=="" set OUT=%~dpn1_OpenPocket.wav
set PRESET=%~3
if "%PRESET%"=="" set PRESET=1
REM chemins WSL
for /f "usebackq delims=" %%p in (`wsl wslpath -a "%IN%"`) do set WIN=%%p
for /f "usebackq delims=" %%p in (`wsl wslpath -a "%OUT%"`) do set WOUT=%%p
wsl --cd "%~dp0engine" -- bash render.sh "%WIN%" "%WOUT%" %PRESET%
echo.
echo Fini : %OUT%
pause
