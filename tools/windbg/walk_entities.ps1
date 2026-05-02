# Walk the AC player entity list (cube/sauerbraten-style vector<dynent*>).
# Layout is typically: { int size; int alloc; T* buf; } at the static address.
[CmdletBinding()]
param(
    [string]$ProcessName = 'ac_client',
    [string]$ModuleName  = 'ac_client.exe',
    [uint32]$EntityListOff = 0x110118,
    [int]$DumpFirstNBytes = 0x140
)
$ErrorActionPreference = 'Stop'
$src = @'
using System;
using System.Runtime.InteropServices;
public static class P {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, uint p);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, uint s, out uint r);
    public static byte[] R(IntPtr h, IntPtr a, uint s) {
        byte[] b = new byte[s]; uint r;
        if (!ReadProcessMemory(h,a,b,s,out r)) throw new Exception("RPM err="+Marshal.GetLastWin32Error()+" @0x"+a.ToInt64().ToString("X"));
        if (r < s) Array.Resize(ref b,(int)r);
        return b;
    }
}
'@
Add-Type -TypeDefinition $src -Language CSharp | Out-Null

$proc = Get-Process $ProcessName | Select -First 1
$mod  = $proc.Modules | ?{$_.ModuleName -ieq $ModuleName}
$base = [int64]$mod.BaseAddress
$h    = [P]::OpenProcess(0x0410, $false, [uint32]$proc.Id)
try {
    $vecAddr = [IntPtr]($base + $EntityListOff)
    $vec = [P]::R($h, $vecAddr, 12)
    # Cube/AC vector layout: { T* buf; int alen; int ulen; }
    $buf   = [BitConverter]::ToUInt32($vec, 0)
    $alen  = [BitConverter]::ToInt32($vec, 4)
    $ulen  = [BitConverter]::ToInt32($vec, 8)
    Write-Host ("EntityList vector @ 0x{0:X8}" -f $vecAddr.ToInt64()) -ForegroundColor Cyan
    Write-Host ("  buf=0x{0:X8}  alen={1}  ulen={2}" -f $buf, $alen, $ulen)
    if ($buf -eq 0 -or $ulen -lt 0 -or $ulen -gt 1024) {
        Write-Host "Layout doesn't look right; aborting." -ForegroundColor Red; return
    }
    $size = $ulen

    Write-Host "`nEntity slots:" -ForegroundColor Cyan
    $found = @()
    for ($i = 0; $i -lt [Math]::Min($size, 64); $i++) {
        try {
            $slotBytes = [P]::R($h, [IntPtr]([int64]$buf + ($i * 4)), 4)
            $entPtr = [BitConverter]::ToUInt32($slotBytes, 0)
            if ($entPtr -eq 0) {
                Write-Host ("  [{0,2}] NULL" -f $i) -ForegroundColor DarkGray
                continue
            }
            # Read health@+0xBC and a few other fields
            $hp = $arm = $px = 0
            try {
                $hb = [P]::R($h, [IntPtr]([int64]$entPtr + 0xBC), 8)
                $hp  = [BitConverter]::ToInt32($hb, 0)
                $arm = [BitConverter]::ToInt32($hb, 4)
                $pb = [P]::R($h, [IntPtr]([int64]$entPtr + 0x04), 12)
                $px = [BitConverter]::ToSingle($pb, 0)
                $py = [BitConverter]::ToSingle($pb, 4)
                $pz = [BitConverter]::ToSingle($pb, 8)
                Write-Host ("  [{0,2}] ent=0x{1:X8}  HP={2,4} Armor={3,3}  pos=({4,7:F1}, {5,7:F1}, {6,6:F1})" -f $i,$entPtr,$hp,$arm,$px,$py,$pz) -ForegroundColor Green
                $found += [pscustomobject]@{ Slot=$i; Ent=$entPtr; HP=$hp; Armor=$arm; X=$px; Y=$py; Z=$pz }
            } catch {
                Write-Host ("  [{0,2}] ent=0x{1:X8}  (unreadable)" -f $i, $entPtr) -ForegroundColor Red
            }
        } catch { break }
    }
    Write-Host ("`nLive entities: {0}" -f $found.Count) -ForegroundColor Cyan
    if ($found.Count -ge 2 -and $DumpFirstNBytes -gt 0) {
        Write-Host "`n=== Cross-comparing slot 0 vs slot 1 (find static-layout fields) ===" -ForegroundColor Cyan
        $a = [P]::R($h, [IntPtr]([int64]$found[0].Ent), $DumpFirstNBytes)
        $b = [P]::R($h, [IntPtr]([int64]$found[1].Ent), $DumpFirstNBytes)
        Write-Host "Off    A(int/float)              B(int/float)              Diff?"
        for ($i = 0; $i -lt $a.Length - 3; $i += 4) {
            $ai = [BitConverter]::ToInt32($a,$i); $af=[BitConverter]::ToSingle($a,$i)
            $bi = [BitConverter]::ToInt32($b,$i); $bf=[BitConverter]::ToSingle($b,$i)
            if ($ai -eq $bi) { continue }
            $tagA = if ($ai -ge 1 -and $ai -le 200) { "*" } elseif (-not [float]::IsNaN($af) -and [Math]::Abs($af) -gt 0.001 -and [Math]::Abs($af) -lt 1000) { "f" } else { " " }
            Write-Host ("0x{0:X3}  {1,11} {2,12:F2}{3}   {4,11} {5,12:F2}   diff" -f $i, $ai, $af, $tagA, $bi, $bf)
        }
    }
} finally { [P]::CloseHandle($h) | Out-Null }
