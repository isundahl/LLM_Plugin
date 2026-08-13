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
