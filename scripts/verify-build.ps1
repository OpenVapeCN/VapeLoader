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

if (-not ('Vape421ResourceVerifier' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class Vape421ResourceVerifier {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint SizeofResource(IntPtr module, IntPtr resource);

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FreeLibrary(IntPtr module);
}
'@
}

$assetRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\assets'))
$requiredAssets = [ordered]@{
    101 = 'close.png'
    102 = 'logo.png'
    103 = 'mask-bottom.png'
    104 = 'mask-left.png'
    105 = 'mask-right.png'
    106 = 'mask-top.png'
    107 = 'minimize.png'
    108 = 'proxima-regular.ttf'
    109 = 'proxima-semibold.otf'
    110 = 'rounded-rect.png'
}

$module = [Vape421ResourceVerifier]::LoadLibraryEx($executable, [IntPtr]::Zero, 2)
if ($module -eq [IntPtr]::Zero) {
    throw "Unable to inspect executable resources: $executable"
}
try {
    foreach ($entry in $requiredAssets.GetEnumerator()) {
        $resource = [Vape421ResourceVerifier]::FindResource(
            $module, [IntPtr]$entry.Key, [IntPtr]10)
        if ($resource -eq [IntPtr]::Zero) {
            throw "Embedded asset is missing: $($entry.Value)"
        }
        $expectedSize = (Get-Item -LiteralPath (Join-Path $assetRoot $entry.Value)).Length
        $actualSize = [Vape421ResourceVerifier]::SizeofResource($module, $resource)
        if ($actualSize -ne $expectedSize) {
            throw "Embedded asset size mismatch: $($entry.Value)"
        }
    }
}
finally {
    [void][Vape421ResourceVerifier]::FreeLibrary($module)
}

$hash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
Write-Host "Build verification passed: $executable"
Write-Host "SHA-256: $hash"
Write-Host "Embedded asset count: $($requiredAssets.Count)"
