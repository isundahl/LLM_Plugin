#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ILocalTextToSpeechBackend.h"
#include "Speech/LocalLLMSpeechTextUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMTextToSpeechMockProviderTest,
    "LocalMultimodalLLM.Speech.TextToSpeechMockProvider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMTextToSpeechMockProviderTest::RunTest(const FString&)
{
    const FLocalLLMTextToSpeechConfig DefaultConfig;
    TestEqual(TEXT("Default synthesis watchdog is finite"),
        DefaultConfig.SynthesisTimeoutSeconds, 30.0f);
    TestTrue(TEXT("Conversational loudness leveling is enabled by default"),
        DefaultConfig.bNormalizeOutputLoudness);
    TestEqual(TEXT("Default conversational loudness target is conservative"),
        DefaultConfig.TargetOutputRmsDbfs, -27.0f);
    TestEqual(TEXT("Default loudness boost is bounded"),
        DefaultConfig.MaxOutputGainDb, 8.0f);
    TestEqual(TEXT("Default TTS sampling temperature favors stable speech"),
        DefaultConfig.SamplingTemperature, 0.70f);
    TestEqual(TEXT("Default TTS top-k limits token drift"),
        DefaultConfig.SamplingTopK, 30);

    const FString LongSentence =
        TEXT("That's the reward notice for two notorious outlaws: Laura Bullion and Harry Longbaugh, ")
        TEXT("who are wanted by the Marshal's office.");
    const TArray<FString> Segments =
        LocalLLMSpeechTextUtils::SplitQueuedSpeech(LongSentence, 100, 0.58f);
    TestEqual(TEXT("Long batch speech is split once"), Segments.Num(), 2);
    if (Segments.Num() == 2)
    {
        TestTrue(TEXT("The first balanced clause is slightly longer"),
            Segments[0].Len() > Segments[1].Len());
        TestTrue(TEXT("The split preserves the first fugitive"),
            Segments[0].Contains(TEXT("Laura Bullion")));
        TestTrue(TEXT("The split preserves the second fugitive"),
            Segments[1].Contains(TEXT("Harry Longbaugh")));
        TestEqual(TEXT("Natural splitting preserves the complete text"),
            FString::Join(Segments, TEXT(" ")), LongSentence);
    }
    TestEqual(TEXT("Short speech remains one segment"),
        LocalLLMSpeechTextUtils::SplitQueuedSpeech(TEXT("Why do you ask?"), 100, 0.58f).Num(), 1);
    TestEqual(TEXT("Zero segment limit disables splitting"),
        LocalLLMSpeechTextUtils::SplitQueuedSpeech(LongSentence, 0, 0.58f).Num(), 1);

    const FString AliasSentence =
        TEXT("Those are reward notices for two fugitives: Laura Bullion, alias Della Rose, ")
        TEXT("is a woman wanted for $500, and Harry Longbaugh, alias the Sundance Kid, ")
        TEXT("is a man wanted for $4,000.");
    const TArray<FString> AliasSegments =
        LocalLLMSpeechTextUtils::SplitQueuedSpeech(AliasSentence, 100, 0.58f);
    TestEqual(TEXT("Long alias sentence is kept in bounded synthesis segments"), AliasSegments.Num(), 3);
    if (AliasSegments.Num() == 3)
    {
        TestEqual(TEXT("Alias sentence text is preserved"),
            FString::Join(AliasSegments, TEXT(" ")), AliasSentence);
    }

    const FString DriftProneSentence =
        TEXT("It's quiet here, but I know the way to keep the ledger straight and protect your secrets from prying eyes.");
    const TArray<FString> DriftProneSegments =
        LocalLLMSpeechTextUtils::SplitQueuedSpeech(DriftProneSentence, 100, 0.58f);
    TestEqual(TEXT("A request just over the limit is split to prevent TTS tail drift"),
        DriftProneSegments.Num(), 2);
    TestEqual(TEXT("Tail-drift sentence text is preserved"),
        FString::Join(DriftProneSegments, TEXT(" ")), DriftProneSentence);

    double BatchSeconds = 0.0;
    TestTrue(TEXT("A completed batch inside its limit is accepted"),
        LocalLLMSpeechTextUtils::ValidateBatchDuration(192000, 24000, 1, 8.0, BatchSeconds));
    TestEqual(TEXT("Accepted batch duration is measured"), BatchSeconds, 8.0);
    TestFalse(TEXT("An oversized completed batch is rejected before publication"),
        LocalLLMSpeechTextUtils::ValidateBatchDuration(192480, 24000, 1, 8.0, BatchSeconds));
    TestTrue(TEXT("Rejected batch reports its actual duration"), BatchSeconds > 8.0);

    TestTrue(TEXT("Mock TTS provider is registered"),
        FLocalTextToSpeechBackendRegistry::GetRegisteredProviders().Contains(TEXT("mock")));
#if LOCAL_MULTIMODAL_LLM_WITH_SHERPA
    TestTrue(TEXT("Native Pocket TTS provider is registered"),
        FLocalTextToSpeechBackendRegistry::GetRegisteredProviders().Contains(TEXT("pocket-tts")));
#endif

    TUniquePtr<ILocalTextToSpeechBackend> Backend = FLocalTextToSpeechBackendRegistry::Create(TEXT("mock"));
    TestNotNull(TEXT("Mock TTS provider can be created"), Backend.Get());
    if (!Backend) return false;

    FLocalLLMTextToSpeechConfig Config;
    Config.Provider = TEXT("mock");
    Config.VoiceId = TEXT("default_voice");
    Config.OutputSampleRate = 16000;
    Config.ChunkMilliseconds = 50;

    FString Error;
    TestTrue(TEXT("Mock TTS provider loads"), Backend->Load(Config, Error));
    TestTrue(TEXT("Load has no error"), Error.IsEmpty());

    FLocalLLMTextToSpeechRequest Request;
    Request.Text = TEXT("Hello from the provider-neutral text-to-speech test.");
    Request.VoiceId = TEXT("test_voice");

    FLocalLLMTextToSpeechRequest WarmupRequest;
    WarmupRequest.Text = TEXT("Ready.");
    WarmupRequest.VoiceId = Config.VoiceId;
    FString WarmupError;
    TestTrue(TEXT("Provider-neutral silent voice prewarm succeeds"),
        Backend->PrewarmVoice(WarmupRequest, WarmupError, []() { return false; }));
    TestTrue(TEXT("Voice prewarm has no error"), WarmupError.IsEmpty());

    FLocalTextToSpeechResult Result;
    TArray<FLocalLLMAudioChunk> Chunks;
    const bool bSynthesized = Backend->Synthesize(Request, Result, Error,
        []() { return false; },
        [&Chunks](const FLocalLLMAudioChunk& Chunk) { Chunks.Add(Chunk); });

    TestTrue(TEXT("Mock synthesis succeeds"), bSynthesized);
    TestTrue(TEXT("Synthesis has no error"), Error.IsEmpty());
    TestTrue(TEXT("Complete PCM is valid"), Result.Audio.IsValid());
    TestEqual(TEXT("Requested sample rate is honored"), Result.Audio.SampleRate, 16000);
    TestEqual(TEXT("Mock output is mono"), Result.Audio.NumChannels, 1);
    TestEqual(TEXT("Per-request voice overrides config"), Result.VoiceId, FString(TEXT("test_voice")));
    TestTrue(TEXT("Synthesis emits streaming chunks"), Chunks.Num() > 1);

    int32 StreamedSampleCount = 0;
    for (int32 Index = 0; Index < Chunks.Num(); ++Index)
    {
        TestEqual(TEXT("Chunk sequence is contiguous"), Chunks[Index].SequenceNumber, Index);
        TestEqual(TEXT("Chunk sample rate is stable"), Chunks[Index].SampleRate, Result.Audio.SampleRate);
        TestEqual(TEXT("Chunk channel count is stable"), Chunks[Index].NumChannels, Result.Audio.NumChannels);
        StreamedSampleCount += Chunks[Index].Samples.Num();
    }
    TestEqual(TEXT("Streamed chunks cover complete output"), StreamedSampleCount, Result.Audio.Samples.Num());

    FLocalTextToSpeechResult CancelledResult;
    FString CancelError;
    const bool bCancelledSucceeded = Backend->Synthesize(Request, CancelledResult, CancelError,
        []() { return true; }, {});
    TestFalse(TEXT("Cancellation stops synthesis"), bCancelledSucceeded);
    TestTrue(TEXT("Cancellation reports its reason"), CancelError.Contains(TEXT("cancelled")));

    Backend->Unload();
    return true;
}

#endif
