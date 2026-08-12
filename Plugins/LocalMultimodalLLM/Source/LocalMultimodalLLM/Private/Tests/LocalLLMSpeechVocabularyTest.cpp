#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LocalLLMSpeechVocabulary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMSpeechVocabularyTest,
    "LocalMultimodalLLM.Speech.ContextualVocabulary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMSpeechVocabularyTest::RunTest(const FString&)
{
    FLocalLLMSpeechVocabularyEntry Minas;
    Minas.CanonicalText = TEXT("Minas Tirith");
    Minas.KnownAsrVariants = { TEXT("minus tirith"), TEXT("meanest tiereth") };
    Minas.EntityId = TEXT("minas_tirith");
    Minas.EntityType = ELocalLLMSpeechVocabularyEntityType::Location;
    Minas.ActivationTags = { TEXT("gondor") };
    Minas.bAllowBodyCorrection = true;

    FLocalLLMTranscriptNormalizationResult Result = ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        TEXT("We should ride to minus tirith tonight."), { Minas }, { TEXT("gondor") });
    TestEqual(TEXT("Exact authored ASR variant is canonicalized"), Result.CanonicalTranscript,
        FString(TEXT("We should ride to Minas Tirith tonight.")));
    TestEqual(TEXT("Correction is audited"), Result.Corrections.Num(), 1);

    Result = ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        TEXT("We should ride to minus tirith tonight."), { Minas }, {});
    TestEqual(TEXT("Inactive vocabulary does not rewrite"), Result.CanonicalTranscript,
        FString(TEXT("We should ride to minus tirith tonight.")));

    Result = ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        TEXT("The administrator discussed a minus tirithian poem."), { Minas }, { TEXT("gondor") });
    TestTrue(TEXT("Partial word resemblance is never rewritten"), Result.Corrections.IsEmpty());

    FLocalLLMSpeechVocabularyEntry Other = Minas;
    Other.CanonicalText = TEXT("Minas Morgul");
    Other.EntityId = TEXT("minas_morgul");
    Result = ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        TEXT("We should ride to minus tirith tonight."), { Minas, Other }, { TEXT("gondor") });
    TestTrue(TEXT("Colliding authored variants require confirmation"), Result.bNeedsConfirmation);
    TestEqual(TEXT("Ambiguous variants remain unchanged"), Result.CanonicalTranscript,
        FString(TEXT("We should ride to minus tirith tonight.")));
    Result = ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        TEXT("A minus tirithian poem."), { Minas, Other }, { TEXT("gondor") });
    TestFalse(TEXT("An ambiguous variant inside a larger word does not request confirmation"), Result.bNeedsConfirmation);

    FLocalLLMSpeechVocabularyEntry Taro;
    Taro.CanonicalText = TEXT("Taro");
    Taro.KnownAsrVariants = { TEXT("Tar Row"), TEXT("Tarro") };
    Taro.EntityId = TEXT("Taro");
    Taro.EntityType = ELocalLLMSpeechVocabularyEntityType::Character;
    Taro.bAllowBodyCorrection = true;
    Result = ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        TEXT("He says his name is Tar Row."), { Taro }, {});
    TestEqual(TEXT("Exact scene-authored character soundalike is corrected"),
        Result.CanonicalTranscript, FString(TEXT("He says his name is Taro.")));
    Result = ULocalLLMSpeechVocabularyLibrary::NormalizeTranscript(
        TEXT("He says his name is Tara."), { Taro }, {});
    TestEqual(TEXT("A valid unconfigured name is preserved"),
        Result.CanonicalTranscript, FString(TEXT("He says his name is Tara.")));
    return true;
}

#endif
