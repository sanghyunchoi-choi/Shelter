# Replace absolute -I paths (folder name contains ')') with Debug-relative -I../ paths.
param(
    [string]$DebugDir = (Join-Path $PSScriptRoot "Debug")
)

$absPattern = '-I"D:/2026.05.08/2025_project/2\)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/([^"]+)"'
$relReplacement = '-I../$1'
# Fix prior broken replace: -I../path" -> -I../path
$brokenPattern = '-I\.\./([^"\s]+)"'
$brokenReplacement = '-I../$1'

$files = Get-ChildItem -Path $DebugDir -Filter 'subdir.mk' -Recurse -File
$changed = 0
foreach ($f in $files) {
    $text = [System.IO.File]::ReadAllText($f.FullName)
    $new = $text -replace $absPattern, $relReplacement
    $new = $new -replace $brokenPattern, $brokenReplacement
    if ($new -eq $text) { continue }
    [System.IO.File]::WriteAllText($f.FullName, $new)
    $changed++
}
Write-Host "Patched $changed subdir.mk file(s)."
