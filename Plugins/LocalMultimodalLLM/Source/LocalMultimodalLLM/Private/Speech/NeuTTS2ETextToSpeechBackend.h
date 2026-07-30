#pragma once

#include "CoreMinimal.h"

class ILocalTextToSpeechBackend;

TUniquePtr<ILocalTextToSpeechBackend> CreateNeuTTS2ETextToSpeechBackend();
