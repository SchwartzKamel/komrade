#requires -Version 5
<#
.SYNOPSIS
    Run a WinDbg / cdb script against a live ac_client.exe.

.EXAMPLE
    pwsh tools\windbg\verify.ps1 -Script verify_offsets.txt

.EXAMPLE
    pwsh tools\windbg\verify.ps1 -ProcessId 1234 -Script dump_patch_context.txt -OutFile sites.txt

.NOTES
    cdb is part of the Windows SDK Debugging Tools. Install via
    "Windows 10/11 SDK" -> "Debugging Tools for Windows" feature.
#>
param(
    [int] $ProcessId = 0,
    [string] $Script = "verify_offsets.txt",
    [string] $OutFile = ""
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Resolve cdb.exe (x86 first — the target is 32-bit ac_client.exe).
$cdbCandidates = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x86\cdb.exe",
    "${env:ProgramFiles}\Windows Kits\10\Debuggers\x86\cdb.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe",
    "${env:ProgramFiles}\Windows Kits\10\Debuggers\x64\cdb.exe"
)
$cdb = $cdbCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $cdb) {
    throw "cdb.exe not found. Install Debugging Tools for Windows (Windows SDK)."
}

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

$args = @('-p', $ProcessId, '-cf', $scriptPath, '-lines')

if ($OutFile) {
    & $cdb @args | Tee-Object -FilePath $OutFile
    Write-Host "Output written to $OutFile"
} else {
    & $cdb @args
}
