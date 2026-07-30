#include "LocalLLMTextToSpeechComponent.h"

#include "Async/Async.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "ILocalTextToSpeechBackend.h"
#include "LocalMultimodalLLMModule.h"
#include "Misc/ScopeLock.h"
#include "Speech/LocalLLMSpeechTextUtils.h"
#include "Sound/AudioBus.h"
#include "Sound/SoundWaveProcedural.h"

struct ULocalLLMTextToSpeechComponent::FImpl
{
    ~FImpl()
    {
        FScopeLock Lock(&BackendMutex);
        if (Backend) Backend->Unload();
    }

    bool EnsureLoaded(const FLocalLLMTextToSpeechConfig& Config, FString& OutError)
    {
        FScopeLock Lock(&BackendMutex);
        const bool bConfigurationChanged = !Backend || ActiveConfig.Provider != Config.Provider ||
            ActiveConfig.ModelPath != Config.ModelPath || ActiveConfig.VoiceId != Config.VoiceId ||
            ActiveConfig.SpeakerReferencePath != Config.SpeakerReferencePath || ActiveConfig.Language != Config.Language ||
            ActiveConfig.OutputSampleRate != Config.OutputSampleRate || ActiveConfig.Threads != Config.Threads ||
            ActiveConfig.bUseGpu != Config.bUseGpu || ActiveConfig.ChunkMilliseconds != Config.ChunkMilliseconds ||
            ActiveConfig.QualitySteps != Config.QualitySteps || ActiveConfig.Seed != Config.Seed ||
            ActiveConfig.SamplingTemperature != Config.SamplingTemperature ||
            ActiveConfig.SamplingTopK != Config.SamplingTopK ||
            !ActiveConfig.SpokenTextReplacements.OrderIndependentCompareEqual(Config.SpokenTextReplacements) ||
            ActiveConfig.MaxReferenceSeconds != Config.MaxReferenceSeconds ||
            ActiveConfig.MaxGeneratedSeconds != Config.MaxGeneratedSeconds;
        if (!bConfigurationChanged) return true;

        if (Backend) Backend->Unload();
        Backend = FLocalTextToSpeechBackendRegistry::Create(Config.Provider);
        bReady.Store(false);
        if (!Backend)
        {
            OutError = FString::Printf(TEXT("Text-to-speech provider '%s' is not installed"), *Config.Provider.ToString());
            return false;
        }
        if (!Backend->Load(Config, OutError))
        {
            Backend.Reset();
            return false;
        }
        ActiveConfig = Config;
        bVoiceWarmed.Store(false);
        bReady.Store(true);
        return true;
    }

    FCriticalSection BackendMutex;
    TUniquePtr<ILocalTextToSpeechBackend> Backend;
    FLocalLLMTextToSpeechConfig ActiveConfig;
    TAtomic<bool> bCancelRequested{false};
    TAtomic<bool> bBusy{false};
    TAtomic<bool> bReady{false};
    TAtomic<bool> bVoiceWarmed{false};
};

namespace
{
constexpr double PlaybackDrainGraceSeconds = 0.25;

struct FSpeechDurationGuard
{
    double StreamedSeconds = 0.0;
    double MaximumSeconds = 8.0;
    TAtomic<bool> bLimitReached{false};
};

struct FStreamingLoudnessNormalizer
{
    explicit FStreamingLoudnessNormalizer(const FLocalLLMTextToSpeechConfig& Config)
        : bEnabled(Config.bNormalizeOutputLoudness)
        , TargetRmsDbfs(FMath::Clamp(Config.TargetOutputRmsDbfs, -40.0f, -12.0f))
        , MaxGainDb(FMath::Clamp(Config.MaxOutputGainDb, 0.0f, 18.0f))
        , MaxAttenuationDb(FMath::Clamp(Config.MaxOutputAttenuationDb, 0.0f, 24.0f))
        , PeakCeiling(FMath::Pow(10.0f, FMath::Clamp(Config.OutputPeakCeilingDbfs, -12.0f, -0.1f) / 20.0f))
        , AdaptationSeconds(FMath::Clamp(Config.LoudnessAdaptationSeconds, 0.05f, 3.0f))
    {
    }

