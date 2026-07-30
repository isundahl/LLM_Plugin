#include "Speech/ChatterboxTurboTextToSpeechBackend.h"

#include "ILocalTextToSpeechBackend.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Speech/LocalLLMSpeechTextUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString ResolveProjectPath(FString Path)
{
    if (FPaths::IsRelative(Path)) Path = FPaths::Combine(FPaths::ProjectDir(), Path);
    return FPaths::ConvertRelativePathToFull(Path);
}

bool SaveJsonAtomically(const FString& Path, const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    const FString Temporary = Path + TEXT(".tmp");
    return FJsonSerializer::Serialize(Object, Writer) && FFileHelper::SaveStringToFile(Text, *Temporary) &&
        IFileManager::Get().Move(*Path, *Temporary, true, true, false, true);
}

bool LoadJson(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

struct FScopedCriticalSectionUnlock
{
    explicit FScopedCriticalSectionUnlock(FCriticalSection& InCriticalSection)
        : CriticalSection(InCriticalSection)
    {
    }

    ~FScopedCriticalSectionUnlock()
    {
        CriticalSection.Unlock();
    }

    FCriticalSection& CriticalSection;
};

/** One process/model shared by every Chatterbox backend instance in this Unreal process. */
class FChatterboxTurboSharedService final
{
public:
    ~FChatterboxTurboSharedService() { Stop(); }

    bool Start(const FLocalLLMTextToSpeechConfig& Config, FString& OutError)
    {
#if !PLATFORM_WINDOWS
        OutError = TEXT("The Chatterbox Turbo sidecar is currently configured for Win64 only");
        return false;
#else
        RuntimeRoot = ResolveProjectPath(Config.ModelPath);
        const FString Python = FPaths::Combine(RuntimeRoot, TEXT("venv"), TEXT("Scripts"), TEXT("python.exe"));
        const FString Worker = FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts"), TEXT("ChatterboxTurboWorker.py"));
        if (!FPaths::FileExists(Python) || !FPaths::FileExists(Worker))
        {
            OutError = FString::Printf(TEXT("Chatterbox runtime or shared worker is missing: %s"), *RuntimeRoot);
            return false;
        }
        InstanceDirectory = FPaths::Combine(RuntimeRoot, TEXT("runtime"),
            TEXT("shared-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
        RequestsDirectory = FPaths::Combine(InstanceDirectory, TEXT("requests"));
        ResponsesDirectory = FPaths::Combine(InstanceDirectory, TEXT("responses"));
        IFileManager::Get().MakeDirectory(*RequestsDirectory, true);
        IFileManager::Get().MakeDirectory(*ResponsesDirectory, true);
        const FString Arguments = FString::Printf(TEXT("\"%s\" --runtime-dir \"%s\" --seed %d"),
            *Worker, *InstanceDirectory, Config.Seed);
        Process = FPlatformProcess::CreateProc(*Python, *Arguments, true, true, true, nullptr, 0,
            *FPaths::ProjectDir(), nullptr, nullptr);
        if (!Process.IsValid())
        {
            OutError = TEXT("Could not launch the shared Chatterbox Turbo worker");
            return false;
        }
        const FString ReadyPath = FPaths::Combine(InstanceDirectory, TEXT("ready.json"));
        const double Deadline = FPlatformTime::Seconds() + 120.0;
        while (FPlatformTime::Seconds() < Deadline)
        {
            if (FPaths::FileExists(ReadyPath))
            {
                TSharedPtr<FJsonObject> Ready;
                if (!LoadJson(ReadyPath, Ready)) { FPlatformProcess::SleepNoStats(0.01f); continue; }
                bool bOk = false;
                Ready->TryGetBoolField(TEXT("ok"), bOk);
                if (!bOk)
                {
                    Ready->TryGetStringField(TEXT("error"), OutError);
                    if (OutError.IsEmpty()) OutError = TEXT("Shared Chatterbox worker initialization failed");
                    Stop();
                    return false;
                }
                bRunning = true;
                return true;
            }
            if (!FPlatformProcess::IsProcRunning(Process))
            {
                OutError = TEXT("Shared Chatterbox worker exited during initialization");
                Stop();
                return false;
            }
            FPlatformProcess::SleepNoStats(0.02f);
        }
        OutError = TEXT("Timed out while loading the shared Chatterbox model");
        Stop();
        return false;
#endif
    }

    const FString& GetRuntimeRoot() const { return RuntimeRoot; }

    bool PrepareVoice(const FLocalLLMTextToSpeechConfig& Config, const bool bWarmup, FString& OutError,
        const FLocalTextToSpeechCancelCheck& IsCancelled = {})
    {
        const FString VoiceId = Config.VoiceId.IsEmpty() ? TEXT("default") : Config.VoiceId;
        const FString Reference = ResolveProjectPath(Config.SpeakerReferencePath);
        const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("operation"), TEXT("prepare"));
        Payload->SetStringField(TEXT("voice_id"), VoiceId);
        Payload->SetStringField(TEXT("reference"), Reference);
        Payload->SetNumberField(TEXT("seed"), Config.Seed);
        Payload->SetBoolField(TEXT("warmup"), bWarmup);
        TSharedPtr<FJsonObject> Response;
        return SendRequest(Payload, Response, OutError, IsCancelled, 120.0);
    }

    bool Synthesize(const FLocalLLMTextToSpeechConfig& Config, const FLocalLLMTextToSpeechRequest& Request,
        FLocalTextToSpeechResult& OutResult, FString& OutError,
        const FLocalTextToSpeechCancelCheck& IsCancelled, const FLocalTextToSpeechChunkCallback& OnChunk)
    {
        const FString VoiceId = Request.VoiceId.IsEmpty() ? Config.VoiceId : Request.VoiceId;
        const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("operation"), TEXT("synthesize"));
        Payload->SetStringField(TEXT("voice_id"), VoiceId.IsEmpty() ? TEXT("default") : VoiceId);
        Payload->SetStringField(TEXT("reference"), ResolveProjectPath(Config.SpeakerReferencePath));
        Payload->SetStringField(TEXT("text"), Request.Text.TrimStartAndEnd());
        Payload->SetNumberField(TEXT("seed"), Config.Seed);
        TSharedPtr<FJsonObject> Response;
        if (!SendRequest(Payload, Response, OutError, IsCancelled,
            FMath::Max(30.0, static_cast<double>(Config.MaxGeneratedSeconds) * 4.0))) return false;

        FString PcmPath;
        double SampleRateNumber = 0.0;
        Response->TryGetStringField(TEXT("pcm_path"), PcmPath);
        Response->TryGetNumberField(TEXT("sample_rate"), SampleRateNumber);
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *PcmPath) || Bytes.Num() % sizeof(float) != 0)
        {
            OutError = TEXT("Could not read shared Chatterbox PCM output");
            return false;
        }
        const int32 NumSamples = Bytes.Num() / sizeof(float);
        OutResult.Audio.Samples.SetNumUninitialized(NumSamples);
        FMemory::Memcpy(OutResult.Audio.Samples.GetData(), Bytes.GetData(), Bytes.Num());
        OutResult.Audio.SampleRate = FMath::RoundToInt(SampleRateNumber);
        OutResult.Audio.NumChannels = 1;
        OutResult.VoiceId = VoiceId;
        IFileManager::Get().Delete(*PcmPath, false, true);

        double AudioSeconds = 0.0;
        if (!LocalLLMSpeechTextUtils::ValidateBatchDuration(
            NumSamples, OutResult.Audio.SampleRate, OutResult.Audio.NumChannels,
            Config.MaxGeneratedSeconds, AudioSeconds))
        {
            OutResult.Audio.Samples.Reset();
            OutError = FString::Printf(
                TEXT("Chatterbox generated %.2f seconds of batch audio, exceeding the %.2f second safety limit; no PCM was published"),
                AudioSeconds, Config.MaxGeneratedSeconds);
            return false;
        }

        const int32 ChunkSamples = FMath::Max(1,
            OutResult.Audio.SampleRate * FMath::Max(20, Config.ChunkMilliseconds) / 1000);
        int32 Sequence = 0;
        for (int32 Offset = 0; Offset < NumSamples; Offset += ChunkSamples)
        {
            FLocalLLMAudioChunk Chunk;
            Chunk.SampleRate = OutResult.Audio.SampleRate;
            Chunk.NumChannels = 1;
            Chunk.SequenceNumber = Sequence++;
            Chunk.Samples.Append(OutResult.Audio.Samples.GetData() + Offset,
                FMath::Min(ChunkSamples, NumSamples - Offset));
            if (OnChunk) OnChunk(Chunk);
        }
        return OutResult.Audio.IsValid();
    }

