# Text-to-speech providers

Text-to-speech is exposed through `ULocalLLMTextToSpeechComponent`. The component owns synthesis lifecycle, background execution, cancellation, Blueprint events, and an optional actor-attached playback route; a registered provider owns model loading and PCM generation.

The native `pocket-tts` provider uses the same packaged CPU-only sherpa-onnx
runtime as speech recognition. The optional `neutts-2e` CPU and
`chatterbox-turbo` CUDA development providers use persistent Python sidecars.
The built-in `mock` provider emits a quiet sine wave so development builds and
automation tests can validate the asynchronous path without loading a model.
Mock, NeuTTS-2E, and Chatterbox are not registered in Shipping builds.

## Blueprint flow

1. Add `Local LLM Text To Speech Component` to an actor.
2. Set `Config.Provider` to a registered provider (`pocket-tts`, or
   `neutts-2e`, `chatterbox-turbo`, or `mock` in a non-Shipping Win64 build).
3. Leave `bAutoPlayAudio` enabled for ordinary character speech. Optionally assign an attenuation asset, sound class, volume, or Audio Bus.
4. Bind once to `On Text To Speech Event` for subtitles, lip sync, telemetry, or custom routing.
5. Optionally call `Initialize Text To Speech`; `Synthesize Speech` also initializes lazily.
6. Optionally enable `bPrewarmVoiceWhenSelected` and call `Prewarm Voice` once the primary recipient has been frozen from pre-utterance gameplay state. It silently primes provider and voice execution and is idempotent.
7. Submit an `FLocalLLMTextToSpeechRequest` with text and optional per-request voice, language, and speaking rate.
8. For guarded LLM dialogue, pass every sentence-sized `TextDelta` to `Queue Speech`; the component serializes sentences while synthesis is busy and appends their audio without cutting off the prior sentence.
9. Use `Cancel Speech Synthesis` when a character is interrupted; it clears queued sentences and buffered playback.

For batch providers that become unreliable on long sentences, set
`MaxQueuedSegmentCharacters` to a nonzero limit. `Queue Speech` divides longer
text at the natural punctuation or conjunction nearest
`PreferredQueuedSplitFraction` (default `0.58`) and preserves order, request
routing, voice, language, and speaking rate. Zero leaves provider behavior
unchanged. The current NeuTTS-2E and Pocket showcase configurations use a
100-character limit; projects can leave Pocket unsplit when its selected voice
is stable on longer sentences.

The event stream uses the same `RequestId`, `SessionId`, and `CharacterId` throughout a synthesis request, so a shared listener can route audio to the correct embodied character. Only one provider request runs on a component at a time. `Queue Speech` supplies a built-in per-component sentence queue; use one component per independently speaking character.

### Speculative voice prewarm

`Prewarm Voice` is provider-neutral, silent, asynchronous, and never emits PCM, subtitles, facial curves, or conversation history. Providers may implement a cheap native preparation operation; otherwise the interface synthesizes a tiny `Ready.` utterance and discards it. The component remembers successful preparation until its provider or voice configuration changes. A call made while model initialization is still running is deferred automatically, and real speech queued during prewarm begins immediately afterward.

Freeze the speculative primary recipient before recording begins using only state already available at that boundary: explicit interaction target, current conversation membership, gaze/facing, and proximity. Do not revise this warmup from STT text, recognized names, utterance loudness, or other evidence produced after capture starts. Those signals can still determine final dialogue routing, but should not create cascading speculative warmups. Warm only the primary likely respondent by default; secondary listeners can receive context without paying a TTS preparation cost unless gameplay promotes one to respondent.

`OutputSampleRate = 0` preserves the provider's native rate. Providers should return normalized interleaved float PCM and must set `SampleRate` and `NumChannels` on every chunk. A provider that cannot stream may emit one chunk at completion.

