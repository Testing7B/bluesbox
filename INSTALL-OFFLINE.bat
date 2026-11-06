@echo off
title OpenPocket - offline installer
REM Installs OpenPocket on the pedal WITHOUT internet: the web installer and the firmware packages are served from
REM this folder, on this computer only (http://localhost:8765), and opened in Chrome or Edge.
REM Needs nothing to be installed: a small PowerShell server (tools\serve_offline.ps1); Python 3 (tools\serve.py) or
REM WSL is used only if PowerShell cannot open a local port.
REM   INSTALL-OFFLINE.bat          uses the packages in docs\firmware (builds them first if there are none)
REM   INSTALL-OFFLINE.bat build    rebuilds the firmware first (build.bat)
REM   INSTALL-OFFLINE.bat editor   opens the web editor instead of the installer
setlocal
cd /d "%~dp0"
set PAGE=index.html
set BUILD=
for %%a in (%*) do (
  if /i "%%~a"=="editor" set PAGE=editor.html
  if /i "%%~a"=="build" set BUILD=1
)
if not exist "docs\firmware\manifest.json" (
  echo No firmware package in docs\firmware yet: building it first.
  set BUILD=1
)
if not defined BUILD goto serve
call "%~dp0build.bat" nopause
if errorlevel 1 (
  echo.
  echo The build failed: nothing to install.
  pause
  exit /b 1
)

:serve
echo.
echo == Packages ready to install:
dir /b "docs\firmware\*.fwsc"
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\serve_offline.ps1" -Page %PAGE%
if not errorlevel 2 goto end
echo.
echo PowerShell could not open a local port: trying Python, then WSL...
where py >nul 2>&1 && ( py -3 tools\serve.py %PAGE% & goto end )
where python >nul 2>&1 && ( python tools\serve.py %PAGE% & goto end )
start "" "http://localhost:8765/%PAGE%"
wsl --cd "%~dp0docs" -- python3 -m http.server 8765 --bind 127.0.0.1
:end
pause
