param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

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
    throw "Model Pack output already exists; choose a new empty path: $destination"
}
New-Item -ItemType Directory -Path $destination | Out-Null

foreach ($file in @("LICENSE", "NOTICE")) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $destination $file)
}
Copy-Item -LiteralPath (Join-Path $source "Plugins/LocalMultimodalLLM/Docs/StarterModelPackREADME.md") `
    -Destination (Join-Path $destination "README.md")

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

$forbidden = Get-ChildItem -LiteralPath $destination -File -Recurse | Where-Object {
    $_.FullName -match '[\\/](test_wavs|EARS|Expresso)[\\/]' -or
    $_.Name -match '^(mtp-|.*mmproj.*\.gguf$)'
}
if ($forbidden) { throw "Forbidden files entered Model Pack: $($forbidden.FullName -join ', ')" }

$checksumPath = Join-Path $destination "SHA256SUMS.txt"
$checksumLines = Get-ChildItem -LiteralPath $destination -File -Recurse |
    Where-Object { $_.FullName -ne $checksumPath } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($destination.Length).TrimStart([char[]]"\/").Replace("\", "/")
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding ascii

foreach ($line in Get-Content -LiteralPath $checksumPath) {
    if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw "Malformed checksum line: $line" }
    $relative = $Matches[2]
    $path = Join-Path $destination $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing hashed Model Pack file: $relative" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Matches[1].ToLowerInvariant()) { throw "Checksum mismatch: $relative" }
}

$bytes = (Get-ChildItem -LiteralPath $destination -File -Recurse | Measure-Object Length -Sum).Sum
Write-Host ("Starter Model Pack validation passed: {0:N1} MB" -f ($bytes / 1MB)) -ForegroundColor Green
