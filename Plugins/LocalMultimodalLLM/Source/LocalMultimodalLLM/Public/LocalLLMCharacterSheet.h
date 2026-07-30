#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LocalLLMTypes.h"
#include "LocalLLMCharacterSheet.generated.h"

UCLASS(BlueprintType)
class LOCALMULTIMODALLLM_API ULocalLLMCharacterSheet final : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Local LLM|Character")
    FLocalLLMCharacterProfile Character;
};

UCLASS(BlueprintType)
class LOCALMULTIMODALLLM_API ULocalLLMWorldSheet final : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Local LLM|World")
    FLocalLLMWorldContext World;
};

UCLASS(BlueprintType)
class LOCALMULTIMODALLLM_API ULocalLLMToolSet final : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Local LLM|Tools")
    TArray<FLocalLLMToolDefinition> Tools;
};
