# License and attribution

Local Multimodal LLM is copyright 2026 Ian Sundahl, Volley Studios, and is
licensed under the Apache License, Version 2.0.

## Redistribution requirements

When redistributing the plug-in in source or binary form, including as part of
a packaged game or application:

1. include the plug-in `LICENSE` file;
2. include the plug-in `NOTICE` file in a readable location;
3. retain existing copyright, patent, trademark, and attribution notices in
   redistributed source files; and
4. identify modified files when distributing modified source.

The build rules stage `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`
automatically as loose NonUFS files. Release tooling also validates their
presence and verifies that the NOTICE preserves both **Ian Sundahl** and
**Volley Studios**.

When practical, please include this concise credit in your project credits,
documentation, About screen, or another readable acknowledgement:

> Local Multimodal LLM created by Ian Sundahl and Volley Studios.

Thank you for preserving this credit. It helps users and other developers find
the original project. Please also preserve the required license and NOTICE
material; the courtesy credit is not a substitute for those files.

## Third-party and model terms

The plug-in license covers the original plug-in code and documentation. It
does not replace the licenses of llama.cpp, ggml, sherpa-onnx, ONNX Runtime,
model weights, voice references, or other bundled components. Preserve
`THIRD_PARTY_NOTICES.md` and each applicable model/voice license alongside the
plug-in notices. See [Packaging](Packaging.md) and [Starter Models](StarterModels.md).
