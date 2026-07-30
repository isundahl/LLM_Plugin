#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && LOCAL_MULTIMODAL_LLM_WITH_SHERPA

#include "Audio.h"
#include "ILocalSpeakerEmbeddingBackend.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
bool LoadPcm16Wave(const FString& Path, FLocalLLMAudioInput& OutAudio, FString& OutError)
{
    TArray<uint8> Bytes;
    FWaveModInfo Info;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !Info.ReadWaveInfo(Bytes.GetData(), Bytes.Num(), &OutError) ||
        !Info.pBitsPerSample || *Info.pBitsPerSample != 16) return false;
    OutAudio.SampleRate = *Info.pSamplesPerSec;
    OutAudio.NumChannels = *Info.pChannels;
    const int32 NumSamples = Info.SampleDataSize / sizeof(int16);
    OutAudio.Samples.SetNumUninitialized(NumSamples);
    const int16* Pcm = reinterpret_cast<const int16*>(Info.SampleDataStart);
    for (int32 Index = 0; Index < NumSamples; ++Index) OutAudio.Samples[Index] = Pcm[Index] / 32768.0f;
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMTitaNetEmbeddingTest,
    "LocalMultimodalLLM.Speech.TitaNetSpeakerEmbedding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMTitaNetEmbeddingTest::RunTest(const FString&)
{
    const FString ModelPath = FPaths::Combine(FPaths::ProjectDir(),
        TEXT("Models/SpeakerVerification/nemo_en_titanet_small.onnx"));
    const FString WavePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Models/SpeakerVerification/test_wavs/an255-fash-b.wav"));
    const FString DifferentWavePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Models/SpeakerVerification/test_wavs/cen7-fash-b.wav"));
    if (!TestTrue(TEXT("TiTaNet Small model exists"), FPaths::FileExists(ModelPath)) ||
        !TestTrue(TEXT("Speaker test WAV exists"), FPaths::FileExists(WavePath)) ||
        !TestTrue(TEXT("Different-speaker WAV exists"), FPaths::FileExists(DifferentWavePath))) return false;

    TUniquePtr<ILocalSpeakerEmbeddingBackend> Backend =
        FLocalSpeakerEmbeddingBackendRegistry::Create(TEXT("sherpa-onnx"));
    if (!TestNotNull(TEXT("Sherpa speaker provider is registered"), Backend.Get())) return false;
    FLocalLLMSpeakerVerificationConfig Config;
    Config.ModelPath = ModelPath;
    FString Error;
    if (!Backend->Load(Config, Error))
    {
        AddError(TEXT("TiTaNet load failed: ") + Error);
        return false;
    }

    FLocalLLMAudioInput Audio;
    FLocalLLMAudioInput DifferentAudio;
    if (!LoadPcm16Wave(WavePath, Audio, Error) || !LoadPcm16Wave(DifferentWavePath, DifferentAudio, Error))
    {
        AddError(TEXT("Could not read speaker test WAV: ") + Error);
        return false;
    }
    TArray<float> First;
    TArray<float> Second;
    TArray<float> Different;
    TestTrue(TEXT("First embedding extracts"), Backend->ExtractEmbedding(Audio, First, Error, []() { return false; }));
    TestTrue(TEXT("Second embedding extracts"), Backend->ExtractEmbedding(Audio, Second, Error, []() { return false; }));
    TestTrue(TEXT("Different-speaker embedding extracts"), Backend->ExtractEmbedding(DifferentAudio, Different, Error, []() { return false; }));
    TestTrue(TEXT("Embedding has dimensions"), First.Num() > 0);
    TestEqual(TEXT("Repeated embedding dimension matches"), First.Num(), Second.Num());
    double Similarity = 0.0;
    for (int32 Index = 0; Index < FMath::Min(First.Num(), Second.Num()); ++Index)
        Similarity += static_cast<double>(First[Index]) * Second[Index];
    TestTrue(TEXT("Same recording verifies against itself"), Similarity > 0.99);
    double DifferentSimilarity = 0.0;
    for (int32 Index = 0; Index < FMath::Min(First.Num(), Different.Num()); ++Index)
        DifferentSimilarity += static_cast<double>(First[Index]) * Different[Index];
    TestTrue(TEXT("Different NVIDIA example speaker is rejected at the default threshold"),
        DifferentSimilarity < Config.SimilarityThreshold);
    AddInfo(FString::Printf(TEXT("TiTaNet dimension %d, self similarity %.4f, different-speaker similarity %.4f"),
        First.Num(), Similarity, DifferentSimilarity));
    Backend->Unload();
    return !HasAnyErrors();
}

#endif
