#include "Speech/SherpaOnnxSpeechToTextBackend.h"
#include "ILocalTextToSpeechBackend.h"

#if LOCAL_MULTIMODAL_LLM_WITH_SHERPA

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "sherpa-onnx/c-api/c-api.h"

#include <string>

namespace
{
FString ResolveConfiguredPath(const FString& ConfiguredPath)
{
    static const FString PluginPrefix = TEXT("Plugin:/");
    if (ConfiguredPath.StartsWith(PluginPrefix, ESearchCase::IgnoreCase))
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalMultimodalLLM"));
        if (!Plugin.IsValid()) return {};
        return FPaths::ConvertRelativePathToFull(
            FPaths::Combine(Plugin->GetBaseDir(), ConfiguredPath.RightChop(PluginPrefix.Len())));
    }

    return FPaths::ConvertRelativePathToFull(FPaths::IsRelative(ConfiguredPath)
        ? FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath)
        : ConfiguredPath);
}

bool ResolveRequiredFile(const FString& Directory, const TCHAR* Name, FString& OutPath)
{
    OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, Name));
    return FPaths::FileExists(OutPath);
}

TArray<float> ResampleMono(const float* Samples, const int32 NumSamples, const int32 InputRate, const int32 OutputRate)
{
    if (!Samples || NumSamples <= 0 || InputRate <= 0 || OutputRate <= 0) return {};
    if (InputRate == OutputRate)
    {
        TArray<float> Copy;
        Copy.Append(Samples, NumSamples);
        return Copy;
    }

    const int32 OutputSamples = FMath::Max(1,
        FMath::RoundToInt(static_cast<double>(NumSamples) * OutputRate / InputRate));
    TArray<float> Result;
    Result.SetNumUninitialized(OutputSamples);
    const double Scale = static_cast<double>(InputRate) / OutputRate;
    for (int32 Index = 0; Index < OutputSamples; ++Index)
    {
        const double Source = Index * Scale;
        const int32 A = FMath::Clamp(FMath::FloorToInt(Source), 0, NumSamples - 1);
        const int32 B = FMath::Min(A + 1, NumSamples - 1);
        Result[Index] = FMath::Lerp(Samples[A], Samples[B], static_cast<float>(Source - A));
    }
    return Result;
}

void ApplyEdgeFade(TArray<float>& Samples, const int32 SampleRate, const int32 FadeMilliseconds,
    const bool bFadeIn, const bool bFadeOut)
{
    if (Samples.IsEmpty() || SampleRate <= 0 || FadeMilliseconds <= 0) return;
    const int32 FadeSamples = FMath::Min(
        FMath::Max(1, SampleRate * FadeMilliseconds / 1000), Samples.Num() / 2);
    for (int32 Index = 0; Index < FadeSamples; ++Index)
    {
        const float Gain = static_cast<float>(Index) / FMath::Max(1, FadeSamples);
        if (bFadeIn) Samples[Index] *= Gain;
        if (bFadeOut) Samples[Samples.Num() - 1 - Index] *= Gain;
    }
}

struct FPocketStreamingContext
{
    const FLocalTextToSpeechCancelCheck* IsCancelled = nullptr;
    const FLocalTextToSpeechChunkCallback* OnChunk = nullptr;
    TArray<float> PendingSamples;
    int32 SampleRate = 0;
    int32 ChunkSamples = 0;
    int32 SequenceNumber = 0;
    int32 ReceivedSamples = 0;
    int32 EdgeFadeMilliseconds = 8;
    bool bEmitNativeChunks = true;

    bool IsCancellationRequested() const
    {
        return IsCancelled && *IsCancelled && (*IsCancelled)();
    }

    void EmitAvailable(const bool bFlush)
    {
        if (!OnChunk || !*OnChunk || !bEmitNativeChunks || ChunkSamples <= 0) return;
        // Retain one chunk during generation so the true final chunk can receive a fade-out
        // before playback. This costs one chunk of latency (20 ms in the demo) and prevents
        // Pocket boundary transients from becoming audible clicks or short "tsch" artifacts.
        while ((!bFlush && PendingSamples.Num() >= ChunkSamples * 2) ||
            (bFlush && !PendingSamples.IsEmpty()))
        {
            const int32 Count = bFlush ? FMath::Min(ChunkSamples, PendingSamples.Num()) : ChunkSamples;
            const bool bFirstChunk = SequenceNumber == 0;
            const bool bLastChunk = bFlush && PendingSamples.Num() == Count;
            FLocalLLMAudioChunk Chunk;
            Chunk.SampleRate = SampleRate;
            Chunk.NumChannels = 1;
            Chunk.SequenceNumber = SequenceNumber++;
            Chunk.Samples.Append(PendingSamples.GetData(), Count);
            PendingSamples.RemoveAt(0, Count, EAllowShrinking::No);
            ApplyEdgeFade(Chunk.Samples, SampleRate, EdgeFadeMilliseconds,
                bFirstChunk, bLastChunk);
            (*OnChunk)(Chunk);
        }
    }
};

