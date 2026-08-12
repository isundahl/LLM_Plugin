# Post-v1 expansion and refinement roadmap

This roadmap deliberately prioritizes reliability and a small core over accumulating providers and features. Complete the release gate before adding broad new capability.

## Release gate: prove v1 outside the development project

### 1. Clean-project and packaged-build validation

- Install the plugin into a blank UE 5.8 C++ project.
- Test Editor and packaged Development/Shipping Win64 builds.
- Verify CPU fallback, NVIDIA CUDA, and Vulkan backend discovery on representative machines.
- Verify DLL staging without relying on developer PATH entries.
- Test model/projector/STT paths outside the source workspace.
- Add third-party notices, model-license instructions, and a versioned compatibility matrix.

This is the highest priority because a successful development-editor build does not prove a distributable plugin.

### 2. Minimal sample content

- One coordinator Blueprint.
- Two contrasting character-sheet assets.
- One world sheet.
- One safe movement/gesture tool example.
- One text-input/subtitle widget.
- A diagnostic panel showing model, backend, session, request, tokens, latency, violations, and errors.

Keep the sample independent of any particular character mesh or game genre.

### 3. Public API stabilization

- Freeze v1 names and defaults.
- Add migration handling for renamed guard fields and other serialized assets.
- Audit every Blueprint node for error behavior and invalid IDs.
- Add concise API comments and release notes.
- Keep test-only mock speech providers and the Python NeuTTS-2E and Chatterbox
  adapters unavailable in Shipping builds; revisit only if a separately
  packaged, licensed development/runtime module is needed.

## Recommended v1.1 work

### Ambient multi-character conversation routing refinements

The current baseline already freezes at most one primary from explicit target,
facing, conversation state, and proximity; emits scored
`OnParticipantHeardDialogue` alerts to every audible registered character; and
classifies non-primary listeners without triggering inference. Remaining work:

- Add an observation history role that records overheard dialogue without inference or relationship evidence.
- Score explicit names/aliases, conversational focus, facing, proximity, perceived loudness, and project wake priority.
- Scale hearing radius from calibrated loudness or an explicit whisper/normal/loud/shout override.
- Normalize ASR names against registered participants before routing or history; preserve raw and canonical transcripts separately.
- Strip a resolved vocative name from direct stored dialogue and attach structured addressee metadata to passive observations.
- Keep the current greedy Parakeet path as default. Optionally expose provider-neutral transcription hints and benchmark a separate modified-beam hotword configuration; ship it only if measured gains outweigh latency and false-bias regressions.
- Propagate completed character replies through the same scored fan-out without response cascades.
- Preserve direct `SessionId` submission as a deterministic fallback.

The proposed Blueprint/API flow is detailed in [ConversationRouting.md](ConversationRouting.md), with ASR correction in [SpeechNormalization.md](SpeechNormalization.md).

### Production speech hardening

- Profile the implemented native `pocket-tts` sherpa-onnx provider on representative low-end CPUs.
- Prototype an optional native `audio.cpp` provider behind the existing TTS
  registry rather than replacing the proven providers immediately. Benchmark
  PocketTTS and Chatterbox parity, true time to first playable PCM, sustained
  real-time factor, voice-state reuse, cancellation, CPU/CUDA/Vulkan memory,
  packaged Win64 size, and symbol/backend coexistence with llama.cpp. Consider
  migration only if its Unreal-facing API can be pinned and its chunk delivery
  matches or improves the current sherpa-onnx Pocket path. Track NeuTTS-2E
  separately because it is not presently an audio.cpp-supported family.
- Define model/reference-voice installation and packaged-game staging policy.
- Bound and expose the existing voice-embedding cache based on measured memory.
- Harden the implemented Unreal procedural-PCM playback, interruption, and
  per-character queue path across packaged builds and audio-device changes.
- Expose timing/envelope data for facial animation without coupling the provider to a specific avatar system.
- Benchmark time to first audio, real-time factor, RAM, and CPU contention with llama.cpp.
- Evaluate a native Shipping implementation for NeuTTS-2E; keep its current
  Python sidecar limited to Editor/Development builds.

### Session persistence

- Versioned save structs for compacted memory, recent turns, relationship state, and character/world revision.
- Restore validation so stale or incompatible model/session data fails safely.
- Let projects choose whether dialogue memory is saved, regenerated, or discarded.
- Define how the one-turn rollback checkpoint interacts with save/load and executed game-side tool effects.

### Request scheduling

- Per-character queues and global priority.
- Interrupt/replace policies for player speech.
- Backpressure when several characters request inference simultaneously.
- Clear busy/queued lifecycle events for UI.

### Guard refinement

- Ship a larger adversarial test corpus without fitting prompts to individual expected sentences.
- Add configurable rule severities and rule IDs to event fields.
- Add Unicode/control-character normalization.
- Keep an optional semantic judge as a provider extension, not a core dependency.
- Measure false positives on normal roleplay, technical discussion, quoted text, and fictional AI characters.

