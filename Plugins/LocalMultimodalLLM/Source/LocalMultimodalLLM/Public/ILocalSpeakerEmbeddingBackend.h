#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"

using FLocalSpeakerEmbeddingCancelCheck = TFunction<bool()>;

/** Optional worker-thread provider for model-specific speaker embeddings. */
class LOCALMULTIMODALLLM_API ILocalSpeakerEmbeddingBackend
{
public:
    virtual ~ILocalSpeakerEmbeddingBackend() = default;
    virtual bool Load(const FLocalLLMSpeakerVerificationConfig& Config, FString& OutError) = 0;
    virtual void Unload() = 0;
    virtual FString GetModelId() const = 0;
    virtual bool ExtractEmbedding(const FLocalLLMAudioInput& Audio, TArray<float>& OutEmbedding,
        FString& OutError, const FLocalSpeakerEmbeddingCancelCheck& IsCancelled) = 0;
};

using FLocalSpeakerEmbeddingBackendFactory = TFunction<TUniquePtr<ILocalSpeakerEmbeddingBackend>()>;

class LOCALMULTIMODALLLM_API FLocalSpeakerEmbeddingBackendRegistry
{
public:
    static bool Register(FName Provider, FLocalSpeakerEmbeddingBackendFactory Factory);
    static void Unregister(FName Provider);
    static TUniquePtr<ILocalSpeakerEmbeddingBackend> Create(FName Provider);
};
