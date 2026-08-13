# Speech vocabulary and transcript normalization

Copyright 2026 Ian Sundahl, Volley Studios. Licensed under Apache 2.0.

General ASR accuracy does not solve rare in-world names. A small error such as `Mara` becoming `more a` can break addressee routing and then persist in conversation memory, where the LLM may treat the odd phrase as meaningful. Speech input therefore needs a normalization boundary before routing, history, relationship evidence, or tool parsing.

## Required representation

Keep both forms:

```text
RawTranscript       = "More a want to buy a turnip."
CanonicalTranscript = "I want to buy a turnip."
ResolvedAddressee   = Mara
Corrections         = [{ raw: "More a", canonical: "Mara", confidence: 0.92 }]
```

The raw form is diagnostic/UI data and must not enter normal history or compaction. The canonical form is the only form submitted as direct player dialogue. An optional subtitle UI may show the canonical text and let the player correct it before submission.

When a probable vocative name has selected the addressee, remove the address phrase from the direct message rather than storing a repeated name:

```text
Heard:     "Mara, I want to buy a turnip."
Routed to: Mara
Stored:    "I want to buy a turnip."
```

Other listeners receive a structured observation equivalent to `Player addressed Mara: I want to buy a turnip.` This avoids preserving either `Mara` or an ASR misspelling as unexplained conversational content.

## Contextual vocabulary

Registered conversation participants contribute a small scene-local vocabulary:

- canonical display name;
- explicit spoken aliases;
- optional pronunciation hints;
- optional known ASR variants authored by the project;
- entity type and priority.

The same provider-neutral vocabulary should include explicitly authored in-world terms:

- locations such as `Minas Tirith`;
- factions, cultures, titles, and character names;
- quest-specific people, objects, and events;
- invented spells, materials, creatures, or technical terms.

Use a `Local LLM Speech Vocabulary` Data Asset rather than automatically treating every capitalized word in backstory as correctable. Entries need an entity type, canonical phrase, spoken aliases, pronunciation hints, known ASR variants, priority, and optional activation tags. World/quest/location state activates only the relevant subset. `Minas Tirith` may be high priority in Gondor-related context without biasing an unrelated scene.

Limit automatic character-name candidates to registered audible participants. This prevents a distant character named `May` from rewriting every occurrence of the ordinary word `may`. Non-character proper nouns use their explicit scope and do not affect addressee selection.

Do not automatically learn a new alias from one ASR mistake. A mistaken correction would otherwise poison all later dialogue. Runtime-learned aliases should require repeated high-confidence evidence or explicit player/developer confirmation.

## Two-stage correction

### 1. Recognition bias when supported

Pass nearby canonical names and aliases as optional transcription hints. Each STT provider advertises whether it supports them.

For sherpa-onnx transducers, hotwords require `modified_beam_search`; the current packaged Parakeet backend uses `greedy_search`, so hotwords are not active today. Keep greedy decoding as the shipped baseline. Enabling hotwords is an optional experimental provider configuration requiring modified beam search, active paths, hotword encoding/vocabulary, score tuning, and latency/accuracy/false-bias benchmarks. It is not a prerequisite for normalization and must not silently replace the default backend behavior. Hotwords improve probability but do not guarantee correctness.

Whisper-family and future providers may expose a prompt, hotword, or contextual-token mechanism through the same provider-neutral hint structure. Providers without native bias simply run post-transcription normalization.

### 2. Conservative entity resolution

After transcription, examine likely address positions:

- `Name, ...`
- `Hey Name ...`
- `Name can you ...`
- `... what do you think, Name?`
- the first one or two suspicious words when gaze/focus strongly favors a nearby participant.

Score candidates from:

- normalized edit distance;
- phonetic/pronunciation similarity;
- authored spoken and ASR aliases;
- participant audibility;
- facing and proximity;
- current conversation focus;
- the gap between the best and second-best candidate.

Only rewrite when the best candidate exceeds a confidence threshold and a winner-margin threshold. Name resolution may still help routing at a lower confidence than permanent transcript rewriting. If uncertain, emit an ambiguity event or request player confirmation rather than inventing a correction.