    void Process(FLocalLLMAudioChunk& Chunk)
    {
        if (!bEnabled || !Chunk.IsValid())
        {
            NormalizedSamples.Append(Chunk.Samples);
            return;
        }

        double SumSquares = 0.0;
        float Peak = 0.0f;
        for (const float Sample : Chunk.Samples)
        {
            SumSquares += static_cast<double>(Sample) * Sample;
            Peak = FMath::Max(Peak, FMath::Abs(Sample));
        }
        const float Rms = FMath::Sqrt(static_cast<float>(SumSquares / FMath::Max(1, Chunk.Samples.Num())));
        const float RmsDbfs = 20.0f * FMath::LogX(10.0f, FMath::Max(Rms, 1.0e-8f));
        RawSumSquares += SumSquares;
        RawSampleCount += Chunk.Samples.Num();
        RawPeak = FMath::Max(RawPeak, Peak);

        // Do not turn codec startup noise, fades, pauses, or nearly empty chunks into
        // audible hiss. NeuTTS can rise from roughly -60 dBFS to full speech within a
        // few frames, so a permissive floor causes a large transient boost.
        const float SpeechFloorDbfs = FMath::Max(TargetRmsDbfs - 18.0f, -50.0f);
        float DesiredGainDb = 0.0f;
        if (RmsDbfs >= SpeechFloorDbfs)
            DesiredGainDb = FMath::Clamp(TargetRmsDbfs - RmsDbfs, -MaxAttenuationDb, MaxGainDb);

        const float ChunkSeconds = static_cast<float>(Chunk.Samples.Num()) /
            FMath::Max(1.0f, static_cast<float>(Chunk.SampleRate * FMath::Max(1, Chunk.NumChannels)));
        // Begin at unity. Increasing gain is deliberately gradual, while attenuation
        // reacts quickly enough to catch the first real syllable after a quiet onset.
        const float TimeConstant = DesiredGainDb < SmoothedGainDb
            ? FMath::Min(AdaptationSeconds, 0.06f)
            : AdaptationSeconds;
        const float Alpha = 1.0f - FMath::Exp(-ChunkSeconds / TimeConstant);
        SmoothedGainDb = FMath::Lerp(SmoothedGainDb, DesiredGainDb, Alpha);

        float LinearGain = FMath::Pow(10.0f, SmoothedGainDb / 20.0f);
        if (Peak > 1.0e-6f)
            LinearGain = FMath::Min(LinearGain, PeakCeiling / Peak);
        const float AppliedGainDb = 20.0f * FMath::LogX(10.0f, FMath::Max(LinearGain, 1.0e-8f));
        MinimumAppliedGainDb = FMath::Min(MinimumAppliedGainDb, AppliedGainDb);
        MaximumAppliedGainDb = FMath::Max(MaximumAppliedGainDb, AppliedGainDb);
        if (ProcessedSeconds < 0.5)
        {
            OnsetRawPeak = FMath::Max(OnsetRawPeak, Peak);
            OnsetMaximumGainDb = FMath::Max(OnsetMaximumGainDb, AppliedGainDb);
        }

        // A single gain value per 20 ms frame can introduce discontinuities that sound
        // like crackle. Ramp from the prior frame's applied gain at sample granularity.
        const int32 SampleCount = Chunk.Samples.Num();
        for (int32 Index = 0; Index < SampleCount; ++Index)
        {
            const float T = static_cast<float>(Index + 1) / FMath::Max(1, SampleCount);
            const float SampleGain = FMath::Lerp(PreviousLinearGain, LinearGain, T);
            Chunk.Samples[Index] =
                FMath::Clamp(Chunk.Samples[Index] * SampleGain, -PeakCeiling, PeakCeiling);
            OutputPeak = FMath::Max(OutputPeak, FMath::Abs(Chunk.Samples[Index]));
        }
        PreviousLinearGain = LinearGain;
        ProcessedSeconds += ChunkSeconds;
        NormalizedSamples.Append(Chunk.Samples);
    }

    FString Describe() const
    {
        const float RawRms = RawSampleCount > 0
            ? FMath::Sqrt(static_cast<float>(RawSumSquares / RawSampleCount)) : 0.0f;
        const auto ToDbfs = [](const float Value)
        {
            return 20.0f * FMath::LogX(10.0f, FMath::Max(Value, 1.0e-8f));
        };
        return FString::Printf(
            TEXT("raw peak %.1f dBFS RMS %.1f dBFS; onset raw peak %.1f dBFS max gain %+.1f dB; ")
            TEXT("output peak %.1f dBFS; gain range %+.1f..%+.1f dB"),
            ToDbfs(RawPeak), ToDbfs(RawRms), ToDbfs(OnsetRawPeak), OnsetMaximumGainDb,
            ToDbfs(OutputPeak), MinimumAppliedGainDb, MaximumAppliedGainDb);
    }

    bool bEnabled = true;
    float TargetRmsDbfs = -27.0f;
    float MaxGainDb = 8.0f;
    float MaxAttenuationDb = 12.0f;
    float PeakCeiling = 0.7079f;
    float AdaptationSeconds = 0.75f;
    float SmoothedGainDb = 0.0f;
    float PreviousLinearGain = 1.0f;
    float MinimumAppliedGainDb = 0.0f;
    float MaximumAppliedGainDb = 0.0f;
    float OnsetMaximumGainDb = 0.0f;
    float RawPeak = 0.0f;
    float OnsetRawPeak = 0.0f;
    float OutputPeak = 0.0f;
    double RawSumSquares = 0.0;
    int64 RawSampleCount = 0;
    double ProcessedSeconds = 0.0;
    TArray<float> NormalizedSamples;
};
}

ULocalLLMTextToSpeechComponent::ULocalLLMTextToSpeechComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    Impl = MakeShared<FImpl, ESPMode::ThreadSafe>();
}

