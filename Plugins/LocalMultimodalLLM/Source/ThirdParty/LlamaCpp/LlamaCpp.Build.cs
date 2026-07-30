using System.IO;
using UnrealBuildTool;

public class LlamaCpp : ModuleRules
{
    public LlamaCpp(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string IncludeDir = Path.Combine(ModuleDirectory, "Include");
        string PlatformDir = Target.Platform.ToString();
        string LibDir = Path.Combine(ModuleDirectory, "Lib", PlatformDir);
        // Runtime DLLs live under the plugin's Binaries tree. This is the
        // standard Unreal location for prebuilt plugin payloads and remains
        // addressable in both Editor and packaged builds.
        string BinDir = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "LlamaCpp", PlatformDir);

        string[] RequiredHeaders =
        {
            "llama.h",
            "ggml.h",
            "ggml-backend.h",
            "mtmd.h"
        };

        string[] CoreLibraries =
        {
            "llama.lib",
            "mtmd.lib",
            "ggml.lib",
            "ggml-base.lib"
        };

        string[] CoreDlls =
        {
            "llama.dll",
            "mtmd.dll",
            "ggml.dll",
            "ggml-base.dll"
        };

        string[] CpuBackendDlls =
        {
            "ggml-cpu-x64.dll",
            "ggml-cpu-sse42.dll",
            "ggml-cpu-sandybridge.dll",
            "ggml-cpu-haswell.dll",
            "ggml-cpu-skylakex.dll",
            "ggml-cpu-cannonlake.dll",
            "ggml-cpu-cascadelake.dll",
            "ggml-cpu-icelake.dll",
            "ggml-cpu-alderlake.dll"
        };

        string[] CudaDlls =
        {
            "ggml-cuda.dll",
            "cudart64_12.dll",
            "cublas64_12.dll",
            "cublasLt64_12.dll"
        };

        string[] VulkanDlls =
        {
            "ggml-vulkan.dll"
        };

        bool bHasHeaders = AllFilesExist(IncludeDir, RequiredHeaders);
        bool bHasCoreArtifacts =
            Target.Platform == UnrealTargetPlatform.Win64 &&
            AllFilesExist(LibDir, CoreLibraries) &&
            AllFilesExist(BinDir, CoreDlls);
        bool bWithLlamaCpp = bHasHeaders && bHasCoreArtifacts;
        bool bWithCpu = bWithLlamaCpp && AllFilesExist(BinDir, CpuBackendDlls);
        bool bWithCuda = bWithLlamaCpp && AllFilesExist(BinDir, CudaDlls);
        bool bWithVulkan = bWithLlamaCpp && AllFilesExist(BinDir, VulkanDlls);

        PublicDefinitions.Add("LOCAL_MULTIMODAL_LLM_WITH_LLAMA=" + (bWithLlamaCpp ? "1" : "0"));
        PublicDefinitions.Add("LOCAL_MULTIMODAL_LLM_WITH_CPU=" + (bWithCpu ? "1" : "0"));
        PublicDefinitions.Add("LOCAL_MULTIMODAL_LLM_WITH_CUDA=" + (bWithCuda ? "1" : "0"));
        PublicDefinitions.Add("LOCAL_MULTIMODAL_LLM_WITH_VULKAN=" + (bWithVulkan ? "1" : "0"));
        PublicDefinitions.Add("LLAMA_SHARED=" + (bWithLlamaCpp ? "1" : "0"));

        // Keep the project buildable until a complete, matching core artifact set is installed.
        if (!bWithLlamaCpp)
        {
            return;
        }

        PublicSystemIncludePaths.Add(IncludeDir);

        foreach (string Library in CoreLibraries)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, Library));
        }

        // Only directly imported core DLLs are delay-loaded. The runtime adapter
        // explicitly asks ggml to discover CPU/GPU backend modules from this
        // staged plug-in runtime directory in Editor and packaged builds.
        foreach (string Dll in CoreDlls)
        {
            PublicDelayLoadDLLs.Add(Dll);
        }

        PublicRuntimeLibraryPaths.Add(BinDir);

        AddRuntimeDlls(BinDir, CoreDlls);
        if (bWithCpu) AddRuntimeDlls(BinDir, CpuBackendDlls);
        if (bWithCuda) AddRuntimeDlls(BinDir, CudaDlls);
        if (bWithVulkan) AddRuntimeDlls(BinDir, VulkanDlls);

        PublicSystemLibraries.AddRange(new[]
        {
            "advapi32.lib",
            "bcrypt.lib"
        });
    }

    private void AddRuntimeDlls(string BinDir, string[] DllNames)
    {
        foreach (string Dll in DllNames)
        {
            RuntimeDependencies.Add(Path.Combine(BinDir, Dll), StagedFileType.NonUFS);
        }
    }

    private static bool AllFilesExist(string DirectoryPath, string[] FileNames)
    {
        foreach (string FileName in FileNames)
        {
            if (!File.Exists(Path.Combine(DirectoryPath, FileName)))
            {
                return false;
            }
        }

        return true;
    }
}
