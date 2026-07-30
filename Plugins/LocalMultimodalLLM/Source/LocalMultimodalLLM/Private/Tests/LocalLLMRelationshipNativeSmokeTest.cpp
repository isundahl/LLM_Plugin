#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Inference/InferenceWorker.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/LocalLLMModelRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
void ReadStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values) || !Values) return;
    Out.Reset();
    for (const TSharedPtr<FJsonValue>& Value : *Values)
        if (Value.IsValid() && Value->Type == EJson::String) Out.Add(Value->AsString());
}

bool LoadSmokeProfile(const FString& Path, FString& OutModelId, FString& OutPrompt, bool& bOutRunEvaluator,
    int32& OutMaxTokens, FLocalLLMCharacterProfile& OutCharacter, FString& OutError)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Path))
    {
        OutError = TEXT("Could not read relationship smoke scenario");
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    {
        OutError = TEXT("Relationship smoke scenario contains malformed JSON");
        return false;
    }
    OutModelId = Root->GetStringField(TEXT("ModelId"));
    OutPrompt = Root->GetStringField(TEXT("Prompt"));
    bOutRunEvaluator = true;
    Root->TryGetBoolField(TEXT("RunEvaluator"), bOutRunEvaluator);
    double MaxTokens = 80;
    Root->TryGetNumberField(TEXT("MaxTokens"), MaxTokens);
    OutMaxTokens = FMath::Clamp(FMath::RoundToInt(MaxTokens), 16, 256);

    const TSharedPtr<FJsonObject>* CharacterObject = nullptr;
    if (!Root->TryGetObjectField(TEXT("Character"), CharacterObject) || !CharacterObject || !CharacterObject->IsValid())
    {
        OutError = TEXT("Relationship smoke scenario requires a Character object");
        return false;
    }
    FString Value;
    if ((*CharacterObject)->TryGetStringField(TEXT("CharacterId"), Value)) OutCharacter.CharacterId = FName(*Value);
    (*CharacterObject)->TryGetStringField(TEXT("DisplayName"), OutCharacter.DisplayName);
    (*CharacterObject)->TryGetStringField(TEXT("Role"), OutCharacter.Role);
    (*CharacterObject)->TryGetStringField(TEXT("Backstory"), OutCharacter.Backstory);
    ReadStringArray(*CharacterObject, TEXT("PersonalityTraits"), OutCharacter.PersonalityTraits);

    const TSharedPtr<FJsonObject>* EvaluationObject = nullptr;
    if (!(*CharacterObject)->TryGetObjectField(TEXT("RelationshipEvaluation"), EvaluationObject) ||
        !EvaluationObject || !EvaluationObject->IsValid())
    {
        OutError = TEXT("Character requires a RelationshipEvaluation object");
        return false;
    }
    FLocalLLMRelationshipEvaluationSettings& Evaluation = OutCharacter.RelationshipEvaluation;
    Evaluation.bEnabled = true;
    if ((*EvaluationObject)->TryGetStringField(TEXT("TargetId"), Value)) Evaluation.TargetId = FName(*Value);
    (*EvaluationObject)->TryGetStringField(TEXT("TargetDisplayName"), Evaluation.TargetDisplayName);
    (*EvaluationObject)->TryGetStringField(TEXT("EvaluationGuidance"), Evaluation.EvaluationGuidance);
    (*EvaluationObject)->TryGetStringField(TEXT("EvaluatorSystemPrompt"), Evaluation.EvaluatorSystemPrompt);
    ReadStringArray(*EvaluationObject, TEXT("Likes"), Evaluation.Likes);
    ReadStringArray(*EvaluationObject, TEXT("Dislikes"), Evaluation.Dislikes);

    const TArray<TSharedPtr<FJsonValue>>* Criteria = nullptr;
    if (!(*EvaluationObject)->TryGetArrayField(TEXT("Criteria"), Criteria) || !Criteria || Criteria->IsEmpty() || Criteria->Num() > 3)
    {
        OutError = TEXT("RelationshipEvaluation requires one to three Criteria");
        return false;
    }
    Evaluation.Criteria.Reset();
    for (const TSharedPtr<FJsonValue>& CriterionValue : *Criteria)
    {
        const TSharedPtr<FJsonObject> CriterionObject = CriterionValue.IsValid() ? CriterionValue->AsObject() : nullptr;
        if (!CriterionObject.IsValid()) { OutError = TEXT("Every criterion must be an object"); return false; }
        FLocalLLMRelationshipCriterion Criterion;
        if (!CriterionObject->TryGetStringField(TEXT("Name"), Value) || Value.IsEmpty())
        {
            OutError = TEXT("Every criterion requires a Name");
            return false;
        }
        Criterion.Name = FName(*Value);
        CriterionObject->TryGetStringField(TEXT("DisplayName"), Criterion.DisplayName);
        CriterionObject->TryGetStringField(TEXT("Description"), Criterion.Description);
        CriterionObject->TryGetStringField(TEXT("EvaluationGuidance"), Criterion.EvaluationGuidance);
        double Rating = 5;
        CriterionObject->TryGetNumberField(TEXT("Rating"), Rating);
        Criterion.Rating = FMath::Clamp(FMath::RoundToInt(Rating), 0, 10);
        ReadStringArray(CriterionObject, TEXT("RatingPromptOverrides"), Criterion.RatingPromptOverrides);
        if (!Criterion.RatingPromptOverrides.IsEmpty() && Criterion.RatingPromptOverrides.Num() != 11)
        {
            OutError = FString::Printf(TEXT("Criterion %s must provide exactly 11 RatingPromptOverrides"), *Criterion.Name.ToString());
            return false;
        }
        Evaluation.Criteria.Add(MoveTemp(Criterion));
    }
    return true;
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FLocalLLMRelationshipNativeSmokeTest,
    "LocalMultimodalLLM.Native.RelationshipMappingSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FLocalLLMRelationshipNativeSmokeTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
    TArray<FString> Roots;
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalMultimodalLLM")))
        Roots.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Examples"), TEXT("RelationshipTests")));
    Roots.Add(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("LocalLLM"), TEXT("RelationshipSmoke")));
    for (const FString& Root : Roots)
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.relationship-smoke.json"), true, false);
        for (const FString& File : Files)
        {
            OutBeautifiedNames.Add(FPaths::GetBaseFilename(File));
            OutTestCommands.Add(File);
        }
    }
}

