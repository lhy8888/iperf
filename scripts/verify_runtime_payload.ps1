param(
    [Parameter(Mandatory = $true)]
    [string]$BinDir
)

$requiredDlls = @(
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6Network.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "libharfbuzz-0.dll",
    "libfreetype-6.dll",
    "libmd4c.dll",
    "libpng16-16.dll"
)

$missingDlls = @()
foreach ($dll in $requiredDlls) {
    if (-not (Test-Path (Join-Path $BinDir $dll))) {
        $missingDlls += $dll
    }
}

if ($missingDlls.Count -gt 0) {
    throw "Missing runtime DLLs in '$BinDir': $($missingDlls -join ', ')"
}
