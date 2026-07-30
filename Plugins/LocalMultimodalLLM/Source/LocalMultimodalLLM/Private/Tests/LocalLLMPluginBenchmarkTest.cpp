#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
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
struct FBenchmarkTurn
{
    FString Prompt;
    TArray<FString> RequiredAll;
    TArray<FString> SignalsAny;
    TArray<FString> Forbidden;
};

struct FBenchmarkScenario
{
    FString Id;
    FString CharacterKey;
    int32 Affinity = 5;
    int32 Trust = 5;
    bool bRunEvaluator = false;
    TArray<FBenchmarkTurn> Turns;
    FString ExpectedToolName;
    FString ExpectedToolArgument;
};

void ReadStrings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values) || !Values) return;
    for (const TSharedPtr<FJsonValue>& Value : *Values)
        if (Value.IsValid() && Value->Type == EJson::String) Out.Add(Value->AsString());
}

bool LoadBenchmark(const FString& Path, FString& OutModelId, int32& OutMaxTokens, FLocalLLMWorldContext& OutWorld,
    TMap<FString, FLocalLLMCharacterProfile>& OutCharacters, TArray<FBenchmarkScenario>& OutScenarios, FString& OutError)
{
    FString Json;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    {
        OutError = TEXT("Could not read the plugin benchmark JSON");
        return false;
    }
    if (!Root->TryGetStringField(TEXT("ModelId"), OutModelId)) { OutError = TEXT("Benchmark requires ModelId"); return false; }
    double MaxTokens = 72;
    Root->TryGetNumberField(TEXT("MaxTokens"), MaxTokens);
    OutMaxTokens = FMath::Clamp(FMath::RoundToInt(MaxTokens), 24, 256);

    const TSharedPtr<FJsonObject>* World = nullptr;
    if (Root->TryGetObjectField(TEXT("World"), World) && World && World->IsValid())
    {
        (*World)->TryGetStringField(TEXT("WorldName"), OutWorld.WorldName);
        (*World)->TryGetStringField(TEXT("SettingDescription"), OutWorld.SettingDescription);
        (*World)->TryGetStringField(TEXT("CurrentLocation"), OutWorld.CurrentLocation);
        (*World)->TryGetStringField(TEXT("CurrentSituation"), OutWorld.CurrentSituation);
    }

    const TSharedPtr<FJsonObject>* Characters = nullptr;
    if (!Root->TryGetObjectField(TEXT("Characters"), Characters) || !Characters || !Characters->IsValid())
    {
        OutError = TEXT("Benchmark requires Characters");
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Characters)->Values)
    {
        const TSharedPtr<FJsonObject> Object = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
        if (!Object.IsValid()) continue;
        FLocalLLMCharacterProfile Character;
        FString Text;
        if (Object->TryGetStringField(TEXT("CharacterId"), Text)) Character.CharacterId = FName(*Text);
        Object->TryGetStringField(TEXT("DisplayName"), Character.DisplayName);
        Object->TryGetStringField(TEXT("Role"), Character.Role);
        Object->TryGetStringField(TEXT("Backstory"), Character.Backstory);
        ReadStrings(Object, TEXT("PersonalityTraits"), Character.PersonalityTraits);
        ReadStrings(Object, TEXT("SpeechPatterns"), Character.SpeechPatterns);
        Character.JailbreakGuard.Mode = ELocalLLMJailbreakGuardMode::DetectOnly;
        Character.ImmersionGuard.Mode = ELocalLLMImmersionGuardMode::DetectOnly;
        FLocalLLMRelationshipEvaluationSettings& Relationship = Character.RelationshipEvaluation;
        Relationship.bEnabled = true;
        Relationship.TargetId = TEXT("player");
        Relationship.TargetDisplayName = TEXT("the player");
        ReadStrings(Object, TEXT("Likes"), Relationship.Likes);
        ReadStrings(Object, TEXT("Dislikes"), Relationship.Dislikes);
        Object->TryGetStringField(TEXT("EvaluationGuidance"), Relationship.EvaluationGuidance);
        OutCharacters.Add(Pair.Key, MoveTemp(Character));
    }

    const TArray<TSharedPtr<FJsonValue>>* Scenarios = nullptr;
    if (!Root->TryGetArrayField(TEXT("Scenarios"), Scenarios) || !Scenarios)
    {
        OutError = TEXT("Benchmark requires Scenarios");
        return false;
    }
    for (const TSharedPtr<FJsonValue>& ScenarioValue : *Scenarios)
    {
        const TSharedPtr<FJsonObject> Object = ScenarioValue.IsValid() ? ScenarioValue->AsObject() : nullptr;
        if (!Object.IsValid()) continue;
        FBenchmarkScenario Scenario;
        Object->TryGetStringField(TEXT("Id"), Scenario.Id);
        Object->TryGetStringField(TEXT("Character"), Scenario.CharacterKey);
        double Number = 5;
        if (Object->TryGetNumberField(TEXT("Affinity"), Number)) Scenario.Affinity = FMath::Clamp(FMath::RoundToInt(Number), 0, 10);
        if (Object->TryGetNumberField(TEXT("Trust"), Number)) Scenario.Trust = FMath::Clamp(FMath::RoundToInt(Number), 0, 10);
        Object->TryGetBoolField(TEXT("RunEvaluator"), Scenario.bRunEvaluator);
        const TArray<TSharedPtr<FJsonValue>>* Turns = nullptr;
        if (Object->TryGetArrayField(TEXT("Turns"), Turns) && Turns)
        {
            for (const TSharedPtr<FJsonValue>& TurnValue : *Turns)
            {
                const TSharedPtr<FJsonObject> TurnObject = TurnValue.IsValid() ? TurnValue->AsObject() : nullptr;
                if (!TurnObject.IsValid()) continue;
                FBenchmarkTurn Turn;
                TurnObject->TryGetStringField(TEXT("Prompt"), Turn.Prompt);
                ReadStrings(TurnObject, TEXT("RequiredAll"), Turn.RequiredAll);
                ReadStrings(TurnObject, TEXT("SignalsAny"), Turn.SignalsAny);
                ReadStrings(TurnObject, TEXT("Forbidden"), Turn.Forbidden);
                Scenario.Turns.Add(MoveTemp(Turn));
            }
        }
        const TSharedPtr<FJsonObject>* Tool = nullptr;
        if (Object->TryGetObjectField(TEXT("ExpectedTool"), Tool) && Tool && Tool->IsValid())
        {
            (*Tool)->TryGetStringField(TEXT("Name"), Scenario.ExpectedToolName);
            (*Tool)->TryGetStringField(TEXT("ArgumentContains"), Scenario.ExpectedToolArgument);
        }
        if (Scenario.Id.IsEmpty() || !OutCharacters.Contains(Scenario.CharacterKey) || Scenario.Turns.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Invalid benchmark scenario: %s"), *Scenario.Id);
            return false;
        }
        OutScenarios.Add(MoveTemp(Scenario));
    }
    return !OutScenarios.IsEmpty();
}