ULocalLLMTextToSpeechComponent::~ULocalLLMTextToSpeechComponent() = default;

void ULocalLLMTextToSpeechComponent::BeginPlay()
{
    Super::BeginPlay();
    if (bInitializeOnBeginPlay) InitializeTextToSpeech();
}

void ULocalLLMTextToSpeechComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelSpeechSynthesis();
    DestroyPlaybackObjects();
    Impl.Reset();
    Super::EndPlay(EndPlayReason);
}

void ULocalLLMTextToSpeechComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!SpeechAudioComponent || !SpeechSoundWave)
    {
        SetComponentTickEnabled(false);
        return;
    }

    const bool bCanQueueMoreAudio = (Impl && Impl->bBusy.Load()) || !QueuedSpeech.IsEmpty();
    const double Now = FPlatformTime::Seconds();
    const bool bEstimatedPcmDrained = EstimatedPlaybackEndAt > 0.0 &&
        Now >= EstimatedPlaybackEndAt + PlaybackDrainGraceSeconds;
    if (!bCanQueueMoreAudio && bEstimatedPcmDrained)
    {
        const int32 RemainingBytes = SpeechSoundWave->GetAvailableAudioByteCount();
        SpeechAudioComponent->Stop();
        SpeechSoundWave->ResetAudio();
        EstimatedPlaybackEndAt = 0.0;
        SetComponentTickEnabled(false);
        UE_LOG(LogLocalMultimodalLLM, Display,
            TEXT("Procedural TTS playback drained by PCM clock (reported buffered bytes=%d)"),
            RemainingBytes);
        return;
    }

    if (!SpeechAudioComponent->IsPlaying() && !bCanQueueMoreAudio)
    {
        EstimatedPlaybackEndAt = 0.0;
        SetComponentTickEnabled(false);
    }
}

FGuid ULocalLLMTextToSpeechComponent::InitializeTextToSpeech()
{
    if (!Impl || !Config.IsEnabled() || Impl->bBusy.Load())
    {
        BroadcastEvent(ELocalLLMEventType::Error, {}, {}, NAME_None,
            !Config.IsEnabled() ? TEXT("Text-to-speech is disabled or has no provider") : TEXT("Text-to-speech is busy"));
        return {};
    }

    const FGuid RequestId = FGuid::NewGuid();
    ActiveRequestId = RequestId;
    Impl->bBusy.Store(true);
    Impl->bCancelRequested.Store(false);
    BroadcastEvent(ELocalLLMEventType::TextToSpeechInitializing, RequestId, {}, NAME_None,
        FString::Printf(TEXT("Initializing %s"), *Config.Provider.ToString()));

    const TSharedPtr<FImpl, ESPMode::ThreadSafe> SharedImpl = Impl;
    const TWeakObjectPtr<ULocalLLMTextToSpeechComponent> WeakThis(this);
    const FLocalLLMTextToSpeechConfig ConfigCopy = Config;
    Async(EAsyncExecution::ThreadPool, [SharedImpl, WeakThis, ConfigCopy, RequestId]()
    {
        FString Error;
        const bool bLoaded = SharedImpl->EnsureLoaded(ConfigCopy, Error);
        SharedImpl->bBusy.Store(false);
        AsyncTask(ENamedThreads::GameThread, [WeakThis, RequestId, bLoaded, Error = MoveTemp(Error)]()
        {
            if (ULocalLLMTextToSpeechComponent* Self = WeakThis.Get())
            {
                Self->ActiveRequestId.Invalidate();
                Self->BroadcastEvent(bLoaded ? ELocalLLMEventType::TextToSpeechReady : ELocalLLMEventType::Error,
                    RequestId, {}, NAME_None, bLoaded ? TEXT("Text-to-speech provider ready") : Error);
                if (bLoaded) Self->DrainPendingPrewarmOrSpeech();
            }
        });
    });
    return RequestId;
}

