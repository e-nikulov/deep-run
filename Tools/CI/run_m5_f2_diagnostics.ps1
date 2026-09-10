Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$path = 'Tests/M5CombatPlaygroundRuntimeChecks.h'
$text = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n")
if ($text.Contains('[M5-F.2 diagnostic]')) {
    throw 'diagnostics already applied'
}

# Instrument the original returns first so the helper itself remains a normal false-returning lambda.
$text = $text.Replace('return false;', 'return fail(__LINE__);')
$text = $text.Replace("#include <cmath>`n", "#include <cmath>`n#include <iostream>`n")
$needle = '    using Game::Combat::CombatPlaygroundCameraMode;'
$helper = @'
    const auto fail = [](const int line) {
        std::cerr << "[M5-F.2 diagnostic] failure line " << line << '\n';
        return false;
    };
    using Game::Combat::CombatPlaygroundCameraMode;
'@.Replace("`r`n", "`n")
if (-not $text.Contains($needle)) {
    throw 'diagnostic insertion point not found'
}
$text = $text.Replace($needle, $helper)
[System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))

cmake --preset windows-debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset windows-debug --target DeepRunM5WeaponRuntimeTests --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& .\build\windows-debug\Debug\DeepRunM5WeaponRuntimeTests.exe
exit $LASTEXITCODE
