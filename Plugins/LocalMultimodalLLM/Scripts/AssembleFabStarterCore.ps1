param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter()]
    [string]$ProjectRoot,

    [Parameter()]
    [string]$VersionLabel = "0.1.0-beta-UE5.8"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
$source = (Resolve-Path -LiteralPath $ProjectRoot).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
$pluginDestination = Join-Path $output "LocalMultimodalLLM"
$zipPath = Join-Path $output "LocalMultimodalLLM-$VersionLabel-Fab-Starter-Core-Win64.zip"
$zipHashPath = "$zipPath.sha256"
$submissionNotes = Join-Path $output "FAB_SUBMISSION_README.md"

foreach ($path in @($pluginDestination, $zipPath, $zipHashPath, $submissionNotes)) {
    if (Test-Path -LiteralPath $path) {
        throw "Fab output already exists; remove it or choose a new directory: $path"
    }
}
New-Item -ItemType Directory -Force -Path $output | Out-Null

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $source "Plugins/LocalMultimodalLLM/Scripts/PreparePluginRelease.ps1") `
    -PluginPath (Join-Path $source "Plugins/LocalMultimodalLLM") `
    -OutputPath $pluginDestination -Profile Core
if ($LASTEXITCODE -ne 0) { throw "Core plug-in preparation failed" }

$files = @(
    "Models/Gemma4E2B/gemma-4-e2b-it-qat.localllm.json",
    "Models/Gemma4E2B/gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/encoder.int8.onnx",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/decoder.int8.onnx",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/joiner.int8.onnx",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/tokens.txt",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/bias.md",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/explainability.md",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/privacy.md",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/safety.md",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/lm_flow.int8.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/lm_main.int8.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/encoder.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/decoder.int8.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/text_conditioner.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/vocab.json",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/token_scores.json",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/LICENSE",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/README.md"
)
foreach ($relative in $files) {
    $sourceFile = Join-Path $source $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Missing approved Starter source file: $relative"
    }
    $destinationFile = Join-Path $pluginDestination $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationFile) | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $destinationFile
}
Copy-Item -LiteralPath (Join-Path $source "ModelLicenses") `
    -Destination (Join-Path $pluginDestination "ModelLicenses") -Recurse

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $pluginDestination "Scripts/GenerateFabStarterChecksums.ps1") `
    -PluginPath $pluginDestination
if ($LASTEXITCODE -ne 0) { throw "Fab checksum generation failed" }

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $pluginDestination "Scripts/ValidateFabStarterCore.ps1") `
    -PluginPath $pluginDestination
if ($LASTEXITCODE -ne 0) { throw "Fab Starter Core validation failed" }

Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$zipStream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
$archive = [IO.Compression.ZipArchive]::new($zipStream, [IO.Compression.ZipArchiveMode]::Create, $false)
try {
    foreach ($file in Get-ChildItem -LiteralPath $pluginDestination -File -Recurse | Sort-Object FullName) {
        $relative = $file.FullName.Substring($pluginDestination.Length + 1).Replace("\", "/")
        $entry = $archive.CreateEntry("LocalMultimodalLLM/$relative", [IO.Compression.CompressionLevel]::NoCompression)
        $entry.LastWriteTime = $file.LastWriteTime
        $input = [IO.File]::OpenRead($file.FullName)
        $outputStream = $entry.Open()
        try { $input.CopyTo($outputStream) } finally { $outputStream.Dispose(); $input.Dispose() }
    }
} finally {
    $archive.Dispose()
    $zipStream.Dispose()
}

$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($zipHashPath, "$zipHash  $([IO.Path]::GetFileName($zipPath))`r`n", [Text.Encoding]::ASCII)

$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if ($archive.Entries.Count -lt 100) { throw "Fab ZIP contains unexpectedly few entries" }
    if (-not ($archive.Entries | Where-Object {
        $_.FullName.Replace("\", "/") -eq "LocalMultimodalLLM/LocalMultimodalLLM.uplugin"
    })) {
        throw "Fab ZIP does not contain the required single top-level plug-in folder"
    }
} finally {
    $archive.Dispose()
}

$zipBytes = (Get-Item -LiteralPath $zipPath).Length
$notes = @"
# Fab submission artifact

- File: $([IO.Path]::GetFileName($zipPath))
- Size: $([Math]::Round($zipBytes / 1GB, 2)) GiB
- SHA-256: $zipHash
- Layout: one top-level LocalMultimodalLLM/ plug-in folder
- Profile: Win64 Starter Core (CPU + Vulkan, Gemma, Parakeet, Pocket TTS)
- CUDA: not included; publish the NVIDIA accelerator separately

Upload this ZIP to stable external storage and use the direct, reviewer-accessible
download link in the Fab listing. Test that link in a signed-out browser before
submission. Preserve this hash alongside the uploaded artifact.
"@
[IO.File]::WriteAllText($submissionNotes, $notes, [Text.UTF8Encoding]::new($false))

Write-Host ("Created Fab Starter Core ZIP: {0} ({1:N2} GiB)" -f $zipPath, ($zipBytes / 1GB)) -ForegroundColor Green
Write-Host "SHA-256: $zipHash" -ForegroundColor Green
