#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LocalLLMTypes.h"
#include "LocalLLMSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Local Multimodal LLM"))
class LOCALMULTIMODALLLM_API ULocalLLMSettings final : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category = "Backend")
    ELocalLLMBackend Backend = ELocalLLMBackend::Mock;

    UPROPERTY(Config, EditAnywhere, Category = "Model", meta = (GetOptions = "GetAvailableModelIds"))
    FString DefaultModelId;

    UPROPERTY(Config, EditAnywhere, Category = "Model")
    TArray<FDirectoryPath> AdditionalModelDirectories;

    UPROPERTY(Config, EditAnywhere, Category = "Model", meta = (FilePathFilter = "gguf", DisplayName = "Legacy Default Model Path"))
    FFilePath DefaultModelPath;

    UPROPERTY(Config, EditAnywhere, Category = "Generation", meta = (ClampMin = "1", ClampMax = "32768"))
    int32 MaxGeneratedTokens = 256;

    /** Disabled is recommended for latency-sensitive spoken dialogue. Model Default leaves the GGUF chat template untouched. */
    UPROPERTY(Config, EditAnywhere, Category = "Generation")
    ELocalLLMReasoningMode ReasoningMode = ELocalLLMReasoningMode::Disabled;

    UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "1.0", ClampMax = "30.0"))
    float MaxAudioSeconds = 30.0f;

    UPROPERTY(Config, EditAnywhere, Category = "Audio")
    ELocalLLMAudioInputStrategy AudioInputStrategy = ELocalLLMAudioInputStrategy::Auto;

    /** Optional fallback used when the loaded LLM has no native audio projector. Providers are registered by separate modules. */
    UPROPERTY(Config, EditAnywhere, Category = "Audio")
    FLocalLLMSpeechToTextConfig SpeechToText;

    /** Optional provider-neutral speech synthesis defaults. Components may override this configuration per actor. */
    UPROPERTY(Config, EditAnywhere, Category = "Audio")
    FLocalLLMTextToSpeechConfig TextToSpeech;

    UFUNCTION()
    TArray<FString> GetAvailableModelIds() const;

    UFUNCTION()
    TArray<FString> GetAvailableSpeechToTextProviders() const;

    UFUNCTION()
    TArray<FString> GetAvailableTextToSpeechProviders() const;

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