private:
    bool SendRequest(const TSharedRef<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse,
        FString& OutError, const FLocalTextToSpeechCancelCheck& IsCancelled, const double TimeoutSeconds)
    {
        const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
        while (!RequestMutex.TryLock())
        {
            if (IsCancelled && IsCancelled())
            {
                OutError = TEXT("Speech synthesis was cancelled while waiting for the shared Chatterbox service");
                return false;
            }
            if (FPlatformTime::Seconds() >= Deadline)
            {
                OutError = TEXT("Timed out waiting to access the shared Chatterbox service");
                return false;
            }
            if (!bRunning || !Process.IsValid() || !FPlatformProcess::IsProcRunning(Process))
            {
                OutError = TEXT("Shared Chatterbox service stopped while waiting for access");
                return false;
            }
            FPlatformProcess::SleepNoStats(0.005f);
        }
        FScopedCriticalSectionUnlock RequestUnlock(RequestMutex);
        if (!bRunning || !Process.IsValid()) { OutError = TEXT("Shared Chatterbox service is not running"); return false; }
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Payload->SetStringField(TEXT("id"), Id);
        const FString RequestPath = FPaths::Combine(RequestsDirectory, Id + TEXT(".json"));
        const FString ResponsePath = FPaths::Combine(ResponsesDirectory, Id + TEXT(".json"));
        if (!SaveJsonAtomically(RequestPath, Payload)) { OutError = TEXT("Could not queue shared Chatterbox request"); return false; }
        while (FPlatformTime::Seconds() < Deadline)
        {
            if (IsCancelled && IsCancelled())
            {
                IFileManager::Get().Delete(*RequestPath, false, true);
                OutError = TEXT("Speech synthesis was cancelled");
                return false;
            }
            if (FPaths::FileExists(ResponsePath) && LoadJson(ResponsePath, OutResponse)) break;
            if (!FPlatformProcess::IsProcRunning(Process)) { OutError = TEXT("Shared Chatterbox worker exited"); return false; }
            FPlatformProcess::SleepNoStats(0.005f);
        }
        IFileManager::Get().Delete(*ResponsePath, false, true);
        if (!OutResponse.IsValid()) { OutError = TEXT("Timed out waiting for shared Chatterbox service"); return false; }
        bool bOk = false;
        OutResponse->TryGetBoolField(TEXT("ok"), bOk);
        if (!bOk)
        {
            OutResponse->TryGetStringField(TEXT("error"), OutError);
            if (OutError.IsEmpty()) OutError = TEXT("Shared Chatterbox request failed");
        }
        return bOk;
    }

    void Stop()
    {
        bRunning = false;
        if (!Process.IsValid()) return;
        FFileHelper::SaveStringToFile(TEXT("stop"), *FPaths::Combine(InstanceDirectory, TEXT("stop")));
        const double Deadline = FPlatformTime::Seconds() + 2.0;
        while (FPlatformProcess::IsProcRunning(Process) && FPlatformTime::Seconds() < Deadline)
            FPlatformProcess::SleepNoStats(0.01f);
        if (FPlatformProcess::IsProcRunning(Process)) FPlatformProcess::TerminateProc(Process, true);
        FPlatformProcess::CloseProc(Process);
        Process.Reset();
    }

    FCriticalSection RequestMutex;
    FProcHandle Process;
    FString RuntimeRoot;
    FString InstanceDirectory;
    FString RequestsDirectory;
    FString ResponsesDirectory;
    bool bRunning = false;
};

