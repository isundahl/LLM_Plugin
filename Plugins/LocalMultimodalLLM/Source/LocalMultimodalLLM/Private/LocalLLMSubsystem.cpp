#include "LocalLLMSubsystem.h"

#include "Inference/InferenceWorker.h"
#include "LocalLLMCharacterSheet.h"
#include "LocalLLMSettings.h"
#include "LocalLLMSpeechVocabulary.h"
#include "Models/LocalLLMModelRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FLocalLLMInferenceWorkerDeleter::operator()(FLocalLLMInferenceWorker* Worker) const
{
    delete Worker;
}

namespace
{
bool IsCharacterDevelopedCategory(const ELocalLLMDynamicLoreCategory Category)
{
    return Category == ELocalLLMDynamicLoreCategory::PersonalPreference ||
        Category == ELocalLLMDynamicLoreCategory::PersonalHabit ||
        Category == ELocalLLMDynamicLoreCategory::PersonalOpinion;
}

bool ParseCharacterDevelopedCategory(const FString& Value, ELocalLLMDynamicLoreCategory& OutCategory)
{
    if (Value.Equals(TEXT("PersonalPreference"), ESearchCase::IgnoreCase))
        OutCategory = ELocalLLMDynamicLoreCategory::PersonalPreference;
    else if (Value.Equals(TEXT("PersonalHabit"), ESearchCase::IgnoreCase))
        OutCategory = ELocalLLMDynamicLoreCategory::PersonalHabit;
    else if (Value.Equals(TEXT("PersonalOpinion"), ESearchCase::IgnoreCase))
        OutCategory = ELocalLLMDynamicLoreCategory::PersonalOpinion;
    else
        return false;
    return true;
}

void NormalizeRelationshipSettings(FLocalLLMRelationshipEvaluationSettings& Settings)
{
    Settings.MaxAbsoluteDelta = FMath::Clamp(Settings.MaxAbsoluteDelta, 1, 2);
    Settings.MinimumConfidence = FMath::Clamp(Settings.MinimumConfidence, 1, 2);
    Settings.MaxConversationTurns = FMath::Clamp(Settings.MaxConversationTurns, 1, 12);
    if (Settings.Criteria.Num() > 3) Settings.Criteria.SetNum(3);
    TSet<FName> Names;
    for (int32 Index = Settings.Criteria.Num() - 1; Index >= 0; --Index)
    {
        FLocalLLMRelationshipCriterion& Criterion = Settings.Criteria[Index];
        Criterion.Rating = FMath::Clamp(Criterion.Rating, 0, 10);
        if (Criterion.Name.IsNone() || Names.Contains(Criterion.Name)) Settings.Criteria.RemoveAt(Index);
        else Names.Add(Criterion.Name);
    }
}

void ApplyProjectGenerationSettings(FLocalLLMModelConfig& Config)
{
    const ULocalLLMSettings* Settings = GetDefault<ULocalLLMSettings>();
    Config.Generation.MaxTokens = FMath::Max(1, Settings->MaxGeneratedTokens);
    Config.Generation.ReasoningMode = Settings->ReasoningMode;
}
}

void ULocalLLMSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Worker.Reset(new FLocalLLMInferenceWorker());
    FLocalLLMCharacterProfile DefaultCharacter;
    DefaultCharacter.CharacterId = TEXT("default");
    DefaultCharacter.DisplayName = TEXT("Assistant");
    DefaultCharacter.JailbreakGuard.Mode = ELocalLLMJailbreakGuardMode::Off;
    DefaultCharacter.ImmersionGuard.Mode = ELocalLLMImmersionGuardMode::Off;
    DefaultSessionId = CreateCharacterSessionFromProfile(DefaultCharacter);
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &ULocalLLMSubsystem::Tick), 0.01f);
}

void ULocalLLMSubsystem::Deinitialize()
{
    if (TickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }
    Worker.Reset();
    Sessions.Reset();
    SessionProfiles.Reset();
    RelationshipStates.Reset();
    bModelLoaded = false;
    Super::Deinitialize();
}

