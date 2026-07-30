#include "Models/LocalLLMModelRegistry.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "LocalLLMSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool ReadBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const bool Default)
{
    bool Value = Default;
    return Object.IsValid() && Object->TryGetBoolField(Name, Value) ? Value : Default;
}

int32 ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const int32 Default)
{
    double Value = Default;
    return Object.IsValid() && Object->TryGetNumberField(Name, Value) ? static_cast<int32>(Value) : Default;
}

float ReadFloat(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const float Default)
{
    double Value = Default;
    return Object.IsValid() && Object->TryGetNumberField(Name, Value) ? static_cast<float>(Value) : Default;
}

FString ResolveArtifact(const FString& ManifestDirectory, const FString& Value)
{
    if (Value.IsEmpty()) return {};
    FString Result = FPaths::IsRelative(Value) ? FPaths::Combine(ManifestDirectory, Value) : Value;
    FPaths::NormalizeFilename(Result);
    return FPaths::ConvertRelativePathToFull(Result);
}

void AddSearchRoot(TArray<FString>& Roots, FString Root)
{
    if (Root.IsEmpty()) return;
    if (FPaths::IsRelative(Root)) Root = FPaths::Combine(FPaths::ProjectDir(), Root);
    Root = FPaths::ConvertRelativePathToFull(Root);
    FPaths::NormalizeDirectoryName(Root);
    Roots.AddUnique(Root);
}
}

