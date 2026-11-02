@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
title Installation fm1buddy (version Pocket X) sur ESP32-S3

echo ============================================================
echo   fm1buddy  -  VERSION POCKET X  -  installateur ESP32-S3
echo ============================================================
echo.
echo   Cette version ajoute un bouton "Dump Pocket X" qui extrait
echo   le firmware d'origine de la Pocket X par la cle materielle
echo   (sans passer par le MIDI, que la Pocket X n'expose pas).
echo.
echo   Cible : carte Freenove ESP32-S3 2.8" FNK0104B (N16R8).
echo   Branche l'ESP32-S3 en USB-C au PC AVANT de continuer.
echo.
echo   IMPORTANT : garde  pocketx-dump.patch  dans le MEME dossier
echo   que ce fichier .bat.
echo.
pause
echo.

REM ---- 0. le patch est-il la ? ----
if not exist "%~dp0pocketx-dump.patch" (
  echo   ERREUR : pocketx-dump.patch est introuvable a cote du .bat.
  echo   Mets les deux fichiers dans le meme dossier et relance.
  pause & exit /b 1
)

REM ---- 1. Python ----
echo [1/6] Verification de Python...
set "PY="
where py  >nul 2>&1 && set "PY=py"
if not defined PY ( where python >nul 2>&1 && set "PY=python" )
if not defined PY (
  echo   ERREUR : Python introuvable. https://www.python.org/downloads/  ^(coche "Add to PATH"^)
  pause & exit /b 1
)
echo   OK : %PY%

REM ---- 2. Git ----
echo [2/6] Verification de Git...
where git >nul 2>&1
if errorlevel 1 ( echo   ERREUR : Git introuvable. https://git-scm.com/download/win & pause & exit /b 1 )
echo   OK : git

REM ---- 3. PlatformIO ----
echo [3/6] Installation / mise a jour de PlatformIO...
%PY% -m pip install --upgrade --quiet platformio
if errorlevel 1 ( echo   ERREUR : pip a echoue. & pause & exit /b 1 )
echo   OK : platformio

REM ---- 4. Clone propre + commit pinne + patch Pocket X ----
echo [4/6] Recuperation de fm1buddy ^(clone propre^)...
if exist fm1buddy (
  echo   Suppression de l'ancien dossier fm1buddy...
  rmdir /s /q fm1buddy
)
git clone https://github.com/konsumer/fm1buddy fm1buddy
if not exist fm1buddy\platformio.ini ( echo   ERREUR : clone echoue. & pause & exit /b 1 )
pushd fm1buddy
echo   Cale sur le commit teste ^(3caf5ca^)...
git -c advice.detachedHead=false checkout 3caf5ca >nul 2>&1
if errorlevel 1 ( echo   ERREUR : commit 3caf5ca introuvable. & popd & pause & exit /b 1 )
echo   Application du patch Pocket X...
git apply --whitespace=nowarn "%~dp0pocketx-dump.patch"
if errorlevel 1 (
  echo   ERREUR : le patch ne s'applique pas. & popd & pause & exit /b 1
)
echo   OK : patch applique.

REM ---- 5. Build + upload ----
echo [5/6] Compilation + installation sur l'ESP32-S3 ^(env cyd-s3^)...
echo   ^(1re fois : telechargement du toolchain, plusieurs minutes.^)
echo   Si ca bloque sur "Connecting....", maintiens BOOT, touche RESET, relache BOOT.
echo.
%PY% -m platformio run -e cyd-s3 -t upload
set "RC=%errorlevel%"
popd
echo.
if not "%RC%"=="0" (
  echo ============================================================
  echo   Echec ^(code %RC%^). Verifie : cable USB-C data, BOOT+RESET,
  echo   port COM libre ^(ferme Arduino / moniteurs serie^).
  echo ============================================================
  pause & exit /b %RC%
)

REM ---- 6. Loader UBOOT pour l'extraction ----
echo [6/6] Telechargement du loader UBOOT ^(wl82loader.bin^)...
if not exist sdcard mkdir sdcard
powershell -NoProfile -Command ^
  "try { Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/c32b28e7a04f873f526ba929cb1bae9f05442fe9/data/loaderblobs/usb/wl82loader.bin' -OutFile 'sdcard\wl82loader.bin' } catch { exit 1 }"
if exist sdcard\wl82loader.bin (
  for /f %%H in ('powershell -NoProfile -Command "(Get-FileHash ''sdcard\wl82loader.bin'' -Algorithm SHA256).Hash.ToLower()"') do set "GOT=%%H"
  set "WANT=d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376"
  if /i "!GOT!"=="!WANT!" ( echo   OK : sdcard\wl82loader.bin ^(SHA256 verifie^) ) else ( echo   ATTENTION : SHA256 inattendu, ne pas l'utiliser. )
) else ( echo   ^(Pas grave : fm1buddy peut le telecharger par Wi-Fi.^) )

echo.
echo ============================================================
echo   TERMINE. Version Pocket X installee sur l'ESP32-S3.
echo.
echo   EXTRAIRE le firmware d'origine de la Pocket X :
echo    1. Copie  sdcard\wl82loader.bin  sur la carte SD, mets-la dans l'ESP32-S3.
echo    2. DEBRANCHE l'ESP32-S3 du PC. Alimente-le autrement.
echo    3. Branche la Pocket X DIRECTEMENT sur l'ESP32-S3 (PAS de hub).
echo    4. Ecran -> Settings -> "Recover the FM-1" -> bouton "Dump Pocket X".
echo    5. Suis : pedale eteinte, puis allumee quand il le demande.
echo    6. Le dump est ecrit dans  /backups  sur la SD. C'est ta sauvegarde.
echo    7. Sur PC :  python pocketx_verify_dump.py  TON_DUMP.bin
echo ============================================================
echo.
pause
endlocal
