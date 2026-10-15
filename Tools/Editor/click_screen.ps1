# Real mouse click at screen coordinates (PIE / UMG verification, docs/ENV.md M4 notes: the Slate inspector "Click" only
# focuses a UMG button). Brings the window whose title matches TitlePattern to the front first.
#   powershell -ExecutionPolicy Bypass -File Tools/Editor/click_screen.ps1 -X 1500 -Y 300 [-TitlePattern "Unreal Editor"]
param(
    [Parameter(Mandatory = $true)][int]$X,
    [Parameter(Mandatory = $true)][int]$Y,
    [string]$TitlePattern = "Unreal Editor"
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ClickNative {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[ClickNative]::SetProcessDPIAware() | Out-Null
$window = Get-Process | Where-Object { $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "*$TitlePattern*" } | Select-Object -First 1
if ($window) { [ClickNative]::SetForegroundWindow($window.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 300 }
# Slate wants hover (mouse-move) events over the button before the press, so approach the target in a few steps.
foreach ($step in 4..0) { [ClickNative]::SetCursorPos($X - 6 * $step, $Y) | Out-Null; [ClickNative]::mouse_event(0x0001, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40 }
Start-Sleep -Milliseconds 150
[ClickNative]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero) # left down
Start-Sleep -Milliseconds 120
[ClickNative]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero) # left up
"clicked $X,$Y"
