#include "LocalLLMComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "LocalLLMSubsystem.h"

ULocalLLMComponent::ULocalLLMComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void ULocalLLMComponent::BeginPlay()
{
    Super::BeginPlay();
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem())
    {
        Subsystem->OnInternalEvent.AddDynamic(this, &ULocalLLMComponent::HandleSubsystemEvent);
        if (bLoadDefaultModelOnBeginPlay) Subsystem->LoadDefaultModel();
    }
}

void ULocalLLMComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem())
    {
        Subsystem->OnInternalEvent.RemoveDynamic(this, &ULocalLLMComponent::HandleSubsystemEvent);
    }
    Super::EndPlay(EndPlayReason);
}

FGuid ULocalLLMComponent::LoadModel(const FString& ModelPath)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->LoadModel(ModelPath);
    return {};
}

FGuid ULocalLLMComponent::LoadModelById(const FString& ModelId)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->LoadModelById(ModelId);
    return {};
}

TArray<FLocalLLMModelInfo> ULocalLLMComponent::GetAvailableModels() const
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->GetAvailableModels();
    return {};
}

FGuid ULocalLLMComponent::SubmitText(const FString& Prompt)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SubmitText(Prompt);
    return {};
}

FGuid ULocalLLMComponent::CreateCharacterSession(ULocalLLMCharacterSheet* CharacterSheet)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->CreateCharacterSession(CharacterSheet);
    return {};
}

FGuid ULocalLLMComponent::CreateCharacterSessionFromProfile(const FLocalLLMCharacterProfile& Character)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->CreateCharacterSessionFromProfile(Character);
    return {};
}

FGuid ULocalLLMComponent::SubmitTextForSession(const FGuid& SessionId, const FString& Prompt)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SubmitTextForSession(SessionId, Prompt);
    return {};
}

FGuid ULocalLLMComponent::SubmitImageForSession(const FGuid& SessionId, const FLocalLLMImageInput& Image, const FString& Prompt)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SubmitImageForSession(SessionId, Image, Prompt);
    return {};
}

FGuid ULocalLLMComponent::SubmitAudioForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio, const FString& Prompt)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SubmitAudioForSession(SessionId, Audio, Prompt);
    return {};
}

FGuid ULocalLLMComponent::PreloadSpeechToText()
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->PreloadSpeechToText();
    return {};
}

bool ULocalLLMComponent::DestroyCharacterSession(const FGuid& SessionId)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->DestroyCharacterSession(SessionId);
    return false;
}

FGuid ULocalLLMComponent::SetSharedWorldContext(const FLocalLLMWorldContext& World)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SetSharedWorldContext(World);
    return {};
}

bool ULocalLLMComponent::UpsertDynamicLoreFact(FLocalLLMDynamicLoreFact Fact)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->UpsertDynamicLoreFact(MoveTemp(Fact));
    return false;
}

bool ULocalLLMComponent::RemoveDynamicLoreFact(const FGuid& FactId)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->RemoveDynamicLoreFact(FactId);
    return false;
}

void ULocalLLMComponent::ClearDynamicLoreFacts()
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) Subsystem->ClearDynamicLoreFacts();
}

TArray<FLocalLLMDynamicLoreFact> ULocalLLMComponent::GetDynamicLoreFacts() const
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->GetDynamicLoreFacts();
    return {};
}

TArray<FLocalLLMDynamicLoreFact> ULocalLLMComponent::GetVisibleDynamicLoreFactsForSession(
    const FGuid& SessionId) const
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem())
        return Subsystem->GetVisibleDynamicLoreFactsForSession(SessionId);
    return {};
}

bool ULocalLLMComponent::CommitCharacterDevelopedFact(
    const FGuid& SessionId, const ELocalLLMDynamicLoreCategory Category,
    const FName Key, const FString& Value)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem())
        return Subsystem->CommitCharacterDevelopedFact(SessionId, Category, Key, Value);
    return false;
}

bool ULocalLLMComponent::SetSessionKnowledgeAreas(const FGuid& SessionId, const TArray<FName>& AreaIds)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem())
        return Subsystem->SetSessionKnowledgeAreas(SessionId, AreaIds);
    return false;
}

bool ULocalLLMComponent::RegisterTool(const FLocalLLMToolDefinition& Tool)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->RegisterTool(Tool);
    return false;
}

FGuid ULocalLLMComponent::SubmitToolResult(const FGuid& SessionId, const FGuid& ToolCallId, const FString& ResultJson, const bool bSuccess)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SubmitToolResult(SessionId, ToolCallId, ResultJson, bSuccess);
    return {};
}

FGuid ULocalLLMComponent::EvaluateRelationshipForSession(const FGuid& SessionId, const bool bApplyChanges)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->EvaluateRelationshipForSession(SessionId, bApplyChanges);
    return {};
}

bool ULocalLLMComponent::SetRelationshipRating(const FGuid& SessionId, const FName CriterionName, const int32 Rating)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SetRelationshipRating(SessionId, CriterionName, Rating);
    return false;
}

bool ULocalLLMComponent::GetRelationshipState(const FGuid& SessionId, FLocalLLMRelationshipEvaluationSettings& OutState) const
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->GetRelationshipState(SessionId, OutState);
    return false;
}

FGuid ULocalLLMComponent::CompactConversationForSession(const FGuid& SessionId)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->CompactConversationForSession(SessionId);
    return {};
}

FGuid ULocalLLMComponent::UndoLastConversationTurnForSession(const FGuid& SessionId)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->UndoLastConversationTurnForSession(SessionId);
    return {};
}

FGuid ULocalLLMComponent::SubmitImage(const FLocalLLMImageInput& Image, const FString& Prompt)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SubmitImage(Image, Prompt);
    return {};
}

FGuid ULocalLLMComponent::SubmitAudio(const FLocalLLMAudioInput& Audio, const FString& Prompt)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->SubmitAudio(Audio, Prompt);
    return {};
}

void ULocalLLMComponent::Cancel(const FGuid& RequestId)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) Subsystem->Cancel(RequestId);
}

void ULocalLLMComponent::HandleSubsystemEvent(const FLocalLLMEvent& Event)
{
    OnInternalEvent.Broadcast(Event);
    switch (Event.Type)
    {
    case ELocalLLMEventType::TextDelta:
        OnTextDelta.Broadcast(Event.RequestId, Event.SessionId, Event.CharacterId, Event.Text);
        break;
    case ELocalLLMEventType::ToolCallCompleted:
        OnToolCall.Broadcast(Event.SessionId, Event.CharacterId, Event.ToolCallId,
            Event.ToolName, Event.Text, Event.bToolRequiresPlayerConfirmation);
        break;
    case ELocalLLMEventType::ToolCallStarted:
    case ELocalLLMEventType::ToolCallArgumentsDelta:
        break;
    case ELocalLLMEventType::ConversationCompacted:
        OnSubsystemStateChanged.Broadcast(Event.SessionId, true, false, false);
        break;
    case ELocalLLMEventType::RelationshipEvaluated:
        OnSubsystemStateChanged.Broadcast(Event.SessionId, false, true, false);
        break;
    case ELocalLLMEventType::ConversationTurnUndone:
        OnSubsystemStateChanged.Broadcast(Event.SessionId, false, false, true);
        break;
    default:
        OnStatusChanged.Broadcast(Event.Type, Event.RequestId, Event.SessionId,
            Event.CharacterId, Event.Text);
        break;
    }
}

ULocalLLMSubsystem* ULocalLLMComponent::GetLocalLLMSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<ULocalLLMSubsystem>() : nullptr;
}
