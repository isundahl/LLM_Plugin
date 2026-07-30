#include "ILocalSpeechToTextBackend.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
FCriticalSection RegistryMutex;
TMap<FName, FLocalSpeechToTextBackendFactory> Factories;
}

bool FLocalSpeechToTextBackendRegistry::Register(const FName Provider, FLocalSpeechToTextBackendFactory Factory)
{
    if (Provider.IsNone() || !Factory) return false;
    FScopeLock Lock(&RegistryMutex);
    Factories.Add(Provider, MoveTemp(Factory));
    return true;
}

void FLocalSpeechToTextBackendRegistry::Unregister(const FName Provider)
{
    FScopeLock Lock(&RegistryMutex);
    Factories.Remove(Provider);
}

TUniquePtr<ILocalSpeechToTextBackend> FLocalSpeechToTextBackendRegistry::Create(const FName Provider)
{
    FLocalSpeechToTextBackendFactory Factory;
    {
        FScopeLock Lock(&RegistryMutex);
        const FLocalSpeechToTextBackendFactory* Found = Factories.Find(Provider);
        if (Found) Factory = *Found;
    }
    return Factory ? Factory() : nullptr;
}

TArray<FName> FLocalSpeechToTextBackendRegistry::GetRegisteredProviders()
{
    FScopeLock Lock(&RegistryMutex);
    TArray<FName> Providers;
    Factories.GetKeys(Providers);
    Providers.Sort(FNameLexicalLess());
    return Providers;
}
