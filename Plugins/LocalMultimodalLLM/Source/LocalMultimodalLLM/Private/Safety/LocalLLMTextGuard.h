#pragma once

#include "CoreMinimal.h"
#include "LocalLLMTypes.h"

struct FLocalLLMTextGuardResult
{
    FString Text;
    FString RuleId;
    FString MatchedPattern;
    bool bViolation = false;
    bool bSanitized = false;
};

namespace LocalLLMTextGuard
{
FLocalLLMTextGuardResult InspectPlayerText(const FString& Text, const FLocalLLMJailbreakGuardSettings& Settings);
FLocalLLMTextGuardResult InspectResponse(const FString& Text, const FLocalLLMImmersionGuardSettings& Settings,
    bool bToolsAvailable);
/** Returns the exclusive end of the next speakable sentence, or INDEX_NONE while the sentence is incomplete. */
int32 FindCompleteSentenceEnd(const FString& Text, int32 StartIndex = 0);
/** Returns the first leaked chat-role/control boundary that must never be presented as character dialogue. */
int32 FindResponseBoundary(const FString& Text, int32 StartIndex = 0);
}
