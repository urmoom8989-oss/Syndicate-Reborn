@echo off
setlocal
:: =============================================================================
::  Syndicate Launcher — Release Build + Code Signing
:: =============================================================================
::  Usage:
::    build.bat          full self-contained SyndicateLauncher.exe
::    build.bat lite     small build (players need .NET 8 Desktop Runtime)
::    build.bat nosign   skip signing even if a cert is configured
::
::  Signing:
::    Drop your .pfx certificate next to this file and name it  syndicate.pfx
::    Set SYNDICATE_PFX_PASS in your environment (or in a .env file this script
::    reads) to the PFX password.  The build signs automatically.
::
::    Alternatively, set SYNDICATE_CERT_THUMBPRINT to sign using a cert already
::    installed in the Windows Certificate Store (no PFX file needed).
::
::    If neither is configured the EXE is left unsigned with a warning.
:: =============================================================================

title Syndicate Launcher - Build
cd /d "%~dp0"

where dotnet >nul 2>nul
if errorlevel 1 goto nodotnet

set "PROJECT=SyndicateLauncher\SyndicateLauncher.csproj"
set "OUT=%~dp0dist"
set "SIGNTOOL=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
set "TIMESTAMP_URL=http://timestamp.digicert.com"

echo.
echo  Building Syndicate Launcher...
echo.

if /i "%~1"=="lite" goto lite
dotnet publish "%PROJECT%" -c Release -r win-x64 --self-contained true ^
  -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true ^
  -p:EnableCompressionInSingleFile=true -p:DebugType=none -p:DebugSymbols=false ^
  -o "%OUT%"
goto check
:lite
dotnet publish "%PROJECT%" -c Release -r win-x64 --self-contained false ^
  -p:DebugType=none -p:DebugSymbols=false ^
  -o "%OUT%"

:check
if errorlevel 1 goto failed

:: ---- signing ----------------------------------------------------------------
if /i "%~1"=="nosign" goto nosign
if /i "%~2"=="nosign" goto nosign

set "EXE=%OUT%\SyndicateLauncher.exe"

:: Load .env if present (KEY=VALUE lines, no spaces around =)
if exist "%~dp0.env" (
    for /f "usebackq tokens=1,2 delims==" %%A in ("%~dp0.env") do (
        if /i "%%A"=="SYNDICATE_PFX_PASS"         set "SYNDICATE_PFX_PASS=%%B"
        if /i "%%A"=="SYNDICATE_CERT_THUMBPRINT"  set "SYNDICATE_CERT_THUMBPRINT=%%B"
    )
)

:: --- path A: thumbprint (cert already in store) ---
if defined SYNDICATE_CERT_THUMBPRINT (
    echo  Signing with certificate from store ^(thumbprint: %SYNDICATE_CERT_THUMBPRINT:~0,8%...^)
    "%SIGNTOOL%" sign ^
        /sha1 "%SYNDICATE_CERT_THUMBPRINT%" ^
        /fd sha256 ^
        /tr "%TIMESTAMP_URL%" /td sha256 ^
        /v "%EXE%"
    if errorlevel 1 goto signfailed
    goto signdone
)

:: --- path B: .pfx file ---
if exist "%~dp0syndicate.pfx" (
    if not defined SYNDICATE_PFX_PASS (
        echo.
        echo  syndicate.pfx found but SYNDICATE_PFX_PASS is not set.
        echo  Set it in your environment or in a .env file next to build.bat:
        echo      SYNDICATE_PFX_PASS=yourpassword
        echo.
        goto nosign
    )
    echo  Signing with syndicate.pfx...
    "%SIGNTOOL%" sign ^
        /f "%~dp0syndicate.pfx" ^
        /p "%SYNDICATE_PFX_PASS%" ^
        /fd sha256 ^
        /tr "%TIMESTAMP_URL%" /td sha256 ^
        /v "%EXE%"
    if errorlevel 1 goto signfailed
    goto signdone
)

:nosign
echo.
echo  [WARNING] No signing certificate configured. The EXE will be unsigned.
echo  Browsers and Windows SmartScreen will warn players on download.
echo  See  dev\signing-setup.md  for how to add a certificate.
echo.
goto done

:signfailed
echo.
echo  Signing failed. Check that the certificate is valid and not expired.
echo.
goto done

:signdone
echo.
echo  Signed OK.
echo.

:done
echo  Done. Your launcher is in:  %OUT%\SyndicateLauncher.exe
echo.
explorer "%OUT%"
pause
exit /b 0

:nodotnet
echo.
echo  The .NET SDK was not found.
echo  Install the .NET 8 SDK from https://dotnet.microsoft.com/download/dotnet/8.0
echo  then run build.bat again.
echo.
pause
exit /b 1

:failed
echo.
echo  The build failed. Scroll up to read the first error message.
echo.
pause
exit /b 1
