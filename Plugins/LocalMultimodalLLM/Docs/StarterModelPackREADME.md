# Local Multimodal LLM Starter Model Pack

This optional model-only package accompanies Local Multimodal LLM 0.1.0-beta
for Unreal Engine 5.8 on Win64. It contains the preconfigured default model
stack:

- Gemma 4 E2B IT QAT `UD-Q4_K_XL` for local text generation
- Parakeet Unified English 0.6B INT8 for local speech recognition
- Pocket TTS INT8 for local speech synthesis

The four CC0 Pocket voice references ship with the plug-in itself, so they are
not duplicated here. Most users should download **Starter Core** or **Starter
NVIDIA**, which already combine this model pack with the plug-in and voices.

## Install

Close Unreal Editor, then copy the `Models` and `ModelLicenses` directories to
the root of the Unreal project that contains the Local Multimodal LLM plug-in.
Merge directories when prompted. The result should begin like this:

```text
<Project>/
  Plugins/LocalMultimodalLLM/
  Models/Gemma4E2B/
  Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming/
  Models/PocketTTS/
  ModelLicenses/
```

Do not rename the supplied directories or model files unless you also update
the plug-in configuration and Gemma manifest. `SHA256SUMS.txt` covers every
file in this package; verify it before installation or redistribution.

## Licensing

The model assets are not licensed under the plug-in's Apache 2.0 license.
Their independent terms and required notices are preserved under
`ModelLicenses`. Pocket TTS is CC BY 4.0, the four plug-in voice references are
CC0, Gemma is Apache 2.0, and Parakeet uses the NVIDIA Open Model License.

Copyright 2026 Ian Sundahl, Volley Studios. See `NOTICE` for plug-in
attribution and source information.
