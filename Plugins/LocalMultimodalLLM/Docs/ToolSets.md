# Safe embodied-character tool sets

Copyright 2026 Ian Sundahl, Volley Studios. Licensed under Apache 2.0.

## Starter kit

`DA_ExampleLocalLLMToolSet` deliberately grants only three reversible, low-impact requests:

- `MoveToTarget(target_id)`: walk toward a nearby target that the game advertised as actionable.
- `FaceTarget(target_id)`: turn toward an advertised nearby target.
- `PlayGesture(gesture)`: play one cosmetic gesture from `Nod`, `ShakeHead`, `Wave`, `Point`, or `Shrug`.

The asset is an allow-list of requests, not a list of functions the model can invoke. The model cannot supply an object path, animation asset, arbitrary Unreal function name, speed, destination coordinates, or persistent state change.

## Map requests to Unreal

1. Add a `Local LLM Tool Executor Component` to a persistent actor such as your dialogue manager.
2. Assign `DA_ExampleLocalLLMToolSet` and leave **Register Tool Set on Begin Play** enabled.
3. Bind **On Tool Call** and use an explicit **Switch on String** for `ToolName`.
4. Parse the validated `ArgumentsJson`.
5. Resolve `target_id` through a game-owned registry containing only targets advertised to this character for this interaction.
6. Recheck current authority, distance, navigation, animation state, and permissions.
7. Execute the fixed Blueprint or C++ branch. Example mappings are `AI MoveTo`/`AAIController::MoveToActor`, controller focus or custom facing logic, and a game-selected animation montage.
8. When the action actually finishes or fails, call **Complete Tool Call** with a small JSON result. Call **Reject Tool Call** for an unknown, stale, unreachable, busy, or forbidden target.

Do not dynamically call a `UFunction` whose name came from the model. Do not turn `target_id` into an object path. The explicit branch and game-owned lookup are security boundaries.

## MetaHuman showcase integration

The project's `MetaHumanLLMDemoCoordinator` now provides a concrete implementation of the starter contract:

- It registers `MoveToTarget`, `FaceTarget`, and `PlayGesture` when **Enable Safe Embodied Tools** is enabled.
- `player` is the only automatic target. Add stable IDs to **Approved Action Targets** for level-owned markers or actors.
- Runtime-spawned or discovered actors can be added with **Register Approved Action Target**; this refreshes the model-visible target allow-list without exposing an object path.
- Facing is immediate and range checked. Movement is short, collision swept, capped by **Maximum Action Distance**, and reports success only after arrival.
- Each character exposes optional attentive, ambient, and speaking animations plus a **Gesture Animations** map. Gesture keys must be one of `Nod`, `ShakeHead`, `Wave`, `Point`, or `Shrug`; missing clips cause a rejected tool result rather than an invented action.
- The existing idle is phase- and speed-varied between characters. While TTS is audible it plays slightly faster unless a speaking loop is assigned.
- `Play Character Gesture` and `Face Character Toward Actor` expose the same paths directly to Blueprint for deterministic scripted interactions.

The included project currently contains locomotion and idle sequences but no suitable gesture clips. Import Manny-compatible gesture sequences, then assign them per character on the coordinator. They are retargeted through the same hidden Manny source already used for the MetaHuman idle.

## Provide actionable targets

Before inference, context should contain a small, session-specific list such as:

```text
Available character actions:
- Face or walk to target `player` (nearby person)
- Face or walk to target `map_table` (reachable location)
- Gestures: Nod, ShakeHead, Wave, Point, Shrug
```

Use opaque stable IDs, advertise only relevant targets, and cap the list. An ID is permission to request an action, not proof that the action remains valid; validate again when the request arrives.

## State ownership

The starter kit excludes inventory changes, combat, spawning, quest mutation, level travel, console commands, arbitrary animation selection, and relationship-score mutation. Dialogue describing an action is never proof that it occurred.

Karma and relationships should be game-owned. Prefer deterministic updates from gameplay events to bounded dimensions such as affinity, trust, fear, and respect. Give the model a concise summary so it can portray the relationship, but do not expose `SetKarma`, `SetLikeability`, or unrestricted numeric-delta tools.

## Change or extend the set

Open the Tool Set Data Asset and edit its **Tools** array. Each tool contains a stable name, model-facing description, typed parameters, required flags, exact allowed string values, and an optional player-confirmation flag. Re-register the Tool Set after an edit or restart Play In Editor.

Treat every added capability as untrusted input from a fallible model:

1. Prefer one narrow intent over a generic command or function executor.
2. Use allow-listed values and game-issued IDs.
3. Enforce server authority and current world rules after schema validation.
4. Keep persistent or irreversible operations out of the default set; require explicit confirmation where appropriate.
5. Add timeouts, cancellation, rate limits, and a per-character action queue.
6. Return the real outcome so later dialogue cannot assume a failed action succeeded.
7. Test malformed arguments, stale targets, repeated calls, prompt injection, and requests made while the character is busy.

Expanding the allow-list increases behavioral and narrative risk. The plugin makes requests structured and constrained, but no model should be treated as an authoritative gameplay system.

## Regenerate the example asset

`Scripts/CreateExampleToolSet.py` recreates or updates the asset through an Unreal Python commandlet after the plugin is compiled. `Examples/ToolSets/example-tool-set.json` is a readable documentation mirror and is not loaded at runtime.
