# Local Multimodal LLM: agent integration contract

This folder is a self-contained Win64 Unreal Engine 5.8 plug-in. A coding
agent integrating it into a game should start with `Docs/AIQuickStart.md` and
the machine-readable `Examples/AIIntegration/integration.recipe.json`.

## Safe default path

1. Place this entire folder at `<UnrealProject>/Plugins/LocalMultimodalLLM`.
2. Enable `LocalMultimodalLLM` and Unreal's `AudioCapture` plug-in.
3. Regenerate project files and compile once.
4. Add `Local LLM`, `Local LLM Microphone`, and `Local LLM Text To Speech`
   components to one persistent actor.
5. Load model ID `gemma-4-e2b-it-qat`, then create one character session per
   NPC after model load completes.
6. Use push-to-talk by default and queue validated `OnTextDelta` sentences to
   TTS.

The Fab Starter Core archive already includes the approved model files in
`Models/`. Source-only checkouts intentionally do not.

## Invariants

- Keep game state authoritative in Unreal code.
- Allow-list and validate every mutating tool call.
- Never manually define `LOCAL_MULTIMODAL_LLM_WITH_LLAMA`; Build.cs detects
  complete native artifacts.
- Keep separate sessions for characters while sharing one loaded model.
- Preserve `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, `ModelLicenses/`, and
  the notices accompanying every redistributed model or voice.
- Vision is a development path, not part of the v0.1 Starter workflow.

When practical, please include this acknowledgement in credits or
documentation:

> Local Multimodal LLM created by Ian Sundahl and Volley Studios.