int32 PocketProgressCallback(const float* Samples, const int32 NumSamples, float, void* UserData)
{
    FPocketStreamingContext* Context = static_cast<FPocketStreamingContext*>(UserData);
    if (!Context || Context->IsCancellationRequested()) return 0;
    if (Samples && NumSamples > 0)
    {
        Context->ReceivedSamples += NumSamples;
        if (Context->bEmitNativeChunks)
        {
            Context->PendingSamples.Append(Samples, NumSamples);
            Context->EmitAvailable(false);
        }
    }
    return Context->IsCancellationRequested() ? 0 : 1;
}

void EmitCompleteChunks(const TArray<float>& Samples, const int32 SampleRate, const int32 ChunkMilliseconds,
    const FLocalTextToSpeechChunkCallback& OnChunk)
{
    if (!OnChunk || Samples.IsEmpty() || SampleRate <= 0) return;
    const int32 ChunkSamples = FMath::Max(1, SampleRate * FMath::Max(20, ChunkMilliseconds) / 1000);
    int32 Sequence = 0;
    for (int32 Offset = 0; Offset < Samples.Num(); Offset += ChunkSamples)
    {
        FLocalLLMAudioChunk Chunk;
        Chunk.SampleRate = SampleRate;
        Chunk.NumChannels = 1;
        Chunk.SequenceNumber = Sequence++;
        Chunk.Samples.Append(Samples.GetData() + Offset, FMath::Min(ChunkSamples, Samples.Num() - Offset));
        OnChunk(Chunk);
    }
}

class FSherpaOnnxPocketTextToSpeechBackend final : public ILocalTextToSpeechBackend
{
public:
    virtual ~FSherpaOnnxPocketTextToSpeechBackend() override { Unload(); }

