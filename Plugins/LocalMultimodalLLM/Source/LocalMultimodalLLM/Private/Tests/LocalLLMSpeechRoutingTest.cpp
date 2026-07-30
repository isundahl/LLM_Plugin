#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformProcess.h"
#include "Audio.h"
#include "ILocalSpeechToTextBackend.h"
#include "Inference/InferenceWorker.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Models/LocalLLMModelRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMSpeechRoutingTest,
    "LocalMultimodalLLM.Speech.MockFallbackRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMSpeechRoutingTest::RunTest(const FString&)
{
    TestTrue(TEXT("Mock speech provider is registered"),
        FLocalSpeechToTextBackendRegistry::GetRegisteredProviders().Contains(TEXT("mock")));
#if LOCAL_MULTIMODAL_LLM_WITH_SHERPA
    TestTrue(TEXT("Sherpa ONNX speech provider is registered"),
        FLocalSpeechToTextBackendRegistry::GetRegisteredProviders().Contains(TEXT("sherpa-onnx")));
#endif

    FLocalLLMModelInfo Qwen;
    TestTrue(TEXT("Qwen 3.5 manifest is discovered"),
        FLocalLLMModelRegistry::FindById(TEXT("qwen-3.5-4b-iq3"), Qwen));
    TestTrue(TEXT("Qwen 3.5 manifest is compatible"), Qwen.bCompatible);
    TestTrue(TEXT("Qwen supports text"), Qwen.Config.Capabilities.bText);
    TestTrue(TEXT("Qwen supports vision"), Qwen.Config.Capabilities.bVision);
    TestFalse(TEXT("Qwen does not claim native audio"), Qwen.Config.Capabilities.bAudioInput);
    TestTrue(TEXT("Qwen advertises tool calling"), Qwen.Config.Capabilities.bToolCalling);
    TestTrue(TEXT("Qwen advertises reasoning"), Qwen.Config.Capabilities.bReasoning);
    TestEqual(TEXT("Qwen reasoning defaults off"), Qwen.Config.Generation.ReasoningMode, ELocalLLMReasoningMode::Disabled);
    TestEqual(TEXT("Qwen projector defaults to lazy loading"), Qwen.Config.Load.ProjectorLoadPolicy, ELocalLLMProjectorLoadPolicy::Lazy);

    FLocalLLMInferenceWorker Worker;
    FLocalLLMCommand Load;
    Load.Type = ELocalLLMCommandType::LoadModel;
    Load.Backend = ELocalLLMBackend::Mock;
    Load.RequestId = FGuid::NewGuid();
    Load.ModelConfig = Qwen.Config;
    Worker.Enqueue(MoveTemp(Load));

    const FGuid SessionId = FGuid::NewGuid();
    FLocalLLMCommand Create;
    Create.Type = ELocalLLMCommandType::CreateSession;
    Create.RequestId = FGuid::NewGuid();
    Create.SessionId = SessionId;
    Create.Character.CharacterId = TEXT("speech_test");
    Create.Character.DisplayName = TEXT("Speech Test");
    Worker.Enqueue(MoveTemp(Create));

    FLocalLLMCommand Audio;
    Audio.Type = ELocalLLMCommandType::SubmitAudio;
    Audio.RequestId = FGuid::NewGuid();
    Audio.SessionId = SessionId;
    Audio.AudioInputStrategy = ELocalLLMAudioInputStrategy::TranscriptionOnly;
    Audio.SpeechToText.Provider = TEXT("mock");
    Audio.Audio.SampleRate = 16000;
    Audio.Audio.NumChannels = 1;
    Audio.Audio.Samples.Init(0.0f, 8000);
    const FGuid AudioRequestId = Audio.RequestId;
    Worker.Enqueue(MoveTemp(Audio));

    bool bStarted = false;
    bool bCompleted = false;
    bool bTurnCompleted = false;
    FString Transcript;
    FString Response;
    const double Deadline = FPlatformTime::Seconds() + 10.0;
    while (!bTurnCompleted && FPlatformTime::Seconds() < Deadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.RequestId != AudioRequestId) continue;
            if (Event.Type == ELocalLLMEventType::Error) AddError(Event.Text);
            if (Event.Type == ELocalLLMEventType::TranscriptionStarted)
            {
                bStarted = true;
                TestEqual(TEXT("Started event identifies provider"), Event.SpeechToTextProvider, FName(TEXT("mock")));
            }
            if (Event.Type == ELocalLLMEventType::TranscriptionCompleted)
            {
                bCompleted = true;
                Transcript = Event.Text;
            }
            if (Event.Type == ELocalLLMEventType::TextDelta) Response += Event.Text;
            bTurnCompleted |= Event.Type == ELocalLLMEventType::TurnCompleted;
        }
        if (!bTurnCompleted) FPlatformProcess::Sleep(0.005f);
    }

    TestTrue(TEXT("Transcription started"), bStarted);
    TestTrue(TEXT("Transcription completed"), bCompleted);
    TestTrue(TEXT("Transcript is observable"), Transcript.Contains(TEXT("Mock speech transcript")));
    TestTrue(TEXT("Transcript enters normal character text pipeline"), Response.Contains(TEXT("Mock speech transcript")));
    TestTrue(TEXT("Character turn completed"), bTurnCompleted);
    return !HasAnyErrors();
}

