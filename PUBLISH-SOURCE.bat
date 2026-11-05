@echo off
title OpenPocket - publish the source code on GitHub
rem Puts the source code of this folder on GitHub (branch main + the tag vNN), without building anything:
rem everyone gets the latest code at https://github.com/YOUR-NAME/openpocket. Edits made on the GitHub site
rem stay in the history underneath; the files become exactly this folder's.
rem Usage: PUBLISH-SOURCE.bat                 (the repository YOUR-NAME/openpocket)
rem        PUBLISH-SOURCE.bat -Repo another-name
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_github.ps1" -SourceOnly %*
pause
