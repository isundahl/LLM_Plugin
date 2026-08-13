param(
    [Parameter(Mandatory = $true)]
    [string]$PluginPath
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $PluginPath).Path
$relativeFiles = [System.Collections.Generic.List[string]]::new()

foreach ($directory in @("Models", "ModelLicenses")) {
    $absolute = Join-Path $root $directory
    if (-not (Test-Path -LiteralPath $absolute -PathType Container)) {
        throw "Cannot hash missing Fab Starter directory: $directory"
    }
    Get-ChildItem -LiteralPath $absolute -File -Recurse -Force |
        Sort-Object FullName |
        ForEach-Object {
            $relativeFiles.Add($_.FullName.Substring($root.Length + 1).Replace("\", "/"))
        }
}

foreach ($voice in @(
    "Content/Voices/pocket-bill-boerst.wav",
    "Content/Voices/pocket-caro-davy.wav",
    "Content/Voices/pocket-peter-yearsley.wav",
    "Content/Voices/pocket-stuart-bell.wav"
)) {
    $relativeFiles.Add($voice)
}

$lines = foreach ($relative in ($relativeFiles | Sort-Object -Unique)) {
    $path = Join-Path $root $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Cannot hash missing Fab Starter asset: $relative"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}

$output = Join-Path $root "SHA256SUMS.txt"
[IO.File]::WriteAllLines($output, $lines, [Text.Encoding]::ASCII)
Write-Host "Wrote $($lines.Count) Fab Starter hashes to $output" -ForegroundColor Green