bool ULocalLLMTextToSpeechComponent::PrewarmVoice(const FName CharacterId)
{
    if (!Impl || !Config.IsEnabled() || !Config.bPrewarmVoiceWhenSelected) return false;
    if (Impl->bVoiceWarmed.Load()) return true;
    if (Impl->bBusy.Load())
    {
        bPendingVoicePrewarm = true;
        PendingPrewarmCharacterId = CharacterId;
        return true;
    }

    const FGuid RequestId = FGuid::NewGuid();
    ActiveRequestId = RequestId;
    Impl->bBusy.Store(true);
    Impl->bCancelRequested.Store(false);
    const TSharedPtr<FImpl, ESPMode::ThreadSafe> SharedImpl = Impl;
    const TWeakObjectPtr<ULocalLLMTextToSpeechComponent> WeakThis(this);
    const FLocalLLMTextToSpeechConfig ConfigCopy = Config;
    const double PrewarmStartedAt = FPlatformTime::Seconds();
    Async(EAsyncExecution::ThreadPool,
        [SharedImpl, WeakThis, ConfigCopy, RequestId, CharacterId, PrewarmStartedAt]()
    {
        FString Error;
        bool bSuccess = SharedImpl->EnsureLoaded(ConfigCopy, Error);
        if (bSuccess && !SharedImpl->bVoiceWarmed.Load())
        {
            FLocalLLMTextToSpeechRequest Warmup;
            Warmup.Text = TEXT("Ready.");
            Warmup.VoiceId = ConfigCopy.VoiceId;
            Warmup.Language = ConfigCopy.Language;
            FScopeLock Lock(&SharedImpl->BackendMutex);
            bSuccess = SharedImpl->Backend->PrewarmVoice(Warmup, Error,
                [SharedImpl]() { return SharedImpl->bCancelRequested.Load(); });
            if (bSuccess) SharedImpl->bVoiceWarmed.Store(true);
        }
        SharedImpl->bBusy.Store(false);
        AsyncTask(ENamedThreads::GameThread,
            [WeakThis, RequestId, CharacterId, PrewarmStartedAt, bSuccess, Error = MoveTemp(Error)]()
        {
            if (ULocalLLMTextToSpeechComponent* Self = WeakThis.Get())
            {
                Self->ActiveRequestId.Invalidate();
                if (bSuccess)
                {
                    UE_LOG(LogLocalMultimodalLLM, Display,
                        TEXT("Silently prewarmed TTS voice '%s' for character '%s' in %.0f ms"),
                        *Self->Config.VoiceId, *CharacterId.ToString(),
                        (FPlatformTime::Seconds() - PrewarmStartedAt) * 1000.0);
                    Self->DrainPendingPrewarmOrSpeech();
                }
                else
                {
                    Self->BroadcastEvent(ELocalLLMEventType::Error, RequestId, {}, CharacterId,
                        FString::Printf(TEXT("Voice prewarm failed: %s"), *Error));
                    Self->StartNextQueuedSpeech();
                }
            }
        });
    });
    return true;
}

