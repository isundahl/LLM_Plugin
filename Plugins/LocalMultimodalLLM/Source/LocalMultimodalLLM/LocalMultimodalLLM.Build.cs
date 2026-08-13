using System.IO;
using UnrealBuildTool;

public class LocalMultimodalLLM : ModuleRules
{
    // Copyright 2026 Ian Sundahl, Volley Studios. SPDX-License-Identifier: Apache-2.0
    public LocalMultimodalLLM(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AudioCaptureCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "DeveloperSettings",
            "Json",
            "Projects",
            "LlamaCpp",
            "SherpaOnnx"
        });

        bEnableExceptions = false;
        bUseRTTI = false;

        // Apache 2.0 Section 4 requires redistributions to preserve applicable
        // license and NOTICE material. Stage it beside the plug-in automatically.
        foreach (string NoticeFile in new[] { "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md" })
        {
            RuntimeDependencies.Add(
                Path.Combine(PluginDirectory, NoticeFile),
                StagedFileType.NonUFS);
        }

        foreach (string Voice in new[]
        {
            "pocket-caro-davy.wav",
            "pocket-bill-boerst.wav",
            "pocket-peter-yearsley.wav",
            "pocket-stuart-bell.wav"
        })
        {
            RuntimeDependencies.Add(
                Path.Combine(PluginDirectory, "Content", "Voices", Voice),
                StagedFileType.NonUFS);
        }

        // A Starter download installs approved model assets beside the
        // project's .uproject. Stage only the known runtime files when they
        // are present; never sweep arbitrary Models content such as upstream
        // test WAVs, developer-only models, or local voice datasets.
        if (Target.ProjectFile != null)
        {
            string ProjectDir = Target.ProjectFile.Directory.FullName;
            AddOptionalStarterFiles(ProjectDir, "Models/Gemma4E2B", new[]
            {
                "gemma-4-e2b-it-qat.localllm.json",
                "gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf"
            });
            AddOptionalStarterFiles(ProjectDir,
                "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming", new[]
            {
                "encoder.int8.onnx",
                "decoder.int8.onnx",
                "joiner.int8.onnx",
                "tokens.txt",
                "bias.md",
                "explainability.md",
                "privacy.md",
                "safety.md"
            });
            AddOptionalStarterFiles(ProjectDir,
                "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26", new[]
            {
                "lm_flow.int8.onnx",
                "lm_main.int8.onnx",
                "encoder.onnx",
                "decoder.int8.onnx",
                "text_conditioner.onnx",
                "vocab.json",
                "token_scores.json",
                "LICENSE",
                "README.md"
            });
            AddOptionalStarterFiles(ProjectDir, "ModelLicenses/Gemma4", new[] { "LICENSE", "NOTICE.md" });
            AddOptionalStarterFiles(ProjectDir, "ModelLicenses/Parakeet", new[] { "LICENSE.pdf", "NOTICE.md" });
            AddOptionalStarterFiles(ProjectDir, "ModelLicenses/PocketTTS", new[] { "LICENSE", "NOTICE.md" });
            AddOptionalStarterFiles(ProjectDir, "ModelLicenses/Voices", new[] { "LICENSE", "NOTICE.md" });
            AddOptionalStarterFiles(ProjectDir, "", new[] { "SHA256SUMS.txt" });
        }

        // Fab distributes one self-contained plug-in directory. If an
        // assembled Fab package contains the Starter assets inside the plug-in,
        // stage the same strict allow-list from there as well.
        AddOptionalPluginStarterFiles("Models/Gemma4E2B", new[]
        {
            "gemma-4-e2b-it-qat.localllm.json",
            "gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf"
        });
        AddOptionalPluginStarterFiles(
            "Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming", new[]
        {
            "encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt",
            "bias.md", "explainability.md", "privacy.md", "safety.md"
        });
        AddOptionalPluginStarterFiles(
            "Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26", new[]
        {
            "lm_flow.int8.onnx", "lm_main.int8.onnx", "encoder.onnx", "decoder.int8.onnx",
            "text_conditioner.onnx", "vocab.json", "token_scores.json", "LICENSE", "README.md"
        });
        AddOptionalPluginStarterFiles("ModelLicenses/Gemma4", new[] { "LICENSE", "NOTICE.md" });
        AddOptionalPluginStarterFiles("ModelLicenses/Parakeet", new[] { "LICENSE.pdf", "NOTICE.md" });
        AddOptionalPluginStarterFiles("ModelLicenses/PocketTTS", new[] { "LICENSE", "NOTICE.md" });
        AddOptionalPluginStarterFiles("ModelLicenses/Voices", new[] { "LICENSE", "NOTICE.md" });
        AddOptionalPluginStarterFiles("", new[] { "SHA256SUMS.txt" });
    }

    private void AddOptionalStarterFiles(string ProjectDir, string RelativeDirectory, string[] FileNames)
    {
        foreach (string FileName in FileNames)
        {
            string RelativePath = Path.Combine(RelativeDirectory, FileName);
            string Source = Path.Combine(ProjectDir, RelativePath);
            if (File.Exists(Source))
            {
                RuntimeDependencies.Add(Path.Combine("$(ProjectDir)", RelativePath), StagedFileType.NonUFS);
            }
        }
    }

    private void AddOptionalPluginStarterFiles(string RelativeDirectory, string[] FileNames)
    {
        foreach (string FileName in FileNames)
        {
            string Source = Path.Combine(PluginDirectory, RelativeDirectory, FileName);
            if (File.Exists(Source))
            {
                RuntimeDependencies.Add(Source, StagedFileType.NonUFS);
            }
        }
    }
}
