param([string]$Dir)
Add-Type -AssemblyName System.Drawing
Set-Location $Dir
foreach ($f in @("m2_d2_frame_004.bmp", "m2_d2_frame_091.bmp")) {
    if (-not (Test-Path $f)) { Write-Output "MISSING: $f"; continue }
    $bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $f).Path)
    Write-Output ("=== {0} : {1}x{2}" -f $f, $bmp.Width, $bmp.Height)
    $rows = @(5, 100, 143, 145, 146, 147, 148, 149, 150, 152, 200, 360, 600)
    foreach ($y in $rows) {
        if ($y -lt $bmp.Height) {
            $px = $bmp.GetPixel(640, $y)
            Write-Output ("row {0}: RGB({1},{2},{3})" -f $y, $px.R, $px.G, $px.B)
        }
    }
    # find first row (from top) where pixel at x=640 is the underwater color (B > R)
    $prev = $null
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        $px = $bmp.GetPixel(640, $y)
        $isUnder = ($px.B -gt $px.R + 20)
        if ($prev -ne $null -and $isUnder -ne $prev.isUnder) {
            Write-Output ("waterline transition at row {0}: above RGB({1},{2},{3}) -> below RGB({4},{5},{6})" -f $y, $prev.px.R, $prev.px.G, $prev.px.B, $px.R, $px.G, $px.B)
        }
        $prev = [pscustomobject]@{ px = $px; isUnder = $isUnder }
    }
    $bmp.Dispose()
}