FGuid ULocalLLMTextToSpeechComponent::SynthesizeSpeech(
    FLocalLLMTextToSpeechRequest Request, const FGuid SessionId, const FName CharacterId)
{
    if (!Impl || !Config.IsEnabled() || Request.Text.TrimStartAndEnd().IsEmpty() || Impl->bBusy.Load())
    {
        const FString Error = !Config.IsEnabled() ? TEXT("Text-to-speech is disabled or has no provider") :
            (Request.Text.TrimStartAndEnd().IsEmpty() ? TEXT("Text-to-speech input is empty") : TEXT("Text-to-speech is busy"));
        BroadcastEvent(ELocalLLMEventType::Error, {}, SessionId, CharacterId, Error);
        return {};
    }

    const FGuid RequestId = FGuid::NewGuid();
    ActiveRequestId = RequestId;
    Impl->bBusy.Store(true);
    Impl->bCancelRequested.Store(false);
    const FString EffectiveVoice = Request.VoiceId.IsEmpty() ? Config.VoiceId : Request.VoiceId;
    BroadcastEvent(ELocalLLMEventType::TextToSpeechStarted, RequestId, SessionId, CharacterId,
        Request.Text, nullptr, EffectiveVoice);
    ArmSynthesisWatchdog(RequestId, SessionId, CharacterId);

    const TSharedPtr<FImpl, ESPMode::ThreadSafe> SharedImpl = Impl;
    const TWeakObjectPtr<ULocalLLMTextToSpeechComponent> WeakThis(this);
    const FLocalLLMTextToSpeechConfig ConfigCopy = Config;
    const double ExpectedSeconds = FMath::Max(1.0,
        Request.Text.Len() / (12.0 * FMath::Max(0.25f, Request.SpeakingRate)));
    const TSharedRef<FSpeechDurationGuard, ESPMode::ThreadSafe> DurationGuard =
        MakeShared<FSpeechDurationGuard, ESPMode::ThreadSafe>();
    const TSharedRef<FStreamingLoudnessNormalizer, ESPMode::ThreadSafe> LoudnessNormalizer =
        MakeShared<FStreamingLoudnessNormalizer, ESPMode::ThreadSafe>(ConfigCopy);
    DurationGuard->MaximumSeconds = FMath::Min(
        static_cast<double>(FMath::Clamp(ConfigCopy.MaxGeneratedSeconds, 1.0f, 60.0f)),
        FMath::Max(2.75, ExpectedSeconds * 1.75 + 1.0));
    Async(EAsyncExecution::ThreadPool,
        [SharedImpl, WeakThis, ConfigCopy, Request = MoveTemp(Request), RequestId, SessionId, CharacterId,
            EffectiveVoice, DurationGuard, LoudnessNormalizer]() mutable
    {
        FString Error;
        FLocalTextToSpeechResult Result;
        bool bSuccess = SharedImpl->EnsureLoaded(ConfigCopy, Error);
        if (bSuccess)
        {
            FScopeLock Lock(&SharedImpl->BackendMutex);
            bSuccess = SharedImpl->Backend->Synthesize(Request, Result, Error,
                [SharedImpl]() { return SharedImpl->bCancelRequested.Load(); },
                [SharedImpl, WeakThis, RequestId, SessionId, CharacterId, EffectiveVoice,
                    DurationGuard, LoudnessNormalizer](const FLocalLLMAudioChunk& Chunk)
                {
                    const double ChunkSeconds = Chunk.SampleRate > 0
                        ? static_cast<double>(Chunk.Samples.Num()) /
                            (Chunk.SampleRate * FMath::Max(1, Chunk.NumChannels))
                        : 0.0;
                    if (DurationGuard->StreamedSeconds + ChunkSeconds > DurationGuard->MaximumSeconds)
                    {
                        DurationGuard->bLimitReached.Store(true);
                        SharedImpl->bCancelRequested.Store(true);
                        return;
                    }
                    DurationGuard->StreamedSeconds += ChunkSeconds;
                    FLocalLLMAudioChunk OutputChunk = Chunk;
                    LoudnessNormalizer->Process(OutputChunk);
                    AsyncTask(ENamedThreads::GameThread,
                        [WeakThis, RequestId, SessionId, CharacterId, EffectiveVoice,
                            Chunk = MoveTemp(OutputChunk)]()
                    {
                        if (ULocalLLMTextToSpeechComponent* Self = WeakThis.Get())
                            Self->BroadcastEvent(ELocalLLMEventType::TextToSpeechChunk, RequestId, SessionId,
                                CharacterId, {}, &Chunk, EffectiveVoice);
                    });
                });
        }

        const bool bCancelled = SharedImpl->bCancelRequested.Load();
        if (bSuccess && !bCancelled)
        {
            if (ConfigCopy.bNormalizeOutputLoudness)
            {
                UE_LOG(LogLocalMultimodalLLM, Display,
                    TEXT("TTS normalization for provider '%s' voice '%s': %s"),
                    *ConfigCopy.Provider.ToString(), *EffectiveVoice,
                    *LoudnessNormalizer->Describe());
            }
            if (LoudnessNormalizer->NormalizedSamples.Num() == Result.Audio.Samples.Num())
            {
                Result.Audio.Samples = MoveTemp(LoudnessNormalizer->NormalizedSamples);
            }
            else if (LoudnessNormalizer->NormalizedSamples.IsEmpty() && Result.Audio.IsValid())
            {
                FLocalLLMAudioChunk CompleteAudio;
                CompleteAudio.Samples = Result.Audio.Samples;
                CompleteAudio.SampleRate = Result.Audio.SampleRate;
                CompleteAudio.NumChannels = Result.Audio.NumChannels;
                LoudnessNormalizer->Process(CompleteAudio);
                Result.Audio.Samples = MoveTemp(CompleteAudio.Samples);
            }
        }
        if (bSuccess && !bCancelled) SharedImpl->bVoiceWarmed.Store(true);
        SharedImpl->bBusy.Store(false);
        AsyncTask(ENamedThreads::GameThread,
            [WeakThis, RequestId, SessionId, CharacterId, EffectiveVoice, bSuccess, bCancelled, DurationGuard,
                Error = MoveTemp(Error), Result = MoveTemp(Result)]() mutable
        {
            if (ULocalLLMTextToSpeechComponent* Self = WeakThis.Get())
            {
                Self->ClearSynthesisWatchdog(RequestId);
                Self->ActiveRequestId.Invalidate();
                const bool bTimedOut = Self->TimedOutRequests.Remove(RequestId) > 0;
                if (bTimedOut)
                {
                    Self->StopSpeechPlayback();
                    Self->BroadcastEvent(ELocalLLMEventType::Error, RequestId, SessionId, CharacterId,
                        FString::Printf(TEXT("Speech synthesis timed out after %.1f seconds"),
                            Self->Config.SynthesisTimeoutSeconds));
                }
                else if (DurationGuard->bLimitReached.Load())
                {
                    Self->StopSpeechPlayback();
                    Self->BroadcastEvent(ELocalLLMEventType::Error, RequestId, SessionId, CharacterId,
                        FString::Printf(TEXT("Speech synthesis exceeded its %.2f second safety budget and was stopped"),
                            DurationGuard->MaximumSeconds));
                }
                else if (bCancelled)
                {
                    Self->BroadcastEvent(ELocalLLMEventType::TextToSpeechCancelled, RequestId, SessionId,
                        CharacterId, TEXT("Speech synthesis cancelled"), nullptr, EffectiveVoice);
                }
                else if (!bSuccess)
                {
                    Self->BroadcastEvent(ELocalLLMEventType::Error, RequestId, SessionId, CharacterId, Error);
                }
                else
                {
                    FLocalLLMAudioChunk Complete;
                    Complete.Samples = MoveTemp(Result.Audio.Samples);
                    Complete.SampleRate = Result.Audio.SampleRate;
                    Complete.NumChannels = Result.Audio.NumChannels;
                    Complete.SequenceNumber = INDEX_NONE;
                    Self->BroadcastEvent(ELocalLLMEventType::TextToSpeechCompleted, RequestId, SessionId,
                        CharacterId, TEXT("Speech synthesis completed"), &Complete,
                        Result.VoiceId.IsEmpty() ? EffectiveVoice : Result.VoiceId);
                }
                Self->DrainPendingPrewarmOrSpeech();
            }
        });
    });
    return RequestId;
}

