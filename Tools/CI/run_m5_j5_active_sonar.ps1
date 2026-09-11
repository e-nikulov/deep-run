$ErrorActionPreference = 'Stop'
$path = 'Tools/CI/apply_m5_j5_active_sonar.ps1'
$text = Get-Content -Raw $path
$needle = '    $text = Read-Lf $Path'
$replacement = @'
    $text = Read-Lf $Path
    $Old = $Old.Trim([char[]]"`r`n")
    $New = $New.Trim([char[]]"`r`n")
'@.Trim([char[]]"`r`n")
if (-not $text.Contains($needle)) { throw 'Replace-Once normalization anchor not found' }
$text = $text.Replace($needle, $replacement)
[System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))
& $path
