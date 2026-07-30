#include "Speech/SherpaOnnxSpeechToTextBackend.h"
#include "ILocalSpeakerEmbeddingBackend.h"

#if LOCAL_MULTIMODAL_LLM_WITH_SHERPA

#include "Misc/Paths.h"
#include "sherpa-onnx/c-api/c-api.h"
#include <string>

namespace
{
class FSherpaOnnxSpeakerEmbeddingBackend final : public ILocalSpeakerEmbeddingBackend
{
public:
    virtual ~FSherpaOnnxSpeakerEmbeddingBackend() override { Unload(); }

    virtual bool Load(const FLocalLLMSpeakerVerificationConfig& Config, FString& OutError) override
    {
        Unload();
        if (!EnsureSherpaOnnxRuntimeLoaded())
        {
            OutError = TEXT("Could not load the packaged sherpa-onnx or ONNX Runtime DLLs");
            return false;
        }
        ModelPath = FPaths::ConvertRelativePathToFull(Config.ModelPath);
        if (!FPaths::FileExists(ModelPath))
        {
            OutError = TEXT("Speaker embedding model was not found: ") + ModelPath;
            return false;
        }
        ModelPathUtf8 = TCHAR_TO_UTF8(*ModelPath);
        SherpaOnnxSpeakerEmbeddingExtractorConfig ExtractorConfig{};
        ExtractorConfig.model = ModelPathUtf8.c_str();
        ExtractorConfig.num_threads = FMath::Clamp(Config.Threads, 1, 16);
        ExtractorConfig.debug = 0;
        ExtractorConfig.provider = "cpu";
        Extractor = SherpaOnnxCreateSpeakerEmbeddingExtractor(&ExtractorConfig);
        if (!Extractor)
        {
            OutError = TEXT("sherpa-onnx could not create the speaker embedding extractor");
            return false;
        }
        Dimension = SherpaOnnxSpeakerEmbeddingExtractorDim(Extractor);
        if (Dimension <= 0)
        {
            OutError = TEXT("Speaker embedding model reported an invalid dimension");
            Unload();
            return false;
        }
        return true;
    }

    virtual void Unload() override
    {
        if (Extractor)
        {
            SherpaOnnxDestroySpeakerEmbeddingExtractor(Extractor);
            Extractor = nullptr;
        }
        Dimension = 0;
    }

    virtual FString GetModelId() const override { return FPaths::GetCleanFilename(ModelPath); }

    virtual bool ExtractEmbedding(const FLocalLLMAudioInput& Audio, TArray<float>& OutEmbedding,
        FString& OutError, const FLocalSpeakerEmbeddingCancelCheck& IsCancelled) override
    {
        if (!Extractor || !Audio.IsValid())
        {
            OutError = TEXT("Speaker embedding extractor is not loaded or audio is invalid");
            return false;
        }
        if (IsCancelled && IsCancelled())
        {
            OutError = TEXT("Speaker embedding extraction was cancelled");
            return false;
        }
        TArray<float> Mono;
        const float* Samples = Audio.Samples.GetData();
        int32 SampleCount = Audio.Samples.Num();
        if (Audio.NumChannels > 1)
        {
            SampleCount = Audio.Samples.Num() / Audio.NumChannels;
            Mono.SetNumUninitialized(SampleCount);
            for (int32 Frame = 0; Frame < SampleCount; ++Frame)
            {
                float Sum = 0.0f;
                for (int32 Channel = 0; Channel < Audio.NumChannels; ++Channel)
                    Sum += Audio.Samples[Frame * Audio.NumChannels + Channel];
                Mono[Frame] = Sum / Audio.NumChannels;
            }
            Samples = Mono.GetData();
        }

        const SherpaOnnxOnlineStream* Stream = SherpaOnnxSpeakerEmbeddingExtractorCreateStream(Extractor);
        if (!Stream)
        {
            OutError = TEXT("Could not create a speaker embedding stream");
            return false;
        }
        SherpaOnnxOnlineStreamAcceptWaveform(Stream, Audio.SampleRate, Samples, SampleCount);
        SherpaOnnxOnlineStreamInputFinished(Stream);
        if (!SherpaOnnxSpeakerEmbeddingExtractorIsReady(Extractor, Stream))
        {
            SherpaOnnxDestroyOnlineStream(Stream);
            OutError = TEXT("The recording is too short to create a speaker embedding");
            return false;
        }
        const float* Values = SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(Extractor, Stream);
        if (Values) OutEmbedding.Append(Values, Dimension);
        if (Values) SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(Values);
        SherpaOnnxDestroyOnlineStream(Stream);
        if (OutEmbedding.Num() != Dimension)
        {
            OutError = TEXT("Speaker embedding extraction returned no vector");
            return false;
        }
        double SumSquares = 0.0;
        for (const float Value : OutEmbedding) SumSquares += static_cast<double>(Value) * Value;
        const float Length = static_cast<float>(FMath::Sqrt(SumSquares));
        if (Length <= UE_SMALL_NUMBER)
        {
            OutError = TEXT("Speaker embedding vector has zero length");
            OutEmbedding.Reset();
            return false;
        }
        for (float& Value : OutEmbedding) Value /= Length;
        return !(IsCancelled && IsCancelled());
    }

private:
    const SherpaOnnxSpeakerEmbeddingExtractor* Extractor = nullptr;
    int32 Dimension = 0;
    FString ModelPath;
    std::string ModelPathUtf8;
};
}

TUniquePtr<ILocalSpeakerEmbeddingBackend> CreateSherpaOnnxSpeakerEmbeddingBackend()
{
    return MakeUnique<FSherpaOnnxSpeakerEmbeddingBackend>();
}

#else

TUniquePtr<ILocalSpeakerEmbeddingBackend> CreateSherpaOnnxSpeakerEmbeddingBackend() { return nullptr; }

#endif