FGuid ULocalLLMSubsystem::LoadModel(const FString& ModelPath)
{
    const FGuid RequestId = FGuid::NewGuid();
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::LoadModel;
    Command.Backend = GetDefault<ULocalLLMSettings>()->Backend;
    Command.RequestId = RequestId;
    if (ModelPath.EndsWith(TEXT(".localllm.json"), ESearchCase::IgnoreCase))
    {
        FLocalLLMModelInfo Info;
        FLocalLLMModelRegistry::LoadManifest(ModelPath, Info);
        if (!Info.bCompatible)
        {
            FLocalLLMEvent Event;
            Event.Type = ELocalLLMEventType::Error;
            Event.RequestId = RequestId;
            Event.Text = Info.Status;
            DispatchEvent(Event);
            return RequestId;
        }
        Command.ModelConfig = MoveTemp(Info.Config);
    }
    else
    {
        Command.ModelConfig = FLocalLLMModelRegistry::MakeLegacyConfig(ModelPath);
    }
    ApplyProjectGenerationSettings(Command.ModelConfig);
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

FGuid ULocalLLMSubsystem::LoadModelById(const FString& ModelId)
{
    const FGuid RequestId = FGuid::NewGuid();
    FLocalLLMModelInfo Info;
    if (!FLocalLLMModelRegistry::FindById(ModelId, Info) || !Info.bCompatible)
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = RequestId;
        Event.Text = Info.Status;
        DispatchEvent(Event);
        return RequestId;
    }

    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::LoadModel;
    Command.Backend = GetDefault<ULocalLLMSettings>()->Backend;
    Command.RequestId = RequestId;
    Command.ModelConfig = MoveTemp(Info.Config);
    ApplyProjectGenerationSettings(Command.ModelConfig);
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

TArray<FLocalLLMModelInfo> ULocalLLMSubsystem::GetAvailableModels() const
{
    return FLocalLLMModelRegistry::Discover();
}

FGuid ULocalLLMSubsystem::LoadDefaultModel()
{
    const ULocalLLMSettings* Settings = GetDefault<ULocalLLMSettings>();
    return Settings->DefaultModelId.IsEmpty()
        ? LoadModel(Settings->DefaultModelPath.FilePath)
        : LoadModelById(Settings->DefaultModelId);
}

FGuid ULocalLLMSubsystem::UnloadModel()
{
    const FGuid RequestId = FGuid::NewGuid();
    EnqueueCommand(static_cast<uint8>(ELocalLLMCommandType::UnloadModel), RequestId);
    return RequestId;
}

FGuid ULocalLLMSubsystem::SubmitText(const FString& Prompt)
{
    return SubmitTextForSession(DefaultSessionId, Prompt);
}

FGuid ULocalLLMSubsystem::CreateCharacterSession(ULocalLLMCharacterSheet* CharacterSheet)
{
    return CharacterSheet ? CreateCharacterSessionFromProfile(CharacterSheet->Character) : FGuid();
}

FGuid ULocalLLMSubsystem::CreateCharacterSessionFromProfile(const FLocalLLMCharacterProfile& Character)
{
    if (!Worker) return {};
    const FGuid SessionId = FGuid::NewGuid();
    const FGuid RequestId = FGuid::NewGuid();
    FLocalLLMSessionInfo Info;
    Info.SessionId = SessionId;
    Info.CharacterId = Character.CharacterId.IsNone() ? FName(*Character.DisplayName) : Character.CharacterId;
    Info.DisplayName = Character.DisplayName;
    Sessions.Add(SessionId, Info);
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::CreateSession;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.Character = Character;
    Command.Character.CharacterId = Info.CharacterId;
    NormalizeRelationshipSettings(Command.Character.RelationshipEvaluation);
    RelationshipStates.Add(SessionId, Command.Character.RelationshipEvaluation);
    SessionProfiles.Add(SessionId, Command.Character);
    Worker->Enqueue(MoveTemp(Command));
    return SessionId;
}

bool ULocalLLMSubsystem::UpdateCharacterSession(const FGuid& SessionId, const FLocalLLMCharacterProfile& Character)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return false;
    FLocalLLMSessionInfo& Info = Sessions.FindChecked(SessionId);
    Info.CharacterId = Character.CharacterId.IsNone() ? Info.CharacterId : Character.CharacterId;
    Info.DisplayName = Character.DisplayName;
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::UpdateSession;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.Character = Character;
    Command.Character.CharacterId = Info.CharacterId;
    NormalizeRelationshipSettings(Command.Character.RelationshipEvaluation);
    RelationshipStates.Add(SessionId, Command.Character.RelationshipEvaluation);
    SessionProfiles.Add(SessionId, Command.Character);
    Worker->Enqueue(MoveTemp(Command));
    return true;
}

bool ULocalLLMSubsystem::DestroyCharacterSession(const FGuid& SessionId)
{
    if (SessionId == DefaultSessionId) return false;
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return false;
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::DestroySession;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Worker->Enqueue(MoveTemp(Command));
    Sessions.Remove(SessionId);
    SessionProfiles.Remove(SessionId);
    RelationshipStates.Remove(SessionId);
    return true;
}

