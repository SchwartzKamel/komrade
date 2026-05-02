# WinDbg verification scripts

These scripts attach `cdb.exe` (console-mode WinDbg) to a live `ac_client.exe`
and dump everything our trainer relies on. They turn the speculative
`// UNVERIFIED:` entries in `komrade/offsets.h` into either confirmed values
or known-bad ones, and they widen the seed signatures in `komrade/sigs.h` so
`mem::AobScan` can return exactly-one-match across AC rebuilds.

## ⚠️ Critical safety note

**Always detach with `qd`, never `q`.**  In cdb, `q` *terminates* the attached
process — it will close AssaultCube and possibly cost you a match.  The
`verify.ps1` wrapper appends `qd` automatically; if you author a new `.txt`
script, end it with `.echo === done ===` (no `q`) and let the wrapper append
the detach command.

If you must run cdb manually, type `qd` (or `.detach`) at the prompt to
disconnect cleanly.

## Setup

`cdb.exe` ships in two places — pick whichever you have:

```
# 1. WinDbg Preview (winget install Microsoft.WinDbg)  -- recommended; smaller install
%ProgramFiles%\WindowsApps\Microsoft.WinDbg_<ver>_x64__8wekyb3d8bbwe\x86\cdb.exe

# 2. Windows 10/11 SDK with "Debugging Tools for Windows" feature
%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x86\cdb.exe
```

`verify.ps1` finds either automatically (uses `Get-AppxPackage` for the Store
package since `WindowsApps\` is ACL-locked against directory enumeration).

The **x86** build of cdb is required because `ac_client.exe` is 32-bit.

Launch AssaultCube **into a match** (LocalPlayer is null in the menu) before
running any script.

## Scripts

| Script                    | Purpose                                                                                       |
| ------------------------- | --------------------------------------------------------------------------------------------- |
| `verify_offsets.txt`      | Dumps every verified + A200K-static offset and prints the bytes at each patch site.           |
| `dump_patch_context.txt`  | Dumps 64 bytes around each code-patch site so seed signatures in `sigs.h` can be widened.     |
| `hunt_candidate.txt`      | Probes `offs::candidate::*` chains from both LocalPlayer roots to disambiguate which is real. |
| `verify.ps1`              | PowerShell wrapper — auto-detects `ac_client.exe` PID and runs a chosen script.               |

## Workflow

```powershell
# Auto-attach to the running ac_client.exe and run the default script.
pwsh tools\windbg\verify.ps1

# Specify a script and capture output to a file.
pwsh tools\windbg\verify.ps1 -Script dump_patch_context.txt -OutFile sites.txt

# Explicit PID (e.g. when running multiple AC instances).
pwsh tools\windbg\verify.ps1 -ProcessId 12345 -Script hunt_candidate.txt
```

After running `verify_offsets.txt`:

- If the **verified** LocalPlayer chain (`ac_client+0x17E0A8`) prints a
  health value near 100, keep `offs::verified::kLocalPlayerBase`.
- If only the **A200K** chain (`ac_client+0x109B74`) prints a sensible
  value, switch the trainer to `offs::a200k::kDirectCurrentPlayer`.
- If the printed bytes at any patch site differ from the expected opcodes
  (`FF 0E` / `50 8D 4C 24 1C 8B CE FF D2` / `FF 08`), the binary has drifted
  and you'll want `dump_patch_context.txt` plus a wider AOB signature.

## Why `cdb` instead of full WinDbg

These scripts are pure-text and idempotent — `cdb` is faster to launch and
trivially scriptable from CI. If you want the GUI, the same scripts run
verbatim in `windbg.exe` via `.scriptload tools\windbg\verify_offsets.txt`
or by pasting them into the command window.

## Scope

Offline / single-player / LAN / private-server only. Do not attach `cdb` to
public competitive servers' clients.
