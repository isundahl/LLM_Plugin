#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LocalLLMDelegates.h"
#include "LocalLLMTypes.h"
#include "LocalLLMComponent.generated.h"

class ULocalLLMSubsystem;
class ULocalLLMCharacterSheet;

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class LOCALMULTIMODALLLM_API ULocalLLMComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    ULocalLLMComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local LLM")
    bool bLoadDefaultModelOnBeginPlay = false;

    /** High-frequency generation stream. Deliberately contains only routing IDs and text. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMTextDeltaDelegate OnTextDelta;

    /** A complete, validated tool request. Partial JSON fragments are never exposed here. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMToolCallDelegate OnToolCall;

    /** Low-frequency conversation-state mutations. SessionId identifies the affected character session. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMSubsystemStateChangedDelegate OnSubsystemStateChanged;

    /** Low-frequency lifecycle, completion, warning, safety, and error messages. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMStatusChangedDelegate OnStatusChanged;

    /** Detailed internal routing for C++ integrations. Intentionally hidden from Blueprint. */
    FLocalLLMEventDelegate OnInternalEvent;

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid LoadModel(const FString& ModelPath);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Models")
    FGuid LoadModelById(const FString& ModelId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Models")
    TArray<FLocalLLMModelInfo> GetAvailableModels() const;

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid SubmitText(const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid CreateCharacterSession(ULocalLLMCharacterSheet* CharacterSheet);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid CreateCharacterSessionFromProfile(const FLocalLLMCharacterProfile& Character);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid SubmitTextForSession(const FGuid& SessionId, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid SubmitImageForSession(const FGuid& SessionId, const FLocalLLMImageInput& Image, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid SubmitAudioForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    bool DestroyCharacterSession(const FGuid& SessionId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|World")
    FGuid SetSharedWorldContext(const FLocalLLMWorldContext& World);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool UpsertDynamicLoreFact(FLocalLLMDynamicLoreFact Fact);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool RemoveDynamicLoreFact(const FGuid& FactId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    void ClearDynamicLoreFacts();

    UFUNCTION(BlueprintPure, Category = "Local LLM|Dynamic Lore")
    TArray<FLocalLLMDynamicLoreFact> GetDynamicLoreFacts() const;

    UFUNCTION(BlueprintPure, Category = "Local LLM|Dynamic Lore")
    TArray<FLocalLLMDynamicLoreFact> GetVisibleDynamicLoreFactsForSession(const FGuid& SessionId) const;

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool CommitCharacterDevelopedFact(const FGuid& SessionId, ELocalLLMDynamicLoreCategory Category,
        FName Key, const FString& Value);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool SetSessionKnowledgeAreas(const FGuid& SessionId, const TArray<FName>& AreaIds);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    bool RegisterTool(const FLocalLLMToolDefinition& Tool);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    FGuid SubmitToolResult(const FGuid& SessionId, const FGuid& ToolCallId, const FString& ResultJson, bool bSuccess = true);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Relationship")
    FGuid EvaluateRelationshipForSession(const FGuid& SessionId, bool bApplyChanges = true);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Relationship")
    bool SetRelationshipRating(const FGuid& SessionId, FName CriterionName, int32 Rating);

    UFUNCTION(BlueprintPure, Category = "Local LLM|Relationship")
    bool GetRelationshipState(const FGuid& SessionId, FLocalLLMRelationshipEvaluationSettings& OutState) const;

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Memory")
    FGuid CompactConversationForSession(const FGuid& SessionId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Memory")
    FGuid UndoLastConversationTurnForSession(const FGuid& SessionId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid SubmitImage(const FLocalLLMImageInput& Image, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid SubmitAudio(const FLocalLLMAudioInput& Audio, const FString& Prompt);

    /** Loads the configured recognizer without submitting audio. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech To Text")
    FGuid PreloadSpeechToText();

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    void Cancel(const FGuid& RequestId);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleSubsystemEvent(const FLocalLLMEvent& Event);

    ULocalLLMSubsystem* GetLocalLLMSubsystem() const;
};
