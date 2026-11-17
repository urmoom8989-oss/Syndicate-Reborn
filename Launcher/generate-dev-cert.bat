@echo off
:: =============================================================================
::  Syndicate Launcher — Generate Self-Signed Developer Certificate
:: =============================================================================
::  Run this ONCE on each dev machine that needs to sign builds locally.
::  Self-signed certs do NOT fix browser download warnings for other players —
::  they only remove the "Unknown Publisher" UAC dialog on your own machine.
::  For player-facing releases, use a CA cert (see signing-setup.md).
:: =============================================================================
setlocal
cd /d "%~dp0"

echo.
echo  Creating Syndicate self-signed developer certificate...
echo.

:: Generate a self-signed cert valid for 3 years, export to syndicate-dev.pfx
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$cert = New-SelfSignedCertificate -Subject 'CN=Syndicate Launcher Dev,O=Syndicate,C=US' -Type CodeSigningCert -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddYears(3) -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256; $pass = ConvertTo-SecureString -String 'SyndicateDev' -Force -AsPlainText; Export-PfxCertificate -Cert $cert -FilePath '%~dp0syndicate.pfx' -Password $pass; Write-Host ('Thumbprint: ' + $cert.Thumbprint)"

if errorlevel 1 (
    echo.
    echo  Certificate generation failed.
    pause
    exit /b 1
)

echo.
echo  Generated: syndicate.pfx  ^(password: SyndicateDev^)
echo.
echo  To use it in build.bat, create a .env file next to build.bat containing:
echo      SYNDICATE_PFX_PASS=SyndicateDev
echo.
echo  IMPORTANT: syndicate.pfx is in .gitignore — it will not be committed.
echo  Every developer generates their own. For player releases, use a CA cert.
echo.
pause
exit /b 0