    virtual bool Load(const FLocalLLMTextToSpeechConfig& InConfig, FString& OutError) override
    {
        Unload();
        if (!EnsureSherpaOnnxRuntimeLoaded())
        {
            OutError = TEXT("Could not load the packaged sherpa-onnx or ONNX Runtime DLLs");
            return false;
        }
        if (InConfig.bUseGpu)
        {
            OutError = TEXT("The packaged Pocket TTS runtime is CPU-only; disable bUseGpu");
            return false;
        }

        FString Directory = InConfig.ModelPath;
        if (Directory.IsEmpty())
        {
            OutError = TEXT("Pocket TTS ModelPath must point to the extracted sherpa-onnx Pocket model directory");
            return false;
        }
        Directory = ResolveConfiguredPath(Directory);
        if (!IFileManager::Get().DirectoryExists(*Directory))
        {
            OutError = FString::Printf(TEXT("Pocket TTS model directory does not exist: %s"), *Directory);
            return false;
        }

        FString LmFlow, LmMain, Encoder, Decoder, TextConditioner, Vocab, TokenScores;
        if (!ResolveRequiredFile(Directory, TEXT("lm_flow.int8.onnx"), LmFlow) ||
            !ResolveRequiredFile(Directory, TEXT("lm_main.int8.onnx"), LmMain) ||
            !ResolveRequiredFile(Directory, TEXT("encoder.onnx"), Encoder) ||
            !ResolveRequiredFile(Directory, TEXT("decoder.int8.onnx"), Decoder) ||
            !ResolveRequiredFile(Directory, TEXT("text_conditioner.onnx"), TextConditioner) ||
            !ResolveRequiredFile(Directory, TEXT("vocab.json"), Vocab) ||
            !ResolveRequiredFile(Directory, TEXT("token_scores.json"), TokenScores))
        {
            OutError = TEXT("Pocket TTS directory is missing one or more required ONNX, vocab.json, or token_scores.json files");
            return false;
        }

        FString ReferencePath = InConfig.SpeakerReferencePath;
        if (ReferencePath.IsEmpty())
        {
            OutError = TEXT("Pocket TTS requires SpeakerReferencePath to point to a consented mono PCM WAV voice sample");
            return false;
        }
        ReferencePath = ResolveConfiguredPath(ReferencePath);
        if (!FPaths::FileExists(ReferencePath))
        {
            OutError = FString::Printf(TEXT("Pocket TTS reference WAV does not exist: %s"), *ReferencePath);
            return false;
        }

        const std::string ReferenceUtf8 = TCHAR_TO_UTF8(*ReferencePath);
        const SherpaOnnxWave* Wave = SherpaOnnxReadWave(ReferenceUtf8.c_str());
        if (!Wave || !Wave->samples || Wave->num_samples <= 0 || Wave->sample_rate <= 0)
        {
            if (Wave) SherpaOnnxFreeWave(Wave);
            OutError = TEXT("Pocket TTS could not decode the reference WAV; use a mono 16-bit PCM WAV file");
            return false;
        }
        ReferenceSamples.Append(Wave->samples, Wave->num_samples);
        ReferenceSampleRate = Wave->sample_rate;
        SherpaOnnxFreeWave(Wave);

        LmFlowUtf8 = TCHAR_TO_UTF8(*LmFlow);
        LmMainUtf8 = TCHAR_TO_UTF8(*LmMain);
        EncoderUtf8 = TCHAR_TO_UTF8(*Encoder);
        DecoderUtf8 = TCHAR_TO_UTF8(*Decoder);
        TextConditionerUtf8 = TCHAR_TO_UTF8(*TextConditioner);
        VocabUtf8 = TCHAR_TO_UTF8(*Vocab);
        TokenScoresUtf8 = TCHAR_TO_UTF8(*TokenScores);

        SherpaOnnxOfflineTtsConfig TtsConfig{};
        TtsConfig.model.pocket.lm_flow = LmFlowUtf8.c_str();
        TtsConfig.model.pocket.lm_main = LmMainUtf8.c_str();
        TtsConfig.model.pocket.encoder = EncoderUtf8.c_str();
        TtsConfig.model.pocket.decoder = DecoderUtf8.c_str();
        TtsConfig.model.pocket.text_conditioner = TextConditionerUtf8.c_str();
        TtsConfig.model.pocket.vocab_json = VocabUtf8.c_str();
        TtsConfig.model.pocket.token_scores_json = TokenScoresUtf8.c_str();
        TtsConfig.model.pocket.voice_embedding_cache_capacity = 8;
        TtsConfig.model.num_threads = InConfig.Threads > 0 ? InConfig.Threads : 2;
        TtsConfig.model.debug = 0;
        TtsConfig.model.provider = "cpu";
        TtsConfig.max_num_sentences = 1;
        TtsConfig.silence_scale = 0.2f;

        Tts = SherpaOnnxCreateOfflineTts(&TtsConfig);
        if (!Tts)
        {
            OutError = TEXT("sherpa-onnx could not create Pocket TTS; verify that the model files match the packaged runtime");
            Unload();
            return false;
        }
        NativeSampleRate = SherpaOnnxOfflineTtsSampleRate(Tts);
        if (NativeSampleRate <= 0)
        {
            OutError = TEXT("Pocket TTS reported an invalid output sample rate");
            Unload();
            return false;
        }
        Config = InConfig;
        return true;
    }

    virtual void Unload() override
    {
        if (Tts)
        {
            SherpaOnnxDestroyOfflineTts(Tts);
            Tts = nullptr;
        }
        NativeSampleRate = 0;
        ReferenceSampleRate = 0;
        ReferenceSamples.Reset();
    }

