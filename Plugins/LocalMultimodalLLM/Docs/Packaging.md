# Release and packaging guide

Local Multimodal LLM is copyright 2026 Ian Sundahl, Volley Studios, and is
licensed under Apache License 2.0. Every plug-in archive must include the
top-level `LICENSE` and `NOTICE` files. Redistributors and derivative works must
preserve the applicable license, copyright, and readable NOTICE attribution as
described by Apache 2.0 Section 4. When practical, please also add “Local
Multimodal LLM created by Ian Sundahl and Volley Studios” to the product
credits, documentation, About screen, or another readable acknowledgement.

This is a plug-in-maintainer release guide, not an installation task for game
developers or players. A released plug-in must already contain the matching
headers, import libraries, and runtime DLLs described below. Keep the working
checkout fully equipped for development and stage a separate copy for release.

The lean Win64 release uses the **Core** profile (CPU + Vulkan). Publish CUDA as
an optional accelerator and also offer a preassembled **Starter NVIDIA** archive
for users who want CUDA without manually overlaying runtime files. Consumers
should never be asked to locate or compile third-party binaries themselves.

## Preparing the plug-in archive

Run Unreal `BuildPlugin` as the clean-host compile gate, but do not zip its raw
output: Unreal leaves `Intermediate` data and an editor PDB in that directory.
Prepare the customer-facing source/runtime plug-in from the working plug-in:

```powershell
powershell -ExecutionPolicy Bypass -File .\Plugins\LocalMultimodalLLM\Scripts\PreparePluginRelease.ps1 `
  -OutputPath .\Saved\Release\LocalMultimodalLLM -Profile Full
