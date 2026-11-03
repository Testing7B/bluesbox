@echo off
REM ===== OpenPocket: builds the firmware, the update loader and the installer's packages =====
REM  -> firmware\openpocket.bin            the app
REM  -> docs\firmware\PocketX_9NN.fwsc     packages for the browser installer (+ BlackBox, manifest.json)
REM  -> fm1-300.bin                         (only if fm1-1.bin, your factory backup, is in this folder): full image
REM                                         for the SD-card recovery (fm1buddy, Recover)
REM The JieLi toolchain must be installed in WSL (firmware\setup.sh, once).
setlocal
cd /d "%~dp0"
if exist "firmware\openpocket.bin" del /q "firmware\openpocket.bin"
if exist "fm1-300.bin" del /q "fm1-300.bin"

echo [1/2] Building (WSL + JieLi toolchain)...
wsl --cd "%~dp0firmware" -- bash build.sh
if not exist "firmware\openpocket.bin" (
  echo.
  echo BUILD FAILED: read the messages above.
  if not "%~1"=="nopause" pause
  exit /b 1
)

set VER=?
for /f "tokens=3" %%v in ('findstr /c:"define OP_VERSION_NUM" firmware\openpocket.c') do set VER=%%~v

if exist "fm1-1.bin" (
  echo.
  echo [2/2] SD-card recovery image ^(fm1-300.bin^)...
  wsl --cd "%~dp0." -- python3 tools/pocketx_appimage.py fm1-1.bin firmware/openpocket.bin fm1-300.bin
) else (
  echo.
  echo [2/2] No fm1-1.bin here: SD-card image not made ^(not needed for the browser installer^).
)

echo.
echo ================================================================
echo  SUCCESS: OpenPocket v%VER%
echo   - installer packages: docs\firmware\  ^(PocketX_9%VER%.fwsc, BlackBox_9%VER%.fwsc^)
if exist "fm1-300.bin" echo   - SD card           : fm1-300.bin  ^(card's backups\ folder, then Recover^)
echo  Publish on GitHub: PUBLISH-GITHUB.bat
echo ================================================================
if not "%~1"=="nopause" pause
endlocal
exit /b 0
