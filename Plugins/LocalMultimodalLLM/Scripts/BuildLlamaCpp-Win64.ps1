param(
    [string]$SourceDirectory = (Join-Path $HOME "llama-src"),
    [string]$BuildDirectory = (Join-Path $HOME "lb")
)

$ErrorActionPreference = "Stop"
$Commit = "2969d6d15d67a08e7b83f26164b15350c79c5248"
$VcVars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$Ninja = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$CudaRoot = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"

if (-not (Test-Path -LiteralPath $SourceDirectory)) {
    git clone https://github.com/ggml-org/llama.cpp.git $SourceDirectory
}

git -C $SourceDirectory fetch --tags origin
git -C $SourceDirectory checkout --detach $Commit

# Import the supported MSVC 14.39 developer environment into this process.
$environment = & cmd.exe /d /s /c "`"$VcVars`" -vcvars_ver=14.39 >nul && set"
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}

$arguments = @(
    "-S", $SourceDirectory,
    "-B", $BuildDirectory,
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_CUDA_COMPILER=$CudaRoot\bin\nvcc.exe",
    "-DCMAKE_CUDA_ARCHITECTURES=50-virtual;61-virtual;70-virtual;75-virtual;80-virtual;86-real;89-real;90-virtual;120a-real",
    "-DBUILD_SHARED_LIBS=ON",
    "-DGGML_BACKEND_DL=ON",
    "-DGGML_NATIVE=OFF",
    "-DGGML_CPU=ON",
    "-DGGML_CPU_ALL_VARIANTS=ON",
    "-DGGML_CUDA=ON",
    "-DGGML_CUDA_NCCL=OFF",
    "-DGGML_VULKAN=ON",
    "-DGGML_OPENMP=OFF",
    "-DGGML_CCACHE=OFF",
    "-DLLAMA_BUILD_TOOLS=ON",
    "-DLLAMA_BUILD_APP=OFF",
    "-DLLAMA_BUILD_SERVER=OFF",
    "-DLLAMA_BUILD_TESTS=OFF",
    "-DLLAMA_BUILD_EXAMPLES=OFF"
)

& cmake @arguments
if ($LASTEXITCODE -ne 0) { throw "llama.cpp configuration failed" }
& cmake --build $BuildDirectory --parallel 12
if ($LASTEXITCODE -ne 0) { throw "llama.cpp build failed" }

Write-Host "Build complete at $BuildDirectory"
