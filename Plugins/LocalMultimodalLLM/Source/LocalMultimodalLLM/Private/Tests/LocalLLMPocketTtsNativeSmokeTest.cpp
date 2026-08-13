#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && LOCAL_MULTIMODAL_LLM_WITH_SHERPA

#include "HAL/PlatformTime.h"
#include "ILocalTextToSpeechBackend.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMPocketTtsNativeSmokeTest,
    "LocalMultimodalLLM.Native.PocketTtsSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMPocketTtsNativeSmokeTest::RunTest(const FString&)
{
    TUniquePtr<ILocalTextToSpeechBackend> Backend =
        FLocalTextToSpeechBackendRegistry::Create(TEXT("pocket-tts"));
    if (!TestNotNull(TEXT("Pocket TTS provider can be created"), Backend.Get())) return false;

    const FString ModelDirectory = FPaths::Combine(TEXT("Models"), TEXT("PocketTTS"),
        TEXT("sherpa-onnx-pocket-tts-int8-2026-01-26"));
    FLocalLLMTextToSpeechConfig Config;
    Config.Provider = TEXT("pocket-tts");
    Config.ModelPath = ModelDirectory;
    Config.SpeakerReferencePath = TEXT("Plugin:/Content/Voices/pocket-caro-davy.wav");
    Config.VoiceId = TEXT("pocket-caro-davy");
    Config.Language = TEXT("en");
    Config.Threads = 2;
    Config.ChunkMilliseconds = 100;
    Config.QualitySteps = 2;
    Config.Seed = 42;

    FString Error;
    const double LoadStart = FPlatformTime::Seconds();
    if (!TestTrue(TEXT("Pocket TTS model loads"), Backend->Load(Config, Error)))
    {
        AddError(Error);
        return false;
    }
    AddInfo(FString::Printf(TEXT("Pocket TTS load time: %.3fs"), FPlatformTime::Seconds() - LoadStart));

    FLocalLLMTextToSpeechRequest Request;
    Request.Text = TEXT("Welcome to Greyhaven.");
    Request.VoiceId = Config.VoiceId;
    Request.SpeakingRate = 1.0f;

    FLocalTextToSpeechResult Result;
    TArray<FLocalLLMAudioChunk> Chunks;
    double FirstChunkSeconds = -1.0;
    const double SynthesisStart = FPlatformTime::Seconds();
    const bool bGenerated = Backend->Synthesize(Request, Result, Error,
        []() { return false; },
        [&Chunks, &FirstChunkSeconds, SynthesisStart](const FLocalLLMAudioChunk& Chunk)
        {
            if (FirstChunkSeconds < 0.0) FirstChunkSeconds = FPlatformTime::Seconds() - SynthesisStart;
            Chunks.Add(Chunk);
        });
    const double SynthesisSeconds = FPlatformTime::Seconds() - SynthesisStart;
    if (!TestTrue(TEXT("Pocket TTS synthesis succeeds"), bGenerated))
    {
        AddError(Error);
        Backend->Unload();
        return false;
    }

    TestTrue(TEXT("Pocket TTS complete PCM is valid"), Result.Audio.IsValid());
    TestEqual(TEXT("Pocket TTS output is mono"), Result.Audio.NumChannels, 1);
    TestTrue(TEXT("Pocket TTS emits at least one streaming chunk"), !Chunks.IsEmpty());
    TestTrue(TEXT("Pocket TTS reports first-audio latency"), FirstChunkSeconds >= 0.0);
    if (!Chunks.IsEmpty())
    {
        TestTrue(TEXT("Pocket stream begins with a de-clicked sample"),
            Chunks[0].Samples.IsEmpty() || FMath::Abs(Chunks[0].Samples[0]) < 1.0e-6f);
        const FLocalLLMAudioChunk& LastChunk = Chunks.Last();
        TestTrue(TEXT("Pocket stream ends with a de-clicked sample"),
            LastChunk.Samples.IsEmpty() || FMath::Abs(LastChunk.Samples.Last()) < 1.0e-6f);
    }
    int32 StreamedSamples = 0;
    for (int32 Index = 0; Index < Chunks.Num(); ++Index)
    {
        TestEqual(TEXT("Pocket chunk sequence is contiguous"), Chunks[Index].SequenceNumber, Index);
        TestEqual(TEXT("Pocket chunk sample rate is stable"), Chunks[Index].SampleRate, Result.Audio.SampleRate);
        TestEqual(TEXT("Pocket chunk channel count is mono"), Chunks[Index].NumChannels, 1);
        StreamedSamples += Chunks[Index].Samples.Num();
    }
    TestEqual(TEXT("Pocket streamed chunks cover complete PCM"), StreamedSamples, Result.Audio.Samples.Num());

    const double DurationSeconds = static_cast<double>(Result.Audio.Samples.Num()) / Result.Audio.SampleRate;
    const double RealTimeFactor = DurationSeconds > 0.0 ? SynthesisSeconds / DurationSeconds : 0.0;
    AddInfo(FString::Printf(TEXT("Pocket TTS: first chunk %.3fs, synthesis %.3fs, audio %.3fs, RTF %.3f, %d Hz"),
        FirstChunkSeconds, SynthesisSeconds, DurationSeconds, RealTimeFactor, Result.Audio.SampleRate));

    FLocalTextToSpeechResult Cancelled;
    FString CancelError;
    TestFalse(TEXT("Pocket TTS honors cancellation before generation"),
        Backend->Synthesize(Request, Cancelled, CancelError, []() { return true; }, {}));
    TestTrue(TEXT("Pocket cancellation reports its reason"), CancelError.Contains(TEXT("cancelled")));
    Backend->Unload();

    struct FBundledVoiceCase
    {
        const TCHAR* VoiceId;
        const TCHAR* FileName;
        const TCHAR* Text;
    };
    const FBundledVoiceCase VoiceCases[] =
    {
        { TEXT("pocket-bill-boerst"), TEXT("pocket-bill-boerst.wav"),
            TEXT("The parcel arrived this morning.") },
        { TEXT("pocket-peter-yearsley"), TEXT("pocket-peter-yearsley.wav"),
            TEXT("The north road is clear today.") },
        { TEXT("pocket-stuart-bell"), TEXT("pocket-stuart-bell.wav"),
            TEXT("I will meet you by the station.") }
    };

    for (const FBundledVoiceCase& VoiceCase : VoiceCases)
    {
        Config.SpeakerReferencePath = FString::Printf(
            TEXT("Plugin:/Content/Voices/%s"), VoiceCase.FileName);
        Config.VoiceId = VoiceCase.VoiceId;
        Error.Reset();
        const FString LoadLabel = FString::Printf(TEXT("Pocket TTS loads bundled voice %s"), VoiceCase.VoiceId);
        if (!TestTrue(LoadLabel, Backend->Load(Config, Error)))
        {
            AddError(Error);
            return false;
        }

        Request.Text = VoiceCase.Text;
        Request.VoiceId = Config.VoiceId;
        FLocalTextToSpeechResult VoiceResult;
        TArray<FLocalLLMAudioChunk> VoiceChunks;
        const bool bVoiceGenerated = Backend->Synthesize(Request, VoiceResult, Error,
            []() { return false; },
            [&VoiceChunks](const FLocalLLMAudioChunk& Chunk) { VoiceChunks.Add(Chunk); });
        TestTrue(FString::Printf(TEXT("Pocket TTS synthesizes bundled voice %s"), VoiceCase.VoiceId),
            bVoiceGenerated);
        if (!bVoiceGenerated) AddError(Error);
        TestTrue(FString::Printf(TEXT("Bundled voice %s produces valid PCM"), VoiceCase.VoiceId),
            VoiceResult.Audio.IsValid());
        TestTrue(FString::Printf(TEXT("Bundled voice %s streams PCM"), VoiceCase.VoiceId),
            !VoiceChunks.IsEmpty());
        Backend->Unload();
    }
    return !HasAnyErrors();
}

#endif
