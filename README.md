# Local Multimodal LLM for Unreal Engine

This repository contains the distributable `LocalMultimodalLLM` Unreal Engine
plugin: native Win64 inference runtimes, C++ source, Blueprint-facing APIs,
configuration examples, starter voices, documentation, and validation scripts.

The host development project, demo map and MetaHumans, model weights, benchmark
data, recordings, and generated Unreal build output are intentionally excluded.

Start with the [plugin README](Plugins/LocalMultimodalLLM/README.md) for
installation, model setup, licensing notes, and the Blueprint quick start.

## Repository checkout

[Git LFS](https://git-lfs.com/) is required because the complete Win64 runtime
includes CUDA redistributables larger than GitHub's standard file limit:

```powershell
git lfs install
git clone https://github.com/isundahl/LLM_Plugin.git
```

Model weights are not stored in this repository. Use the documented model
configuration workflow to install compatible models separately.

No license for the plugin's original source has been selected yet. Third-party
components retain their respective licenses; see
[THIRD_PARTY_NOTICES.md](Plugins/LocalMultimodalLLM/THIRD_PARTY_NOTICES.md).
