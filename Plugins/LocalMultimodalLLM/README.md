# Local Multimodal LLM

Local Multimodal LLM is a Win64 Unreal Engine runtime plug-in for private,
offline character dialogue. It embeds llama.cpp for text generation, exposes
isolated multi-character sessions, and provides provider-neutral speech input
and output components.

The plug-in is currently beta (`0.1.0`). Begin with the
[User Guide](Docs/UserGuide.md), then use the
[Release and Packaging Guide](Docs/Packaging.md) before distributing a build.

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
- optional image/audio projector input through libmtmd.

These systems reduce accidental persona drift and unsafe tool dispatch, but an
LLM remains nondeterministic. Authoritative gameplay state and all mutating
actions must remain validated by game code.

## Quick start

The complete Starter Bundle is intended to contain the Win64 plug-in, its
pinned native runtimes, and preconfigured default LLM, STT, and TTS assets.
Installing a different model is not part of the normal Quick Start.

> **Current beta packaging status:** the source checkout contains the native
> runtimes, manifests, and development model assets, but the distribution
> validator still excludes model weights. A release archive is not a complete
> Starter Bundle until its redistributable starter weights and notices have
> been assembled and validated. See **Using other models** below when working
> directly from this source checkout.

1. Install the complete Starter Bundle into the Unreal project root. This adds
   `Plugins/LocalMultimodalLLM` and its preconfigured starter-model payload.
2. Enable **Local Multimodal LLM** and **Audio Capture**, regenerate project
   files if required, and compile the project.
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

Custom model setup is an advanced workflow, not a prerequisite for the complete
Starter Bundle. To add or replace a model:

1. Put its weights under `<Project>/Models`,
   `<Project>/Saved/LocalMultimodalLLM/Models`, or a configured additional model
   directory.
2. Add a matching `*.localllm.json` manifest, using `Examples/Models` as the
   schema reference.
3. Call `Get Available Models`, verify that the entry is compatible, and select
   it with `Load Model By Id`.

Model capabilities differ. Do not assume that a text GGUF accepts native audio
or images; configure the independent STT/TTS providers when it does not.

## Runtime profiles

| Profile | Backends | Approximate third-party size | Intended use |
| --- | --- | ---: | --- |
| Full | CPU + Vulkan + CUDA 12 + sherpa-onnx | about 1.0 GB | Standard complete Win64 plug-in release |
| Core | CPU + Vulkan + sherpa-onnx | about 80 MB | Optional slim non-CUDA release |

The development checkout and standard release keep the Full profile so CUDA,
Vulkan, and CPU paths are all available without user setup. A publisher may
deliberately create a separately labeled Core package to reduce download size;
omitting CUDA does not disable CPU or Vulkan. See
[Packaging.md](Docs/Packaging.md) for exact files and exclusions.

## Blueprint architecture

Use one loaded model and one session per character:

1. `Set Shared World From Sheet`
2. `Create Character Session`
3. `Submit Text For Session` or the speech/image equivalent
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
- Raw voice datasets, benchmark configurations, Python environments, model
  weights, MetaHumans, and demo-map assets are project test material, not plug-in
  payload.
- `Intermediate`, PDB/OBJ files, caches, and Saved output must not be distributed.
- Pocket TTS and sherpa-onnx provide the native CPU Shipping-capable code path.
  Pocket source is MIT, its upstream weights and the tested ONNX bundle carry
  CC BY 4.0, and sherpa-onnx is Apache 2.0. Commercial use is permitted subject
  to attribution, license, model-use-policy, and voice-reference rights. Do not
  redistribute the ONNX archive's example `test_wavs`; use a properly licensed
  and consented project voice instead.
- `neutts-2e` is currently an Editor/Development CPU sidecar with four fixed
  speakers. Its NeuTTS Open License v1.0 permits redistribution and commercial
  use below its USD $5 million annual-revenue threshold; a separate license is
  required at or above that threshold. A native Shipping adapter is planned,
  not implemented.
- `chatterbox-turbo` is an Editor/Development CUDA sidecar with reference-voice
  cloning. Its Python runtime is not part of the Shipping plug-in payload.

Run `Scripts/ValidateDistribution.ps1` against a staged copy before shipping.

## Documentation

- [User Guide](Docs/UserGuide.md) - complete setup and Blueprint workflow
- [Packaging](Docs/Packaging.md) - runtime profiles, exclusions, and release checks
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
