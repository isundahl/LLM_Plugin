#include "LocalLLMMicrophoneComponent.h"

#include "AudioCaptureCore.h"
#include "Containers/Queue.h"
#include "Engine/GameInstance.h"
#include "LocalLLMSubsystem.h"
#include "Speech/LocalVoiceActivityDetector.h"

DEFINE_LOG_CATEGORY_STATIC(LogLocalLLMMicrophone, Log, All);

namespace
{
struct FCapturedMicrophoneChunk
{
    TArray<float> Samples;
    int32 SampleRate = 0;
    int32 NumChannels = 0;
};

struct FCaptureSharedState : TSharedFromThis<FCaptureSharedState, ESPMode::ThreadSafe>
{
    TQueue<FCapturedMicrophoneChunk, EQueueMode::Spsc> Chunks;
    TAtomic<bool> bAccepting = false;
    TAtomic<bool> bOverflowed = false;
};
}

struct ULocalLLMMicrophoneComponent::FImpl
{
    Audio::FAudioCapture Capture;
    TSharedPtr<FCaptureSharedState, ESPMode::ThreadSafe> Shared = MakeShared<FCaptureSharedState, ESPMode::ThreadSafe>();
    FLocalLLMVoiceActivityDetector Vad;
    bool bListening = false;
    bool bCalibrating = false;
    float CalibrationDurationSeconds = 0.0f;
    int64 CalibrationFrames = 0;
    TArray<float> CalibrationBlockLevelsDb;
    bool bEnrollmentPending = false;
    bool bEnrolling = false;
    bool bInputSuppressed = false;
    bool bStopAfterEnrollment = false;
    float EnrollmentDurationSeconds = 0.0f;
    FString EnrollmentScript;
    FLocalLLMAudioInput EnrollmentAudio;
    FLocalLLMAudioInput ManualAudio;
    bool bManualMaximumReached = false;
    int64 ManualActiveFrames = 0;
    float ManualPeakBlockDb = -96.0f;
};

ULocalLLMMicrophoneComponent::ULocalLLMMicrophoneComponent()
    : Impl(new FImpl())
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

ULocalLLMMicrophoneComponent::~ULocalLLMMicrophoneComponent()
{
    delete Impl;
    Impl = nullptr;
}

void ULocalLLMMicrophoneComponent::BeginPlay()
{
    Super::BeginPlay();
    UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    Subsystem = GameInstance ? GameInstance->GetSubsystem<ULocalLLMSubsystem>() : nullptr;
    if (ULocalLLMSubsystem* LocalSubsystem = Subsystem.Get())
        LocalSubsystem->OnInternalEvent.AddUniqueDynamic(this, &ULocalLLMMicrophoneComponent::HandleSubsystemEvent);
}

void ULocalLLMMicrophoneComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopListening(false);
    if (ULocalLLMSubsystem* LocalSubsystem = Subsystem.Get())
        LocalSubsystem->OnInternalEvent.RemoveDynamic(this, &ULocalLLMMicrophoneComponent::HandleSubsystemEvent);
    Subsystem.Reset();
    Super::EndPlay(EndPlayReason);
}

