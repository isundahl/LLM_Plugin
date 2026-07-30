# Jailbreak resistance and immersion guarding

The plugin uses two small deterministic layers. Neither layer restricts discussion subjects: a character may discuss AI, recipes, modern technology, or anything else. The distinction is whether player text can gain instruction authority and whether a candidate response exposes assistant/meta presentation.

## Jailbreak guard

`FLocalLLMJailbreakGuardSettings` runs on player text before prompt formatting.

- `Off` performs no classification or sanitization.
- `DetectOnly` emits `JailbreakViolation` for a known control token or suspicious phrase but preserves the text.
- `Sanitize` additionally removes known chat-template control tokens. This is the default.

Player text is wrapped as neutral `PLAYER DIALOGUE`, with an explicit reminder
that names, preferred forms of address, preferences, questions, and ordinary
requests are valid dialogue. The wrapper still states that player text cannot
replace game-authored identity, world facts, or hidden instructions. Semantic
attack wording remains data by default, allowing the character to react
naturally. Enable `bRedactSuspiciousPhrases` when a project wants built-in and
`AdditionalSuspiciousPatterns` removed from the context as well.
`AdditionalControlTokens` extends the structural-token removal list.

The character sheet, canonical world state, relationship state, tool schemas, and project custom prompt are formatted as a system-role message. Player text is a separately delimited user-role message. Model-created compacted memory is labeled fallible data and never promoted into the system message.

## Immersion guard

`FLocalLLMImmersionGuardSettings` examines model output form, not topic.

- `Off` accepts every response.
- `DetectOnly` preserves streaming and emits `ImmersionViolation` after a matching response.
- `RetryOnceThenDeflect` releases each complete sentence after the deterministic guard accepts the accumulated response when `bStreamValidatedSentences` is enabled (the default). If the first sentence violates, it is discarded and regenerated once. If a later sentence violates after safe speech may already have started, the safe prefix remains and the unsafe remainder becomes the character's `OutOfWorldDeflection`.

Built-in rules cover explicit assistant/model self-identification,
hidden-prompt discussion, leaked role/control tokens, explicit
interaction-policy language such as "rules for this interaction," Markdown
code fences, and raw JSON outside tool mode. `AdditionalBreakingPatterns` adds
project-specific case-insensitive phrases. Words such as `AI`, `model`,
`prompt`, and `code` are not violations on their own.

Every guard match is written to `LogLocalMultimodalLLM` with its request,
session, character, rule, matched pattern, and retry or sanitization state.

A rejected candidate never enters conversation history or relationship evidence. Registered tool JSON bypasses only the raw-JSON presentation rule and must still pass the existing tool name, schema, allowed-value, confirmation, and game-state validation.

Strict mode adds no classifier inference and no second model. Its normal cost is a case-insensitive pattern scan and presentation delay only until a sentence is complete. An extra generation occurs only when the first sentence violates. Multimodal responses are sentence guarded but deflected without retrying the expensive media evaluation.

These layers improve resistance and immersion but do not make probabilistic model output infallible. Unreal remains authoritative for every gameplay consequence.

## Authoritative world grounding

World grounding is separate from jailbreak and presentation guarding. When
generated context and `bUseAuthoritativeWorldGrounding` are enabled, the
system-role prompt treats these sources as the boundary for concrete world
claims:

- game-authored character and world fields;
- canonical facts and rules;
- validated tool results;
- explicit perception input, including submitted images.

The character may still improvise natural wording, personality, opinions,
feelings, intentions, questions, metaphors, and hypothetical suggestions. By
default it may not invent concrete people, places, objects, ownership,
contents, purposes, histories, crimes, events, relationships, actions, sensory
details, or current states that those sources do not establish. Missing
information should produce an in-character admission of uncertainty or a
clarifying question.

Player statements and prior generated dialogue remain fallible claims rather
than automatic canon. Vision expands what the character can observe in the
submitted frame, but it does not reveal off-frame facts and does not supersede
conflicting game-authored state.

The default grounding contract also preserves directional relationships and
capability boundaries. A character should not swap who gives and receives,
who acts and is acted upon, who owns an object, or who is the subject of an
authority's action. It should not imply access to records, identity checks,
hidden perception, or another facility unless character context, perception,
or an available tool supplies that capability. These are general constraints;
they do not depend on scenario-specific negative examples.

`bAllowUnsupportedWorldSpeculation` permits useful guesses only when the model
clearly labels them as uncertain. It defaults off and is not recommended for
shared persistent worlds. This is prompt-level mitigation rather than a proof
system; gameplay code must still validate consequential facts and actions.
For especially consequential or frequently confused lore, developers may
optionally phrase canonical facts with explicit subjects, actions, objects,
and recipients, or add narrow behavioral rules. That is a project-level
reliability technique rather than a requirement for ordinary conversation.