TArray<FLocalLLMToolDefinition> MakeBenchmarkTools(const FString& OnlyTool)
{
    FLocalLLMToolDefinition Move;
    Move.Name = TEXT("MoveToTarget");
    Move.Description = TEXT("Request that this character walk to a nearby game-advertised target. Unreal validates and executes it.");
    FLocalLLMToolParameter Target;
    Target.Name = TEXT("target_id");
    Target.Description = TEXT("Stable ID from the game-provided actionable target list");
    Target.Type = ELocalLLMToolValueType::String;
    Target.AllowedValues = { TEXT("map_table") };
    Move.Parameters.Add(Target);

    FLocalLLMToolDefinition Gesture;
    Gesture.Name = TEXT("PlayGesture");
    Gesture.Description = TEXT("Request one short cosmetic gesture from the allow-list.");
    FLocalLLMToolParameter GestureName;
    GestureName.Name = TEXT("gesture");
    GestureName.Description = TEXT("Allow-listed cosmetic gesture");
    GestureName.Type = ELocalLLMToolValueType::String;
    GestureName.AllowedValues = { TEXT("Nod"), TEXT("ShakeHead"), TEXT("Wave"), TEXT("Point"), TEXT("Shrug") };
    Gesture.Parameters.Add(GestureName);
    if (OnlyTool == Move.Name) return { Move };
    if (OnlyTool == Gesture.Name) return { Gesture };
    return {};
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMPluginBenchmarkTest,
    "LocalMultimodalLLM.Benchmark.PluginV1",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMPluginBenchmarkTest::RunTest(const FString&)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalMultimodalLLM"));
    if (!Plugin) { AddError(TEXT("LocalMultimodalLLM plugin was not found")); return false; }
    const FString BenchmarkPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Examples"), TEXT("Benchmarks"), TEXT("plugin-benchmark-v1.json"));
    FString ModelId;
    FString ParseError;
    int32 MaxTokens = 72;
    FLocalLLMWorldContext World;
    TMap<FString, FLocalLLMCharacterProfile> Characters;
    TArray<FBenchmarkScenario> Scenarios;
    if (!LoadBenchmark(BenchmarkPath, ModelId, MaxTokens, World, Characters, Scenarios, ParseError))
    {
        AddError(ParseError);
        return false;
    }
    TestEqual(TEXT("Benchmark scenario count"), Scenarios.Num(), 10);

    FLocalLLMModelInfo ModelInfo;
    if (!FLocalLLMModelRegistry::FindById(ModelId, ModelInfo) || !ModelInfo.bCompatible)
    {
        AddError(FString::Printf(TEXT("Benchmark model '%s' is unavailable: %s"), *ModelId, *ModelInfo.Status));
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
    if (!TestTrue(TEXT("Benchmark model loaded once"), bLoaded)) return false;
    FLocalLLMCommand SetWorld;
    SetWorld.Type = ELocalLLMCommandType::UpdateWorldContext;
    SetWorld.RequestId = FGuid::NewGuid();
    SetWorld.World = World;
    Worker.Enqueue(MoveTemp(SetWorld));

    auto WaitForTurn = [this, &Worker](const FGuid& SessionId, FString& OutText)
    {
        const double Deadline = FPlatformTime::Seconds() + 120.0;
        while (FPlatformTime::Seconds() < Deadline)
        {
            FLocalLLMEvent Event;
            while (Worker.DequeueEvent(Event))
            {
                if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
                if (Event.SessionId != SessionId) continue;
                if (Event.Type == ELocalLLMEventType::TextDelta) OutText += Event.Text;
                if (Event.Type == ELocalLLMEventType::TurnCompleted) return true;
            }
            FPlatformProcess::Sleep(0.01f);
        }
        AddError(TEXT("Benchmark dialogue timed out"));
        return false;
    };

    int32 HardPassed = 0;
    int32 SoftMatched = 0;
    int32 SoftTotal = 0;
    for (const FBenchmarkScenario& Scenario : Scenarios)
    {
        bool bScenarioPassed = true;
        FLocalLLMCharacterProfile Character = Characters.FindChecked(Scenario.CharacterKey);
        Character.RelationshipEvaluation.Criteria[0].Rating = Scenario.Affinity;
        Character.RelationshipEvaluation.Criteria[1].Rating = Scenario.Trust;
        FLocalLLMCommand Tools;
        Tools.Type = ELocalLLMCommandType::UpdateTools;
        Tools.RequestId = FGuid::NewGuid();
        if (!Scenario.ExpectedToolName.IsEmpty()) Tools.Tools = MakeBenchmarkTools(Scenario.ExpectedToolName);
        Worker.Enqueue(MoveTemp(Tools));

        const FGuid SessionId = FGuid::NewGuid();
        FLocalLLMCommand Create;
        Create.Type = ELocalLLMCommandType::CreateSession;
        Create.RequestId = FGuid::NewGuid();
        Create.SessionId = SessionId;
        Create.Character = MoveTemp(Character);
        Worker.Enqueue(MoveTemp(Create));

        if (Scenario.ExpectedToolName.IsEmpty())
        {
            for (int32 TurnIndex = 0; TurnIndex < Scenario.Turns.Num(); ++TurnIndex)
            {
                const FBenchmarkTurn& Turn = Scenario.Turns[TurnIndex];
                FLocalLLMCommand Submit;
                Submit.Type = ELocalLLMCommandType::SubmitText;
                Submit.RequestId = FGuid::NewGuid();
                Submit.SessionId = SessionId;
                Submit.Text = Turn.Prompt;
                Worker.Enqueue(MoveTemp(Submit));
                FString Response;
                if (!WaitForTurn(SessionId, Response)) return false;
                AddInfo(FString::Printf(TEXT("[%s turn %d] %s"), *Scenario.Id, TurnIndex + 1, *Response));
                if (Response.TrimStartAndEnd().IsEmpty())
                {
                    AddError(FString::Printf(TEXT("%s returned empty dialogue"), *Scenario.Id));
                    bScenarioPassed = false;
                }
                for (const FString& Required : Turn.RequiredAll)
                {
                    if (!Response.Contains(Required, ESearchCase::IgnoreCase))
                    {
                        AddError(FString::Printf(TEXT("%s omitted required marker: %s"), *Scenario.Id, *Required));
                        bScenarioPassed = false;
                    }
                }
                for (const FString& Forbidden : Turn.Forbidden)
                {
                    if (Response.Contains(Forbidden, ESearchCase::IgnoreCase))
                    {
                        AddError(FString::Printf(TEXT("%s leaked forbidden character marker: %s"), *Scenario.Id, *Forbidden));
                        bScenarioPassed = false;
                    }
                }
                if (!Turn.SignalsAny.IsEmpty())
                {
                    ++SoftTotal;
                    const bool bSignal = Turn.SignalsAny.ContainsByPredicate([&Response](const FString& Signal)
                    {
                        return Response.Contains(Signal, ESearchCase::IgnoreCase);
                    });
                    if (bSignal) ++SoftMatched;
                    else AddWarning(FString::Printf(TEXT("%s did not contain a configured relationship/style signal"), *Scenario.Id));
                }
            }
        }
        else
        {
            const FBenchmarkTurn& Turn = Scenario.Turns[0];
            FLocalLLMCommand Submit;
            Submit.Type = ELocalLLMCommandType::SubmitText;
            Submit.RequestId = FGuid::NewGuid();
            Submit.SessionId = SessionId;
            Submit.Text = Turn.Prompt;
            Worker.Enqueue(MoveTemp(Submit));
            FGuid ToolCallId;
            FString ActualTool;
            FString Arguments;
            FString NonToolResponse;
            bool bTurnCompletedWithoutTool = false;
            const double ToolDeadline = FPlatformTime::Seconds() + 120.0;
            while (!ToolCallId.IsValid() && !bTurnCompletedWithoutTool && FPlatformTime::Seconds() < ToolDeadline)
            {
                FLocalLLMEvent Event;
                while (Worker.DequeueEvent(Event))
                {
                    if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
                    if (Event.Type == ELocalLLMEventType::TextDelta && Event.SessionId == SessionId) NonToolResponse += Event.Text;
                    if (Event.Type == ELocalLLMEventType::TurnCompleted && Event.SessionId == SessionId) bTurnCompletedWithoutTool = true;
                    if (Event.Type == ELocalLLMEventType::ToolCallCompleted && Event.SessionId == SessionId)
                    {
                        ToolCallId = Event.ToolCallId;
                        ActualTool = Event.ToolName;
                        Arguments = Event.Text;
                    }
                }
                if (!ToolCallId.IsValid()) FPlatformProcess::Sleep(0.01f);
            }
            if (!ToolCallId.IsValid() || ActualTool != Scenario.ExpectedToolName ||
                !Arguments.Contains(Scenario.ExpectedToolArgument, ESearchCase::CaseSensitive))
            {
                AddError(FString::Printf(TEXT("%s tool mismatch: expected %s/%s, received %s/%s"),
                    *Scenario.Id, *Scenario.ExpectedToolName, *Scenario.ExpectedToolArgument, *ActualTool, *Arguments));
                if (!NonToolResponse.IsEmpty()) AddInfo(FString::Printf(TEXT("[%s non-tool response] %s"), *Scenario.Id, *NonToolResponse));
                bScenarioPassed = false;
            }
            else
            {
                AddInfo(FString::Printf(TEXT("[%s] validated tool %s %s"), *Scenario.Id, *ActualTool, *Arguments));
                FLocalLLMCommand Result;
                Result.Type = ELocalLLMCommandType::SubmitToolResult;
                Result.RequestId = FGuid::NewGuid();
                Result.SessionId = SessionId;
                Result.ToolCallId = ToolCallId;
                Result.Text = TEXT("{\"success\":true,\"status\":\"completed\"}");
                Worker.Enqueue(MoveTemp(Result));
                FString Continuation;
                if (!WaitForTurn(SessionId, Continuation)) return false;
                if (Continuation.TrimStartAndEnd().IsEmpty())
                {
                    AddError(FString::Printf(TEXT("%s produced no dialogue after tool completion"), *Scenario.Id));
                    bScenarioPassed = false;
                }
                AddInfo(FString::Printf(TEXT("[%s continuation] %s"), *Scenario.Id, *Continuation));
            }
        }
        if (Scenario.bRunEvaluator)
        {
            FLocalLLMCommand Evaluate;
            Evaluate.Type = ELocalLLMCommandType::EvaluateRelationship;
            Evaluate.RequestId = FGuid::NewGuid();
            Evaluate.SessionId = SessionId;
            Evaluate.bApplyRelationshipChanges = false;
            Worker.Enqueue(MoveTemp(Evaluate));
            bool bEvaluated = false;
            const double EvaluationDeadline = FPlatformTime::Seconds() + 120.0;
            while (!bEvaluated && FPlatformTime::Seconds() < EvaluationDeadline)
            {
                FLocalLLMEvent Event;
                while (Worker.DequeueEvent(Event))
                {
                    if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
                    if (Event.Type != ELocalLLMEventType::RelationshipEvaluated || Event.SessionId != SessionId) continue;
                    bEvaluated = true;
                    TArray<FString> Results;
                    for (const FLocalLLMRelationshipCriterionResult& Criterion : Event.Relationship.Criteria)
                        Results.Add(FString::Printf(TEXT("%s=%+d"), *Criterion.Name.ToString(), Criterion.SuggestedDelta));
                    AddInfo(FString::Printf(TEXT("[%s evaluator] %s; confidence=%d; %s"), *Scenario.Id,
                        *FString::Join(Results, TEXT(", ")), Event.Relationship.Confidence, *Event.Relationship.Reason));
                    if (Event.Relationship.Criteria.Num() != 2) bScenarioPassed = false;
                }
                if (!bEvaluated) FPlatformProcess::Sleep(0.01f);
            }
            if (!bEvaluated)
            {
                AddError(FString::Printf(TEXT("%s relationship evaluator timed out"), *Scenario.Id));
                bScenarioPassed = false;
            }
        }
        if (bScenarioPassed) ++HardPassed;
    }

    AddInfo(FString::Printf(TEXT("PLUGIN BENCHMARK V1: hard scenarios %d/%d; relationship/style signals %d/%d"),
        HardPassed, Scenarios.Num(), SoftMatched, SoftTotal));
    TestEqual(TEXT("All hard benchmark scenarios passed"), HardPassed, Scenarios.Num());
    return !HasAnyErrors();
}

#endif