bool ULocalLLMMicrophoneComponent::StartListening(FGuid InSessionId, const FString& InPrompt)
{
    if (Impl->bListening) return true;
    UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    ULocalLLMSubsystem* LocalSubsystem = GameInstance ? GameInstance->GetSubsystem<ULocalLLMSubsystem>() : nullptr;
    if (!LocalSubsystem)
    {
        BroadcastLocalEvent(ELocalLLMEventType::Error, TEXT("Local LLM subsystem is unavailable"));
        return false;
    }
    if (!InSessionId.IsValid()) InSessionId = LocalSubsystem->GetDefaultSessionId();

    Audio::FCaptureDeviceInfo DeviceInfo;
    if (!Impl->Capture.GetCaptureDeviceInfo(DeviceInfo, Config.DeviceIndex))
    {
        BroadcastLocalEvent(ELocalLLMEventType::Error, TEXT("Could not query the selected microphone device"));
        return false;
    }

    Impl->Vad.SetConfig(Config);
    Impl->ManualAudio = {};
    Impl->bManualMaximumReached = false;
    Impl->ManualActiveFrames = 0;
    Impl->ManualPeakBlockDb = -96.0f;
    Impl->Shared->bOverflowed.Store(false);
    Impl->Shared->bAccepting.Store(true);
    Audio::FAudioCaptureDeviceParams Params;
    Params.DeviceIndex = Config.DeviceIndex;
    Params.NumInputChannels = Audio::InvalidDeviceChannelCount;
    Params.SampleRate = Audio::InvalidDeviceSampleRate;
    Params.PCMAudioEncoding = Audio::EPCMAudioEncoding::FLOATING_POINT_32;
    Params.bUseHardwareAEC = Config.bUseHardwareEchoCancellation;
    const TSharedPtr<FCaptureSharedState, ESPMode::ThreadSafe> Shared = Impl->Shared;
    const bool bOpened = Impl->Capture.OpenAudioCaptureStream(Params,
        [Shared](const void* Buffer, const int32 NumFrames, const int32 NumChannels, const int32 SampleRate, double, const bool bOverflow)
        {
            if (!Shared->bAccepting.Load() || !Buffer || NumFrames <= 0 || NumChannels <= 0) return;
            FCapturedMicrophoneChunk Chunk;
            Chunk.SampleRate = SampleRate;
            Chunk.NumChannels = NumChannels;
            Chunk.Samples.Append(static_cast<const float*>(Buffer), NumFrames * NumChannels);
            Shared->Chunks.Enqueue(MoveTemp(Chunk));
            if (bOverflow) Shared->bOverflowed.Store(true);
        }, 1024);
    if (!bOpened || !Impl->Capture.StartStream())
    {
        Impl->Shared->bAccepting.Store(false);
        if (Impl->Capture.IsStreamOpen()) Impl->Capture.CloseStream();
        BroadcastLocalEvent(ELocalLLMEventType::Error, TEXT("Could not open or start the microphone capture stream"));
        return false;
    }

    Subsystem = LocalSubsystem;
    SessionId = InSessionId;
    Prompt = InPrompt;
    ActivePartialRequestId.Invalidate();
    LastPartialRequestTime = FPlatformTime::Seconds();
    LocalSubsystem->OnInternalEvent.AddUniqueDynamic(this, &ULocalLLMMicrophoneComponent::HandleSubsystemEvent);
    Impl->bListening = true;
    SetComponentTickEnabled(true);
    BroadcastLocalEvent(ELocalLLMEventType::MicrophoneStarted, DeviceInfo.DeviceName);
    if (Config.SegmentationMode == ELocalLLMMicrophoneSegmentationMode::ManualButton)
        BroadcastLocalEvent(ELocalLLMEventType::SpeechStarted, TEXT("Manual recording started"));
    else if (Config.bAutoCalibrateNoiseFloor)
        RecalibrateNoiseFloor(Config.CalibrationSeconds);
    return true;
}

bool ULocalLLMMicrophoneComponent::StartPushToTalkRecording(
    const FGuid InSessionId, const FString& InPrompt)
{
    Config.SegmentationMode = ELocalLLMMicrophoneSegmentationMode::ManualButton;
    return StartListening(InSessionId, InPrompt);
}

