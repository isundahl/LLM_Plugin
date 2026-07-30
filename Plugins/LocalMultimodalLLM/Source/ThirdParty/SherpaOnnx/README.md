# sherpa-onnx Win64 C API

Pinned source: sherpa-onnx v1.13.2 (`13d0ae6c539d2809d32f5eaa3ef1db0c459d0b24`).

The staged build is CPU-only and shared. It excludes command-line binaries,
PortAudio, websockets, tests, and speaker diarization, while retaining the C API
implementations needed by Parakeet STT, speaker embeddings, and Pocket TTS.
It was configured with `SHERPA_ONNX_ENABLE_TTS=ON`,
`SHERPA_ONNX_ENABLE_C_API=ON`, and `SHERPA_ONNX_ENABLE_BINARY=OFF`.
Runtime dependencies are
`sherpa-onnx-c-api.dll`, `onnxruntime.dll`, and
`onnxruntime_providers_shared.dll`.

The module defines `LOCAL_MULTIMODAL_LLM_WITH_SHERPA=1` only when the matching
header, import library, and runtime DLL are present. Removing this ThirdParty
payload leaves the core plugin buildable and disables only the sherpa provider.
