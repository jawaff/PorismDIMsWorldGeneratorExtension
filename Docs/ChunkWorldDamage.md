# ChunkWorldDamage

## Purpose
`UChunkWorldBlockDamageBlueprintLibrary` and `UPorismPredictedBlockStateComponent` provide the shared chunk-world block damage route for projects using the extension plugin.

They provide:
- authoritative block damage application from `FChunkWorldResolvedBlockHit`
- local-only predicted health results without mutating client chunk-world custom data
- schema-aligned health and invincibility reads through `FBlockDamageDefinition` and `FBlockDamageCustomData`
- a damage-aware interaction component for health-capable blocks
- replicated nearby hit, destroy sound, and destroy Niagara feedback
- event-driven retirement of predicted values when authoritative custom-data replication arrives

## Main Types
- `UChunkWorldBlockDamageBlueprintLibrary`
- `UPorismDamageTraceInteractionComponent`
- `UPorismPredictedBlockStateComponent`
- `UChunkWorldBlockFeedbackComponent`
- `FChunkWorldBlockDamageResult`
- `FChunkWorldResolvedBlockHit`
- `FBlockDamageDefinition`
- `FBlockDamageCustomData`

## Required Schema
The shared damage path assumes the resolved block uses the plugin damage schema family.

Required definition family:
- `FBlockDamageDefinition`
  - `MaxHealth`
  - `HitSound`

Required custom-data family:
- `FBlockDamageCustomData`
  - `Health`
  - inherited `bInvincible`

Optional destroy feedback comes from `FBlockDefinitionBase`:
- `DestroyedSound`
- `DestroyedEffect`

If the resolved block does not use those schema families, the shared damage helpers return `false` and do nothing.

## Current Behavior
- Applies authoritative damage through `TryApplyBlockDamageForResolvedBlockHit(...)`.
- Calculates local-only predicted damage through `TryApplyPredictedBlockDamageForResolvedBlockHit(...)`.
- Reads current health and invincibility through `TryGetCurrentBlockHealthStateForResolvedBlockHit(...)`.
- Initializes block custom data on demand for authoritative writes only.
- Honors `bInvincible` from `FBlockCustomDataBase`.
- Removes the block when health reaches zero.
- Reports `BlockTypeName` in `FChunkWorldBlockDamageResult`.
- Plays non-lethal hit feedback from `FBlockDamageDefinition::HitSound`.
- Plays destroy feedback from `FBlockDefinitionBase::DestroyedSound` and `FBlockDefinitionBase::DestroyedEffect`.
- Replicates feedback through `UChunkWorldBlockFeedbackComponent`.
- Retires predictions when authoritative custom-data replication is applied for the same `ChunkWorld + BlockWorldPos`.
- Falls back to timeout cleanup for stale predictions.

The shared path intentionally does not own:
- collision routing
- melee dedupe
- combat damage calculation
- team filtering
- project-specific UI payloads

## How To Use
1. Resolve a block hit into `FChunkWorldResolvedBlockHit` with `UChunkWorldHitBlueprintLibrary`.
2. Apply damage with `UChunkWorldBlockDamageBlueprintLibrary`.
3. For local UI, store predicted results in `UPorismPredictedBlockStateComponent`.
4. Read current block health through `UPorismPredictedBlockStateComponent` when prediction is in use, or directly from the damage library when it is not.

If your interaction flow should only target health-capable blocks, use `UPorismDamageTraceInteractionComponent` instead of `UPorismTraceInteractionComponent`.

`UPorismDamageTraceInteractionComponent`:
- only accepts block targets that support the shared damage schema family
- exposes `FChunkWorldDamageBlockInteractionResult`
- exposes `ApplyDamageToCurrentBlock(...)`
- reads prediction through `UPorismPredictedBlockStateComponent` when present on the owner

Typical authoritative flow:
1. Resolve `FChunkWorldResolvedBlockHit`.
2. Compute project-specific damage outside the plugin.
3. Call `TryApplyBlockDamageForResolvedBlockHit(...)`.
4. Inspect `FChunkWorldBlockDamageResult` for:
   - `bAppliedDamage`
   - `bWasInvincible`
   - `bDestroyed`
   - `PreviousHealth`
   - `NewHealth`
   - `BlockTypeName`

Typical predicted flow:
1. Resolve `FChunkWorldResolvedBlockHit`.
2. Compute project-specific damage outside the plugin.
3. Call `TryApplyPredictedBlockDamageForResolvedBlockHit(...)`.
4. Store the returned result in `UPorismPredictedBlockStateComponent`.

## Feedback Replication
`UChunkWorldBlockFeedbackComponent` is the shared replicated feedback host for chunk-world block damage.

Current behavior:
- multicasts authoritative feedback from the chunk world
- applies a local distance filter before spawning sound or Niagara
- supports:
  - hit sound only for non-lethal damage
  - destroyed sound and destroyed Niagara for block removal

Chunk worlds that want shared feedback should own a `UChunkWorldBlockFeedbackComponent`.

`AChunkWorldExtended` creates one by default.
`ASpellbladeChunkWorld` now also creates one so Spellblade can consume the same shared route.

## Prediction Model
`UPorismPredictedBlockStateComponent` is the local-only prediction cache.

Use it when:
- a locally controlled pawn needs predicted block health for UI or gameplay

Current behavior:
- stores predicted results by `ChunkWorld + BlockWorldPos`
- overwrites older predictions for the same block
- prefers predicted values over authoritative reads until reconciliation
- never writes authoritative values into the prediction cache
- retires predictions on:
  - first authoritative replicated custom-data update for the same block
  - timeout fallback

The server should not use prediction state.

## Reconciliation
The shared reconciliation path is event-driven.

Current route:
1. authoritative custom-data replication is applied by the chunk world
2. chunk-world code calls `UPorismPredictedBlockStateComponent::NotifyAuthoritativeBlockStateUpdated(...)`
3. matching predictions for that `ChunkWorld + BlockWorldPos` are retired

This avoids chunk scans and keeps the prediction cache bounded.

## Intended Use
Use this shared damage route when your project:
- already uses the plugin schema system
- wants block damage keyed by `BlockTypeName`
- wants shared hit and destroy feedback replication
- wants local predicted health without client-side custom-data mutation

Keep project-specific behavior outside the plugin, for example:
- collision-manager routing
- damage shaping from stats, tools, spells, or weapon rules
- project-specific UI presentation
- project-specific actor interaction and swap behavior
