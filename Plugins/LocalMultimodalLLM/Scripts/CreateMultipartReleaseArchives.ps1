param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseRoot,

    [Parameter()]
    [string]$OutputDirectory,

    [Parameter()]
    [ValidateRange(256, 2000)]
    [int]$PartSizeMiB = 1900
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$release = (Resolve-Path -LiteralPath $ReleaseRoot).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $release "Assets"
}
$assets = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $assets) {
    throw "Archive output already exists; choose a new empty path: $assets"
}
New-Item -ItemType Directory -Path $assets | Out-Null

$packages = @(
    "LocalMultimodalLLM-0.1.0-beta-Starter-Core-Win64",
    "LocalMultimodalLLM-0.1.0-beta-Starter-NVIDIA-Win64",
    "LocalMultimodalLLM-0.1.0-beta-Starter-Model-Pack"
)
$checksumLines = New-Object System.Collections.Generic.List[string]
$partSize = [int64]$PartSizeMiB * 1MB
$buffer = New-Object byte[] (8MB)

function Get-RelativeArchivePath([string]$Root, [string]$Path) {
    return $Path.Substring($Root.Length).TrimStart([char[]]"\/").Replace("\", "/")
}

function Get-ConcatenatedHash([System.IO.FileInfo[]]$Parts) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($part in $Parts) {
            $stream = [IO.File]::OpenRead($part.FullName)
            try {
                while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    [void]$sha.TransformBlock($buffer, 0, $read, $null, 0)
                }
            }
            finally { $stream.Dispose() }
        }
        [void]$sha.TransformFinalBlock((New-Object byte[] 0), 0, 0)
        return ([BitConverter]::ToString($sha.Hash)).Replace("-", "").ToLowerInvariant()
    }
    finally { $sha.Dispose() }
}

foreach ($package in $packages) {
    $source = Join-Path $release $package
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Missing assembled release package: $source"
    }
    $archiveName = "$package.zip"
    $archivePath = Join-Path $assets $archiveName
    Write-Host "Creating ZIP64 archive: $archiveName" -ForegroundColor Cyan

    $zip = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File | Sort-Object FullName) {
            $entryName = Get-RelativeArchivePath $source $file.FullName
            $entry = $zip.CreateEntry($entryName, [IO.Compression.CompressionLevel]::NoCompression)
            $input = $file.OpenRead()
            $output = $entry.Open()
            try { $input.CopyTo($output, $buffer.Length) }
            finally { $output.Dispose(); $input.Dispose() }
        }
    }
    finally { $zip.Dispose() }

    $expectedFiles = @(Get-ChildItem -LiteralPath $source -Recurse -File).Count
    $readZip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        if ($readZip.Entries.Count -ne $expectedFiles) {
            throw "$archiveName contains $($readZip.Entries.Count) entries; expected $expectedFiles"
        }
    }
    finally { $readZip.Dispose() }

    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $inputArchive = [IO.File]::OpenRead($archivePath)
    $partIndex = 1
    try {
        while ($inputArchive.Position -lt $inputArchive.Length) {
            $partName = "{0}.part{1:d3}" -f $archiveName, $partIndex
            $partPath = Join-Path $assets $partName
            $part = [IO.File]::Create($partPath)
            try {
                $remaining = [Math]::Min($partSize, $inputArchive.Length - $inputArchive.Position)
                while ($remaining -gt 0) {
                    $read = $inputArchive.Read($buffer, 0, [int][Math]::Min($buffer.Length, $remaining))
                    if ($read -le 0) { throw "Unexpected end of archive while creating $partName" }
                    $part.Write($buffer, 0, $read)
                    $remaining -= $read
                }
            }
            finally { $part.Dispose() }
            $partHash = (Get-FileHash -LiteralPath $partPath -Algorithm SHA256).Hash.ToLowerInvariant()
            $checksumLines.Add("$partHash  $partName")
            ++$partIndex
        }
    }
    finally { $inputArchive.Dispose() }

    $parts = @(Get-ChildItem -LiteralPath $assets -Filter "$archiveName.part*" -File | Sort-Object Name)
    $combinedHash = Get-ConcatenatedHash $parts
    if ($combinedHash -ne $archiveHash) { throw "Multipart reassembly hash mismatch for $archiveName" }
    $checksumLines.Add("$archiveHash  $archiveName (after reassembly)")
    Remove-Item -LiteralPath $archivePath -Force
    Write-Host ("Validated {0}: {1} part(s), reconstructed SHA-256 {2}" -f `
        $archiveName, $parts.Count, $archiveHash) -ForegroundColor Green
}

$reassembler = @'
param(
    [Parameter(Mandatory = $true)] [string]$PackageBaseName,
    [Parameter()] [string]$OutputDirectory = "Extracted"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$parts = @(Get-ChildItem -LiteralPath $here -Filter "$PackageBaseName.zip.part*" -File | Sort-Object Name)
if ($parts.Count -eq 0) { throw "No parts found for $PackageBaseName.zip" }
for ($i = 0; $i -lt $parts.Count; ++$i) {
    $expected = "{0}.zip.part{1:d3}" -f $PackageBaseName, ($i + 1)
    if ($parts[$i].Name -ne $expected) { throw "Missing or misordered part: expected $expected" }
}
$zipPath = Join-Path $here "$PackageBaseName.zip"
if (Test-Path -LiteralPath $zipPath) { throw "Refusing to overwrite existing archive: $zipPath" }
$output = [IO.File]::Create($zipPath)
try {
    foreach ($part in $parts) {
        $input = $part.OpenRead()
        try { $input.CopyTo($output) }
        finally { $input.Dispose() }
    }
}
finally { $output.Dispose() }
$manifest = Join-Path $here "RELEASE_ASSET_SHA256SUMS.txt"
$line = Get-Content -LiteralPath $manifest | Where-Object { $_ -like "*  $PackageBaseName.zip (after reassembly)" }
if (-not $line -or $line -notmatch '^([0-9a-fA-F]{64})  ') { throw "Missing archive hash for $PackageBaseName" }
$actual = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
if ($actual -ne $Matches[1]) { throw "Reassembled archive checksum mismatch" }
$destination = [IO.Path]::GetFullPath((Join-Path $here $OutputDirectory))
if (Test-Path -LiteralPath $destination) { throw "Extraction destination already exists: $destination" }
[IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $destination)
Write-Host "Verified and extracted to $destination" -ForegroundColor Green
'@
Set-Content -LiteralPath (Join-Path $assets "Reassemble-And-Extract.ps1") -Value $reassembler -Encoding utf8

foreach ($package in $packages) {
    $short = if ($package -like '*Starter-Core*') { 'Starter-Core' }
        elseif ($package -like '*Starter-NVIDIA*') { 'Starter-NVIDIA' }
        else { 'Starter-Model-Pack' }
    $cmd = "@echo off`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0Reassemble-And-Extract.ps1`" -PackageBaseName `"$package`" -OutputDirectory `"Extracted-$short`"`r`npause`r`n"
    Set-Content -LiteralPath (Join-Path $assets "Extract-$short.cmd") -Value $cmd -Encoding ascii
}

