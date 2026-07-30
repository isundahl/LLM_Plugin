#pragma once

#include "CoreMinimal.h"

class ILocalTextToSpeechBackend;

TUniquePtr<ILocalTextToSpeechBackend> CreateChatterboxTurboTextToSpeechBackend();
