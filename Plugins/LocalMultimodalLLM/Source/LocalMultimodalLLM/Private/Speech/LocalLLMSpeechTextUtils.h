#pragma once

#include "CoreMinimal.h"

namespace LocalLLMSpeechTextUtils
{
/** Splits long queued speech at the natural boundary nearest the preferred fraction. */
TArray<FString> SplitQueuedSpeech(const FString& Text, int32 MaxCharacters, float PreferredSplitFraction);

/** Validates a completed batch before any PCM is published to playback or facial animation. */
bool ValidateBatchDuration(int32 SampleCount, int32 SampleRate, int32 NumChannels,
    double MaximumSeconds, double& OutDurationSeconds);
}
