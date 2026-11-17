# Syndicate Launcher — Code Signing Setup

## How signing works in this repo

`build.bat` auto-signs `SyndicateLauncher.exe` after every release build.
It looks for a certificate in two places, in order:

1. **Windows Certificate Store** — set `SYNDICATE_CERT_THUMBPRINT` in your environment or `.env`
2. **PFX file** — drop `syndicate.pfx` next to `build.bat` and set `SYNDICATE_PFX_PASS`

If neither is found, the build completes but warns that the EXE is unsigned.

---

## Developer machines (local testing)

Run once per machine:

```
Launcher\generate-dev-cert.bat
```

Then create `Launcher\.env`:

```
SYNDICATE_PFX_PASS=SyndicateDev
```

Self-signed certs remove the "Unknown Publisher" UAC dialog on your own machine.
They do **not** fix Chrome/Firefox download warnings for players — that needs a CA cert.

---

## Player releases — getting a trusted CA cert

This is the only permanent fix for browser download blocks and SmartScreen warnings.

### Option A — OV Certificate (cheapest, ~$80–150/yr)

| Provider | Price | Verification time |
|---|---|---|
| Sectigo (formerly Comodo) | ~$80/yr | 1–3 days |
| DigiCert | ~$300/yr | 1–2 days |
| GlobalSign | ~$200/yr | 2–5 days |

OV certs still show a SmartScreen warning until ~200–500 downloads build reputation.
After that, SmartScreen clears permanently (until the cert is renewed).

### Option B — EV Certificate (best, ~$300–700/yr)

EV certs carry **instant SmartScreen trust** — no reputation-building period.
Requires a registered legal entity (business or sole trader) and a hardware token (USB key the CA sends you).

Recommended for Syndicate once player count grows — the hardware token signs during CI/CD via:
```bat
signtool sign /sha1 <THUMBPRINT> /fd sha256 /tr http://timestamp.digicert.com /td sha256 SyndicateLauncher.exe
```

---

## Installing a purchased cert

**From a PFX file (most CAs deliver this):**

```bat
:: Set these before building:
:: SYNDICATE_PFX_PASS=<your pfx password>
:: Then drop  syndicate.pfx  next to  build.bat
:: build.bat will sign automatically
```

**From the Windows Certificate Store:**

```powershell
# Install the cert
Import-PfxCertificate -FilePath syndicate.pfx -CertStoreLocation Cert:\CurrentUser\My

# Get the thumbprint
Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -like "*Syndicate*" } | Select-Object Thumbprint, Subject
```

Then in `.env`:
```
SYNDICATE_CERT_THUMBPRINT=<40-char hex thumbprint here>
```

---

## Submitting flagged downloads for review

After signing, if a URL is still blocked:

| System | Review URL |
|---|---|
| Google Safe Browsing | https://safebrowsing.google.com/safebrowsing/report_error/ |
| Microsoft SmartScreen | https://www.microsoft.com/en-us/wdsi/filesubmission |
| VirusTotal false positive | Submit via each engine's vendor portal (linked in VT results) |

Each submission clears within 1–3 business days.

---

## Files that must stay out of Git

`.gitignore` already covers these — do not commit them:

```
syndicate.pfx       # private key — never commit
.env                # contains PFX password — never commit
```
