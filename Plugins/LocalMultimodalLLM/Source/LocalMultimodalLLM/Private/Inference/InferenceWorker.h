#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "LocalLLMTypes.h"

class FRunnableThread;
class ILocalMultimodalBackend;
class ILocalSpeechToTextBackend;
class ILocalSpeakerEmbeddingBackend;

enum class ELocalLLMCommandType : uint8
{
    LoadModel,
    UnloadModel,
    CreateSession,
    UpdateSession,
    DestroySession,
    UpdateWorldContext,
    UpdateTools,
    SubmitToolResult,
    SubmitText,
    SubmitImage,
    SubmitAudio,
    TranscribeAudio,
    PreloadSpeechToText,
    CreateSpeakerProfile,
    VerifySpeaker,
    EvaluateRelationship,
    SetRelationshipRating,
    CompactConversation,
    UndoConversationTurn,
    Cancel,
    ResetConversation
};

struct FLocalLLMCommand
{
    ELocalLLMCommandType Type = ELocalLLMCommandType::SubmitText;
    ELocalLLMBackend Backend = ELocalLLMBackend::Mock;
    FGuid RequestId;
    FGuid SessionId;
    FString Text;
    FLocalLLMModelConfig ModelConfig;
    FLocalLLMImageInput Image;
    FLocalLLMAudioInput Audio;
    ELocalLLMAudioInputStrategy AudioInputStrategy = ELocalLLMAudioInputStrategy::Auto;
    FLocalLLMSpeechToTextConfig SpeechToText;
    TArray<FLocalLLMSpeechVocabularyEntry> SpeechVocabulary;
    TArray<FName> ActiveSpeechVocabularyTags;
    FLocalLLMSpeakerVerificationConfig SpeakerVerification;
    FLocalLLMSpeakerProfile SpeakerProfile;
    FLocalLLMCharacterProfile Character;
    FLocalLLMWorldContext World;
    TArray<FLocalLLMToolDefinition> Tools;
    FGuid ToolCallId;
    FName CriterionName;
    int32 RelationshipRating = 5;
    bool bApplyRelationshipChanges = true;
    bool bToolSuccess = true;
};

class FLocalLLMInferenceWorker final : public FRunnable
{
public:
    FLocalLLMInferenceWorker();
    virtual ~FLocalLLMInferenceWorker() override;
    void Enqueue(FLocalLLMCommand&& Command);
    bool DequeueEvent(FLocalLLMEvent& OutEvent);
    virtual uint32 Run() override;
    virtual void Stop() override;

private:
    void ProcessCommand(FLocalLLMCommand&& Command);
    void ProcessAudio(FLocalLLMCommand&& Command);
    void ProcessSpeechToTextPreload(FLocalLLMCommand&& Command);
    void ProcessTranscription(FLocalLLMCommand&& Command, bool bSubmitText);
    bool EnsureSpeechToTextLoaded(const FLocalLLMSpeechToTextConfig& Config, FString& OutError);
    void ProcessSpeakerEmbedding(FLocalLLMCommand&& Command, bool bCreateProfile);
    void PushEvent(FLocalLLMEvent&& Event);

    TQueue<FLocalLLMCommand, EQueueMode::Mpsc> Commands;
    TQueue<FLocalLLMEvent, EQueueMode::Spsc> Events;
    TUniquePtr<ILocalMultimodalBackend> Backend;
    TUniquePtr<ILocalSpeechToTextBackend> SpeechToTextBackend;
    TUniquePtr<ILocalSpeakerEmbeddingBackend> SpeakerEmbeddingBackend;
    ELocalLLMBackend ActiveBackend = ELocalLLMBackend::Mock;
    FName ActiveSpeechToTextProvider;
    FString ActiveSpeechToTextModelPath;
    FName ActiveSpeakerEmbeddingProvider;
    FString ActiveSpeakerEmbeddingModelPath;
    FRunnableThread* Thread = nullptr;
    FEvent* WakeEvent = nullptr;
    TAtomic<bool> bStopRequested = false;
    TAtomic<bool> bCancelRequested = false;
    TMap<FGuid, FLocalLLMCharacterProfile> SessionDefinitions;
    FLocalLLMWorldContext SharedWorld;
    bool bHasSharedWorld = false;
    TArray<FLocalLLMToolDefinition> RegisteredTools;
};
