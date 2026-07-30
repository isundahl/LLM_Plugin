# llama.cpp artifact layout

This directory is the Unreal external-module boundary for the pinned llama.cpp
build recorded in `Version.txt`. Never mix headers, import libraries, or DLLs
from different commits or build configurations.

```text
LlamaCpp/
|-- LlamaCpp.Build.cs
|-- Version.txt
|-- LICENSE
|-- Include/                 # llama, ggml, and MTMD public headers
`-- Lib/Win64/               # core import libraries only
```

The runtime payload uses Unreal's standard plugin binaries layout:

```text
LocalMultimodalLLM/Binaries/ThirdParty/LlamaCpp/Win64/
    |-- llama.dll
    |-- mtmd.dll
    |-- ggml.dll
    |-- ggml-base.dll
    |-- ggml-cpu-*.dll       # dynamically selected CPU variants
    |-- ggml-cuda.dll
    |-- ggml-vulkan.dll
    |-- cudart64_12.dll
    |-- cublas64_12.dll
    `-- cublasLt64_12.dll
```

The module enables `LOCAL_MULTIMODAL_LLM_WITH_LLAMA=1` when the core artifact
set is complete. CPU, CUDA, and Vulkan have separate feature definitions and
are staged only when each backend's complete runtime set is present. The module
loads the core DLLs from this directory explicitly; ggml discovers its CPU and
GPU backend modules there dynamically. Unreal records every DLL as a runtime
dependency so the directory is included in staged and packaged builds.

`nvcuda.dll` and `vulkan-1.dll` are supplied by GPU drivers and are not bundled.
The Unreal prerequisite installer supplies the Microsoft Visual C++ runtime.