TArray<FLocalLLMModelInfo> FLocalLLMModelRegistry::Discover()
{
    TArray<FString> Roots;
    AddSearchRoot(Roots, FPaths::Combine(FPaths::ProjectDir(), TEXT("Models")));
    AddSearchRoot(Roots, FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LocalMultimodalLLM"), TEXT("Models")));
    for (const FDirectoryPath& Directory : GetDefault<ULocalLLMSettings>()->AdditionalModelDirectories)
    {
        AddSearchRoot(Roots, Directory.Path);
    }

    TArray<FLocalLLMModelInfo> Results;
    TSet<FString> SeenIds;
    for (const FString& Root : Roots)
    {
        TArray<FString> Manifests;
        IFileManager::Get().FindFilesRecursive(Manifests, *Root, TEXT("*.localllm.json"), true, false);
        for (const FString& Manifest : Manifests)
        {
            FLocalLLMModelInfo Info;
            LoadManifest(Manifest, Info);
            if (!Info.Config.Id.IsEmpty() && SeenIds.Contains(Info.Config.Id))
            {
                Info.bCompatible = false;
                Info.Status = FString::Printf(TEXT("Duplicate model id '%s'"), *Info.Config.Id);
            }
            SeenIds.Add(Info.Config.Id);
            Results.Add(MoveTemp(Info));
        }
    }
    Results.Sort([](const FLocalLLMModelInfo& A, const FLocalLLMModelInfo& B)
    {
        return A.Config.DisplayName < B.Config.DisplayName;
    });
    return Results;
}

bool FLocalLLMModelRegistry::FindById(const FString& ModelId, FLocalLLMModelInfo& OutInfo)
{
    for (FLocalLLMModelInfo& Info : Discover())
    {
        if (Info.Config.Id.Equals(ModelId, ESearchCase::IgnoreCase))
        {
            OutInfo = MoveTemp(Info);
            return true;
        }
    }
    OutInfo.Status = FString::Printf(TEXT("No discovered model has id '%s'"), *ModelId);
    return false;
}

bool FLocalLLMModelRegistry::LoadManifest(const FString& ManifestPath, FLocalLLMModelInfo& OutInfo)
{
    OutInfo = {};
    OutInfo.Config.ManifestPath = FPaths::ConvertRelativePathToFull(ManifestPath);
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *ManifestPath))
    {
        OutInfo.Status = TEXT("Manifest could not be read");
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutInfo.Status = TEXT("Manifest contains invalid JSON");
        return false;
    }

    const int32 SchemaVersion = ReadInt(Root, TEXT("schemaVersion"), 0);
    Root->TryGetStringField(TEXT("id"), OutInfo.Config.Id);
    Root->TryGetStringField(TEXT("displayName"), OutInfo.Config.DisplayName);
    Root->TryGetStringField(TEXT("architecture"), OutInfo.Config.Architecture);
    if (OutInfo.Config.DisplayName.IsEmpty()) OutInfo.Config.DisplayName = OutInfo.Config.Id;

    const TSharedPtr<FJsonObject>* FilesPtr = nullptr;
    const TSharedPtr<FJsonObject> Files = Root->TryGetObjectField(TEXT("files"), FilesPtr) ? *FilesPtr : nullptr;
    FString ModelFile, ProjectorFile, DraftFile;
    if (Files.IsValid())
    {
        Files->TryGetStringField(TEXT("model"), ModelFile);
        Files->TryGetStringField(TEXT("multimodalProjector"), ProjectorFile);
        Files->TryGetStringField(TEXT("draftModel"), DraftFile);
    }
    const FString ManifestDirectory = FPaths::GetPath(OutInfo.Config.ManifestPath);
    OutInfo.Config.ModelPath = ResolveArtifact(ManifestDirectory, ModelFile);
    OutInfo.Config.MultimodalProjectorPath = ResolveArtifact(ManifestDirectory, ProjectorFile);
    OutInfo.Config.DraftModelPath = ResolveArtifact(ManifestDirectory, DraftFile);

    const TSharedPtr<FJsonObject>* ChatPtr = nullptr;
    const TSharedPtr<FJsonObject> Chat = Root->TryGetObjectField(TEXT("chat"), ChatPtr) ? *ChatPtr : nullptr;
    if (Chat.IsValid())
    {
        Chat->TryGetStringField(TEXT("promptPrefix"), OutInfo.Config.PromptPrefix);
        Chat->TryGetStringField(TEXT("assistantPrefill"), OutInfo.Config.AssistantPrefill);
        Chat->TryGetStringField(TEXT("noThinkAssistantPrefill"), OutInfo.Config.NoThinkAssistantPrefill);
        Chat->TryGetStringField(TEXT("thinkingAssistantPrefill"), OutInfo.Config.ThinkingAssistantPrefill);
    }

    const TSharedPtr<FJsonObject>* CapabilitiesPtr = nullptr;
    const TSharedPtr<FJsonObject> Capabilities = Root->TryGetObjectField(TEXT("capabilities"), CapabilitiesPtr) ? *CapabilitiesPtr : nullptr;
    OutInfo.Config.Capabilities.bText = ReadBool(Capabilities, TEXT("text"), true);
    OutInfo.Config.Capabilities.bVision = ReadBool(Capabilities, TEXT("vision"), false);
    OutInfo.Config.Capabilities.bAudioInput = ReadBool(Capabilities, TEXT("audioInput"), false);
    OutInfo.Config.Capabilities.bToolCalling = ReadBool(Capabilities, TEXT("toolCalling"), false);
    OutInfo.Config.Capabilities.bSpeechOutput = ReadBool(Capabilities, TEXT("speechOutput"), false);
    OutInfo.Config.Capabilities.bSpeculativeDecoding = ReadBool(Capabilities, TEXT("speculativeDecoding"), false);
    OutInfo.Config.Capabilities.bReasoning = ReadBool(Capabilities, TEXT("reasoning"), false);

    const TSharedPtr<FJsonObject>* LoadPtr = nullptr;
    const TSharedPtr<FJsonObject> Load = Root->TryGetObjectField(TEXT("load"), LoadPtr) ? *LoadPtr : nullptr;
    FString ContextPresetError;
    FString ContextPreset;
    if (Load.IsValid() && Load->TryGetStringField(TEXT("contextPreset"), ContextPreset))
    {
        if (ContextPreset.Equals(TEXT("compact"), ESearchCase::IgnoreCase) || ContextPreset.Equals(TEXT("compact4k"), ESearchCase::IgnoreCase))
        {
            OutInfo.Config.Load.ContextPreset = ELocalLLMContextPreset::Compact4K;
            OutInfo.Config.Load.ContextSize = 4096;
        }
        else if (ContextPreset.Equals(TEXT("standard"), ESearchCase::IgnoreCase) || ContextPreset.Equals(TEXT("standard8k"), ESearchCase::IgnoreCase))
        {
            OutInfo.Config.Load.ContextPreset = ELocalLLMContextPreset::Standard8K;
            OutInfo.Config.Load.ContextSize = 8192;
        }
        else if (ContextPreset.Equals(TEXT("extended"), ESearchCase::IgnoreCase) || ContextPreset.Equals(TEXT("extended16k"), ESearchCase::IgnoreCase))
        {
            OutInfo.Config.Load.ContextPreset = ELocalLLMContextPreset::Extended16K;
            OutInfo.Config.Load.ContextSize = 16384;
        }
        else if (ContextPreset.Equals(TEXT("custom"), ESearchCase::IgnoreCase))
        {
            OutInfo.Config.Load.ContextPreset = ELocalLLMContextPreset::Custom;
            OutInfo.Config.Load.ContextSize = ReadInt(Load, TEXT("contextSize"), OutInfo.Config.Load.ContextSize);
        }
        else
        {
            ContextPresetError = TEXT("load.contextPreset must be compact, standard, extended, or custom");
        }
    }
    else
    {
        // Backward-compatible manifests with only contextSize are explicit custom configurations.
        OutInfo.Config.Load.ContextPreset = ELocalLLMContextPreset::Custom;
        OutInfo.Config.Load.ContextSize = ReadInt(Load, TEXT("contextSize"), OutInfo.Config.Load.ContextSize);
    }
    OutInfo.Config.Load.BatchSize = ReadInt(Load, TEXT("batchSize"), OutInfo.Config.Load.BatchSize);
    OutInfo.Config.Load.MicroBatchSize = ReadInt(Load, TEXT("microBatchSize"), OutInfo.Config.Load.MicroBatchSize);
    OutInfo.Config.Load.GpuLayers = ReadInt(Load, TEXT("gpuLayers"), OutInfo.Config.Load.GpuLayers);
    OutInfo.Config.Load.MainGpu = ReadInt(Load, TEXT("mainGpu"), OutInfo.Config.Load.MainGpu);
    OutInfo.Config.Load.Threads = ReadInt(Load, TEXT("threads"), OutInfo.Config.Load.Threads);
    OutInfo.Config.Load.BatchThreads = ReadInt(Load, TEXT("batchThreads"), OutInfo.Config.Load.BatchThreads);
    OutInfo.Config.Load.bUseMemoryMap = ReadBool(Load, TEXT("useMemoryMap"), OutInfo.Config.Load.bUseMemoryMap);
    OutInfo.Config.Load.bLockMemory = ReadBool(Load, TEXT("lockMemory"), OutInfo.Config.Load.bLockMemory);
    OutInfo.Config.Load.bCheckTensors = ReadBool(Load, TEXT("checkTensors"), OutInfo.Config.Load.bCheckTensors);
    OutInfo.Config.Load.bFlashAttention = ReadBool(Load, TEXT("flashAttention"), OutInfo.Config.Load.bFlashAttention);
    OutInfo.Config.Load.bOffloadKqv = ReadBool(Load, TEXT("offloadKqv"), OutInfo.Config.Load.bOffloadKqv);
    OutInfo.Config.Load.bWarmupModel = ReadBool(Load, TEXT("warmupModel"), OutInfo.Config.Load.bWarmupModel);
    FString ProjectorPolicyError;
    FString ProjectorPolicy;
    if (Load.IsValid() && Load->TryGetStringField(TEXT("projectorLoadPolicy"), ProjectorPolicy))
    {
        if (ProjectorPolicy.Equals(TEXT("disabled"), ESearchCase::IgnoreCase))
            OutInfo.Config.Load.ProjectorLoadPolicy = ELocalLLMProjectorLoadPolicy::Disabled;
        else if (ProjectorPolicy.Equals(TEXT("lazy"), ESearchCase::IgnoreCase))
            OutInfo.Config.Load.ProjectorLoadPolicy = ELocalLLMProjectorLoadPolicy::Lazy;
        else if (ProjectorPolicy.Equals(TEXT("preload"), ESearchCase::IgnoreCase))
            OutInfo.Config.Load.ProjectorLoadPolicy = ELocalLLMProjectorLoadPolicy::Preload;
        else
            ProjectorPolicyError = TEXT("load.projectorLoadPolicy must be disabled, lazy, or preload");
    }
    OutInfo.Config.Load.bProjectorOnGpu = ReadBool(Load, TEXT("projectorOnGpu"), OutInfo.Config.Load.bProjectorOnGpu);
    OutInfo.Config.Load.bWarmupProjector = ReadBool(Load, TEXT("warmupProjector"), OutInfo.Config.Load.bWarmupProjector);

    const TSharedPtr<FJsonObject>* GenerationPtr = nullptr;
    const TSharedPtr<FJsonObject> Generation = Root->TryGetObjectField(TEXT("generation"), GenerationPtr) ? *GenerationPtr : nullptr;
    OutInfo.Config.Generation.MaxTokens = ReadInt(Generation, TEXT("maxTokens"), OutInfo.Config.Generation.MaxTokens);
    OutInfo.Config.Generation.Temperature = ReadFloat(Generation, TEXT("temperature"), OutInfo.Config.Generation.Temperature);
    OutInfo.Config.Generation.TopK = ReadInt(Generation, TEXT("topK"), OutInfo.Config.Generation.TopK);
    OutInfo.Config.Generation.TopP = ReadFloat(Generation, TEXT("topP"), OutInfo.Config.Generation.TopP);
    OutInfo.Config.Generation.MinP = ReadFloat(Generation, TEXT("minP"), OutInfo.Config.Generation.MinP);
    OutInfo.Config.Generation.Seed = ReadInt(Generation, TEXT("seed"), static_cast<int32>(OutInfo.Config.Generation.Seed));
    FString ReasoningModeError;
    FString ReasoningMode;
    if (Generation.IsValid() && Generation->TryGetStringField(TEXT("reasoningMode"), ReasoningMode))
    {
        if (ReasoningMode.Equals(TEXT("disabled"), ESearchCase::IgnoreCase))
            OutInfo.Config.Generation.ReasoningMode = ELocalLLMReasoningMode::Disabled;
        else if (ReasoningMode.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
            OutInfo.Config.Generation.ReasoningMode = ELocalLLMReasoningMode::Enabled;
        else if (ReasoningMode.Equals(TEXT("modelDefault"), ESearchCase::IgnoreCase) ||
            ReasoningMode.Equals(TEXT("default"), ESearchCase::IgnoreCase))
            OutInfo.Config.Generation.ReasoningMode = ELocalLLMReasoningMode::ModelDefault;
        else
            ReasoningModeError = TEXT("generation.reasoningMode must be disabled, enabled, or modelDefault");
    }

    TArray<FString> Errors;
    TArray<FString> Warnings;
    if (!ContextPresetError.IsEmpty()) Errors.Add(ContextPresetError);
    if (!ProjectorPolicyError.IsEmpty()) Errors.Add(ProjectorPolicyError);
    if (!ReasoningModeError.IsEmpty()) Errors.Add(ReasoningModeError);
    if (SchemaVersion != 1) Errors.Add(TEXT("schemaVersion must be 1"));
    if (OutInfo.Config.Id.IsEmpty()) Errors.Add(TEXT("id is required"));
    if (OutInfo.Config.ModelPath.IsEmpty()) Errors.Add(TEXT("files.model is required"));
    else if (!FPaths::FileExists(OutInfo.Config.ModelPath)) Errors.Add(TEXT("primary model file is missing"));
    if (OutInfo.Config.Load.ProjectorLoadPolicy != ELocalLLMProjectorLoadPolicy::Disabled &&
        (OutInfo.Config.Capabilities.bVision || OutInfo.Config.Capabilities.bAudioInput) && OutInfo.Config.MultimodalProjectorPath.IsEmpty())
        Warnings.Add(TEXT("optional multimodal projector is not configured; text remains available"));
    else if (!OutInfo.Config.MultimodalProjectorPath.IsEmpty() && !FPaths::FileExists(OutInfo.Config.MultimodalProjectorPath))
        Warnings.Add(TEXT("optional multimodal projector file is missing; text remains available"));
    if (OutInfo.Config.Capabilities.bSpeculativeDecoding && OutInfo.Config.DraftModelPath.IsEmpty())
        Errors.Add(TEXT("speculative decoding requires files.draftModel"));
    else if (!OutInfo.Config.DraftModelPath.IsEmpty() && !FPaths::FileExists(OutInfo.Config.DraftModelPath))
        Errors.Add(TEXT("draft model file is missing"));
    if (OutInfo.Config.Capabilities.bReasoning &&
        OutInfo.Config.Generation.ReasoningMode == ELocalLLMReasoningMode::Disabled &&
        OutInfo.Config.NoThinkAssistantPrefill.IsEmpty())
        Warnings.Add(TEXT("reasoning is disabled but chat.noThinkAssistantPrefill is not configured; direct-response mode cannot be guaranteed"));

#if LOCAL_MULTIMODAL_LLM_WITH_LLAMA
    OutInfo.bCompatible = Errors.IsEmpty();
#else
    Errors.Add(TEXT("plugin was compiled without llama.cpp support"));
    OutInfo.bCompatible = false;
#endif
    OutInfo.Status = Errors.IsEmpty()
        ? (Warnings.IsEmpty() ? TEXT("Ready") : FString::Printf(TEXT("Ready; %s"), *FString::Join(Warnings, TEXT("; "))))
        : FString::Join(Errors, TEXT("; "));
    return OutInfo.bCompatible;
}

FLocalLLMModelConfig FLocalLLMModelRegistry::MakeLegacyConfig(const FString& ModelPath)
{
    FLocalLLMModelConfig Config;
    Config.Id = FPaths::GetBaseFilename(ModelPath);
    Config.DisplayName = Config.Id;
    Config.ModelPath = FPaths::ConvertRelativePathToFull(ModelPath);
    return Config;
}