```

The script retains source, documentation, configuration, examples, approved
content, import libraries, and native runtime DLLs. It excludes local/editor
build products and validates the resulting candidate before returning success.

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
does not prohibit approved, licensed starter weights stored at the release
root; see [Starter Models](StarterModels.md).

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

### Full: Core + CUDA (Starter NVIDIA / maintainer profile)

Also keep:

- `ggml-cuda.dll`
- `cudart64_12.dll`
- `cublas64_12.dll`
- `cublasLt64_12.dll`

The CUDA set is detected as a unit. Starter NVIDIA includes it, so NVIDIA
acceleration works without another download. The Core release omits it while
retaining CPU and Vulkan support.

Do not mix DLLs or import libraries from different llama.cpp builds. Rebuild and
replace the entire matching set when upgrading.

## Speech runtime

The native `sherpa-onnx` STT and Pocket TTS adapter require:

- `sherpa-onnx-c-api.dll`
- `onnxruntime.dll`
- `onnxruntime_providers_shared.dll`
- the matching `sherpa-onnx-c-api.lib` and headers

Speech model directories are project-level assets rather than files inside the
plug-in. The Starter downloads stage their approved Parakeet and Pocket model
directories under `<Project>/Models`; a source-only checkout does not. NeuTTS-2E
is a development Python/CPU adapter and Chatterbox is a development Python/CUDA
adapter. Both are registered only in non-Shipping Win64 builds and rely on
project-side environments under `Saved/`; neither is staged as part of the
plug-in. The built-in Shipping speech-output path is native Pocket TTS.

`LocalMultimodalLLM.Build.cs` adds only the approved Gemma, Parakeet, Pocket,
model-license, and checksum files as loose NonUFS runtime dependencies when they
exist beside the `.uproject`. It deliberately does not recursively stage the
whole `Models` directory. This keeps upstream `test_wavs`, experimental models,
speaker-verification samples, and local datasets out of packaged games. Custom
models require an explicit project-owned staging rule.

Pocket source is MIT, upstream and converted weights are CC BY 4.0, and
sherpa-onnx is Apache 2.0. The exporter's verified January 28 license commit and
the bundle's February 10 CC BY legal file resolve its stale non-commercial
README sentence; preserve the evidence in `ModelLicenses/PocketTTS`. Never
stage its `test_wavs`. The Caro Davy, Bill Boerst, Peter Yearsley, and Stuart Bell presets are CC0
Voice-Zero recordings and are explicitly staged as NonUFS runtime files. Preserve the
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

1. Run Unreal AutomationTool's `BuildPlugin` command as a clean compile gate for
   Win64. It must pass its editor, Development game, and Shipping game targets.
   The generated package contains precompiled build products; it is not the
   same thing as the lean source-package layout described below.
2. Stage a fresh source copy of the plug-in, excluding `Intermediate` and
   `Binaries/Win64` while retaining `Binaries/ThirdParty`.
3. Remove the exclusions above and choose Core or Full.
4. Run:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
     .\Scripts\ValidateDistribution.ps1 `
     -PluginPath <staged-plugin-path> -Profile Full
   ```

   For a complete Starter download, also run from its extracted root:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
     .\Plugins\LocalMultimodalLLM\Scripts\ValidateStarterBundle.ps1 `
     -BundleRoot <staged-bundle-root>
   ```

5. Compile a clean Development build and a Shipping build.
6. Package a small test project and launch it on a clean Win64 machine.
7. Test model discovery, CPU fallback, the selected GPU backend, STT/TTS, tool
   validation, cancellation, and missing-asset errors.
8. Verify every external model, voice, dataset, CUDA runtime, and demo asset has
   suitable redistribution terms.
9. Confirm the plug-in's Apache 2.0 `LICENSE` and Ian Sundahl / Volley Studios
   `NOTICE` survived staging, along with all applicable third-party notices.

The validation script checks structure and obvious accidental payloads; it does
not replace a compile, runtime test, security review, or license review.

### Maintainer backend matrix

Non-Shipping builds accept a diagnostic-only command-line selection for the
native Gemma smoke test. It does not change normal automatic backend selection
and is compiled out of Shipping:

```text
-LocalLLMDiagnosticBackend=cpu
-LocalLLMDiagnosticBackend=vulkan
-LocalLLMDiagnosticBackend=cuda
```

Run `LocalMultimodalLLM.Native.Gemma4TextSmoke` once with each value. The test
requires the requested device to be recorded as `diagnostic-selected` in the
model-loaded event, so a passing result demonstrates inference on that selected
device rather than merely proving that its DLL exists.

## Runtime selection and logging

The Full profile stages CPU, Vulkan, and CUDA together. With the default
`GpuLayers = -1`, llama.cpp enumerates available devices, prefers CUDA when the
same physical NVIDIA GPU is also exposed through Vulkan, otherwise uses an
available GPU backend, and finally retains CPU as the universal fallback. The
Core profile removes CUDA, leaving Vulkan with CPU fallback.

Each successful model load emits one concise `inference=` status containing the
selected device and actual GPU layer count. If GPU offload was requested but
zero layers were offloaded, the plug-in emits one `Warning` event as well as an
Unreal warning log so a game can show a performance notice. An intentional
`GpuLayers = 0` CPU configuration is reported at Display level and is not
treated as fallback failure.

Model manifests default `load.allowGpuLoadFallback` to `true`. If a
GPU-requested model load fails (most commonly because its allocation does not
fit), the plug-in emits a warning and makes one bounded CPU load attempt. The
successful `ModelLoaded` detail includes `load-fallback=CPU`. Set the field to
`false` for fail-fast behavior. The recovery intentionally does not attempt a
mixed partial-layer load: the tested llama.cpp scheduler can reject some
architectures after such a load, whereas the complete CPU retry is predictable.

Maintainers can exercise this path without exhausting a machine's VRAM by
adding `-LocalLLMDiagnosticGpuLoadFailures=1` to a non-Shipping native Gemma
smoke-test run. This simulation switch is compiled out of Shipping.

Normal llama.cpp informational output is routed to `Verbose`, per-layer/debug
output to `VeryVerbose`, and only errors plus the first occurrence of an
upstream warning are shown at normal verbosity. Repeated identical warnings are
demoted to `Verbose`. Developers can opt into full diagnostics with:

```text
-LogCmds="LogLocalMultimodalLLM VeryVerbose"
```