FGuid ULocalLLMSubsystem::SubmitTextForSession(const FGuid& SessionId, const FString& Prompt)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    EnqueueCommand(static_cast<uint8>(ELocalLLMCommandType::SubmitText), RequestId, SessionId, Prompt);
    return RequestId;
}

FGuid ULocalLLMSubsystem::SubmitImage(const FLocalLLMImageInput& Image, const FString& Prompt)
{
    return SubmitImageForSession(DefaultSessionId, Image, Prompt);
}

FGuid ULocalLLMSubsystem::SubmitImageForSession(const FGuid& SessionId, const FLocalLLMImageInput& Image, const FString& Prompt)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    if (!Image.IsValid())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = RequestId;
        Event.Text = TEXT("Image input must contain exactly Width * Height * 3 RGB bytes");
        DispatchEvent(Event);
        return RequestId;
    }

    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::SubmitImage;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.Text = Prompt;
    Command.Image = Image;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

FGuid ULocalLLMSubsystem::SubmitAudio(const FLocalLLMAudioInput& Audio, const FString& Prompt)
{
    return SubmitAudioForSession(DefaultSessionId, Audio, Prompt);
}

FGuid ULocalLLMSubsystem::SubmitAudioForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio, const FString& Prompt)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    if (!Audio.IsValid())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = RequestId;
        Event.Text = TEXT("Audio input must contain samples and have a positive sample rate and channel count");
        DispatchEvent(Event);
        return RequestId;
    }

    const ULocalLLMSettings* Settings = GetDefault<ULocalLLMSettings>();
    const double Duration = static_cast<double>(Audio.Samples.Num()) / static_cast<double>(Audio.SampleRate * Audio.NumChannels);
    if (Duration > Settings->MaxAudioSeconds)
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = RequestId;
        Event.Text = FString::Printf(TEXT("Audio input is %.2f seconds; the configured limit is %.2f seconds"), Duration, Settings->MaxAudioSeconds);
        DispatchEvent(Event);
        return RequestId;
    }

    EnqueueCommand(static_cast<uint8>(ELocalLLMCommandType::SubmitAudio), RequestId, SessionId, Prompt, Audio,
        Settings->AudioInputStrategy, Settings->SpeechToText);
    return RequestId;
}

FGuid ULocalLLMSubsystem::TranscribeAudio(const FLocalLLMAudioInput& Audio)
{
    return TranscribeAudioForSession(DefaultSessionId, Audio);
}

FGuid ULocalLLMSubsystem::PreloadSpeechToText()
{
    const ULocalLLMSettings* Settings = GetDefault<ULocalLLMSettings>();
    const FGuid RequestId = FGuid::NewGuid();
    if (!Worker || !Settings->SpeechToText.IsEnabled())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = RequestId;
        Event.Text = TEXT("No speech-to-text provider is configured");
        DispatchEvent(Event);
        return RequestId;
    }
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::PreloadSpeechToText;
    Command.RequestId = RequestId;
    Command.SpeechToText = Settings->SpeechToText;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

FGuid ULocalLLMSubsystem::TranscribeAudioForSession(const FGuid& SessionId, const FLocalLLMAudioInput& Audio)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    if (!Audio.IsValid())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = RequestId;
        Event.SessionId = SessionId;
        Event.Text = TEXT("Audio input must contain samples and have a positive sample rate and channel count");
        DispatchEvent(Event);
        return RequestId;
    }
    const ULocalLLMSettings* Settings = GetDefault<ULocalLLMSettings>();
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::TranscribeAudio;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.Audio = Audio;
    Command.AudioInputStrategy = ELocalLLMAudioInputStrategy::TranscriptionOnly;
    Command.SpeechToText = Settings->SpeechToText;
    Command.SpeechVocabulary = SpeechVocabularyEntries;
    Command.ActiveSpeechVocabularyTags = ActiveSpeechVocabularyTags;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

void ULocalLLMSubsystem::SetSpeechVocabulary(ULocalLLMSpeechVocabulary* Vocabulary)
{
    SpeechVocabularyEntries = Vocabulary ? Vocabulary->Entries : TArray<FLocalLLMSpeechVocabularyEntry>();
}

void ULocalLLMSubsystem::SetSpeechVocabularyEntries(const TArray<FLocalLLMSpeechVocabularyEntry>& Entries)
{
    SpeechVocabularyEntries = Entries;
}

void ULocalLLMSubsystem::AddSpeechVocabularyEntry(const FLocalLLMSpeechVocabularyEntry& Entry)
{
    SpeechVocabularyEntries.Add(Entry);
}

