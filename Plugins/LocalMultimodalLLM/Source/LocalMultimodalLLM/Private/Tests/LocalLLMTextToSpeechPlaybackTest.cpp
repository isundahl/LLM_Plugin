#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LocalLLMTextToSpeechComponent.h"
#include "Sound/SoundWaveProcedural.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalLLMTextToSpeechPlaybackTest,
    "LocalMultimodalLLM.Speech.TextToSpeechProceduralPlayback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalLLMTextToSpeechPlaybackTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient playback test world is created"), World)) return false;

    AActor* Actor = World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Playback owner is created"), Actor))
    {
        World->DestroyWorld(false);
        return false;
    }
    ULocalLLMTextToSpeechComponent* Component = NewObject<ULocalLLMTextToSpeechComponent>(Actor);
    Actor->AddInstanceComponent(Component);
    Component->RegisterComponent();

    TestTrue(TEXT("Automatic playback is the default"), Component->bAutoPlayAudio);
    FLocalLLMAudioChunk Chunk;
    Chunk.SampleRate = 24000;
    Chunk.NumChannels = 1;
    Chunk.SequenceNumber = 0;
    Chunk.Samples = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    TestTrue(TEXT("Valid float PCM is accepted for playback"), Component->PlayAudioChunk(Chunk));
    TestNotNull(TEXT("Procedural sound wave is created lazily"), Component->GetSpeechSoundWave());
    TestNotNull(TEXT("Actor-attached audio component is created lazily"), Component->GetSpeechAudioComponent());
    if (USoundWaveProcedural* Wave = Component->GetSpeechSoundWave())
    {
        TestEqual(TEXT("Playback sample rate follows the chunk"), Wave->GetSampleRateForCurrentPlatform(), 24000.0f);
        TestEqual(TEXT("Float samples are queued as 16-bit PCM"), Wave->GetAvailableAudioByteCount(),
            Chunk.Samples.Num() * static_cast<int32>(sizeof(int16)));
    }

    Component->StopSpeechPlayback();
    Component->DestroyComponent();
    World->DestroyWorld(false);
    return !HasAnyErrors();
}

#endif
