@echo off
title OpenPocket - publish on GitHub
rem Publishes OpenPocket on your GitHub: the source (branch main), the release (the .fwsc packages) and the web
rem installer on GitHub Pages. Run build.bat first (it builds the packages the release and the installer publish).
rem Usage: PUBLISH-GITHUB.bat                 (the repository YOUR-NAME/openpocket)
rem        PUBLISH-GITHUB.bat -Repo another-name
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_github.ps1" %*
pause