## Precision policy

This is not a global substitution table. The normalizer must never scan an entire sentence and replace every word that resembles a character name.

Correction is allowed only when all required gates pass:

1. **Address span:** The text parser isolates a likely vocative span, such as the words after `hey`, before an opening comma, or after a closing `what do you think, ...?`. Ordinary sentence content is outside the correction region.
2. **Scene candidate:** The canonical target is a registered participant who actually heard the utterance. Distant or unloaded character names cannot become candidates.
3. **Configured identity:** The match resolves to a canonical name, explicit spoken alias, pronunciation hint, or project-authored ASR variant. Fuzzy matching never invents a new entity.
4. **Similarity threshold:** The best phonetic/edit score exceeds a conservative rewrite threshold.
5. **Winner margin:** The best candidate leads the second-best candidate by a configured margin.
6. **World evidence:** Gaze, proximity, or current conversational focus supplies supporting evidence for a fuzzy match. An exact authored alias may require less spatial support.
7. **Ambiguity protection:** Common-word names such as `May`, `Will`, `Rose`, `Ash`, or `Hope` require an explicit address cue, exact alias, or confirmation. They are never corrected from ordinary grammar merely because the character is nearby.

Use two distinct confidence thresholds:

```text
Below routing threshold   -> no name inference; preserve raw text
Routing confidence only   -> may rank an addressee, but do not rewrite transcript
Rewrite confidence        -> resolve and remove the isolated vocative span
Ambiguous top candidates  -> emit confirmation/ambiguity; do not submit automatically
```

Even at rewrite confidence, prefer removing a resolved address span over substituting words inside the sentence. For example:

```text
Raw:       "More a, I want to buy a turnip."
Resolved:  Mara
Submitted: "I want to buy a turnip."
```

The normalizer does not need to rewrite `More a` into `Mara` inside dialogue because the addressee is already carried as structured session metadata.

Non-address proper nouns inside the body may be corrected only from an exact
canonical phrase or exact project-authored ASR variant, matched as a complete
word/phrase span while that vocabulary entry is active. For example, an
authored variant of `Minas Tirith` may map to the canonical location, but a
generic fuzzy resemblance may not rewrite arbitrary sentence content.

Fuzzy body-phrase correction should remain disabled by default. If added later, it requires a higher threshold than vocative routing, a unique active candidate, no collision with common language, and either provider confidence or player confirmation. It must be independently testable and configurable per vocabulary entry.

Every normalization result must retain an audit record with the original span, selected canonical entity, component scores, final confidence, winner margin, and applied action. Blueprint can reject or override the proposed action before submission.

Required negative regression cases include:

- `May I buy a turnip?` must not address May;
- `Will you open the door?` must not address Will;
- `The rose is red` must not address Rose;
- `Brush the ash away` must not address Ash;
- a story mentioning `Mara` must not switch addressee unless it uses an address cue;
- two similarly named audible characters must produce ambiguity rather than an arbitrary correction.

## Implemented API

```text
FLocalLLMSpeechVocabularyEntry
  CanonicalText
  SpokenAliases[]
  PronunciationHints[]
  KnownAsrVariants[]
  EntityId
  EntityType
  Priority
  ActivationTags[]
  bAllowBodyCorrection

FLocalLLMTranscriptNormalizationResult
  RawTranscript
  CanonicalTranscript
  ResolvedAddresseeSessionId
  Corrections[]
  Confidence
  bNeedsConfirmation
```

Suggested operations/events:

- `Set Speech Vocabulary`
- `Add Speech Vocabulary Entry`
- `Normalize Transcript`
- `OnTranscriptNormalized`
- `OnTranscriptAmbiguous`
- `Confirm Transcript Correction`

Create a `Local LLM Speech Vocabulary` Data Asset and call `Set Speech Vocabulary`, or supply entries directly with `Set Speech Vocabulary Entries`. Use `Set Active Speech Vocabulary Tags` as location/quest state changes. The implementation applies exact, complete-span `KnownAsrVariants` only when `bAllowBodyCorrection` is enabled and every activation tag is active. It emits `TranscriptNormalized` with an audit record, or `TranscriptAmbiguous` and pauses automatic submission when active entries claim the same variant. Fuzzy body correction remains intentionally unsupported.