void ULocalLLMSubsystem::ClearSpeechVocabulary()
{
    SpeechVocabularyEntries.Reset();
}

void ULocalLLMSubsystem::SetActiveSpeechVocabularyTags(const TArray<FName>& ActiveTags)
{
    ActiveSpeechVocabularyTags = ActiveTags;
}

FLocalLLMTranscriptNormalizationResult ULocalLLMSubsystem::NormalizeTranscript(const FString& Transcript) const
{
    return ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        Transcript, SpeechVocabularyEntries, ActiveSpeechVocabularyTags);
}

FGuid ULocalLLMSubsystem::CreateSpeakerProfileForSession(
    const FGuid& SessionId, const FLocalLLMAudioInput& Audio,
    const FLocalLLMSpeakerVerificationConfig& Config, const FString& DisplayName)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    if (!Audio.IsValid())
    {
        FLocalLLMEvent Error;
        Error.Type = ELocalLLMEventType::Error;
        Error.RequestId = RequestId;
        Error.SessionId = SessionId;
        Error.Text = TEXT("Speaker enrollment audio is invalid");
        DispatchEvent(Error);
        return RequestId;
    }
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::CreateSpeakerProfile;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.Audio = Audio;
    Command.SpeakerVerification = Config;
    Command.SpeakerProfile.DisplayName = DisplayName.IsEmpty() ? TEXT("Player") : DisplayName;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

FGuid ULocalLLMSubsystem::VerifySpeakerForSession(
    const FGuid& SessionId, const FLocalLLMAudioInput& Audio,
    const FLocalLLMSpeakerProfile& Profile, const FLocalLLMSpeakerVerificationConfig& Config)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    if (!Audio.IsValid() || !Profile.IsValid())
    {
        FLocalLLMEvent Error;
        Error.Type = ELocalLLMEventType::Error;
        Error.RequestId = RequestId;
        Error.SessionId = SessionId;
        Error.Text = TEXT("Speaker verification requires valid audio and an enrolled profile");
        DispatchEvent(Error);
        return RequestId;
    }
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::VerifySpeaker;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.Audio = Audio;
    Command.SpeakerVerification = Config;
    Command.SpeakerProfile = Profile;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

