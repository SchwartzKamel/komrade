# Copilot Instructions for KOMRADE

> KOMRADE: **K**ernel-**O**utside **M**emory **R**eader **A**nd **D**ebug **E**ngine
>
> An external trainer for AssaultCube 1.2.0.2 (ac_client.exe, 32-bit), built with zero dependencies via Win32 APIs only (OpenProcess, ReadProcessMemory, WriteProcessMemory).

## Build & Run

### Build (x86 Debug — canonical config)

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
    komrade.sln /p:Configuration=Debug /p:Platform=x86 /v:minimal
```

Output: `Debug\komrade.exe`. Requires C++17 (set in vcxproj).

### Run

1. Launch AssaultCube and **enter a match** (LocalPlayer is null in menus).
2. Run `Debug\komrade.exe` — it auto-attaches and prints a hotkey table.
3. Press hotkeys to toggle features (health/armor freeze, aimbot, code patches).
4. Press NUMPAD9 to exit cleanly (auto-restores all patches).

### Live Verification (no VS debugger needed)

If offsets rot on an AC update, verify them live without rebuilding:

```powershell
# Quick: confirm LocalPlayer + health still resolve (Tier 2)
pwsh tools\windbg\probe_localplayer.ps1

# Full: dump playerent and tag every field (Tier 1)
pwsh tools\windbg\dump_player_struct.ps1

