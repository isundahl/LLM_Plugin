#include "Inference/InferenceWorker.h"

#include "Backends/ILocalMultimodalBackend.h"
#include "ILocalSpeechToTextBackend.h"
#include "ILocalSpeakerEmbeddingBackend.h"
#include "LocalLLMSpeechVocabulary.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"

FLocalLLMInferenceWorker::FLocalLLMInferenceWorker()
{
    WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
    Backend = CreateLocalLLMBackend(
        ActiveBackend,
        [this](FLocalLLMEvent&& Event) { PushEvent(MoveTemp(Event)); },
        [this]() { return bCancelRequested.Load() || bStopRequested.Load(); });
    Thread = FRunnableThread::Create(this, TEXT("LocalLLMInference"), 0, TPri_BelowNormal);
    check(Thread);
}

FLocalLLMInferenceWorker::~FLocalLLMInferenceWorker()
{
    Stop();
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
    Backend.Reset();
    if (SpeechToTextBackend) SpeechToTextBackend->Unload();
    SpeechToTextBackend.Reset();
    if (SpeakerEmbeddingBackend) SpeakerEmbeddingBackend->Unload();
    SpeakerEmbeddingBackend.Reset();
    if (WakeEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
        WakeEvent = nullptr;
    }
}

void FLocalLLMInferenceWorker::Enqueue(FLocalLLMCommand&& Command)
{
    if (Command.Type == ELocalLLMCommandType::Cancel) bCancelRequested.Store(true);
    else if (Command.Type == ELocalLLMCommandType::SubmitText || Command.Type == ELocalLLMCommandType::SubmitImage || Command.Type == ELocalLLMCommandType::SubmitAudio || Command.Type == ELocalLLMCommandType::TranscribeAudio || Command.Type == ELocalLLMCommandType::CreateSpeakerProfile || Command.Type == ELocalLLMCommandType::VerifySpeaker || Command.Type == ELocalLLMCommandType::EvaluateRelationship)
        bCancelRequested.Store(false);
    Commands.Enqueue(MoveTemp(Command));
    WakeEvent->Trigger();
}

bool FLocalLLMInferenceWorker::DequeueEvent(FLocalLLMEvent& OutEvent) { return Events.Dequeue(OutEvent); }

uint32 FLocalLLMInferenceWorker::Run()
{
    while (!bStopRequested.Load())
    {
        FLocalLLMCommand Command;
        while (Commands.Dequeue(Command))
        {
            ProcessCommand(MoveTemp(Command));
            if (bStopRequested.Load()) break;
        }
        if (!bStopRequested.Load()) WakeEvent->Wait();
    }
    return 0;
}

void FLocalLLMInferenceWorker::Stop()
{
    bStopRequested.Store(true);
    if (WakeEvent) WakeEvent->Trigger();
}