Provider-neutral loudness normalization does not immediately amplify the first
quiet frame. It ignores codec startup noise below a conservative speech floor,
ramps gain increases slowly, applies attenuation quickly, and interpolates gain
at sample granularity to avoid 20 ms frame-boundary clicks. Completion logs
report raw peak/RMS, first-half-second onset peak and maximum gain, output peak,
and the applied gain range.

## Direct playback and MetaSounds

Automatic playback is enabled by default. The component lazily creates a transient `USoundWaveProcedural` and `UAudioComponent`, attaches the audio component to its owning actor, converts float PCM to 16-bit PCM, and starts playback when the first chunk arrives. It stops after synthesis and the buffered audio both finish. `Get Speech Audio Component` exposes the created component for additional runtime controls, and `Play Audio Chunk` can feed compatible PCM explicitly.

- `bSpatializePlayback` permits 3D positioning; assign `PlaybackAttenuationSettings` to define audible range and falloff.
- `PlaybackSoundClass` and `PlaybackVolumeMultiplier` integrate speech with the project's normal mix.
- Assign a mono Audio Bus to `MetaSoundAudioBus` to send the live pre-effect speech signal to an `Audio Bus Reader (Mono)` node in a MetaSound. This supports analysis, effects, lip-sync signals, or alternate output routing while direct playback remains active.
- `Stop Speech Playback` discards already-buffered audio without unloading the TTS model.

Disable `bAutoPlayAudio` only when a project needs complete ownership of playback - for example a dedicated server, offline rendering, a custom voice-chat buffer, or a specialized lip-sync/MetaSound pipeline. Raw `TextToSpeechChunk` and `TextToSpeechCompleted` events remain available in either mode.

## Adding a provider

Implement `ILocalTextToSpeechBackend`, then register a factory during the provider module's startup:

```cpp
FLocalTextToSpeechBackendRegistry::Register(TEXT("my-tts"), []
{
    return MakeUnique<FMyTextToSpeechBackend>();
});
```

Unregister it during shutdown. `Load` should create and cache heavyweight model and voice state. `Synthesize` should poll `IsCancelled`, call `OnChunk` as samples become available, and also populate the complete `FLocalTextToSpeechResult`. Provider code runs on a thread-pool worker and must not touch UObjects or other game-thread-only APIs.

Keep a native runtime in its own optional Unreal module. That module should own third-party includes, import libraries, DLL staging, runtime availability checks, and its registry entry. The core plugin therefore continues to compile when that SDK is absent.

## Pocket TTS provider

`pocket-tts` is the first production adapter, registered as `pocket-tts`. The downloaded development model is located at `Models/PocketTTS/sherpa-onnx-pocket-tts-int8-2026-01-26`. Map the generic fields as follows:

| Plugin field | Pocket adapter meaning |
| --- | --- |
| `ModelPath` | Local Pocket model directory or provider-specific model identifier |
| `VoiceId` | Gameplay-facing label copied into result events; the reference WAV determines the synthesized voice |
| `SpeakerReferencePath` | Required consented mono 16-bit PCM reference WAV |
| `Language` | Must be English (`en`) for the included model |
| `ChunkMilliseconds` | Target size of emitted PCM chunks |
| `bUseGpu` | Must remain `false`; the packaged runtime is CPU-only |
| `QualitySteps` | Flow-matching quality/speed tradeoff; start with `2` |
| `Seed` | Deterministic synthesis seed; use a negative value for provider randomness |
| `MaxReferenceSeconds` | Maximum duration consumed from the reference WAV; defaults to `10` |

The adapter creates and caches one sherpa-onnx engine per component configuration. Pocket's internal voice-embedding cache is enabled, reference audio is decoded once at load, and generation uses the native progress callback for cancellable PCM chunks. The included model is English-only and requires a mono 16-bit PCM reference WAV; unsupported languages and malformed references fail explicitly.

