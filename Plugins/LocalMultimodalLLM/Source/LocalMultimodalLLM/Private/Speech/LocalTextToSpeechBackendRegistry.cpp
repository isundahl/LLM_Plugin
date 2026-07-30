#include "ILocalTextToSpeechBackend.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
FCriticalSection TextToSpeechRegistryMutex;
TMap<FName, FLocalTextToSpeechBackendFactory> TextToSpeechFactories;
}

bool FLocalTextToSpeechBackendRegistry::Register(const FName Provider, FLocalTextToSpeechBackendFactory Factory)
{
    if (Provider.IsNone() || !Factory) return false;
    FScopeLock Lock(&TextToSpeechRegistryMutex);
    TextToSpeechFactories.Add(Provider, MoveTemp(Factory));
    return true;
}

void FLocalTextToSpeechBackendRegistry::Unregister(const FName Provider)
{
    FScopeLock Lock(&TextToSpeechRegistryMutex);
    TextToSpeechFactories.Remove(Provider);
}

TUniquePtr<ILocalTextToSpeechBackend> FLocalTextToSpeechBackendRegistry::Create(const FName Provider)
{
    FLocalTextToSpeechBackendFactory Factory;
    {
        FScopeLock Lock(&TextToSpeechRegistryMutex);
        if (const FLocalTextToSpeechBackendFactory* Found = TextToSpeechFactories.Find(Provider)) Factory = *Found;
    }
    return Factory ? Factory() : nullptr;
}

TArray<FName> FLocalTextToSpeechBackendRegistry::GetRegisteredProviders()
{
    FScopeLock Lock(&TextToSpeechRegistryMutex);
    TArray<FName> Providers;
    TextToSpeechFactories.GetKeys(Providers);
    Providers.Sort(FNameLexicalLess());
    return Providers;
}
