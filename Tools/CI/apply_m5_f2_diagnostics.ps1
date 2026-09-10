Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$path = 'Tests/M5CombatPlaygroundRuntimeChecks.h'
$text = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n")

if ($text.Contains('#include <iostream>')) {
    throw 'M5-F.2 diagnostics already present'
}
$text = $text.Replace("#include <cmath>`n", "#include <cmath>`n#include <iostream>`n")

$needle = '    using Game::Combat::CombatPlaygroundCameraMode;'
$replacement = @'
    const auto fail = [](const int line) {
        std::cerr << "[M5-F.2 diagnostic] failure line " << line << '\n';
        return false;
    };
    using Game::Combat::CombatPlaygroundCameraMode;
'@.Replace("`r`n", "`n")
if (-not $text.Contains($needle)) {
    throw 'Could not insert M5-F.2 diagnostic fail helper'
}
$text = $text.Replace($needle, $replacement)
$text = $text.Replace('return false;', 'return fail(__LINE__);')

[System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))
Write-Host 'M5-F.2 line diagnostics applied.'
