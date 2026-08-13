#include "LocalLLMToolExecutorComponent.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "LocalLLMCharacterSheet.h"
#include "LocalLLMSubsystem.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

ULocalLLMToolExecutorComponent::ULocalLLMToolExecutorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void ULocalLLMToolExecutorComponent::BeginPlay()
{
    Super::BeginPlay();
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem())
    {
        Subsystem->OnToolCall.AddDynamic(this, &ULocalLLMToolExecutorComponent::HandleToolCall);
        if (bRegisterToolSetOnBeginPlay) RegisterConfiguredToolSet();
    }
}

void ULocalLLMToolExecutorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem())
        Subsystem->OnToolCall.RemoveDynamic(this, &ULocalLLMToolExecutorComponent::HandleToolCall);
    PendingSessions.Reset();
    Super::EndPlay(EndPlayReason);
}

int32 ULocalLLMToolExecutorComponent::RegisterConfiguredToolSet()
{
    if (ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem()) return Subsystem->RegisterToolSet(ToolSet);
    return 0;
}

FGuid ULocalLLMToolExecutorComponent::CompleteToolCall(const FGuid& ToolCallId, const FString& ResultJson, const bool bSuccess)
{
    const FGuid* SessionId = PendingSessions.Find(ToolCallId);
    ULocalLLMSubsystem* Subsystem = GetLocalLLMSubsystem();
    if (!SessionId || !Subsystem) return {};
    const FGuid RequestId = Subsystem->SubmitToolResult(*SessionId, ToolCallId, ResultJson, bSuccess);
    PendingSessions.Remove(ToolCallId);
    return RequestId;
}

FGuid ULocalLLMToolExecutorComponent::RejectToolCall(const FGuid& ToolCallId, const FString& Reason)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("error"), Reason);
    FString Json;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
    FJsonSerializer::Serialize(Result, Writer);
    return CompleteToolCall(ToolCallId, Json, false);
}

void ULocalLLMToolExecutorComponent::HandleToolCall(
    const FGuid SessionId,
    const FName CharacterId,
    const FGuid ToolCallId,
    const FString& ToolName,
    const FString& ArgumentsJson,
    const bool bRequiresPlayerConfirmation)
{
    PendingSessions.Add(ToolCallId, SessionId);
    OnToolCall.Broadcast(SessionId, CharacterId, ToolCallId, ToolName,
        ArgumentsJson, bRequiresPlayerConfirmation);
}

ULocalLLMSubsystem* ULocalLLMToolExecutorComponent::GetLocalLLMSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<ULocalLLMSubsystem>() : nullptr;
}
