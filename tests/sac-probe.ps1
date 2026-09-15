param([string]$DllPath)
$sig = @'
using System;
using System.Runtime.InteropServices;
public static class NativeProbe
{
    public const uint LOAD_WITH_ALTERED_SEARCH_PATH = 0x8;
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool FreeLibrary(IntPtr h);
}
'@
Add-Type -TypeDefinition $sig
$h = [NativeProbe]::LoadLibraryExW($DllPath, [IntPtr]::Zero,
                                 [NativeProbe]::LOAD_WITH_ALTERED_SEARCH_PATH)
if ($h -eq [IntPtr]::Zero) {
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Write-Output "LOAD FAIL: Win32 error $err"
    exit 1
}
Write-Output "LOAD OK handle=$h"
[NativeProbe]::FreeLibrary($h) | Out-Null
