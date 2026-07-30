#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && LOCAL_MULTIMODAL_LLM_WITH_LLAMA

#include "HAL/PlatformProcess.h"
#include "Inference/InferenceWorker.h"
#include "Models/LocalLLMModelRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMConversationCompactionTest,
    "LocalMultimodalLLM.Native.ConversationCompaction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMConversationCompactionTest::RunTest(const FString&)
{
    FLocalLLMModelInfo ModelInfo;
    if (!FLocalLLMModelRegistry::FindById(TEXT("gemma-4-e2b-it-qat"), ModelInfo) || !ModelInfo.bCompatible)
    {
        AddError(FString::Printf(TEXT("Gemma compaction test model is unavailable: %s"), *ModelInfo.Status));
        return false;
    }
    ModelInfo.Config.Load.ContextSize = 4096;
    ModelInfo.Config.Generation.MaxTokens = 24;
    ModelInfo.Config.Generation.Temperature = 0.1f;

    FLocalLLMInferenceWorker Worker;
    FLocalLLMCommand Load;
    Load.Type = ELocalLLMCommandType::LoadModel;
    Load.Backend = ELocalLLMBackend::LlamaCpp;
    Load.RequestId = FGuid::NewGuid();
    Load.ModelConfig = ModelInfo.Config;
    Worker.Enqueue(MoveTemp(Load));

    bool bLoaded = false;
    const double LoadDeadline = FPlatformTime::Seconds() + 180.0;
    while (!bLoaded && FPlatformTime::Seconds() < LoadDeadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.Type == ELocalLLMEventType::Error) AddError(Event.Text);
            bLoaded |= Event.Type == ELocalLLMEventType::ModelLoaded;
        }
        if (!bLoaded) FPlatformProcess::Sleep(0.01f);
    }
    if (!TestTrue(TEXT("Compaction test model loaded"), bLoaded)) return false;

    auto WaitForRequest = [&](const FGuid& RequestId, FString& OutText, bool& bOutCompacted, bool& bOutRejected)
    {
        const double Deadline = FPlatformTime::Seconds() + 120.0;
        bool bCompleted = false;
        while (!bCompleted && FPlatformTime::Seconds() < Deadline)
        {
            FLocalLLMEvent Event;
            while (Worker.DequeueEvent(Event))
            {
                if (Event.RequestId != RequestId) continue;
                if (Event.Type == ELocalLLMEventType::Error) AddError(Event.Text);
                if (Event.Type == ELocalLLMEventType::TextDelta) OutText += Event.Text;
                bOutCompacted |= Event.Type == ELocalLLMEventType::ConversationCompacted;
                bOutRejected |= Event.Type == ELocalLLMEventType::InputRejected;
                bCompleted |= Event.Type == ELocalLLMEventType::TurnCompleted;
            }
            if (!bCompleted) FPlatformProcess::Sleep(0.005f);
        }
        return TestTrue(TEXT("Request completed"), bCompleted);
    };

    FLocalLLMCharacterProfile Character;
    Character.CharacterId = TEXT("compaction_tester");
    Character.DisplayName = TEXT("Memory Tester");
    Character.bUseGeneratedContext = false;
    Character.CustomSystemPrompt = TEXT("You are a concise memory-test assistant. Follow the user's requested response format and preserve explicitly identified personal facts.");
    Character.JailbreakGuard.Mode = ELocalLLMJailbreakGuardMode::Off;
    Character.JailbreakGuard.bTreatPlayerTextAsUntrustedDialogue = false;
    Character.ImmersionGuard.Mode = ELocalLLMImmersionGuardMode::Off;
    Character.ConversationMemory.CompactAfterTurns = 10;
    Character.ConversationMemory.RecentTurnsToKeep = 5;

    const FGuid SessionId = FGuid::NewGuid();
    FLocalLLMCommand Create;
    Create.Type = ELocalLLMCommandType::CreateSession;
    Create.RequestId = FGuid::NewGuid();
    Create.SessionId = SessionId;
    Create.Character = Character;
    const FGuid CreateRequestId = Create.RequestId;
    Worker.Enqueue(MoveTemp(Create));

    bool bCreated = false;
    FString CreatedDetail;
    const double CreateDeadline = FPlatformTime::Seconds() + 10.0;
    while (!bCreated && FPlatformTime::Seconds() < CreateDeadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.RequestId == CreateRequestId && Event.Type == ELocalLLMEventType::SessionCreated)
            {
                bCreated = true;
                CreatedDetail = Event.Text;
            }
        }
        if (!bCreated) FPlatformProcess::Sleep(0.005f);
    }
    TestTrue(TEXT("Compaction session created"), bCreated);
    TestTrue(TEXT("4K context halves generated budget"), CreatedDetail.Contains(TEXT("generated=1280")));
    TestTrue(TEXT("4K context halves compacted-memory budget"), CreatedDetail.Contains(TEXT("memory=512")));
    TestTrue(TEXT("4K context halves recent-dialogue budget"), CreatedDetail.Contains(TEXT("recent=1280")));
    TestTrue(TEXT("4K context halves player-input budget"), CreatedDetail.Contains(TEXT("input=384")));

    for (int32 Turn = 1; Turn <= 10; ++Turn)
    {
        FLocalLLMCommand Submit;
        Submit.Type = ELocalLLMCommandType::SubmitText;
        Submit.RequestId = FGuid::NewGuid();
        Submit.SessionId = SessionId;
        Submit.Text = Turn == 1
            ? TEXT("Remember this durable personal fact: my signal word is THISTLE-947. Reply only ACK.")
            : FString::Printf(TEXT("This is filler turn %d. Reply only ACK."), Turn);
        const FGuid RequestId = Submit.RequestId;
        Worker.Enqueue(MoveTemp(Submit));
        FString Text;
        bool bCompacted = false;
        bool bRejected = false;
        if (!WaitForRequest(RequestId, Text, bCompacted, bRejected)) return false;
    }

    FLocalLLMCommand Recall;
    Recall.Type = ELocalLLMCommandType::SubmitText;
    Recall.RequestId = FGuid::NewGuid();
    Recall.SessionId = SessionId;
    Recall.Text = TEXT("What is my signal word? Reply only with the signal word.");
    const FGuid RecallRequestId = Recall.RequestId;
    Worker.Enqueue(MoveTemp(Recall));
    FString RecallText;
    bool bCompacted = false;
    bool bRejected = false;
    if (!WaitForRequest(RecallRequestId, RecallText, bCompacted, bRejected)) return false;
    TestTrue(TEXT("Automatic compaction event emitted"), bCompacted);
    TestTrue(TEXT("Compacted memory preserved early personal fact"), RecallText.Contains(TEXT("THISTLE-947"), ESearchCase::IgnoreCase));

    FLocalLLMCharacterProfile Limited = Character;
    Limited.CharacterId = TEXT("input_limit_tester");
    Limited.ConversationMemory.MaxPlayerInputTokens = 32;
    Limited.ConversationMemory.OverlongInputResponse = TEXT("Please shorten that speech.");
    const FGuid LimitedSessionId = FGuid::NewGuid();
    FLocalLLMCommand CreateLimited;
    CreateLimited.Type = ELocalLLMCommandType::CreateSession;
    CreateLimited.RequestId = FGuid::NewGuid();
    CreateLimited.SessionId = LimitedSessionId;
    CreateLimited.Character = Limited;
    Worker.Enqueue(MoveTemp(CreateLimited));

    FLocalLLMCommand Oversized;
    Oversized.Type = ELocalLLMCommandType::SubmitText;
    Oversized.RequestId = FGuid::NewGuid();
    Oversized.SessionId = LimitedSessionId;
    Oversized.Text = FString::ChrN(1200, TEXT('x'));
    const FGuid OversizedRequestId = Oversized.RequestId;
    Worker.Enqueue(MoveTemp(Oversized));
    FString RejectionText;
    bool bUnexpectedCompaction = false;
    bool bInputRejected = false;
    if (!WaitForRequest(OversizedRequestId, RejectionText, bUnexpectedCompaction, bInputRejected)) return false;
    TestTrue(TEXT("Oversized input rejected without inference"), bInputRejected);
    TestEqual(TEXT("Configured in-character input response"), RejectionText, FString(TEXT("Please shorten that speech.")));
    return true;
}

#endif