void ULocalLLMMicrophoneComponent::StopListening(const bool bSubmitPendingSpeech)
{
    if (!Impl || !Impl->bListening) return;
    // Keep accepting buffers until WASAPI has actually stopped. Disabling the callback first can
    // discard the last in-flight capture block, which is especially visible with push-to-talk.
    Impl->Capture.StopStream();
    Impl->Shared->bAccepting.Store(false);
    Impl->Capture.CloseStream();
    ProcessCapturedAudio();
    Impl->bCalibrating = false;
    Impl->CalibrationBlockLevelsDb.Reset();
    Impl->bEnrollmentPending = false;
    Impl->bEnrolling = false;
    Impl->EnrollmentAudio = {};
    if (bSubmitPendingSpeech)
    {
        FLocalLLMAudioInput Pending = Config.SegmentationMode == ELocalLLMMicrophoneSegmentationMode::ManualButton
            ? MoveTemp(Impl->ManualAudio)
            : Impl->Vad.Flush(true);
        const bool bManual = Config.SegmentationMode == ELocalLLMMicrophoneSegmentationMode::ManualButton;
        if (Pending.IsValid())
        {
            const int64 Frames = Pending.Samples.Num() / FMath::Max(1, Pending.NumChannels);
            const int64 MinimumFrames = static_cast<int64>(Pending.SampleRate) * Config.MinimumUtteranceMilliseconds / 1000;
            const int64 MinimumActiveFrames = static_cast<int64>(Pending.SampleRate) *
                Config.ManualMinimumActiveMilliseconds / 1000;
            const bool bHasManualActivity = !bManual || !Config.bRejectSilentManualRecordings ||
                Impl->ManualActiveFrames >= MinimumActiveFrames;
            BroadcastLocalEvent(ELocalLLMEventType::SpeechEnded,
                bManual
                    ? TEXT("Manual recording stopped") : TEXT("Listening stopped during an utterance"));
            if (Frames < MinimumFrames)
            {
                BroadcastLocalEvent(ELocalLLMEventType::InputRejected, TEXT("Manual recording was too short"));
            }
            else if (!bHasManualActivity)
            {
                const FString Reason = FString::Printf(
                    TEXT("Manual recording contained no sustained speech activity (peak block %.1f dBFS)"),
                    Impl->ManualPeakBlockDb);
                BroadcastLocalEvent(ELocalLLMEventType::InputRejected, Reason);
                UE_LOG(LogLocalLLMMicrophone, Display, TEXT("%s; STT submission skipped"), *Reason);
            }
            else
            {
                SubmitUtterance(MoveTemp(Pending), Impl->bManualMaximumReached);
            }
        }
        else if (bManual)
        {
            BroadcastLocalEvent(ELocalLLMEventType::SpeechEnded, TEXT("Manual recording stopped"));
            BroadcastLocalEvent(ELocalLLMEventType::InputRejected, TEXT("Manual recording contained no audio"));
        }
    }
    Impl->ManualAudio = {};
    Impl->bManualMaximumReached = false;
    Impl->ManualActiveFrames = 0;
    Impl->ManualPeakBlockDb = -96.0f;
    Impl->Vad.Reset();
    Impl->bListening = false;
    SetComponentTickEnabled(false);
    BroadcastLocalEvent(ELocalLLMEventType::MicrophoneStopped);
}

void ULocalLLMMicrophoneComponent::StopPushToTalkRecordingAndSubmit()
{
    StopListening(true);
}

bool ULocalLLMMicrophoneComponent::RecalibrateNoiseFloor(const float DurationSeconds)
{
    if (!Impl || !Impl->bListening)
    {
        BroadcastLocalEvent(ELocalLLMEventType::Error, TEXT("Noise-floor calibration requires active microphone capture"));
        return false;
    }
    Impl->Vad.Reset();
    Impl->bCalibrating = true;
    Impl->CalibrationDurationSeconds = FMath::Clamp(
        DurationSeconds > 0.0f ? DurationSeconds : Config.CalibrationSeconds, 0.5f, 10.0f);
    Impl->CalibrationFrames = 0;
    Impl->CalibrationBlockLevelsDb.Reset();
    FLocalLLMEvent Event;
    Event.Type = ELocalLLMEventType::MicrophoneCalibrationStarted;
    Event.SessionId = SessionId;
    Event.VoiceThresholdDb = Config.VoiceThresholdDb;
    Event.Text = FString::Printf(TEXT("Remain quiet for %.1f seconds"), Impl->CalibrationDurationSeconds);
    OnInternalMicrophoneEvent.Broadcast(Event);
    return true;
}

