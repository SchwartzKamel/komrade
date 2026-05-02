#requires -Version 5
<#
.SYNOPSIS
    Run a WinDbg / cdb script against a live ac_client.exe.

.EXAMPLE
    pwsh tools\windbg\verify.ps1 -Script verify_offsets.txt

.EXAMPLE
    pwsh tools\windbg\verify.ps1 -ProcessId 1234 -Script dump_patch_context.txt -OutFile sites.txt

.NOTES
    cdb.exe is found inside the WinDbg Preview Store package
    (winget install Microsoft.WinDbg) or the Windows SDK Debugging Tools.

    *** ALWAYS detach with `qd`, never `q`.  cdb's `q` TERMINATES the
        attached process — it will close AssaultCube.  This wrapper appends
        `qd` automatically; just make sure your scripts don't end with `q`.
#>
param(
    [int] $ProcessId = 0,
    [string] $Script = "verify_offsets.txt",
    [string] $OutFile = ""
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Resolve cdb.exe (x86 first — the target is 32-bit ac_client.exe).
# WinDbg Preview (winget install Microsoft.WinDbg) ships cdb.exe inside its
# Store package; we find it dynamically because the version is in the path.
$cdbCandidates = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x86\cdb.exe",
    "${env:ProgramFiles}\Windows Kits\10\Debuggers\x86\cdb.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe",
    "${env:ProgramFiles}\Windows Kits\10\Debuggers\x64\cdb.exe"
)
# Search WinDbg Preview install for x86 cdb. WindowsApps is ACL-locked so we
# can't enumerate the parent or expand wildcards — but Get-AppxPackage works.
try {
    $pkg = Get-AppxPackage -Name 'Microsoft.WinDbg' -ErrorAction SilentlyContinue
    if ($pkg -and $pkg.InstallLocation) {
        $cdbCandidates += @(
            (Join-Path $pkg.InstallLocation 'x86\cdb.exe'),
            (Join-Path $pkg.InstallLocation 'amd64\cdb.exe')
        )
    }
} catch {}
$cdb = $cdbCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $cdb) {
    throw "cdb.exe not found. Either install Debugging Tools for Windows (Windows SDK) or 'winget install Microsoft.WinDbg'."
}
Write-Verbose "Using cdb at: $cdb"

# Resolve target PID.
if ($ProcessId -eq 0) {
    $proc = Get-Process -Name 'ac_client' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { throw "ac_client.exe is not running. Pass -ProcessId explicitly or launch the game first." }
    $ProcessId = $proc.Id
    Write-Host "Auto-attaching to ac_client.exe (PID $ProcessId)"
}

# Resolve script path.
$scriptPath = if (Test-Path $Script) { (Resolve-Path $Script).Path } else { Join-Path $here $Script }
if (-not (Test-Path $scriptPath)) { throw "Script not found: $Script" }

$initCmd = "`$`$<$scriptPath; qd"
$cdbArgs = @('-p', $ProcessId, '-c', $initCmd, '-lines')

if ($OutFile) {
    & $cdb @cdbArgs *>&1 | Tee-Object -FilePath $OutFile
    Write-Host "Output written to $OutFile"
} else {
    & $cdb @cdbArgs
}
