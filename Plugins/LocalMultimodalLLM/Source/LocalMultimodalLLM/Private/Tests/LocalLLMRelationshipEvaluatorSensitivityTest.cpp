#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "HAL/PlatformProcess.h"
#include "Inference/InferenceWorker.h"
#include "Models/LocalLLMModelRegistry.h"

namespace
{
struct FEvaluatorSensitivityCase
{
    FString Id;
    FLocalLLMCharacterProfile Character;
    TArray<FString> PlayerTurns;
    FName ExpectedCriterion;
    int32 MinimumDelta = -2;
    int32 MaximumDelta = 2;
};

FLocalLLMCharacterProfile MakeNessaEvaluatorProfile()
{
    FLocalLLMCharacterProfile Character;
    Character.CharacterId = TEXT("validator_nessa");
    Character.DisplayName = TEXT("Nessa");
    Character.Role = TEXT("An irreverent Ashland caravan scout");
    Character.Backstory = TEXT("Nessa survived the Glass Wastes and values companions who can share danger without self-pity.");
    Character.PersonalityTraits = { TEXT("irreverent"), TEXT("resourceful"), TEXT("fond of rough humor") };
    Character.RelationshipEvaluation.bEnabled = true;
    Character.RelationshipEvaluation.Likes = { TEXT("friendly crude language"), TEXT("rough jokes without cruelty"), TEXT("practical competence") };
    Character.RelationshipEvaluation.Dislikes = { TEXT("cruel personal attacks"), TEXT("snobbery"), TEXT("humiliation disguised as humor") };
    Character.RelationshipEvaluation.EvaluationGuidance = TEXT("Profanity is not negative by itself. Friendly rough praise should increase affinity. Cruel or demeaning rough speech should reduce affinity. Do not change trust merely because profanity was used.");
    return Character;
}

FLocalLLMCharacterProfile MakeSuspiciousEvaluatorProfile()
{
    FLocalLLMCharacterProfile Character;
    Character.CharacterId = TEXT("validator_sable");
    Character.DisplayName = TEXT("Sable");
    Character.Role = TEXT("A dramatic intelligence courier who expects deception");
    Character.Backstory = TEXT("Sable survived years in a court of spies where small contradictions often concealed larger betrayals.");
    Character.PersonalityTraits = { TEXT("dramatic"), TEXT("hypervigilant"), TEXT("slow to trust") };
    Character.RelationshipEvaluation.bEnabled = true;
    Character.RelationshipEvaluation.Likes = { TEXT("precise consistency"), TEXT("verifiable details") };
    Character.RelationshipEvaluation.Dislikes = { TEXT("self-contradiction"), TEXT("changing stories") };
    Character.RelationshipEvaluation.EvaluationGuidance = TEXT("For this unusually suspicious character, an admitted deliberate lie about even a harmless personal preference is weak evidence of unreliability and should reduce trust by 1, but should not cause a major change.");
    return Character;
}

FLocalLLMCharacterProfile MakeTrustingEvaluatorProfile()
{
    FLocalLLMCharacterProfile Character;
    Character.CharacterId = TEXT("validator_elian");
    Character.DisplayName = TEXT("Elian");
    Character.Role = TEXT("A patient village healer who assumes good faith");
    Character.Backstory = TEXT("Elian was raised in a cooperative village and believes mistakes and changing preferences are normal.");
    Character.PersonalityTraits = { TEXT("patient"), TEXT("forgiving"), TEXT("naturally trusting") };
    Character.RelationshipEvaluation.bEnabled = true;
    Character.RelationshipEvaluation.Likes = { TEXT("honest admissions"), TEXT("good-faith correction") };
    Character.RelationshipEvaluation.Dislikes = { TEXT("deliberate consequential lies"), TEXT("betrayal after a promise") };
    Character.RelationshipEvaluation.EvaluationGuidance = TEXT("Do not reduce trust for harmless preference changes, trivial contradictions, ordinary mistakes, or an admitted inconsequential lie about a favorite color. Reduce trust for an explicit admission of a deliberate consequential lie or broken promise.");
    return Character;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMRelationshipEvaluatorSensitivityTest,
    "LocalMultimodalLLM.Native.RelationshipEvaluatorSensitivity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMRelationshipEvaluatorSensitivityTest::RunTest(const FString&)
{
    FLocalLLMModelInfo ModelInfo;
    if (!FLocalLLMModelRegistry::FindById(TEXT("gemma-4-e2b-it-qat"), ModelInfo) || !ModelInfo.bCompatible)
    {
        AddError(FString::Printf(TEXT("Gemma evaluator sensitivity model is unavailable: %s"), *ModelInfo.Status));
        return false;
    }
    ModelInfo.Config.Generation.MaxTokens = 56;
    ModelInfo.Config.Generation.Temperature = 0.2f;
    ModelInfo.Config.Load.ContextSize = 4096;
    FLocalLLMInferenceWorker Worker;
    FLocalLLMCommand Load;
    Load.Type = ELocalLLMCommandType::LoadModel;
    Load.Backend = ELocalLLMBackend::LlamaCpp;
    Load.RequestId = FGuid::NewGuid();
    Load.ModelConfig = ModelInfo.Config;
    Worker.Enqueue(MoveTemp(Load));

    bool bLoaded = false;
    const double LoadDeadline = FPlatformTime::Seconds() + 180.0;
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
    if (!TestTrue(TEXT("Gemma loaded for evaluator sensitivity test"), bLoaded)) return false;

    TArray<FEvaluatorSensitivityCase> Cases;
    {
        FEvaluatorSensitivityCase Case;
        Case.Id = TEXT("rough_friendly_increases_affinity");
        Case.Character = MakeNessaEvaluatorProfile();
        Case.PlayerTurns = { TEXT("Hell of a route, you stubborn bastard. You got us through that damned storm. Fine work, friend.") };
        Case.ExpectedCriterion = TEXT("affinity");
        Case.MinimumDelta = 1;
        Cases.Add(MoveTemp(Case));
    }
    {
        FEvaluatorSensitivityCase Case;
        Case.Id = TEXT("rough_cruelty_reduces_affinity");
        Case.Character = MakeNessaEvaluatorProfile();
        Case.PlayerTurns = { TEXT("You are a worthless bastard and everyone would be better off if you kept your mouth shut. I mean that as a cruel personal attack, not friendly rough humor.") };
        Case.ExpectedCriterion = TEXT("affinity");
        Case.MaximumDelta = -1;
        Cases.Add(MoveTemp(Case));
    }
    {
        FEvaluatorSensitivityCase Case;
        Case.Id = TEXT("suspicious_minor_contradiction_reduces_trust");
        Case.Character = MakeSuspiciousEvaluatorProfile();
        Case.PlayerTurns = { TEXT("My favorite color is blue."), TEXT("I lied about blue. Red has always been my favorite. It was a deliberate but trivial lie.") };
        Case.ExpectedCriterion = TEXT("trust");
        Case.MaximumDelta = -1;
        Cases.Add(MoveTemp(Case));
    }
    {
        FEvaluatorSensitivityCase Case;
        Case.Id = TEXT("trusting_minor_contradiction_is_tolerated");
        Case.Character = MakeTrustingEvaluatorProfile();
        Case.PlayerTurns = { TEXT("My favorite color is blue."), TEXT("I lied about blue. Red has always been my favorite. It was a deliberate but trivial lie.") };
        Case.ExpectedCriterion = TEXT("trust");
        Case.MinimumDelta = 0;
        Case.MaximumDelta = 0;
        Cases.Add(MoveTemp(Case));
    }
    {
        FEvaluatorSensitivityCase Case;
        Case.Id = TEXT("trusting_character_penalizes_admitted_lie");
        Case.Character = MakeTrustingEvaluatorProfile();
        Case.PlayerTurns = { TEXT("I promised I would not steal your medicine. I stole it, sold it, and lied to you about what happened.") };
        Case.ExpectedCriterion = TEXT("trust");
        Case.MaximumDelta = -1;
        Cases.Add(MoveTemp(Case));
    }

    auto WaitForTurn = [this, &Worker](const FGuid& SessionId)
    {
        const double Deadline = FPlatformTime::Seconds() + 120.0;
        while (FPlatformTime::Seconds() < Deadline)
        {
            FLocalLLMEvent Event;
            while (Worker.DequeueEvent(Event))
            {
                if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
                if (Event.Type == ELocalLLMEventType::TurnCompleted && Event.SessionId == SessionId) return true;
            }
            FPlatformProcess::Sleep(0.01f);
        }
        AddError(TEXT("Evaluator sensitivity conversation turn timed out"));
        return false;
    };

    int32 Passed = 0;
    for (const FEvaluatorSensitivityCase& Case : Cases)
    {
        const FGuid SessionId = FGuid::NewGuid();
        FLocalLLMCommand Create;
        Create.Type = ELocalLLMCommandType::CreateSession;
        Create.RequestId = FGuid::NewGuid();
        Create.SessionId = SessionId;
        Create.Character = Case.Character;
        Worker.Enqueue(MoveTemp(Create));
        for (const FString& PlayerTurn : Case.PlayerTurns)
        {
            FLocalLLMCommand Submit;
            Submit.Type = ELocalLLMCommandType::SubmitText;
            Submit.RequestId = FGuid::NewGuid();
            Submit.SessionId = SessionId;
            Submit.Text = PlayerTurn;
            Worker.Enqueue(MoveTemp(Submit));
            if (!WaitForTurn(SessionId)) return false;
        }

        FLocalLLMCommand Evaluate;
        Evaluate.Type = ELocalLLMCommandType::EvaluateRelationship;
        Evaluate.RequestId = FGuid::NewGuid();
        Evaluate.SessionId = SessionId;
        Evaluate.bApplyRelationshipChanges = false;
        Worker.Enqueue(MoveTemp(Evaluate));
        bool bReceived = false;
        int32 ActualDelta = 0;
        FString Reason;
        int32 Confidence = 0;
        const double EvaluationDeadline = FPlatformTime::Seconds() + 120.0;
        while (!bReceived && FPlatformTime::Seconds() < EvaluationDeadline)
        {
            FLocalLLMEvent Event;
            while (Worker.DequeueEvent(Event))
            {
                if (Event.Type == ELocalLLMEventType::Error) { AddError(Event.Text); return false; }
                if (Event.Type != ELocalLLMEventType::RelationshipEvaluated || Event.SessionId != SessionId) continue;
                bReceived = true;
                Confidence = Event.Relationship.Confidence;
                Reason = Event.Relationship.Reason;
                const FLocalLLMRelationshipCriterionResult* Result = Event.Relationship.Criteria.FindByPredicate(
                    [&Case](const FLocalLLMRelationshipCriterionResult& Criterion) { return Criterion.Name.IsEqual(Case.ExpectedCriterion); });
                if (!Result)
                {
                    AddError(FString::Printf(TEXT("%s omitted criterion %s"), *Case.Id, *Case.ExpectedCriterion.ToString()));
                    return false;
                }
                ActualDelta = Result->SuggestedDelta;
            }
            if (!bReceived) FPlatformProcess::Sleep(0.01f);
        }
        if (!bReceived) { AddError(FString::Printf(TEXT("%s evaluator timed out"), *Case.Id)); return false; }
        const bool bInRange = ActualDelta >= Case.MinimumDelta && ActualDelta <= Case.MaximumDelta;
        AddInfo(FString::Printf(TEXT("[%s] %s=%+d expected [%+d,%+d], confidence=%d, reason=%s"),
            *Case.Id, *Case.ExpectedCriterion.ToString(), ActualDelta, Case.MinimumDelta, Case.MaximumDelta, Confidence, *Reason));
        if (!bInRange)
            AddError(FString::Printf(TEXT("%s produced an out-of-range personality-relative judgment"), *Case.Id));
        else
            ++Passed;
    }
    AddInfo(FString::Printf(TEXT("RELATIONSHIP EVALUATOR SENSITIVITY: %d/%d cases passed"), Passed, Cases.Num()));
    TestEqual(TEXT("All personality-sensitive evaluator cases passed"), Passed, Cases.Num());
    return !HasAnyErrors();
}

#endif