FCriticalSection GChatterboxServiceMutex;
TSharedPtr<FChatterboxTurboSharedService, ESPMode::ThreadSafe> GChatterboxService;
int32 GChatterboxServiceReferences = 0;

TSharedPtr<FChatterboxTurboSharedService, ESPMode::ThreadSafe> AcquireService(
    const FLocalLLMTextToSpeechConfig& Config, FString& OutError)
{
    FScopeLock Lock(&GChatterboxServiceMutex);
    const FString RequestedRoot = ResolveProjectPath(Config.ModelPath);
    if (GChatterboxService.IsValid() && GChatterboxService->GetRuntimeRoot() != RequestedRoot)
    {
        OutError = TEXT("All active Chatterbox voices must share the same ModelPath");
        return {};
    }
    if (!GChatterboxService.IsValid())
    {
        GChatterboxService = MakeShared<FChatterboxTurboSharedService, ESPMode::ThreadSafe>();
        if (!GChatterboxService->Start(Config, OutError)) { GChatterboxService.Reset(); return {}; }
    }
    ++GChatterboxServiceReferences;
    return GChatterboxService;
}

void ReleaseService(TSharedPtr<FChatterboxTurboSharedService, ESPMode::ThreadSafe>& Service)
{
    if (!Service.IsValid()) return;
    FScopeLock Lock(&GChatterboxServiceMutex);
    Service.Reset();
    GChatterboxServiceReferences = FMath::Max(0, GChatterboxServiceReferences - 1);
    if (GChatterboxServiceReferences == 0) GChatterboxService.Reset();
}

