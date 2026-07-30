#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"

class FLocalLLMModelRegistry
{
public:
    static TArray<FLocalLLMModelInfo> Discover();
    static bool FindById(const FString& ModelId, FLocalLLMModelInfo& OutInfo);
    static bool LoadManifest(const FString& ManifestPath, FLocalLLMModelInfo& OutInfo);
    static FLocalLLMModelConfig MakeLegacyConfig(const FString& ModelPath);
};