FGuid ULocalLLMSubsystem::SetSharedWorldContext(const FLocalLLMWorldContext& World)
{
    SharedWorld = World;
    const FGuid RequestId = FGuid::NewGuid();
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::UpdateWorldContext;
    Command.RequestId = RequestId;
    Command.World = World;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

FGuid ULocalLLMSubsystem::SetSharedWorldFromSheet(ULocalLLMWorldSheet* WorldSheet)
{
    return WorldSheet ? SetSharedWorldContext(WorldSheet->World) : FGuid();
}

bool ULocalLLMSubsystem::UpsertDynamicLoreFact(FLocalLLMDynamicLoreFact Fact)
{
    Fact.Value = Fact.Value.TrimStartAndEnd();
    if (!Worker || Fact.Key.IsNone() || Fact.Value.IsEmpty()) return false;
    if (Fact.Key.ToString().Len() > 64 || Fact.Value.Len() > 512) return false;
    if (Fact.Scope == ELocalLLMDynamicLoreScope::CharacterPrivate && Fact.TargetCharacterId.IsNone()) return false;
    if (Fact.Scope == ELocalLLMDynamicLoreScope::Area && Fact.AreaId.IsNone()) return false;
    if (Fact.Source == ELocalLLMDynamicLoreSource::Character &&
        (!IsCharacterDevelopedCategory(Fact.Category) ||
            Fact.Scope != ELocalLLMDynamicLoreScope::CharacterPrivate ||
            Fact.SubjectCharacterId.IsNone() ||
            !Fact.SubjectCharacterId.IsEqual(Fact.TargetCharacterId)))
        return false;

    int32 ExistingIndex = INDEX_NONE;
    if (Fact.FactId.IsValid())
        ExistingIndex = SharedWorld.DynamicLore.IndexOfByPredicate(
            [&Fact](const FLocalLLMDynamicLoreFact& Existing) { return Existing.FactId == Fact.FactId; });
    if (ExistingIndex == INDEX_NONE)
        ExistingIndex = SharedWorld.DynamicLore.IndexOfByPredicate(
            [&Fact](const FLocalLLMDynamicLoreFact& Existing)
            {
                return Existing.Category == Fact.Category && Existing.Scope == Fact.Scope &&
                    Existing.SubjectCharacterId.IsEqual(Fact.SubjectCharacterId) &&
                    Existing.TargetCharacterId.IsEqual(Fact.TargetCharacterId) &&
                    Existing.AreaId.IsEqual(Fact.AreaId) && Existing.Key.IsEqual(Fact.Key);
            });
    if (ExistingIndex == INDEX_NONE && SharedWorld.DynamicLore.Num() >= 256) return false;

    Fact.FactId = ExistingIndex == INDEX_NONE ? FGuid::NewGuid() : SharedWorld.DynamicLore[ExistingIndex].FactId;
    Fact.Revision = ExistingIndex == INDEX_NONE ? 1 : SharedWorld.DynamicLore[ExistingIndex].Revision + 1;
    if (ExistingIndex == INDEX_NONE) SharedWorld.DynamicLore.Add(MoveTemp(Fact));
    else SharedWorld.DynamicLore[ExistingIndex] = MoveTemp(Fact);
    ++SharedWorld.Revision;
    SetSharedWorldContext(SharedWorld);
    return true;
}

bool ULocalLLMSubsystem::RemoveDynamicLoreFact(const FGuid& FactId)
{
    if (!FactId.IsValid()) return false;
    const int32 Removed = SharedWorld.DynamicLore.RemoveAll(
        [&FactId](const FLocalLLMDynamicLoreFact& Fact) { return Fact.FactId == FactId; });
    if (Removed == 0) return false;
    ++SharedWorld.Revision;
    SetSharedWorldContext(SharedWorld);
    return true;
}

void ULocalLLMSubsystem::ClearDynamicLoreFacts()
{
    if (SharedWorld.DynamicLore.IsEmpty()) return;
    SharedWorld.DynamicLore.Reset();
    ++SharedWorld.Revision;
    SetSharedWorldContext(SharedWorld);
}

TArray<FLocalLLMDynamicLoreFact> ULocalLLMSubsystem::GetVisibleDynamicLoreFactsForSession(
    const FGuid& SessionId) const
{
    TArray<FLocalLLMDynamicLoreFact> Result;
    const FLocalLLMCharacterProfile* Profile = SessionProfiles.Find(SessionId);
    if (!Profile) return Result;
    for (const FLocalLLMDynamicLoreFact& Fact : SharedWorld.DynamicLore)
    {
        const bool bVisible =
            (Fact.Scope == ELocalLLMDynamicLoreScope::Global) ||
            (Fact.Scope == ELocalLLMDynamicLoreScope::CharacterPrivate &&
                Fact.TargetCharacterId.IsEqual(Profile->CharacterId)) ||
            (Fact.Scope == ELocalLLMDynamicLoreScope::Area &&
                !Fact.AreaId.IsNone() && Profile->ActiveKnowledgeAreas.Contains(Fact.AreaId));
        if (bVisible) Result.Add(Fact);
    }
    return Result;
}

bool ULocalLLMSubsystem::CommitCharacterDevelopedFact(
    const FGuid& SessionId, const ELocalLLMDynamicLoreCategory Category,
    const FName Key, const FString& Value)
{
    const FLocalLLMCharacterProfile* Profile = SessionProfiles.Find(SessionId);
    if (!Profile || !Profile->DevelopedCanon.bEnableCharacterProposals ||
        !IsCharacterDevelopedCategory(Category) || Key.IsNone())
        return false;
    const FString CleanValue = Value.TrimStartAndEnd();
    if (CleanValue.IsEmpty() || CleanValue.Len() > Profile->DevelopedCanon.MaxFactCharacters) return false;

    int32 ExistingCharacterFacts = 0;
    for (const FLocalLLMDynamicLoreFact& Existing : SharedWorld.DynamicLore)
        if (Existing.Source == ELocalLLMDynamicLoreSource::Character &&
            Existing.TargetCharacterId.IsEqual(Profile->CharacterId))
            ++ExistingCharacterFacts;
    const bool bReplacing = SharedWorld.DynamicLore.ContainsByPredicate(
        [Profile, Category, Key](const FLocalLLMDynamicLoreFact& Fact)
        {
            return Fact.Source == ELocalLLMDynamicLoreSource::Character &&
                Fact.TargetCharacterId.IsEqual(Profile->CharacterId) &&
                Fact.Category == Category && Fact.Key.IsEqual(Key);
        });
    if (!bReplacing && ExistingCharacterFacts >= Profile->DevelopedCanon.MaxStoredFacts) return false;

    FLocalLLMDynamicLoreFact Fact;
    Fact.Category = Category;
    Fact.Scope = ELocalLLMDynamicLoreScope::CharacterPrivate;
    Fact.Source = ELocalLLMDynamicLoreSource::Character;
    Fact.SubjectCharacterId = Profile->CharacterId;
    Fact.TargetCharacterId = Profile->CharacterId;
    Fact.Key = Key;
    Fact.Value = CleanValue;
    return UpsertDynamicLoreFact(MoveTemp(Fact));
}

bool ULocalLLMSubsystem::SetSessionKnowledgeAreas(const FGuid& SessionId, const TArray<FName>& AreaIds)
{
    FLocalLLMCharacterProfile* Profile = SessionProfiles.Find(SessionId);
    if (!Profile) return false;
    Profile->ActiveKnowledgeAreas.Reset();
    for (const FName AreaId : AreaIds)
        if (!AreaId.IsNone()) Profile->ActiveKnowledgeAreas.AddUnique(AreaId);
    const FLocalLLMCharacterProfile UpdatedProfile = *Profile;
    return UpdateCharacterSession(SessionId, UpdatedProfile);
}

bool ULocalLLMSubsystem::RegisterTool(const FLocalLLMToolDefinition& Tool)
{
    if (!Worker || Tool.Name.IsEmpty()) return false;
    if (Tool.Name.Equals(TEXT("ProposeDevelopedFact"), ESearchCase::IgnoreCase)) return false;
    for (const TCHAR Character : Tool.Name)
        if (!FChar::IsAlnum(Character) && Character != TEXT('_')) return false;
    Tools.Add(Tool.Name.ToLower(), Tool);
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::UpdateTools;
    Command.RequestId = FGuid::NewGuid();
    Tools.GenerateValueArray(Command.Tools);
    Worker->Enqueue(MoveTemp(Command));
    return true;
}

int32 ULocalLLMSubsystem::RegisterToolSet(ULocalLLMToolSet* ToolSet)
{
    if (!ToolSet) return 0;
    int32 Registered = 0;
    for (const FLocalLLMToolDefinition& Tool : ToolSet->Tools) Registered += RegisterTool(Tool) ? 1 : 0;
    return Registered;
}

bool ULocalLLMSubsystem::UnregisterTool(const FString& ToolName)
{
    if (!Worker || Tools.Remove(ToolName.ToLower()) == 0) return false;
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::UpdateTools;
    Command.RequestId = FGuid::NewGuid();
    Tools.GenerateValueArray(Command.Tools);
    Worker->Enqueue(MoveTemp(Command));
    return true;
}

void ULocalLLMSubsystem::ClearTools()
{
    Tools.Reset();
    if (!Worker) return;
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::UpdateTools;
    Command.RequestId = FGuid::NewGuid();
    Worker->Enqueue(MoveTemp(Command));
}

TArray<FLocalLLMToolDefinition> ULocalLLMSubsystem::GetRegisteredTools() const
{
    TArray<FLocalLLMToolDefinition> Result;
    Tools.GenerateValueArray(Result);
    return Result;
}

FGuid ULocalLLMSubsystem::SubmitToolResult(const FGuid& SessionId, const FGuid& ToolCallId, const FString& ResultJson, const bool bSuccess)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::SubmitToolResult;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.ToolCallId = ToolCallId;
    Command.Text = ResultJson;
    Command.bToolSuccess = bSuccess;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

FGuid ULocalLLMSubsystem::EvaluateRelationshipForSession(const FGuid& SessionId, const bool bApplyChanges)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    const FLocalLLMRelationshipEvaluationSettings* State = RelationshipStates.Find(SessionId);
    if (!State || !State->bEnabled || State->Criteria.IsEmpty())
    {
        FLocalLLMEvent Event;
        Event.Type = ELocalLLMEventType::Error;
        Event.RequestId = RequestId;
        Event.SessionId = SessionId;
        Event.Text = TEXT("Relationship evaluation is disabled or has no criteria for this character session");
        DispatchEvent(Event);
        return RequestId;
    }
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::EvaluateRelationship;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.bApplyRelationshipChanges = bApplyChanges;
    Worker->Enqueue(MoveTemp(Command));
    return RequestId;
}

