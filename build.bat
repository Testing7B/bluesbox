@echo off
REM ===== OpenPocket : compile le firmware, le chargeur de mise a jour et les paquets du site d'installation =====
REM  -> firmware\openpocket.bin            l'application
REM  -> docs\firmware\PocketX_9NN.fwsc     paquets pour l'installation dans le navigateur (+ BlackBox, manifest.json)
REM  -> fm1-300.bin                         (seulement si fm1-1.bin, ton backup usine, est dans ce dossier) : image
REM                                         complete pour la recuperation par carte SD (fm1buddy, Recover)
REM Le toolchain JieLi doit etre installe dans WSL (firmware\setup.sh, une seule fois).
setlocal
cd /d "%~dp0"
if exist "firmware\openpocket.bin" del /q "firmware\openpocket.bin"
if exist "fm1-300.bin" del /q "fm1-300.bin"

echo [1/2] Compilation (WSL + toolchain JieLi)...
wsl --cd "%~dp0firmware" -- bash build.sh
if not exist "firmware\openpocket.bin" (
  echo.
  echo ECHEC de la compilation : lis les messages ci-dessus.
  pause & exit /b 1
)

set VER=?
for /f "tokens=3" %%v in ('findstr /c:"define OP_VERSION_NUM" firmware\openpocket.c') do set VER=%%~v

if exist "fm1-1.bin" (
  echo.
  echo [2/2] Image de recuperation pour la carte SD ^(fm1-300.bin^)...
  wsl --cd "%~dp0." -- python3 tools/pocketx_appimage.py fm1-1.bin firmware/openpocket.bin fm1-300.bin
) else (
  echo.
  echo [2/2] Pas de fm1-1.bin ici : image carte SD non generee ^(pas necessaire pour le navigateur^).
)

echo.
echo ================================================================
echo  SUCCES : OpenPocket v%VER%
echo   - paquets du site : docs\firmware\  ^(PocketX_9%VER%.fwsc, BlackBox_9%VER%.fwsc^)
if exist "fm1-300.bin" echo   - carte SD        : fm1-300.bin  ^(backups\ de la carte, puis Recover^)
echo  Publier sur GitHub : PUBLISH-GITHUB.bat
echo ================================================================
pause
endlocal
