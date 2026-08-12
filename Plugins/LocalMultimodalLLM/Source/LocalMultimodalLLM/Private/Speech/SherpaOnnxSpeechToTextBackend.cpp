#include "Speech/SherpaOnnxSpeechToTextBackend.h"
#include "ILocalSpeechToTextBackend.h"

#if LOCAL_MULTIMODAL_LLM_WITH_SHERPA

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "sherpa-onnx/c-api/c-api.h"
#include <string>

namespace
{
FCriticalSection RuntimeMutex;
TArray<void*> RuntimeHandles;

bool FindModelFile(const FString& Directory, const TArray<FString>& PreferredNames, FString& OutPath)
{
    for (const FString& Name : PreferredNames)
    {
        const FString Candidate = FPaths::Combine(Directory, Name);
        if (FPaths::FileExists(Candidate))
        {
            OutPath = FPaths::ConvertRelativePathToFull(Candidate);
            return true;
        }
    }
    return false;
}

FString ResolveModelDirectory(const FString& ConfiguredPath)
{
    if (ConfiguredPath.IsEmpty()) return {};

    TArray<FString> Candidates;
    if (!FPaths::IsRelative(ConfiguredPath))
    {
        Candidates.Add(ConfiguredPath);
    }
    else
    {
        // Configuration paths are project-relative by default. Also accept a plugin-relative
        // path so a distributor can bundle an STT model inside the plugin if desired.
        Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
        if (const TSharedPtr<IPlugin> Plugin =
                IPluginManager::Get().FindPlugin(TEXT("LocalMultimodalLLM")); Plugin.IsValid())
        {
            Candidates.Add(FPaths::Combine(Plugin->GetBaseDir(), ConfiguredPath));
        }
        Candidates.Add(ConfiguredPath);
    }

    for (FString Candidate : Candidates)
    {
        Candidate = FPaths::ConvertRelativePathToFull(Candidate);
        FPaths::NormalizeDirectoryName(Candidate);
        if (IFileManager::Get().DirectoryExists(*Candidate)) return Candidate;
    }
    return {};
}

class FSherpaOnnxSpeechToTextBackend final : public ILocalSpeechToTextBackend
{
public:
    virtual ~FSherpaOnnxSpeechToTextBackend() override { Unload(); }

    virtual bool Load(const FLocalLLMSpeechToTextConfig& Config, FString& OutError) override
    {
        Unload();
        if (!EnsureSherpaOnnxRuntimeLoaded())
        {
            OutError = TEXT("Could not load the packaged sherpa-onnx or ONNX Runtime DLLs");
            return false;
        }
        if (Config.bUseGpu)
        {
            OutError = TEXT("This packaged sherpa-onnx provider is CPU-only; disable bUseGpu or provide a GPU-enabled provider module");
            return false;
        }

        const FString Directory = ResolveModelDirectory(Config.ModelPath);
        if (Directory.IsEmpty())
        {
            OutError = FString::Printf(
                TEXT("Parakeet ModelPath '%s' was not found relative to the project or plugin; it must be a directory containing encoder, decoder, joiner, and tokens files"),
                *Config.ModelPath);
            return false;
        }

        FString Encoder;
        FString Decoder;
        FString Joiner;
        FString Tokens;
        if (!FindModelFile(Directory, {TEXT("encoder.int8.onnx"), TEXT("encoder.onnx")}, Encoder) ||
            !FindModelFile(Directory, {TEXT("decoder.int8.onnx"), TEXT("decoder.onnx")}, Decoder) ||
            !FindModelFile(Directory, {TEXT("joiner.int8.onnx"), TEXT("joiner.onnx")}, Joiner) ||
            !FindModelFile(Directory, {TEXT("tokens.txt")}, Tokens))
        {
            OutError = TEXT("Parakeet directory is missing encoder(.int8).onnx, decoder(.int8).onnx, joiner(.int8).onnx, or tokens.txt");
            return false;
        }

        EncoderUtf8 = TCHAR_TO_UTF8(*Encoder);
        DecoderUtf8 = TCHAR_TO_UTF8(*Decoder);
        JoinerUtf8 = TCHAR_TO_UTF8(*Joiner);
        TokensUtf8 = TCHAR_TO_UTF8(*Tokens);

        const FString DecodingMethod = Config.DecodingMethod.IsEmpty()
            ? TEXT("greedy_search") : Config.DecodingMethod.ToLower();
        if (DecodingMethod != TEXT("greedy_search") &&
            DecodingMethod != TEXT("modified_beam_search"))
        {
            OutError = FString::Printf(TEXT(
                "Unsupported sherpa-onnx decoding method '%s'; use greedy_search or modified_beam_search"),
                *Config.DecodingMethod);
            return false;
        }
        DecodingMethodUtf8 = TCHAR_TO_UTF8(*DecodingMethod);

        SherpaOnnxOfflineRecognizerConfig RecognizerConfig{};
        RecognizerConfig.feat_config.sample_rate = 16000;
        RecognizerConfig.feat_config.feature_dim = 80;
        RecognizerConfig.model_config.transducer.encoder = EncoderUtf8.c_str();
        RecognizerConfig.model_config.transducer.decoder = DecoderUtf8.c_str();
        RecognizerConfig.model_config.transducer.joiner = JoinerUtf8.c_str();
        RecognizerConfig.model_config.tokens = TokensUtf8.c_str();
        RecognizerConfig.model_config.num_threads = Config.Threads > 0 ? Config.Threads : FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads() / 2);
        RecognizerConfig.model_config.debug = 0;
        RecognizerConfig.model_config.provider = "cpu";
        RecognizerConfig.model_config.model_type = "nemo_transducer";
        RecognizerConfig.decoding_method = DecodingMethodUtf8.c_str();
        RecognizerConfig.max_active_paths = FMath::Clamp(Config.MaxActivePaths, 1, 16);

