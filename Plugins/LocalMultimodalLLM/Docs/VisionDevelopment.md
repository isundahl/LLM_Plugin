# Development vision input

Copyright 2026 Ian Sundahl, Volley Studios. Licensed under Apache 2.0.

Vision is a usable **development feature** in Local Multimodal LLM. The plug-in
exposes `Submit Image For Session` to Blueprint and C++, then evaluates the
image through the included llama.cpp/libmtmd runtime. Vision is not part of the
preconfigured Starter path: Starter downloads do not include an `mmproj`, and
the default Gemma Starter manifest disables projector loading.

## Requirements

A development project must supply:

1. a GGUF model whose llama.cpp architecture supports multimodal input;
2. the projector built for that exact model/revision;
3. a custom `*.localllm.json` manifest identifying both files; and
4. RGB image bytes with dimensions matching the submitted buffer.

Do not mix projectors between model families, parameter sizes, or incompatible
revisions. A failed optional projector load leaves text inference available and
emits an error for the image request.

Minimal manifest fields:

```json
{
  "schemaVersion": 1,
  "id": "my-vision-model",
  "displayName": "My Vision Model",
  "architecture": "model-architecture",
  "files": {
    "model": "model.gguf",
    "multimodalProjector": "mmproj-model-f16.gguf"
  },
  "capabilities": {
    "text": true,
    "vision": true,
    "audioInput": false
  },
  "load": {
    "projectorLoadPolicy": "lazy",
    "projectorOnGpu": true,
    "warmupProjector": false
  }
}
```

`lazy` avoids projector memory cost until the first image request. `preload`
loads it with the text model; `disabled` makes image requests fail explicitly.

## Blueprint flow

1. Load the custom model and wait for `ModelLoaded`.
2. Create the target character session.
3. Construct `Local LLM Image Input`:
   - `Width` and `Height` must be positive;
   - `Rgb Pixels` must contain exactly `Width * Height * 3` bytes;
   - bytes are packed RGB, eight bits per channel.
4. Call `Submit Image For Session(SessionId, Image, Prompt)`.
5. Route `OnTextDelta` and completion events exactly as for a text turn.

The plug-in does not yet provide the polished convenience layer for converting
a `Texture2D`, render target, camera frame, or screenshot into the RGB struct.
Projects should perform that conversion in their own Blueprint/C++ capture
code and avoid synchronous GPU readback during gameplay.

## Development boundary

The image node and native evaluation path are functional, but v1 does not make
cross-model projector compatibility, automated scene capture, projector
packaging, memory budgeting, or vision latency a Starter guarantee. Validate
the chosen model/projector pair and include its separate license and notices in
the product distribution. Text, Parakeet STT, and native Pocket TTS remain the
recommended Starter workflow.
