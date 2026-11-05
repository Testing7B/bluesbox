@echo off
REM Installs OpenPocket without internet: serves docs\ (installer, packages) on this computer only
REM (http://localhost:8765) and opens it, through tools\op.mjs. Needs Node.js and Python 3 (python.org).
REM INSTALL-OFFLINE.bat editor opens the editor. The packages are built first (needs Docker) when docs\firmware
REM has none, or with: INSTALL-OFFLINE.bat build
setlocal
cd /d "%~dp0"
where node >nul 2>&1 || ( echo Node.js 18 or newer is needed: install it from https://nodejs.org, then run this again. & pause & exit /b 1 )
node tools\op.mjs serve %*
pause
