// Copyright 2026 Ian Sundahl, Volley Studios. SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LocalLLMDelegates.h"
#include "LocalLLMTypes.h"
#include "LocalLLMSubsystem.generated.h"

class FLocalLLMInferenceWorker;
class ULocalLLMCharacterSheet;
class ULocalLLMWorldSheet;
class ULocalLLMToolSet;
class ULocalLLMSpeechVocabulary;

/** Keeps the worker implementation private while allowing generated code to destroy the pointer safely. */
struct FLocalLLMInferenceWorkerDeleter
{
    void operator()(FLocalLLMInferenceWorker* Worker) const;
};

UCLASS()
class LOCALMULTIMODALLLM_API ULocalLLMSubsystem final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Detailed internal event bus. Use the focused Blueprint delegates below in gameplay graphs. */
    FLocalLLMEventDelegate OnInternalEvent;

    /** High-frequency generation stream with no unrelated telemetry pins. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMTextDeltaDelegate OnTextDelta;

    /** Complete tool calls only; partial argument fragments remain internal. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMToolCallDelegate OnToolCall;

    /** Low-frequency compaction, relationship, and rollback notifications. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMSubsystemStateChangedDelegate OnSubsystemStateChanged;

    /** Low-frequency lifecycle, completion, warning, safety, and error messages. */
    UPROPERTY(BlueprintAssignable, Category = "Local LLM|Events")
    FLocalLLMStatusChangedDelegate OnStatusChanged;

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid LoadModel(const FString& ModelPath);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Models")
    FGuid LoadModelById(const FString& ModelId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Models")
    TArray<FLocalLLMModelInfo> GetAvailableModels() const;

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid LoadDefaultModel();

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid UnloadModel();

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid SubmitText(const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid CreateCharacterSession(ULocalLLMCharacterSheet* CharacterSheet);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid CreateCharacterSessionFromProfile(const FLocalLLMCharacterProfile& Character);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    bool UpdateCharacterSession(const FGuid& SessionId, const FLocalLLMCharacterProfile& Character);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    bool DestroyCharacterSession(const FGuid& SessionId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid SubmitTextForSession(const FGuid& SessionId, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid SubmitImage(const FLocalLLMImageInput& Image, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid SubmitImageForSession(const FGuid& SessionId, const FLocalLLMImageInput& Image, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid SubmitAudio(const FLocalLLMAudioInput& Audio, const FString& Prompt);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid SubmitAudioForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio, const FString& Prompt);

    /** Transcribe audio without adding it to conversation history or starting an LLM turn. Useful for live partial captions. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech To Text")
    FGuid TranscribeAudio(const FLocalLLMAudioInput& Audio);

    /** Loads the configured recognizer without submitting audio, removing its first-utterance load penalty. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech To Text")
    FGuid PreloadSpeechToText();

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech To Text")
    FGuid TranscribeAudioForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech Vocabulary")
    void SetSpeechVocabulary(ULocalLLMSpeechVocabulary* Vocabulary);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech Vocabulary")
    void SetSpeechVocabularyEntries(const TArray<FLocalLLMSpeechVocabularyEntry>& Entries);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech Vocabulary")
    void AddSpeechVocabularyEntry(const FLocalLLMSpeechVocabularyEntry& Entry);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech Vocabulary")
    void ClearSpeechVocabulary();

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speech Vocabulary")
    void SetActiveSpeechVocabularyTags(const TArray<FName>& ActiveTags);

    UFUNCTION(BlueprintPure, Category = "Local LLM|Speech Vocabulary")
    FLocalLLMTranscriptNormalizationResult NormalizeTranscript(const FString& Transcript) const;

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speaker")
    FGuid CreateSpeakerProfileForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio,
        const FLocalLLMSpeakerVerificationConfig& Config, const FString& DisplayName = TEXT("Player"));

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Speaker")
    FGuid VerifySpeakerForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio,
        const FLocalLLMSpeakerProfile& Profile, const FLocalLLMSpeakerVerificationConfig& Config);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|World")
    FGuid SetSharedWorldContext(const FLocalLLMWorldContext& World);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|World")
    FGuid SetSharedWorldFromSheet(ULocalLLMWorldSheet* WorldSheet);

    /** Add or replace a validated runtime fact. Game-authored callers may use any category or scope. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool UpsertDynamicLoreFact(FLocalLLMDynamicLoreFact Fact);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool RemoveDynamicLoreFact(const FGuid& FactId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    void ClearDynamicLoreFacts();

    UFUNCTION(BlueprintPure, Category = "Local LLM|Dynamic Lore")
    TArray<FLocalLLMDynamicLoreFact> GetDynamicLoreFacts() const { return SharedWorld.DynamicLore; }

    UFUNCTION(BlueprintPure, Category = "Local LLM|Dynamic Lore")
    TArray<FLocalLLMDynamicLoreFact> GetVisibleDynamicLoreFactsForSession(const FGuid& SessionId) const;

    /** Commit one private character-developed fact after applying the safe-category and size policy. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool CommitCharacterDevelopedFact(const FGuid& SessionId, ELocalLLMDynamicLoreCategory Category,
        FName Key, const FString& Value);

    /** Update the area/level lore visible to one character without replacing the rest of its profile. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Dynamic Lore")
    bool SetSessionKnowledgeAreas(const FGuid& SessionId, const TArray<FName>& AreaIds);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    bool RegisterTool(const FLocalLLMToolDefinition& Tool);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    int32 RegisterToolSet(ULocalLLMToolSet* ToolSet);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    bool UnregisterTool(const FString& ToolName);

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    void ClearTools();

    UFUNCTION(BlueprintPure, Category = "Local LLM|Tools")
    TArray<FLocalLLMToolDefinition> GetRegisteredTools() const;

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Tools")
    FGuid SubmitToolResult(const FGuid& SessionId, const FGuid& ToolCallId, const FString& ResultJson, bool bSuccess = true);

    /** Evaluate only conversation messages accumulated since the last applied evaluation. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Relationship")
    FGuid EvaluateRelationshipForSession(const FGuid& SessionId, bool bApplyChanges = true);

    /** Game-authoritative override; the value is clamped to the 0-10 relationship scale. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Relationship")
    bool SetRelationshipRating(const FGuid& SessionId, FName CriterionName, int32 Rating);

    UFUNCTION(BlueprintPure, Category = "Local LLM|Relationship")
    bool GetRelationshipState(const FGuid& SessionId, FLocalLLMRelationshipEvaluationSettings& OutState) const;

    /** Summarize expired history now while preserving the configured number of recent complete turns. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Memory")
    FGuid CompactConversationForSession(const FGuid& SessionId);

    /** Restores the last atomic turn checkpoint. External tool effects are reported but cannot be reversed automatically. */
    UFUNCTION(BlueprintCallable, Category = "Local LLM|Memory")
    FGuid UndoLastConversationTurnForSession(const FGuid& SessionId);

    UFUNCTION(BlueprintPure, Category = "Local LLM|Session")
    TArray<FLocalLLMSessionInfo> GetCharacterSessions() const;

    UFUNCTION(BlueprintPure, Category = "Local LLM|Session")
    FGuid GetDefaultSessionId() const { return DefaultSessionId; }

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    void Cancel(const FGuid& RequestId);

    UFUNCTION(BlueprintCallable, Category = "Local LLM")
    FGuid ResetConversation();

    UFUNCTION(BlueprintCallable, Category = "Local LLM|Session")
    FGuid ResetConversationForSession(const FGuid& SessionId);

    UFUNCTION(BlueprintPure, Category = "Local LLM")
    bool IsModelLoaded() const { return bModelLoaded; }

private:
    bool Tick(float DeltaTime);
    void EnqueueCommand(uint8 CommandType, const FGuid& RequestId, const FGuid& SessionId = {}, FString Text = {}, FLocalLLMAudioInput Audio = {},
        ELocalLLMAudioInputStrategy AudioInputStrategy = ELocalLLMAudioInputStrategy::Auto, FLocalLLMSpeechToTextConfig SpeechToText = {});
    void DispatchEvent(const FLocalLLMEvent& Event);
    bool ValidateSession(const FGuid& SessionId, const FGuid& RequestId);

    TUniquePtr<FLocalLLMInferenceWorker, FLocalLLMInferenceWorkerDeleter> Worker;
    FTSTicker::FDelegateHandle TickHandle;
    bool bModelLoaded = false;
    FString LoadedModelId;
    FGuid DefaultSessionId;
    FLocalLLMWorldContext SharedWorld;
    TMap<FGuid, FLocalLLMSessionInfo> Sessions;
    TMap<FGuid, FLocalLLMCharacterProfile> SessionProfiles;
    TMap<FGuid, FLocalLLMRelationshipEvaluationSettings> RelationshipStates;
    TMap<FString, FLocalLLMToolDefinition> Tools;
    TArray<FLocalLLMSpeechVocabularyEntry> SpeechVocabularyEntries;
    TArray<FName> ActiveSpeechVocabularyTags;
};
