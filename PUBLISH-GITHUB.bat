@echo off
title OpenPocket - build and publish on GitHub
rem One click: builds OpenPocket (build.bat), then puts EVERYTHING of this version on your GitHub:
rem   the source (branch main = exactly this folder's files, with the version history, tag vNN),
rem   the release (the .fwsc packages + what's new) and the web installer / editor on GitHub Pages.
rem Usage: PUBLISH-GITHUB.bat                 (the repository YOUR-NAME/openpocket)
rem        PUBLISH-GITHUB.bat -Repo another-name
rem Only the source, without building: PUBLISH-SOURCE.bat
cd /d "%~dp0"
echo == Step 1: build
call "%~dp0build.bat" nopause
if errorlevel 1 (
  echo.
  echo The build failed: the release and the installer cannot be updated.
  choice /c YN /m "Publish the source code only"
  if errorlevel 2 exit /b 1
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_github.ps1" -SourceOnly %*
  pause
  exit /b
)
echo.
echo == Step 2: publish on GitHub (source, release, installer)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_github.ps1" %*
pause
