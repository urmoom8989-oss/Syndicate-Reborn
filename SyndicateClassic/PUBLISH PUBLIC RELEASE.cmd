@echo off
title Syndicate - publish public release
rem Uploads the EXE that MAKE PUBLIC RELEASE built to the public GitHub repo and posts the
rem announcement to Discord. It shows a preview and asks before each step.
rem Extra options go straight through, e.g.  -Preview  -SkipDiscord  -SkipGithub  -Version 0.4.2
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0public-release\Publish-Release.ps1" %*
echo.
pause
