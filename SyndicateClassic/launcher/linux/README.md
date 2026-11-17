# Syndicate launcher — Linux port (proof-of-life)

WPF cannot run on Linux, so the launcher is being ported to **Avalonia** (a cross-platform
UI framework with a WPF-like API). This folder is the first milestone: a minimal real
window that proves the toolchain on actual hardware (your Steam Deck) **before** the full
port of the ~23k-line UI. The Windows launcher under `../launcher-source/` is untouched.

What this builds: a window with the Syndicate Night palette, title text, and a Launch button.
If it opens on the Deck, Avalonia + self-contained publish + Deck Desktop Mode all work,
and we port the real screens onto this foundation.

---

## Recommended: cross-publish on the host PC, copy to the Deck (no .NET on the Deck)

The host PC already has the .NET 8 SDK (it builds the WPF launcher). .NET can cross-compile
to Linux, and `--self-contained` bundles the runtime, so the Deck needs nothing installed.

On the **host PC** (PowerShell or cmd), from this folder:

```
dotnet publish SyndicateLauncher.Linux.csproj -c Release -r linux-x64 --self-contained true -o out-linux
```

Copy the whole `out-linux` folder to the Deck (USB stick, scp, or Warpinator). Then on the
**Deck, in Desktop Mode**, open Konsole in that folder:

```
chmod +x ./SyndicateLauncher
./SyndicateLauncher
```

The window should open. (Steam Deck is X11/Wayland; Avalonia auto-detects.)

## Alternative: build on the Deck itself

Steam Deck's OS is immutable, so don't use pacman. Install the SDK into your home dir:

```
curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0
export PATH="$HOME/.dotnet:$PATH"
dotnet run -c Release
```

---

## Notes for the port ahead

- Package versions are pinned to Avalonia `11.2.1`; bump to the latest `11.x` on first
  restore if you want. First build on the host needs network (NuGet restore).
- `app.manifest` is Windows-only and ignored on Linux.
- The genuinely Windows-only code in the full launcher (`System.Windows.Interop`,
  `WindowInteropHelper` — minimize-on-launch, UPnP window handles) will go behind a
  platform check during the port; everything else maps to Avalonia equivalents.
