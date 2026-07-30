#include "Speech/LocalVoiceActivityDetector.h"

FLocalLLMVoiceActivityDetector::FLocalLLMVoiceActivityDetector(const FLocalLLMMicrophoneConfig& InConfig)
    : Config(InConfig)
{
}

void FLocalLLMVoiceActivityDetector::SetConfig(const FLocalLLMMicrophoneConfig& InConfig)
{
    Config = InConfig;
    Reset();
}

void FLocalLLMVoiceActivityDetector::Reset()
{
    SampleRate = 0;
    NumChannels = 0;
    ConsecutiveVoiceFrames = 0;
    SpeechVoiceFrames = 0;
    SilenceFrames = 0;
    bSpeechActive = false;
    PreRoll.Reset();
    Utterance.Reset();
}

void FLocalLLMVoiceActivityDetector::ResetStream(const int32 InSampleRate, const int32 InNumChannels)
{
    Reset();
    SampleRate = InSampleRate;
    NumChannels = InNumChannels;
}

bool FLocalLLMVoiceActivityDetector::IsVoiceBlock(const float* Samples, const int32 NumSamples) const
{
    return CalculateRmsDb(Samples, NumSamples) >= Config.VoiceThresholdDb;
}

float FLocalLLMVoiceActivityDetector::CalculateRmsDb(const float* Samples, const int32 NumSamples)
{
    if (!Samples || NumSamples <= 0) return -96.0f;
    double SumSquares = 0.0;
    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        const double Value = FMath::Clamp(static_cast<double>(Samples[Index]), -1.0, 1.0);
        SumSquares += Value * Value;
    }
    const double Rms = FMath::Sqrt(SumSquares / static_cast<double>(NumSamples));
    return static_cast<float>(20.0 * FMath::LogX(10.0, FMath::Max(Rms, 1.0e-8)));
}

bool FLocalLLMVoiceActivityDetector::EstimateCalibratedThreshold(
    const TArray<float>& BlockLevelsDb, const float NoiseMarginDb, float MinimumThresholdDb,
    float MaximumThresholdDb, float& OutNoiseFloorDb, float& OutThresholdDb)
{
    if (BlockLevelsDb.Num() < 3) return false;
    if (MinimumThresholdDb > MaximumThresholdDb) Swap(MinimumThresholdDb, MaximumThresholdDb);
    TArray<float> Sorted = BlockLevelsDb;
    Sorted.Sort();
    // Remove the loudest fifth so a click, cough, or short accidental word does not dominate calibration.
    const int32 QuietCount = FMath::Max(1, FMath::FloorToInt(Sorted.Num() * 0.8f));
    Sorted.SetNum(QuietCount, EAllowShrinking::No);
    OutNoiseFloorDb = Sorted[QuietCount / 2];
    OutThresholdDb = FMath::Clamp(OutNoiseFloorDb + NoiseMarginDb, MinimumThresholdDb, MaximumThresholdDb);
    return FMath::IsFinite(OutNoiseFloorDb) && FMath::IsFinite(OutThresholdDb);
}

FLocalLLMVadUpdate FLocalLLMVoiceActivityDetector::Process(
    const float* Samples, const int32 NumSamples, const int32 InSampleRate, const int32 InNumChannels)
{
    FLocalLLMVadUpdate Update;
    if (!Samples || NumSamples <= 0 || InSampleRate <= 0 || InNumChannels <= 0) return Update;
    if (SampleRate != InSampleRate || NumChannels != InNumChannels) ResetStream(InSampleRate, InNumChannels);

    const int32 Frames = NumSamples / NumChannels;
    if (Frames <= 0) return Update;
    const bool bVoice = IsVoiceBlock(Samples, Frames * NumChannels);
    const int64 StartFrames = FMath::Max<int64>(1, static_cast<int64>(SampleRate) * Config.SpeechStartMilliseconds / 1000);
    const int64 EndFrames = FMath::Max<int64>(1, static_cast<int64>(SampleRate) * Config.SpeechEndSilenceMilliseconds / 1000);
    const int64 MaximumFrames = FMath::Max<int64>(1, static_cast<int64>(SampleRate * Config.MaximumUtteranceSeconds));

    if (!bSpeechActive)
    {
        PreRoll.Append(Samples, Frames * NumChannels);
        const int32 MaxPreRollSamples = SampleRate * NumChannels * Config.PreRollMilliseconds / 1000;
        if (PreRoll.Num() > MaxPreRollSamples)
            PreRoll.RemoveAt(0, PreRoll.Num() - MaxPreRollSamples, EAllowShrinking::No);

        ConsecutiveVoiceFrames = bVoice ? ConsecutiveVoiceFrames + Frames : 0;
        if (ConsecutiveVoiceFrames >= StartFrames)
        {
            bSpeechActive = true;
            Update.bSpeechStarted = true;
            Utterance = MoveTemp(PreRoll);
            SpeechVoiceFrames = ConsecutiveVoiceFrames;
            SilenceFrames = 0;
        }
        return Update;
    }

    Utterance.Append(Samples, Frames * NumChannels);
    if (bVoice)
    {
        SpeechVoiceFrames += Frames;
        SilenceFrames = 0;
    }
    else
    {
        SilenceFrames += Frames;
    }

    const int64 UtteranceFrames = Utterance.Num() / NumChannels;
    if (SilenceFrames >= EndFrames || UtteranceFrames >= MaximumFrames)
    {
        Update.bSpeechEnded = true;
        Update.bMaximumDurationReached = UtteranceFrames >= MaximumFrames;
        Update.CompletedUtterance = Flush(false);
    }
    return Update;
}

FLocalLLMAudioInput FLocalLLMVoiceActivityDetector::Snapshot() const
{
    FLocalLLMAudioInput Audio;
    if (!bSpeechActive) return Audio;
    Audio.SampleRate = SampleRate;
    Audio.NumChannels = NumChannels;
    Audio.Samples = Utterance;
    return Audio;
}

FLocalLLMAudioInput FLocalLLMVoiceActivityDetector::Flush(const bool bIgnoreMinimumDuration)
{
    FLocalLLMAudioInput Audio;
    const int64 MinimumFrames = static_cast<int64>(SampleRate) * Config.MinimumUtteranceMilliseconds / 1000;
    if (bSpeechActive && (bIgnoreMinimumDuration || SpeechVoiceFrames >= MinimumFrames))
    {
        Audio.SampleRate = SampleRate;
        Audio.NumChannels = NumChannels;
        Audio.Samples = MoveTemp(Utterance);
    }
    ConsecutiveVoiceFrames = 0;
    SpeechVoiceFrames = 0;
    SilenceFrames = 0;
    bSpeechActive = false;
    PreRoll.Reset();
    Utterance.Reset();
    return Audio;
}
