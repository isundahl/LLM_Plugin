#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LocalLLMDelegates.h"
#include "LocalLLMTypes.h"
#include "LocalLLMMicrophoneComponent.generated.h"

class ULocalLLMSubsystem;

/** Captures a microphone, segments speech locally, and routes utterances into a Local LLM character session. */
UCLASS(ClassGroup = (LocalLLM), meta = (BlueprintSpawnableComponent))
class LOCALMULTIMODALLLM_API ULocalLLMMicrophoneComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    ULocalLLMMicrophoneComponent();
    virtual ~ULocalLLMMicrophoneComponent() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Microphone")
    FLocalLLMMicrophoneConfig Config;

    /** Stored on the player-character component and included by SaveGame-aware serialization. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Local LLM|Speaker")
    FLocalLLMSpeakerProfile SpeakerProfile;

    /** Fires once after a submitted utterance has a final transcript. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Microphone")
    FLocalLLMUserSpeechCapturedDelegate OnUserSpeechCaptured;

    /** Detailed microphone routing for C++ integrations. Intentionally hidden from Blueprint. */
    FLocalLLMEventDelegate OnInternalMicrophoneEvent;

    /** Invalid SessionId selects the subsystem's default session. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone")
    bool StartListening(FGuid SessionId, const FString& Prompt = TEXT(""));

    /** Convenience button-press path: selects ManualButton mode and starts one utterance immediately. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone")
    bool StartPushToTalkRecording(FGuid SessionId, const FString& Prompt = TEXT(""));

    /** Stops capture. Optionally submits speech currently in progress even if VAD has not observed trailing silence. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone")
    void StopListening(bool bSubmitPendingSpeech = true);

    /** Convenience button-release path: submits all audio captured since StartPushToTalkRecording. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone")
    void StopPushToTalkRecordingAndSubmit();

    /** Restarts noise-floor calibration while listening. A non-positive duration uses Config.CalibrationSeconds. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone")
    bool RecalibrateNoiseFloor(float DurationSeconds = -1.0f);

    /** Manual override; also cancels calibration currently in progress. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone")
    void SetVoiceThresholdDb(float ThresholdDb);

    UFUNCTION(BlueprintPure, Category = "Local LLM|Microphone")
    float GetVoiceThresholdDb() const { return Config.VoiceThresholdDb; }

    /** Opens the microphone if needed, displays the script in the event, records a fixed sample, and creates the profile. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speaker")
    bool EnrollPlayerSpeakerProfile(FGuid InSessionId, const FString& Script = TEXT(""), float DurationSeconds = -1.0f);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speaker")
    void ClearPlayerSpeakerProfile();

    UFUNCTION(BlueprintPure, Category = "Local LLM|Speaker")
    bool HasPlayerSpeakerProfile() const { return SpeakerProfile.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "Local LLM|Microphone") bool IsListening() const;

    /** Keeps capture open but discards samples and resets VAD. Useful while an NPC is speaking. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone")
    void SetInputSuppressed(bool bSuppressed);

    UFUNCTION(BlueprintPure, Category = "Local LLM|Microphone")
    bool IsInputSuppressed() const;

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Microphone") TArray<FLocalLLMMicrophoneDevice> GetAvailableDevices();

private:
    UFUNCTION() void HandleSubsystemEvent(const FLocalLLMEvent& Event);
    void ProcessCapturedAudio();
    void FinishNoiseCalibration();
    void BeginPendingSpeakerEnrollment();
    void SubmitAudioAfterSpeakerCheck(FLocalLLMAudioInput&& Audio, bool bForcedByMaximumDuration);
    void SubmitUtterance(FLocalLLMAudioInput&& Audio, bool bForcedByMaximumDuration);
    void BroadcastLocalEvent(ELocalLLMEventType Type, const FString& Text = {}, const FGuid& RequestId = {});

    struct FImpl;
    TUniquePtr<FImpl> Impl;
    TWeakObjectPtr<ULocalLLMSubsystem> Subsystem;
    FGuid SessionId;
    FGuid ActivePartialRequestId;
    FGuid ActiveEnrollmentRequestId;
    TSet<FGuid> PendingUtteranceRequests;
    FString Prompt;
    double LastPartialRequestTime = 0.0;
    float LastMeasuredNoiseFloorDb = -96.0f;
    float LastSpeakerSimilarity = -1.0f;
    TMap<FGuid, FLocalLLMAudioInput> PendingSpeakerChecks;
};