bool ULocalLLMTextToSpeechComponent::QueueSpeech(
    FLocalLLMTextToSpeechRequest Request, const FGuid SessionId, const FName CharacterId)
{
    Request.Text.TrimStartAndEndInline();
    if (!Impl || !Config.IsEnabled() || Request.Text.IsEmpty())
    {
        BroadcastEvent(ELocalLLMEventType::Error, {}, SessionId, CharacterId,
            !Config.IsEnabled() ? TEXT("Text-to-speech is disabled or has no provider") : TEXT("Text-to-speech input is empty"));
        return false;
    }
    const TArray<FString> Segments = LocalLLMSpeechTextUtils::SplitQueuedSpeech(
        Request.Text, Config.MaxQueuedSegmentCharacters, Config.PreferredQueuedSplitFraction);
    if (Segments.Num() > 1)
    {
        UE_LOG(LogLocalMultimodalLLM, Display,
            TEXT("Split queued TTS text into %d segments (characters=%d limit=%d preferred=%.2f)"),
            Segments.Num(), Request.Text.Len(), Config.MaxQueuedSegmentCharacters,
            Config.PreferredQueuedSplitFraction);
    }
    for (const FString& Segment : Segments)
    {
        FQueuedSpeechRequest& Queued = QueuedSpeech.AddDefaulted_GetRef();
        Queued.Request = Request;
        Queued.Request.Text = Segment;
        Queued.SessionId = SessionId;
        Queued.CharacterId = CharacterId;
    }
    StartNextQueuedSpeech();
    return !Segments.IsEmpty();
}

void ULocalLLMTextToSpeechComponent::ClearQueuedSpeech()
{
    QueuedSpeech.Reset();
}

void ULocalLLMTextToSpeechComponent::StartNextQueuedSpeech()
{
    if (!Impl || Impl->bBusy.Load() || QueuedSpeech.IsEmpty()) return;
    FQueuedSpeechRequest Next = MoveTemp(QueuedSpeech[0]);
    QueuedSpeech.RemoveAt(0, EAllowShrinking::No);
    if (!SynthesizeSpeech(MoveTemp(Next.Request), Next.SessionId, Next.CharacterId).IsValid())
        StartNextQueuedSpeech();
}

void ULocalLLMTextToSpeechComponent::DrainPendingPrewarmOrSpeech()
{
    if (bPendingVoicePrewarm)
    {
        const FName CharacterId = PendingPrewarmCharacterId;
        bPendingVoicePrewarm = false;
        PendingPrewarmCharacterId = NAME_None;
        if (!Impl->bVoiceWarmed.Load() && PrewarmVoice(CharacterId)) return;
    }
    StartNextQueuedSpeech();
}

void ULocalLLMTextToSpeechComponent::CancelSpeechSynthesis()
{
    QueuedSpeech.Reset();
    TimedOutRequests.Reset();
    ClearSynthesisWatchdog(ActiveRequestId);
    if (Impl && Impl->bBusy.Load()) Impl->bCancelRequested.Store(true);
    StopSpeechPlayback();
}

void ULocalLLMTextToSpeechComponent::ArmSynthesisWatchdog(
    const FGuid& RequestId, const FGuid& SessionId, const FName CharacterId)
{
    if (!GetWorld() || Config.SynthesisTimeoutSeconds <= 0.0f) return;
    GetWorld()->GetTimerManager().ClearTimer(SynthesisWatchdogTimer);
    WatchdogRequestId = RequestId;
    WatchdogSessionId = SessionId;
    WatchdogCharacterId = CharacterId;
    GetWorld()->GetTimerManager().SetTimer(SynthesisWatchdogTimer, this,
        &ULocalLLMTextToSpeechComponent::HandleSynthesisWatchdogExpired,
        FMath::Max(5.0f, Config.SynthesisTimeoutSeconds), false);
}

void ULocalLLMTextToSpeechComponent::ClearSynthesisWatchdog(const FGuid& RequestId)
{
    if (RequestId.IsValid() && WatchdogRequestId != RequestId) return;
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SynthesisWatchdogTimer);
    WatchdogRequestId.Invalidate();
    WatchdogSessionId.Invalidate();
    WatchdogCharacterId = NAME_None;
}

