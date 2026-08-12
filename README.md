# Local Multimodal LLM for Unreal Engine

This repository contains the distributable `LocalMultimodalLLM` Unreal Engine
plugin: native Win64 inference runtimes, C++ source, Blueprint-facing APIs,
configuration examples, starter voices, documentation, and validation scripts.

The host development project, demo map and MetaHumans, benchmark data,
recordings, and generated Unreal build output are intentionally excluded.
Large starter weights are distributed with the Full Starter Bundle rather than
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

The source checkout does not contain model weights because the tested starter
stack is larger than 4 GB and its main Gemma file exceeds GitHub Free's
per-file LFS limit. This is not an end-user setup requirement: the Full Starter
Bundle is designed to include the preconfigured Gemma 4 E2B, Parakeet, and
Pocket TTS assets and their license notices. See
[Starter Models](Plugins/LocalMultimodalLLM/Docs/StarterModels.md).

No license for the plugin's original source has been selected yet. Third-party
components retain their respective licenses; see
[THIRD_PARTY_NOTICES.md](Plugins/LocalMultimodalLLM/THIRD_PARTY_NOTICES.md).
