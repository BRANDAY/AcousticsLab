# M9 runtime plugin switch check: PIE on the open industry level, keys 6 -> 5 -> 1 -> 6 through SendKeys after a click
# into the viewport (docs/ENV.md: digits reach PIE, letters do not), then the LogAcoustics lines of the switch.
#   powershell -ExecutionPolicy Bypass -File Tools/Editor/verify_m9_switch.ps1
param([string[]]$Keys = @("6", "5", "1", "6"))
$editorDir = $PSScriptRoot
Push-Location $editorDir
try {
    python -c "from verify_m5_pie import Editor, EDITOR_APP; Editor().call(EDITOR_APP, 'StartPIE', options={'playMode': 'PlayMode_InViewPort'})"
    Start-Sleep 8
    Add-Type -AssemblyName System.Windows.Forms
    foreach ($key in $Keys) {
        powershell -ExecutionPolicy Bypass -File (Join-Path $editorDir "click_screen.ps1") -X 960 -Y 560 | Out-Null
        Start-Sleep 1
        [System.Windows.Forms.SendKeys]::SendWait($key)
        Start-Sleep 3
    }
    python verify_m9_pie.py log "Industry" | Select-Object -Last 30
    python verify_m9_pie.py log "A/B variant" | Select-Object -Last 6
    python verify_m9_pie.py stop
}
finally {
    Pop-Location
}
