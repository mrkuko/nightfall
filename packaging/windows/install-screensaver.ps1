# Sets nightfall.scr as the current user's screensaver (no admin needed).
#   .\install-screensaver.ps1 [-Minutes 5]      .\install-screensaver.ps1 -Remove
param([int]$Minutes = 5, [switch]$Remove)
$key = 'HKCU:\Control Panel\Desktop'
if ($Remove) {
    Remove-ItemProperty $key SCRNSAVE.EXE -ErrorAction SilentlyContinue
    Set-ItemProperty $key ScreenSaveActive 0
} else {
    $scr = Join-Path $PSScriptRoot 'nightfall.scr'
    if (-not (Test-Path $scr)) { $scr = Join-Path $PSScriptRoot '..\..\nightfall.scr' }
    $scr = (Resolve-Path $scr).Path
    Set-ItemProperty $key SCRNSAVE.EXE $scr
    Set-ItemProperty $key ScreenSaveActive 1
    Set-ItemProperty $key ScreenSaveTimeOut ($Minutes * 60)
}
rundll32.exe user32.dll,UpdatePerUserSystemParameters
"Done. Open 'Change screen saver' to check."
