#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"

struct FLocalLLMVadUpdate
{
    bool bSpeechStarted = false;
    bool bSpeechEnded = false;
    bool bMaximumDurationReached = false;
    FLocalLLMAudioInput CompletedUtterance;
};

/** Small deterministic RMS gate used to segment microphone PCM before transcription. */
class FLocalLLMVoiceActivityDetector
{
public:
    explicit FLocalLLMVoiceActivityDetector(const FLocalLLMMicrophoneConfig& InConfig = {});
    void SetConfig(const FLocalLLMMicrophoneConfig& InConfig);
    void Reset();
    FLocalLLMVadUpdate Process(const float* Samples, int32 NumSamples, int32 SampleRate, int32 NumChannels);
    bool IsSpeechActive() const { return bSpeechActive; }
    FLocalLLMAudioInput Snapshot() const;
    FLocalLLMAudioInput Flush(bool bIgnoreMinimumDuration = false);
    static float CalculateRmsDb(const float* Samples, int32 NumSamples);
    static bool EstimateCalibratedThreshold(const TArray<float>& BlockLevelsDb, float NoiseMarginDb,
        float MinimumThresholdDb, float MaximumThresholdDb, float& OutNoiseFloorDb, float& OutThresholdDb);

private:
    void ResetStream(int32 InSampleRate, int32 InNumChannels);
    bool IsVoiceBlock(const float* Samples, int32 NumSamples) const;

    FLocalLLMMicrophoneConfig Config;
    int32 SampleRate = 0;
    int32 NumChannels = 0;
    int64 ConsecutiveVoiceFrames = 0;
    int64 SpeechVoiceFrames = 0;
    int64 SilenceFrames = 0;
    bool bSpeechActive = false;
    TArray<float> PreRoll;
    TArray<float> Utterance;
};
