# Third-party notices

Local Multimodal LLM interoperates with third-party software and externally
provided model assets. This file is an inventory, not legal advice.

## Bundled native components

### llama.cpp / ggml / libmtmd

- Pinned source commit: `2969d6d15`
- Reported ggml version: `0.16.0`
- License copy: `Source/ThirdParty/LlamaCpp/LICENSE`
- Runtime: `Binaries/ThirdParty/LlamaCpp/Win64`

The development runtime includes CPU, Vulkan, and CUDA backend modules. CUDA
runtime redistribution must comply with the applicable NVIDIA terms.

### sherpa-onnx

- Pinned release: `1.13.2`
- License copy: `Source/ThirdParty/SherpaOnnx/LICENSE`
- ONNX Runtime notice/license files are retained beside the imported SDK
- Runtime: `Binaries/ThirdParty/SherpaOnnx/Win64`

## Not bundled as plug-in content

Model weights, tokenizers, speech models, reference voices, EARS recordings,
Python environments, Chatterbox, NeuTTS, MetaHumans, maps, animation packs, and
other demo assets are supplied separately by the project developer. Their
licenses and voice/data consent requirements must be reviewed independently.

### Pocket TTS model assets

- Pocket TTS source: MIT
- Upstream Pocket weights: Creative Commons Attribution 4.0
- Tested sherpa-onnx ONNX bundle: Creative Commons Attribution 4.0
- The gated model's acceptable-use and voice-cloning consent requirements also
  apply.
- Do not redistribute the downloaded bundle's example `test_wavs`; use a
  separately licensed and consented voice reference.

### Bundled Pocket reference voices

`Content/Voices/pocket-caro-davy.wav` and
`Content/Voices/pocket-bill-boerst.wav` come from the Voice-Zero selection in
Kyutai's `tts-voices` repository. Kyutai declares the Voice-Zero recordings
CC0. Attribution is not required, but this notice preserves provenance:

- https://huggingface.co/kyutai/tts-voices
- https://huggingface.co/kyutai/tts-voices/tree/main/voice-zero

EARS and Expresso recordings are CC BY-NC 4.0 and are not bundled.

### NeuTTS model assets

NeuTTS code and weights use the NeuTTS Open License v1.0. It permits
redistribution and limited commercial use, but commercial rights under that
license stop when the using legal entity reaches USD $5 million in annual
revenue. Such users require a separate license from Neuphonic. Include the
license and NOTICE material, retain applicable attribution, and identify
modified files.

Before public distribution, retain all notices required by the selected runtime
profile and add a license governing the plug-in's own source and binaries.