bool ULocalLLMSubsystem::SetRelationshipRating(const FGuid& SessionId, const FName CriterionName, const int32 Rating)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return false;
    FLocalLLMRelationshipEvaluationSettings* State = RelationshipStates.Find(SessionId);
    if (!State) return false;
    bool bFound = false;
    for (FLocalLLMRelationshipCriterion& Criterion : State->Criteria)
    {
        if (Criterion.Name.IsEqual(CriterionName))
        {
            Criterion.Rating = FMath::Clamp(Rating, 0, 10);
            bFound = true;
            break;
        }
    }
    if (!bFound) return false;
    FLocalLLMCommand Command;
    Command.Type = ELocalLLMCommandType::SetRelationshipRating;
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.CriterionName = CriterionName;
    Command.RelationshipRating = Rating;
    Worker->Enqueue(MoveTemp(Command));
    return true;
}

bool ULocalLLMSubsystem::GetRelationshipState(const FGuid& SessionId, FLocalLLMRelationshipEvaluationSettings& OutState) const
{
    const FLocalLLMRelationshipEvaluationSettings* State = RelationshipStates.Find(SessionId);
    if (!State) return false;
    OutState = *State;
    return true;
}

FGuid ULocalLLMSubsystem::CompactConversationForSession(const FGuid& SessionId)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    EnqueueCommand(static_cast<uint8>(ELocalLLMCommandType::CompactConversation), RequestId, SessionId);
    return RequestId;
}

