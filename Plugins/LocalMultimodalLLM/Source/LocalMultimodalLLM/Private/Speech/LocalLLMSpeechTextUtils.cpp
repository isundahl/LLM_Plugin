#include "Speech/LocalLLMSpeechTextUtils.h"

namespace
{
int32 FindNaturalSplit(const FString& Text, const float PreferredFraction)
{
    constexpr int32 MinimumPartCharacters = 24;
    if (Text.Len() < MinimumPartCharacters * 2) return INDEX_NONE;

    const int32 Target = FMath::Clamp(
        FMath::RoundToInt(Text.Len() * FMath::Clamp(PreferredFraction, 0.5f, 0.7f)),
        MinimumPartCharacters, Text.Len() - MinimumPartCharacters);
    const int32 SearchStart = FMath::Max(MinimumPartCharacters, FMath::RoundToInt(Text.Len() * 0.35f));
    const int32 SearchEnd = FMath::Min(Text.Len() - MinimumPartCharacters,
        FMath::RoundToInt(Text.Len() * 0.75f));

    TArray<int32> Candidates;
    for (int32 Index = SearchStart; Index <= SearchEnd; ++Index)
    {
        const TCHAR Ch = Text[Index];
        if (Ch == TEXT(',') || Ch == TEXT(';') || Ch == TEXT(':') ||
            Ch == TEXT('-') || Ch == static_cast<TCHAR>(0x2013) ||
            Ch == static_cast<TCHAR>(0x2014))
        {
            Candidates.Add(Index + 1);
        }
    }

    static const TCHAR* Boundaries[] =
    {
        TEXT(" and "), TEXT(" but "), TEXT(" because "), TEXT(" while "),
        TEXT(" although "), TEXT(" who "), TEXT(" which "), TEXT(" so "), TEXT(" or ")
    };
    for (const TCHAR* Boundary : Boundaries)
    {
        int32 SearchAt = SearchStart;
        while (SearchAt <= SearchEnd)
        {
            const int32 Index = Text.Find(Boundary, ESearchCase::IgnoreCase,
                ESearchDir::FromStart, SearchAt);
            if (Index == INDEX_NONE || Index > SearchEnd) break;
            Candidates.Add(Index + 1); // Preserve the conjunction on the following spoken segment.
            SearchAt = Index + 1;
        }
    }

    if (Candidates.IsEmpty())
    {
        int32 BestWhitespace = INDEX_NONE;
        int32 BestDistance = MAX_int32;
        for (int32 Index = SearchStart; Index <= SearchEnd; ++Index)
        {
            if (!FChar::IsWhitespace(Text[Index])) continue;
            const int32 Distance = FMath::Abs(Index - Target);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestWhitespace = Index;
            }
        }
        return BestWhitespace;
    }

    int32 Best = Candidates[0];
    int32 BestDistance = FMath::Abs(Best - Target);
    for (const int32 Candidate : Candidates)
    {
        const int32 Distance = FMath::Abs(Candidate - Target);
        if (Distance < BestDistance)
        {
            Best = Candidate;
            BestDistance = Distance;
        }
    }
    return Best;
}
}

TArray<FString> LocalLLMSpeechTextUtils::SplitQueuedSpeech(
    const FString& Text, const int32 MaxCharacters, const float PreferredSplitFraction)
{
    TArray<FString> Pending = { Text.TrimStartAndEnd() };
    TArray<FString> Result;
    while (!Pending.IsEmpty())
    {
        FString Segment = MoveTemp(Pending[0]);
        Pending.RemoveAt(0, EAllowShrinking::No);
        if (MaxCharacters <= 0 || Segment.Len() <= MaxCharacters)
        {
            if (!Segment.IsEmpty()) Result.Add(MoveTemp(Segment));
            continue;
        }

        const int32 SplitAt = FindNaturalSplit(Segment, PreferredSplitFraction);
        if (SplitAt == INDEX_NONE)
        {
            Result.Add(MoveTemp(Segment));
            continue;
        }

        FString First = Segment.Left(SplitAt).TrimStartAndEnd();
        FString Second = Segment.Mid(SplitAt).TrimStartAndEnd();
        if (First.IsEmpty() || Second.IsEmpty())
        {
            Result.Add(MoveTemp(Segment));
            continue;
        }
        Pending.Insert(MoveTemp(Second), 0);
        Pending.Insert(MoveTemp(First), 0);
    }
    return Result;
}

bool LocalLLMSpeechTextUtils::ValidateBatchDuration(
    const int32 SampleCount, const int32 SampleRate, const int32 NumChannels,
    const double MaximumSeconds, double& OutDurationSeconds)
{
    OutDurationSeconds = 0.0;
    if (SampleCount <= 0 || SampleRate <= 0 || NumChannels <= 0 || MaximumSeconds <= 0.0) return false;
    OutDurationSeconds = static_cast<double>(SampleCount) /
        (static_cast<double>(SampleRate) * NumChannels);
    return OutDurationSeconds <= MaximumSeconds + UE_DOUBLE_SMALL_NUMBER;
}
