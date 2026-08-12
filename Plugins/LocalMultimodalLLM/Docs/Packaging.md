# Release and packaging guide

This is a plug-in-maintainer release guide, not an installation task for game
developers or players. A released plug-in must already contain the matching
headers, import libraries, and runtime DLLs described below. Keep the working
checkout fully equipped for development and stage a separate copy for release.

The standard Win64 release uses the **Full** profile so CPU, Vulkan, and CUDA
work without any manual third-party setup. A publisher may additionally offer
a clearly labeled slim **Core** package, but consumers should never be asked to
assemble either runtime themselves.

## Do not ship

Exclude all of the following from the **plug-in folder**:

- `Intermediate/`
- `Binaries/Win64/*.pdb`
- object files, compiler caches, logs, and crash output
- model weights (stage approved starter weights at the release root under
  `Models`, never inside `Plugins/LocalMultimodalLLM`)
- Python environments and benchmark output under the project's `Saved/`
- raw voice datasets and derived recordings
- project-only demo maps, MetaHumans, animation packs, and licensed environment assets

The project's EARS benchmark recordings and TTS benchmark configurations live
under `<Project>/TestData/TTS`, outside the plug-in, for this reason. This rule
does not prohibit the approved, licensed starter weights in the Full Starter
Bundle; see [Starter Models](StarterModels.md).

Keep the plug-in's small cooked `Content/Examples/DA_ExampleLocalLLMToolSet`
asset if the distributable is meant to include the starter tool-set example.

## Required llama.cpp core

The following artifacts form one matching llama.cpp ABI set:

`Source/ThirdParty/LlamaCpp/Lib/Win64`

- `llama.lib`
- `mtmd.lib`
- `ggml.lib`
- `ggml-base.lib`

`Binaries/ThirdParty/LlamaCpp/Win64`

- `llama.dll`
- `mtmd.dll`
- `ggml.dll`
- `ggml-base.dll`

The headers in `Source/ThirdParty/LlamaCpp/Include` must come from that same
build. If any core artifact is absent, the Build.cs intentionally sets
`LOCAL_MULTIMODAL_LLM_WITH_LLAMA=0`.

## Backend profiles

### Core: CPU + Vulkan (optional slim package)

Keep:

- all nine `ggml-cpu-*.dll` variants;
- `ggml-vulkan.dll`;
- the llama.cpp core files above;
- the SherpaOnnx import library, headers, and its three runtime DLLs.

CPU variants let ggml select a safe implementation for the customer's
processor, while Vulkan supplies a vendor-neutral GPU option.

### Full: Core + CUDA (standard release)

Also keep:

- `ggml-cuda.dll`
- `cudart64_12.dll`
- `cublas64_12.dll`
- `cublasLt64_12.dll`

The CUDA set is detected as a unit. The standard complete plug-in includes it,
so NVIDIA acceleration works without another download. A deliberately slim
Core release may omit it while retaining CPU and Vulkan support.

Do not mix DLLs or import libraries from different llama.cpp builds. Rebuild and
replace the entire matching set when upgrading.

## Speech runtime

The native `sherpa-onnx` STT and Pocket TTS adapter require:

- `sherpa-onnx-c-api.dll`
- `onnxruntime.dll`
- `onnxruntime_providers_shared.dll`
- the matching `sherpa-onnx-c-api.lib` and headers

Speech model directories are project-level assets rather than files inside the
plug-in. The Full Starter Bundle stages its approved Parakeet and Pocket model
directories under `<Project>/Models`; a source-only checkout does not. NeuTTS-2E
is a development Python/CPU adapter and Chatterbox is a development Python/CUDA
adapter. Both are registered only in non-Shipping Win64 builds and rely on
project-side environments under `Saved/`; neither is staged as part of the
plug-in. The built-in Shipping speech-output path is native Pocket TTS.

The Pocket native path may be used commercially: Pocket source is MIT, the
upstream weights and tested ONNX bundle are CC BY 4.0, and sherpa-onnx is
Apache 2.0. Do not stage the downloaded bundle's `test_wavs`; stage a
project-owned, licensed, and consented reference voice instead. The plugin's
`pocket-caro-davy.wav` and `pocket-bill-boerst.wav` presets are CC0 Voice-Zero
recordings and are explicitly staged as NonUFS runtime files. Preserve the
required model/runtime attribution and license files and comply with the
upstream gated-model use policy.

EARS and Expresso references are CC BY-NC 4.0 and are forbidden from a normal
commercial release profile. They may remain only in excluded local benchmark
data unless separate commercial permission is obtained.

NeuTTS-2E uses the NeuTTS Open License v1.0. Redistribution and commercial use
are permitted below its USD $5 million annual-revenue threshold, while larger
commercial users require a separate Neuphonic license. The current provider is
Python-based and unavailable in Shipping regardless of model-license
eligibility.

## Before distributing

1. Stage a fresh copy of the plug-in.
2. Remove the exclusions above and choose Core or Full.
3. Run:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
     .\Scripts\ValidateDistribution.ps1 `
     -PluginPath <staged-plugin-path> -Profile Full
   ```

4. Compile a clean Development build and a Shipping build.
5. Package a small test project and launch it on a clean Win64 machine.
6. Test model discovery, CPU fallback, the selected GPU backend, STT/TTS, tool
   validation, cancellation, and missing-asset errors.
7. Verify every external model, voice, dataset, CUDA runtime, and demo asset has
   suitable redistribution terms.
8. Add a license for the plug-in itself before public distribution.

The validation script checks structure and obvious accidental payloads; it does
not replace a compile, runtime test, security review, or license review.
