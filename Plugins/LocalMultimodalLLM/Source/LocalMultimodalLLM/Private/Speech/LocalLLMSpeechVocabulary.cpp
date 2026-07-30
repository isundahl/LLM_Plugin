#include "LocalLLMSpeechVocabulary.h"

namespace
{
bool IsBoundary(const FString& Text, const int32 Index)
{
    return Index < 0 || Index >= Text.Len() || (!FChar::IsAlnum(Text[Index]) && Text[Index] != TEXT('_'));
}

bool IsEntryActive(const FLocalLLMSpeechVocabularyEntry& Entry, const TArray<FName>& ActiveTags)
{
    for (const FName Required : Entry.ActivationTags)
        if (!ActiveTags.Contains(Required)) return false;
    return true;
}

bool ContainsBounded(const FString& Text, const FString& Phrase)
{
    int32 SearchFrom = 0;
    while (SearchFrom < Text.Len())
    {
        const int32 Index = Text.Find(Phrase, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
        if (Index == INDEX_NONE) return false;
        const int32 End = Index + Phrase.Len();
        if (IsBoundary(Text, Index - 1) && IsBoundary(Text, End)) return true;
        SearchFrom = Index + 1;
    }
    return false;
}

struct FVariantCandidate
{
    FString Variant;
    const FLocalLLMSpeechVocabularyEntry* Entry = nullptr;
};
}

FLocalLLMTranscriptNormalizationResult ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
    const FString& Transcript,
    const TArray<FLocalLLMSpeechVocabularyEntry>& Entries,
    const TArray<FName>& ActiveTags)
{
    FLocalLLMTranscriptNormalizationResult Result;
    Result.RawTranscript = Transcript;
    Result.CanonicalTranscript = Transcript;

    TArray<FVariantCandidate> Candidates;
    TMap<FString, TArray<const FLocalLLMSpeechVocabularyEntry*>> OwnersByVariant;
    for (const FLocalLLMSpeechVocabularyEntry& Entry : Entries)
    {
        if (!Entry.bAllowBodyCorrection || Entry.CanonicalText.IsEmpty() || !IsEntryActive(Entry, ActiveTags)) continue;
        for (FString Variant : Entry.KnownAsrVariants)
        {
            Variant.TrimStartAndEndInline();
            if (Variant.IsEmpty() || Variant.Equals(Entry.CanonicalText, ESearchCase::IgnoreCase)) continue;
            OwnersByVariant.FindOrAdd(Variant.ToLower()).Add(&Entry);
        }
    }

    for (const TPair<FString, TArray<const FLocalLLMSpeechVocabularyEntry*>>& Pair : OwnersByVariant)
    {
        if (Pair.Value.Num() != 1)
        {
            if (ContainsBounded(Result.CanonicalTranscript, Pair.Key))
            {
                Result.bNeedsConfirmation = true;
                Result.AmbiguousVariant = Pair.Key;
                Result.Confidence = 0.0f;
            }
            continue;
        }
        FVariantCandidate& Candidate = Candidates.AddDefaulted_GetRef();
        Candidate.Variant = Pair.Key;
        Candidate.Entry = Pair.Value[0];
    }

    Candidates.Sort([](const FVariantCandidate& A, const FVariantCandidate& B)
    {
        if (A.Variant.Len() != B.Variant.Len()) return A.Variant.Len() > B.Variant.Len();
        return A.Entry->Priority > B.Entry->Priority;
    });

    for (const FVariantCandidate& Candidate : Candidates)
    {
        int32 SearchFrom = 0;
        while (SearchFrom < Result.CanonicalTranscript.Len())
        {
            const int32 Index = Result.CanonicalTranscript.Find(
                Candidate.Variant, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
            if (Index == INDEX_NONE) break;
            const int32 End = Index + Candidate.Variant.Len();
            if (!IsBoundary(Result.CanonicalTranscript, Index - 1) || !IsBoundary(Result.CanonicalTranscript, End))
            {
                SearchFrom = Index + 1;
                continue;
            }

            FLocalLLMTranscriptCorrection& Correction = Result.Corrections.AddDefaulted_GetRef();
            Correction.OriginalText = Result.CanonicalTranscript.Mid(Index, Candidate.Variant.Len());
            Correction.CanonicalText = Candidate.Entry->CanonicalText;
            Correction.EntityId = Candidate.Entry->EntityId;
            Correction.EntityType = Candidate.Entry->EntityType;
            Correction.StartIndex = Index;
            Result.CanonicalTranscript.RemoveAt(Index, Candidate.Variant.Len(), EAllowShrinking::No);
            Result.CanonicalTranscript.InsertAt(Index, Candidate.Entry->CanonicalText);
            SearchFrom = Index + Candidate.Entry->CanonicalText.Len();
        }
    }
    return Result;
}
