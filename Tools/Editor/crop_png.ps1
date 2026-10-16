# Crops a PNG in place to X, Y, Width, Height (used to keep only the PIE viewport of an editor capture).
param([string]$Path, [int]$X, [int]$Y, [int]$Width, [int]$Height)
Add-Type -AssemblyName System.Drawing
$Source = [System.Drawing.Bitmap]::FromFile($Path)
$Rect = New-Object System.Drawing.Rectangle($X, $Y, [Math]::Min($Width, $Source.Width - $X), [Math]::Min($Height, $Source.Height - $Y))
$Cropped = $Source.Clone($Rect, $Source.PixelFormat)
$Source.Dispose()
$Cropped.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
$Cropped.Dispose()
"cropped $Path to $($Rect.Width) x $($Rect.Height)"