The project-only MetaHuman demo exposes `Demo Speech Vocabulary Entries` on its
coordinator and installs them before STT is loaded. Its default example safely
maps the exact variants `Tar Row` and `Tarro` to `Taro`; it deliberately does not
map `Tara`, because that is a valid name. Projects should normally move equivalent
entries into a reusable `Local LLM Speech Vocabulary` Data Asset.

## Do not make the model globally uncertain

The text model does not receive audio; without provenance, it reasonably treats the transcript as the player's exact message. However, adding a standing instruction that every player message may be wrong could make the character second-guess correct dialogue.

The default solution is pre-model normalization and confirmation, not generalized doubt:

- high-confidence safe corrections enter as canonical text;
- ambiguous corrections pause for Blueprint/player confirmation;
- low-confidence unmatched text remains unchanged or is not submitted, according to project policy;
- the system prompt is not globally told to distrust all player speech.

If an STT provider exposes trustworthy span confidence, a project may label only the affected unresolved span as low confidence. Do not fabricate confidence that the provider did not return, and do not ask the LLM to repair the entire sentence.

## One-turn rollback

`Undo Last Conversation Turn For Session` lets a developer offer a “correct last speech” action. It rolls back an atomic turn, not only the player line while leaving an assistant reply derived from it.

Before committing each direct turn, retain one lightweight session checkpoint containing:

- recent history boundary;
- compacted-memory state if compaction changed during the turn;
- pending relationship evidence boundary;
- plugin-owned relationship ratings when configured for rollback;
- pending internal tool-call state;
- a stable `DialogueEventId` shared by routed passive observations.

Undo restores the checkpoint, removes the player input and every assistant/tool-continuation message derived from it, invalidates pending calls, and emits `ConversationTurnUndone`. The corrected canonical transcript can then be resubmitted.

For ambient conversations, `Undo Dialogue Event(DialogueEventId)` should also retract the linked passive observation from every listener session. Otherwise Mara may forget the mistaken line while nearby characters continue remembering it.

Rollback cannot generically reverse external Unreal side effects already executed by a tool, audio already played, animations, inventory changes, or quest mutations. The result event must report whether the removed turn had an executed tool so the game can deny rollback or run its own compensating action. Relationship evaluations or save operations performed after the turn also require an explicit project policy.

The implementation keeps exactly one committed rollback checkpoint per session. `ConversationTurnUndone` includes the stable `DialogueEventId`, removed-message count, restored relationship state, and `bTurnHadExecutedTool`. External Unreal effects still require a project-authored denial or compensating action.

## Memory and evaluator rules

- Never store the raw ASR text after a correction is accepted.
- Never store both raw and canonical versions in character history.
- Use canonical text for direct relationship evidence.
- Passive observers receive structured speaker/addressee metadata, not an unexplained raw string.
- Preserve raw text only in transient diagnostics unless the project deliberately logs it.
- Label unresolved speech as fallible transcription so the character is instructed not to obsess over isolated odd wording.
- Do not let the LLM itself rewrite the transcript before routing; it may invent content or choose the wrong character.
- Retract linked passive observations when a routed speech turn is undone.

## Recommended delivery order

1. Add the scoped speech-vocabulary asset and canonical/spoken aliases to participant registration.
2. Add deterministic vocative extraction and exact alias matching.
3. Add exact, explicitly authored ASR-variant correction for active body proper nouns.
4. Add conservative edit-distance and phonetic scoring for vocatives using proximity/gaze/focus priors.
5. Submit only canonical text and retain raw text in events.
6. Add one-turn session/dialogue-event rollback.
7. Add provider-neutral transcription hints.
8. Optionally benchmark sherpa modified-beam hotwords against the current greedy Parakeet path; retain greedy unless measured benefits outweigh latency and false-bias regressions.
9. Add optional player confirmation for ambiguous corrections.

The post-transcription resolver is the essential protection because it works across Parakeet, Whisper, and future providers. Native hotwords are an optional optimization experiment, not the default or the sole solution.