Keep `OutputSampleRate` at `0` for true native streaming. Requesting another output rate is supported through final-buffer resampling, but necessarily delays chunk delivery until synthesis completes.

Voice cloning must only use audio for which the project has the necessary permission and consent. The provider surfaces missing model access, unsupported languages, and malformed reference audio through the normal `Error` event rather than silently falling back to a different speaker.

### Pocket licensing and voice rights

Pocket's components have separate licenses:

- Kyutai's Pocket TTS source code is MIT.
- The upstream Pocket model weights are CC BY 4.0.
- The tested `sherpa-onnx-pocket-tts-int8-2026-01-26` ONNX bundle includes a
  CC BY 4.0 license.
- The sherpa-onnx runtime is Apache 2.0.

These licenses permit commercial use, but their attribution, license-copy, and
change-notice requirements still apply. The upstream gated model also includes
an acceptable-use notice, including consent requirements for voice cloning.

The ONNX model card contains an ambiguous sentence saying “It is for
non-commercial” immediately after identifying its bundled `test_wavs`. That
statement conflicts with the bundle's actual CC BY 4.0 license and appears to
describe or mistakenly characterize the example recordings rather than apply a
CC BY-NC license to the model. The plugin does not rely on or redistribute
those example WAV files. A release must provide its own properly licensed and
consented reference audio and preserve the Pocket, CC BY 4.0, sherpa-onnx, and
ONNX Runtime notices.

The plugin includes two small CC0 Pocket reference voices:

| Voice ID | `SpeakerReferencePath` | Presentation |
| --- | --- | --- |
| `pocket-caro-davy` | `Plugin:/Content/Voices/pocket-caro-davy.wav` | Female |
| `pocket-bill-boerst` | `Plugin:/Content/Voices/pocket-bill-boerst.wav` | Male |

They originate from Kyutai's Voice-Zero selection. `Plugin:/` resolves against
the installed `LocalMultimodalLLM` plugin directory in both project-plugin and
engine-plugin installations. The files are staged as NonUFS runtime assets.
CC0 does not require attribution, but source provenance is retained in
`Content/Voices/README.md` and `THIRD_PARTY_NOTICES.md`.

EARS and Expresso recordings are CC BY-NC 4.0 and must remain
development/benchmark material. They cannot be included in a commercial game,
plugin, Starter Bundle, trailer asset pack, or commercial voice preset without
separate permission.

## NeuTTS Nano qualification candidate

NeuTTS Nano is being qualified as a second provider. It uses a Q4 GGUF speech-token backbone through llama.cpp and a CPU ONNX NeuCodec decoder. Unlike Pocket, a voice consists of both pre-encoded reference codes and the exact transcript of the consented reference recording. Enrollment may use a heavier codec encoder offline; gameplay should load only the encoded reference, GGUF backbone, and ONNX decoder.

The development-only benchmark is isolated under `Saved/NeuTTSBenchmark` so its Python and Torch dependencies are never staged with the plugin:

```powershell
.\Scripts\SetupNeuTTSNanoBenchmark.ps1
.\Scripts\RunNeuTTSNanoBenchmark.ps1
```

The benchmark uses the project-only `TestData/TTS/EARS/p003` reference, reports model load time, time to first audio, real-time factor, baseline/loaded/peak resident memory, and writes both WAV and JSON results. The shipping provider should only be implemented after this qualification confirms acceptable quality, latency, and stability.

## NeuTTS-2E development provider

NeuTTS-2E is registered as the development-only `neutts-2e` runtime provider.
Like Chatterbox, it uses a persistent Python sidecar in editor/development builds
and is not registered in Shipping builds. The project keeps only the Q4 backbone with the INT8 NeuCodec
configuration; the FP32 codec was removed because it produced no meaningful
waveform improvement in the qualification run while increasing memory and
distribution size.