#if LOCAL_MULTIMODAL_LLM_WITH_SHERPA

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMParakeetTranscriptionTest,
    "LocalMultimodalLLM.Speech.ParakeetNativeTranscription",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMParakeetTranscriptionTest::RunTest(const FString&)
{
    const FString ModelDirectory = FPaths::Combine(FPaths::ProjectDir(),
        TEXT("Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming"));
    FString WavePath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("ParakeetTestWav="), WavePath))
        WavePath = FPaths::Combine(ModelDirectory, TEXT("test_wavs/0.wav"));
    WavePath = FPaths::ConvertRelativePathToFull(WavePath);
    if (!FPaths::DirectoryExists(ModelDirectory) || !FPaths::FileExists(WavePath))
    {
        AddError(TEXT("Parakeet model or its reference WAV is missing"));
        return false;
    }

    TUniquePtr<ILocalSpeechToTextBackend> Backend = FLocalSpeechToTextBackendRegistry::Create(TEXT("sherpa-onnx"));
    if (!TestNotNull(TEXT("Sherpa provider factory creates a backend"), Backend.Get())) return false;

    FLocalLLMSpeechToTextConfig Config;
    Config.Provider = TEXT("sherpa-onnx");
    // Exercise the same project-relative path used by config/PIE, not merely an absolute test path.
    Config.ModelPath = TEXT("Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming");
    Config.Language = TEXT("en");
    Config.Threads = 2;
    FString Error;
    if (!Backend->Load(Config, Error))
    {
        AddError(TEXT("Parakeet load failed: ") + Error);
        return false;
    }

    TArray<uint8> WaveBytes;
    if (!FFileHelper::LoadFileToArray(WaveBytes, *WavePath))
    {
        AddError(TEXT("Could not read Parakeet reference WAV"));
        return false;
    }
    FWaveModInfo WaveInfo;
    if (!WaveInfo.ReadWaveInfo(WaveBytes.GetData(), WaveBytes.Num(), &Error) ||
        !WaveInfo.pFormatTag || *WaveInfo.pFormatTag != FWaveModInfo::WAVE_INFO_FORMAT_PCM ||
        !WaveInfo.pBitsPerSample || *WaveInfo.pBitsPerSample != 16)
    {
        AddError(TEXT("Reference WAV is not readable 16-bit PCM: ") + Error);
        return false;
    }

    FLocalLLMAudioInput Audio;
    Audio.SampleRate = static_cast<int32>(*WaveInfo.pSamplesPerSec);
    Audio.NumChannels = static_cast<int32>(*WaveInfo.pChannels);
    const int32 NumSamples = WaveInfo.SampleDataSize / sizeof(int16);
    Audio.Samples.SetNumUninitialized(NumSamples);
    const int16* Pcm = reinterpret_cast<const int16*>(WaveInfo.SampleDataStart);
    for (int32 Index = 0; Index < NumSamples; ++Index)
        Audio.Samples[Index] = static_cast<float>(Pcm[Index]) / 32768.0f;

    FLocalSpeechToTextResult Result;
    if (!Backend->Transcribe(Audio, Result, Error, []() { return false; }))
    {
        AddError(TEXT("Parakeet transcription failed: ") + Error);
        return false;
    }
    TestFalse(TEXT("Parakeet produced a non-empty transcript"), Result.Text.TrimStartAndEnd().IsEmpty());
    AddInfo(TEXT("Parakeet transcript: ") + Result.Text);
    Backend->Unload();
    return !HasAnyErrors();
}

#endif

#endif
