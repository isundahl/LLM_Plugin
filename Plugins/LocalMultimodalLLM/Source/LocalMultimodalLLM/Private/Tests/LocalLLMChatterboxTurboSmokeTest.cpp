#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS

#include "Misc/AutomationTest.h"
#include "ILocalTextToSpeechBackend.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalLLMChatterboxTurboSmokeTest,
    "LocalMultimodalLLM.Native.ChatterboxTurboSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMChatterboxTurboSmokeTest::RunTest(const FString&)
{
    const FString Runtime = FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/ChatterboxTurboBenchmark"));
    if (!FPaths::FileExists(FPaths::Combine(Runtime, TEXT("venv/Scripts/python.exe"))))
    {
        AddWarning(TEXT("Optional Chatterbox benchmark runtime is not installed; skipping native sidecar smoke test"));
        return true;
    }
    TUniquePtr<ILocalTextToSpeechBackend> Backend =
        FLocalTextToSpeechBackendRegistry::Create(TEXT("chatterbox-turbo"));
    if (!TestNotNull(TEXT("Chatterbox Turbo provider can be created"), Backend.Get())) return false;
    TUniquePtr<ILocalTextToSpeechBackend> TaroBackend =
        FLocalTextToSpeechBackendRegistry::Create(TEXT("chatterbox-turbo"));
    if (!TestNotNull(TEXT("A second Chatterbox voice backend can be created"), TaroBackend.Get())) return false;

    FLocalLLMTextToSpeechConfig Config;
    Config.Provider = TEXT("chatterbox-turbo");
    Config.ModelPath = Runtime;
    Config.VoiceId = TEXT("ada-p003");
    Config.SpeakerReferencePath = FPaths::Combine(FPaths::ProjectDir(),
        TEXT("TestData/TTS/EARS/p003/emo_neutral_sentences.wav"));
    Config.bUseGpu = true;
    Config.ChunkMilliseconds = 20;
    Config.Seed = 42;
    FString Error;
    const double LoadStarted = FPlatformTime::Seconds();
    if (!TestTrue(TEXT("Chatterbox worker loads and restores Ada's cached voice"), Backend->Load(Config, Error)))
    {
        AddError(Error);
        return false;
    }
    AddInfo(FString::Printf(TEXT("Chatterbox sidecar load and warmup: %.3fs"),
        FPlatformTime::Seconds() - LoadStarted));

    FLocalLLMTextToSpeechConfig TaroConfig = Config;
    TaroConfig.VoiceId = TEXT("taro-p008");
    TaroConfig.SpeakerReferencePath = FPaths::Combine(FPaths::ProjectDir(),
        TEXT("TestData/TTS/EARS/p008/emo_neutral_sentences.wav"));
    const double SecondVoiceStarted = FPlatformTime::Seconds();
    if (!TestTrue(TEXT("Taro joins the existing shared model without another model load"),
        TaroBackend->Load(TaroConfig, Error)))
    {
        AddError(Error);
        return false;
    }
    AddInfo(FString::Printf(TEXT("Second voice preparation and warmup: %.3fs"),
        FPlatformTime::Seconds() - SecondVoiceStarted));

    FLocalLLMTextToSpeechRequest Request;
    Request.Text = TEXT("I might have known you would ask that. [chuckle] The mail has a way of finding trouble.");
    FLocalTextToSpeechResult Result;
    TArray<FLocalLLMAudioChunk> Chunks;
    const double Started = FPlatformTime::Seconds();
    const bool bGenerated = Backend->Synthesize(Request, Result, Error, []() { return false; },
        [&Chunks](const FLocalLLMAudioChunk& Chunk) { Chunks.Add(Chunk); });
    const double GenerationSeconds = FPlatformTime::Seconds() - Started;
    if (!TestTrue(TEXT("Chatterbox emotional-tag synthesis succeeds"), bGenerated)) AddError(Error);
    TestTrue(TEXT("Chatterbox PCM is valid"), Result.Audio.IsValid());
    TestTrue(TEXT("Chatterbox PCM is chunked for the MetaHuman bridge"), Chunks.Num() > 1);
    AddInfo(FString::Printf(TEXT("Chatterbox tagged line ready in %.3fs; audio %.3fs; chunks %d"),
        GenerationSeconds,
        static_cast<double>(Result.Audio.Samples.Num()) / FMath::Max(1, Result.Audio.SampleRate), Chunks.Num()));

    FLocalLLMTextToSpeechRequest TaroRequest;
    TaroRequest.Text = TEXT("Keep to the road and mind the water. I will carry the parcel.");
    FLocalTextToSpeechResult TaroResult;
    const bool bTaroGenerated = TaroBackend->Synthesize(TaroRequest, TaroResult, Error,
        []() { return false; }, {});
    if (!TestTrue(TEXT("Shared worker switches to Taro and synthesizes"), bTaroGenerated)) AddError(Error);
    TestTrue(TEXT("Taro PCM is valid"), TaroResult.Audio.IsValid());

    FString PrewarmError;
    TestTrue(TEXT("Shared worker can switch back to Ada during speculative prewarm"),
        Backend->PrewarmVoice({}, PrewarmError, []() { return false; }));
    TestTrue(TEXT("Ada voice switch has no error"), PrewarmError.IsEmpty());
    TaroBackend->Unload();
    Backend->Unload();
    return bGenerated && bTaroGenerated && Result.Audio.IsValid() && TaroResult.Audio.IsValid();
}

#endif

