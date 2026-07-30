#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LocalLLMDelegates.h"
#include "LocalLLMToolExecutorComponent.generated.h"

class ULocalLLMSubsystem;
class ULocalLLMToolSet;

/**
 * Safe Blueprint bridge for validated LLM tool requests. It never invokes a
 * UFunction by name; game code must explicitly handle each allow-listed tool.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class LOCALMULTIMODALLLM_API ULocalLLMToolExecutorComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    ULocalLLMToolExecutorComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Local LLM|Tools")
    TObjectPtr<ULocalLLMToolSet> ToolSet;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM|Tools")
    bool bRegisterToolSetOnBeginPlay = true;

    /** Fired only after the plugin has validated tool name, arguments, and types. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Tools")
    FLocalLLMToolCallDelegate OnToolCall;

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    int32 RegisterConfiguredToolSet();

    /** Complete a pending call after the explicit Blueprint/C++ implementation runs. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    FGuid CompleteToolCall(const FGuid& ToolCallId, const FString& ResultJson, bool bSuccess = true);

    /** Reject or cancel a pending call, including a declined confirmation prompt. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    FGuid RejectToolCall(const FGuid& ToolCallId, const FString& Reason);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleToolCall(FGuid SessionId, FName CharacterId, FGuid ToolCallId,
        const FString& ToolName, const FString& ArgumentsJson, bool bRequiresPlayerConfirmation);

    ULocalLLMSubsystem* GetLocalLLMSubsystem() const;
    TMap<FGuid, FGuid> PendingSessions;
};