NeuTTS code and model artifacts use the NeuTTS Open License v1.0. It permits
redistribution and commercial use while the using legal entity remains below
USD $5 million in annual revenue. At or above that threshold, commercial use
requires a separate license from Neuphonic. Distributions must include the
license and NOTICE material, retain applicable attribution, and identify
modified files. NeuTTS is therefore a useful optional commercial provider, but
not an unrestricted default for every possible plugin customer.

The GGUF backbone can run through llama.cpp and the NeuCodec decoder can run
through ONNX Runtime, making a native Win64 provider feasible with runtimes the
plugin already ships. The current implementation still uses Python, Torch, and
a persistent worker process; native Shipping support is planned and must not be
claimed by a v1 package.

Prepare its environment and retained artifacts from the project root:

```powershell
.\Scripts\SetupNeuTTSNanoBenchmark.ps1
.\Scripts\SetupNeuTTS2EBenchmark.ps1
```

Then configure:

| Plugin field | NeuTTS-2E adapter meaning |
| --- | --- |
| `Provider` | `neutts-2e` |
| `ModelPath` | `Saved/NeuTTS2EBenchmark` |
| `VoiceId` | `emily`, `paul`, `sophie`, or `steven` |
| `SpeakerReferencePath` | Unused; leave empty |
| `bUseGpu` | Currently ignored; the retained runtime uses CPU |
| `ChunkMilliseconds` | Published PCM frame size; each native inference chunk is subdivided before events |
| `Seed` | Deterministic backbone seed |
| `SamplingTemperature` | Autoregressive randomness; `0.7` is the stable-dialogue default |
| `SamplingTopK` | Speech-token candidate limit; `30` is the stable-dialogue default |
| `SpokenTextReplacements` | Optional pronunciation-only replacements for difficult names |
| `SynthesisTimeoutSeconds` | Wall-clock ceiling including shared-service waits |
| `MaxGeneratedSeconds` | Absolute generated-audio safety ceiling |

The retained model and codec total about 585 MB. The earlier batch qualification
measured a 1.57-second model load, 1.31-second warmup, 0.44-second average warmed
time to first audio, 0.57 average real-time factor, and approximately 1.75 GB
peak process resident memory. A later runtime smoke test produced its first
native PCM chunk in 0.55 seconds and completed 2.28 seconds of speech in 1.78
seconds. These figures exclude
microphone capture, STT, LLM prompt evaluation, generation to the first sentence
boundary, Unreal playback buffering, and audio-device latency.

NeuTTS-2E offers four fixed speakers rather than arbitrary reference-voice
cloning. Set `VoiceId` to `emily`, `paul`, `sophie`, or `steven`; the model and
warmup are shared across character components using the same `ModelPath`, and
requests are serialized through that worker. Native inference chunks are
subdivided into the configured PCM frame duration and published incrementally
rather than waiting for the complete waveform. Use 20 ms frames for the
MetaHuman Audio Live Link bridge. Runtime
playback currently uses the neutral emotion. Its output is somewhat robotic but competitive for
constrained production hardware. NeuTTS receives self-contained spoken fragments
(leading conjunctions removed and artificial comma boundaries closed) and defaults
to conservative `0.7`/`30` sampling to reduce occasional phonetic drift. Currency
notation is expanded before synthesis (`$4,000` becomes `four thousand dollars`)
without changing subtitles or stored dialogue. `SpokenTextReplacements` provides
the same separation for project-specific proper names (for example,
`Harry Longbaugh` to `Harry Long-baw`). Raise these
sampling values only when greater variation is worth reduced stability. A future shipping release should replace the
Python bridge with an accepted native runtime and expose emotion as a
provider-neutral request field.

Use `.\Scripts\RunNeuTTS2EBenchmark.ps1` for the retained benchmark suite.

The benchmark configuration lives at
`<Project>/TestData/TTS/Configs/neutts-2e-q4.example.json`.

## Chatterbox Turbo development provider

`chatterbox-turbo` is an optional quality-first Win64/CUDA provider. It is not staged with packaged builds: developers must prepare its isolated runtime and download the model assets before using it:

