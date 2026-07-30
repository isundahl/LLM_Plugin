#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"

struct FLocalTextToSpeechResult
{
    FLocalLLMAudioInput Audio;
    FString VoiceId;
};

using FLocalTextToSpeechCancelCheck = TFunction<bool()>;
using FLocalTextToSpeechChunkCallback = TFunction<void(const FLocalLLMAudioChunk&)>;

/** Provider-neutral worker-thread backend. Optional modules such as PocketTTS register an implementation. */
class LOCALMULTIMODALLLM_API ILocalTextToSpeechBackend
{
public:
    virtual ~ILocalTextToSpeechBackend() = default;
    virtual bool Load(const FLocalLLMTextToSpeechConfig& Config, FString& OutError) = 0;
    virtual void Unload() = 0;
    /**
     * Primes provider execution and voice-specific state without publishing PCM. Providers with a cheaper
     * native preparation path may override this; the default performs one discarded synthesis pass.
     */
    virtual bool PrewarmVoice(const FLocalLLMTextToSpeechRequest& Request, FString& OutError,
        const FLocalTextToSpeechCancelCheck& IsCancelled)
    {
        FLocalTextToSpeechResult Discarded;
        return Synthesize(Request, Discarded, OutError, IsCancelled,
            [](const FLocalLLMAudioChunk&) {});
    }
    virtual bool Synthesize(const FLocalLLMTextToSpeechRequest& Request, FLocalTextToSpeechResult& OutResult,
        FString& OutError, const FLocalTextToSpeechCancelCheck& IsCancelled,
        const FLocalTextToSpeechChunkCallback& OnChunk) = 0;
};

using FLocalTextToSpeechBackendFactory = TFunction<TUniquePtr<ILocalTextToSpeechBackend>()>;

/** Thread-safe registry. Provider names are data-driven and do not require a core enum change. */
class LOCALMULTIMODALLLM_API FLocalTextToSpeechBackendRegistry
{
public:
    static bool Register(FName Provider, FLocalTextToSpeechBackendFactory Factory);
    static void Unregister(FName Provider);
    static TUniquePtr<ILocalTextToSpeechBackend> Create(FName Provider);
    static TArray<FName> GetRegisteredProviders();
};
