@echo off
title Syndicate - tester release
rem Makes the Tester EXE and pushes it to every tester. Close Syndicate Launcher and Fortnite first.
rem Step 1 builds the latest launcher / game server / backend from this clone into your Syndicate
rem folder (same as UPDATE FROM GIT). Step 2 packs the Tester EXE (only opens for the Tester role in the
rem Syndicate Discord), uploads it as a GitHub pre-release and tells the relay, so testers' launchers
rem update to it the next time they open. Output: Desktop\Project-Syndicate-Tester-Release.
echo.
echo  == Step 1 of 2: updating your Syndicate folder from this clone
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev\update-from-git.ps1"
if errorlevel 1 goto failed
echo.
echo  == Step 2 of 2: packing and publishing the Tester EXE
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0public-release\build-public-release.ps1" -Tester
if errorlevel 1 goto failed
echo.
echo  New testers: send them the EXE once. After that it updates itself.
echo.
pause
exit /b 0
:failed
echo.
echo  Something went wrong - see the messages above. Nothing was sent to testers.
pause
exit /b 1
