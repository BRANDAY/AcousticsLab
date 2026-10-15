# Captures the client area of a top-level window (default: the Unreal editor of this project) to a PNG.
# The editor's MCP CaptureViewport returns the level viewport only, so PIE and UMG panels are captured from the screen.
#
#   powershell -ExecutionPolicy Bypass -File Tools/Editor/capture_window.ps1 -Out docs/milestones/img/M3_paths.png
#   powershell -ExecutionPolicy Bypass -File Tools/Editor/capture_window.ps1 -TitlePattern "AcousticsLab" -Out shot.png
param(
    [string]$TitlePattern = "AcousticsLab",
    [string]$Out = "capture.png",
    [int]$Crop = 0
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Capture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

[void][Win32Capture]::SetProcessDPIAware()
$process = Get-Process | Where-Object { $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "*$TitlePattern*" } | Select-Object -First 1
if (-not $process) { Write-Error "No window matching '$TitlePattern'"; exit 1 }
$handle = $process.MainWindowHandle
[void][Win32Capture]::ShowWindow($handle, 9)   # SW_RESTORE
[void][Win32Capture]::SetForegroundWindow($handle)
Start-Sleep -Milliseconds 400

$rect = New-Object Win32Capture+RECT
[void][Win32Capture]::GetClientRect($handle, [ref]$rect)
$origin = New-Object Win32Capture+POINT
[void][Win32Capture]::ClientToScreen($handle, [ref]$origin)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { Write-Error "Window has no client area"; exit 1 }

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
$graphics.Dispose()
$full = [System.IO.Path]::GetFullPath($Out)
$dir = [System.IO.Path]::GetDirectoryName($full)
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
$bitmap.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output "Saved $full ($width x $height, client origin $($origin.X),$($origin.Y) on screen) from '$($process.MainWindowTitle)'"
