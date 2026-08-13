#include "Speech/NeuTTS2ETextToSpeechBackend.h"

#include "ILocalTextToSpeechBackend.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Speech/LocalLLMSpeechTextUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString ResolveNeuPath(FString Path)
{
    if (!FPaths::IsRelative(Path)) return FPaths::ConvertRelativePathToFull(Path);
    const FString ProjectRoot = FPaths::ConvertRelativePathToFull(
        FPlatformProcess::BaseDir(), FPaths::ProjectDir());
    return FPaths::Combine(ProjectRoot, Path);
}

FString IntegerToEnglishWords(const int64 Value)
{
    static const TCHAR* Small[] =
    {
        TEXT("zero"), TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"),
        TEXT("five"), TEXT("six"), TEXT("seven"), TEXT("eight"), TEXT("nine"),
        TEXT("ten"), TEXT("eleven"), TEXT("twelve"), TEXT("thirteen"), TEXT("fourteen"),
        TEXT("fifteen"), TEXT("sixteen"), TEXT("seventeen"), TEXT("eighteen"), TEXT("nineteen")
    };
    static const TCHAR* Tens[] =
    {
        TEXT(""), TEXT(""), TEXT("twenty"), TEXT("thirty"), TEXT("forty"),
        TEXT("fifty"), TEXT("sixty"), TEXT("seventy"), TEXT("eighty"), TEXT("ninety")
    };

    if (Value < 0) return TEXT("minus ") + IntegerToEnglishWords(-Value);
    if (Value < 20) return Small[Value];
    if (Value < 100)
    {
        const int64 Remainder = Value % 10;
        return Remainder == 0
            ? FString(Tens[Value / 10])
            : FString(Tens[Value / 10]) + TEXT(" ") + IntegerToEnglishWords(Remainder);
    }
    if (Value < 1000)
    {
        const int64 Remainder = Value % 100;
        FString Result = IntegerToEnglishWords(Value / 100) + TEXT(" hundred");
        if (Remainder != 0) Result += TEXT(" ") + IntegerToEnglishWords(Remainder);
        return Result;
    }
    if (Value < 1000000)
    {
        const int64 Remainder = Value % 1000;
        FString Result = IntegerToEnglishWords(Value / 1000) + TEXT(" thousand");
        if (Remainder != 0) Result += TEXT(" ") + IntegerToEnglishWords(Remainder);
        return Result;
    }
    if (Value < 1000000000)
    {
        const int64 Remainder = Value % 1000000;
        FString Result = IntegerToEnglishWords(Value / 1000000) + TEXT(" million");
        if (Remainder != 0) Result += TEXT(" ") + IntegerToEnglishWords(Remainder);
        return Result;
    }
    return FString::Printf(TEXT("%lld"), Value);
}

FString ExpandCurrencyForSpeech(const FString& Input)
{
    static const FRegexPattern CurrencyPattern(TEXT("\\$([0-9][0-9,]*)"));
    FRegexMatcher Matcher(CurrencyPattern, Input);
    FString Result;
    int32 PreviousEnd = 0;
    bool bChanged = false;
    while (Matcher.FindNext())
    {
        const int32 MatchBegin = Matcher.GetMatchBeginning();
        const int32 MatchEnd = Matcher.GetMatchEnding();
        FString Digits = Matcher.GetCaptureGroup(1);
        Digits.ReplaceInline(TEXT(","), TEXT(""));
        if (Digits.Len() > 9) continue;
        const int64 Dollars = FCString::Atoi64(*Digits);

        Result += Input.Mid(PreviousEnd, MatchBegin - PreviousEnd);
        Result += IntegerToEnglishWords(Dollars);
        Result += Dollars == 1 ? TEXT(" dollar") : TEXT(" dollars");
        PreviousEnd = MatchEnd;
        bChanged = true;
    }
    if (!bChanged) return Input;
    Result += Input.Mid(PreviousEnd);
    return Result;
}

bool SaveNeuJsonAtomically(const FString& Path, const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    const FString Temporary = Path + TEXT(".tmp");
    return FJsonSerializer::Serialize(Object, Writer) && FFileHelper::SaveStringToFile(Text, *Temporary) &&
        IFileManager::Get().Move(*Path, *Temporary, true, true, false, true);
}

