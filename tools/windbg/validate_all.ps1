# validate_all.ps1 -- Tier 4 auto-validation harness.
#
# Runs every verified offset against the live ac_client.exe via P/Invoke RPM
# and produces a PASS/FAIL table. Exits non-zero if anything fails.
#
# This is the single command that answers: "are my offsets still good?"
#
# Usage:
#   pwsh tools\windbg\validate_all.ps1
#   pwsh tools\windbg\validate_all.ps1 -RequireInMatch   # also expects HP > 0

[CmdletBinding()]
param(
    [string]$ProcessName     = 'ac_client',
    [string]$ModuleName      = 'ac_client.exe',
    [switch]$RequireInMatch
)

$ErrorActionPreference = 'Stop'

$src = @'
using System;
using System.Runtime.InteropServices;
public static class P {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint da, bool inh, uint pid);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr,
        byte[] buf, uint size, out uint read);
    public static byte[] Read(IntPtr h, IntPtr addr, uint size) {
        byte[] b = new byte[size];
        uint r;
        if (!ReadProcessMemory(h, addr, b, size, out r) || r != size)
            throw new Exception("RPM failed at 0x" + addr.ToInt64().ToString("X")
                + " err=" + Marshal.GetLastWin32Error());
        return b;
    }
}
'@
Add-Type -TypeDefinition $src -Language CSharp -ErrorAction SilentlyContinue | Out-Null

function Get-AcModuleBase {
    $p = Get-Process -Name $ProcessName -ErrorAction Stop | Select-Object -First 1
    $mod = $p.Modules | Where-Object { $_.ModuleName -ieq $ModuleName }
    if (-not $mod) { throw "module $ModuleName not loaded in $ProcessName" }
    return @{ Pid = $p.Id; Base = [uint64]$mod.BaseAddress.ToInt64(); Size = $mod.ModuleMemorySize }
}

$results = New-Object System.Collections.Generic.List[object]
function Add-Result($name, $pass, $detail) {
    $results.Add([pscustomobject]@{ Name = $name; Pass = $pass; Detail = $detail }) | Out-Null
}

$ac = Get-AcModuleBase
Write-Host ("ac_client.exe PID={0}  base=0x{1:X}  size=0x{2:X}" -f $ac.Pid, $ac.Base, $ac.Size)

