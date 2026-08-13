# Starter models and release layout

The plug-in itself is copyright 2026 Ian Sundahl, Volley Studios, under Apache
License 2.0. Model, voice, and native-runtime assets retain the independent
terms listed below.

The normal end-user downloads are **Starter Core** and **Starter NVIDIA**, not a
bare Git checkout. Both are designed to work without asking a developer to
find, convert, rename, or configure model weights. The same weights are also
published as an optional standalone **Starter Model Pack** for modular installs.

## Default stack

| Role | Tested asset | Approximate size | Governing terms |
| --- | --- | ---: | --- |
| LLM | Gemma 4 E2B IT QAT, Unsloth `UD-Q4_K_XL` GGUF | 2.50 GB | Apache 2.0 |
| Speech to text | Parakeet Unified English 0.6B INT8 sherpa-onnx export | 633 MB | NVIDIA Open Model License |
| Text to speech | Pocket TTS INT8 sherpa-onnx export | 194 MB | CC BY 4.0 |
| Starter voices | Caro Davy, Bill Boerst, Peter Yearsley, and Stuart Bell Pocket references | about 2.8 MiB | CC0 / Voice-Zero |

The current tested default is **Gemma 4 E2B**, not E4B. E4B can be offered as
an optional higher-capability profile after it receives the same native and
roleplay benchmark coverage.

The v1 Starter profile does not distribute a projector or MTP file. Vision is
nevertheless usable as a development feature through the existing Blueprint
image node and llama.cpp/libmtmd when a project supplies a matching projector
and custom manifest. The default Gemma manifest disables vision and native
model audio so the easy Starter path carries no projector memory or download
cost. MTP remains deferred until speculative decoding is implemented by the
plug-in runtime; merely discovering that file provides no current acceleration.
See [Development Vision](VisionDevelopment.md).

## Release modules

| Archive/module | Approximate size | Contents |
| --- | ---: | --- |
| Plug-in Core | 80.5 MiB | Plug-in, CPU/Vulkan llama.cpp and native speech runtimes |
| CUDA 12 accelerator | 922.1 MiB | `ggml-cuda`, CUDA runtime and cuBLAS DLLs |
| Starter Model Pack | about 3.25 GiB | Gemma text + Parakeet + Pocket; voices already ship in Plug-in Core |
| Starter Core | about 3.33 GiB | Plug-in Core + Starter Model Pack |
| Starter NVIDIA | about 4.23 GiB | Starter Core + CUDA accelerator |

The CUDA figure is not a typo. The four measured files total 966,875,648 bytes
(922.1 MiB): `cublasLt64_12.dll` 660.4 MiB, `ggml-cuda.dll` 152.7 MiB,
`cublas64_12.dll` 108.4 MiB, and `cudart64_12.dll` 0.5 MiB.

## Bundle layout

```text
<Project>/
  Plugins/
    LocalMultimodalLLM/
  Models/
    Gemma4E2B/
      gemma-4-e2b-it-qat.localllm.json
      gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf
    sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/
      encoder.int8.onnx
      decoder.int8.onnx
      joiner.int8.onnx
      tokens.txt
    PocketTTS/
      sherpa-onnx-pocket-tts-int8-2026-01-26/
  ModelLicenses/
    Gemma4/
    Parakeet/
    PocketTTS/
```

The default INI and Gemma manifest already target these project-relative paths.
The bundle must not include upstream `test_wavs`, EARS/Expresso recordings,
benchmark output, Python environments, or unrelated model variants.

The plugin build whitelists the exact default-stack files as NonUFS runtime
dependencies, so packaging a project with a Starter download carries them into
the packaged build without placing multi-gigabyte weights in Unreal Content.
Run `Plugins/LocalMultimodalLLM/Scripts/ValidateStarterBundle.ps1` against the
assembled root before publishing. Additional models remain opt-in and need an
explicit project packaging rule.

## Why weights are not in Git history

The text-and-speech Starter Model Pack is approximately 3.25 GiB. The main
Gemma GGUF alone is 2.50 GiB, above GitHub Free's 2 GB per-file Git LFS limit.
Storing frequently downloaded release binaries in Git history would also spend
the repository owner's LFS bandwidth and make every source clone unnecessarily
large.

Publish the modules as versioned multipart ZIP64 release assets, split so every
individual asset remains below the host's file limit. The supplied
`Reassemble-And-Extract.ps1` and package-specific `Extract-*.cmd` wrappers verify
the reconstructed archive before extraction. Starter Core and Starter NVIDIA
extract into the layout above; users do not need to edit paths or manifests.
Keep the source/plugin-only download for contributors and developers who already
manage their own weights.

## Redistribution checklist

Commercial use does not remove attribution and notice obligations.

- **Gemma 4:** the official Gemma 4 checkpoints and the tested Unsloth GGUF
  repository identify Apache 2.0. Include the Apache 2.0 license, preserve
  notices, identify Google DeepMind as the model author, identify the quantized
  source and revision, and mark any local modifications.
- **Pocket TTS:** include CC BY 4.0, credit Kyutai and the ONNX exporter, identify
  the exact bundle revision, preserve its model-use conditions, and include only
  voice references with separate commercial rights. Preserve the relicensing
  evidence recorded in the model notice. The four plug-in starter voices are
  CC0 Voice-Zero recordings.
- **Parakeet:** include the NVIDIA Open Model License Agreement and a Notice file
  containing `Licensed by NVIDIA Corporation under the NVIDIA Open Model
  License`. Record the NVIDIA base model and sherpa-onnx export source/revision.
- Include hashes for every distributed weight and rerun native model, STT, and
  TTS smoke tests against the exact staged files.

The bundle is not release-complete until `ModelLicenses` and `SHA256SUMS.txt`
exist and the Starter-package validator passes. The local development copies of
upstream test recordings are intentionally ignored rather than copied.

Licenses can change between model versions. Recheck the pinned upstream revision
before each release rather than treating a family name as a blanket approval.
