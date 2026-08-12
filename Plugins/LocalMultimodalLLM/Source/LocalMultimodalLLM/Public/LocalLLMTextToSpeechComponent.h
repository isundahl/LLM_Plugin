#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LocalLLMDelegates.h"
#include "LocalLLMTypes.h"
#include "TimerManager.h"
#include "LocalLLMTextToSpeechComponent.generated.h"

class UAudioBus;
class UAudioComponent;
class USoundAttenuation;
class USoundClass;
class USoundWaveProcedural;

/** Asynchronously synthesizes provider-neutral float PCM and can play it directly on the owning actor. */
UCLASS(ClassGroup = (LocalLLM), meta = (BlueprintSpawnableComponent))
class LOCALMULTIMODALLLM_API ULocalLLMTextToSpeechComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    ULocalLLMTextToSpeechComponent();
    virtual ~ULocalLLMTextToSpeechComponent() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech")
    FLocalLLMTextToSpeechConfig Config;

    /** Load the provider during BeginPlay to avoid first-line latency. Disabled by default to preserve resources. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech")
    bool bInitializeOnBeginPlay = false;

    /** Stream synthesized PCM through a procedural audio component attached to this component's owner. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech|Playback")
    bool bAutoPlayAudio = true;

    /** Permit actor-relative spatialization. Assign an attenuation asset to control range and falloff. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech|Playback")
    bool bSpatializePlayback = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech|Playback",
        meta = (ClampMin = "0.0", ClampMax = "4.0"))
    float PlaybackVolumeMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech|Playback")
    TObjectPtr<USoundAttenuation> PlaybackAttenuationSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech|Playback")
    TObjectPtr<USoundClass> PlaybackSoundClass;

    /** Optional pre-effect send. A MetaSound can read this Audio Bus for analysis, effects, or alternate routing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech|Playback")
    TObjectPtr<UAudioBus> MetaSoundAudioBus;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Text To Speech|Playback",
        meta = (EditCondition = "MetaSoundAudioBus != nullptr", ClampMin = "0.0", ClampMax = "1.0"))
    float MetaSoundAudioBusSendLevel = 1.0f;

    /** One stream for initialization, synthesis, PCM chunks, completion, cancellation, and errors. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Text To Speech")
    FLocalLLMEventDelegate OnTextToSpeechEvent;

    /** Asynchronously creates and loads Config.Provider. Returns an invalid ID when the request is rejected immediately. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech")
    FGuid InitializeTextToSpeech();

    /**
     * Silently primes the configured voice. Safe to call repeatedly; subsequent calls are no-ops until the
     * provider configuration changes. If initialization is busy, the warmup is deferred automatically.
     */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech")
    bool PrewarmVoice(FName CharacterId);

    /** Lazily initializes when necessary, then streams TextToSpeechChunk events and a final complete PCM event. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech")
    FGuid SynthesizeSpeech(FLocalLLMTextToSpeechRequest Request, FGuid SessionId, FName CharacterId);

    /** Queues a validated sentence and begins it immediately when the provider is idle. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech")
    bool QueueSpeech(FLocalLLMTextToSpeechRequest Request, FGuid SessionId, FName CharacterId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech")
    void ClearQueuedSpeech();

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech")
    void CancelSpeechSynthesis();

    /** Immediately stops audible speech and discards PCM already buffered for playback. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech|Playback")
    void StopSpeechPlayback();

    /** Explicitly queues provider-neutral float PCM through the same actor-attached playback route. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech|Playback")
    bool PlayAudioChunk(FLocalLLMAudioChunk Audio);

    UFUNCTION(BlueprintPure, Category = "Local LLM|Text To Speech") bool IsTextToSpeechReady() const;
    UFUNCTION(BlueprintPure, Category = "Local LLM|Text To Speech") bool IsSpeechSynthesisBusy() const;
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Text To Speech") TArray<FName> GetAvailableTextToSpeechProviders() const;
    UFUNCTION(BlueprintPure, Category = "Local LLM|Text To Speech|Playback") bool IsSpeechPlaybackActive() const;
    UFUNCTION(BlueprintPure, Category = "Local LLM|Text To Speech|Playback") UAudioComponent* GetSpeechAudioComponent() const;
    UFUNCTION(BlueprintPure, Category = "Local LLM|Text To Speech|Playback") USoundWaveProcedural* GetSpeechSoundWave() const;

private:
    struct FQueuedSpeechRequest
    {
        FLocalLLMTextToSpeechRequest Request;
        FGuid SessionId;
        FName CharacterId;
        bool bSuppressOnsetFade = false;
    };

    void StartNextQueuedSpeech();
    void DrainPendingPrewarmOrSpeech();
    void BroadcastEvent(ELocalLLMEventType Type, const FGuid& RequestId, const FGuid& SessionId,
        FName CharacterId, const FString& Text = {}, const FLocalLLMAudioChunk* Audio = nullptr,
        const FString& VoiceId = {});
    void HandleAutomaticPlayback(ELocalLLMEventType Type, const FGuid& RequestId,
        const FLocalLLMAudioChunk* Audio);
    bool QueuePlaybackAudio(const FLocalLLMAudioChunk& Audio);
    bool EnsurePlaybackObjects(int32 SampleRate, int32 NumChannels);
    void ApplyPlaybackSettings();
    void DestroyPlaybackObjects();
    void ArmSynthesisWatchdog(const FGuid& RequestId, const FGuid& SessionId, FName CharacterId);
    void ClearSynthesisWatchdog(const FGuid& RequestId);
    void HandleSynthesisWatchdogExpired();

    struct FImpl;
    TSharedPtr<FImpl, ESPMode::ThreadSafe> Impl;
    FGuid ActiveRequestId;
    TArray<FQueuedSpeechRequest> QueuedSpeech;
    bool bPendingVoicePrewarm = false;
    FName PendingPrewarmCharacterId;
    TSet<FGuid> RequestsWithStreamedAudio;
    TSet<FGuid> RequestsNeedingLeadingPause;
    bool bInsertPauseBeforeNextSpeech = false;
    bool bSuppressOnsetFadeForNextSynthesis = false;
    TSet<FGuid> TimedOutRequests;
    FTimerHandle SynthesisWatchdogTimer;
    FGuid WatchdogRequestId;
    FGuid WatchdogSessionId;
    FName WatchdogCharacterId;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> SpeechAudioComponent;

    UPROPERTY(Transient)
    TObjectPtr<USoundWaveProcedural> SpeechSoundWave;

    UPROPERTY(Transient)
    TObjectPtr<UAudioBus> AppliedMetaSoundAudioBus;

    int32 PlaybackSampleRate = 0;
    int32 PlaybackNumChannels = 0;
    /** Wall-clock end of the PCM submitted to the procedural wave. Unreal can keep
     * a starved procedural audio component in IsPlaying() indefinitely. */
    double EstimatedPlaybackEndAt = 0.0;
};