void FLocalLLMInferenceWorker::ProcessCommand(FLocalLLMCommand&& Command)
{
    if (Command.Type == ELocalLLMCommandType::LoadModel && Command.Backend != ActiveBackend)
    {
        Backend.Reset();
        ActiveBackend = Command.Backend;
        Backend = CreateLocalLLMBackend(
            ActiveBackend,
            [this](FLocalLLMEvent&& Event) { PushEvent(MoveTemp(Event)); },
            [this]() { return bCancelRequested.Load() || bStopRequested.Load(); });
        if (bHasSharedWorld) Backend->UpdateWorldContext(SharedWorld, FGuid::NewGuid());
        if (!RegisteredTools.IsEmpty()) Backend->UpdateTools(RegisteredTools, FGuid::NewGuid());
        for (const TPair<FGuid, FLocalLLMCharacterProfile>& Pair : SessionDefinitions)
            Backend->CreateSession(Pair.Key, Pair.Value, FGuid::NewGuid());
    }

    if (Command.Type == ELocalLLMCommandType::CreateSession || Command.Type == ELocalLLMCommandType::UpdateSession)
        SessionDefinitions.Add(Command.SessionId, Command.Character);
    else if (Command.Type == ELocalLLMCommandType::DestroySession)
        SessionDefinitions.Remove(Command.SessionId);
    else if (Command.Type == ELocalLLMCommandType::UpdateWorldContext)
    {
        SharedWorld = Command.World;
        bHasSharedWorld = true;
    }
    else if (Command.Type == ELocalLLMCommandType::UpdateTools)
        RegisteredTools = Command.Tools;

    switch (Command.Type)
    {
    case ELocalLLMCommandType::LoadModel: Backend->LoadModel(Command.ModelConfig, Command.RequestId); break;
    case ELocalLLMCommandType::UnloadModel: Backend->UnloadModel(Command.RequestId); break;
    case ELocalLLMCommandType::CreateSession: Backend->CreateSession(Command.SessionId, Command.Character, Command.RequestId); break;
    case ELocalLLMCommandType::UpdateSession: Backend->UpdateSession(Command.SessionId, Command.Character, Command.RequestId); break;
    case ELocalLLMCommandType::DestroySession: Backend->DestroySession(Command.SessionId, Command.RequestId); break;
    case ELocalLLMCommandType::UpdateWorldContext: Backend->UpdateWorldContext(Command.World, Command.RequestId); break;
    case ELocalLLMCommandType::UpdateTools: Backend->UpdateTools(Command.Tools, Command.RequestId); break;
    case ELocalLLMCommandType::SubmitToolResult: Backend->SubmitToolResult(Command.SessionId, Command.ToolCallId, Command.Text, Command.bToolSuccess, Command.RequestId); break;
    case ELocalLLMCommandType::SubmitText: Backend->SubmitText(Command.SessionId, Command.Text, Command.RequestId); break;
    case ELocalLLMCommandType::SubmitImage: Backend->SubmitImage(Command.SessionId, Command.Image, Command.Text, Command.RequestId); break;
    case ELocalLLMCommandType::SubmitAudio: ProcessAudio(MoveTemp(Command)); break;
    case ELocalLLMCommandType::TranscribeAudio: ProcessTranscription(MoveTemp(Command), false); break;
    case ELocalLLMCommandType::PreloadSpeechToText: ProcessSpeechToTextPreload(MoveTemp(Command)); break;
    case ELocalLLMCommandType::CreateSpeakerProfile: ProcessSpeakerEmbedding(MoveTemp(Command), true); break;
    case ELocalLLMCommandType::VerifySpeaker: ProcessSpeakerEmbedding(MoveTemp(Command), false); break;
    case ELocalLLMCommandType::EvaluateRelationship: Backend->EvaluateRelationship(Command.SessionId, Command.bApplyRelationshipChanges, Command.RequestId); break;
    case ELocalLLMCommandType::SetRelationshipRating: Backend->SetRelationshipRating(Command.SessionId, Command.CriterionName, Command.RelationshipRating, Command.RequestId); break;
    case ELocalLLMCommandType::CompactConversation: Backend->CompactConversation(Command.SessionId, Command.RequestId); break;
    case ELocalLLMCommandType::UndoConversationTurn: Backend->UndoLastConversationTurn(Command.SessionId, Command.RequestId); break;
    case ELocalLLMCommandType::Cancel: Backend->Cancel(Command.RequestId); bCancelRequested.Store(false); break;
    case ELocalLLMCommandType::ResetConversation: Backend->ResetConversation(Command.SessionId, Command.RequestId); break;
    default: checkNoEntry(); break;
    }
}