## Recommended v1.2 work

### Embodied-character integration helpers

- Generalize the project-owned playback/MetaHuman bridge into a reusable
  optional integration package consuming TTS PCM, interruption, and tool events.
- Optional gesture intents mapped only to project allow-lists.
- Lip-sync/viseme adapter interface with no MetaHuman dependency in the core runtime.
- Example navigation and facing adapters that revalidate target, distance, permission, and pathing.

### Speech improvements

- Benchmark Parakeet variants and optional Whisper-family provider modules.
- Streaming STT where the runtime supports it, instead of repeated snapshot transcription.
- Device hot-plug recovery and clearer permission errors.
- Speaker-verification calibration tooling and ROC-style threshold reports from project samples.

### Model compatibility profiles

- Per-family chat/tool formatting adapters.
- Optional grammar-constrained tool JSON.
- Automated manifest validation against the installed llama.cpp ABI.
- Capability probes rather than trusting every manifest claim.
- Clear handling for models whose native tool format is XML or another schema.

## Larger v2 candidates

### Pluggable local, remote, and project-defined inference providers

Make the primary dialogue model optional behind the same provider boundary while
keeping character sessions, context assembly, memory, relationship guidance,
guards, tool authorization, and Unreal events owned by the plugin.

- Define an asynchronous `ILocalLLMInferenceProvider`-style interface for request
  submission, cancellation, streamed text deltas, structured tool calls, usage,
  errors, and capability discovery.
- Retain llama.cpp as the bundled local provider, but allow a project module to
  register its own native provider without modifying the plugin.
- Add an optional generic HTTP provider and an OpenAI-compatible protocol adapter;
  keep vendor-specific authentication and wire formats in separate adapters.
- Support per-project and optionally per-character provider/profile selection, so
  a developer can use a preferred hosted or self-hosted AI while preserving the
  same character-sheet and Blueprint workflow.
- Normalize provider-specific chat roles, tool schemas, finish reasons, token
  accounting, and streaming behavior into the plugin's existing events.
- Never serialize API secrets in character assets, config committed to source
  control, logs, or packaged client content. Accept credentials through a
  project-supplied secure resolver, and document that secrets in a client build
  cannot be made truly private without a developer-controlled relay service.
- Expose offline/timeout/rate-limit/fallback policies explicitly. Do not silently
  send local dialogue to a remote service or silently switch providers.
- Add conformance tests using a mock provider plus at least one local HTTP test
  server, including cancellation, malformed tool calls, partial streams,
  provider failure, and local fallback.

This is a moderate architectural change rather than a rewrite: most character
and gameplay systems can remain unchanged, but inference lifecycle code must be
separated cleanly from llama.cpp before promising a stable third-party provider
API. Target this for v2.0 after the v1 public API and packaged-build behavior are
stable.

### Speculative decoding and performance

- Implement and benchmark MTP/application-layer speculative orchestration.
- Measure actual end-to-end latency rather than decode throughput alone.
- Profile the implemented common-prefix KV reuse and RAM-backed character-session restoration with many characters; add an eviction budget if measured memory requires one.
- Dynamic context presets based on measured memory budget.

Do not enable MTP merely because a sidecar is present; require measurable wins and identical accepted behavior.

### Retrieval and authoritative knowledge

- Optional game-owned retrieval interface for lore and records too large for the character sheet.
- Separate retrieved evidence from world canon and player claims.
- Provenance IDs so generated answers can be traced during debugging.
- Never let the model write directly into authoritative retrieval storage.

### Multimodal refinement

- Blueprint-friendly texture/render-target conversion instead of raw RGB-only input.
- Projector lifetime controls and memory diagnostics.
- Benchmarked use cases for vision before making projector loading a default.
- Audio-projector comparison against the STT-first route.

### Additional platforms and multiplayer

- Linux and additional GPU backend packaging.
- Platform microphone permissions and native runtime builds.
- Explicit server/client authority design.
- Per-player audio and speaker profiles only after a real multiplayer requirement exists.

## Features to resist until justified

- Arbitrary reflected `UFunction` execution.
- Broad console-command, actor-spawn, or property-write tools.
- A mandatory second guard model on every response.
- Loading separate primary LLMs for every character.
- Bundling several overlapping STT/TTS runtimes without benchmarks.
- Automatic vision/projector loading for characters that never inspect images.
- Treating generated dialogue, relationship scores, or compacted memory as authoritative game truth.

## Suggested next-session order

1. Run the user-guide acceptance test in a real UE scene.
2. Record failures, latency, VRAM/RAM, and Blueprint friction.
3. Fix release-blocking usability and packaging issues.
4. Harden native Pocket playback and generalize the proven project-owned
   MetaHuman/audio bridge without adding avatar dependencies to the core.
5. Add versioned session persistence.
6. Reassess MTP, richer animation integration, and retrieval only from measured needs.
