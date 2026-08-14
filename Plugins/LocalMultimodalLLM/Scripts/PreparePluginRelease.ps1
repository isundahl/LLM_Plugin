param(
    [Parameter()]
    [string]$PluginPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [Parameter()]
    [ValidateSet("Core", "Full")]
    [string]$Profile = "Full"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($PluginPath)) {
    $PluginPath = Split-Path -Parent $PSScriptRoot
}

$source = (Resolve-Path -LiteralPath $PluginPath).Path
$destination = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $destination) {
    throw "Release output already exists; choose a new empty path: $destination"
}

New-Item -ItemType Directory -Path $destination | Out-Null

foreach ($file in @(
    "LocalMultimodalLLM.uplugin", "README.md", "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md",
    "AGENTS.md", "llms.txt"
)) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $destination $file)
}

foreach ($directory in @("Config", "Content", "Docs", "Examples", "Resources", "Scripts", "Source")) {
    Copy-Item -LiteralPath (Join-Path $source $directory) -Destination $destination -Recurse
}

$thirdPartyBinaries = Join-Path $source "Binaries/ThirdParty"
if (Test-Path -LiteralPath $thirdPartyBinaries -PathType Container) {
    $binaryDestination = Join-Path $destination "Binaries"
    New-Item -ItemType Directory -Path $binaryDestination | Out-Null
    Copy-Item -LiteralPath $thirdPartyBinaries -Destination $binaryDestination -Recurse
}

if ($Profile -eq "Core") {
    $llamaRuntime = Join-Path $destination "Binaries/ThirdParty/LlamaCpp/Win64"
    foreach ($cudaFile in @("ggml-cuda.dll", "cudart64_12.dll", "cublas64_12.dll", "cublasLt64_12.dll")) {
        $candidate = Join-Path $llamaRuntime $cudaFile
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Remove-Item -LiteralPath $candidate -Force
        }
    }
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $destination "Scripts/ValidateDistribution.ps1") `
    -PluginPath $destination -Profile $Profile
if ($LASTEXITCODE -ne 0) {
    throw "Prepared release candidate failed distribution validation"
}

$bytes = (Get-ChildItem -LiteralPath $destination -File -Recurse | Measure-Object Length -Sum).Sum
Write-Host ("Prepared clean {0} release candidate at {1} ({2:N1} MB)" -f `
    $Profile, $destination, ($bytes / 1MB)) -ForegroundColor Green
