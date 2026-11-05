@echo off
title OpenPocket - offline installer
rem Installs OpenPocket on the pedal WITHOUT internet: the web installer and the firmware packages are served from
rem this folder, on this computer only (http://localhost:8765), and opened in Chrome or Edge.
rem   INSTALL-OFFLINE.bat          uses the packages in docs\firmware (builds them first if there are none)
rem   INSTALL-OFFLINE.bat build    rebuilds the firmware first (build.bat: WSL + JieLi toolchain)
rem   INSTALL-OFFLINE.bat editor   opens the web editor instead of the installer
cd /d "%~dp0"
set PAGE=index.html
if /i "%~1"=="editor" set PAGE=editor.html

if /i "%~1"=="build" goto build
if exist "docs\firmware\manifest.json" goto serve
echo No firmware package in docs\firmware yet: building it first.
:build
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
if errorlevel 2 (
  echo.
  echo PowerShell could not open a local port: trying WSL's Python instead...
  start "" "http://localhost:8765/%PAGE%"
  wsl --cd "%~dp0docs" -- python3 -m http.server 8765 --bind 127.0.0.1
)
pause
