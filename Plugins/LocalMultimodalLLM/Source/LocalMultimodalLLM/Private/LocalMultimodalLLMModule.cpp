#include "LocalMultimodalLLMModule.h"
#include "ILocalSpeechToTextBackend.h"
#include "ILocalSpeakerEmbeddingBackend.h"
#include "ILocalTextToSpeechBackend.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Speech/SherpaOnnxSpeechToTextBackend.h"
#include "Speech/ChatterboxTurboTextToSpeechBackend.h"
#include "Speech/NeuTTS2ETextToSpeechBackend.h"

DEFINE_LOG_CATEGORY(LogLocalMultimodalLLM);

namespace
{
#if !UE_BUILD_SHIPPING
class FMockSpeechToTextBackend final : public ILocalSpeechToTextBackend
{
public:
    virtual bool Load(const FLocalLLMSpeechToTextConfig&, FString&) override
    {
        bLoaded = true;
        return true;
    }

    virtual void Unload() override { bLoaded = false; }

    virtual bool Transcribe(const FLocalLLMAudioInput& Audio, FLocalSpeechToTextResult& OutResult,
        FString& OutError, const FLocalSpeechToTextCancelCheck& IsCancelled) override
    {
        if (!bLoaded)
        {
            OutError = TEXT("Mock speech-to-text backend is not loaded");
            return false;
        }
        if (IsCancelled && IsCancelled())
        {
            OutError = TEXT("Speech transcription was cancelled");
            return false;
        }
        const double Duration = static_cast<double>(Audio.Samples.Num()) /
            static_cast<double>(FMath::Max(1, Audio.SampleRate * Audio.NumChannels));
        OutResult.Text = FString::Printf(TEXT("Mock speech transcript from %.2f seconds of audio."), Duration);
        OutResult.DetectedLanguage = TEXT("en");
        OutResult.Confidence = 1.0f;
        return true;
    }

private:
    bool bLoaded = false;
};

class FMockTextToSpeechBackend final : public ILocalTextToSpeechBackend
{
public:
    virtual bool Load(const FLocalLLMTextToSpeechConfig& InConfig, FString&) override
    {
        Config = InConfig;
        bLoaded = true;
        return true;
    }

    virtual void Unload() override { bLoaded = false; }

    virtual bool Synthesize(const FLocalLLMTextToSpeechRequest& Request, FLocalTextToSpeechResult& OutResult,
        FString& OutError, const FLocalTextToSpeechCancelCheck& IsCancelled,
        const FLocalTextToSpeechChunkCallback& OnChunk) override
    {
        if (!bLoaded)
        {
            OutError = TEXT("Mock text-to-speech backend is not loaded");
            return false;
        }
        if (Request.Text.TrimStartAndEnd().IsEmpty())
        {
            OutError = TEXT("Text-to-speech input is empty");
            return false;
        }

        const int32 SampleRate = Config.OutputSampleRate > 0 ? Config.OutputSampleRate : 24000;
        const float Rate = FMath::Clamp(Request.SpeakingRate, 0.25f, 4.0f);
        const float Duration = FMath::Clamp(Request.Text.Len() * 0.04f / Rate, 0.2f, 3.0f);
        const int32 SampleCount = FMath::RoundToInt(Duration * SampleRate);
        OutResult.Audio.SampleRate = SampleRate;
        OutResult.Audio.NumChannels = 1;
        OutResult.Audio.Samples.SetNumUninitialized(SampleCount);
        OutResult.VoiceId = Request.VoiceId.IsEmpty() ? Config.VoiceId : Request.VoiceId;
        for (int32 Index = 0; Index < SampleCount; ++Index)
        {
            if (IsCancelled && IsCancelled())
            {
                OutError = TEXT("Speech synthesis was cancelled");
                return false;
            }
            OutResult.Audio.Samples[Index] = 0.05f * FMath::Sin(2.0f * PI * 220.0f * Index / SampleRate);
        }

        const int32 ChunkSamples = FMath::Max(1, SampleRate * FMath::Max(20, Config.ChunkMilliseconds) / 1000);
        int32 Sequence = 0;
        for (int32 Offset = 0; Offset < SampleCount; Offset += ChunkSamples)
        {
            if (IsCancelled && IsCancelled())
            {
                OutError = TEXT("Speech synthesis was cancelled");
                return false;
            }
            FLocalLLMAudioChunk Chunk;
            Chunk.SampleRate = SampleRate;
            Chunk.NumChannels = 1;
            Chunk.SequenceNumber = Sequence++;
            Chunk.Samples.Append(OutResult.Audio.Samples.GetData() + Offset, FMath::Min(ChunkSamples, SampleCount - Offset));
            if (OnChunk) OnChunk(Chunk);
        }
        return true;
    }

private:
    FLocalLLMTextToSpeechConfig Config;
    bool bLoaded = false;
};
#endif
}

