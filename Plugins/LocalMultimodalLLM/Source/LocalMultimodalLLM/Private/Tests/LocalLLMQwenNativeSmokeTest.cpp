#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && LOCAL_MULTIMODAL_LLM_WITH_LLAMA

#include "HAL/PlatformProcess.h"
#include "Inference/InferenceWorker.h"
#include "Models/LocalLLMModelRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMQwenNativeSmokeTest,
    "LocalMultimodalLLM.Native.Qwen35TextVisionSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMQwenNativeSmokeTest::RunTest(const FString&)
{
    FLocalLLMModelInfo ModelInfo;
    if (!FLocalLLMModelRegistry::FindById(TEXT("qwen-3.5-4b-iq3"), ModelInfo) || !ModelInfo.bCompatible)
    {
        AddError(FString::Printf(TEXT("Qwen 3.5 model is unavailable: %s"), *ModelInfo.Status));
        return false;
    }
    TestTrue(TEXT("Qwen text capability"), ModelInfo.Config.Capabilities.bText);
    TestTrue(TEXT("Qwen vision capability"), ModelInfo.Config.Capabilities.bVision);
    TestFalse(TEXT("Qwen native audio capability"), ModelInfo.Config.Capabilities.bAudioInput);
    TestTrue(TEXT("Qwen declares reasoning capability"), ModelInfo.Config.Capabilities.bReasoning);
    TestEqual(TEXT("Qwen defaults to direct-response mode"), ModelInfo.Config.Generation.ReasoningMode, ELocalLLMReasoningMode::Disabled);
    TestFalse(TEXT("Qwen provides a model-specific No-Think prefill"), ModelInfo.Config.NoThinkAssistantPrefill.IsEmpty());
    TestEqual(TEXT("Qwen projector is lazy by default"), ModelInfo.Config.Load.ProjectorLoadPolicy, ELocalLLMProjectorLoadPolicy::Lazy);
    ModelInfo.Config.Load.ContextSize = 4096;
    ModelInfo.Config.Generation.MaxTokens = 32;
    ModelInfo.Config.Generation.Temperature = 0.2f;

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
            if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
            if (Event.Type == ELocalLLMEventType::ModelLoaded) { bLoaded = true; AddInfo(Event.Text); }
        }
        if (!bLoaded) FPlatformProcess::Sleep(0.01f);
    }
    if (!TestTrue(TEXT("Qwen text model loaded; projector remains deferred until image submission"), bLoaded)) return false;

    const FGuid SessionId = FGuid::NewGuid();
    FLocalLLMCommand Create;
    Create.Type = ELocalLLMCommandType::CreateSession;
    Create.RequestId = FGuid::NewGuid();
    Create.SessionId = SessionId;
    Create.Character.CharacterId = TEXT("qwen_smoke");
    Create.Character.DisplayName = TEXT("Qwen Smoke Character");
    Create.Character.bUseGeneratedContext = false;
    Create.Character.CustomSystemPrompt = TEXT("Reply concisely and follow the requested output format.");
    Create.Character.JailbreakGuard.Mode = ELocalLLMJailbreakGuardMode::Off;
    Create.Character.ImmersionGuard.Mode = ELocalLLMImmersionGuardMode::Off;
    Worker.Enqueue(MoveTemp(Create));

    auto WaitForTurn = [this, &Worker](const FGuid& RequestId, FString& OutText, const double Timeout)
    {
        const double Deadline = FPlatformTime::Seconds() + Timeout;
        while (FPlatformTime::Seconds() < Deadline)
        {
            FLocalLLMEvent Event;
            while (Worker.DequeueEvent(Event))
            {
                if (Event.RequestId != RequestId) continue;
                if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
                if (Event.Type == ELocalLLMEventType::TextDelta) OutText += Event.Text;
                if (Event.Type == ELocalLLMEventType::TurnCompleted) return true;
            }
            FPlatformProcess::Sleep(0.01f);
        }
        AddError(TEXT("Qwen smoke turn timed out"));
        return false;
    };

    FLocalLLMCommand Text;
    Text.Type = ELocalLLMCommandType::SubmitText;
    Text.RequestId = FGuid::NewGuid();
    Text.SessionId = SessionId;
    Text.Text = TEXT("Reply with exactly one short greeting.");
    const FGuid TextRequestId = Text.RequestId;
    Worker.Enqueue(MoveTemp(Text));
    FString TextResponse;
    if (!WaitForTurn(TextRequestId, TextResponse, 120.0)) return false;
    TestFalse(TEXT("Qwen text generation produced output"), TextResponse.TrimStartAndEnd().IsEmpty());
    TestFalse(TEXT("Qwen direct-response mode does not expose an opening reasoning tag"), TextResponse.Contains(TEXT("<think>")));
    TestFalse(TEXT("Qwen direct-response mode does not expose a closing reasoning tag"), TextResponse.Contains(TEXT("</think>")));

    FLocalLLMCommand Reset;
    Reset.Type = ELocalLLMCommandType::ResetConversation;
    Reset.RequestId = FGuid::NewGuid();
    Reset.SessionId = SessionId;
    const FGuid ResetRequestId = Reset.RequestId;
    Worker.Enqueue(MoveTemp(Reset));
    FString Ignored;
    if (!WaitForTurn(ResetRequestId, Ignored, 10.0)) return false;

    FLocalLLMCommand Image;
    Image.Type = ELocalLLMCommandType::SubmitImage;
    Image.RequestId = FGuid::NewGuid();
    Image.SessionId = SessionId;
    Image.Text = TEXT("What is the dominant color? Answer with one color word.");
    Image.Image.Width = 224;
    Image.Image.Height = 224;
    Image.Image.RgbPixels.SetNumUninitialized(224 * 224 * 3);
    for (int32 Pixel = 0; Pixel < 224 * 224; ++Pixel)
    {
        Image.Image.RgbPixels[Pixel * 3] = 255;
        Image.Image.RgbPixels[Pixel * 3 + 1] = 0;
        Image.Image.RgbPixels[Pixel * 3 + 2] = 0;
    }
    const FGuid ImageRequestId = Image.RequestId;
    Worker.Enqueue(MoveTemp(Image));
    FString ImageResponse;
    if (!WaitForTurn(ImageRequestId, ImageResponse, 120.0)) return false;
    if (ImageResponse.TrimStartAndEnd().IsEmpty()) AddWarning(TEXT("Qwen vision returned only whitespace"));
    else AddInfo(FString::Printf(TEXT("Qwen vision response: %s"), *ImageResponse));
    return !HasAnyErrors();
}

#endif
