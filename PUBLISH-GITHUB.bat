@echo off
title OpenPocket - build and publish on GitHub
rem One click: builds OpenPocket (build.bat), then publishes it on your GitHub: the source (branch main), the release
rem (the .fwsc packages) and the web installer on GitHub Pages.
rem Usage: PUBLISH-GITHUB.bat                 (the repository YOUR-NAME/openpocket)
rem        PUBLISH-GITHUB.bat -Repo another-name
cd /d "%~dp0"
echo == Step 1: build
call "%~dp0build.bat" nopause
if errorlevel 1 (
  echo.
  echo The build failed: nothing was published.
  pause
  exit /b 1
)
echo.
echo == Step 2: publish on GitHub
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_github.ps1" %*
pause
