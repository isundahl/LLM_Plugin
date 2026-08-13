# AI-agent quick start

This page is the shortest reliable route for a coding agent adding offline
spoken NPCs to an Unreal Engine 5.8 Win64 project. The adjacent
`Examples/AIIntegration/integration.recipe.json` contains the same contract in
machine-readable form.

## Install and compile

1. Copy the complete `LocalMultimodalLLM` directory to
   `<Project>/Plugins/LocalMultimodalLLM`. Do not move its internal `Models`,
   `ModelLicenses`, `Binaries`, or `Source` directories.
2. Enable **Local Multimodal LLM** and **Audio Capture**.
3. Regenerate project files and compile the project once. Do not define native
   feature macros manually.

Fab Starter Core is self-contained. Its model paths are resolved relative to
the plug-in. A source-only Git checkout needs a separately assembled model
pack as described in [Starter Models](StarterModels.md).

## Minimal Blueprint graph

Create one persistent actor (a player, game-state conversation manager, or
dedicated actor) and add:

- `Local LLM`
- `Local LLM Microphone`
- `Local LLM Text To Speech`

Then implement this order:

1. On `BeginPlay`, call `Load Model By Id` with `gemma-4-e2b-it-qat`.
2. When the model-ready status arrives, call `Create Character Session` with a
   `Local LLM Character Sheet`. Store the returned `SessionId` per NPC.
3. Bind push-to-talk **Pressed** to
   `Start Push To Talk Recording(SessionId)` and **Released** to
   `Stop Push To Talk Recording And Submit`.
4. Bind `OnTextDelta`. Accumulate text by request and send only complete,
   validated sentences to `Queue Speech`; show the same text as subtitles.
5. Bind `OnToolCall` only when gameplay actions are needed. Validate the
   allow-listed tool name, arguments, authority, distance, and cooldown before
   invoking Unreal behavior, then return the outcome with `Submit Tool Result`.

The default configuration selects native Parakeet STT and Pocket TTS. Set
`Initialize On Begin Play` for speech and preload STT during a loading screen
when first-response latency matters.

## Architectural rules

- Load one text model and retain one isolated session per character.
- Store authoritative world facts in game state and inject only relevant
  character/world context.
- Treat relationship scores as decision context, not dialogue that must be
  recited.
- Prefer push-to-talk; use VAD only for an opt-in always-listening mode.
- Never permit a model-generated function name or JSON object to execute
  arbitrary Unreal reflection.
- Handle failure through `OnStatusChanged`; CPU fallback is valid but should be
  surfaced because it can be slower.

## Completion checks

- `Get Available Models` contains `gemma-4-e2b-it-qat`.
- Model, STT, both character sessions, and required voices report ready.
- Two NPCs retain distinct names, memories, and relationship settings.
- Push-to-talk produces one final transcript and at most one normal responder.
- A permitted tool succeeds and an unknown tool is rejected.
- A packaged Win64 build runs without Python, developer tools, or network
  access.

For configuration details and delegate semantics, continue with the
[User Guide](UserGuide.md).