$readme = @"
Local Multimodal LLM 0.1.0-beta downloads
==========================================

Choose exactly one package:

* Starter Core: CPU and Vulkan inference; recommended universal download.
* Starter NVIDIA: Starter Core plus CUDA 12 acceleration.
* Starter Model Pack: models only, for users who already installed the plug-in.

Download every .part file for your chosen package, plus:

  Reassemble-And-Extract.ps1
  RELEASE_ASSET_SHA256SUMS.txt
  the matching Extract-*.cmd file

Keep those files in one directory and double-click the matching Extract-*.cmd.
The script checks part ordering, reconstructs the ZIP64 archive, verifies its
SHA-256 hash, and extracts it. Starter Core and Starter NVIDIA extract directly
into an Unreal project layout containing Plugins, Models, and ModelLicenses.

Each data part is at most $PartSizeMiB MiB so it remains below GitHub's 2 GiB
per-release-asset limit.
"@
Set-Content -LiteralPath (Join-Path $assets "DOWNLOAD_README.txt") -Value $readme -Encoding utf8

foreach ($support in @("Reassemble-And-Extract.ps1", "Extract-Starter-Core.cmd",
    "Extract-Starter-NVIDIA.cmd", "Extract-Starter-Model-Pack.cmd", "DOWNLOAD_README.txt")) {
    $path = Join-Path $assets $support
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumLines.Add("$hash  $support")
}
$checksumLines | Set-Content -LiteralPath (Join-Path $assets "RELEASE_ASSET_SHA256SUMS.txt") -Encoding ascii

$tooLarge = Get-ChildItem -LiteralPath $assets -File | Where-Object { $_.Length -ge 2GB }
if ($tooLarge) { throw "Release assets meet or exceed 2 GiB: $($tooLarge.Name -join ', ')" }
Write-Host "Multipart release assets created at $assets" -ForegroundColor Green