void ULocalLLMMicrophoneComponent::SetVoiceThresholdDb(const float ThresholdDb)
{
    Config.VoiceThresholdDb = FMath::Clamp(ThresholdDb, -80.0f, -5.0f);
    if (!Impl) return;
    Impl->bCalibrating = false;
    Impl->CalibrationBlockLevelsDb.Reset();
    Impl->Vad.SetConfig(Config);
    FLocalLLMEvent Event;
    Event.Type = ELocalLLMEventType::MicrophoneCalibrationCompleted;
    Event.SessionId = SessionId;
    Event.VoiceThresholdDb = Config.VoiceThresholdDb;
    Event.Text = TEXT("Voice threshold manually overridden");
    OnInternalMicrophoneEvent.Broadcast(Event);
    BeginPendingSpeakerEnrollment();
}

bool ULocalLLMMicrophoneComponent::EnrollPlayerSpeakerProfile(
    FGuid InSessionId, const FString& Script, const float DurationSeconds)
{
    if (!Impl || Impl->bEnrollmentPending || Impl->bEnrolling) return false;
    Impl->bEnrollmentPending = true;
    Impl->EnrollmentDurationSeconds = FMath::Clamp(
        DurationSeconds > 0.0f ? DurationSeconds : Config.SpeakerVerification.EnrollmentSeconds, 3.0f, 30.0f);
    Impl->EnrollmentScript = Script.IsEmpty() ? Config.SpeakerVerification.EnrollmentScript : Script;
    Impl->bStopAfterEnrollment = !IsListening();
    if (!IsListening() && !StartListening(InSessionId))
    {
        Impl->bEnrollmentPending = false;
        return false;
    }
    if (InSessionId.IsValid()) SessionId = InSessionId;
    if (!Impl->bCalibrating) BeginPendingSpeakerEnrollment();
    return true;
}

void ULocalLLMMicrophoneComponent::ClearPlayerSpeakerProfile()
{
    SpeakerProfile = {};
}

bool ULocalLLMMicrophoneComponent::IsListening() const
{
    return Impl && Impl->bListening;
}

void ULocalLLMMicrophoneComponent::SetInputSuppressed(const bool bSuppressed)
{
    if (!Impl || Impl->bInputSuppressed == bSuppressed) return;
    Impl->bInputSuppressed = bSuppressed;
    Impl->Vad.Reset();
    ActivePartialRequestId.Invalidate();
    UE_LOG(LogLocalLLMMicrophone, Display, TEXT("Microphone input %s for session %s"),
        bSuppressed ? TEXT("suppressed") : TEXT("resumed"),
        *SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
}

bool ULocalLLMMicrophoneComponent::IsInputSuppressed() const
{
    return Impl && Impl->bInputSuppressed;
}

TArray<FLocalLLMMicrophoneDevice> ULocalLLMMicrophoneComponent::GetAvailableDevices()
{
    TArray<FLocalLLMMicrophoneDevice> Result;
    TArray<Audio::FCaptureDeviceInfo> Devices;
    Impl->Capture.GetCaptureDevicesAvailable(Devices);
    for (int32 Index = 0; Index < Devices.Num(); ++Index)
    {
        FLocalLLMMicrophoneDevice Device;
        Device.DeviceIndex = Index;
        Device.Name = Devices[Index].DeviceName;
        Device.DeviceId = Devices[Index].DeviceId;
        Device.InputChannels = Devices[Index].InputChannels;
        Device.PreferredSampleRate = Devices[Index].PreferredSampleRate;
        Device.bSupportsHardwareEchoCancellation = Devices[Index].bSupportsHardwareAEC;
        Result.Add(MoveTemp(Device));
    }
    return Result;
}

void ULocalLLMMicrophoneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ProcessCapturedAudio();
}