bool FLocalLLMRelationshipNativeSmokeTest::RunTest(const FString& Parameters)
{
    FString ModelId;
    FString Prompt;
    FString ParseError;
    bool bRunEvaluator = true;
    int32 MaxTokens = 80;
    FLocalLLMCharacterProfile Character;
    if (!LoadSmokeProfile(Parameters, ModelId, Prompt, bRunEvaluator, MaxTokens, Character, ParseError))
    {
        AddError(FString::Printf(TEXT("%s: %s"), *Parameters, *ParseError));
        return false;
    }

    FLocalLLMModelInfo ModelInfo;
    if (!FLocalLLMModelRegistry::FindById(ModelId, ModelInfo) || !ModelInfo.bCompatible)
    {
        AddError(FString::Printf(TEXT("Relationship smoke model '%s' is unavailable: %s"), *ModelId, *ModelInfo.Status));
        return false;
    }
    ModelInfo.Config.Generation.MaxTokens = MaxTokens;
    ModelInfo.Config.Generation.Temperature = 0.2f;
    ModelInfo.Config.Load.ContextSize = 4096;
    FLocalLLMInferenceWorker Worker;
    FLocalLLMCommand Load;
    Load.Type = ELocalLLMCommandType::LoadModel;
    Load.Backend = ELocalLLMBackend::LlamaCpp;
    Load.RequestId = FGuid::NewGuid();
    Load.ModelConfig = ModelInfo.Config;
    Worker.Enqueue(MoveTemp(Load));

    const double LoadDeadline = FPlatformTime::Seconds() + 180.0;
    bool bLoaded = false;
    while (!bLoaded && FPlatformTime::Seconds() < LoadDeadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
            bLoaded |= Event.Type == ELocalLLMEventType::ModelLoaded;
        }
        if (!bLoaded) FPlatformProcess::Sleep(0.01f);
    }
    if (!TestTrue(TEXT("Configured model loaded"), bLoaded)) return false;

    const FGuid SessionId = FGuid::NewGuid();
    FLocalLLMCommand Create;
    Create.Type = ELocalLLMCommandType::CreateSession;
    Create.RequestId = FGuid::NewGuid();
    Create.SessionId = SessionId;
    Create.Character = Character;
    Worker.Enqueue(MoveTemp(Create));
    FLocalLLMCommand Submit;
    Submit.Type = ELocalLLMCommandType::SubmitText;
    Submit.RequestId = FGuid::NewGuid();
    Submit.SessionId = SessionId;
    Submit.Text = Prompt;
    Worker.Enqueue(MoveTemp(Submit));

    FString Response;
    bool bCompleted = false;
    const double DialogueDeadline = FPlatformTime::Seconds() + 120.0;
    while (!bCompleted && FPlatformTime::Seconds() < DialogueDeadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
            if (Event.SessionId != SessionId) continue;
            if (Event.Type == ELocalLLMEventType::TextDelta) Response += Event.Text;
            if (Event.Type == ELocalLLMEventType::TurnCompleted) bCompleted = true;
        }
        if (!bCompleted) FPlatformProcess::Sleep(0.01f);
    }
    if (!TestTrue(TEXT("Relationship mapping probe completed"), bCompleted)) return false;
    TestFalse(TEXT("Relationship mapping probe produced dialogue"), Response.TrimStartAndEnd().IsEmpty());
    AddInfo(FString::Printf(TEXT("Scenario: %s\nResponse: %s"), *FPaths::GetBaseFilename(Parameters), *Response));
    if (!bRunEvaluator) return !HasAnyErrors();

    FLocalLLMCommand Evaluate;
    Evaluate.Type = ELocalLLMCommandType::EvaluateRelationship;
    Evaluate.RequestId = FGuid::NewGuid();
    Evaluate.SessionId = SessionId;
    Evaluate.bApplyRelationshipChanges = false;
    Worker.Enqueue(MoveTemp(Evaluate));
    const double EvaluationDeadline = FPlatformTime::Seconds() + 120.0;
    while (FPlatformTime::Seconds() < EvaluationDeadline)
    {
        FLocalLLMEvent Event;
        while (Worker.DequeueEvent(Event))
        {
            if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
            if (Event.Type != ELocalLLMEventType::RelationshipEvaluated || Event.SessionId != SessionId) continue;
            TestEqual(TEXT("Evaluator returned every configured criterion"), Event.Relationship.Criteria.Num(), Character.RelationshipEvaluation.Criteria.Num());
            TArray<FString> Results;
            for (const FLocalLLMRelationshipCriterionResult& Criterion : Event.Relationship.Criteria)
                Results.Add(FString::Printf(TEXT("%s=%+d"), *Criterion.Name.ToString(), Criterion.SuggestedDelta));
            AddInfo(FString::Printf(TEXT("Evaluator: %s; confidence=%d; reason=%s"),
                *FString::Join(Results, TEXT(", ")), Event.Relationship.Confidence, *Event.Relationship.Reason));
            return !HasAnyErrors();
        }
        FPlatformProcess::Sleep(0.01f);
    }
    AddError(TEXT("Relationship evaluator timed out"));
    return false;
}

#endif
