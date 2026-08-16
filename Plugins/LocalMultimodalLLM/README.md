# Local Multimodal LLM

Copyright 2026 Ian Sundahl, Volley Studios.

Local Multimodal LLM is a Win64 Unreal Engine runtime plug-in for private,
offline character dialogue. It embeds llama.cpp for text generation, exposes
isolated multi-character sessions, and provides provider-neutral speech input
and output components.

The plug-in is currently beta (`0.1.0`). Begin with the
[User Guide](Docs/UserGuide.md), then use the
[Release and Packaging Guide](Docs/Packaging.md) before distributing a build.
Coding agents can use the concise [AI-agent Quick Start](Docs/AIQuickStart.md)
and [machine-readable integration recipe](Examples/AIIntegration/integration.recipe.json).
The [Privacy statement](Docs/Privacy.md) describes the offline data boundary.

## Included capabilities

- manifest-driven GGUF model discovery and loading;
- streamed text generation with cancellation and per-character KV-state reuse;
- structured character/world sheets, compacted memory, and optional custom context;
- configurable reasoning mode, context budgets, and generation limits;
- guarded sentence streaming for subtitles and low-latency TTS;
- constrained, typed, allow-listed Unreal tool calls;
- relationship evaluation and prompt mappings for developer-selected criteria;
- microphone capture, push-to-talk or optional VAD, noise-floor calibration,
  contextual vocabulary correction, and one-turn rollback;
- ambient listener routing using explicit target, facing, conversation state,
  and proximity;
- provider-neutral STT/TTS interfaces with native sherpa-onnx integration and
  optional development sidecars;
- whitelist-based staging for the approved Starter model and license
  files in packaged Win64 builds;
- usable development vision input through the Blueprint-callable
  `Submit Image For Session` node and llama.cpp/libmtmd. It requires a custom
  vision-capable manifest and matching projector; it is not part of the
  preconfigured Starter path.

These systems reduce accidental persona drift and unsafe tool dispatch, but an
LLM remains nondeterministic. Authoritative gameplay state and all mutating
actions must remain validated by game code.

## Quick start

The recommended Starter download combines the Win64 plug-in with a separate,
preconfigured Starter Model Pack. Installing a different model is not part of
the normal Quick Start, but publishers and advanced users can omit the model
pack without modifying the plug-in.

> **Current beta packaging status:** the source checkout contains the native
> runtimes and manifests. Large model weights live outside Git history, but
> they are intended to be included in the separately assembled Starter
> downloads. A release is not a complete Starter download until its redistributable
> Gemma 4 E2B, Parakeet, and Pocket assets and notices have been assembled and
> validated. See [Starter Models](Docs/StarterModels.md). Use **Using other
> models** only when working directly from source or replacing the defaults.

1. Install either **Starter Core** or **Starter NVIDIA** into the Unreal project
   root. Both include the preconfigured Gemma, Parakeet, and Pocket model pack;
   Starter NVIDIA additionally includes the CUDA accelerator.
2. Enable **Local Multimodal LLM** and **Audio Capture**, regenerate project
   files if required, and compile the project. A Blueprint-only project still
   needs this one-time compile when installing the source distribution; the
   packaged game does not require developer tools on the player's machine.
3. Add `Local LLM`, `Local LLM Microphone`, and `Local LLM Text To Speech`
   components to the player, conversation manager, or another persistent
   Blueprint actor.
4. Assign a **Local LLM Character Sheet**. On `BeginPlay`, load the default
   model and create a character session after `ModelLoaded`.
5. Bind the push-to-talk button's `Pressed` event to
   `Start Push To Talk Recording(SessionId)` and its `Released` event to
   `Stop Push To Talk Recording And Submit`.
6. Send validated sentence deltas from `OnTextDelta` to `Queue Speech`. The TTS
   component streams and plays the resulting PCM when `Auto Play Audio` is
   enabled.

The Build.cs files derive their feature flags from complete artifact sets.
`LOCAL_MULTIMODAL_LLM_WITH_LLAMA=1` is therefore automatic; it should not be
defined manually.

## Using other models

