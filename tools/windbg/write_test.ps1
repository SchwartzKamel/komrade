$src = @"
using System;
using System.Runtime.InteropServices;
public static class P {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, uint p);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, uint s, out uint r);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, uint s, out uint r);
}
"@
Add-Type -TypeDefinition $src -Language CSharp | Out-Null
$proc = Get-Process ac_client | Select-Object -First 1
$mod  = $proc.Modules | ?{$_.ModuleName -ieq 'ac_client.exe'}
$base = [int64]$mod.BaseAddress
$h = [P]::OpenProcess(0x0438, $false, [uint32]$proc.Id)
$rb = New-Object byte[] 4; $rd = 0
[void][P]::ReadProcessMemory($h, [IntPtr]($base + 0x109B74), $rb, 4, [ref]$rd)
$root = [BitConverter]::ToUInt32($rb,0)
$healthAddr = [IntPtr]([int64]$root + 0xBC)
[void][P]::ReadProcessMemory($h, $healthAddr, $rb, 4, [ref]$rd)
$orig = [BitConverter]::ToInt32($rb, 0)
Write-Host ("Original health @ 0x{0:X8} = {1}" -f $healthAddr.ToInt64(), $orig)
[void][P]::WriteProcessMemory($h, $healthAddr, [BitConverter]::GetBytes([int]137), 4, [ref]$rd)
[void][P]::ReadProcessMemory($h, $healthAddr, $rb, 4, [ref]$rd)
Write-Host ("After write 137  -> {0}" -f ([BitConverter]::ToInt32($rb,0)))
[void][P]::WriteProcessMemory($h, $healthAddr, [BitConverter]::GetBytes([int]$orig), 4, [ref]$rd)
[void][P]::ReadProcessMemory($h, $healthAddr, $rb, 4, [ref]$rd)
Write-Host ("Restored         -> {0}" -f ([BitConverter]::ToInt32($rb,0)))
[void][P]::CloseHandle($h)
