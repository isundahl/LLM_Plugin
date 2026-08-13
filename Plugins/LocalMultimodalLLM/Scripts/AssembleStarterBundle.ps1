param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [Parameter()]
    [ValidateSet("Core", "Full")]
    [string]$Profile = "Full",

    [Parameter()]
    [string]$ProjectRoot
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
$source = (Resolve-Path -LiteralPath $ProjectRoot).Path
$destination = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $destination) {
    throw "Starter output already exists; choose a new empty path: $destination"
}
New-Item -ItemType Directory -Path $destination | Out-Null

foreach ($file in @("README.md", "LICENSE", "NOTICE", "SHA256SUMS.txt")) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $destination $file)
}

$pluginDestination = Join-Path $destination "Plugins/LocalMultimodalLLM"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $source "Plugins/LocalMultimodalLLM/Scripts/PreparePluginRelease.ps1") `
    -PluginPath (Join-Path $source "Plugins/LocalMultimodalLLM") `
    -OutputPath $pluginDestination -Profile $Profile
if ($LASTEXITCODE -ne 0) { throw "Plugin release preparation failed" }

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
    $destinationFile = Join-Path $destination $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationFile) | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $destinationFile
}
Copy-Item -LiteralPath (Join-Path $source "ModelLicenses") `
    -Destination (Join-Path $destination "ModelLicenses") -Recurse

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $pluginDestination "Scripts/ValidateStarterBundle.ps1") `
    -BundleRoot $destination -Profile $Profile
if ($LASTEXITCODE -ne 0) { throw "Starter candidate failed validation" }

Write-Host "Pocket TTS CC BY 4.0 provenance is recorded in ModelLicenses/PocketTTS/NOTICE.md." -ForegroundColor Green