void FLocalLLMInferenceWorker::ProcessAudio(FLocalLLMCommand&& Command)
{
    const bool bCanUseNative = Backend && Backend->SupportsNativeAudio();
    if (Command.AudioInputStrategy != ELocalLLMAudioInputStrategy::TranscriptionOnly && bCanUseNative)
    {
        Backend->SubmitAudio(Command.SessionId, Command.Audio, Command.Text, Command.RequestId);
        return;
    }

    if (Command.AudioInputStrategy == ELocalLLMAudioInputStrategy::NativeModelOnly)
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = Command.RequestId;
        Event.SessionId = Command.SessionId;
        Event.Text = TEXT("The loaded model has no native audio input support and transcription fallback is disabled by the audio strategy");
        PushEvent(MoveTemp(Event));
        return;
    }

    if (!Command.SpeechToText.IsEnabled())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = Command.RequestId;
        Event.SessionId = Command.SessionId;
        Event.Text = TEXT("The loaded model has no native audio input support and no speech-to-text provider is configured");
        PushEvent(MoveTemp(Event));
        return;
    }

    ProcessTranscription(MoveTemp(Command), true);
}

void FLocalLLMInferenceWorker::ProcessTranscription(FLocalLLMCommand&& Command, const bool bSubmitText)
{
    if (!Command.SpeechToText.IsEnabled())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = Command.RequestId;
        Event.SessionId = Command.SessionId;
        Event.Text = TEXT("No speech-to-text provider is configured");
        PushEvent(MoveTemp(Event));
        return;
    }

    FString LoadError;
    if (!EnsureSpeechToTextLoaded(Command.SpeechToText, LoadError))
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = Command.RequestId;
        Event.SessionId = Command.SessionId;
        Event.SpeechToTextProvider = Command.SpeechToText.Provider;
        Event.Text = TEXT("Could not load speech-to-text provider: ") + LoadError;
        PushEvent(MoveTemp(Event));
        return;
    }

    FLocalLLMEvent Started;
    Started.Type = ELocalLLMEventType::TranscriptionStarted;
    Started.RequestId = Command.RequestId;
    Started.SessionId = Command.SessionId;
    Started.SpeechToTextProvider = Command.SpeechToText.Provider;
    PushEvent(MoveTemp(Started));

    FLocalSpeechToTextResult Result;
    FString Error;
    if (!SpeechToTextBackend->Transcribe(Command.Audio, Result, Error,
        [this]() { return bCancelRequested.Load() || bStopRequested.Load(); }))
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = Command.RequestId;
        Event.SessionId = Command.SessionId;
        Event.SpeechToTextProvider = Command.SpeechToText.Provider;
        Event.Text = Error.IsEmpty() ? TEXT("Speech transcription failed") : MoveTemp(Error);
        PushEvent(MoveTemp(Event));
        return;
    }
    Result.Text.TrimStartAndEndInline();
    if (Result.Text.IsEmpty())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = Command.RequestId;
        Event.SessionId = Command.SessionId;
        Event.SpeechToTextProvider = Command.SpeechToText.Provider;
        Event.Text = TEXT("Speech transcription returned no text");
        PushEvent(MoveTemp(Event));
        return;
    }

    const FLocalLLMTranscriptNormalizationResult Normalized =
        ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
            Result.Text, Command.SpeechVocabulary, Command.ActiveSpeechVocabularyTags);
    if (Normalized.bNeedsConfirmation)
    {
        FLocalLLMEvent Ambiguous;
        Ambiguous.Type = ELocalLLMEventType::TranscriptAmbiguous;
        Ambiguous.RequestId = Command.RequestId;
        Ambiguous.SessionId = Command.SessionId;
        Ambiguous.SpeechToTextProvider = Command.SpeechToText.Provider;
        Ambiguous.Text = Result.Text;
        Ambiguous.TranscriptNormalization = Normalized;
        PushEvent(MoveTemp(Ambiguous));
        return;
    }
    if (!Normalized.Corrections.IsEmpty())
    {
        FLocalLLMEvent NormalizedEvent;
        NormalizedEvent.Type = ELocalLLMEventType::TranscriptNormalized;
        NormalizedEvent.RequestId = Command.RequestId;
        NormalizedEvent.SessionId = Command.SessionId;
        NormalizedEvent.SpeechToTextProvider = Command.SpeechToText.Provider;
        NormalizedEvent.Text = Normalized.CanonicalTranscript;
        NormalizedEvent.TranscriptNormalization = Normalized;
        PushEvent(MoveTemp(NormalizedEvent));
    }

    Result.Text = Normalized.CanonicalTranscript;
    FLocalLLMEvent Completed;
    Completed.Type = bSubmitText ? ELocalLLMEventType::TranscriptionCompleted : ELocalLLMEventType::TranscriptionPartial;
    Completed.RequestId = Command.RequestId;
    Completed.SessionId = Command.SessionId;
    Completed.SpeechToTextProvider = Command.SpeechToText.Provider;
    Completed.Text = Result.Text;
    Completed.TranscriptNormalization = Normalized;
    PushEvent(MoveTemp(Completed));

    if (!bSubmitText) return;

    const FString PlayerText = Command.Text.IsEmpty()
        ? Result.Text
        : FString::Printf(TEXT("%s\n\n[Player speech transcript]\n%s"), *Command.Text, *Result.Text);
    Backend->SubmitText(Command.SessionId, PlayerText, Command.RequestId);
}