FGuid ULocalLLMSubsystem::UndoLastConversationTurnForSession(const FGuid& SessionId)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    EnqueueCommand(static_cast<uint8>(ELocalLLMCommandType::UndoConversationTurn), RequestId, SessionId);
    return RequestId;
}

TArray<FLocalLLMSessionInfo> ULocalLLMSubsystem::GetCharacterSessions() const
{
    TArray<FLocalLLMSessionInfo> Result;
    Sessions.GenerateValueArray(Result);
    return Result;
}

void ULocalLLMSubsystem::Cancel(const FGuid& RequestId)
{
    EnqueueCommand(static_cast<uint8>(ELocalLLMCommandType::Cancel), RequestId);
}

FGuid ULocalLLMSubsystem::ResetConversation()
{
    return ResetConversationForSession(DefaultSessionId);
}

FGuid ULocalLLMSubsystem::ResetConversationForSession(const FGuid& SessionId)
{
    const FGuid RequestId = FGuid::NewGuid();
    if (!ValidateSession(SessionId, RequestId)) return RequestId;
    EnqueueCommand(static_cast<uint8>(ELocalLLMCommandType::ResetConversation), RequestId, SessionId);
    return RequestId;
}

bool ULocalLLMSubsystem::Tick(float)
{
    if (!Worker) return true;
    FLocalLLMEvent Event;
    while (Worker->DequeueEvent(Event))
    {
        DispatchEvent(Event);
    }
    return true;
}

void ULocalLLMSubsystem::EnqueueCommand(
    const uint8 CommandType,
    const FGuid& RequestId,
    const FGuid& SessionId,
    FString Text,
    FLocalLLMAudioInput Audio,
    const ELocalLLMAudioInputStrategy AudioInputStrategy,
    FLocalLLMSpeechToTextConfig SpeechToText)
{
    if (!Worker) return;
    FLocalLLMCommand Command;
    Command.Type = static_cast<ELocalLLMCommandType>(CommandType);
    Command.RequestId = RequestId;
    Command.SessionId = SessionId;
    Command.Text = MoveTemp(Text);
    Command.Audio = MoveTemp(Audio);
    Command.AudioInputStrategy = AudioInputStrategy;
    Command.SpeechToText = MoveTemp(SpeechToText);
    Command.SpeechVocabulary = SpeechVocabularyEntries;
    Command.ActiveSpeechVocabularyTags = ActiveSpeechVocabularyTags;
    Worker->Enqueue(MoveTemp(Command));
}

