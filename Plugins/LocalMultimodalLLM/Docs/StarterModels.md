# Starter models and release layout

The normal end-user download is the **Full Starter Bundle**, not a bare Git
checkout. It is designed to work without asking a developer to find, convert,
rename, or configure model weights.

## Default stack

| Role | Tested asset | Approximate size | Governing terms |
| --- | --- | ---: | --- |
| LLM | Gemma 4 E2B IT QAT, Unsloth `UD-Q4_K_XL` GGUF | 2.50 GB | Apache 2.0 |
| Optional multimodal projector | Gemma 4 E2B F16 `mmproj` | 940 MB | Apache 2.0 |
| Optional MTP assistant | Gemma 4 E2B MTP GGUF | 56 MB | Apache 2.0 |
| Speech to text | Parakeet Unified English 0.6B INT8 sherpa-onnx export | 633 MB | NVIDIA Open Model License |
| Text to speech | Pocket TTS INT8 sherpa-onnx export | 194 MB | CC BY 4.0 |
| Starter voices | Caro Davy and Bill Boerst Pocket references | less than 1 MB | CC0 / Voice-Zero |

The current tested default is **Gemma 4 E2B**, not E4B. E4B can be offered as
an optional higher-capability profile after it receives the same native and
roleplay benchmark coverage.

Gemma's projector remains lazy and optional at runtime even when its file is
present. Text-only games do not pay its VRAM cost. The MTP file is also optional
until speculative decoding is enabled by a compatible runtime.

## Bundle layout

```text
<Project>/
  Plugins/
    LocalMultimodalLLM/
  Models/
    Gemma4E2B/
      gemma-4-e2b-it-qat.localllm.json
      gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf
      gemma-4-E2B-mmproj-F16.gguf
      mtp-gemma-4-E2B-it.gguf
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

## Why weights are not in Git history

The three tested model directories total approximately 4.32 GB. The main Gemma
GGUF is approximately 2.50 GB, above GitHub Free's 2 GB per-file Git LFS limit.
Storing frequently downloaded release binaries in Git history would also spend
the repository owner's LFS bandwidth and make every source clone unnecessarily
large.

Publish the Full Starter Bundle as versioned release assets, split so every
individual asset remains below the host's file limit. Extraction produces the
layout above; users should not need to edit paths or manifests. Keep a smaller
source/plugin download for contributors and developers who already manage their
own weights.

## Redistribution checklist

Commercial use does not remove attribution and notice obligations.

- **Gemma 4:** the official Gemma 4 checkpoints and the tested Unsloth GGUF
  repository identify Apache 2.0. Include the Apache 2.0 license, preserve
  notices, identify Google DeepMind as the model author, identify the quantized
  source and revision, and mark any local modifications.
- **Pocket TTS:** include CC BY 4.0, credit Kyutai and the ONNX exporter, identify
  the exact bundle revision, preserve its model-use conditions, and include only
  voice references with separate commercial rights. The two plug-in starter
  voices are CC0 Voice-Zero recordings.
- **Parakeet:** include the NVIDIA Open Model License Agreement and a Notice file
  containing `Licensed by NVIDIA Corporation under the NVIDIA Open Model
  License`. Record the NVIDIA base model and sherpa-onnx export source/revision.
- Include hashes for every distributed weight and rerun native model, STT, and
  TTS smoke tests against the exact staged files.

Licenses can change between model versions. Recheck the pinned upstream revision
before each release rather than treating a family name as a blanket approval.
