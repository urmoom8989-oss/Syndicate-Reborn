@echo off
title Syndicate - private test release
rem Makes one EXE to send to a few testers. Close Syndicate Launcher and Fortnite first.
rem Step 1 builds the latest launcher / game server / backend from this clone into your Syndicate
rem folder (same as UPDATE FROM GIT). Step 2 packs everything into Desktop\Project-Syndicate-Private-Release.
echo.
echo  == Step 1 of 2: updating your Syndicate folder from this clone
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev\update-from-git.ps1"
if errorlevel 1 goto failed
echo.
echo  == Step 2 of 2: packing the private test EXE (press Enter at the version question to accept it)
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0public-release\build-public-release.ps1" -Private
if errorlevel 1 goto failed
echo.
echo  Send the testers the EXE from the folder that just opened (the .sha256 file is optional).
if exist "%USERPROFILE%\Desktop\Project-Syndicate-Private-Release" explorer "%USERPROFILE%\Desktop\Project-Syndicate-Private-Release"
echo.
pause
exit /b 0
:failed
echo.
echo  Something failed - see the messages above. Nothing was handed out.
echo.
pause
exit /b 1