void ULocalLLMTextToSpeechComponent::HandleSynthesisWatchdogExpired()
{
    if (!Impl || !WatchdogRequestId.IsValid() || ActiveRequestId != WatchdogRequestId ||
        !Impl->bBusy.Load())
    {
        ClearSynthesisWatchdog(WatchdogRequestId);
        return;
    }

    UE_LOG(LogLocalMultimodalLLM, Error,
        TEXT("Speech synthesis watchdog expired for %s after %.1f seconds; cancelling the stalled request"),
        *WatchdogCharacterId.ToString(), Config.SynthesisTimeoutSeconds);
    TimedOutRequests.Add(WatchdogRequestId);
    QueuedSpeech.Reset();
    bPendingVoicePrewarm = false;
    PendingPrewarmCharacterId = NAME_None;
    Impl->bCancelRequested.Store(true);
    StopSpeechPlayback();
}

void ULocalLLMTextToSpeechComponent::StopSpeechPlayback()
{
    RequestsWithStreamedAudio.Reset();
    if (SpeechAudioComponent) SpeechAudioComponent->Stop();
    if (SpeechSoundWave) SpeechSoundWave->ResetAudio();
    EstimatedPlaybackEndAt = 0.0;
    SetComponentTickEnabled(false);
}

bool ULocalLLMTextToSpeechComponent::PlayAudioChunk(FLocalLLMAudioChunk Audio)
{
    return QueuePlaybackAudio(Audio);
}

bool ULocalLLMTextToSpeechComponent::IsTextToSpeechReady() const
{
    return Impl && Impl->bReady.Load();
}

bool ULocalLLMTextToSpeechComponent::IsSpeechSynthesisBusy() const
{
    return Impl && Impl->bBusy.Load();
}

TArray<FName> ULocalLLMTextToSpeechComponent::GetAvailableTextToSpeechProviders() const
{
    return FLocalTextToSpeechBackendRegistry::GetRegisteredProviders();
}

bool ULocalLLMTextToSpeechComponent::IsSpeechPlaybackActive() const
{
    if (!SpeechAudioComponent || !SpeechAudioComponent->IsPlaying()) return false;
    if (EstimatedPlaybackEndAt <= 0.0) return true;
    return FPlatformTime::Seconds() < EstimatedPlaybackEndAt + PlaybackDrainGraceSeconds;
}

UAudioComponent* ULocalLLMTextToSpeechComponent::GetSpeechAudioComponent() const
{
    return SpeechAudioComponent;
}

USoundWaveProcedural* ULocalLLMTextToSpeechComponent::GetSpeechSoundWave() const
{
    return SpeechSoundWave;
}

void ULocalLLMTextToSpeechComponent::BroadcastEvent(const ELocalLLMEventType Type, const FGuid& RequestId,
    const FGuid& SessionId, const FName CharacterId, const FString& Text, const FLocalLLMAudioChunk* Audio,
    const FString& VoiceId)
{
    FLocalLLMEvent Event;
    Event.Type = Type;
    Event.RequestId = RequestId;
    Event.SessionId = SessionId;
    Event.CharacterId = CharacterId;
    Event.Text = Text;
    Event.TextToSpeechProvider = Config.Provider;
    Event.VoiceId = VoiceId;
    if (Audio) Event.Audio = *Audio;
    HandleAutomaticPlayback(Type, RequestId, Audio);
    OnTextToSpeechEvent.Broadcast(Event);
}

void ULocalLLMTextToSpeechComponent::HandleAutomaticPlayback(const ELocalLLMEventType Type,
    const FGuid& RequestId, const FLocalLLMAudioChunk* Audio)
{
    if (!bAutoPlayAudio) return;
    if (Type == ELocalLLMEventType::TextToSpeechStarted)
    {
        RequestsWithStreamedAudio.Remove(RequestId);
    }
    else if (Type == ELocalLLMEventType::TextToSpeechChunk && Audio && Audio->IsValid())
    {
        RequestsWithStreamedAudio.Add(RequestId);
        QueuePlaybackAudio(*Audio);
    }
    else if (Type == ELocalLLMEventType::TextToSpeechCompleted)
    {
        if (Audio && Audio->IsValid() && !RequestsWithStreamedAudio.Contains(RequestId))
            QueuePlaybackAudio(*Audio);
        RequestsWithStreamedAudio.Remove(RequestId);
    }
    else if (Type == ELocalLLMEventType::TextToSpeechCancelled)
    {
        RequestsWithStreamedAudio.Remove(RequestId);
        StopSpeechPlayback();
    }
}

