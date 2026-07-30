#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LocalLLMComponent.h"
#include "LocalLLMMicrophoneComponent.h"
#include "LocalLLMSubsystem.h"
#include "LocalLLMToolExecutorComponent.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMBlueprintEventSurfaceTest,
    "LocalMultimodalLLM.API.BlueprintEventSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
int32 DelegateParameterCount(const UClass* OwnerClass, const FName PropertyName)
{
    const FMulticastDelegateProperty* Property =
        FindFProperty<FMulticastDelegateProperty>(OwnerClass, PropertyName);
    return Property && Property->SignatureFunction ? Property->SignatureFunction->NumParms : INDEX_NONE;
}
}

bool FLocalLLMBlueprintEventSurfaceTest::RunTest(const FString&)
{
    TestEqual(TEXT("Component OnTextDelta has four pins"),
        DelegateParameterCount(ULocalLLMComponent::StaticClass(), TEXT("OnTextDelta")), 4);
    TestEqual(TEXT("Component OnToolCall has six pins"),
        DelegateParameterCount(ULocalLLMComponent::StaticClass(), TEXT("OnToolCall")), 6);
    TestEqual(TEXT("Component OnSubsystemStateChanged has four pins"),
        DelegateParameterCount(ULocalLLMComponent::StaticClass(), TEXT("OnSubsystemStateChanged")), 4);
    TestEqual(TEXT("Component OnStatusChanged has five pins"),
        DelegateParameterCount(ULocalLLMComponent::StaticClass(), TEXT("OnStatusChanged")), 5);
    TestEqual(TEXT("Microphone OnUserSpeechCaptured has four pins"),
        DelegateParameterCount(ULocalLLMMicrophoneComponent::StaticClass(), TEXT("OnUserSpeechCaptured")), 4);
    TestEqual(TEXT("Tool executor OnToolCall has six pins"),
        DelegateParameterCount(ULocalLLMToolExecutorComponent::StaticClass(), TEXT("OnToolCall")), 6);
    TestNull(TEXT("Universal component event is not Blueprint-reflected"),
        FindFProperty<FMulticastDelegateProperty>(ULocalLLMComponent::StaticClass(), TEXT("OnInternalEvent")));
    TestNull(TEXT("Universal subsystem event is not Blueprint-reflected"),
        FindFProperty<FMulticastDelegateProperty>(ULocalLLMSubsystem::StaticClass(), TEXT("OnInternalEvent")));
    return !HasAnyErrors();
}

#endif