$h = [P]::OpenProcess(0x0410, $false, [uint32]$ac.Pid)
if ($h -eq [IntPtr]::Zero) { throw "OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

try {
    function Read-U32([uint64]$addr) {
        $b = [P]::Read($h, [IntPtr]([int64]$addr), 4)
        return [BitConverter]::ToUInt32($b, 0)
    }
    function Read-F32([uint64]$addr) {
        $b = [P]::Read($h, [IntPtr]([int64]$addr), 4)
        return [BitConverter]::ToSingle($b, 0)
    }
    function Read-Bytes([uint64]$addr, [uint32]$n) {
        return [P]::Read($h, [IntPtr]([int64]$addr), $n)
    }
    function Read-Str([uint64]$addr, [uint32]$n) {
        $b = Read-Bytes $addr $n
        $end = [Array]::IndexOf($b, [byte]0)
        if ($end -lt 0) { $end = $b.Length }
        return [Text.Encoding]::ASCII.GetString($b, 0, $end)
    }

    # --- Test 1: LocalPlayer base in module range ---
    $lpAddr = $ac.Base + 0x109B74
    $lp     = Read-U32 $lpAddr
    Add-Result 'LocalPlayer ptr non-null'  ($lp -ne 0)             ("[0x{0:X8}] = 0x{1:X8}" -f $lpAddr, $lp)

    if ($lp -eq 0) {
        Write-Warning "LocalPlayer is NULL -- game may be at title screen before any map loads"
        $results | Format-Table -AutoSize
        exit 1
    }

    # --- Test 2: HP/Armor sanity (+0xBC == +0xD0, +0xC0 == +0xD4) ---
    $hp     = Read-U32 ($lp + 0xBC)
    $armor  = Read-U32 ($lp + 0xC0)
    $hpBak  = Read-U32 ($lp + 0xD0)
    $arBak  = Read-U32 ($lp + 0xD4)
    Add-Result 'HP backup mirrors +0xBC'    ($hp -eq $hpBak)        ("hp={0} bak={1}" -f $hp, $hpBak)
    Add-Result 'Armor backup mirrors +0xC0' ($armor -eq $arBak)     ("armor={0} bak={1}" -f $armor, $arBak)
    Add-Result 'HP in [0,200]'              ($hp -le 200)           ("hp={0}" -f $hp)
    Add-Result 'Armor in [0,200]'           ($armor -le 200)        ("armor={0}" -f $armor)
    if ($RequireInMatch) {
        Add-Result 'HP > 0 (in match)'      ($hp -gt 0)             ("hp={0}" -f $hp)
    }

    # --- Test 3: Position floats look like world coords ---
    $px = Read-F32 ($lp + 0x04)
    $py = Read-F32 ($lp + 0x08)
    $pz = Read-F32 ($lp + 0x0C)
    $posOk = ($px -ne 0 -or $py -ne 0) -and ([math]::Abs($px) -lt 10000) -and ([math]::Abs($py) -lt 10000) -and ([math]::Abs($pz) -lt 10000)
    Add-Result 'Position floats sane'       $posOk                  ("({0:N2},{1:N2},{2:N2})" -f $px,$py,$pz)

    # --- Test 4: Yaw/pitch in expected ranges ---
    $yaw   = Read-F32 ($lp + 0x40)
    $pitch = Read-F32 ($lp + 0x44)
    $roll  = Read-F32 ($lp + 0x48)
    Add-Result 'Yaw in [0,360]'             ($yaw -ge 0 -and $yaw -le 360)         ("yaw={0:N2}" -f $yaw)
    Add-Result 'Pitch in [-90,90]'          ($pitch -ge -90 -and $pitch -le 90)    ("pitch={0:N2}" -f $pitch)
    Add-Result 'Roll in [-90,90]'           ($roll  -ge -90 -and $roll  -le 90)    ("roll={0:N2}" -f $roll)

    # --- Test 5: Map name is printable ASCII ---
    $mapName = Read-Str ($ac.Base + 0x109EC0) 32
    $mapOk   = $mapName.Length -gt 0 -and ($mapName -match '^[a-zA-Z0-9_\- ]+$')
    Add-Result 'Map name printable'         $mapOk                  ("'{0}'" -f $mapName)

    # --- Test 6: FovY plausible (30..120) ---
    $fov = Read-F32 ($ac.Base + 0x101BAC)
    Add-Result 'FovY in [30,120]'           ($fov -ge 30 -and $fov -le 120)        ("fov={0:N2}" -f $fov)

    # --- Test 7: Patch-site opcodes match ---
    $ammoBytes  = Read-Bytes ($ac.Base + 0x637E9) 2
    $grenBytes  = Read-Bytes ($ac.Base + 0x63378) 2
    Add-Result 'Ammo dec opcode FF 0E'      (($ammoBytes[0] -eq 0xFF) -and ($ammoBytes[1] -eq 0x0E))   ("{0:X2} {1:X2}" -f $ammoBytes[0], $ammoBytes[1])
    Add-Result 'Grenade dec opcode FF 08'   (($grenBytes[0] -eq 0xFF) -and ($grenBytes[1] -eq 0x08))   ("{0:X2} {1:X2}" -f $grenBytes[0], $grenBytes[1])
}
finally {
    [P]::CloseHandle($h) | Out-Null
}

Write-Host ""
$results | Format-Table -AutoSize
$failed = ($results | Where-Object { -not $_.Pass }).Count
Write-Host ""
if ($failed -eq 0) {
    Write-Host "ALL CHECKS PASSED" -ForegroundColor Green
    exit 0
} else {
    Write-Host ("{0} CHECK(S) FAILED" -f $failed) -ForegroundColor Red
    exit 1
}