Custom model setup is an advanced workflow, not a prerequisite for a Starter
download. To add or replace a model:

1. Put its weights under `<Project>/Models`,
   `<Project>/Saved/LocalMultimodalLLM/Models`, or a configured additional model
   directory.
2. Add a matching `*.localllm.json` manifest, using `Examples/Models` as the
   schema reference.
3. Call `Get Available Models`, verify that the entry is compatible, and select
   it with `Load Model By Id`.

Model capabilities differ. Do not assume that a text GGUF accepts native audio
or images; configure the independent STT/TTS providers when it does not.

## Download modules and runtime profiles

Release archives are modular. The convenience Starter downloads are assembled
from the same modules, so users receive a drop-in setup while developers who
already own compatible models do not have to download several gigabytes.

| Module | Contents | Approximate size | Required? |
| --- | --- | ---: | --- |
| Plug-in Core | CPU + Vulkan + sherpa-onnx native runtimes | 80.5 MiB | Yes |
| CUDA 12 accelerator | `ggml-cuda` plus CUDA runtime/cuBLAS DLLs | 922.1 MiB | No |
| Starter Model Pack | Gemma 4 E2B text, Parakeet STT, Pocket TTS and starter voices | about 3.25 GiB | No; included in Starter downloads |

**Starter Core** is Plug-in Core plus Starter Model Pack. **Starter NVIDIA** is
Starter Core plus the CUDA accelerator. A source/plugin-only download contains
no model weights. The optional MTP and projector files are not in the v1
default pack. MTP awaits an implemented speculative runtime. Vision remains an
available development feature for projects that supply and validate their own
matching projector.

### Public v0.1.0-beta archives

