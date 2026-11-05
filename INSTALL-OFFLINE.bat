@echo off
REM Installs OpenPocket without internet: serves docs\ (installer, packages) on this computer only
REM (http://localhost:8765) and opens it. Needs Python 3 (python.org). INSTALL-OFFLINE.bat editor opens the editor.
REM Builds the packages first (build.bat, needs Docker) when docs\firmware has none, or with: INSTALL-OFFLINE.bat build
setlocal
cd /d "%~dp0"
set PAGE=index.html
if /i "%~1"=="editor" set PAGE=editor.html
if /i "%~1"=="build" goto build
if exist "docs\firmware\manifest.json" goto serve
:build
call "%~dp0build.bat" nopause || exit /b 1
:serve
where py >nul 2>&1 && ( py -3 tools\serve.py %PAGE% & goto end )
python tools\serve.py %PAGE%
:end
pause
