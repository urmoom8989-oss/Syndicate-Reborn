# SyndicateInject — the Linux/Proton injection spike

This is the make-or-break test for playing on Linux. Everything else (backend, launcher
UI) already works or is straightforward; the one unproven thing is getting Erbium/cobalt
**injected into Fortnite while it runs under Proton/Wine**.

## Why this tool exists

On Windows the launcher injects DLLs directly into Fortnite (`NativeMethods.InjectDll`:
`OpenProcess` → `VirtualAllocEx` → `WriteProcessMemory` → `CreateRemoteThread(LoadLibraryW)`).
On Linux the launcher is a native process and **cannot** reach into a Wine process's
memory. The working pattern under Wine is to run a small **Win64 injector inside the same
Proton prefix** as the game. `SyndicateInject.cpp` is exactly that — the same calls as the
Windows launcher, as a standalone `.exe` Wine can run.

If this injects a DLL into Fortnite under Proton, the Linux launcher can drive the whole
Windows injection chain by running this tool under Wine once per DLL (cobalt redirect,
then the client mods). If it can't, we learn the real blocker now.

## Build (on the host PC)

Open "x64 Native Tools Command Prompt for VS" in this folder:

```
cl /EHsc /O2 /DUNICODE /D_UNICODE SyndicateInject.cpp /link /OUT:TiltedInject.exe
```

That produces `TiltedInject.exe`, a plain Win64 console program that runs under Proton.

## The spike (on the Steam Deck)

Goal: prove the injector can load a DLL into Fortnite running under Proton. Rough shape —
the exact commands depend on how Fortnite is launched on the Deck (Steam+Proton, `umu`,
or raw `wine`), which we'll pin down together:

1. Get one season's Fortnite build onto the Deck and launch `FortniteClient-Win64-Shipping`
   under a Proton/Wine prefix so it reaches the main menu (no backend needed for the test).
2. Note that prefix (`WINEPREFIX`), and make sure `TiltedInject.exe` and a test DLL are on
   a path the prefix can see (its `Z:\` maps the Linux filesystem).
3. Run the injector **in that same prefix**, e.g. with the same Proton/wine runtime:
   ```
   WINEPREFIX=<prefix> wine TiltedInject.exe FortniteClient-Win64-Shipping.exe Z:\path\to\cobalt.dll
   ```
4. Success = `Injected OK (module handle ...)`. Failure prints which Win32 call failed.

A clean "Injected OK" against the real `cobalt.dll` (with the backend running and Epic
redirected) means the season is one step from playable. Even "Injected OK" with a trivial
test DLL proves the mechanism and unblocks the rest of the pipeline.

## After the spike

With injection proven, the Linux launcher's Launch does, per season:
create/prepare the prefix → start the Node backend + Mongo (native Linux) → launch Fortnite
under Proton → run `TiltedInject.exe` under the prefix for cobalt + the client mods →
the game talks to the local backend. The match server (Erbium) runs the same way: a
Fortnite server process under Proton, injected by the same tool.
