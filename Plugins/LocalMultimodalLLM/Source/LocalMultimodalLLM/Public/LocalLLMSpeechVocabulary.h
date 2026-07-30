#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LocalLLMTypes.h"
#include "LocalLLMSpeechVocabulary.generated.h"

/** Reusable, explicitly authored vocabulary for a scene, world, or quest. */
UCLASS(BlueprintType)
class LOCALMULTIMODALLLM_API ULocalLLMSpeechVocabulary final : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Local LLM|Speech Vocabulary")
    TArray<FLocalLLMSpeechVocabularyEntry> Entries;
};

UCLASS()
class LOCALMULTIMODALLLM_API ULocalLLMSpeechVocabularyLibrary final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Applies only exact, active, complete-span KnownAsrVariants. Ambiguous variants remain unchanged. */
    UFUNCTION(BlueprintPure, Category = "Local LLM|Speech Vocabulary")
    static FLocalLLMTranscriptNormalizationResult NormalizeTranscript(
        const FString& Transcript,
        const TArray<FLocalLLMSpeechVocabularyEntry>& Entries,
        const TArray<FName>& ActiveTags);
};
