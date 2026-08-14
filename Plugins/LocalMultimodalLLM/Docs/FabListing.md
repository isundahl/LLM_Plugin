# Fab listing worksheet

This is copy-ready publisher material for Local Multimodal LLM v0.1.0. Replace
only the fields marked `TODO` after the publisher account is approved.

## Product identity

**Title:** Local Multimodal LLM

**Publisher:** Volley Studios

**Category:** Tools & Plugins

**Distribution method:** Unreal Engine Code Plugin

**Engine version:** Unreal Engine 5.8

**Platforms:** Win64 development and Win64 packaged games

**Recommended launch license and price:** Standard license, Free

**Created with AI:** Yes

**Mature content:** No

**NoAI:** Off. This project is intentionally documented for discovery and use
by coding agents. The licenses embedded in the product remain authoritative.

## Short summary

Build private offline NPC conversations in Unreal Engine with local GGUF
inference, speech input/output, per-character memory, safe actions, and a
ready-to-run starter model stack.

## Description

Local Multimodal LLM is a native Win64 Unreal Engine code plug-in for private,
offline AI characters. It provides the runtime pieces needed to turn player
speech or text into locally generated, voiced NPC dialogue without requiring a
cloud inference service.

The Fab Starter Core download is self-contained and includes a preconfigured
Gemma 4 E2B text model, Parakeet speech recognition, Pocket TTS, four starter
voice references, CPU and Vulkan inference backends, matching native DLLs, and
all applicable model/runtime notices.

### Core features

- Manifest-driven GGUF discovery and local llama.cpp inference
- One loaded model with isolated sessions and memory for multiple characters
- Streamed subtitles and sentence-level speech queuing
- Native push-to-talk or optional voice-activity detection
- Native Parakeet STT and Pocket TTS through sherpa-onnx
- Character sheets, grounded world context, compacted conversation memory,
  relationship criteria, and controlled dynamic lore
- Typed, allow-listed Unreal tool calls with explicit game-code validation
- Recipient routing based on explicit target, facing, active conversation, and
  proximity
- Configurable guard patterns, contextual speech vocabulary, rollback, and
  provider-neutral speech interfaces
- Blueprint-facing components plus a documented public C++ API
- Packaging validators, checksums, model notices, agent instructions, and a
  machine-readable integration recipe

### Minimal integration

Enable Local Multimodal LLM and Unreal's Audio Capture plug-in, compile the
project once, add the Local LLM, Microphone, and Text To Speech components to a
persistent actor, load the included model, and create one session per NPC.
The included AI Quick Start and full User Guide document the event flow.

### Important scope

This beta release supports Unreal Engine 5.8 on Win64. Starter Core includes
CPU and Vulkan; CUDA is a separate optional accelerator. CPU fallback works but
can be substantially slower. Vision has an experimental C++/Blueprint path but
requires a developer-supplied matching projector and is not part of the
preconfigured starter stack. MTP speculative decoding is not enabled in v0.1.

Local models remain nondeterministic. Games must keep authoritative state and
validate every mutating action in Unreal code. The MetaHuman characters,
environment, animations, facial-animation setup, Qwen, and NeuTTS shown in the
demonstration are integration examples and are not included in this product.

Original plug-in source and documentation are Apache 2.0. Bundled runtimes,
models, and voices retain the independent licenses and notices supplied in the
archive.

## Technical information

- Product type: Unreal Engine C++ Code Plugin
- Version: 0.1.0 beta
- Engine compatibility: 5.8
- Supported development platform: Win64
- Supported target platform: Win64
- Network required at runtime: No
- Python required at runtime: No
- Blueprint-only projects: one source compile is required after installation
- Built-in dependency: Unreal Engine Audio Capture plug-in
- Included text model: Gemma 4 E2B IT QAT, Q4 GGUF
- Included STT: Parakeet Unified English 0.6B INT8
- Included TTS: Pocket TTS INT8 with four CC0 starter references
- Included acceleration: Vulkan and CPU variants
- Optional acceleration: CUDA 12 module, distributed separately
- Archive size: approximately 3.32 GiB
- Source code included: Yes
- Demo map/assets included: No
- Documentation included: Yes

## Suggested tags

Select only tags that exist in Fab's picker:

- Blueprint
- Scripts
- Procedural
- MetaSounds, only if Fab treats the exposed audio-bus integration as eligible

Search phrases are already present naturally in the title and description:
local LLM, offline AI, NPC dialogue, AI characters, speech to text, text to
speech, function calling, character memory, and Unreal Engine.

## Public links

- Documentation: https://github.com/isundahl/LLM_Plugin/blob/main/Plugins/LocalMultimodalLLM/Docs/UserGuide.md
- AI-agent Quick Start: https://github.com/isundahl/LLM_Plugin/blob/main/Plugins/LocalMultimodalLLM/Docs/AIQuickStart.md
- Source: https://github.com/isundahl/LLM_Plugin
- Support: https://github.com/isundahl/LLM_Plugin/issues
- Privacy: https://github.com/isundahl/LLM_Plugin/blob/main/Plugins/LocalMultimodalLLM/Docs/Privacy.md
- Demonstration video: TODO: public YouTube URL
- Project File Link: TODO: direct no-login ZIP URL

## Version notes for reviewers

Initial beta release for Unreal Engine 5.8 Win64. The supplied project-file ZIP
contains exactly one self-contained code plug-in folder. It includes source,
Config, Content, Resources, native CPU/Vulkan and speech runtimes, the approved
Gemma/Parakeet/Pocket starter weights, four starter voices, checksums, and all
license/NOTICE material. It contains no password, CUDA payload, Python runtime,
test recordings, MetaHumans, demo environment, Intermediate directory, PDBs,
or project-specific assets.

Expected SHA-256 for the submitted ZIP must match its adjacent `.sha256` file.
The direct download must work without a host login or access request.

## Media checklist

Fab requires a thumbnail and at least one gallery image. Gallery images must be
at least 1920x1080, JPEG or PNG, and less than 3 MB each.

Recommended order:

1. Product overview/title card
2. Live Taro conversation with relationship UI visible
3. Live Ada conversation with subtitles visible
4. A second character/session example
5. Optional architecture or Blueprint screenshot
6. Public demonstration video

Add this disclaimer to an image caption or the description:

> Demonstration environment, MetaHumans, animations, facial-animation setup,
> and optional model/provider stacks are not included.

