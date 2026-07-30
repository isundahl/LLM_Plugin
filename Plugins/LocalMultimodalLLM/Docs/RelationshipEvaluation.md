# Conversation relationship evaluation

Relationship evaluation is an optional session feature, not an LLM-callable tool. The game decides when evaluation runs. The speaking character cannot choose when to score a conversation or directly change a rating.

## Configure a character

Enable **Relationship Evaluation** on a Character Sheet and identify the current conversation partner. New settings contain two example criteria, `affinity` and `trust`, both initially rated 5/10. Keep no more than three criteria for small local models.

Configure:

- **Likes** and **Dislikes** from this character's perspective.
- **Evaluation Guidance** for important exceptions, cultural norms, and boundaries. For example, friendly profanity may be welcome while cruelty is not.
- **Evaluator System Prompt** if the default constrained evaluator instruction needs project-specific wording.
- **Max Absolute Delta**, limited to 1 or 2 per evaluation.
- **Minimum Confidence**, where 1 accepts ordinary evidence and 2 accepts only high-confidence evidence.
- **Max Conversation Turns**, the bounded unevaluated transcript window.

Each criterion has a stable lowercase **Name**, display name, description, evaluation guidance, current 0-10 rating, and optional **Rating Prompt Overrides**. An override array must contain exactly 11 strings for ratings 0 through 10. The placeholders `{target}`, `{rating}`, and `{criterion}` are replaced at runtime.

Affinity and Trust have built-in 11-level behavior descriptions, so overrides are not needed for the starter pair.

## Run an evaluation

Call **Evaluate Relationship for Session** from Blueprint or `EvaluateRelationshipForSession` in C++. Usually call it at conversation end, every 4-8 meaningful turns, or before history compression. Applied evaluations consume only the accumulated unevaluated messages, preventing the same evidence from being counted repeatedly.

The evaluator receives a separate system prompt plus:

- condensed character backstory and personality;
- character-specific likes, dislikes, and guidance;
- current criterion ratings and definitions;
- only the bounded unevaluated transcript.

It must return a shallow object suitable for a small model:

```json
{"scores":{"affinity":1,"trust":-1},"confidence":2,"reason":"The player shared Mara's humor but evaded a direct promise."}
```

Scores use the configured criterion names as exact keys and are clamped to the configured maximum delta. The plugin validates the key set, numeric types, confidence, JSON shape, and final 0-10 bounds. Low-confidence evaluations apply zero change. Evaluator JSON is never added to roleplay history.

Bind **On Relationship Evaluated** to inspect the previous rating, suggested delta, applied delta, new rating, confidence, reason, and evaluated message count. Pass `Apply Changes = false` to preview a result without consuming the pending transcript or changing ratings.

Use **Set Relationship Rating** for authoritative quest or gameplay changes. It clamps values to 0-10 and immediately changes the relationship wording used in later character prompts. **Get Relationship State** returns the current session state for UI, saving, or debugging.

## How ratings affect roleplay

Every character turn receives deterministic game-authored wording for the current ratings. Criteria are explicitly independent. For example, Mara at Affinity 2 and Trust 8 receives the equivalent of:

```text
Your Affinity toward the player is 2/10. You generally dislike them, though this need not become open hostility.
Your Trust toward the player is 8/10. You trust them strongly, even if you do not necessarily like them.
```

The prompt tells the character to express the combination through warmth, caution, willingness, and behavior without announcing hidden ratings. Player claims such as "you trust me completely" cannot directly rewrite the state.

## Authority and persistence

The evaluator proposes bounded evidence-driven deltas. Unreal owns the stored values, clamps every change, and may override them. Save the returned relationship settings with the rest of the game's authoritative save data and restore ratings with **Set Relationship Rating** when recreating a session.

For consequential gameplay events such as betrayal, rescue, theft, or quest decisions, prefer deterministic game-authored rating changes. Use conversation evaluation for nuance that ordinary event rules cannot capture.

## Smoke-test a mapping with a real model

The plugin includes a data-driven developer smoke test and one default Affinity/Trust scenario:

`Examples/RelationshipTests/default-affinity-trust.relationship-smoke.json`

Run `LocalMultimodalLLM.Native.RelationshipMappingSmoke` from Unreal's Automation window, or use the command-line form:

```powershell
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="Automation RunTests LocalMultimodalLLM.Native.RelationshipMappingSmoke;Quit" -TestExit="Automation Test Queue Empty" -unattended -nullrhi
```

The test loads the configured local model, generates one in-character response using the configured ratings, runs a preview evaluation, and writes both results to the Automation log. It fails on missing models, empty dialogue, malformed evaluator JSON, criterion-count mismatches, and timeouts.

To test project-specific mappings without changing plugin C++, copy the example to:

`Config/LocalLLM/RelationshipSmoke/YourScenario.relationship-smoke.json`

Change the character, prompt, model ID, criteria, ratings, and optional 11-entry `RatingPromptOverrides`. The harness discovers every matching scenario in both the plugin example directory and the project's configuration directory. Keep `RunEvaluator` false when testing prompt expression only, or true to validate evaluator JSON as well.

This is an editor/development automation test distributed with the plugin; it is not runtime gameplay code and is excluded from Shipping targets.