bool LoadNeuJson(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), OutObject) && OutObject.IsValid();
}

struct FNeuScopedUnlock
{
    explicit FNeuScopedUnlock(FCriticalSection& InLock) : Lock(InLock) {}
    ~FNeuScopedUnlock() { Lock.Unlock(); }
    FCriticalSection& Lock;
};

class FNeuTTS2ESharedService final
{
public:
    ~FNeuTTS2ESharedService() { Stop(); }

    bool Start(const FLocalLLMTextToSpeechConfig& Config, FString& OutError)
    {
#if !PLATFORM_WINDOWS
        OutError = TEXT("The NeuTTS-2E development sidecar is currently configured for Win64 only");
        return false;
#else
        ModelRoot = ResolveNeuPath(Config.ModelPath);
        const FString ResolvedConfig = FPaths::Combine(ModelRoot, TEXT("resolved-config.json"));
        const FString Python = FPaths::Combine(FPaths::GetPath(ModelRoot), TEXT("NeuTTSBenchmark"),
            TEXT("venv"), TEXT("Scripts"), TEXT("python.exe"));
        const FString Worker = FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts"), TEXT("NeuTTS2EWorker.py"));
        if (!FPaths::FileExists(Python) || !FPaths::FileExists(Worker) || !FPaths::FileExists(ResolvedConfig))
        {
            OutError = FString::Printf(
                TEXT("NeuTTS-2E runtime, worker, or resolved model configuration is missing: %s"), *ModelRoot);
            return false;
        }
        InstanceDirectory = FPaths::Combine(ModelRoot, TEXT("runtime"),
            TEXT("shared-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
        RequestsDirectory = FPaths::Combine(InstanceDirectory, TEXT("requests"));
        ResponsesDirectory = FPaths::Combine(InstanceDirectory, TEXT("responses"));
        IFileManager::Get().MakeDirectory(*RequestsDirectory, true);
        IFileManager::Get().MakeDirectory(*ResponsesDirectory, true);
        const FString Arguments = FString::Printf(
            TEXT("\"%s\" --runtime-dir \"%s\" --model-root \"%s\" --seed %d"),
            *Worker, *InstanceDirectory, *ModelRoot, Config.Seed);
        Process = FPlatformProcess::CreateProc(*Python, *Arguments, true, true, true, nullptr, 0,
            *FPaths::ProjectDir(), nullptr, nullptr);
        if (!Process.IsValid())
        {
            OutError = TEXT("Could not launch the shared NeuTTS-2E worker");
            return false;
        }
        const FString ReadyPath = FPaths::Combine(InstanceDirectory, TEXT("ready.json"));
        const double Deadline = FPlatformTime::Seconds() + 120.0;
        while (FPlatformTime::Seconds() < Deadline)
        {
            if (FPaths::FileExists(ReadyPath))
            {
                TSharedPtr<FJsonObject> Ready;
                if (!LoadNeuJson(ReadyPath, Ready))
                {
                    FPlatformProcess::SleepNoStats(0.01f);
                    continue;
                }
                bool bOk = false;
                Ready->TryGetBoolField(TEXT("ok"), bOk);
                if (!bOk)
                {
                    Ready->TryGetStringField(TEXT("error"), OutError);
                    if (OutError.IsEmpty()) OutError = TEXT("NeuTTS-2E worker initialization failed");
                    Stop();
                    return false;
                }
                bRunning = true;
                return true;
            }
            if (!FPlatformProcess::IsProcRunning(Process))
            {
                OutError = TEXT("NeuTTS-2E worker exited during initialization");
                Stop();
                return false;
            }
            FPlatformProcess::SleepNoStats(0.02f);
        }
        OutError = TEXT("Timed out while loading NeuTTS-2E");
        Stop();
        return false;
#endif
    }

    const FString& GetModelRoot() const { return ModelRoot; }

    bool PrepareVoice(const FString& VoiceId, FString& OutError,
        const FLocalTextToSpeechCancelCheck& IsCancelled = {})
    {
        const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("operation"), TEXT("prepare"));
        Payload->SetStringField(TEXT("voice_id"), VoiceId);
        TSharedPtr<FJsonObject> Response;
        return SendRequest(Payload, Response, OutError, IsCancelled, 30.0);
    }

    bool Synthesize(const FLocalLLMTextToSpeechConfig& Config, const FLocalLLMTextToSpeechRequest& Request,
        FLocalTextToSpeechResult& OutResult, FString& OutError,
        const FLocalTextToSpeechCancelCheck& IsCancelled, const FLocalTextToSpeechChunkCallback& OnChunk)
    {
        FString VoiceId = Request.VoiceId.IsEmpty() ? Config.VoiceId : Request.VoiceId;
        VoiceId = VoiceId.ToLower();
        const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("operation"), TEXT("synthesize"));
        Payload->SetStringField(TEXT("voice_id"), VoiceId);
        FString SpokenText = Request.Text.TrimStartAndEnd();
        for (const TPair<FString, FString>& Replacement : Config.SpokenTextReplacements)
        {
            if (!Replacement.Key.IsEmpty() && !Replacement.Value.IsEmpty())
                SpokenText.ReplaceInline(*Replacement.Key, *Replacement.Value, ESearchCase::IgnoreCase);
        }
        // NeuTTS-2E can treat a short clause beginning with the written label
        // "alias" as an unstable pronunciation/language cue. Preserve the displayed
        // dialogue while giving the speech model the natural English expansion.
        SpokenText.ReplaceInline(TEXT(" alias "), TEXT(" also known as "), ESearchCase::IgnoreCase);
        if (SpokenText.StartsWith(TEXT("alias "), ESearchCase::IgnoreCase))
            SpokenText = TEXT("also known as ") + SpokenText.Mid(6);

        // Each queued segment is a fresh autoregressive request. Remove conjunctions
        // that only make sense when attached to a previous clause, and turn an
        // artificial comma boundary into a complete spoken sentence.
        static const TCHAR* LeadingConjunctions[] =
        {
            TEXT("and "), TEXT("but "), TEXT("or "), TEXT("so ")
        };
        for (const TCHAR* Prefix : LeadingConjunctions)
        {
            if (!SpokenText.StartsWith(Prefix, ESearchCase::IgnoreCase)) continue;
            SpokenText.RightChopInline(FCString::Strlen(Prefix), EAllowShrinking::No);
            SpokenText.TrimStartInline();
            if (!SpokenText.IsEmpty()) SpokenText[0] = FChar::ToUpper(SpokenText[0]);
            break;
        }
        if (SpokenText.EndsWith(TEXT(",")) || SpokenText.EndsWith(TEXT(";")) ||
            SpokenText.EndsWith(TEXT(":")))
        {
            SpokenText.LeftChopInline(1, EAllowShrinking::No);
            SpokenText += TEXT(".");
        }
        SpokenText = ExpandCurrencyForSpeech(SpokenText);
        Payload->SetStringField(TEXT("text"), SpokenText);
        Payload->SetStringField(TEXT("emotion"), TEXT("neutral"));
        Payload->SetNumberField(TEXT("temperature"),
            FMath::Clamp(Config.SamplingTemperature, 0.1f, 2.0f));
        Payload->SetNumberField(TEXT("top_k"),
            FMath::Clamp(Config.SamplingTopK, 1, 200));
        TSharedPtr<FJsonObject> Response;
        int32 StreamSequence = 0;
        TArray<float> PendingStreamSamples;
        const int32 StreamChunkSamples = FMath::Max(1,
            24000 * FMath::Max(20, Config.ChunkMilliseconds) / 1000);
        if (!SendRequest(Payload, Response, OutError, IsCancelled,
            FMath::Max(30.0, static_cast<double>(Config.SynthesisTimeoutSeconds)),
            [&OnChunk, &IsCancelled, &PendingStreamSamples, &StreamSequence,
                StreamChunkSamples](const FString& StreamPath)
            {
                TArray<uint8> StreamBytes;
                // The Python worker publishes segments with an atomic rename, but Windows
                // indexing or antivirus can briefly make the visible file unopenable. Use a
                // silent reader and leave the sequence in place so the polling loop retries it.
                TUniquePtr<FArchive> Reader(
                    IFileManager::Get().CreateFileReader(*StreamPath, FILEREAD_Silent));
                const int64 StreamByteCount = Reader ? Reader->TotalSize() : 0;
                if (!Reader || StreamByteCount <= 0 || StreamByteCount > MAX_int32 ||
                    StreamByteCount % sizeof(float) != 0) return false;
                StreamBytes.SetNumUninitialized(static_cast<int32>(StreamByteCount));
                Reader->Serialize(StreamBytes.GetData(), StreamBytes.Num());
                if (Reader->IsError()) return false;
                const int32 NewSampleCount = StreamBytes.Num() / sizeof(float);
                const int32 PreviousSampleCount = PendingStreamSamples.Num();
                PendingStreamSamples.AddUninitialized(NewSampleCount);
                FMemory::Memcpy(PendingStreamSamples.GetData() + PreviousSampleCount,
                    StreamBytes.GetData(), StreamBytes.Num());
                while (PendingStreamSamples.Num() >= StreamChunkSamples)
                {
                    if (IsCancelled && IsCancelled()) return false;
                    FLocalLLMAudioChunk Chunk;
                    Chunk.SampleRate = 24000;
                    Chunk.NumChannels = 1;
                    Chunk.SequenceNumber = StreamSequence++;
                    Chunk.Samples.Append(PendingStreamSamples.GetData(), StreamChunkSamples);
                    PendingStreamSamples.RemoveAt(0, StreamChunkSamples, EAllowShrinking::No);
                    if (OnChunk) OnChunk(Chunk);
                }
                return true;
            })) return false;

        if (!PendingStreamSamples.IsEmpty() && !(IsCancelled && IsCancelled()))
        {
            FLocalLLMAudioChunk FinalStreamChunk;
            FinalStreamChunk.SampleRate = 24000;
            FinalStreamChunk.NumChannels = 1;
            FinalStreamChunk.SequenceNumber = StreamSequence++;
            FinalStreamChunk.Samples = MoveTemp(PendingStreamSamples);
            if (OnChunk) OnChunk(FinalStreamChunk);
        }

        FString PcmPath;
        double SampleRateNumber = 0.0;
        Response->TryGetStringField(TEXT("pcm_path"), PcmPath);
        Response->TryGetNumberField(TEXT("sample_rate"), SampleRateNumber);
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *PcmPath) || Bytes.Num() % sizeof(float) != 0)
        {
            OutError = TEXT("Could not read NeuTTS-2E PCM output");
            return false;
        }
        OutResult.Audio.Samples.SetNumUninitialized(Bytes.Num() / sizeof(float));
        FMemory::Memcpy(OutResult.Audio.Samples.GetData(), Bytes.GetData(), Bytes.Num());
        OutResult.Audio.SampleRate = FMath::RoundToInt(SampleRateNumber);
        OutResult.Audio.NumChannels = 1;
        OutResult.VoiceId = VoiceId;
        IFileManager::Get().Delete(*PcmPath, false, true);

        double AudioSeconds = 0.0;
        if (!LocalLLMSpeechTextUtils::ValidateBatchDuration(OutResult.Audio.Samples.Num(),
            OutResult.Audio.SampleRate, 1, Config.MaxGeneratedSeconds, AudioSeconds))
        {
            OutResult.Audio.Samples.Reset();
            OutError = FString::Printf(TEXT(
                "NeuTTS-2E generated %.2f seconds of audio, exceeding the %.2f second safety limit"),
                AudioSeconds, Config.MaxGeneratedSeconds);
            return false;
        }
        if (StreamSequence == 0)
        {
            const int32 ChunkSamples = FMath::Max(1,
                OutResult.Audio.SampleRate * FMath::Max(20, Config.ChunkMilliseconds) / 1000);
            int32 Sequence = 0;
            for (int32 Offset = 0; Offset < OutResult.Audio.Samples.Num(); Offset += ChunkSamples)
            {
                FLocalLLMAudioChunk Chunk;
                Chunk.SampleRate = OutResult.Audio.SampleRate;
                Chunk.NumChannels = 1;
                Chunk.SequenceNumber = Sequence++;
                Chunk.Samples.Append(OutResult.Audio.Samples.GetData() + Offset,
                    FMath::Min(ChunkSamples, OutResult.Audio.Samples.Num() - Offset));
                if (OnChunk) OnChunk(Chunk);
            }
        }
        return OutResult.Audio.IsValid();
    }