        Recognizer = SherpaOnnxCreateOfflineRecognizer(&RecognizerConfig);
        if (!Recognizer)
        {
            OutError = TEXT("sherpa-onnx could not create a Parakeet recognizer; verify that all files come from the same exported model");
            return false;
        }
        ConfiguredLanguage = Config.Language;
        FinalSilencePaddingMilliseconds = FMath::Clamp(
            Config.FinalSilencePaddingMilliseconds, 0, 1000);
        return true;
    }

    virtual void Unload() override
    {
        if (Recognizer)
        {
            SherpaOnnxDestroyOfflineRecognizer(Recognizer);
            Recognizer = nullptr;
        }
    }

    virtual bool Transcribe(const FLocalLLMAudioInput& Audio, FLocalSpeechToTextResult& OutResult,
        FString& OutError, const FLocalSpeechToTextCancelCheck& IsCancelled) override
    {
        if (!Recognizer)
        {
            OutError = TEXT("Parakeet recognizer is not loaded");
            return false;
        }
        if (IsCancelled && IsCancelled())
        {
            OutError = TEXT("Speech transcription was cancelled");
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

        // Offline transducer models need a small amount of right-context to flush the final
        // tokens. Supplying an utterance that ends close to speech can otherwise turn
        // "Do you like your job here?" into "Do you like". This padding is synthesized in
        // memory, so it does not add a real-time endpoint/VAD wait.
        TArray<float> FinalizedSamples;
        const int32 PaddingSamples = Audio.SampleRate > 0
            ? FMath::DivideAndRoundUp(Audio.SampleRate * FinalSilencePaddingMilliseconds, 1000)
            : 0;
        if (PaddingSamples > 0)
        {
            FinalizedSamples.Reserve(SampleCount + PaddingSamples);
            FinalizedSamples.Append(Samples, SampleCount);
            FinalizedSamples.AddZeroed(PaddingSamples);
            Samples = FinalizedSamples.GetData();
            SampleCount = FinalizedSamples.Num();
        }

        const SherpaOnnxOfflineStream* Stream = SherpaOnnxCreateOfflineStream(Recognizer);
        if (!Stream)
        {
            OutError = TEXT("sherpa-onnx could not create an offline transcription stream");
            return false;
        }
        SherpaOnnxAcceptWaveformOffline(Stream, Audio.SampleRate, Samples, SampleCount);
        SherpaOnnxDecodeOfflineStream(Recognizer, Stream);
        const SherpaOnnxOfflineRecognizerResult* Result = SherpaOnnxGetOfflineStreamResult(Stream);
        if (Result)
        {
            if (Result->text) OutResult.Text = UTF8_TO_TCHAR(Result->text);
            OutResult.DetectedLanguage = Result->lang ? UTF8_TO_TCHAR(Result->lang) : ConfiguredLanguage;
            SherpaOnnxDestroyOfflineRecognizerResult(Result);
        }
        SherpaOnnxDestroyOfflineStream(Stream);

        if (IsCancelled && IsCancelled())
        {
            OutError = TEXT("Speech transcription was cancelled");
            return false;
        }
        return true;
    }

private:
    const SherpaOnnxOfflineRecognizer* Recognizer = nullptr;
    std::string EncoderUtf8;
    std::string DecoderUtf8;
    std::string JoinerUtf8;
    std::string TokensUtf8;
    std::string DecodingMethodUtf8;
    FString ConfiguredLanguage;
    int32 FinalSilencePaddingMilliseconds = 320;
};
}

bool EnsureSherpaOnnxRuntimeLoaded()
{
    FScopeLock Lock(&RuntimeMutex);
    if (RuntimeHandles.Num() == 3) return true;
    RuntimeHandles.Reset();
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalMultimodalLLM"));
    if (!Plugin.IsValid()) return false;
    const FString RuntimeDirectory = FPaths::Combine(
        Plugin->GetBaseDir(), TEXT("Binaries"), TEXT("ThirdParty"), TEXT("SherpaOnnx"), TEXT("Win64"));
    FPlatformProcess::PushDllDirectory(*RuntimeDirectory);
    for (const TCHAR* DllName : {TEXT("onnxruntime.dll"), TEXT("onnxruntime_providers_shared.dll"), TEXT("sherpa-onnx-c-api.dll")})
    {
        if (void* Handle = FPlatformProcess::GetDllHandle(*FPaths::Combine(RuntimeDirectory, DllName)))
            RuntimeHandles.Add(Handle);
        else
            break;
    }
    FPlatformProcess::PopDllDirectory(*RuntimeDirectory);
    if (RuntimeHandles.Num() == 3) return true;
    for (int32 Index = RuntimeHandles.Num() - 1; Index >= 0; --Index)
        FPlatformProcess::FreeDllHandle(RuntimeHandles[Index]);
    RuntimeHandles.Reset();
    return false;
}

void ShutdownSherpaOnnxRuntime()
{
    FScopeLock Lock(&RuntimeMutex);
    for (int32 Index = RuntimeHandles.Num() - 1; Index >= 0; --Index)
        FPlatformProcess::FreeDllHandle(RuntimeHandles[Index]);
    RuntimeHandles.Reset();
}

TUniquePtr<ILocalSpeechToTextBackend> CreateSherpaOnnxSpeechToTextBackend()
{
    return MakeUnique<FSherpaOnnxSpeechToTextBackend>();
}

#else

bool EnsureSherpaOnnxRuntimeLoaded() { return false; }
void ShutdownSherpaOnnxRuntime() {}
TUniquePtr<ILocalSpeechToTextBackend> CreateSherpaOnnxSpeechToTextBackend() { return nullptr; }

#endif
