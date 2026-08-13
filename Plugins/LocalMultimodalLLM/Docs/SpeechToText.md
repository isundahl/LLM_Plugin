# Optional speech-to-text backends

Copyright 2026 Ian Sundahl, Volley Studios. Licensed under Apache 2.0.

Audio submission uses `AudioInputStrategy`:

- `Auto` uses native libmtmd audio when the loaded LLM supports it, otherwise it uses the configured speech-to-text provider.
- `NativeModelOnly` never loads a transcription model.
- `TranscriptionOnly` always transcribes before submitting a normal player-text turn.

Transcription is lazy-loaded on the inference worker. The transcript emits
`TranscriptionStarted` and `TranscriptionCompleted`, then enters the same
character history, guard, relationship evaluator, compaction, and tool-calling
pipeline as typed player text.

## Parakeet through sherpa-onnx

The Win64 plugin includes a pinned CPU-only sherpa-onnx 1.13.2 C API runtime.
Set `SpeechToText.Provider` to `sherpa-onnx` and set `ModelPath` to an exported
Parakeet transducer directory containing:

```text
encoder.int8.onnx (or encoder.onnx)
decoder.int8.onnx (or decoder.onnx)
joiner.int8.onnx  (or joiner.onnx)
tokens.txt
```

The included project configuration points to the supplied
`Models/sherpa-onnx-nemo-parakeet-unified-en-0.6b-int8-non-streaming`
directory. The model is not loaded until audio actually needs transcription.
The packaged provider is CPU-only so the primary LLM retains GPU memory.

## Adding another provider

Implement `ILocalSpeechToTextBackend` in a runtime module and register a factory
during that module's startup:

```cpp
FLocalSpeechToTextBackendRegistry::Register(TEXT("whisper-cpp"),
    []() { return MakeUnique<FMyWhisperBackend>(); });
```

Unregister the same provider during module shutdown. Character sessions and
audio routing require no changes. Provider names are data-driven, so adding a
backend does not require modifying a core enum.

Model weights and provider-specific licenses remain the responsibility of the
shipping project.

Rare character and world names need contextual biasing and conservative
post-transcription correction before routing or history. The current packaged
Parakeet recognizer uses greedy decoding and does not enable hotwords. The
greedy path remains the supported default; modified-beam hotwords are only a
future opt-in benchmark candidate. The provider-neutral v1.1 design is documented in
[SpeechNormalization.md](SpeechNormalization.md).

## Live microphone, VAD, and partial captions

Add `Local LLM Microphone Component` to an Actor and call `Start Listening`.
An invalid session ID selects the default character session. The component uses
Unreal's runtime `AudioCaptureCore` API for PCM input and supplies the remaining
speech workflow:

- deterministic RMS voice activity detection;
- configurable pre-roll, speech-start, end-silence, minimum, and maximum durations;
- automatic utterance submission to the selected character session;
- one compact `On User Speech Captured` Blueprint event after the final transcript.

`OnUserSpeechCaptured` exposes only `RawTranscript`, `MeasuredNoiseFloorDb`,
`VoiceThresholdDb`, and `SpeakerSimilarity`. Detailed VAD, PCM, partial-caption,
provider, and diagnostic events remain available to C++ integrations through
the internal microphone event stream rather than expanding every Blueprint node.

Offline providers such as the packaged Parakeet model simulate partial captions
by periodically transcribing the current utterance snapshot. The component keeps
at most one partial request in flight, so transcription cannot build an unbounded
queue. Partials never enter conversation history or invoke the LLM. The final
utterance follows the normal `AudioInputStrategy`: Gemma can receive native audio,
while Qwen uses the configured transcription provider.

### Automatic VAD versus push-to-talk

`FLocalLLMMicrophoneConfig.SegmentationMode` selects one of two provider-neutral capture boundaries:

- `Manual / Push To Talk` is the default. It treats every sample between button press and release as one utterance, with no VAD thresholding, startup noise-floor calibration, or trailing-silence wait.
- `Automatic VAD` is the opt-in always-listening mode. It continuously detects speech using the calibrated local RMS threshold, retains pre-roll, and submits after `SpeechEndSilenceMilliseconds` (350 ms by default).

For push-to-talk Blueprints, call `Start Push To Talk Recording(SessionId)` from the button's Pressed event and `Stop Push To Talk Recording And Submit` from Released. The latter submits immediately. The existing minimum-duration and maximum-duration safeguards remain active, and input suppression still discards microphone audio while an NPC is speaking. This mode removes the default 350 ms end-of-speech latency but depends on the player releasing the button cleanly.