bool ULocalLLMTextToSpeechComponent::QueuePlaybackAudio(const FLocalLLMAudioChunk& Audio)
{
    if (!Audio.IsValid()) return false;
    if (!EnsurePlaybackObjects(Audio.SampleRate, Audio.NumChannels))
    {
        UE_LOG(LogLocalMultimodalLLM, Warning,
            TEXT("Could not create procedural TTS playback objects (rate=%d channels=%d)"),
            Audio.SampleRate, Audio.NumChannels);
        return false;
    }

    TArray<int16> Pcm16;
    Pcm16.SetNumUninitialized(Audio.Samples.Num());
    for (int32 Index = 0; Index < Audio.Samples.Num(); ++Index)
    {
        const float Sample = FMath::Clamp(Audio.Samples[Index], -1.0f, 1.0f);
        Pcm16[Index] = Sample <= -1.0f ? MIN_int16 : static_cast<int16>(FMath::RoundToInt(Sample * MAX_int16));
    }
    const double Now = FPlatformTime::Seconds();
    const double ChunkSeconds = static_cast<double>(Audio.Samples.Num()) /
        static_cast<double>(Audio.SampleRate * FMath::Max(1, Audio.NumChannels));
    if (!SpeechAudioComponent->IsPlaying() || EstimatedPlaybackEndAt <= Now)
        EstimatedPlaybackEndAt = Now + ChunkSeconds;
    else
        EstimatedPlaybackEndAt += ChunkSeconds;

    SpeechSoundWave->QueueAudio(reinterpret_cast<const uint8*>(Pcm16.GetData()),
        Pcm16.Num() * sizeof(int16));
    ApplyPlaybackSettings();
    if (!SpeechAudioComponent->IsPlaying())
    {
        SpeechAudioComponent->Play();
        UE_LOG(LogLocalMultimodalLLM, Display,
            TEXT("Started procedural TTS playback: rate=%d channels=%d queued=%d bytes spatial=%s volume=%.2f active=%s"),
            Audio.SampleRate, Audio.NumChannels, SpeechSoundWave->GetAvailableAudioByteCount(),
            bSpatializePlayback ? TEXT("yes") : TEXT("no"), PlaybackVolumeMultiplier,
            SpeechAudioComponent->IsPlaying() ? TEXT("yes") : TEXT("no"));
    }
    SetComponentTickEnabled(true);
    return true;
}

bool ULocalLLMTextToSpeechComponent::EnsurePlaybackObjects(const int32 SampleRate, const int32 NumChannels)
{
    AActor* Owner = GetOwner();
    if (!Owner || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return false;

    if (!SpeechAudioComponent)
    {
        SpeechAudioComponent = NewObject<UAudioComponent>(Owner, NAME_None, RF_Transient);
        if (!SpeechAudioComponent) return false;
        SpeechAudioComponent->bAutoActivate = false;
        SpeechAudioComponent->bAutoDestroy = false;
        if (USceneComponent* Root = Owner->GetRootComponent())
            SpeechAudioComponent->SetupAttachment(Root);
        Owner->AddInstanceComponent(SpeechAudioComponent);
        SpeechAudioComponent->RegisterComponent();
    }

    if (!SpeechSoundWave || PlaybackSampleRate != SampleRate || PlaybackNumChannels != NumChannels)
    {
        if (SpeechAudioComponent->IsPlaying()) SpeechAudioComponent->Stop();
        EstimatedPlaybackEndAt = 0.0;
        SpeechSoundWave = NewObject<USoundWaveProcedural>(this, NAME_None, RF_Transient);
        if (!SpeechSoundWave) return false;
        SpeechSoundWave->SetSampleRate(SampleRate);
        SpeechSoundWave->NumChannels = NumChannels;
        SpeechSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
        SpeechSoundWave->SoundGroup = SOUNDGROUP_Voice;
        SpeechSoundWave->bLooping = false;
        SpeechSoundWave->bCanProcessAsync = true;
        PlaybackSampleRate = SampleRate;
        PlaybackNumChannels = NumChannels;
        SpeechAudioComponent->SetSound(SpeechSoundWave);
    }
    ApplyPlaybackSettings();
    return true;
}

void ULocalLLMTextToSpeechComponent::ApplyPlaybackSettings()
{
    if (!SpeechAudioComponent) return;
    SpeechAudioComponent->bAllowSpatialization = bSpatializePlayback;
    SpeechAudioComponent->SetUISound(!bSpatializePlayback);
    SpeechAudioComponent->SetVolumeMultiplier(FMath::Max(0.0f, PlaybackVolumeMultiplier));
    SpeechAudioComponent->SoundClassOverride = PlaybackSoundClass;
    SpeechAudioComponent->SetAttenuationSettings(PlaybackAttenuationSettings);
    if (AppliedMetaSoundAudioBus && AppliedMetaSoundAudioBus != MetaSoundAudioBus)
        SpeechAudioComponent->SetAudioBusSendPreEffect(AppliedMetaSoundAudioBus, 0.0f);
    if (MetaSoundAudioBus)
        SpeechAudioComponent->SetAudioBusSendPreEffect(MetaSoundAudioBus,
            FMath::Clamp(MetaSoundAudioBusSendLevel, 0.0f, 1.0f));
    AppliedMetaSoundAudioBus = MetaSoundAudioBus;
}

void ULocalLLMTextToSpeechComponent::DestroyPlaybackObjects()
{
    StopSpeechPlayback();
    if (SpeechAudioComponent)
    {
        SpeechAudioComponent->DestroyComponent();
        SpeechAudioComponent = nullptr;
    }
    SpeechSoundWave = nullptr;
    AppliedMetaSoundAudioBus = nullptr;
    PlaybackSampleRate = 0;
    PlaybackNumChannels = 0;
}
