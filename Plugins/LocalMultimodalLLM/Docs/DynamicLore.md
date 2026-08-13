# Dynamic lore and developed canon

Copyright 2026 Ian Sundahl, Volley Studios. Licensed under Apache 2.0.

Static character sheets and `CanonicalFacts` are the durable authoring source.
Dialogue, compacted memory, and model output never become canon automatically.

The optional `FLocalLLMWorldContext.DynamicLore` ledger holds validated runtime
facts. Every entry has a plugin-managed category, source, revision, and
visibility scope:

- `CharacterPrivate`: visible only to `TargetCharacterId`;
- `Area`: visible to sessions whose `ActiveKnowledgeAreas` contains `AreaId`;
- `Global`: visible to every character session.

Use `SetSessionKnowledgeAreas` when a character enters or leaves a level,
district, room, faction channel, or other project-defined knowledge region.
Area IDs are semantic identifiers; they do not need to match Unreal package
names.

## Categories

The plug-in owns the category enum:

- `PersonalPreference`
- `PersonalHabit`
- `PersonalOpinion`
- `CurrentState`
- `LocationDetail`
- `WorldEvent`
- `GameAuthored`

Game code may commit any category through `UpsertDynamicLoreFact`. A character
may create only the first three, only about itself, and only in
`CharacterPrivate` scope. This prevents a model from creating world history,
events, possessions, relationships, locations, or facts about somebody else.

To share a private character-developed fact, retrieve it, change its source to
`Game`, select `Area` or `Global`, provide the required area when applicable,
and upsert the same `FactId`. Promotion is therefore an explicit game decision.

## Character proposals

Set `CharacterProfile.DevelopedCanon.bEnableCharacterProposals` to expose the
built-in `ProposeDevelopedFact` tool. The model can propose a concise
preference, habit, or personal opinion. Saying a fact in ordinary dialogue does
not save it.

By default, proposals arrive through the normal `OnToolCall` event for project
approval. Commit an accepted value with `CommitCharacterDevelopedFact`, then
return the decision through `SubmitToolResult`. Set
`bAutoCommitCharacterProposals` only when schema and size validation are enough
for the project; semantic category mistakes remain possible with a small model.
Automatic commits are still private and cannot create shared world lore.

The feature defaults off. The plug-in reserves the tool name
`ProposeDevelopedFact`; projects cannot replace it with a normal registered
tool.

## Budgets

`DevelopedCanon` independently limits:

- stored character facts;
- characters per fact;
- dynamic-lore tokens inserted into that character's prompt.

The dynamic section still counts toward the normal generated-context budget.
Changing world or character lore invalidates the affected prompt cache so the
next turn cannot reuse stale context.

## Blueprint and persistence

The component and subsystem expose:

- `Upsert Dynamic Lore Fact`
- `Remove Dynamic Lore Fact`
- `Clear Dynamic Lore Facts`
- `Get Dynamic Lore Facts`
- `Get Visible Dynamic Lore Facts For Session`
- `Commit Character Developed Fact`
- `Set Session Knowledge Areas`

`DynamicLore` and its fields are marked `SaveGame`, but the plug-in does not
choose a save slot or write project files. Copy the returned array into the
game's own `USaveGame` object and restore it through `SetSharedWorldContext`
before creating or preparing character sessions.

Static facts should remain in data assets or project-authored configuration.
Use dynamic lore only for facts whose runtime revision is meaningful.
