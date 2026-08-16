# Local Multimodal LLM for Unreal Engine

Copyright 2026 Ian Sundahl, Volley Studios.

This repository contains the distributable `LocalMultimodalLLM` Unreal Engine
plugin: native Win64 inference runtimes, C++ source, Blueprint-facing APIs,
configuration examples, starter voices, documentation, and validation scripts.

The host development project, demo map and MetaHumans, benchmark data,
recordings, and generated Unreal build output are intentionally excluded.
Large starter weights are distributed with the Starter downloads rather than
stored in Git history.

Start with the [plugin README](Plugins/LocalMultimodalLLM/README.md) for
installation, model setup, licensing notes, and the Blueprint quick start.

## v0.1.0-beta downloads

**Starter Core** is the normal self-contained UE 5.8 Win64 package. It includes
the plug-in, CPU/Vulkan runtimes, Gemma 4 E2B, Parakeet STT, Pocket TTS, four
starter voice references, and the applicable license notices.

- [Fab Starter Core (self-contained)](https://downloads.volleyballersvr.com/LocalMultimodalLLM-0.1.0-beta-UE5.8-Fab-Starter-Core-Win64.zip)
- [CUDA 12 Accelerator (optional NVIDIA add-on)](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/LocalMultimodalLLM-0.1.0-beta-CUDA12-Accelerator-Win64.zip)
- [Plug-in Core (alternative download without model weights)](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/LocalMultimodalLLM-0.1.0-beta-UE5.8-Plugin-Core-Win64.zip)
- [Starter Model Pack (alternative model-only download)](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/LocalMultimodalLLM-0.1.0-beta-Starter-Model-Pack.zip)
- [Supplementary SHA-256 checksums](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/SUPPLEMENTARY_SHA256SUMS.txt)

The Plug-in Core and Starter Model Pack are modular alternatives, not extra
requirements for Starter Core users. Install the CUDA archive over an existing
Starter Core installation only when NVIDIA acceleration is desired.

**Pocket TTS v0.1 limitation:** a Pocket reference voice is initialized per
`LocalLLMTextToSpeechComponent`. Use one component per distinct Pocket voice
reference; characters may share a component when they share a voice. Dynamic
reference-voice switching through one component is planned for a future release.

## Repository checkout

[Git LFS](https://git-lfs.com/) is required because the complete Win64 runtime
includes CUDA redistributables larger than GitHub's standard file limit:

```powershell
git lfs install
git clone https://github.com/isundahl/LLM_Plugin.git
```

The source checkout does not contain model weights because the text-and-speech
starter stack is roughly 3.25 GiB and its main Gemma file exceeds GitHub Free's
per-file LFS limit. This is not an end-user setup requirement: Starter Core and
Starter NVIDIA are designed to include preconfigured Gemma 4 E2B, Parakeet,
and Pocket TTS assets and their license notices. See
[Starter Models](Plugins/LocalMultimodalLLM/Docs/StarterModels.md).

The original plug-in code and documentation are licensed under Apache License 2.0.
Redistributions must satisfy its `LICENSE` and `NOTICE` preservation terms.
Third-party components retain their respective licenses; see
[THIRD_PARTY_NOTICES.md](Plugins/LocalMultimodalLLM/THIRD_PARTY_NOTICES.md).
The practical redistribution checklist is in
[License and Attribution](Plugins/LocalMultimodalLLM/Docs/Attribution.md).

When practical, please include this acknowledgement in projects built with the
plug-in: **Local Multimodal LLM created by Ian Sundahl and Volley Studios.**

Coding agents and automated integration tools should begin with
[`AGENTS.md`](AGENTS.md) and [`llms.txt`](llms.txt).
