@echo off
title Syndicate - public release
rem Makes the public EXE. Close Syndicate Launcher and Fortnite first.
rem Step 1 builds the latest launcher / game server / backend from this clone into your Syndicate
rem folder (same as UPDATE FROM GIT). Step 2 packs everything into Desktop\Project-Syndicate-Public-Release.
rem Nothing is uploaded or posted. Run PUBLISH PUBLIC RELEASE.cmd afterwards to put it on GitHub and Discord.
echo.
echo  == Step 1 of 2: updating your Syndicate folder from this clone
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev\update-from-git.ps1"
if errorlevel 1 goto failed
echo.
echo  == Step 2 of 2: packing the public EXE (it asks for the version number)
echo     If it asks "Publish to github.com/... now?", answer n and use PUBLISH PUBLIC RELEASE.cmd
echo     instead: that one also writes the release notes and posts to Discord.
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0public-release\build-public-release.ps1" %*
if errorlevel 1 goto failed
echo.
echo  The EXE is in the folder that just opened. Test it once, then run PUBLISH PUBLIC RELEASE.cmd
echo  to upload it to GitHub and announce it on Discord (it asks before each step).
echo.
pause
exit /b 0
:failed
echo.
echo  Something failed - see the messages above. Nothing was published.
echo.
pause
exit /b 1
