#include "Backends/ILocalMultimodalBackend.h"

namespace
{
FLocalLLMEvent MakeTextEvent(const ELocalLLMEventType Type, const FGuid& RequestId, FString Text = {}, const FGuid& SessionId = {}, const FName CharacterId = NAME_None)
{
    FLocalLLMEvent Event;
    Event.Type = Type;
    Event.RequestId = RequestId;
    Event.SessionId = SessionId;
    Event.CharacterId = CharacterId;
    Event.Text = MoveTemp(Text);
    return Event;
}

class FMockLocalLLMBackend final : public ILocalMultimodalBackend
{
public:
    explicit FMockLocalLLMBackend(FLocalLLMEventSink&& InEventSink) : EventSink(MoveTemp(InEventSink)) {}

    virtual void LoadModel(const FLocalLLMModelConfig& Config, const FGuid& RequestId) override
    {
        bLoaded = true;
        EventSink(MakeTextEvent(ELocalLLMEventType::ModelLoaded, RequestId,
            Config.ModelPath.IsEmpty() ? TEXT("Mock backend loaded") : FString::Printf(TEXT("Mock backend loaded: %s"), *Config.DisplayName)));
    }
    virtual void UnloadModel(const FGuid& RequestId) override
    {
        bLoaded = false;
        EventSink(MakeTextEvent(ELocalLLMEventType::ModelUnloaded, RequestId));
    }
    virtual void CreateSession(const FGuid& SessionId, const FLocalLLMCharacterProfile& Character, const FGuid& RequestId) override
    {
        Sessions.Add(SessionId, Character);
        EventSink(MakeTextEvent(ELocalLLMEventType::SessionCreated, RequestId, TEXT("Session created"), SessionId, Character.CharacterId));
    }
    virtual void UpdateSession(const FGuid& SessionId, const FLocalLLMCharacterProfile& Character, const FGuid& RequestId) override
    {
        Sessions.Add(SessionId, Character);
        EventSink(MakeTextEvent(ELocalLLMEventType::SessionCreated, RequestId, TEXT("Session updated"), SessionId, Character.CharacterId));
    }
    virtual void DestroySession(const FGuid& SessionId, const FGuid& RequestId) override
    {
        const FName CharacterId = Sessions.FindRef(SessionId).CharacterId;
        Sessions.Remove(SessionId);
        EventSink(MakeTextEvent(ELocalLLMEventType::SessionDestroyed, RequestId, TEXT("Session destroyed"), SessionId, CharacterId));
    }
    virtual void UpdateWorldContext(const FLocalLLMWorldContext& World, const FGuid& RequestId) override
    {
        SharedWorld = World;
        EventSink(MakeTextEvent(ELocalLLMEventType::TurnCompleted, RequestId, TEXT("Shared world context updated")));
    }
    virtual void UpdateTools(const TArray<FLocalLLMToolDefinition>& InTools, const FGuid& RequestId) override
    {
        Tools = InTools;
        EventSink(MakeTextEvent(ELocalLLMEventType::TurnCompleted, RequestId, TEXT("Tool registry updated")));
    }
    virtual void SubmitToolResult(const FGuid& SessionId, const FGuid&, const FString& ResultJson, bool, const FGuid& RequestId) override
    {
        SubmitText(SessionId, FString::Printf(TEXT("Mock tool result: %s"), *ResultJson), RequestId);
    }
    virtual void SubmitText(const FGuid& SessionId, const FString& Prompt, const FGuid& RequestId) override
    {
        if (!EnsureLoaded(RequestId)) return;
        const FName CharacterId = Sessions.FindRef(SessionId).CharacterId;
        EventSink(MakeTextEvent(ELocalLLMEventType::TextDelta, RequestId, FString::Printf(TEXT("Mock response: %s"), *Prompt), SessionId, CharacterId));
        EventSink(MakeTextEvent(ELocalLLMEventType::TurnCompleted, RequestId, {}, SessionId, CharacterId));
    }
    virtual void SubmitImage(const FGuid& SessionId, const FLocalLLMImageInput&, const FString& Prompt, const FGuid& RequestId) override
    {
        SubmitText(SessionId, FString::Printf(TEXT("[Mock image] %s"), *Prompt), RequestId);
    }
    virtual void SubmitAudio(const FGuid& SessionId, const FLocalLLMAudioInput& Audio, const FString& Prompt, const FGuid& RequestId) override
    {
        if (!EnsureLoaded(RequestId)) return;
        const double Duration = static_cast<double>(Audio.Samples.Num()) / static_cast<double>(Audio.SampleRate * Audio.NumChannels);
        const FName CharacterId = Sessions.FindRef(SessionId).CharacterId;
        EventSink(MakeTextEvent(ELocalLLMEventType::TextDelta, RequestId,
            FString::Printf(TEXT("Mock audio response (%.2f seconds): %s"), Duration, *Prompt), SessionId, CharacterId));
        EventSink(MakeTextEvent(ELocalLLMEventType::TurnCompleted, RequestId, {}, SessionId, CharacterId));
    }
    virtual bool SupportsNativeAudio() const override { return true; }
    virtual void EvaluateRelationship(const FGuid& SessionId, const bool bApplyChanges, const FGuid& RequestId) override
    {
        FLocalLLMCharacterProfile* Character = Sessions.Find(SessionId);
        if (!Character)
        {
            EventSink(MakeTextEvent(ELocalLLMEventType::Error, RequestId, TEXT("Character session does not exist"), SessionId));
            return;
        }
        FLocalLLMEvent Event = MakeTextEvent(ELocalLLMEventType::RelationshipEvaluated, RequestId,
            TEXT("Mock evaluator returned no relationship change"), SessionId, Character->CharacterId);
        Event.Relationship.TargetId = Character->RelationshipEvaluation.TargetId;
        Event.Relationship.TargetDisplayName = Character->RelationshipEvaluation.TargetDisplayName;
        Event.Relationship.Confidence = 2;
        Event.Relationship.bApplied = bApplyChanges;
        for (const FLocalLLMRelationshipCriterion& Criterion : Character->RelationshipEvaluation.Criteria)
        {
            FLocalLLMRelationshipCriterionResult Result;
            Result.Name = Criterion.Name;
            Result.PreviousRating = Criterion.Rating;
            Result.NewRating = Criterion.Rating;
            Event.Relationship.Criteria.Add(Result);
        }
        EventSink(MoveTemp(Event));
    }
    virtual void SetRelationshipRating(const FGuid& SessionId, const FName CriterionName, const int32 Rating, const FGuid& RequestId) override
    {
        FLocalLLMCharacterProfile* Character = Sessions.Find(SessionId);
        if (!Character)
        {
            EventSink(MakeTextEvent(ELocalLLMEventType::Error, RequestId, TEXT("Character session does not exist"), SessionId));
            return;
        }
        for (FLocalLLMRelationshipCriterion& Criterion : Character->RelationshipEvaluation.Criteria)
        {
            if (Criterion.Name.IsEqual(CriterionName))
            {
                const int32 PreviousRating = Criterion.Rating;
                Criterion.Rating = FMath::Clamp(Rating, 0, 10);
                FLocalLLMEvent Event = MakeTextEvent(ELocalLLMEventType::RelationshipEvaluated, RequestId,
                    TEXT("Relationship rating updated"), SessionId, Character->CharacterId);
                Event.Relationship.TargetId = Character->RelationshipEvaluation.TargetId;
                Event.Relationship.TargetDisplayName = Character->RelationshipEvaluation.TargetDisplayName;
                Event.Relationship.bApplied = true;
                FLocalLLMRelationshipCriterionResult Result;
                Result.Name = Criterion.Name;
                Result.PreviousRating = PreviousRating;
                Result.SuggestedDelta = Criterion.Rating - PreviousRating;
                Result.AppliedDelta = Result.SuggestedDelta;
                Result.NewRating = Criterion.Rating;
                Event.Relationship.Criteria.Add(Result);
                EventSink(MoveTemp(Event));
                return;
            }
        }
        EventSink(MakeTextEvent(ELocalLLMEventType::Error, RequestId, TEXT("Relationship criterion does not exist"), SessionId, Character->CharacterId));
    }
    virtual void CompactConversation(const FGuid& SessionId, const FGuid& RequestId) override
    {
        EventSink(MakeTextEvent(ELocalLLMEventType::ConversationCompacted, RequestId,
            TEXT("Mock conversation compacted"), SessionId, Sessions.FindRef(SessionId).CharacterId));
    }
    virtual void UndoLastConversationTurn(const FGuid& SessionId, const FGuid& RequestId) override
    {
        FLocalLLMEvent Event = MakeTextEvent(ELocalLLMEventType::ConversationTurnUndone, RequestId,
            TEXT("Mock conversation turn undone"), SessionId, Sessions.FindRef(SessionId).CharacterId);
        Event.Rollback.bUndone = true;
        Event.Rollback.RemovedMessageCount = 2;
        Event.Rollback.DialogueEventId = FGuid::NewGuid();
        Event.DialogueEventId = Event.Rollback.DialogueEventId;
        EventSink(MoveTemp(Event));
    }
    virtual void Cancel(const FGuid& RequestId) override
    {
        EventSink(MakeTextEvent(ELocalLLMEventType::TurnCompleted, RequestId, TEXT("Cancelled")));
    }
    virtual void ResetConversation(const FGuid& SessionId, const FGuid& RequestId) override
    {
        EventSink(MakeTextEvent(ELocalLLMEventType::TurnCompleted, RequestId, TEXT("Conversation reset"), SessionId, Sessions.FindRef(SessionId).CharacterId));
    }

private:
    bool EnsureLoaded(const FGuid& RequestId)
    {
        if (bLoaded) return true;
        EventSink(MakeTextEvent(ELocalLLMEventType::Error, RequestId, TEXT("No model is loaded")));
        return false;
    }
    FLocalLLMEventSink EventSink;
    bool bLoaded = false;
    TMap<FGuid, FLocalLLMCharacterProfile> Sessions;
    FLocalLLMWorldContext SharedWorld;
    TArray<FLocalLLMToolDefinition> Tools;
};

class FUnavailableLlamaCppBackend final : public ILocalMultimodalBackend
{
public:
    explicit FUnavailableLlamaCppBackend(FLocalLLMEventSink&& InEventSink) : EventSink(MoveTemp(InEventSink)) {}
    virtual void LoadModel(const FLocalLLMModelConfig&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void UnloadModel(const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void CreateSession(const FGuid&, const FLocalLLMCharacterProfile&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void UpdateSession(const FGuid&, const FLocalLLMCharacterProfile&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void DestroySession(const FGuid&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void UpdateWorldContext(const FLocalLLMWorldContext&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void UpdateTools(const TArray<FLocalLLMToolDefinition>&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void SubmitToolResult(const FGuid&, const FGuid&, const FString&, bool, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void SubmitText(const FGuid&, const FString&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void SubmitImage(const FGuid&, const FLocalLLMImageInput&, const FString&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void SubmitAudio(const FGuid&, const FLocalLLMAudioInput&, const FString&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual bool SupportsNativeAudio() const override { return false; }
    virtual void EvaluateRelationship(const FGuid&, bool, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void SetRelationshipRating(const FGuid&, FName, int32, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void CompactConversation(const FGuid&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void UndoLastConversationTurn(const FGuid&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void Cancel(const FGuid& RequestId) override { ReportUnavailable(RequestId); }
    virtual void ResetConversation(const FGuid&, const FGuid& RequestId) override { ReportUnavailable(RequestId); }

private:
    void ReportUnavailable(const FGuid& RequestId)
    {
#if LOCAL_MULTIMODAL_LLM_WITH_LLAMA
        EventSink(MakeTextEvent(ELocalLLMEventType::Error, RequestId,
            TEXT("llama.cpp artifacts were detected, but the native backend adapter has not been implemented yet.")));
#else
        EventSink(MakeTextEvent(ELocalLLMEventType::Error, RequestId,
            TEXT("llama.cpp support is not compiled in. Add pinned LlamaCpp artifacts before selecting this backend.")));
#endif
    }
    FLocalLLMEventSink EventSink;
};
}

TUniquePtr<ILocalMultimodalBackend> CreateLocalLLMBackend(
    const ELocalLLMBackend Backend,
    FLocalLLMEventSink EventSink,
    FLocalLLMCancelCheck CancelCheck)
{
    switch (Backend)
    {
    case ELocalLLMBackend::Mock:
        return MakeUnique<FMockLocalLLMBackend>(MoveTemp(EventSink));
    case ELocalLLMBackend::LlamaCpp:
#if LOCAL_MULTIMODAL_LLM_WITH_LLAMA
        return CreateLlamaCppBackend(MoveTemp(EventSink), MoveTemp(CancelCheck));
#else
        return MakeUnique<FUnavailableLlamaCppBackend>(MoveTemp(EventSink));
#endif
    default:
        checkNoEntry();
        return MakeUnique<FMockLocalLLMBackend>(MoveTemp(EventSink));
    }
}
