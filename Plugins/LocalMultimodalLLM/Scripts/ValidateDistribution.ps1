param(
    [Parameter()]
    [string]$PluginPath,

    [Parameter()]
    [ValidateSet("Core", "Full")]
    [string]$Profile = "Full"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($PluginPath)) {
    $PluginPath = Split-Path -Parent $PSScriptRoot
}
$plugin = (Resolve-Path -LiteralPath $PluginPath).Path
$errors = [System.Collections.Generic.List[string]]::new()

function Require-File([string]$RelativePath) {
    if (-not (Test-Path -LiteralPath (Join-Path $plugin $RelativePath) -PathType Leaf)) {
        $errors.Add("Missing required file: $RelativePath")
    }
}

foreach ($path in @(
    "LocalMultimodalLLM.uplugin",
    "README.md",
    "LICENSE",
    "NOTICE",
    "THIRD_PARTY_NOTICES.md",
    "Source/ThirdParty/SherpaOnnx/ONNXRUNTIME-LICENSE",
    "Source/ThirdParty/LlamaCpp/LICENSE",
    "Source/ThirdParty/SherpaOnnx/LICENSE",
    "Source/ThirdParty/LlamaCpp/Lib/Win64/llama.lib",
    "Source/ThirdParty/LlamaCpp/Lib/Win64/mtmd.lib",
    "Source/ThirdParty/LlamaCpp/Lib/Win64/ggml.lib",
    "Source/ThirdParty/LlamaCpp/Lib/Win64/ggml-base.lib",
    "Content/Voices/README.md",
    "Content/Voices/pocket-caro-davy.wav",
    "Content/Voices/pocket-bill-boerst.wav",
    "Content/Voices/pocket-peter-yearsley.wav",
    "Content/Voices/pocket-stuart-bell.wav"
)) {
    Require-File $path
}

$noticePath = Join-Path $plugin "NOTICE"
if (Test-Path -LiteralPath $noticePath -PathType Leaf) {
    $noticeText = Get-Content -LiteralPath $noticePath -Raw
    if ($noticeText -notmatch [regex]::Escape("Ian Sundahl")) {
        $errors.Add("NOTICE does not preserve the Ian Sundahl attribution")
    }
    if ($noticeText -notmatch [regex]::Escape("Volley Studios")) {
        $errors.Add("NOTICE does not preserve the Volley Studios attribution")
    }
}

$descriptorPath = Join-Path $plugin "LocalMultimodalLLM.uplugin"
if (Test-Path -LiteralPath $descriptorPath -PathType Leaf) {
    $descriptorText = Get-Content -LiteralPath $descriptorPath -Raw
    if ($descriptorText -notmatch [regex]::Escape("Ian Sundahl") -or
        $descriptorText -notmatch [regex]::Escape("Volley Studios")) {
        $errors.Add("Plug-in metadata does not identify Ian Sundahl and Volley Studios")
    }
}

$llamaRuntime = "Binaries/ThirdParty/LlamaCpp/Win64"
foreach ($dll in @(
    "llama.dll", "mtmd.dll", "ggml.dll", "ggml-base.dll",
    "ggml-cpu-x64.dll", "ggml-cpu-sse42.dll",
    "ggml-cpu-sandybridge.dll", "ggml-cpu-haswell.dll",
    "ggml-cpu-skylakex.dll", "ggml-cpu-cannonlake.dll",
    "ggml-cpu-cascadelake.dll", "ggml-cpu-icelake.dll",
    "ggml-cpu-alderlake.dll", "ggml-vulkan.dll"
)) {
    Require-File "$llamaRuntime/$dll"
}

if ($Profile -eq "Full") {
    foreach ($dll in @("ggml-cuda.dll", "cudart64_12.dll", "cublas64_12.dll", "cublasLt64_12.dll")) {
        Require-File "$llamaRuntime/$dll"
    }
}

foreach ($dll in @("sherpa-onnx-c-api.dll", "onnxruntime.dll", "onnxruntime_providers_shared.dll")) {
    Require-File "Binaries/ThirdParty/SherpaOnnx/Win64/$dll"
}
Require-File "Source/ThirdParty/SherpaOnnx/Lib/Win64/sherpa-onnx-c-api.lib"

$forbiddenDirectories = Get-ChildItem -LiteralPath $plugin -Directory -Recurse -Force |
    Where-Object { $_.Name -in @("Intermediate", "__pycache__", ".venv", "venv") }
foreach ($directory in $forbiddenDirectories) {
    $errors.Add("Generated/development directory present: $($directory.FullName.Substring($plugin.Length + 1))")
}

$forbiddenFiles = Get-ChildItem -LiteralPath $plugin -File -Recurse -Force |
    Where-Object {
        $_.Extension -in @(".pdb", ".obj", ".pyc", ".gguf", ".onnx", ".pt", ".pth", ".safetensors") -or
        ($_.Extension -eq ".wav" -and $_.FullName -notlike "*Content*")
    }
foreach ($file in $forbiddenFiles) {
    $errors.Add("Generated/model/test artifact present: $($file.FullName.Substring($plugin.Length + 1))")
}

foreach ($requiredDoc in @(
    "Docs/UserGuide.md",
    "Docs/Packaging.md",
    "Docs/StarterModels.md",
    "Docs/VisionDevelopment.md",
    "Docs/TextToSpeech.md",
    "Docs/SpeechToText.md"
)) {
    Require-File $requiredDoc
}

$restrictedVoiceFiles = Get-ChildItem -LiteralPath $plugin -File -Recurse -Force |
    Where-Object {
        $_.Extension -eq ".wav" -and
        ($_.FullName -match "[\\/](EARS|Expresso|test_wavs)[\\/]")
    }
foreach ($file in $restrictedVoiceFiles) {
    $errors.Add("Restricted or unapproved reference voice present: $($file.FullName.Substring($plugin.Length + 1))")
}

if ($Profile -eq "Core") {
    foreach ($dll in @("ggml-cuda.dll", "cudart64_12.dll", "cublas64_12.dll", "cublasLt64_12.dll")) {
        if (Test-Path -LiteralPath (Join-Path $plugin "$llamaRuntime/$dll")) {
            $errors.Add("CUDA file is present in Core profile: $dll")
        }
    }
}

if ($errors.Count -gt 0) {
    Write-Host "Distribution validation failed ($($errors.Count) issue(s)):" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" }
    exit 1
}

$bytes = (Get-ChildItem -LiteralPath $plugin -File -Recurse | Measure-Object Length -Sum).Sum
Write-Host ("Distribution validation passed: {0} profile, {1:N1} MB" -f $Profile, ($bytes / 1MB)) -ForegroundColor Green
