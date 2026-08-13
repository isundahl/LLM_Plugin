# Version 1 release checklist

Copyright 2026 Ian Sundahl, Volley Studios. Licensed under Apache 2.0.

Do not rename the descriptor to `1.0.0` or clear its beta flag until every
release gate below is complete.

## Local clean-room result (2026-08-13)

An isolated UE 5.8 Win64 project was created with only the assembled Starter
Core directories and a minimal game module. It compiled the plug-in from the
distributed source, passed native Gemma, Parakeet, and four-voice Pocket tests,
then built, cooked, staged, archived, and launched a Shipping package. A
Shipping-only harness also discovered and loaded Gemma from the staged layout
and exited successfully.

This test uncovered and corrected project-relative path duplication in model
discovery and native speech model resolution. It is strong evidence for a
fresh project on the current machine, but it does **not** replace the remaining
external gate: launch the extracted package on a separate Win64 machine without
Visual Studio, Unreal Engine, CUDA, or project development files installed.

## Code and native runtime

- Unreal `BuildPlugin` passes Win64 editor, Development game, and Shipping game builds.
- `PreparePluginRelease.ps1` produces a clean candidate and that candidate passes
  `ValidateDistribution.ps1 -Profile Full`. Raw `BuildPlugin` output is a build
  artifact and intentionally contains `Intermediate` files and an editor PDB;
  do not distribute it unchanged.
- Git LFS integrity passes and every native DLL/import library is from its documented ABI set.
- Automation tests pass in a clean host project.
- A packaged Shipping smoke test passes on a Win64 machine without developer tools installed.
- CPU fallback plus Vulkan and CUDA paths have each passed the explicit
  `LocalLLMDiagnosticBackend` native smoke where suitable hardware exists.

## Starter bundle

- Only the approved Gemma, Parakeet, and Pocket runtime files are present.
- Upstream `test_wavs`, local recordings, benchmark data, and experimental weights are absent.
- `ModelLicenses` contains a license and product-specific notice for every bundled model.
- `SHA256SUMS.txt` covers every shipped weight and tokenizer/model-data file.
- `ValidateStarterBundle.ps1` passes against the final extracted bundle.
- The extracted bundle launches without downloading or moving files manually.

## Product and legal

- The plug-in's Apache 2.0 `LICENSE` and Ian Sundahl / Volley Studios `NOTICE`
  are present in source, plugin-only, and Starter release archives.
- Release documentation accurately explains that Apache Section 4 requires
  preservation of applicable license/NOTICE material but does not mandate a
  splash screen, advertising credit, or visible in-game credit.
- Third-party notices and model/voice attributions have been reviewed for the exact release files.
- Version, beta status, compatibility, release notes, archive names, and download sizes are final.
- The quick start has been followed by someone using a clean project rather than the demo project.
- Known limitations distinguish native Shipping providers from development-only Python adapters.
