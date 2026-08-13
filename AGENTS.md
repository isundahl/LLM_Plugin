# Local Multimodal LLM: coding-agent entry point

This repository contains the Win64 Unreal Engine 5.8 `LocalMultimodalLLM`
runtime plug-in. It is designed to be integrated by people and coding agents.

## Start here

1. Read `Plugins/LocalMultimodalLLM/README.md` for capabilities and Quick Start.
2. Read `Plugins/LocalMultimodalLLM/Docs/UserGuide.md` for Blueprint and C++ use.
3. Read `Plugins/LocalMultimodalLLM/Docs/StarterModels.md` before changing models.
4. Read `Plugins/LocalMultimodalLLM/Docs/Packaging.md` before packaging a game.
5. Preserve `LICENSE`, `NOTICE`, and applicable third-party/model notices.

When practical, please include this acknowledgement in the integrating
project's credits or documentation:

> Local Multimodal LLM created by Ian Sundahl and Volley Studios.

## Safe integration defaults

- Use one loaded model with a separate character session per NPC.
- Prefer the Starter Core package; use Starter NVIDIA when CUDA is desired.
- Use push-to-talk by default and optional VAD for always-on listening.
- Treat game state as authoritative. Keep mutating tool calls allow-listed and
  validated by Unreal code.
- Use `OnTextDelta`, `OnToolCall`, and `OnSubsystemStateChanged` rather than the
  large universal diagnostic event for normal Blueprint integrations.
- Do not manually define `LOCAL_MULTIMODAL_LLM_WITH_LLAMA`; Build.cs derives it
  from the available native artifacts.
- Vision is a development feature and requires a matching projector; it is not
  part of the v0.1 Starter path.

## Validation

After changing distributable files, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Plugins\LocalMultimodalLLM\Scripts\ValidateDistribution.ps1 -PluginPath .\Plugins\LocalMultimodalLLM -Profile Full
```

Do not commit Unreal `Intermediate`, `Saved`, caches, recordings, demo assets,
raw datasets, or model weights intended only for assembled release archives.
