@echo off
REM Builds OpenPocket: firmware\openpocket.bin, docs\firmware\*.fwsc + manifest.json (see docs\BUILDING.md).
REM  - with Node.js and Docker Desktop running: in the pinned container, through tools\op.mjs (the same as make build);
REM  - otherwise in WSL, with the JieLi toolchain installed there once (wsl sh firmware/setup.sh).
REM Usage: build.bat [docker^|wsl] [nopause]      docker or wsl forces one way.
setlocal
cd /d "%~dp0"
set WAY=
set NOPAUSE=
for %%a in (%*) do (
  if /i "%%~a"=="nopause" set NOPAUSE=1
  if /i "%%~a"=="docker" set WAY=docker
  if /i "%%~a"=="wsl" set WAY=wsl
)
if defined WAY goto %WAY%
where node >nul 2>&1 || goto auto_wsl
docker info >nul 2>&1 || goto auto_wsl

:docker
where node >nul 2>&1 || ( echo Node.js 18 or newer is needed: install it from https://nodejs.org, then run this again. & goto fail )
echo == Building in the container (Docker, tools\op.mjs)
node tools\op.mjs build || goto fail
goto ok

:auto_wsl
echo Docker Desktop or Node.js is not available: building in WSL instead.
:wsl
where wsl >nul 2>&1 || ( echo Neither Docker + Node.js nor WSL is installed: see docs\BUILDING.md. & goto fail )
if exist "firmware\openpocket.bin" del /q "firmware\openpocket.bin"
echo == Building in WSL (JieLi toolchain from firmware/setup.sh)
wsl --cd "%~dp0firmware" -- sh build.sh
if not exist "firmware\openpocket.bin" goto fail

:ok
set VER=?
for /f "tokens=3" %%v in ('findstr /c:"define OP_VERSION_NUM" firmware\openpocket.c') do set VER=%%~v
echo.
echo SUCCESS: OpenPocket v%VER%
echo   - installer packages: docs\firmware\  (PocketX_9%VER%.fwsc, BlackBox_9%VER%.fwsc)
echo   - SD-card image: makeimage.bat (needs your fm1-1.bin backup)
if not defined NOPAUSE pause
exit /b 0

:fail
echo.
echo BUILD FAILED: read the messages above. See docs\BUILDING.md.
if not defined NOPAUSE pause
exit /b 1