    virtual bool Synthesize(const FLocalLLMTextToSpeechRequest& Request, FLocalTextToSpeechResult& OutResult,
        FString& OutError, const FLocalTextToSpeechCancelCheck& IsCancelled,
        const FLocalTextToSpeechChunkCallback& OnChunk) override
    {
        if (!Tts)
        {
            OutError = TEXT("Pocket TTS is not loaded");
            return false;
        }
        FString Text = Request.Text;
        Text.TrimStartAndEndInline();
        if (Text.IsEmpty())
        {
            OutError = TEXT("Text-to-speech input is empty");
            return false;
        }
        const FString Language = Request.Language.IsEmpty() ? Config.Language : Request.Language;
        if (!Language.IsEmpty() && !Language.StartsWith(TEXT("en"), ESearchCase::IgnoreCase))
        {
            OutError = FString::Printf(TEXT("The installed Pocket TTS model is English-only; unsupported language: %s"), *Language);
            return false;
        }
        if (IsCancelled && IsCancelled())
        {
            OutError = TEXT("Speech synthesis was cancelled");
            return false;
        }

        const std::string TextUtf8 = TCHAR_TO_UTF8(*Text);
        const float MaxReferenceSeconds = FMath::Clamp(Config.MaxReferenceSeconds, 1.0f, 30.0f);
        const int32 MaxReferenceSamples = FMath::Min(ReferenceSamples.Num(),
            FMath::RoundToInt(MaxReferenceSeconds * ReferenceSampleRate));
        const int32 Seed = Config.Seed;
        const std::string Extra = Seed < 0
            ? std::string(TCHAR_TO_UTF8(*FString::Printf(TEXT("{\"max_reference_audio_len\":%.3f}"), MaxReferenceSeconds)))
            : std::string(TCHAR_TO_UTF8(*FString::Printf(TEXT("{\"max_reference_audio_len\":%.3f,\"seed\":%d}"), MaxReferenceSeconds, Seed)));

        SherpaOnnxGenerationConfig Generation{};
        Generation.silence_scale = 0.2f;
        Generation.speed = FMath::Clamp(Request.SpeakingRate, 0.25f, 4.0f);
        Generation.sid = 0;
        Generation.reference_audio = ReferenceSamples.GetData();
        Generation.reference_audio_len = MaxReferenceSamples;
        Generation.reference_sample_rate = ReferenceSampleRate;
        Generation.num_steps = FMath::Clamp(Config.QualitySteps, 1, 16);
        Generation.extra = Extra.c_str();

        const int32 OutputRate = Config.OutputSampleRate > 0 ? Config.OutputSampleRate : NativeSampleRate;
        FPocketStreamingContext Streaming;
        Streaming.IsCancelled = &IsCancelled;
        Streaming.OnChunk = &OnChunk;
        Streaming.SampleRate = NativeSampleRate;
        Streaming.ChunkSamples = FMath::Max(1,
            NativeSampleRate * FMath::Max(20, Config.ChunkMilliseconds) / 1000);
        Streaming.EdgeFadeMilliseconds = 8;
        Streaming.bEmitNativeChunks = OutputRate == NativeSampleRate;

        const SherpaOnnxGeneratedAudio* Audio = SherpaOnnxOfflineTtsGenerateWithConfig(
            Tts, TextUtf8.c_str(), &Generation, PocketProgressCallback, &Streaming);
        if (IsCancelled && IsCancelled())
        {
            if (Audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(Audio);
            OutError = TEXT("Speech synthesis was cancelled");
            return false;
        }
        if (!Audio || !Audio->samples || Audio->n <= 0 || Audio->sample_rate <= 0)
        {
            if (Audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(Audio);
            OutError = TEXT("Pocket TTS did not produce audio");
            return false;
        }

        if (OutputRate == Audio->sample_rate)
        {
            OutResult.Audio.Samples.Append(Audio->samples, Audio->n);
            if (Streaming.ReceivedSamples < Audio->n)
                Streaming.PendingSamples.Append(Audio->samples + Streaming.ReceivedSamples, Audio->n - Streaming.ReceivedSamples);
            Streaming.EmitAvailable(true);
        }
        else
        {
            OutResult.Audio.Samples = ResampleMono(Audio->samples, Audio->n, Audio->sample_rate, OutputRate);
            ApplyEdgeFade(OutResult.Audio.Samples, OutputRate, 8, true, true);
            EmitCompleteChunks(OutResult.Audio.Samples, OutputRate, Config.ChunkMilliseconds, OnChunk);
        }
        if (OutputRate == Audio->sample_rate)
            ApplyEdgeFade(OutResult.Audio.Samples, OutputRate, 8, true, true);
        OutResult.Audio.SampleRate = OutputRate;
        OutResult.Audio.NumChannels = 1;
        OutResult.VoiceId = Request.VoiceId.IsEmpty() ? Config.VoiceId : Request.VoiceId;
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(Audio);
        if (!OutResult.Audio.IsValid())
        {
            OutError = TEXT("Pocket TTS produced an invalid output buffer");
            return false;
        }
        return true;
    }

private:
    const SherpaOnnxOfflineTts* Tts = nullptr;
    FLocalLLMTextToSpeechConfig Config;
    TArray<float> ReferenceSamples;
    int32 ReferenceSampleRate = 0;
    int32 NativeSampleRate = 0;
    std::string LmFlowUtf8;
    std::string LmMainUtf8;
    std::string EncoderUtf8;
    std::string DecoderUtf8;
    std::string TextConditionerUtf8;
    std::string VocabUtf8;
    std::string TokenScoresUtf8;
};
}

TUniquePtr<ILocalTextToSpeechBackend> CreateSherpaOnnxPocketTextToSpeechBackend()
{
    return MakeUnique<FSherpaOnnxPocketTextToSpeechBackend>();
}

#else

TUniquePtr<ILocalTextToSpeechBackend> CreateSherpaOnnxPocketTextToSpeechBackend()
{
    return nullptr;
}

#endif