void ULocalLLMMicrophoneComponent::ProcessCapturedAudio()
{
    if (!Impl) return;
    if (Impl->Shared->bOverflowed.Exchange(false))
        BroadcastLocalEvent(ELocalLLMEventType::Error, TEXT("The microphone input overflowed; some samples were dropped"));

    FCapturedMicrophoneChunk Chunk;
    while (Impl->Shared->Chunks.Dequeue(Chunk))
    {
        if (Impl->bInputSuppressed) continue;
        if (Impl->bCalibrating)
        {
            Impl->CalibrationBlockLevelsDb.Add(FLocalLLMVoiceActivityDetector::CalculateRmsDb(
                Chunk.Samples.GetData(), Chunk.Samples.Num()));
            Impl->CalibrationFrames += Chunk.Samples.Num() / FMath::Max(1, Chunk.NumChannels);
            if (Impl->CalibrationFrames >= static_cast<int64>(Chunk.SampleRate * Impl->CalibrationDurationSeconds))
                FinishNoiseCalibration();
            continue;
        }
        if (Impl->bEnrolling)
        {
            if (Impl->EnrollmentAudio.Samples.IsEmpty())
            {
                Impl->EnrollmentAudio.SampleRate = Chunk.SampleRate;
                Impl->EnrollmentAudio.NumChannels = Chunk.NumChannels;
            }
            if (Impl->EnrollmentAudio.SampleRate == Chunk.SampleRate && Impl->EnrollmentAudio.NumChannels == Chunk.NumChannels)
                Impl->EnrollmentAudio.Samples.Append(Chunk.Samples);
            const int64 EnrollmentFrames = Impl->EnrollmentAudio.Samples.Num() / FMath::Max(1, Impl->EnrollmentAudio.NumChannels);
            if (EnrollmentFrames >= static_cast<int64>(Impl->EnrollmentAudio.SampleRate * Impl->EnrollmentDurationSeconds))
            {
                Impl->bEnrolling = false;
                if (ULocalLLMSubsystem* LocalSubsystem = Subsystem.Get())
                {
                    ActiveEnrollmentRequestId = LocalSubsystem->CreateSpeakerProfileForSession(
                        SessionId, Impl->EnrollmentAudio, Config.SpeakerVerification, TEXT("Player"));
                }
                Impl->EnrollmentAudio = {};
                Impl->Vad.SetConfig(Config);
            }
            continue;
        }
        if (Config.SegmentationMode == ELocalLLMMicrophoneSegmentationMode::ManualButton)
        {
            if (Impl->ManualAudio.Samples.IsEmpty())
            {
                Impl->ManualAudio.SampleRate = Chunk.SampleRate;
                Impl->ManualAudio.NumChannels = Chunk.NumChannels;
            }
            if (Impl->ManualAudio.SampleRate == Chunk.SampleRate &&
                Impl->ManualAudio.NumChannels == Chunk.NumChannels && !Impl->bManualMaximumReached)
            {
                const float BlockDb = FLocalLLMVoiceActivityDetector::CalculateRmsDb(
                    Chunk.Samples.GetData(), Chunk.Samples.Num());
                Impl->ManualPeakBlockDb = FMath::Max(Impl->ManualPeakBlockDb, BlockDb);
                if (BlockDb >= Config.ManualActivityThresholdDb)
                    Impl->ManualActiveFrames += Chunk.Samples.Num() / FMath::Max(1, Chunk.NumChannels);
                const int64 MaximumSamples = static_cast<int64>(Chunk.SampleRate) * Chunk.NumChannels *
                    FMath::Max(1.0f, Config.MaximumUtteranceSeconds);
                const int64 Remaining = MaximumSamples - Impl->ManualAudio.Samples.Num();
                if (Remaining > 0)
                    Impl->ManualAudio.Samples.Append(Chunk.Samples.GetData(),
                        static_cast<int32>(FMath::Min<int64>(Remaining, Chunk.Samples.Num())));
                Impl->bManualMaximumReached = Impl->ManualAudio.Samples.Num() >= MaximumSamples;
            }
            continue;
        }
        FLocalLLMVadUpdate Update = Impl->Vad.Process(
            Chunk.Samples.GetData(), Chunk.Samples.Num(), Chunk.SampleRate, Chunk.NumChannels);
        if (Update.bSpeechStarted)
        {
            LastPartialRequestTime = FPlatformTime::Seconds();
            BroadcastLocalEvent(ELocalLLMEventType::SpeechStarted);
        }
        if (Update.bSpeechEnded)
        {
            BroadcastLocalEvent(ELocalLLMEventType::SpeechEnded,
                Update.bMaximumDurationReached ? TEXT("Maximum utterance duration reached") : FString());
            if (Update.CompletedUtterance.IsValid())
                SubmitUtterance(MoveTemp(Update.CompletedUtterance), Update.bMaximumDurationReached);
        }
    }

    const double Now = FPlatformTime::Seconds();
    if (Impl->Vad.IsSpeechActive() && Config.bEmitPartialTranscripts &&
        !Config.SpeakerVerification.bUseSpeakerProfile && !ActivePartialRequestId.IsValid() &&
        Now - LastPartialRequestTime >= Config.PartialTranscriptIntervalSeconds)
    {
        if (ULocalLLMSubsystem* LocalSubsystem = Subsystem.Get())
        {
            FLocalLLMAudioInput Snapshot = Impl->Vad.Snapshot();
            if (Snapshot.IsValid())
            {
                ActivePartialRequestId = LocalSubsystem->TranscribeAudioForSession(SessionId, Snapshot);
                LastPartialRequestTime = Now;
            }
        }
    }
}

