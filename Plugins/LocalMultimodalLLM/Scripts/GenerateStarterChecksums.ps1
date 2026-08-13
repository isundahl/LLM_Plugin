param(
    [Parameter()]
    [string]$BundleRoot
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BundleRoot)) {
    $BundleRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
$root = (Resolve-Path -LiteralPath $BundleRoot).Path
$relativeFiles = @(
    "Plugins/LocalMultimodalLLM/Content/Voices/pocket-bill-boerst.wav",
    "Plugins/LocalMultimodalLLM/Content/Voices/pocket-caro-davy.wav",
    "Plugins/LocalMultimodalLLM/Content/Voices/pocket-peter-yearsley.wav",
    "Plugins/LocalMultimodalLLM/Content/Voices/pocket-stuart-bell.wav",
    "Models/Gemma4E2B/gemma-4-e2b-it-qat.localllm.json",
    "Models/Gemma4E2B/gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/encoder.int8.onnx",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/decoder.int8.onnx",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/joiner.int8.onnx",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/tokens.txt",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/lm_flow.int8.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/lm_main.int8.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/encoder.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/decoder.int8.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/text_conditioner.onnx",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/vocab.json",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/token_scores.json",
    "ModelLicenses/Gemma4/LICENSE",
    "ModelLicenses/Gemma4/NOTICE.md",
    "ModelLicenses/Parakeet/LICENSE.pdf",
    "ModelLicenses/Parakeet/NOTICE.md",
    "ModelLicenses/PocketTTS/LICENSE",
    "ModelLicenses/PocketTTS/NOTICE.md",
    "ModelLicenses/Voices/LICENSE",
    "ModelLicenses/Voices/NOTICE.md"
)

$lines = foreach ($relative in $relativeFiles) {
    $nativeRelative = $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
    $path = Join-Path $root $nativeRelative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Cannot hash missing Starter asset: $relative"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}

$output = Join-Path $root "SHA256SUMS.txt"
[IO.File]::WriteAllLines($output, $lines, [Text.Encoding]::ASCII)
Write-Host "Wrote $($lines.Count) verified Starter hashes to $output" -ForegroundColor Green