bool FLocalLLMInferenceWorker::EnsureSpeechToTextLoaded(
    const FLocalLLMSpeechToTextConfig& Config, FString& OutError)
{
    const bool bProviderChanged = ActiveSpeechToTextProvider != Config.Provider ||
        ActiveSpeechToTextModelPath != Config.ModelPath;
    if (SpeechToTextBackend && !bProviderChanged) return true;

    if (SpeechToTextBackend) SpeechToTextBackend->Unload();
    SpeechToTextBackend = FLocalSpeechToTextBackendRegistry::Create(Config.Provider);
    ActiveSpeechToTextProvider = Config.Provider;
    ActiveSpeechToTextModelPath = Config.ModelPath;
    if (!SpeechToTextBackend)
    {
        OutError = FString::Printf(TEXT("Speech-to-text provider '%s' is not installed"),
            *Config.Provider.ToString());
        return false;
    }
    if (!SpeechToTextBackend->Load(Config, OutError))
    {
        SpeechToTextBackend.Reset();
        return false;
    }
    return true;
}

void FLocalLLMInferenceWorker::ProcessSpeechToTextPreload(FLocalLLMCommand&& Command)
{
    FLocalLLMEvent Event;
    Event.RequestId = Command.RequestId;
    Event.SpeechToTextProvider = Command.SpeechToText.Provider;
    FString Error;
    if (Command.SpeechToText.IsEnabled() && EnsureSpeechToTextLoaded(Command.SpeechToText, Error))
    {
        Event.Type = ELocalLLMEventType::SpeechToTextReady;
        Event.Text = FString::Printf(TEXT("Speech-to-text provider '%s' ready"),
            *Command.SpeechToText.Provider.ToString());
    }
    else
    {
        Event.Type = ELocalLLMEventType::Error;
        Event.Text = Error.IsEmpty() ? TEXT("No speech-to-text provider is configured")
            : TEXT("Could not preload speech-to-text provider: ") + Error;
    }
    PushEvent(MoveTemp(Event));
}

void FLocalLLMInferenceWorker::PushEvent(FLocalLLMEvent&& Event) { Events.Enqueue(MoveTemp(Event)); }