void ULocalLLMMicrophoneComponent::BeginPendingSpeakerEnrollment()
{
    if (!Impl || !Impl->bEnrollmentPending) return;
    Impl->bEnrollmentPending = false;
    Impl->bEnrolling = true;
    Impl->EnrollmentAudio = {};
    Impl->Vad.Reset();
    FLocalLLMEvent Event;
    Event.Type = ELocalLLMEventType::SpeakerEnrollmentStarted;
    Event.SessionId = SessionId;
    Event.Text = Impl->EnrollmentScript;
    OnInternalMicrophoneEvent.Broadcast(Event);
}

void ULocalLLMMicrophoneComponent::FinishNoiseCalibration()
{
    if (!Impl || !Impl->bCalibrating) return;
    Impl->bCalibrating = false;
    float NoiseFloorDb = -96.0f;
    float ThresholdDb = Config.VoiceThresholdDb;
    if (!FLocalLLMVoiceActivityDetector::EstimateCalibratedThreshold(
        Impl->CalibrationBlockLevelsDb, Config.NoiseMarginDb, Config.MinimumAutoThresholdDb,
        Config.MaximumAutoThresholdDb, NoiseFloorDb, ThresholdDb))
    {
        Impl->CalibrationBlockLevelsDb.Reset();
        BroadcastLocalEvent(ELocalLLMEventType::Error, TEXT("Noise-floor calibration did not receive enough valid audio"));
        BeginPendingSpeakerEnrollment();
        return;
    }
    Config.VoiceThresholdDb = ThresholdDb;
    Impl->CalibrationBlockLevelsDb.Reset();
    Impl->Vad.SetConfig(Config);
    FLocalLLMEvent Event;
    Event.Type = ELocalLLMEventType::MicrophoneCalibrationCompleted;
    Event.SessionId = SessionId;
    Event.MeasuredNoiseFloorDb = NoiseFloorDb;
    Event.VoiceThresholdDb = ThresholdDb;
    Event.Text = FString::Printf(TEXT("Noise floor %.1f dBFS; voice threshold %.1f dBFS"), NoiseFloorDb, ThresholdDb);
    LastMeasuredNoiseFloorDb = NoiseFloorDb;
    OnInternalMicrophoneEvent.Broadcast(Event);
    BeginPendingSpeakerEnrollment();
}

