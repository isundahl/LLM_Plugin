#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"

struct FLocalSpeechToTextResult
{
    FString Text;
    FString DetectedLanguage;
    float Confidence = -1.0f;
};

using FLocalSpeechToTextCancelCheck = TFunction<bool()>;

/** Provider-neutral worker-thread backend. Implement this in optional modules such as SherpaOnnx or WhisperCpp. */
class LOCALMULTIMODALLLM_API ILocalSpeechToTextBackend
{
public:
    virtual ~ILocalSpeechToTextBackend() = default;
    virtual bool Load(const FLocalLLMSpeechToTextConfig& Config, FString& OutError) = 0;
    virtual void Unload() = 0;
    virtual bool Transcribe(const FLocalLLMAudioInput& Audio, FLocalSpeechToTextResult& OutResult,
        FString& OutError, const FLocalSpeechToTextCancelCheck& IsCancelled) = 0;
};

using FLocalSpeechToTextBackendFactory = TFunction<TUniquePtr<ILocalSpeechToTextBackend>()>;

/** Thread-safe registry used by optional provider modules. Provider names are data-driven and do not require a core enum change. */
class LOCALMULTIMODALLLM_API FLocalSpeechToTextBackendRegistry
{
public:
    static bool Register(FName Provider, FLocalSpeechToTextBackendFactory Factory);
    static void Unregister(FName Provider);
    static TUniquePtr<ILocalSpeechToTextBackend> Create(FName Provider);
    static TArray<FName> GetRegisteredProviders();
};
