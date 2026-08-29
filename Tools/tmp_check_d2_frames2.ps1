param([string]$Dir)
Add-Type -AssemblyName System.Drawing
Set-Location $Dir
foreach ($f in @("m2_d2_frame_004.bmp", "m2_d2_frame_091.bmp")) {
    if (-not (Test-Path $f)) { Write-Output "MISSING: $f"; continue }
    $bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $f).Path)
    Write-Output ("=== {0} : {1}x{2}" -f $f, $bmp.Width, $bmp.Height)
    # map vertical transitions at x=640 and x=100
    foreach ($x in @(100, 640)) {
        Write-Output ("--- column x={0}" -f $x)
        $prev = $null
        for ($y = 0; $y -lt $bmp.Height; $y++) {
            $px = $bmp.GetPixel($x, $y)
            if ($prev -eq $null -or ($px.R -ne $prev.R -or $px.G -ne $prev.G -or $px.B -ne $prev.B)) {
                Write-Output ("row {0}: RGB({1},{2},{3})" -f $y, $px.R, $px.G, $px.B)
            }
            $prev = $px
        }
    }
    # horizontal scan at a few rows to see if the cream band is full-width or a panel
    foreach ($y in @(5, 30, 60)) {
        if ($y -lt $bmp.Height) {
            $vals = @()
            foreach ($x in @(10, 200, 400, 640, 900, 1200)) {
                if ($x -lt $bmp.Width) {
                    $px = $bmp.GetPixel($x, $y)
                    $vals += ("({0},{1},{2})" -f $px.R, $px.G, $px.B)
                }
            }
            Write-Output ("row {0} across x: {1}" -f $y, ($vals -join " "))
        }
    }
    $bmp.Dispose()
}
