@echo off
REM ===== Makes the flashable image fm1-300.bin (OpenPocket + your original firmware) =====
REM Run from the OpenPocket folder (next to firmware\ and tools\).
REM Usage: double-click (looks for fm1-1.bin here), OR drag your dump onto this .bat.
setlocal
cd /d "%~dp0"

REM --- original dump: the dragged file, else fm1-1.bin in this folder ---
set DUMP=%~1
if "%DUMP%"=="" set DUMP=fm1-1.bin
if not exist "%DUMP%" (
  echo ERROR: dump not found: "%DUMP%"
  echo  -> copy your fm1-1.bin backup into this folder, or drag it onto this .bat.
  pause & exit /b 1
)
if not exist "firmware\openpocket.bin" (
  echo ERROR: firmware\openpocket.bin missing -> run build.bat first.
  pause & exit /b 1
)

REM --- Python: Windows (py/python), else WSL ---
set PY=
where py     >nul 2>&1 && set PY=py -3
if not defined PY ( where python >nul 2>&1 && set PY=python )

echo Making fm1-300.bin from "%DUMP%" ...
echo.
if defined PY (
  %PY% tools\pocketx_appimage.py "%DUMP%" firmware\openpocket.bin fm1-300.bin
) else (
  echo (no Windows Python -> through WSL)
  wsl --cd "%~dp0." -- python3 tools/pocketx_appimage.py "%DUMP%" firmware/openpocket.bin fm1-300.bin
)
echo.
if exist fm1-300.bin (
  echo SUCCESS: fm1-300.bin made. Copy it to /backups on the SD card, then Recover -^> fm1-300.
) else (
  echo FAILED: see the messages above.
)
pause
endlocal
