# KOMRADE

> **K**ernel-**O**utside **M**emory **R**eader **A**nd **D**ebug **E**ngine
> (or just: a shamelessly Linux-named external trainer for AssaultCube 1.2.0.2)

A small, dependency-free Windows tool that attaches to `ac_client.exe`,
patches a handful of code sites, and freezes a handful of player fields —
all from outside the process via `OpenProcess` / `ReadProcessMemory` /
`WriteProcessMemory`. No DLL injection, no detours, no AGPL.

```
  __ _____  __  __ ___   ___    ___  ____
 / //_/ _ \/  |/  / _ \ / _ | / _ \/ __/
/ ,< / // / /|_/ / , _// __ |/ // / _/
_/|_|\___/_/  /_/_/|_|/_/ |_/____/___/
                       AC 1.2.0.2 trainer
```

## What it does

| Hotkey      | Effect                       | Mechanism            |
|-------------|------------------------------|----------------------|
| `NUMPAD0`   | Freeze health (live-verified) | Periodic `WriteProcessMemory` to `*LocalPlayer + 0xBC` |
| `NUMPAD1`   | Freeze armor                 | Same, `+ 0xC0`       |
| `NUMPAD2`   | Aimbot (closest live enemy)  | Heap-scan playerent-shaped structs; write yaw/pitch to `*LocalPlayer + 0x40 / +0x44` every 20 ms |
| `F2`        | Ammo no-decrement            | Code patch at `ac_client+0x637E9`: `FF 0E` → `FF 06` |
| `F3`        | No recoil                    | 10-byte NOP slide at `ac_client+0x63786` |
| `F4`        | Grenade no-decrement         | `FF 08` → `90 90` at `ac_client+0x63378` |
| `NUMPAD9`   | Exit + restore all patches   | Atomic shutdown, worker.join() |

## Layout

```
.
├─ komrade.sln                     # MSVC 2022 solution
├─ komrade/                        # main project
│  ├─ komrade.cpp                  # entrypoint + Feature[] + freeze worker thread
│  ├─ offsets.h                    # offset registry (verified / a200k / candidate)
│  ├─ sigs.h                       # AOB signatures (with widening notes)
│  ├─ mem.{h,cpp}                  # RPM/WPM, AobScan, libmem-shaped wrappers
│  └─ proc.{h,cpp}                 # process discovery / module base
├─ tools/windbg/                   # live verification scripts
│  ├─ probe_localplayer.ps1        # disambiguates LocalPlayer roots
│  ├─ dump_player_struct.ps1       # decodes playerent fields by heuristic
│  ├─ walk_entities.ps1            # enumerates the entity vector
│  ├─ write_test.ps1               # round-trip RPM/WPM access check
│  └─ verify.ps1                   # cdb.exe wrapper for the .txt scripts
└─ .github/agents/                 # repo-scoped Copilot CLI sub-agents
   ├─ memory-cartographer.md       # maps structs (player, entity, world)
   ├─ offset-hunter.md             # nails down static offsets
   ├─ pattern-scanner.md           # AOB widening + uniqueness checks
   ├─ detour-smith.md              # planned: in-process hooks (separate project)
   ├─ trainer-architect.md         # ties Feature[] / threading / restore-on-exit
   └─ wpm-rpm-auditor.md           # checks every WPM/RPM site
```

## Build

```powershell
# x86 Debug (the canonical config — AC is 32-bit)
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
    komrade.sln /p:Configuration=Debug /p:Platform=x86
```

Output: `Debug\komrade.exe`. Requires C++17 (already in the vcxproj).

## Run

1. Launch AssaultCube `ac_client.exe`.
2. Enter a match (LocalPlayer pointer is null in the menu).
3. Run `Debug\komrade.exe` — it auto-attaches.
4. Toggle features with the hotkeys above. The console echoes each toggle.

## Verifying offsets

If the game updates and the offsets rot, the WinDbg / P/Invoke scripts in
`tools/windbg/` re-validate everything live without needing a Visual Studio
debug session:

```powershell
# Quick: confirm LocalPlayer + health field still resolve
powershell -ExecutionPolicy Bypass -File tools\windbg\probe_localplayer.ps1

# Full: dump the playerent and tag every plausible field
powershell -ExecutionPolicy Bypass -File tools\windbg\dump_player_struct.ps1

# Walk other players / bots
powershell -ExecutionPolicy Bypass -File tools\windbg\walk_entities.ps1
```

## Provenance & honesty

- Patch-site offsets cross-validate with [A200K/AssaultCube_Hack][a200k]
  (file-relative `0x463786` ⇒ module-relative `0x63786` matches the
  recoil site we patch).
- LocalPlayer base + health/armor offsets in `offsets.h` were
  **live-corrected** during development — the legacy values that shipped in
  the original repo (`0x17E0A8`, `+0xEC`, `+0xF0`) were wrong for AC 1.2.0.2.
  See `offsets.h` for the comment trail.
- We mirror the [libmem][libmem] API surface (`ReadMemoryEx`, `DeepPointerEx`,
  …) but **do not link** against it — libmem is AGPLv3 and would relicense
  this MIT codebase. The wrappers in `mem.{h,cpp}` are clean-room reimplementations
  small enough that swapping in real libmem later is a mechanical change.

## Ethics

This is for the **single-player / LAN bot match** experience and reverse-engineering
practice. Don't run it on public servers — it ruins the game for other people
and gets you banned (deservedly).

[a200k]:  https://github.com/A200K/AssaultCube_Hack
[libmem]: https://github.com/rdbo/libmem

## License

MIT. See `LICENSE.txt`.
