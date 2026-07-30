#pragma once

#include "Containers/Array.h"
#include "Modules/ModuleManager.h"

class FLocalMultimodalLLMModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TArray<void*> LoadedLlamaDllHandles;
    bool bSherpaProviderRegistered = false;
    bool bSherpaSpeakerProviderRegistered = false;
    bool bPocketTtsProviderRegistered = false;
    bool bChatterboxTtsProviderRegistered = false;
    bool bNeuTts2EProviderRegistered = false;
};

DECLARE_LOG_CATEGORY_EXTERN(LogLocalMultimodalLLM, Log, All);
