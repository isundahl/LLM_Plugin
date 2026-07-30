using System.IO;
using UnrealBuildTool;

public class SherpaOnnx : ModuleRules
{
    public SherpaOnnx(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string IncludeDir = Path.Combine(ModuleDirectory, "Include");
        string LibDir = Path.Combine(ModuleDirectory, "Lib", "Win64");
        string RuntimeDir = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "SherpaOnnx", "Win64");
        string ImportLib = Path.Combine(LibDir, "sherpa-onnx-c-api.lib");
        string Header = Path.Combine(IncludeDir, "sherpa-onnx", "c-api", "c-api.h");
        string ApiDll = Path.Combine(RuntimeDir, "sherpa-onnx-c-api.dll");
        bool bAvailable = Target.Platform == UnrealTargetPlatform.Win64 &&
            File.Exists(Header) && File.Exists(ImportLib) && File.Exists(ApiDll);

        PublicDefinitions.Add("LOCAL_MULTIMODAL_LLM_WITH_SHERPA=" + (bAvailable ? "1" : "0"));
        if (!bAvailable) return;

        PublicSystemIncludePaths.Add(IncludeDir);
        PublicAdditionalLibraries.Add(ImportLib);
        PublicDefinitions.Add("SHERPA_ONNX_BUILD_SHARED_LIBS=1");
        PublicDelayLoadDLLs.Add("sherpa-onnx-c-api.dll");
        PublicRuntimeLibraryPaths.Add(RuntimeDir);

        foreach (string Dll in new[] { "onnxruntime.dll", "onnxruntime_providers_shared.dll", "sherpa-onnx-c-api.dll" })
        {
            string Source = Path.Combine(RuntimeDir, Dll);
            if (File.Exists(Source))
                RuntimeDependencies.Add(Source, StagedFileType.NonUFS);
        }
    }
}