void ULocalLLMSubsystem::DispatchEvent(const FLocalLLMEvent& Event)
{
    if (Event.Type == ELocalLLMEventType::ModelLoaded) bModelLoaded = true;
    else if (Event.Type == ELocalLLMEventType::ModelUnloaded) bModelLoaded = false;
    else if (Event.Type == ELocalLLMEventType::SessionCreated)
    {
        if (FLocalLLMSessionInfo* Info = Sessions.Find(Event.SessionId)) Info->bReady = true;
    }
    else if (Event.Type == ELocalLLMEventType::TurnCompleted && Event.SessionId.IsValid())
    {
        if (FLocalLLMSessionInfo* Info = Sessions.Find(Event.SessionId))
            Info->StoredMessageCount = Event.Text == TEXT("Conversation reset") ? 0 : Info->StoredMessageCount + 2;
    }
    else if (Event.Type == ELocalLLMEventType::ConversationCompacted && Event.SessionId.IsValid())
    {
        if (FLocalLLMSessionInfo* Info = Sessions.Find(Event.SessionId))
            Info->StoredMessageCount = Event.Compaction.RemainingMessageCount;
    }
    else if (Event.Type == ELocalLLMEventType::ConversationTurnUndone && Event.SessionId.IsValid() && Event.Rollback.bUndone)
    {
        if (FLocalLLMSessionInfo* Info = Sessions.Find(Event.SessionId))
            Info->StoredMessageCount = FMath::Max(0, Info->StoredMessageCount - Event.Rollback.RemovedMessageCount);
        RelationshipStates.Add(Event.SessionId, Event.Rollback.RestoredRelationshipState);
    }
    else if (Event.Type == ELocalLLMEventType::RelationshipEvaluated && Event.Relationship.bApplied)
    {
        if (FLocalLLMRelationshipEvaluationSettings* State = RelationshipStates.Find(Event.SessionId))
        {
            for (const FLocalLLMRelationshipCriterionResult& Applied : Event.Relationship.Criteria)
                for (FLocalLLMRelationshipCriterion& Criterion : State->Criteria)
                    if (Criterion.Name.IsEqual(Applied.Name)) Criterion.Rating = Applied.NewRating;
        }
    }

    OnInternalEvent.Broadcast(Event);
    if (Event.Type == ELocalLLMEventType::ToolCallCompleted &&
        Event.ToolName.Equals(TEXT("ProposeDevelopedFact"), ESearchCase::IgnoreCase))
    {
        const FLocalLLMCharacterProfile* Profile = SessionProfiles.Find(Event.SessionId);
        if (!Profile || !Profile->DevelopedCanon.bAutoCommitCharacterProposals)
        {
            OnToolCall.Broadcast(Event.SessionId, Event.CharacterId, Event.ToolCallId,
                Event.ToolName, Event.Text, Event.bToolRequiresPlayerConfirmation);
            return;
        }
        FString CategoryText;
        FString KeyText;
        FString ValueText;
        TSharedPtr<FJsonObject> Arguments;
        const bool bValidJson = FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Event.Text), Arguments) &&
            Arguments.IsValid() &&
            Arguments->TryGetStringField(TEXT("category"), CategoryText) &&
            Arguments->TryGetStringField(TEXT("key"), KeyText) &&
            Arguments->TryGetStringField(TEXT("value"), ValueText);
        ELocalLLMDynamicLoreCategory Category = ELocalLLMDynamicLoreCategory::PersonalPreference;
        const bool bCommitted = bValidJson && ParseCharacterDevelopedCategory(CategoryText, Category) &&
            CommitCharacterDevelopedFact(Event.SessionId, Category, FName(*KeyText), ValueText);
        SubmitToolResult(Event.SessionId, Event.ToolCallId,
            bCommitted
                ? TEXT("{\"committed\":true,\"scope\":\"CharacterPrivate\"}")
                : TEXT("{\"committed\":false,\"reason\":\"The proposal violated the developed-canon policy or capacity.\"}"),
            bCommitted);
        return;
    }
    switch (Event.Type)
    {
    case ELocalLLMEventType::TextDelta:
        OnTextDelta.Broadcast(Event.RequestId, Event.SessionId, Event.CharacterId, Event.Text);
        break;
    case ELocalLLMEventType::ToolCallCompleted:
        OnToolCall.Broadcast(Event.SessionId, Event.CharacterId, Event.ToolCallId,
            Event.ToolName, Event.Text, Event.bToolRequiresPlayerConfirmation);
        break;
    case ELocalLLMEventType::ToolCallStarted:
    case ELocalLLMEventType::ToolCallArgumentsDelta:
        break;
    case ELocalLLMEventType::ConversationCompacted:
        OnSubsystemStateChanged.Broadcast(Event.SessionId, true, false, false);
        break;
    case ELocalLLMEventType::RelationshipEvaluated:
        OnSubsystemStateChanged.Broadcast(Event.SessionId, false, true, false);
        break;
    case ELocalLLMEventType::ConversationTurnUndone:
        OnSubsystemStateChanged.Broadcast(Event.SessionId, false, false, true);
        break;
    default:
        OnStatusChanged.Broadcast(Event.Type, Event.RequestId, Event.SessionId,
            Event.CharacterId, Event.Text);
        break;
    }
}

bool ULocalLLMSubsystem::ValidateSession(const FGuid& SessionId, const FGuid& RequestId)
{
    if (Worker && Sessions.Contains(SessionId)) return true;
    FLocalLLMEvent Event;
    Event.Type = ELocalLLMEventType::Error;
    Event.RequestId = RequestId;
    Event.SessionId = SessionId;
    Event.Text = TEXT("Character session does not exist");
    DispatchEvent(Event);
    return false;
}