```powershell
.\Scripts\SetupChatterboxTurboBenchmark.ps1
```

Use these provider-neutral fields:

| Plugin field | Chatterbox adapter meaning |
| --- | --- |
| `Provider` | `chatterbox-turbo` |
| `ModelPath` | Runtime directory, normally `Saved/ChatterboxTurboBenchmark` |
| `VoiceId` | Stable cache key for the prepared voice conditionals |
| `SpeakerReferencePath` | Required consented reference WAV |
| `bUseGpu` | Must be `true` |
| `ChunkMilliseconds` | Size of the PCM chunks emitted after generation completes |
| `Seed` | Generation seed |
| `MaxReferenceSeconds` | Maximum reference audio used during voice preparation |
| `MaxGeneratedSeconds` | Utterance safety ceiling |
| `MaxQueuedSegmentCharacters` | Optional clause-aware queue split; the demo uses `100` |
| `PreferredQueuedSplitFraction` | Preferred balance for a split; the demo uses `0.58` |

All Chatterbox components using the same `ModelPath` acquire one process-wide hidden worker and one CUDA model. Each component registers its stable `VoiceId` and reference; prepared conditionals are cached under `Saved/ChatterboxTurboBenchmark/voices`. Requests select their voice inside the shared worker, so switching characters restores cached conditionals rather than reloading model weights or duplicating model VRAM. The worker serializes synthesis because simultaneous speakers would contend for the same model and are rarely desirable for dialogue.

The first active component pays the model startup cost. Additional voices only pay condition preparation plus the optional discarded warmup. In the two-character smoke test, the commandlet's first model/voice startup took 12.27 seconds, the second cached voice preparation and warmup took 1.45 seconds without another model load, and switching back through `Prewarm Voice` succeeded. Normal cached condition switching is expected to be milliseconds; PIE logs the prewarm duration for verification on the target machine.

Chatterbox's open-source `generate()` API returns a complete waveform rather
than incremental model output. Before publishing any chunk, the adapter now
validates the completed batch against `MaxGeneratedSeconds`. An oversized or
malformed batch is rejected atomically, so neither audible playback nor facial
animation can consume PCM that will later be discarded. Accepted waveforms are
then divided into normal provider-neutral PCM chunks. First audio still cannot
arrive until the segment waveform is ready. Sentence-guarded LLM streaming and
optional clause splitting reduce the amount of text in each batch.

The provider accepts the inline non-verbal tags `[laugh]`, `[chuckle]`, `[sigh]`, `[gasp]`, `[cough]`, and `[whisper]`. Only expose these tags in a character system prompt when that character is configured for Chatterbox. Recommend at most one genuinely appropriate tag per short response and usually none; excessive tags reduce naturalness and add generation time. The demo coordinator applies this contract automatically to Chatterbox characters.

On the qualification machine, the EARS `p003` voice produced a short untagged waveform in about 0.8 seconds after warmup. A longer tagged line took about 1.3 seconds in the worker and 1.66 seconds through the Unreal adapter. Peak TTS VRAM was approximately 3.3 GB and incremental system memory was approximately 2 GB. These measurements exclude microphone/VAD, STT, LLM prompt evaluation, generation to the first sentence boundary, playback buffering, and device latency. Treat Chatterbox as an opt-in high-quality tier; keep Pocket available for constrained hardware.

See `<Project>/TestData/TTS/Configs/chatterbox-turbo-p003.example.json` for the
development demo configuration. Voice cloning must only use audio for which the
project has the necessary permission and consent.

## Scope boundary

The built-in path handles reliable low-latency playback and common Unreal mixing controls. It deliberately does not choose facial animation, subtitle timing, character-specific effects, or whether a MetaSound should replace the direct output. Those systems can consume the unchanged PCM events or the optional Audio Bus without modifying a TTS provider.