void ULocalLLMMicrophoneComponent::SubmitUtterance(FLocalLLMAudioInput&& Audio, const bool bForcedByMaximumDuration)
{
    const double AudioSeconds = Audio.SampleRate > 0 && Audio.NumChannels > 0
        ? static_cast<double>(Audio.Samples.Num()) / (Audio.SampleRate * Audio.NumChannels) : 0.0;
    UE_LOG(LogLocalLLMMicrophone, Display,
        TEXT("Captured utterance for session %s: %.2f s, forced=%s"),
        *SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), AudioSeconds,
        bForcedByMaximumDuration ? TEXT("yes") : TEXT("no"));
    LastSpeakerSimilarity = -1.0f;
    FLocalLLMEvent Captured;
    Captured.Type = ELocalLLMEventType::UtteranceCaptured;
    Captured.SessionId = SessionId;
    Captured.Text = bForcedByMaximumDuration ? TEXT("Maximum utterance duration reached") : TEXT("Utterance captured");
    Captured.Audio.SampleRate = Audio.SampleRate;
    Captured.Audio.NumChannels = Audio.NumChannels;
    Captured.Audio.Samples = Audio.Samples;
    OnInternalMicrophoneEvent.Broadcast(Captured);

    if (!Config.bAutoSubmitFinalUtterance) return;
    if (Config.SpeakerVerification.bUseSpeakerProfile)
    {
        if (!SpeakerProfile.IsValid())
        {
            FLocalLLMEvent Warning;
            Warning.Type = ELocalLLMEventType::Warning;
            Warning.SessionId = SessionId;
            Warning.Text = TEXT("Speaker verification is enabled but no player profile exists; continuing without verification");
            OnInternalMicrophoneEvent.Broadcast(Warning);
        }
        else if (ULocalLLMSubsystem* LocalSubsystem = Subsystem.Get())
        {
            const FGuid RequestId = LocalSubsystem->VerifySpeakerForSession(
                SessionId, Audio, SpeakerProfile, Config.SpeakerVerification);
            PendingSpeakerChecks.Add(RequestId, MoveTemp(Audio));
            return;
        }
    }
    SubmitAudioAfterSpeakerCheck(MoveTemp(Audio), bForcedByMaximumDuration);
}

