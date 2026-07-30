#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"

using FLocalLLMEventSink = TFunction<void(FLocalLLMEvent&&)>;

class ILocalMultimodalBackend
{
public:
    virtual ~ILocalMultimodalBackend() = default;
    virtual void LoadModel(const FLocalLLMModelConfig& Config, const FGuid& RequestId) = 0;
    virtual void UnloadModel(const FGuid& RequestId) = 0;
    virtual void CreateSession(const FGuid& SessionId, const FLocalLLMCharacterProfile& Character, const FGuid& RequestId) = 0;
    virtual void UpdateSession(const FGuid& SessionId, const FLocalLLMCharacterProfile& Character, const FGuid& RequestId) = 0;
    virtual void DestroySession(const FGuid& SessionId, const FGuid& RequestId) = 0;
    virtual void UpdateWorldContext(const FLocalLLMWorldContext& World, const FGuid& RequestId) = 0;
    virtual void UpdateTools(const TArray<FLocalLLMToolDefinition>& Tools, const FGuid& RequestId) = 0;
    virtual void SubmitToolResult(const FGuid& SessionId, const FGuid& ToolCallId, const FString& ResultJson, bool bSuccess, const FGuid& RequestId) = 0;
    virtual void SubmitText(const FGuid& SessionId, const FString& Prompt, const FGuid& RequestId) = 0;
    virtual void SubmitImage(const FGuid& SessionId, const FLocalLLMImageInput& Image, const FString& Prompt, const FGuid& RequestId) = 0;
    virtual void SubmitAudio(const FGuid& SessionId, const FLocalLLMAudioInput& Audio, const FString& Prompt, const FGuid& RequestId) = 0;
    virtual bool SupportsNativeAudio() const = 0;
    virtual void EvaluateRelationship(const FGuid& SessionId, bool bApplyChanges, const FGuid& RequestId) = 0;
    virtual void SetRelationshipRating(const FGuid& SessionId, FName CriterionName, int32 Rating, const FGuid& RequestId) = 0;
    virtual void CompactConversation(const FGuid& SessionId, const FGuid& RequestId) = 0;
    virtual void UndoLastConversationTurn(const FGuid& SessionId, const FGuid& RequestId) = 0;
    virtual void Cancel(const FGuid& RequestId) = 0;
    virtual void ResetConversation(const FGuid& SessionId, const FGuid& RequestId) = 0;
};

using FLocalLLMCancelCheck = TFunction<bool()>;

TUniquePtr<ILocalMultimodalBackend> CreateLocalLLMBackend(
    ELocalLLMBackend Backend,
    FLocalLLMEventSink EventSink,
    FLocalLLMCancelCheck CancelCheck);
TUniquePtr<ILocalMultimodalBackend> CreateLlamaCppBackend(FLocalLLMEventSink EventSink, FLocalLLMCancelCheck CancelCheck);
