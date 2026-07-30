using System.IO;
using UnrealBuildTool;

public class LocalMultimodalLLM : ModuleRules
{
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

        foreach (string Voice in new[]
        {
            "pocket-caro-davy.wav",
            "pocket-bill-boerst.wav"
        })
        {
            RuntimeDependencies.Add(
                Path.Combine(PluginDirectory, "Content", "Voices", Voice),
                StagedFileType.NonUFS);
        }
    }
}
