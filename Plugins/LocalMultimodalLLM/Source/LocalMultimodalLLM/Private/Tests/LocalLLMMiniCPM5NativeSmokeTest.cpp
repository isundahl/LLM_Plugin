#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && LOCAL_MULTIMODAL_LLM_WITH_LLAMA

#include "HAL/PlatformProcess.h"
#include "Inference/InferenceWorker.h"
#include "Models/LocalLLMModelRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMMiniCPM5NativeSmokeTest,
    "LocalMultimodalLLM.Native.MiniCPM5TextSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMMiniCPM5NativeSmokeTest::RunTest(const FString&)
{
    FLocalLLMModelInfo ModelInfo;
    if (!FLocalLLMModelRegistry::FindById(TEXT("minicpm5-1b-q4-k-m"), ModelInfo) || !ModelInfo.bCompatible)
    {
        AddError(FString::Printf(TEXT("MiniCPM5 model is unavailable: %s"), *ModelInfo.Status));
        return false;
    }

    TestTrue(TEXT("MiniCPM5 supports text"), ModelInfo.Config.Capabilities.bText);
    TestFalse(TEXT("MiniCPM5 XML tool calls are not advertised through the plugin's JSON tool protocol"),
        ModelInfo.Config.Capabilities.bToolCalling);
    TestFalse(TEXT("MiniCPM5 does not advertise vision without a projector"), ModelInfo.Config.Capabilities.bVision);
    TestFalse(TEXT("MiniCPM5 does not advertise native audio"), ModelInfo.Config.Capabilities.bAudioInput);
    TestEqual(TEXT("MiniCPM5 disables projector loading"), ModelInfo.Config.Load.ProjectorLoadPolicy,
        ELocalLLMProjectorLoadPolicy::Disabled);

    ModelInfo.Config.Load.ContextSize = 4096;
    ModelInfo.Config.Generation.MaxTokens = 128;
    ModelInfo.Config.Generation.Temperature = 0.2f;

    FLocalLLMInferenceWorker Worker;
    FLocalLLMCommand Load;
    Load.Type = ELocalLLMCommandType::LoadModel;
    Load.Backend = ELocalLLMBackend::LlamaCpp;
    Load.RequestId = FGuid::NewGuid();
    Load.ModelConfig = ModelInfo.Config;
    Worker.Enqueue(MoveTemp(Load));

    bool bLoaded = false;
    const double LoadDeadline = FPlatformTime::Seconds() + 120.0;
    while (!bLoaded && FPlatformTime::Seconds() < LoadDeadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.Type == ELocalLLMEventType::Error)
            {
                AddError(Event.Text);
                return false;
            }
            if (Event.Type == ELocalLLMEventType::ModelLoaded)
            {
                bLoaded = true;
                AddInfo(Event.Text);
            }
        }
        if (!bLoaded) FPlatformProcess::Sleep(0.01f);
    }
    if (!TestTrue(TEXT("MiniCPM5 loaded before timeout"), bLoaded)) return false;

    const FGuid SessionId = FGuid::NewGuid();
    FLocalLLMCommand Create;
    Create.Type = ELocalLLMCommandType::CreateSession;
    Create.RequestId = FGuid::NewGuid();
    Create.SessionId = SessionId;
    Create.Character.CharacterId = TEXT("minicpm5_smoke");
    Create.Character.DisplayName = TEXT("MiniCPM5 Smoke Character");
    Create.Character.bUseGeneratedContext = false;
    Create.Character.CustomSystemPrompt = TEXT("Reply concisely and remain in character.");
    Create.Character.JailbreakGuard.Mode = ELocalLLMJailbreakGuardMode::Off;
    Create.Character.ImmersionGuard.Mode = ELocalLLMImmersionGuardMode::Off;
    Worker.Enqueue(MoveTemp(Create));

    FLocalLLMCommand Text;
    Text.Type = ELocalLLMCommandType::SubmitText;
    Text.RequestId = FGuid::NewGuid();
    Text.SessionId = SessionId;
    Text.Text = TEXT("Reply with one short greeting.");
    const FGuid TextRequestId = Text.RequestId;
    Worker.Enqueue(MoveTemp(Text));

    FString Response;
    bool bCompleted = false;
    const double GenerationDeadline = FPlatformTime::Seconds() + 120.0;
    while (!bCompleted && FPlatformTime::Seconds() < GenerationDeadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.RequestId != TextRequestId) continue;
            if (Event.Type == ELocalLLMEventType::Error)
            {
                AddError(Event.Text);
                return false;
            }
            if (Event.Type == ELocalLLMEventType::TextDelta) Response += Event.Text;
            if (Event.Type == ELocalLLMEventType::TurnCompleted) bCompleted = true;
        }
        if (!bCompleted) FPlatformProcess::Sleep(0.01f);
    }

    TestTrue(TEXT("MiniCPM5 generation completed"), bCompleted);
    TestFalse(TEXT("MiniCPM5 produced non-whitespace output"), Response.TrimStartAndEnd().IsEmpty());
    FString DistinctResponse = Response;
    DistinctResponse.ReplaceInline(TEXT("*"), TEXT(""));
    TestFalse(TEXT("MiniCPM5 output is not a degenerate asterisk run"), DistinctResponse.TrimStartAndEnd().IsEmpty());
    TestFalse(TEXT("MiniCPM5 No-Think mode does not expose reasoning tags"), Response.Contains(TEXT("<think>")));
    if (!Response.IsEmpty()) AddInfo(FString::Printf(TEXT("MiniCPM5 response: %s"), *Response));
    return !HasAnyErrors();
}

#endif
