$ErrorActionPreference = 'Stop'

$mainPath = 'DeepRun/Main.cpp'
$main = [System.IO.File]::ReadAllText($mainPath)

$pattern = '    static bool IsBlankFrame\(const void\* bits, const std::uint32_t width, const std::uint32_t height\) noexcept\r?\n    \{.*?\r?\n    \}\r?\n\r?\n(?=    bool logged_ = false;)'
$regex = [regex]::new($pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
$matches = $regex.Matches($main)
if ($matches.Count -ne 1) {
    throw "Main IsBlankFrame replacement expected exactly one match, found $($matches.Count)"
}

$replacement = @'
    static bool IsBlankFrame(const void* bits, const std::uint32_t width, const std::uint32_t height) noexcept
    {
        if (bits == nullptr || width < 4U || height < 4U)
        {
            return true;
        }

        // PrintWindow may paint top-level non-client chrome into a DIB sized from GetClientRect. A completely
        // white D3D12 client can therefore appear "non-blank" because the title bar/border contributes a few
        // dark pixels. Judge the central 80% instead: real Deep Run presentation is strongly non-uniform there,
        // while compositor startup white/black remains near-uniform. The tolerance also ignores a few driver or
        // window-decoration pixels without turning this into an image-quality gate.
        const std::uint32_t marginX = (std::max)(1U, width / 10U);
        const std::uint32_t marginY = (std::max)(1U, height / 10U);
        const std::uint32_t endX = width - marginX;
        const std::uint32_t endY = height - marginY;
        if (endX <= marginX || endY <= marginY)
        {
            return true;
        }

        const auto* pixels = static_cast<const std::uint8_t*>(bits);
        std::uint64_t blackPixels = 0U;
        std::uint64_t whitePixels = 0U;
        std::uint64_t sampledPixels = 0U;
        for (std::uint32_t y = marginY; y < endY; ++y)
        {
            for (std::uint32_t x = marginX; x < endX; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
                const bool blackPixel = pixels[offset] <= 8 && pixels[offset + 1] <= 8 && pixels[offset + 2] <= 8;
                const bool whitePixel = pixels[offset] >= 247 && pixels[offset + 1] >= 247 && pixels[offset + 2] >= 247;
                blackPixels += blackPixel ? 1U : 0U;
                whitePixels += whitePixel ? 1U : 0U;
                ++sampledPixels;
            }
        }
        if (sampledPixels == 0U)
        {
            return true;
        }
        constexpr std::uint64_t BlankPercent = 98U;
        return blackPixels * 100U >= sampledPixels * BlankPercent ||
               whitePixels * 100U >= sampledPixels * BlankPercent;
    }

'@
$main = $regex.Replace($main, $replacement, 1)
[System.IO.File]::WriteAllText($mainPath, $main, [System.Text.UTF8Encoding]::new($false))

git config user.name 'Deep Run CI'
git config user.email 'actions@users.noreply.github.com'
git add DeepRun/Main.cpp
git rm Tools/CI/apply_h4_capture_blank_fix.ps1 .github/workflows/h4-capture-blank-apply.yml
git commit -m 'fix: reject compositor blank frames in H4 capture'
git push origin HEAD:tmp/m5-h4-scalable-environment
