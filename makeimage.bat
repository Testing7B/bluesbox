@echo off
REM ===== Genere l'image flashable fm1-300.bin (openpocket + ton firmware d'origine) =====
REM A placer dans ...\codex\nvavepedal\ (a cote de firmware\ et tools\).
REM Usage : double-clic (cherche fm1-1.bin ici), OU glisse ton dump sur ce .bat.
setlocal
cd /d "%~dp0"

REM --- dump d'origine : argument glisse, sinon fm1-1.bin dans ce dossier ---
set DUMP=%~1
if "%DUMP%"=="" set DUMP=fm1-1.bin
if not exist "%DUMP%" (
  echo ERREUR: dump introuvable : "%DUMP%"
  echo  -> copie ton backup fm1-1.bin dans ce dossier, ou glisse-le sur ce .bat.
  pause & exit /b 1
)
if not exist "firmware\openpocket.bin" (
  echo ERREUR: firmware\openpocket.bin absent -> lance d'abord build.bat.
  pause & exit /b 1
)

REM --- Python : Windows (py/python) sinon WSL ---
set PY=
where py     >nul 2>&1 && set PY=py -3
if not defined PY ( where python >nul 2>&1 && set PY=python )

echo Generation de fm1-300.bin a partir de "%DUMP%" ...
echo.
if defined PY (
  %PY% tools\pocketx_appimage.py "%DUMP%" firmware\openpocket.bin fm1-300.bin
) else (
  echo (pas de Python Windows -> via WSL)
  wsl --cd "%~dp0." -- python3 tools/pocketx_appimage.py "%DUMP%" firmware/openpocket.bin fm1-300.bin
)
echo.
if exist fm1-300.bin (
  echo SUCCES: fm1-300.bin genere. Copie-le dans /backups de la SD, puis Recover -^> fm1-300.
) else (
  echo ECHEC: voir les messages ci-dessus.
)
pause
endlocal
