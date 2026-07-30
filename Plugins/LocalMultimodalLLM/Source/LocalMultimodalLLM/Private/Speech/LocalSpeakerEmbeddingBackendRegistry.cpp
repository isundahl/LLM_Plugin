#include "ILocalSpeakerEmbeddingBackend.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
FCriticalSection SpeakerRegistryMutex;
TMap<FName, FLocalSpeakerEmbeddingBackendFactory> SpeakerFactories;
}

bool FLocalSpeakerEmbeddingBackendRegistry::Register(const FName Provider, FLocalSpeakerEmbeddingBackendFactory Factory)
{
    if (Provider.IsNone() || !Factory) return false;
    FScopeLock Lock(&SpeakerRegistryMutex);
    return SpeakerFactories.Add(Provider, MoveTemp(Factory)) != nullptr;
}

void FLocalSpeakerEmbeddingBackendRegistry::Unregister(const FName Provider)
{
    FScopeLock Lock(&SpeakerRegistryMutex);
    SpeakerFactories.Remove(Provider);
}

TUniquePtr<ILocalSpeakerEmbeddingBackend> FLocalSpeakerEmbeddingBackendRegistry::Create(const FName Provider)
{
    FLocalSpeakerEmbeddingBackendFactory Factory;
    {
        FScopeLock Lock(&SpeakerRegistryMutex);
        const FLocalSpeakerEmbeddingBackendFactory* Found = SpeakerFactories.Find(Provider);
        if (!Found) return nullptr;
        Factory = *Found;
    }
    return Factory();
}