# Comprehensive: run all 13 sanity checks (Tier 4)
pwsh tools\windbg\validate_all.ps1
```

See `tools/windbg/README.md` for cdb.exe setup (bundled in WinDbg Preview AppX).

## Architecture

### Offset Registry (`komrade/offsets.h`)

Three namespaces, increasing confidence:

- **`offs::verified`** — Live-verified (via `tools/windbg/` probes) and actively used by the trainer.
  - **LocalPlayer static pointer:** `0x109B74` (module-relative). Dereferences to the playerent struct at runtime.
  - **Health/armor:** `+0xBC` / `+0xC0` from `*LocalPlayer`, with backups at `+0xD0` / `+0xD4` (discriminator pattern).
  - **Position:** `+0x04..+0x0C` (X, Y, Z floats).
  - **Angles:** `+0x40` (yaw), `+0x44` (pitch), `+0x48` (roll) in degrees.
  - **Code patch sites:** `0x637E9` (ammo), `0x63786` (recoil), `0x63378` (grenade).

- **`offs::a200k`** — Sourced from [A200K/AssaultCube_Hack](https://github.com/A200K/AssaultCube_Hack) (Offsets.h). File-relative values (PE base 0x400000) are rebased to module-relative (subtract 0x400000). Cross-validated against `verified` (A200K's recoil patch `0x463786` → `0x63786` matches).

- **`offs::candidate`** — Community offsets, unverified. Promote to `verified` only after live-testing them with the P/Invoke probes in `tools/windbg/`.

### Feature Table & Worker Threads

`komrade.cpp` defines `Feature[]` struct (enum members: F_HEALTH, F_ARMOR, F_AMMO, F_RECOIL, F_GRENADE, F_AIMBOT). Two worker threads:

1. **Freeze worker** (every 10 ms) — Ping-pongs health/armor values to `kFreezeValue` if the freeze feature is enabled.
2. **Aimbot worker** (every 20 ms) — If aimbot is enabled, scans heap for enemy playerent structs, picks closest live enemy, computes target angles, WPM them into LocalPlayer yaw/pitch.

Main thread polls hotkeys and toggles features on/off.

### Memory I/O Primitives (`komrade/mem.{h,cpp}`)

API surface intentionally mirrors [libmem](https://github.com/rdbo/libmem) so a future vendoring is mechanical. Key functions:

- **`AobScan`** — External pattern scan via VirtualQueryEx, scans readable committed pages for a byte pattern (with wildcard mask).
- **`ReadMemoryEx` / `WriteMemoryEx`** — Thin Win32 wrappers (no protection toggling).
- **`PatchEx`** — Wraps `VirtualProtectEx` to change protection, WPM, restore.
- **`DeepPointerEx`** — Pointer-chain walker (resolves `base + offset[0] → deref → + offset[1] → ...`).

### Aimbot (`komrade/aimbot.h`, header-only)

**Tier 3 implementation:** A self-contained aimbot that works without knowing the players vector address:

1. **Heap-walk:** VirtualQueryEx scans RW committed pages for playerent-shaped structures. Discriminator: HP at `+0xBC == +0xD0` (backup pattern), armor `+0xC0 == +0xD4`, sane position floats, vtable-ish `+0x00` pointer.
2. **Cache & re-scan:** Caches candidate pointers, re-scans every 750 ms; re-validates cache each tick. Eliminates full-heap scans every frame.
3. **Target picking:** Closest live (HP > 0) enemy, excluding LocalPlayer.
4. **Angle computation:** yaw = atan2(dx, dy), pitch = atan2(dz, dist2D) in degrees. AC convention: yaw 0° = +Y, pitch 0° = horizon.

If pitch inverts in-game, negate `dz` in `ComputeAngles`. If yaw is 180° off, try `atan2(dy, dx)`.

### Custom Copilot Agents (`.github/agents/`)

Repo ships seven specialized Copilot CLI sub-agents (in `.github/agents/`) decomposing the memory-hacking workflow:

- **memory-cartographer** — Maps unfamiliar process regions (dumps, disassembles, annotates).
- **offset-hunter** — Finds & verifies dynamic pointer chains.
- **pattern-scanner** — Makes addresses survive game rebuilds (AOB signatures).
- **detour-smith** — Patches instructions, NOPs, code caves, relocations.
- **trainer-architect** — Refactors main loop, hotkey dispatch, toggle state.
- **wpm-rpm-auditor** — Audits Win32 RPM/WPM call sites for safety.

See `.github/agents/README.md` for the decision flow and boundary rules between agents.

## Key Conventions

### Offset Naming

- **Module-relative:** `kThingOffset` (0x12345). Call site: `moduleBase + kThingOffset`.
- **Pointer chains:** `kThingChain[]` (e.g., `{ 0x04, 0x08, 0xC }` for three dereferences). Passed to `DeepPointerEx` or `FindDMAAddy`.
- **Patch sites:** `kXyzSite` (module offset). Call site: `moduleBase + kXyzSite`, then WPM the patch bytes.

### Code Patches

Original and patched byte sequences are defined as `constexpr BYTE[]` pairs:

```cpp
constexpr BYTE kAmmoOriginal[2] = { 0xFF, 0x0E };  // dec [esi]
constexpr BYTE kAmmoPatched[2]  = { 0xFF, 0x06 };  // inc [esi] — inverts to prevent ammo deduction
```

At startup, `ResolveCodeSite` tries AobScan first (signature-based, resilient to small ASM changes), falls back to hard-coded offset. On exit, original bytes are restored.

### Hotkey Dispatch

`KeyPressed(vk)` edge-triggers (returns true once per physical press). Main loop polls all features:

```cpp
if (KeyPressed(VK_NUMPAD0)) { features[F_HEALTH].enabled.store(!enabled); }
```

This avoids the need for a message hook.

### P/Invoke Probes (`tools/windbg/`)

PowerShell scripts use P/Invoke to call Win32 APIs without managed wrappers. Pattern:

```powershell
Add-Type -TypeDefinition $src -Language CSharp | Out-Null  # define P class
$h = [P]::OpenProcess(0x0410, $false, $pid)  # call via managed wrapper
$bytes = [P]::Read($h, [IntPtr]$addr, 4)     # read from process
```

This sidesteps the need for a C++ debug build or an external debugger for offset validation.

## Common Tasks

### Adding a new feature

1. Add offset(s) to `offs::verified` (or `candidate` with verification plan).
2. Define original/patched byte pairs (code patch) or freeze target (freeze write).
3. Add `Feature` struct entry in the Feature[] table.
4. Add hotkey dispatch in the main input loop.
5. Test with a build and live validation.

### Fixing rotted offsets

1. Run `tools/windbg/validate_all.ps1` — prints which checks fail.
2. Use `tools/windbg/dump_player_struct.ps1` to re-map the playerent struct.
3. For code patches, use `tools/windbg/dump_patch_context.txt` to re-examine the patch sites.
4. Update `offsets.h`, rebuild, re-test with the validator.

### Writing a new aimbot targeting mode

The `aimbot::Aimbot` class owns the heap-scan and candidate cache. To add a different targeting strategy (e.g., leading shots, predictive aim):

1. Add a new virtual method or branching logic in `Aimbot::Tick`.
2. Reuse `ScanCandidates` (or inline a new scan) to enumerate candidates.
3. Implement the targeting heuristic (closest, highest health, highest threat score, etc.).
4. Return the computed yaw/pitch.

## Scope

**Offline / single-player / LAN / private-server use only.** Do not use on public servers or against anti-cheat systems. See LICENSE.txt (MIT) and README.md (Ethics section).

## Debugging

- **Build failures** → Check C++17 support in vcxproj. MSBuild 17+ required.
- **Attach failures** → AC must be running in a match (LocalPlayer null in menus). Use `Get-Process ac_client` in PowerShell to confirm.
- **Offset mismatches** → Run `validate_all.ps1` to quickly isolate which offset(s) broke. It's faster than rebuilding and in-game testing.
- **Aimbot not tracking** → Run `validate_all.ps1` to confirm yaw/pitch fields are correct. If yaw is 180° off or pitch inverted, flip the angle computation in `aimbot.h:ComputeAngles`.
- **WPM permission denied** → Ensure trainer runs with sufficient privileges (UAC might require admin). Token bucket filtering or antivirus interference is rare on single-player.

## References

- **AC 1.2.0.2 source:** http://assault.cubers.net/ (open-source).
- **A200K offsets:** [AssaultCube_Hack](https://github.com/A200K/AssaultCube_Hack) — authoritative baseline for AC 1.2.0.2.
- **libmem API:** [libmem](https://github.com/rdbo/libmem) — our mem.{h,cpp} mirrors this (MIT rewrite, not linked).
- **cdb.exe & WinDbg:** Bundled in WinDbg Preview (Microsoft Store). See `tools/windbg/README.md` for the invocation + qd-not-q trap.
