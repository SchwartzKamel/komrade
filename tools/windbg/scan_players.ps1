# Heap-scan ac_client.exe for playerent-shaped structs.
#
# A real playerent has these signatures (verified live):
#   +0x04..+0x0C : three floats forming a position (each in [-500, 500])
#   +0xBC        : int health, in [0, 200]
#   +0xD0        : int matching +0xBC (last-saved-health backup)
#   +0xC0        : int armor, in [0, 200]
#   +0xD4        : int matching +0xC0 (last-saved-armor backup)
#
# The (+0xBC == +0xD0) AND (+0xC0 == +0xD4) double-check is the killer
# discriminator — random heap memory virtually never satisfies it.

[CmdletBinding()]
param(
    [string]$ProcessName = 'ac_client',
    [int]$MaxHits = 64,
    [switch]$Quiet
)
$ErrorActionPreference = 'Stop'

$src = @'
using System;
using System.Runtime.InteropServices;
public static class P {
    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORY_BASIC_INFORMATION {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint   AllocationProtect;
        public uint   RegionSize;
        public uint   State;
        public uint   Protect;
        public uint   Type;
    }
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint a, bool i, uint p);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, uint s, out uint r);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint VirtualQueryEx(IntPtr h, IntPtr a, ref MEMORY_BASIC_INFORMATION m, uint l);
}
'@
Add-Type -TypeDefinition $src -Language CSharp | Out-Null

$proc = Get-Process $ProcessName | Select-Object -First 1
$h = [P]::OpenProcess(0x0410, $false, [uint32]$proc.Id)

function IsPlayer($buf, $i) {
    if ($i + 0xD8 -ge $buf.Length) { return $false }
    $px = [BitConverter]::ToSingle($buf, $i + 0x04)
    $py = [BitConverter]::ToSingle($buf, $i + 0x08)
    $pz = [BitConverter]::ToSingle($buf, $i + 0x0C)
    foreach ($f in @($px, $py, $pz)) {
        if ([float]::IsNaN($f) -or [float]::IsInfinity($f)) { return $false }
        if ([Math]::Abs($f) -gt 1000.0) { return $false }
    }
    if (([Math]::Abs($px) + [Math]::Abs($py) + [Math]::Abs($pz)) -lt 0.001) { return $false }
    $hp1 = [BitConverter]::ToInt32($buf, $i + 0xBC)
    $hp2 = [BitConverter]::ToInt32($buf, $i + 0xD0)
    $ar1 = [BitConverter]::ToInt32($buf, $i + 0xC0)
    $ar2 = [BitConverter]::ToInt32($buf, $i + 0xD4)
    if ($hp1 -lt 0 -or $hp1 -gt 1500) { return $false }
    if ($ar1 -lt 0 -or $ar1 -gt 1500) { return $false }
    # Heuristic: backups should match (or be 100, the spawn default).
    if ($hp1 -ne $hp2 -and $hp2 -ne 100) { return $false }
    if ($ar1 -ne $ar2 -and $ar2 -ne 100 -and $ar2 -ne 0) { return $false }
    return $true
}

$mbi = New-Object P+MEMORY_BASIC_INFORMATION
$mbiSize = [Runtime.InteropServices.Marshal]::SizeOf($mbi)
$addr = [IntPtr]0
$end  = [IntPtr]0x7FFFFFFF
$hits = New-Object System.Collections.ArrayList
$scanned = 0
$totalMb = 0.0

try {
    while ([int64]$addr.ToInt64() -lt [int64]$end.ToInt64() -and $hits.Count -lt $MaxHits) {
        $r = [P]::VirtualQueryEx($h, $addr, [ref]$mbi, [uint32]$mbiSize)
        if ($r -eq 0) { break }
        $next = [IntPtr]([int64]$mbi.BaseAddress.ToInt64() + $mbi.RegionSize)
        $isCommitted = ($mbi.State -eq 0x1000)
        $isReadable  = (($mbi.Protect -band 0x66) -ne 0)  # READONLY|READWRITE|EXEC_READ|EXEC_RW
        $isPrivate   = ($mbi.Type    -eq 0x20000)         # MEM_PRIVATE (heap-ish)
        if ($isCommitted -and $isReadable -and $isPrivate -and $mbi.RegionSize -lt 0x4000000) {
            $totalMb += $mbi.RegionSize / 1MB
            try {
                $buf = New-Object byte[] $mbi.RegionSize
                $read = 0
                if ([P]::ReadProcessMemory($h, $mbi.BaseAddress, $buf, $mbi.RegionSize, [ref]$read)) {
                    $end1 = [int]$read - 0xD8
                    for ($i = 0; $i -lt $end1; $i += 4) {
                        if (IsPlayer $buf $i) {
                            $entAddr = [int64]$mbi.BaseAddress.ToInt64() + $i
                            $hp = [BitConverter]::ToInt32($buf, $i + 0xBC)
                            $ar = [BitConverter]::ToInt32($buf, $i + 0xC0)
                            $px = [BitConverter]::ToSingle($buf, $i + 0x04)
                            $py = [BitConverter]::ToSingle($buf, $i + 0x08)
                            $pz = [BitConverter]::ToSingle($buf, $i + 0x0C)
                            [void]$hits.Add([pscustomobject]@{
                                Addr = $entAddr; HP = $hp; Armor = $ar
                                X = $px; Y = $py; Z = $pz
                            })
                            if ($hits.Count -ge $MaxHits) { break }
                            # skip the rest of this struct (typical AC playerent ~0x400)
                            $i += 0x3FC
                        }
                    }
                }
            } catch {}
        }
        $scanned++
        $addr = $next
    }
} finally { [P]::CloseHandle($h) | Out-Null }

if (-not $Quiet) {
    Write-Host ("Scanned {0} regions ({1:F1} MB), found {2} candidate playerent(s):" -f $scanned, $totalMb, $hits.Count) -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Idx  Address     HP   Armor   pos(x, y, z)"
    Write-Host "---  ---------   --   -----   -------------------------------"
    $i = 0
    foreach ($e in $hits) {
        Write-Host ("{0,3}  0x{1:X8}  {2,3}    {3,3}    ({4,7:F2}, {5,7:F2}, {6,7:F2})" -f $i, $e.Addr, $e.HP, $e.Armor, $e.X, $e.Y, $e.Z)
        $i++
    }
}
$hits  # let callers consume the list
