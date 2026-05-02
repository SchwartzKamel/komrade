# Disambiguate LocalPlayer roots in live ac_client.exe via ReadProcessMemory.
# Path C fallback when cdb.exe is unavailable.
#
# Reads two candidate root pointers and dereferences +0xEC (health) for each.
# Whichever yields a sane value (1..150) is the correct root.

[CmdletBinding()]
param(
    [string]$ProcessName = 'ac_client',
    [string]$ModuleName  = 'ac_client.exe',
    # offs::verified::kLocalPlayerBase  vs  offs::a200k::kDirectCurrentPlayer
    [uint32[]]$Candidates = @(0x17E0A8, 0x109B74),
    [uint32]$HealthOffset = 0xEC
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
Add-Type -TypeDefinition $src -Language CSharp | Out-Null

$proc = Get-Process -Name $ProcessName -ErrorAction Stop | Select-Object -First 1
$mod  = $proc.Modules | Where-Object { $_.ModuleName -ieq $ModuleName } | Select-Object -First 1
if (-not $mod) { throw "Module $ModuleName not found in PID $($proc.Id)" }

$base = [int64]$mod.BaseAddress
Write-Host ("Attached PID {0} base=0x{1:X}" -f $proc.Id, $base) -ForegroundColor Cyan

# PROCESS_VM_READ | PROCESS_QUERY_INFORMATION = 0x0410
$h = [P]::OpenProcess(0x0410, $false, [uint32]$proc.Id)
if ($h -eq [IntPtr]::Zero) { throw "OpenProcess failed err=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

try {
    foreach ($off in $Candidates) {
        $rootAddr = [IntPtr]($base + $off)
        try {
            $rootBytes = [P]::Read($h, $rootAddr, 4)
            $rootPtr   = [BitConverter]::ToUInt32($rootBytes, 0)
            if ($rootPtr -eq 0) {
                Write-Host ("  off=0x{0:X6}  root=NULL (game not in match?)" -f $off) -ForegroundColor Yellow
                continue
            }
            $healthBytes = [P]::Read($h, [IntPtr]([int64]$rootPtr + $HealthOffset), 4)
            $health = [BitConverter]::ToInt32($healthBytes, 0)
            $verdict =
                if ($health -ge 1 -and $health -le 1500) { 'PLAUSIBLE HEALTH' }
                elseif ($health -eq 0)                   { 'zero (dead/menu)' }
                else                                     { 'garbage' }
            $color = switch ($verdict) {
                'PLAUSIBLE HEALTH' { 'Green' }
                'zero (dead/menu)' { 'Yellow' }
                default            { 'Red' }
            }
            Write-Host ("  off=0x{0:X6}  root=0x{1:X8}  +0xEC=0x{2:X8} ({2,11})  {3}" `
                -f $off, $rootPtr, $health, $verdict) -ForegroundColor $color
        } catch {
            Write-Host ("  off=0x{0:X6}  ERROR {1}" -f $off, $_.Exception.Message) -ForegroundColor Red
        }
    }
} finally {
    [P]::CloseHandle($h) | Out-Null
}
