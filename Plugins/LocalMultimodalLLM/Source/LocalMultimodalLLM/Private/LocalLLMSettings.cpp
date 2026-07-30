#include "LocalLLMSettings.h"

#include "Models/LocalLLMModelRegistry.h"
#include "ILocalSpeechToTextBackend.h"
#include "ILocalTextToSpeechBackend.h"

TArray<FString> ULocalLLMSettings::GetAvailableModelIds() const
{
    TArray<FString> Result;
    for (const FLocalLLMModelInfo& Info : FLocalLLMModelRegistry::Discover())
    {
        if (Info.bCompatible) Result.Add(Info.Config.Id);
    }
    return Result;
}

TArray<FString> ULocalLLMSettings::GetAvailableSpeechToTextProviders() const
{
    TArray<FString> Result{TEXT("none")};
    for (const FName Provider : FLocalSpeechToTextBackendRegistry::GetRegisteredProviders())
        Result.Add(Provider.ToString());
    return Result;
}

TArray<FString> ULocalLLMSettings::GetAvailableTextToSpeechProviders() const
{
    TArray<FString> Result{TEXT("none")};
    for (const FName Provider : FLocalTextToSpeechBackendRegistry::GetRegisteredProviders())
        Result.Add(Provider.ToString());
    return Result;
}