class FChatterboxTurboTextToSpeechBackend final : public ILocalTextToSpeechBackend
{
public:
    virtual ~FChatterboxTurboTextToSpeechBackend() override { Unload(); }

    virtual bool Load(const FLocalLLMTextToSpeechConfig& InConfig, FString& OutError) override
    {
        Unload();
        if (!InConfig.bUseGpu) { OutError = TEXT("Chatterbox Turbo requires bUseGpu"); return false; }
        if (InConfig.VoiceId.IsEmpty() || !FPaths::FileExists(ResolveProjectPath(InConfig.SpeakerReferencePath)))
        {
            OutError = TEXT("Chatterbox requires a stable VoiceId and an existing SpeakerReferencePath");
            return false;
        }
        Service = AcquireService(InConfig, OutError);
        if (!Service.IsValid()) return false;
        Config = InConfig;
        if (!Service->PrepareVoice(Config, true, OutError)) { Unload(); return false; }
        bLoaded = true;
        return true;
    }

    virtual void Unload() override
    {
        bLoaded = false;
        ReleaseService(Service);
    }

    virtual bool PrewarmVoice(const FLocalLLMTextToSpeechRequest&, FString& OutError,
        const FLocalTextToSpeechCancelCheck& IsCancelled) override
    {
        if (!bLoaded || !Service.IsValid()) { OutError = TEXT("Chatterbox Turbo is not loaded"); return false; }
        return Service->PrepareVoice(Config, false, OutError, IsCancelled);
    }

    virtual bool Synthesize(const FLocalLLMTextToSpeechRequest& Request, FLocalTextToSpeechResult& OutResult,
        FString& OutError, const FLocalTextToSpeechCancelCheck& IsCancelled,
        const FLocalTextToSpeechChunkCallback& OnChunk) override
    {
        if (!bLoaded || !Service.IsValid()) { OutError = TEXT("Chatterbox Turbo is not loaded"); return false; }
        if (Request.Text.TrimStartAndEnd().IsEmpty()) { OutError = TEXT("Text-to-speech input is empty"); return false; }
        return Service->Synthesize(Config, Request, OutResult, OutError, IsCancelled, OnChunk);
    }

private:
    TSharedPtr<FChatterboxTurboSharedService, ESPMode::ThreadSafe> Service;
    FLocalLLMTextToSpeechConfig Config;
    bool bLoaded = false;
};
}

TUniquePtr<ILocalTextToSpeechBackend> CreateChatterboxTurboTextToSpeechBackend()
{
    return MakeUnique<FChatterboxTurboTextToSpeechBackend>();
}