void ULocalLLMMicrophoneComponent::SubmitAudioAfterSpeakerCheck(
    FLocalLLMAudioInput&& Audio, const bool bForcedByMaximumDuration)
{
    if (ULocalLLMSubsystem* LocalSubsystem = Subsystem.Get())
    {
        const FGuid RequestId = Config.bAutoSubmitTranscriptToConversation
            ? LocalSubsystem->SubmitAudioForSession(SessionId, Audio, Prompt)
            : LocalSubsystem->TranscribeAudioForSession(SessionId, Audio);
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::UtteranceSubmitted;
        Event.RequestId = RequestId;
        Event.SessionId = SessionId;
        Event.Text = Config.bAutoSubmitTranscriptToConversation
            ? TEXT("Utterance submitted") : TEXT("Utterance submitted for transcription only");
        PendingUtteranceRequests.Add(RequestId);
        UE_LOG(LogLocalLLMMicrophone, Display,
            TEXT("Submitted utterance request %s for session %s"),
            *RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
            *SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
        OnInternalMicrophoneEvent.Broadcast(Event);
    }
}

void ULocalLLMMicrophoneComponent::HandleSubsystemEvent(const FLocalLLMEvent& Event)
{
    if (PendingUtteranceRequests.Contains(Event.RequestId))
    {
        // TranscribeAudioForSession is also used for periodic snapshots and therefore
        // emits TranscriptionPartial. A request tracked here is not a snapshot: it is a
        // finalized microphone utterance deliberately sent through transcription-only
        // mode so the game can apply contextual normalization before LLM submission.
        // Promote that event locally to the final boundary expected by microphone clients.
        if (Event.Type == ELocalLLMEventType::TranscriptionCompleted ||
            (!Config.bAutoSubmitTranscriptToConversation &&
             Event.Type == ELocalLLMEventType::TranscriptionPartial))
        {
            FLocalLLMEvent FinalEvent = Event;
            FinalEvent.Type = ELocalLLMEventType::TranscriptionCompleted;
            const FString& RawTranscript = Event.TranscriptNormalization.RawTranscript.IsEmpty()
                ? Event.Text
                : Event.TranscriptNormalization.RawTranscript;
            OnUserSpeechCaptured.Broadcast(RawTranscript, LastMeasuredNoiseFloorDb,
                Config.VoiceThresholdDb, LastSpeakerSimilarity);
            UE_LOG(LogLocalLLMMicrophone, Display,
                TEXT("Transcription completed for request %s: %d characters"),
                *Event.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), RawTranscript.Len());
            OnInternalMicrophoneEvent.Broadcast(FinalEvent);
            PendingUtteranceRequests.Remove(Event.RequestId);
        }
        else if (Event.Type == ELocalLLMEventType::Error)
        {
            OnInternalMicrophoneEvent.Broadcast(Event);
            PendingUtteranceRequests.Remove(Event.RequestId);
        }
    }

    if (ActiveEnrollmentRequestId.IsValid() && Event.RequestId == ActiveEnrollmentRequestId)
    {
        if (Event.Type == ELocalLLMEventType::SpeakerProfileCreated)
        {
            SpeakerProfile = Event.SpeakerProfile;
            OnInternalMicrophoneEvent.Broadcast(Event);
            ActiveEnrollmentRequestId.Invalidate();
            if (Impl->bStopAfterEnrollment) StopListening(false);
        }
        else if (Event.Type == ELocalLLMEventType::Error)
        {
            FLocalLLMEvent Warning = Event;
            Warning.Type = ELocalLLMEventType::Warning;
            Warning.Text = TEXT("Speaker enrollment failed: ") + Event.Text;
            OnInternalMicrophoneEvent.Broadcast(Warning);
            ActiveEnrollmentRequestId.Invalidate();
            if (Impl->bStopAfterEnrollment) StopListening(false);
        }
        return;
    }

    if (FLocalLLMAudioInput* PendingAudio = PendingSpeakerChecks.Find(Event.RequestId))
    {
        if (Event.Type == ELocalLLMEventType::SpeakerVerificationCompleted)
        {
            LastSpeakerSimilarity = Event.SpeakerSimilarity;
            OnInternalMicrophoneEvent.Broadcast(Event);
            FLocalLLMAudioInput Audio = MoveTemp(*PendingAudio);
            PendingSpeakerChecks.Remove(Event.RequestId);
            if (Event.bSpeakerAccepted || !Config.SpeakerVerification.bRejectMismatchedSpeaker)
                SubmitAudioAfterSpeakerCheck(MoveTemp(Audio), false);
            else
            {
                FLocalLLMEvent Rejected = Event;
                Rejected.Type = ELocalLLMEventType::SpeakerRejected;
                OnInternalMicrophoneEvent.Broadcast(Rejected);
            }
        }
        else if (Event.Type == ELocalLLMEventType::Error)
        {
            FLocalLLMAudioInput Audio = MoveTemp(*PendingAudio);
            PendingSpeakerChecks.Remove(Event.RequestId);
            FLocalLLMEvent Warning = Event;
            Warning.Type = ELocalLLMEventType::Warning;
            Warning.Text = TEXT("Speaker verification unavailable; continuing normally: ") + Event.Text;
            OnInternalMicrophoneEvent.Broadcast(Warning);
            SubmitAudioAfterSpeakerCheck(MoveTemp(Audio), false);
        }
        return;
    }

    if (!ActivePartialRequestId.IsValid() || Event.RequestId != ActivePartialRequestId) return;
    if (Event.Type == ELocalLLMEventType::TranscriptionPartial)
    {
        OnInternalMicrophoneEvent.Broadcast(Event);
        ActivePartialRequestId.Invalidate();
    }
    else if (Event.Type == ELocalLLMEventType::Error)
    {
        OnInternalMicrophoneEvent.Broadcast(Event);
        ActivePartialRequestId.Invalidate();
    }
}

void ULocalLLMMicrophoneComponent::BroadcastLocalEvent(
    const ELocalLLMEventType Type, const FString& Text, const FGuid& RequestId)
{
    FLocalLLMEvent Event;
    Event.Type = Type;
    Event.RequestId = RequestId;
    Event.SessionId = SessionId;
    Event.Text = Text;
    OnInternalMicrophoneEvent.Broadcast(Event);
}
