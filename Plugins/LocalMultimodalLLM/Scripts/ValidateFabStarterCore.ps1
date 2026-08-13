param(
    [Parameter(Mandatory = $true)]
    [string]$PluginPath
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $PluginPath).Path
$errors = [System.Collections.Generic.List[string]]::new()

function Require-File([string]$RelativePath) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $RelativePath) -PathType Leaf)) {
        $errors.Add("Missing required Fab Starter file: $RelativePath")
    }
}

if ((Split-Path -Leaf $root) -ne "LocalMultimodalLLM") {
    $errors.Add("Fab archive root must be named LocalMultimodalLLM")
}

foreach ($path in @(
    "LocalMultimodalLLM.uplugin", "README.md", "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md",
    "AGENTS.md", "llms.txt", "Docs/AIQuickStart.md",
    "Examples/AIIntegration/integration.recipe.json",
    "Source/LocalMultimodalLLM/LocalMultimodalLLM.Build.cs",
    "Source/ThirdParty/LlamaCpp/Lib/Win64/llama.lib",
    "Source/ThirdParty/SherpaOnnx/Lib/Win64/sherpa-onnx-c-api.lib",
    "Binaries/ThirdParty/LlamaCpp/Win64/llama.dll",
    "Binaries/ThirdParty/LlamaCpp/Win64/ggml-vulkan.dll",
    "Binaries/ThirdParty/SherpaOnnx/Win64/sherpa-onnx-c-api.dll",
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
    "ModelLicenses/Gemma4/LICENSE", "ModelLicenses/Gemma4/NOTICE.md",
    "ModelLicenses/Parakeet/LICENSE.pdf", "ModelLicenses/Parakeet/NOTICE.md",
    "ModelLicenses/PocketTTS/LICENSE", "ModelLicenses/PocketTTS/NOTICE.md",
    "ModelLicenses/Voices/LICENSE", "ModelLicenses/Voices/NOTICE.md",
    "SHA256SUMS.txt"
)) { Require-File $path }

foreach ($cuda in @("ggml-cuda.dll", "cudart64_12.dll", "cublas64_12.dll", "cublasLt64_12.dll")) {
    if (Test-Path -LiteralPath (Join-Path $root "Binaries/ThirdParty/LlamaCpp/Win64/$cuda")) {
        $errors.Add("CUDA accelerator belongs in the separate NVIDIA package: $cuda")
    }
}

foreach ($deferred in @(
    "Models/Gemma4E2B/gemma-4-E2B-mmproj-F16.gguf",
    "Models/Gemma4E2B/mtp-gemma-4-E2B-it.gguf"
)) {
    if (Test-Path -LiteralPath (Join-Path $root $deferred)) {
        $errors.Add("Deferred v2/experimental asset is present: $deferred")
    }
}

$badDirectories = Get-ChildItem -LiteralPath $root -Directory -Recurse -Force |
    Where-Object { $_.Name -in @("Intermediate", "Saved", "__pycache__", ".venv", "venv", "test_wavs", "EARS", "Expresso") }
foreach ($directory in $badDirectories) {
    $errors.Add("Generated, test, or restricted directory is present: $($directory.FullName.Substring($root.Length + 1))")
}

$badFiles = Get-ChildItem -LiteralPath $root -File -Recurse -Force | Where-Object {
    $_.Extension -in @(".pdb", ".obj", ".pyc", ".pt", ".pth", ".safetensors") -or
    (($_.Extension -in @(".gguf", ".onnx")) -and
        -not $_.FullName.StartsWith((Join-Path $root "Models") + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase))
}
foreach ($file in $badFiles) {
    $errors.Add("Generated or misplaced model artifact is present: $($file.FullName.Substring($root.Length + 1))")
}

$checksum = Join-Path $root "SHA256SUMS.txt"
if (Test-Path -LiteralPath $checksum -PathType Leaf) {
    $manifestPaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in Get-Content -LiteralPath $checksum) {
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') {
            $errors.Add("Malformed SHA256SUMS line: $line")
            continue
        }
        $expected = $Matches[1].ToLowerInvariant()
        $relative = $Matches[2]
        [void]$manifestPaths.Add($relative)
        $filePath = Join-Path $root $relative.Replace("/", [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            $errors.Add("SHA256SUMS references a missing file: $relative")
            continue
        }
        $actual = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $expected) { $errors.Add("Checksum mismatch: $relative") }
    }
    $assets = Get-ChildItem -LiteralPath (Join-Path $root "Models"), (Join-Path $root "ModelLicenses") -File -Recurse
    foreach ($asset in $assets) {
        $relative = $asset.FullName.Substring($root.Length + 1).Replace("\", "/")
        if (-not $manifestPaths.Contains($relative)) {
            $errors.Add("Model or license asset is absent from SHA256SUMS: $relative")
        }
    }
}

if ($errors.Count -gt 0) {
    Write-Host "Fab Starter Core validation failed ($($errors.Count) issue(s)):" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" }
    exit 1
}

$bytes = (Get-ChildItem -LiteralPath $root -File -Recurse | Measure-Object Length -Sum).Sum
Write-Host ("Fab Starter Core validation passed: {0:N2} GiB" -f ($bytes / 1GB)) -ForegroundColor Green

