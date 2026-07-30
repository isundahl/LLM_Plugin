#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backends/ILocalMultimodalBackend.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMRelationshipTest,
    "LocalMultimodalLLM.Relationship.MockPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMRelationshipTest::RunTest(const FString&)
{
    const FLocalLLMCharacterProfile PromptDefaults;
    TestTrue(TEXT("Generated character context defaults on"), PromptDefaults.bUseGeneratedContext);
    TestTrue(TEXT("Authoritative world grounding defaults on"), PromptDefaults.bUseAuthoritativeWorldGrounding);
    TestFalse(TEXT("Unsupported world speculation defaults off"), PromptDefaults.bAllowUnsupportedWorldSpeculation);
    TestFalse(TEXT("Character-developed canon defaults off"), PromptDefaults.DevelopedCanon.bEnableCharacterProposals);
    TestFalse(TEXT("Character-developed canon auto-commit defaults off"), PromptDefaults.DevelopedCanon.bAutoCommitCharacterProposals);
    TestEqual(TEXT("Developed canon prompt budget"), PromptDefaults.DevelopedCanon.MaxPromptTokens, 384);
    TestEqual(TEXT("Developed canon fact limit"), PromptDefaults.DevelopedCanon.MaxStoredFacts, 32);
    TestEqual(TEXT("Preferred spoken sentence target"), PromptDefaults.PreferredSpokenSentences, 2);
    TestEqual(TEXT("Emergency spoken sentence ceiling"), PromptDefaults.MaxSpokenSentences, 6);
    TestTrue(TEXT("Tool instructions default on"), PromptDefaults.bIncludeToolInstructions);
    TestTrue(TEXT("Conversation history replay defaults on"), PromptDefaults.bIncludeConversationHistory);
    TestTrue(TEXT("Custom system prompt defaults empty"), PromptDefaults.CustomSystemPrompt.IsEmpty());
    TestTrue(TEXT("Automatic compaction defaults on"), PromptDefaults.ConversationMemory.bEnableAutoCompaction);
    TestTrue(TEXT("Conversation budgets scale with the loaded context by default"), PromptDefaults.ConversationMemory.bScaleBudgetsWithModelContext);
    TestEqual(TEXT("Compaction threshold"), PromptDefaults.ConversationMemory.CompactAfterTurns, 10);
    TestEqual(TEXT("Recent turns retained"), PromptDefaults.ConversationMemory.RecentTurnsToKeep, 5);
    TestEqual(TEXT("Generated context token budget"), PromptDefaults.ConversationMemory.MaxGeneratedContextTokens, 2560);
    TestEqual(TEXT("Compacted memory token budget"), PromptDefaults.ConversationMemory.MaxCompactedMemoryTokens, 1024);
    TestEqual(TEXT("Recent dialogue token budget"), PromptDefaults.ConversationMemory.RecentDialogueTokenBudget, 2560);
    TestEqual(TEXT("Player input token limit"), PromptDefaults.ConversationMemory.MaxPlayerInputTokens, 768);

    FLocalLLMRelationshipEvaluationSettings Defaults;
    TestEqual(TEXT("Starter criterion count"), Defaults.Criteria.Num(), 2);
    TestEqual(TEXT("Starter affinity name"), Defaults.Criteria[0].Name, FName(TEXT("affinity")));
    TestEqual(TEXT("Starter trust name"), Defaults.Criteria[1].Name, FName(TEXT("trust")));
    TestEqual(TEXT("Starter affinity rating"), Defaults.Criteria[0].Rating, 5);
    TestEqual(TEXT("Starter trust rating"), Defaults.Criteria[1].Rating, 5);

    TArray<FLocalLLMEvent> Events;
    TUniquePtr<ILocalMultimodalBackend> Backend = CreateLocalLLMBackend(
        ELocalLLMBackend::Mock,
        [&Events](FLocalLLMEvent&& Event) { Events.Add(MoveTemp(Event)); },
        []() { return false; });

    FLocalLLMModelConfig Config;
    Backend->LoadModel(Config, FGuid::NewGuid());
    const FGuid SessionId = FGuid::NewGuid();
    FLocalLLMCharacterProfile Character;
    Character.CharacterId = TEXT("mara");
    Character.DisplayName = TEXT("Mara");
    Character.RelationshipEvaluation.bEnabled = true;
    Character.RelationshipEvaluation.Criteria[0].Rating = 2;
    Character.RelationshipEvaluation.Criteria[1].Rating = 8;
    Backend->CreateSession(SessionId, Character, FGuid::NewGuid());
    Backend->CompactConversation(SessionId, FGuid::NewGuid());
    Backend->EvaluateRelationship(SessionId, true, FGuid::NewGuid());

    TestTrue(TEXT("Mock manual compaction event"), Events.ContainsByPredicate([](const FLocalLLMEvent& Event)
    {
        return Event.Type == ELocalLLMEventType::ConversationCompacted;
    }));

    const FLocalLLMEvent* Evaluation = Events.FindByPredicate([](const FLocalLLMEvent& Event)
    {
        return Event.Type == ELocalLLMEventType::RelationshipEvaluated;
    });
    TestNotNull(TEXT("Mock relationship event"), Evaluation);
    if (Evaluation)
    {
        TestEqual(TEXT("Evaluation criterion count"), Evaluation->Relationship.Criteria.Num(), 2);
        TestEqual(TEXT("Affinity remains independent"), Evaluation->Relationship.Criteria[0].NewRating, 2);
        TestEqual(TEXT("Trust remains independent"), Evaluation->Relationship.Criteria[1].NewRating, 8);
    }

    Events.Reset();
    Backend->SetRelationshipRating(SessionId, TEXT("affinity"), 99, FGuid::NewGuid());
    const FLocalLLMEvent* Override = Events.FindByPredicate([](const FLocalLLMEvent& Event)
    {
        return Event.Type == ELocalLLMEventType::RelationshipEvaluated;
    });
    TestNotNull(TEXT("Authoritative override event"), Override);
    if (Override) TestEqual(TEXT("Authoritative override clamps to ten"), Override->Relationship.Criteria[0].NewRating, 10);
    return true;
}

#endif