private:
    bool SendRequest(const TSharedRef<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse,
        FString& OutError, const FLocalTextToSpeechCancelCheck& IsCancelled, const double TimeoutSeconds,
        const TFunction<bool(const FString&)>& OnStreamFile = {})
    {
        const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
        while (!RequestMutex.TryLock())
        {
            if (IsCancelled && IsCancelled())
            {
                OutError = TEXT("Speech synthesis was cancelled while waiting for NeuTTS-2E");
                return false;
            }
            if (FPlatformTime::Seconds() >= Deadline)
            {
                OutError = TEXT("Timed out waiting to access NeuTTS-2E");
                return false;
            }
            if (!bRunning || !Process.IsValid() || !FPlatformProcess::IsProcRunning(Process))
            {
                OutError = TEXT("NeuTTS-2E stopped while waiting for access");
                return false;
            }
            FPlatformProcess::SleepNoStats(0.005f);
        }
        FNeuScopedUnlock RequestUnlock(RequestMutex);
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Payload->SetStringField(TEXT("id"), Id);
        const FString RequestPath = FPaths::Combine(RequestsDirectory, Id + TEXT(".json"));
        const FString ResponsePath = FPaths::Combine(ResponsesDirectory, Id + TEXT(".json"));
        if (!SaveNeuJsonAtomically(RequestPath, Payload))
        {
            OutError = TEXT("Could not queue NeuTTS-2E request");
            return false;
        }
        int32 NextStreamSequence = 0;
        const auto ConsumeReadyStreamFiles = [&]()
        {
            if (!OnStreamFile) return;
            while (true)
            {
                const FString StreamPath = FPaths::Combine(InstanceDirectory, TEXT("audio"),
                    FString::Printf(TEXT("%s-%06d.f32"), *Id, NextStreamSequence));
                if (!FPaths::FileExists(StreamPath)) break;
                // File visibility can precede successful reads on Windows. Never delete or
                // advance past a stream segment until its PCM has actually been consumed.
                if (!OnStreamFile(StreamPath)) break;
                IFileManager::Get().Delete(*StreamPath, false, true);
                ++NextStreamSequence;
            }
        };
        while (FPlatformTime::Seconds() < Deadline)
        {
            if (IsCancelled && IsCancelled())
            {
                IFileManager::Get().Delete(*RequestPath, false, true);
                OutError = TEXT("Speech synthesis was cancelled");
                return false;
            }
            ConsumeReadyStreamFiles();
            if (FPaths::FileExists(ResponsePath) && LoadNeuJson(ResponsePath, OutResponse))
            {
                double StreamChunkCountNumber = 0.0;
                OutResponse->TryGetNumberField(TEXT("stream_chunk_count"), StreamChunkCountNumber);
                const int32 ExpectedStreamChunkCount =
                    FMath::Max(0, FMath::RoundToInt(StreamChunkCountNumber));
                const double StreamDrainDeadline =
                    FMath::Min(Deadline, FPlatformTime::Seconds() + 1.0);
                do
                {
                    ConsumeReadyStreamFiles();
                    if (NextStreamSequence >= ExpectedStreamChunkCount) break;
                    FPlatformProcess::SleepNoStats(0.002f);
                }
                while (FPlatformTime::Seconds() < StreamDrainDeadline);

                if (OnStreamFile && NextStreamSequence < ExpectedStreamChunkCount)
                {
                    OutError = FString::Printf(
                        TEXT("NeuTTS-2E stream was incomplete (%d/%d PCM segments consumed)"),
                        NextStreamSequence, ExpectedStreamChunkCount);
                    return false;
                }
                break;
            }
            if (!FPlatformProcess::IsProcRunning(Process))
            {
                OutError = TEXT("NeuTTS-2E worker exited");
                return false;
            }
            FPlatformProcess::SleepNoStats(0.005f);
        }
        IFileManager::Get().Delete(*ResponsePath, false, true);
        if (!OutResponse.IsValid())
        {
            OutError = TEXT("Timed out waiting for NeuTTS-2E");
            return false;
        }
        bool bOk = false;
        OutResponse->TryGetBoolField(TEXT("ok"), bOk);
        if (!bOk)
        {
            OutResponse->TryGetStringField(TEXT("error"), OutError);
            if (OutError.IsEmpty()) OutError = TEXT("NeuTTS-2E request failed");
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
    FString ModelRoot;
    FString InstanceDirectory;
    FString RequestsDirectory;
    FString ResponsesDirectory;
    bool bRunning = false;
};

FCriticalSection GNeuServiceMutex;
TSharedPtr<FNeuTTS2ESharedService, ESPMode::ThreadSafe> GNeuService;
int32 GNeuServiceReferences = 0;

TSharedPtr<FNeuTTS2ESharedService, ESPMode::ThreadSafe> AcquireNeuService(
    const FLocalLLMTextToSpeechConfig& Config, FString& OutError)
{
    FScopeLock Lock(&GNeuServiceMutex);
    const FString RequestedRoot = ResolveNeuPath(Config.ModelPath);
    if (GNeuService.IsValid() && GNeuService->GetModelRoot() != RequestedRoot)
    {
        OutError = TEXT("All active NeuTTS-2E voices must share the same ModelPath");
        return {};
    }
    if (!GNeuService.IsValid())
    {
        GNeuService = MakeShared<FNeuTTS2ESharedService, ESPMode::ThreadSafe>();
        if (!GNeuService->Start(Config, OutError))
        {
            GNeuService.Reset();
            return {};
        }
    }
    ++GNeuServiceReferences;
    return GNeuService;
}

void ReleaseNeuService(TSharedPtr<FNeuTTS2ESharedService, ESPMode::ThreadSafe>& Service)
{
    if (!Service.IsValid()) return;
    FScopeLock Lock(&GNeuServiceMutex);
    Service.Reset();
    GNeuServiceReferences = FMath::Max(0, GNeuServiceReferences - 1);
    if (GNeuServiceReferences == 0) GNeuService.Reset();
}

class FNeuTTS2ETextToSpeechBackend final : public ILocalTextToSpeechBackend
{
public:
    virtual ~FNeuTTS2ETextToSpeechBackend() override { Unload(); }

    virtual bool Load(const FLocalLLMTextToSpeechConfig& InConfig, FString& OutError) override
    {
        Unload();
        static const TSet<FString> Speakers = {TEXT("emily"), TEXT("paul"), TEXT("sophie"), TEXT("steven")};
        const FString Voice = InConfig.VoiceId.ToLower();
        if (!Speakers.Contains(Voice))
        {
            OutError = TEXT("NeuTTS-2E VoiceId must be emily, paul, sophie, or steven");
            return false;
        }
        Service = AcquireNeuService(InConfig, OutError);
        if (!Service.IsValid()) return false;
        Config = InConfig;
        Config.VoiceId = Voice;
        if (!Service->PrepareVoice(Voice, OutError))
        {
            Unload();
            return false;
        }
        bLoaded = true;
        return true;
    }

    virtual void Unload() override
    {
        bLoaded = false;
        ReleaseNeuService(Service);
    }

    virtual bool PrewarmVoice(const FLocalLLMTextToSpeechRequest&, FString& OutError,
        const FLocalTextToSpeechCancelCheck& IsCancelled) override
    {
        if (!bLoaded || !Service.IsValid())
        {
            OutError = TEXT("NeuTTS-2E is not loaded");
            return false;
        }
        return Service->PrepareVoice(Config.VoiceId, OutError, IsCancelled);
    }

    virtual bool Synthesize(const FLocalLLMTextToSpeechRequest& Request, FLocalTextToSpeechResult& OutResult,
        FString& OutError, const FLocalTextToSpeechCancelCheck& IsCancelled,
        const FLocalTextToSpeechChunkCallback& OnChunk) override
    {
        if (!bLoaded || !Service.IsValid())
        {
            OutError = TEXT("NeuTTS-2E is not loaded");
            return false;
        }
        if (Request.Text.TrimStartAndEnd().IsEmpty())
        {
            OutError = TEXT("Text-to-speech input is empty");
            return false;
        }
        return Service->Synthesize(Config, Request, OutResult, OutError, IsCancelled, OnChunk);
    }

private:
    TSharedPtr<FNeuTTS2ESharedService, ESPMode::ThreadSafe> Service;
    FLocalLLMTextToSpeechConfig Config;
    bool bLoaded = false;
};
}

TUniquePtr<ILocalTextToSpeechBackend> CreateNeuTTS2ETextToSpeechBackend()
{
    return MakeUnique<FNeuTTS2ETextToSpeechBackend>();
}
