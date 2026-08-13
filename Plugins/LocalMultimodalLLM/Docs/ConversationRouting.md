# Ambient multi-character conversation routing

Copyright 2026 Ian Sundahl, Volley Studios. Licensed under Apache 2.0.

## Current behavior

The reusable plug-in currently selects the addressee explicitly:

- typed dialogue calls `Submit Text For Session(SessionId, Text)`;
- microphone capture starts with a target `SessionId` and submits its final utterance there.

This explicit surface is predictable and portable. The host demo implements a
working project-level router without making camera or pawn assumptions part of
the core plug-in API.

The project demo now includes a deliberately narrow bridge toward the full router design. `Start Conversation(CharacterId)` freezes an explicit primary before capture, prewarms that character's TTS voice, and safely switches the microphone session when the explicit target changes. `Find Best Recipient From View` previews a deterministic camera/head score, and `Start Conversation With Best Recipient` freezes that result and starts capture. An optional `AttentionSourceActor` supports VR heads, third-person aim pivots, or project-specific cameras.

The demo's default pre-utterance score is 60% facing, 30% active-conversation membership, and 10% proximity. All weights and the secondary threshold are exposed. Distance only gates whether a character could hear the utterance. After final transcription, `OnParticipantHeardDialogue` fires for the primary and every audible registered demo character; non-primary characters are classified as `SecondaryParticipant` or `Bystander` from the attention score, and every alert exposes the component scores. Alerts never invoke inference automatically. This supplies Blueprint wake/animation hooks but does not yet persist passive observations.

## Design principle

Hearing and responding are separate decisions:

1. Determine every registered character inside the utterance's effective audible radius.
2. Score and classify every audible character, then emit one addressed hearing event for each of them.
3. Add a passive observation to those listeners without running inference.
4. Select at most one `PrimaryAddressee` using the same deterministic address/attention score.
5. Submit direct dialogue only to that primary session.
6. When the responder finishes, score/classify the audible reply for every nearby listener in the same way.

Every audible participant therefore “wakes” at the routing/event layer, even when they are only an uninvolved bystander. Passive observation and hearing events must never trigger inference by themselves. The project may subscribe to a tier and choose a reaction, but the plugin does not create response storms or accidental chains of characters talking indefinitely.

## Candidate reusable Unreal surface

Add `ULocalLLMConversationRouterComponent` to the player controller, pawn, or dialogue coordinator.

Suggested Blueprint operations:

- `Register Conversation Participant(Actor, SessionId, DisplayName, Aliases, Settings)`
- `Unregister Conversation Participant(SessionId)`
- `Route Player Dialogue(Text, SpeakerLocation, SpeakerForward, Loudness)`
- `Set Focused Character(SessionId)`
- `Clear Focused Character()`
- `Notify Character Spoke(SessionId, Text, Loudness)`

Suggested events:

- `OnAddresseeSelected`
- `OnParticipantHeardDialogue` (one event per audible registered participant)
- `OnAmbiguousAddressee`
- `OnNoAddressee`

Suggested attention enum:

```text
ELocalLLMConversationAttentionTier
  PrimaryAddressee      // at most one; receives the direct turn and may answer
  SecondaryParticipant // already involved or strongly addressed; project decides any reaction
  TertiaryListener      // attentive but not a current conversation participant
  AmbientHearer         // heard the utterance but is not involved
```

Every hearing event should contain the listener `SessionId`/`CharacterId`, tier, total score, individual name/focus/facing/proximity/perceived-loudness scores, distance, source identity, text, and whether the listener is currently allowed to respond. This makes thresholds explainable and lets Blueprint react only to the tiers it cares about. Not binding the event has no side effect beyond the passive observation policy.

By default, every audible listener receives a passive observation regardless of tier. Participant settings may opt out of observation storage for disposable crowds, but event classification still occurs while registered and audible.

The subsystem also needs one narrow operation:

```text
Observe Dialogue For Session(SessionId, Observation)
```

An observation is stored and compacted but does not invoke inference. It should include speaker identity, text, whether the listener was directly addressed, and optional distance/loudness metadata. It must use an `observation` history role rather than pretending the player spoke directly to that character. Passive observations should not count as relationship-evaluator player evidence by default.

## Audibility

Each participant supplies:

- actor/location source;
- base hearing radius and hearing multiplier;
- whether hearing is currently enabled;
- optional occlusion test/channel;
- optional faction/conversation channel filters.

Microphone dBFS is hardware- and gain-dependent, so raw amplitude should not directly become Unreal distance. Normalize the utterance against the calibrated voice threshold:

```text
Loudness01 = clamp((UtteranceDb - VoiceThresholdDb) / ConfiguredDynamicRangeDb, 0, 1)
AudibleRadius = BaseRadius * lerp(MinimumVolumeMultiplier, MaximumVolumeMultiplier, Loudness01)
```

Also expose an explicit speech-effort override such as `Whisper`, `Normal`, `Loud`, or `Shout`. This lets input/gameplay intentionally control distance even when two microphones have different automatic gain.

Recommended first pass:

- broad-phase sphere distance;
- optional visibility/occlusion trace that reduces or rejects audibility;
- configurable radius/multiplier only;
- no acoustic simulation.

## Attention scoring and responder selection

Every audible registered character is scored, including characters that cannot currently respond. Use deterministic normalized signals:

- explicit name or alias used as an address;
- retained conversational focus from the previous exchange;
- facing/camera dot product;
- proximity within audible radius;
- project-authored wake/interrupt priority;
- penalties for busy, incapacitated, leaving, or recently rejected characters.

An example—not a fixed contract—is:

```text
Score = 0.50 * NameAddress
      + 0.25 * CurrentFocus
      + 0.20 * Facing
      + 0.15 * Proximity
      + 0.10 * PerceivedLoudness
      + WakeBonus
      - StatePenalties
```

Loudness primarily controls audible radius. Perceived loudness at each listener—after distance and optional occlusion—may also raise attention/wake score, but direction, name, focus, and proximity do the actual addressee discrimination.

Clamp the final score and map it through configurable tier thresholds. `bInConversation` or retained focus may establish a minimum `SecondaryParticipant` tier. Everyone else inside the radius still receives either `TertiaryListener` or `AmbientHearer`.

Select a primary responder only from audible characters with `bCanRespond`. The top eligible candidate must exceed the primary threshold and lead second place by a configurable margin. An exact addressed name may override the normal threshold when that character heard the utterance and may respond. At most one participant receives `PrimaryAddressee`; all remaining audible participants retain their non-primary tier.

Name scoring should recognize names/aliases in likely vocative positions (`Mara, ...`, `Hey Mara`, `... what do you think, Mara?`) rather than awarding a full match whenever a name appears in an unrelated story.

ASR text must pass through the canonical-name resolver before name scoring or history. Registered participant names, spoken aliases, phonetic similarity, gaze, focus, and proximity may resolve `more a` to `Mara`. Once the addressee is selected, strip the vocative from the direct content so the session stores `I want to buy a turnip`, not either spelling of the address. Preserve raw ASR only in diagnostic events. See [SpeechNormalization.md](SpeechNormalization.md).

The same normalization boundary accepts active, explicitly authored lore vocabulary such as `Minas Tirith`. Lore correction changes canonical content but never selects an addressee. Every routed utterance receives a stable `DialogueEventId`, allowing a developer-requested one-turn rollback to remove both the primary exchange and its passive copies from listeners.

Conversational focus supplies natural follow-ups: after Mara responds, the next nearby utterance can remain addressed to Mara without repeating her name. Focus expires after a configurable timeout, excessive distance, loss of eligibility, an explicit different name, or a strong gaze switch.

When top scores are ambiguous, the safe default is no primary inference plus `OnAmbiguousAddressee`. All audible participants still receive their scored hearing events and passive observations. A project may display a subtle UI hint, wake secondary listeners, or require the next utterance to include a name. Do not make every tied character answer automatically.

## Wake criteria

Every audible participant receives its tier event. What “wake” does after that is a game-authored reaction, not another automatic LLM call. Examples:

- the character was named;
- a quest event authorizes interruption;
- a participant owns a relevant gameplay tag;
- the conversation is explicitly addressed to a group;
- the character has a high-priority authored reaction.

Blueprint subscribers may turn a head, enter an attentive animation state, raise an awareness value, request a scripted bark, or do nothing. They can subscribe only to `SecondaryParticipant` and above, listen to every tier, or ignore the event entirely. Even then, the router selects at most one normal LLM responder. Group or interruption dialogue should be an explicit project policy with cooldowns and priority—not emergent model behavior.

## Propagating character replies

The router can bind to subsystem events and accumulate response text by `RequestId`. On `TurnCompleted`:

1. locate the speaking participant from `SessionId`;
2. calculate every participant who heard the character at the configured speaking loudness;
3. score, tier, and emit `OnParticipantHeardDialogue` once for each listener;
4. call `Observe Dialogue For Session` for each listener except the speaker;
5. do not trigger listener inference;
6. allow Blueprint wake reactions and focus changes based on the emitted tier.

This lets a later direct conversation reference something genuinely overheard without spending inference on every nearby character.

## Microphone path

Automatic name selection needs text. For microphone routing:

1. set `bAutoSubmitFinalUtterance = false`;
2. receive `UtteranceCaptured` and its PCM;
3. call standalone `Transcribe Audio`;
4. on `TranscriptionCompleted`, normalize registered names/aliases while preserving the raw transcript in event data;
5. pass canonical text and measured utterance loudness to the router;
6. route the resulting text to the selected session.

This favors STT-first routing even when the primary LLM has native audio input, because proximity/name selection cannot reliably inspect untranscribed audio. A project can still choose gaze-only routing and send native audio when it does not need spoken-name recognition.

## Context and performance controls

- Batch or compact passive observations more aggressively than direct turns.
- Cap retained observations by tokens and age.
- Deduplicate one utterance per listener.
- Never copy an observation into the selected responder as well as submitting the same direct dialogue.
- Keep passive observations out of relationship evidence unless explicitly configured.
- Sanitize observations with the existing jailbreak control-token layer.
- Never start inference merely because a character heard something.

## Remaining promotion work

1. Add the observation history role and `Observe Dialogue For Session` with native/mock tests.
2. Add participant registration, audibility, focus, gaze, proximity, and explicit-name scoring.
3. Route typed dialogue and test deterministic multi-character scenarios.
4. Propagate completed character replies to passive listeners.
5. Integrate microphone transcription and calibrated loudness.
6. Add occlusion, group address, and project wake predicates only after the core behavior is usable.

Promoting the proven demo router into a reusable plugin component remains v1.1
work. The manual `SessionId` APIs remain the deterministic fallback and should
not be removed.