void FLocalMultimodalLLMModule::StartupModule()
{
#if !UE_BUILD_SHIPPING
    FLocalSpeechToTextBackendRegistry::Register(TEXT("mock"),
        []() { return MakeUnique<FMockSpeechToTextBackend>(); });
    FLocalTextToSpeechBackendRegistry::Register(TEXT("mock"),
        []() { return MakeUnique<FMockTextToSpeechBackend>(); });
#endif
#if PLATFORM_WINDOWS && LOCAL_MULTIMODAL_LLM_WITH_LLAMA
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalMultimodalLLM"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogLocalMultimodalLLM, Error, TEXT("Could not locate the LocalMultimodalLLM plugin runtime directory"));
        return;
    }

    const FString RuntimeDirectory = FPaths::Combine(
        Plugin->GetBaseDir(), TEXT("Binaries"), TEXT("ThirdParty"), TEXT("LlamaCpp"), TEXT("Win64"));

    // Load the directly linked DLLs in dependency order. Backends remain
    // dynamic modules and are selected by ggml when an inference context starts.
    static const TCHAR* CoreDlls[] =
    {
        TEXT("ggml-base.dll"),
        TEXT("ggml.dll"),
        TEXT("llama.dll"),
        TEXT("mtmd.dll")
    };

    FPlatformProcess::PushDllDirectory(*RuntimeDirectory);
    for (const TCHAR* DllName : CoreDlls)
    {
        const FString DllPath = FPaths::Combine(RuntimeDirectory, DllName);
        if (void* Handle = FPlatformProcess::GetDllHandle(*DllPath))
        {
            LoadedLlamaDllHandles.Add(Handle);
        }
        else
        {
            UE_LOG(LogLocalMultimodalLLM, Error, TEXT("Failed to load llama.cpp runtime DLL: %s"), *DllPath);
        }
    }
    FPlatformProcess::PopDllDirectory(*RuntimeDirectory);
#endif

#if PLATFORM_WINDOWS && LOCAL_MULTIMODAL_LLM_WITH_SHERPA
    bSherpaProviderRegistered = FLocalSpeechToTextBackendRegistry::Register(TEXT("sherpa-onnx"),
        []() { return CreateSherpaOnnxSpeechToTextBackend(); });
    bSherpaSpeakerProviderRegistered = FLocalSpeakerEmbeddingBackendRegistry::Register(TEXT("sherpa-onnx"),
        []() { return CreateSherpaOnnxSpeakerEmbeddingBackend(); });
    bPocketTtsProviderRegistered = FLocalTextToSpeechBackendRegistry::Register(TEXT("pocket-tts"),
        []() { return CreateSherpaOnnxPocketTextToSpeechBackend(); });
#endif

#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
    bChatterboxTtsProviderRegistered = FLocalTextToSpeechBackendRegistry::Register(TEXT("chatterbox-turbo"),
        []() { return CreateChatterboxTurboTextToSpeechBackend(); });
    bNeuTts2EProviderRegistered = FLocalTextToSpeechBackendRegistry::Register(TEXT("neutts-2e"),
        []() { return CreateNeuTTS2ETextToSpeechBackend(); });
#endif

    UE_LOG(LogLocalMultimodalLLM, Log, TEXT("LocalMultimodalLLM module started"));
}

void FLocalMultimodalLLMModule::ShutdownModule()
{
    if (bSherpaProviderRegistered)
    {
        FLocalSpeechToTextBackendRegistry::Unregister(TEXT("sherpa-onnx"));
        bSherpaProviderRegistered = false;
    }
    if (bSherpaSpeakerProviderRegistered)
    {
        FLocalSpeakerEmbeddingBackendRegistry::Unregister(TEXT("sherpa-onnx"));
        bSherpaSpeakerProviderRegistered = false;
    }
    if (bPocketTtsProviderRegistered)
    {
        FLocalTextToSpeechBackendRegistry::Unregister(TEXT("pocket-tts"));
        bPocketTtsProviderRegistered = false;
    }
    if (bChatterboxTtsProviderRegistered)
    {
        FLocalTextToSpeechBackendRegistry::Unregister(TEXT("chatterbox-turbo"));
        bChatterboxTtsProviderRegistered = false;
    }
    if (bNeuTts2EProviderRegistered)
    {
        FLocalTextToSpeechBackendRegistry::Unregister(TEXT("neutts-2e"));
        bNeuTts2EProviderRegistered = false;
    }
#if !UE_BUILD_SHIPPING
    FLocalSpeechToTextBackendRegistry::Unregister(TEXT("mock"));
    FLocalTextToSpeechBackendRegistry::Unregister(TEXT("mock"));
#endif
    ShutdownSherpaOnnxRuntime();
    for (int32 Index = LoadedLlamaDllHandles.Num() - 1; Index >= 0; --Index)
    {
        FPlatformProcess::FreeDllHandle(LoadedLlamaDllHandles[Index]);
    }
    LoadedLlamaDllHandles.Reset();
}

IMPLEMENT_MODULE(FLocalMultimodalLLMModule, LocalMultimodalLLM)
