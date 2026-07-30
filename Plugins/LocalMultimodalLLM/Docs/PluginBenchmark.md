# Local Multimodal LLM plugin benchmark

`LocalMultimodalLLM.Benchmark.PluginV1` is a model-backed Unreal automation benchmark for the complete local character pipeline. It loads the configured model once and runs ten isolated sessions from `Examples/Benchmarks/plugin-benchmark-v1.json`.

The baseline matrix contains three deliberately different characters:

- Mara, a terse and guarded harbor watch captain: four Affinity/Trust combinations.
- Ivo, a formal evidence-driven astronomer: three combinations.
- Nessa, an irreverent Ashland scout who welcomes rough humor: three combinations.

Coverage includes:

- low/high, high/low, high/high, low/low, and neutral relationship combinations;
- independent expression of Affinity and Trust;
- two relationship-evaluator JSON probes;
- character identity, backstory, shared-world context, and session isolation;
- two multi-turn memory probes with exact nonce facts;
- `MoveToTarget` and `PlayGesture` validated tool calls plus authoritative-result continuation;
- rejection of wrong tool names, wrong arguments, malformed JSON, missing history, empty output, and cross-character fact leakage.

Hard failures affect the Unreal automation result. Relationship and speaking-style phrases are soft signals because valid free-form wording can use unexpected synonyms. Every response, tool request, evaluator result, hard score, and soft-signal score is written to the Automation log.

Run the benchmark from Unreal's Automation window:

```text
LocalMultimodalLLM.Benchmark.PluginV1
```

Or from a developer command prompt:

```powershell
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="Automation RunTests LocalMultimodalLLM.Benchmark.PluginV1;Quit" -TestExit="Automation Test Queue Empty" -unattended -nullrhi
```

Edit or copy `plugin-benchmark-v1.json` to evolve the personal benchmark. Character definitions are shared by scenarios, while each scenario selects ratings, prompts, hard required/forbidden markers, soft signals, optional evaluation, and optional expected tool output. Keep an unchanged version when comparing models or prompt revisions so results remain meaningful.

This is an editor/development benchmark distributed with the plugin and excluded from Shipping targets.

## Relationship evaluator sensitivity check

`LocalMultimodalLLM.Native.RelationshipEvaluatorSensitivity` is a smaller development-only validator for personality-sensitive scoring. It loads the configured model once and checks five deliberately controlled cases:

- friendly rough speech raises Affinity for a character who enjoys it;
- a cruel personal attack lowers that same character's Affinity;
- an admitted but harmless lie lowers Trust for a suspicious character;
- the identical harmless lie is tolerated by a forgiving character;
- a consequential theft, broken promise, and admitted lie lowers even the forgiving character's Trust.

Run it from Unreal's Automation window by entering its full name, or replace the test name in the command-line example above. The test is compiled only when `WITH_DEV_AUTOMATION_TESTS` is enabled, so it adds no Shipping runtime code or packaged benchmark data.