- [Fab Starter Core (self-contained)](https://downloads.volleyballersvr.com/LocalMultimodalLLM-0.1.0-beta-UE5.8-Fab-Starter-Core-Win64.zip)
- [CUDA 12 Accelerator (optional NVIDIA add-on)](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/LocalMultimodalLLM-0.1.0-beta-CUDA12-Accelerator-Win64.zip)
- [Plug-in Core (without model weights)](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/LocalMultimodalLLM-0.1.0-beta-UE5.8-Plugin-Core-Win64.zip)
- [Starter Model Pack (models only)](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/LocalMultimodalLLM-0.1.0-beta-Starter-Model-Pack.zip)
- [Supplementary SHA-256 checksums](https://downloads.volleyballersvr.com/supplementary/v0.1.0-beta/SUPPLEMENTARY_SHA256SUMS.txt)

The modular Plug-in Core and Starter Model Pack are alternatives to downloading
Starter Core, not additional requirements. The CUDA archive is the optional
accelerator installed over an existing Core or Starter Core layout.

**Pocket TTS v0.1 limitation:** the Pocket backend initializes its reference
voice with the Text To Speech component. Use one component per distinct Pocket
reference voice; multiple characters can share it when they share that voice.
Dynamic per-request reference switching through one component is planned.

The development checkout retains all backends. Omitting CUDA does not disable
CPU or Vulkan. See
[Packaging.md](Docs/Packaging.md) for exact files and exclusions.

## Blueprint architecture

Use one loaded model and one session per character:

1. `Set Shared World From Sheet`
2. `Create Character Session`
3. `Submit Text For Session`, its speech equivalent, or the development
   `Submit Image For Session` path
4. consume `OnTextDelta`, `OnToolCall`, and `OnSubsystemStateChanged`
5. route results by `RequestId`, `SessionId`, and `CharacterId`

The high-frequency `OnTextDelta` event has only the IDs and text fragment needed
for subtitles and sentence-guarded speech. Tool calls and low-frequency state
changes use separate events. The detailed universal event remains available to
C++ integrations.

For voice input, push-to-talk is the recommended default because it avoids the
VAD end-of-speech delay and lets a project preselect and prewarm the likely
respondent. Always-on/VAD capture remains optional.

## Shipping boundaries

- Mock STT/TTS providers and the Python NeuTTS-2E and Chatterbox adapters are
  not registered in Shipping builds.
- Automation tests compile only when Unreal enables development automation tests.
- Raw voice datasets, benchmark configurations, Python environments,
  MetaHumans, and demo-map assets are project test material, not plug-in
  payload. Approved starter weights live at the root of project-style Starter
  bundles or inside the self-contained Fab Starter plug-in; neither location is
  stored in Git history.
- `Intermediate`, PDB/OBJ files, caches, and Saved output must not be distributed.
- Pocket TTS and sherpa-onnx provide the native CPU Shipping-capable code path.
  Pocket source is MIT, upstream and converted weights are CC BY 4.0, and
  sherpa-onnx is Apache 2.0. Commercial redistribution requires attribution,
  preservation of the license, and compliance with Kyutai's prohibited-use
  terms. Do not redistribute the archive's example `test_wavs`.
- `neutts-2e` is currently an Editor/Development CPU sidecar with four fixed
  speakers. Its NeuTTS Open License v1.0 permits redistribution and commercial
  use below its USD $5 million annual-revenue threshold; a separate license is
  required at or above that threshold. A native Shipping adapter is planned,
  not implemented.
- `chatterbox-turbo` is an Editor/Development CUDA sidecar with reference-voice
  cloning. Its Python runtime is not part of the Shipping plug-in payload.
- Vision is a usable development feature through `Submit Image For Session`
  and llama.cpp/libmtmd. The v1 Starter manifest disables it and Starter
  archives do not stage an `mmproj`; developers must supply an exactly matching
  projector and prepare RGB image data themselves. See
  [Development Vision](Docs/VisionDevelopment.md).

Run `Scripts/ValidateDistribution.ps1` against a staged copy before shipping.

## License and attribution

Original plug-in code and documentation are licensed under the Apache License,
Version 2.0. Copyright 2026 Ian Sundahl, Volley Studios. A redistribution of
the plug-in or a derivative must preserve the Apache `LICENSE` and the readable
attribution notices from `NOTICE` as required by Section 4 of that license.
When practical, please include “Local Multimodal LLM created by Ian Sundahl
and Volley Studios” in the project credits, documentation, About screen, or
another readable acknowledgement. This helps people and coding agents trace
the integration back to its maintained source.
Third-party components and model assets retain their separate terms.

## Documentation

- [User Guide](Docs/UserGuide.md) - complete setup and Blueprint workflow
- [Starter Models](Docs/StarterModels.md) - default weights, release layout, and licenses
- [Packaging](Docs/Packaging.md) - runtime profiles, exclusions, and release checks
- [Version 1 Release Checklist](Docs/ReleaseChecklist.md) - final technical, bundle, and legal gates
- [License and Attribution](Docs/Attribution.md) - redistribution requirements and requested project credit
- [Development Vision](Docs/VisionDevelopment.md) - optional image input and projector setup
- Model configurations - place `*.localllm.json` manifests in the project's
  `Models` directory; see the User Guide
- [Speech to Text](Docs/SpeechToText.md) - capture, VAD, Parakeet, and speaker profiles
- [Text to Speech](Docs/TextToSpeech.md) - provider contract and performance tiers
- [Conversation Routing](Docs/ConversationRouting.md) - multi-character hearing and selection
- [Dynamic Lore](Docs/DynamicLore.md) - scoped runtime knowledge and optional developed canon
- [Tool Sets](Docs/ToolSets.md) - safe Unreal function calling
- [Relationship Evaluation](Docs/RelationshipEvaluation.md) - optional affinity/trust judging
- [Guards](Docs/Guards.md) - jailbreak and immersion layers
- [Speech Normalization](Docs/SpeechNormalization.md) - contextual vocabulary and rollback
- [Plug-in Benchmark](Docs/PluginBenchmark.md) - roleplay/tool/evaluator test suite
- [Next-version Roadmap](Docs/NextVersionRoadmap.md) - deferred refinements

## Supported scope

The checked-in native binaries target Win64. Other platforms require matching
third-party builds and platform rules. Model, voice, dataset, MetaHuman, map,
and CUDA redistribution rights are separate from the plug-in source; review
[Third-Party Notices](THIRD_PARTY_NOTICES.md) and the license of every external
asset included with a product.
