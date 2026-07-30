#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformProcess.h"
#include "Inference/InferenceWorker.h"
#include "Speech/LocalVoiceActivityDetector.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMMicrophoneVadTest,
    "LocalMultimodalLLM.Speech.MicrophoneVadSegmentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMMicrophoneVadTest::RunTest(const FString&)
{
    FLocalLLMMicrophoneConfig DefaultConfig;
    TestEqual(TEXT("Push-to-talk is the default segmentation mode"), DefaultConfig.SegmentationMode,
        ELocalLLMMicrophoneSegmentationMode::ManualButton);
    TestTrue(TEXT("Silent manual recordings are rejected by default"),
        DefaultConfig.bRejectSilentManualRecordings);

    TArray<float> CalibrationLevels = { -61.0f, -60.0f, -59.0f, -60.0f, -61.0f, -58.0f, -60.0f, -20.0f, -18.0f, -16.0f };
    float NoiseFloorDb = 0.0f;
    float CalibratedThresholdDb = 0.0f;
    TestTrue(TEXT("Calibration estimate succeeds"), FLocalLLMVoiceActivityDetector::EstimateCalibratedThreshold(
        CalibrationLevels, 12.0f, -55.0f, -25.0f, NoiseFloorDb, CalibratedThresholdDb));
    TestTrue(TEXT("Loud calibration outliers are ignored"), NoiseFloorDb <= -58.0f);
    TestTrue(TEXT("Calibrated threshold applies margin"), CalibratedThresholdDb >= -49.0f && CalibratedThresholdDb <= -46.0f);

    FLocalLLMMicrophoneConfig Config;
    Config.SegmentationMode = ELocalLLMMicrophoneSegmentationMode::VoiceActivityDetection;
    Config.VoiceThresholdDb = -35.0f;
    Config.SpeechStartMilliseconds = 50;
    Config.SpeechEndSilenceMilliseconds = 100;
    Config.PreRollMilliseconds = 50;
    Config.MinimumUtteranceMilliseconds = 100;
    FLocalLLMVoiceActivityDetector Vad(Config);

    constexpr int32 Rate = 16000;
    TArray<float> Silence;
    Silence.Init(0.0f, 800);
    TArray<float> Speech;
    Speech.Init(0.1f, 800);

    TestTrue(TEXT("Manual activity gate distinguishes silence"),
        FLocalLLMVoiceActivityDetector::CalculateRmsDb(Silence.GetData(), Silence.Num()) <
            DefaultConfig.ManualActivityThresholdDb);
    TestTrue(TEXT("Manual activity gate accepts clear speech energy"),
        FLocalLLMVoiceActivityDetector::CalculateRmsDb(Speech.GetData(), Speech.Num()) >=
            DefaultConfig.ManualActivityThresholdDb);

    TestFalse(TEXT("Initial silence does not start speech"), Vad.Process(Silence.GetData(), Silence.Num(), Rate, 1).bSpeechStarted);
    TestTrue(TEXT("Sustained energy starts speech"), Vad.Process(Speech.GetData(), Speech.Num(), Rate, 1).bSpeechStarted);
    Vad.Process(Speech.GetData(), Speech.Num(), Rate, 1);
    FLocalLLMVadUpdate End = Vad.Process(Silence.GetData(), Silence.Num(), Rate, 1);
    if (!End.bSpeechEnded) End = Vad.Process(Silence.GetData(), Silence.Num(), Rate, 1);
    TestTrue(TEXT("Configured trailing silence ends speech"), End.bSpeechEnded);
    TestTrue(TEXT("Completed utterance contains PCM"), End.CompletedUtterance.IsValid());
    TestTrue(TEXT("Pre-roll is preserved"), End.CompletedUtterance.Samples.Num() >= 2400);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMPartialTranscriptionTest,
    "LocalMultimodalLLM.Speech.PartialDoesNotStartCharacterTurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMPartialTranscriptionTest::RunTest(const FString&)
{
    FLocalLLMInferenceWorker Worker;
    const FGuid SessionId = FGuid::NewGuid();
    FLocalLLMCommand Create;
    Create.Type = ELocalLLMCommandType::CreateSession;
    Create.RequestId = FGuid::NewGuid();
    Create.SessionId = SessionId;
    Create.Character.DisplayName = TEXT("Partial Test");
    Worker.Enqueue(MoveTemp(Create));

    FLocalLLMCommand Partial;
    Partial.Type = ELocalLLMCommandType::TranscribeAudio;
    Partial.RequestId = FGuid::NewGuid();
    Partial.SessionId = SessionId;
    Partial.SpeechToText.Provider = TEXT("mock");
    Partial.Audio.SampleRate = 16000;
    Partial.Audio.NumChannels = 1;
    Partial.Audio.Samples.Init(0.1f, 8000);
    const FGuid RequestId = Partial.RequestId;
    Worker.Enqueue(MoveTemp(Partial));

    bool bPartial = false;
    bool bTurn = false;
    const double Deadline = FPlatformTime::Seconds() + 2.0;
    while (!bPartial && FPlatformTime::Seconds() < Deadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.RequestId != RequestId) continue;
            if (Event.Type == ELocalLLMEventType::Error) AddError(Event.Text);
            bPartial |= Event.Type == ELocalLLMEventType::TranscriptionPartial;
            bTurn |= Event.Type == ELocalLLMEventType::TurnCompleted;
        }
        if (!bPartial) FPlatformProcess::Sleep(0.005f);
    }
    TestTrue(TEXT("Partial transcript is emitted"), bPartial);
    TestFalse(TEXT("Partial transcript does not enter character history"), bTurn);
    return !HasAnyErrors();
}

#endif
