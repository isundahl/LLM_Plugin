#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"
#include "LocalLLMDelegates.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLocalLLMEventDelegate, const FLocalLLMEvent&, Event);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FLocalLLMTextDeltaDelegate,
    FGuid, RequestId,
    FGuid, SessionId,
    FName, CharacterId,
    const FString&, Text);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FLocalLLMUserSpeechCapturedDelegate,
    const FString&, RawTranscript,
    float, MeasuredNoiseFloorDb,
    float, VoiceThresholdDb,
    float, SpeakerSimilarity);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(
    FLocalLLMToolCallDelegate,
    FGuid, SessionId,
    FName, CharacterId,
    FGuid, ToolCallId,
    const FString&, ToolName,
    const FString&, ArgumentsJson,
    bool, bRequiresPlayerConfirmation);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FLocalLLMSubsystemStateChangedDelegate,
    FGuid, SessionId,
    bool, bCompactionExecuted,
    bool, bRelationshipUpdated,
    bool, bRollbackTriggered);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
    FLocalLLMStatusChangedDelegate,
    ELocalLLMEventType, EventType,
    FGuid, RequestId,
    FGuid, SessionId,
    FName, CharacterId,
    const FString&, Message);

USTRUCT()
struct FLocalLLMDelegatesReflectionAnchor
{
    GENERATED_BODY()
};
