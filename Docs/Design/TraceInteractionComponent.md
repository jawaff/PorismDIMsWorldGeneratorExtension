# TraceInteractionComponent

## Purpose
`UPorismTraceInteractionComponent` is the reusable actor-attached trace component for shared actor and chunk-world block interaction.

It provides:
- one trace loop that can resolve either an interactable actor or a chunk-world block
- block identity through `BlockTypeName` gameplay tags
- a shared actor interaction contract through `UPorismInteractableInterface`
- an optional actor-side view-provider interface through `UPorismInteractionTraceViewProviderInterface`
- start/end/update delegates for actor and block interaction state
- a block custom-data initialization delegate for the currently focused block
- debug trace drawing and block lookup diagnostics

## Main Types
- `UPorismTraceInteractionComponent`
- `UPorismInteractionTraceViewProviderInterface`
- `UPorismInteractableInterface`
- `FPorismTraceInteractionResult`
- `FChunkWorldBlockInteractionResult`
- `FChunkWorldResolvedBlockHit`

## Current Behavior
- Traces from the owning actor's view point on a timer or when `ForceTrace()` is called.
- Resolves the best current target as:
  - no target
  - interactable actor
  - chunk-world block
- Uses `UChunkWorldHitBlueprintLibrary` to resolve block hits and `BlockTypeName`.
- Executes actor interaction through `UPorismInteractableInterface` and built-in RPC helpers.
- Uses `FGameplayTag` interaction tags for shared actor interaction execution.
- Exposes:
  - `OnTraceInteractionUpdated`
  - `OnInteractionSucceeded`
  - `OnActorInteractionStarted`
  - `OnActorInteractionEnded`
  - `OnBlockInteractionStarted`
  - `OnBlockInteractionEnded`
  - `OnBlockInteractionUpdated`
  - `OnBlockCustomDataInitialized`

The component intentionally does not own:
- block health
- block damage
- destructibility rules
- project-specific UI payloads

Those should be resolved downstream from `BlockTypeName`, `FChunkWorldResolvedBlockHit`, or the block schema helpers.

## How To Use
1. Add `UPorismTraceInteractionComponent` to a pawn or actor that should own interaction tracing.
2. Set `TraceChannel` for actor interaction and `BlockTraceChannel` when block interaction should use a different collision channel.
3. If your project needs actor interaction, implement `UPorismInteractableInterface` on the target actor.
4. Bind to the component delegates for UI or gameplay reactions.
5. Call `Interact(InteractionTag)` when the player confirms the current actor interaction target.

For block interaction:
1. Bind to `OnBlockInteractionStarted` or `OnBlockInteractionUpdated`.
2. Read `BlockTypeName` from `FChunkWorldBlockInteractionResult`.
3. Use `ResolvedBlockHit` or `UChunkWorldHitBlueprintLibrary` to continue into schema lookup when you need definition or custom-data payloads.

## Actor Integration
Actors that want to participate in shared interaction should implement `UPorismInteractableInterface`.

The interface provides:
- `CanHandleInteraction`
- `HandleInteractionByPawn`
- `HandleLocalInteractionByPawn`

Use `CanHandleInteraction` for gating.
Use `HandleInteractionByPawn` for authoritative gameplay behavior.
Use `HandleLocalInteractionByPawn` for local cosmetic confirmation.
Both shared interaction callbacks now receive an `FGameplayTag` interaction tag instead of a string.

## Block Integration
The block branch uses `BlockTypeName` as the canonical identity.

Typical block flow:
1. `UPorismTraceInteractionComponent` resolves `FChunkWorldBlockInteractionResult`.
2. The result carries `BlockTypeName` and `FChunkWorldResolvedBlockHit`.
3. Downstream code uses `UChunkWorldHitBlueprintLibrary` or `UBlockTypeSchemaComponent` to resolve definition or custom data.

If you need to react when the focused block has runtime custom data available, bind `OnBlockCustomDataInitialized`.
This fires when:
- the newly focused block is already initialized
- or the focused block becomes initialized while it remains the active interaction target

This keeps the trace component aligned with [BlockTypeSchema.md](./BlockTypeSchema.md).

## View Resolution
By default the component uses:
1. `UPorismInteractionTraceViewProviderInterface` on the owner when implemented
2. the owning pawn controller view when available
3. `GetActorEyesViewPoint()`

If your project uses a custom camera source, prefer implementing `UPorismInteractionTraceViewProviderInterface` on the owning actor instead of creating a component subclass just for view routing.

## Debugging
- Enable `bDebugDrawTrace` to draw the trace line and impact points.
- Enable `bDebugDrawBlockLookup` to draw the resolved block cube.
- Enable `bLogBlockLookupDiagnostics` to log block resolution failures and successes.

## Intended Use
Use this component as the shared interaction entry point for projects built on the extension plugin.

Keep project-specific behavior outside the component, for example:
- project-specific view routing
- project-specific block UI payloads
- project-specific damage handling
- project-specific wrapper delegates for legacy systems

## Damage Extension
`UPorismDamageTraceInteractionComponent` extends the shared trace component with health-aware block payloads and shared damage helpers.

The layering is intentional:
- `UPorismTraceInteractionComponent` stays the generic focus detector
- `UPorismDamageTraceInteractionComponent` interprets the currently focused block through shared health state
- `UPorismPredictedBlockStateComponent` owns the shared read/apply/reconciliation path for block health

The damage component does not replace the base trace contract. It listens to the base block lifecycle and only becomes active when the focused block can produce a health view through `UPorismPredictedBlockStateComponent`.

It adds:
- `FChunkWorldDamageBlockInteractionResult`
- `OnDamageBlockInteractionStarted`
- `OnDamageBlockInteractionEnded`
- `OnDamageBlockInteractionUpdated`
- `OnDamageBlockCustomDataInitialized`
- `ApplyDamageToCurrentBlock(...)`

The damage result includes:
- `bSupportsHealth`
- `bUsingPredictedHealth`
- `bHasCustomData`
- `bHasAuthoritativeHealth`
- `bIsDestructible`
- `bIsInvincible`
- `CurrentHealth`
- `MaxHealth`

### Damage Event Semantics
- `OnDamageBlockInteractionStarted` fires when the currently focused block becomes damage-displayable.
- `OnDamageBlockInteractionUpdated` fires when the same focused block stays active and one of the displayable damage fields changes.
- `OnDamageBlockCustomDataInitialized` fires once per focused block when runtime custom data becomes initialized while that block remains focused.
- `OnDamageBlockInteractionEnded` fires when the active focused damage block is lost or changes to another block.

The component caches one focused damage-state record and emits transitions from that record. It does not treat every generic trace refresh as a damage update, and it ignores tracked block-state change notifications for blocks that are not currently focused.

### Damage Apply Helper
`ApplyDamageToCurrentBlock(...)` is a convenience wrapper for focused interaction flows. It builds a shared `FChunkWorldBlockDamageRequest` and forwards it into `UPorismPredictedBlockStateComponent`.

That means:
- authority-owned callers apply real damage immediately
- client-owned callers write predicted health, optionally play immediate local feedback, and queue an aggregated authoritative flush
- the trace interaction component itself is not the main damage system API
