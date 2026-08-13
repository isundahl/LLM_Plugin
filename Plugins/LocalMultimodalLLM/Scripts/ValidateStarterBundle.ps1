param(
    [Parameter()]
    [string]$BundleRoot,

    [Parameter()]
    [ValidateSet("Core", "Full")]
    [string]$Profile = "Full"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BundleRoot)) {
    $BundleRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
$root = (Resolve-Path -LiteralPath $BundleRoot).Path
$errors = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()

function Require-File([string]$RelativePath) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $RelativePath) -PathType Leaf)) {
        $errors.Add("Missing required bundle file: $RelativePath")
    }
}

function Reject-Path([string]$RelativePath) {
    if (Test-Path -LiteralPath (Join-Path $root $RelativePath)) {
        $errors.Add("Forbidden bundle path is present: $RelativePath")
    }
}

$plugin = Join-Path $root "Plugins/LocalMultimodalLLM"
if (-not (Test-Path -LiteralPath $plugin -PathType Container)) {
    $errors.Add("Missing Plugins/LocalMultimodalLLM")
} else {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $plugin "Scripts/ValidateDistribution.ps1") `
        -PluginPath $plugin -Profile $Profile
    if ($LASTEXITCODE -ne 0) {
        $errors.Add("The bundled plugin failed $Profile-profile distribution validation")
    }
}

foreach ($path in @(
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
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/LICENSE",
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/README.md",
    "ModelLicenses/Gemma4/LICENSE",
    "ModelLicenses/Gemma4/NOTICE.md",
    "ModelLicenses/Parakeet/LICENSE.pdf",
    "ModelLicenses/Parakeet/NOTICE.md",
    "ModelLicenses/PocketTTS/LICENSE",
    "ModelLicenses/PocketTTS/NOTICE.md",
    "ModelLicenses/Voices/LICENSE",
    "ModelLicenses/Voices/NOTICE.md",
    "SHA256SUMS.txt"
)) {
    Require-File $path
}

foreach ($deferred in @(
    "Models/Gemma4E2B/gemma-4-E2B-mmproj-F16.gguf",
    "Models/Gemma4E2B/mtp-gemma-4-E2B-it.gguf"
)) {
    if (Test-Path -LiteralPath (Join-Path $root $deferred) -PathType Leaf) {
        $errors.Add("Deferred v2/experimental Gemma asset is present in the v1 Starter package: $deferred")
    }
}

foreach ($path in @(
    "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26/test_wavs",
    "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/test_wavs",
    "Models/SpeakerVerification/test_wavs",
    "Saved",
    "Intermediate"
)) {
    Reject-Path $path
}

$expectedModelRoots = @(
    (Join-Path $root "Models/Gemma4E2B"),
    (Join-Path $root "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming"),
    (Join-Path $root "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26")
)
$modelFiles = Get-ChildItem -LiteralPath (Join-Path $root "Models") -File -Recurse -Force -ErrorAction SilentlyContinue
foreach ($file in $modelFiles) {
    $insideApprovedRoot = $false
    foreach ($approvedRoot in $expectedModelRoots) {
        if ($file.FullName.StartsWith($approvedRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            $insideApprovedRoot = $true
            break
        }
    }
    if (-not $insideApprovedRoot -and $file.Name -ne "README.md") {
        $errors.Add("Unexpected model payload is present: $($file.FullName.Substring($root.Length + 1))")
    }
}

$restricted = Get-ChildItem -LiteralPath $root -File -Recurse -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "[\\/](EARS|Expresso|test_wavs)[\\/]" }
foreach ($file in $restricted) {
    $errors.Add("Restricted or test recording is present: $($file.FullName.Substring($root.Length + 1))")
}

$checksumPath = Join-Path $root "SHA256SUMS.txt"
if (Test-Path -LiteralPath $checksumPath -PathType Leaf) {
    foreach ($line in Get-Content -LiteralPath $checksumPath) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') {
            $errors.Add("Malformed SHA256SUMS line: $line")
            continue
        }
        $expected = $Matches[1].ToLowerInvariant()
        $relative = $Matches[2]
        $assetPath = Join-Path $root $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
            $errors.Add("SHA256SUMS references a missing file: $relative")
            continue
        }
        $actual = (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $expected) {
            $errors.Add("Checksum mismatch: $relative")
        }
    }
}

if ($warnings.Count -gt 0) {
    Write-Host "Starter package warning(s):" -ForegroundColor Yellow
    $warnings | ForEach-Object { Write-Host " - $_" }
}
if ($errors.Count -gt 0) {
    Write-Host "Starter package validation failed ($($errors.Count) issue(s)):" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" }
    exit 1
}

$bytes = (Get-ChildItem -LiteralPath $root -File -Recurse | Measure-Object Length -Sum).Sum
Write-Host ("Starter package validation passed: {0:N1} MB" -f ($bytes / 1MB)) -ForegroundColor Green