Manual capture includes a submission-only activity gate; it is not VAD segmentation and adds no trailing delay. By default, the recording must be at least 250 ms long and contain at least 80 ms of audio blocks above `-50 dBFS`. A tap, a long silent hold, or one isolated button-click transient emits `InputRejected` and never emits `UtteranceCaptured`, invokes speaker verification, reaches Parakeet/native audio, or starts an LLM turn. `bRejectSilentManualRecordings`, `ManualActivityThresholdDb`, and `ManualMinimumActiveMilliseconds` are exposed for unusually quiet microphones or accessibility requirements. Rejections log the peak measured block level for tuning.

The demo coordinator provides the higher-level `Begin Push To Talk` and `End Push To Talk` pair. Pressed freezes the facing-first recipient and begins voice prewarm before opening capture; Released submits. `Begin Push To Talk For Character` is the explicit-target alternative. The demo preloads the model and all character sessions without opening the microphone. If a player presses before preparation completes, capture is rejected rather than starting late after the button has already been released.

The original `Start Listening` and `Stop Listening` operations also honor `SegmentationMode`, so an existing graph can switch modes through configuration. Partial snapshot transcription remains a VAD-mode feature; manual capture goes directly to final submission on release.

For lower CPU use, disable `bEmitPartialTranscripts` or increase
`PartialTranscriptIntervalSeconds`. `VoiceThresholdDb` must be tuned for the
microphone and room; the default is `-42 dBFS`.

Microphone permission and device availability remain platform responsibilities.
On Windows this uses Unreal's Audio Capture platform backend; packaged mobile
projects must also declare and request the operating system's microphone permission.

### Noise-floor calibration

Automatic calibration is enabled by default when `Start Listening` opens the
microphone in `Automatic VAD` mode. Manual push-to-talk skips calibration because it does not use an amplitude threshold. The player should remain quiet for the configured two seconds. Each
capture block is measured in RMS dBFS, the loudest 20 percent of blocks are
discarded, and the median remaining level becomes the estimated noise floor.
The active threshold is:

```text
clamp(noise floor + NoiseMarginDb, MinimumAutoThresholdDb, MaximumAutoThresholdDb)
```

Defaults are a `12 dB` margin and a `-55` to `-25 dBFS` clamp. The completion
event exposes both `MeasuredNoiseFloorDb` and `VoiceThresholdDb`. Call
`Recalibrate Noise Floor` while listening to measure again, or call
`Set Voice Threshold Db` to cancel calibration and apply a manual override.
`VoiceThresholdDb` also remains directly editable in the component configuration.

## Optional player speaker profile

`Enroll Player Speaker Profile` opens the microphone when necessary, emits
`SpeakerEnrollmentStarted` with the script in `Event.Text`, records the configured
eight seconds, and creates a normalized TiTaNet embedding on the inference worker.
The resulting `SpeakerProfile` property lives on the microphone component attached
to the player character and is marked `SaveGame`. A project using a custom save
system can also copy this struct into its player save record explicitly.

Enable `Config.SpeakerVerification.bUseSpeakerProfile` to check each final VAD
utterance before native model audio or transcription. `SimilarityThreshold` is
editable and defaults to `0.80` for the included TiTaNet Small model. Matching emits `SpeakerVerificationCompleted`
and continues normally. A mismatch additionally emits `SpeakerRejected` and is
not sent to STT or the LLM. Partial captions are suppressed while verification
is active so an unverified background voice cannot leak into captions.

Thresholds are model-, microphone-, and room-dependent. For initial tuning,
disable `bRejectMismatchedSpeaker` to observe `SpeakerSimilarity` without
discarding audio, then choose a threshold from real enrolled-player and
background-speaker samples. The included native smoke pair scores `1.0000` for
the identical recording and approximately `0.7803` for the different speaker;
these values are validation evidence, not a universal biometric guarantee.

If verification is enabled without a valid profile, or the optional embedding
model/provider cannot load, the component emits `Warning` and continues normally.
Use `Clear Player Speaker Profile` to delete the stored embedding. This first
implementation intentionally supports one local player profile rather than
multi-speaker or co-op identification.

The default model is `Models/SpeakerVerification/nemo_en_titanet_small.onnx`.
It is lazy-loaded through the existing CPU sherpa-onnx/ONNX Runtime deployment.
Speaker verification does not separate simultaneous voices; overlapping speech
can still produce an ambiguous embedding.
