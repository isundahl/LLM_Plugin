#pragma once

#include "CoreMinimal.h"

class ILocalSpeechToTextBackend;
class ILocalSpeakerEmbeddingBackend;
class ILocalTextToSpeechBackend;

bool EnsureSherpaOnnxRuntimeLoaded();
void ShutdownSherpaOnnxRuntime();
TUniquePtr<ILocalSpeechToTextBackend> CreateSherpaOnnxSpeechToTextBackend();
TUniquePtr<ILocalSpeakerEmbeddingBackend> CreateSherpaOnnxSpeakerEmbeddingBackend();
TUniquePtr<ILocalTextToSpeechBackend> CreateSherpaOnnxPocketTextToSpeechBackend();
