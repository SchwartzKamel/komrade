# Dump first N bytes of a player-struct candidate to spot health/armor.
# Health in AC is typically 1..150, armor 0..100, plausible ints.
[CmdletBinding()]
param(
    [string]$ProcessName = 'ac_client',
    [string]$ModuleName  = 'ac_client.exe',
    [uint32]$RootStaticOff = 0x109B74,
    [uint32]$Bytes = 0x200
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
        byte[] b = new byte[size]; uint r;
        if (!ReadProcessMemory(h, addr, b, size, out r))
            throw new Exception("RPM err=" + Marshal.GetLastWin32Error()
                + " at 0x" + addr.ToInt64().ToString("X"));
        if (r < size) Array.Resize(ref b, (int)r);
        return b;
    }
}
'@
Add-Type -TypeDefinition $src -Language CSharp | Out-Null

$proc = Get-Process -Name $ProcessName | Select-Object -First 1
$mod  = $proc.Modules | Where-Object { $_.ModuleName -ieq $ModuleName } | Select-Object -First 1
$base = [int64]$mod.BaseAddress
$h = [P]::OpenProcess(0x0410, $false, [uint32]$proc.Id)
try {
    $rootBytes = [P]::Read($h, [IntPtr]($base + $RootStaticOff), 4)
    $root = [BitConverter]::ToUInt32($rootBytes, 0)
    Write-Host ("Root = 0x{0:X8} (struct base)" -f $root) -ForegroundColor Cyan
    $buf = [P]::Read($h, [IntPtr]([int64]$root), $Bytes)
    Write-Host "`n=== INTERESTING INT/FLOAT FIELDS ===`n" -ForegroundColor Cyan
    Write-Host "Off   Hex        IntDec       Float           Note"
    for ($i = 0; $i -lt $buf.Length - 3; $i += 4) {
        $u = [BitConverter]::ToUInt32($buf, $i)
        $s = [BitConverter]::ToInt32($buf, $i)
        $f = [BitConverter]::ToSingle($buf, $i)
        $note = ''
        if ($s -ge 1 -and $s -le 200)        { $note = '<- HEALTH/ARMOR candidate' }
        elseif ($s -ge 1 -and $s -le 1500)   { $note = '<- ammo/score candidate' }
        elseif ([math]::Abs($f) -gt 0.001 -and [math]::Abs($f) -lt 1000.0 `
                -and -not [float]::IsNaN($f) -and -not [float]::IsInfinity($f)) {
            $note = ('<- float {0:F3}' -f $f)
        }
        if ($note) {
            Write-Host ('0x{0:X3}  0x{1:X8}  {2,11}  {3,12}  {4}' -f $i, $u, $s, $f, $note)
        }
    }
} finally { [P]::CloseHandle($h) | Out-Null }