void FLocalLLMInferenceWorker::ProcessSpeakerEmbedding(FLocalLLMCommand&& Command, const bool bCreateProfile)
{
    const bool bProviderChanged = ActiveSpeakerEmbeddingProvider != Command.SpeakerVerification.Provider ||
        ActiveSpeakerEmbeddingModelPath != Command.SpeakerVerification.ModelPath;
    if (!SpeakerEmbeddingBackend || bProviderChanged)
    {
        if (SpeakerEmbeddingBackend) SpeakerEmbeddingBackend->Unload();
        SpeakerEmbeddingBackend = FLocalSpeakerEmbeddingBackendRegistry::Create(Command.SpeakerVerification.Provider);
        ActiveSpeakerEmbeddingProvider = Command.SpeakerVerification.Provider;
        ActiveSpeakerEmbeddingModelPath = Command.SpeakerVerification.ModelPath;
        if (!SpeakerEmbeddingBackend)
        {
            FLocalLLMEvent Error;
            Error.Type = ELocalLLMEventType::Error;
            Error.RequestId = Command.RequestId;
            Error.SessionId = Command.SessionId;
            Error.Text = FString::Printf(TEXT("Speaker embedding provider '%s' is not installed"), *Command.SpeakerVerification.Provider.ToString());
            PushEvent(MoveTemp(Error));
            return;
        }
        FString LoadError;
        if (!SpeakerEmbeddingBackend->Load(Command.SpeakerVerification, LoadError))
        {
            SpeakerEmbeddingBackend.Reset();
            FLocalLLMEvent Error;
            Error.Type = ELocalLLMEventType::Error;
            Error.RequestId = Command.RequestId;
            Error.SessionId = Command.SessionId;
            Error.Text = TEXT("Could not load speaker embedding provider: ") + LoadError;
            PushEvent(MoveTemp(Error));
            return;
        }
    }

    TArray<float> Embedding;
    FString ExtractionError;
    if (!SpeakerEmbeddingBackend->ExtractEmbedding(Command.Audio, Embedding, ExtractionError,
        [this]() { return bCancelRequested.Load() || bStopRequested.Load(); }))
    {
        FLocalLLMEvent Error;
        Error.Type = ELocalLLMEventType::Error;
        Error.RequestId = Command.RequestId;
        Error.SessionId = Command.SessionId;
        Error.Text = ExtractionError.IsEmpty() ? TEXT("Speaker embedding extraction failed") : MoveTemp(ExtractionError);
        PushEvent(MoveTemp(Error));
        return;
    }

    FLocalLLMEvent Event;
    Event.RequestId = Command.RequestId;
    Event.SessionId = Command.SessionId;
    if (bCreateProfile)
    {
        Event.Type = ELocalLLMEventType::SpeakerProfileCreated;
        Event.SpeakerProfile = Command.SpeakerProfile;
        Event.SpeakerProfile.Provider = Command.SpeakerVerification.Provider;
        Event.SpeakerProfile.ModelId = SpeakerEmbeddingBackend->GetModelId();
        Event.SpeakerProfile.Embedding = MoveTemp(Embedding);
        Event.bSpeakerAccepted = true;
        Event.Text = TEXT("Player speaker profile created");
    }
    else
    {
        Event.Type = ELocalLLMEventType::SpeakerVerificationCompleted;
        if (Command.SpeakerProfile.Provider != Command.SpeakerVerification.Provider ||
            Command.SpeakerProfile.ModelId != SpeakerEmbeddingBackend->GetModelId() ||
            Command.SpeakerProfile.Embedding.Num() != Embedding.Num())
        {
            Event.SpeakerSimilarity = -1.0f;
            Event.bSpeakerAccepted = false;
            Event.Text = TEXT("Speaker profile was created with a different embedding model");
        }
        else
        {
            double Similarity = 0.0;
            for (int32 Index = 0; Index < Embedding.Num(); ++Index)
                Similarity += static_cast<double>(Embedding[Index]) * Command.SpeakerProfile.Embedding[Index];
            Event.SpeakerSimilarity = static_cast<float>(Similarity);
            Event.bSpeakerAccepted = Event.SpeakerSimilarity >= Command.SpeakerVerification.SimilarityThreshold;
            Event.Text = FString::Printf(TEXT("Speaker similarity %.3f (threshold %.3f)"),
                Event.SpeakerSimilarity, Command.SpeakerVerification.SimilarityThreshold);
        }
    }
    PushEvent(MoveTemp(Event));
}
