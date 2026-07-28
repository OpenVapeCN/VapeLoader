[CmdletBinding()]
param(
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $PSScriptRoot '..\build\Release'
}
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$executable = Join-Path $BuildDirectory 'vape_v4_rewrite.exe'

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Build output not found: $executable"
}

$stream = [System.IO.File]::OpenRead($executable)
try {
    if ($stream.Length -lt 2 -or $stream.ReadByte() -ne 0x4D -or $stream.ReadByte() -ne 0x5A) {
        throw "Build output is not a valid PE file: $executable"
    }
}
finally {
    $stream.Dispose()
}

$requiredAssets = @(
    'close.png',
    'logo.png',
    'mask-bottom.png',
    'mask-left.png',
    'mask-right.png',
    'mask-top.png',
    'minimize.png',
    'proxima-regular.ttf',
    'proxima-semibold.otf',
    'rounded-rect.png'
)

$missing = foreach ($asset in $requiredAssets) {
    $path = Join-Path $BuildDirectory "assets\$asset"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $asset
    }
}

if ($missing) {
    throw "Build output is missing assets: $($missing -join ', ')"
}

$hash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
Write-Host "Build verification passed: $executable"
Write-Host "SHA-256: $hash"
Write-Host "Asset count: $($requiredAssets.Count)"
