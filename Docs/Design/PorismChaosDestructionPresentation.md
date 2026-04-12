# Porism Chaos Destruction Presentation

## Purpose

`Plugins/PorismDIMsWorldGeneratorExtension/` includes a reusable Chaos destruction presentation actor hierarchy for chunk-world block removal visuals triggered through `ChunkWorldDestructionActorInterface`.

Use this when a block definition needs an authored destruction actor that:

- spawns from `AChunkWorldExtended` block destruction
- presents a fractured `GeometryCollection`
- can be tuned per class or per Blueprint subclass
- supports runtime diagnostics for fracture tuning

## Networking requirement

Multiplayer destruction presentation in `Plugins/PorismDIMsWorldGeneratorExtension/` uses ordinary replicated trigger state.

Why:

- destruction actors replicate one trigger payload with ordinary property replication
- the authority updates `ReplicatedTriggerState`, increments its trigger serial, and calls `ForceNetUpdate()`
- clients replay the same local startup path from `OnRep_ReplicatedTriggerState()`

Current plugin behavior:

- networked `AChunkWorldExtended` instances do not impose a push-model-specific startup requirement for destruction presentation

## Actor hierarchy

- `AChunkWorldChaosDestructionPresentationActor`
  - reusable base actor
  - owns the shared scene root, geometry collection component, field system component, staged collision behavior, and runtime diagnostics
- `AChunkWorldChaosGentleBreakApartActor`
  - project-facing default subclass for heavy "break and slump" behavior
  - uses uniform external strain by default
  - keeps the optional separation impulse off by default
- `AChunkWorldChaosGentleReleaseActor`
  - minimal unlock-only subclass for pre-fractured rocks that should simply separate a little and fall under gravity
  - keeps the optional separation impulse disabled
  - uses temporary bottom support and delayed world-collision response restoration to avoid immediate kickout
- `AChunkWorldChaosTreeChopActor`
  - tree-focused subclass for a pre-fractured trunk with a single low cut plane
  - keeps the stump anchored for the full presentation by default
  - resolves the strain field near an explicit bottom-measured chop plane instead of using full-tree height fractions
  - now defaults to a lower-energy uniform strain release with delayed gravity re-enable so the top falls after the fracture window instead of blasting outward immediately

## Component setup

The base actor owns:

- a shared scene root so the geometry collection can be offset locally
- a `UGeometryCollectionComponent` for the visible destruction presentation
- a `UFieldSystemComponent` for transient Chaos field dispatch

The geometry collection component is configured for physics simulation, gravity, and heavier damping so released chunks settle faster instead of reading as a blast.

## Tuning surface

All runtime tuning is exposed through `FChunkWorldChaosDestructionPresentationTuning`.

Important fields:

- `GeometryCollectionAsset`
  - fractured collection used for the presentation
- `GeometryCollectionRelativeOffset`
  - local offset under the actor root
- `ExternalStrainMagnitude`
  - primary fracture control
  - compare this against the Geometry Collection `Damage Threshold` array
  - keep it above the first cluster level you want to break
  - when using radial falloff, allow extra headroom because only the center receives the full value
- `FractureFieldMode`
  - `Uniform` is the recommended mode for heavy break-and-slump behavior
  - `RadialFalloff` is better suited for directional or impact-like fracture patterns
- `bApplyGentleSeparationImpulse`
  - optional post-break nudge
  - leave this `false` for the gentle-break subclass unless a specific asset needs extra chunk separation
- `bDelayCollisionUntilAfterFracture`
  - temporarily ignores `WorldStatic` and `WorldDynamic` during the immediate fracture window to reduce overlap kickout
- `CollisionEnableDelaySeconds`
  - delay before normal world collision responses are restored
- `bUseTemporaryBottomAnchor`
  - temporarily supports the bottom of the collection so fracture can begin before the base fully gives way
- `BottomAnchorHeightFraction`
  - how much of the lower collection height stays anchored briefly
- `bReleaseTemporaryBottomAnchor`
  - if `false`, the lower anchored region stays pinned for the full actor lifetime
  - use this for tree-chop style setups where the stump should not release
- `BottomAnchorReleaseDelaySeconds`
  - how long the temporary bottom support stays active before releasing into the slump

## Recommended defaults for gentle rock breakup

For `AChunkWorldChaosGentleBreakApartActor`, the intended strategy is:

- use `Uniform` strain
- keep the optional impulse off
- keep delayed world-collision response staging on
- keep the temporary bottom anchor on
- let gravity and damping create most of the visible motion

This subclass is intended for:

- rock chunks
- heavy props
- block replacements that should collapse or slump instead of explode outward

For the simplest "just separate and fall" behavior, prefer `AChunkWorldChaosGentleReleaseActor`.

For a tree chop presentation with one authored fracture slice, prefer `AChunkWorldChaosTreeChopActor`.
Author the collection as a pre-fractured trunk with a small anchored stump section and one larger upper section that can detach and fall.

## Runtime diagnostics

The base actor exposes runtime diagnostics through `FChunkWorldChaosDestructionDiagnosticsConfig` and `FChunkWorldChaosDestructionDiagnosticsMetrics`.

Useful flags:

- `bEnableRuntimeDiagnostics`
  - enables runtime observation for one destruction presentation
- `bLogSuggestions`
  - writes the generated suggestion summary to the log after the evaluation window completes
- `bDisableSeparationImpulseDuringDiagnostics`
  - useful when isolating fracture behavior from any optional post-break impulse

When diagnostic logging is enabled, the actor writes to:

- log category: `LogChunkWorldChaosDestructionPresentation`

The log entry includes:

- suggestion text
- spread delta
- horizontal drift
- upward displacement
- resolved field origin
- resolved strain radius
- resolved separation radius
- whether the separation impulse was skipped for diagnostics

Search for:

- `LogChunkWorldChaosDestructionPresentation`
- `diagnostics for block`

## Practical tuning workflow

Use this order when tuning a new destruction presentation:

1. Assign the fractured `GeometryCollection`.
2. Start with `FractureFieldMode = Uniform`.
3. Keep `bApplyGentleSeparationImpulse = false`.
4. Enable diagnostics with `bEnableRuntimeDiagnostics = true` and `bLogSuggestions = true`.
5. Raise `ExternalStrainMagnitude` until the collection reliably breaks.
6. Keep the magnitude only slightly above the first Geometry Collection threshold that must release.
7. Only consider reintroducing a very small impulse after fracture already looks correct without it.

If the collection flies outward:

- reduce strain magnitude
- keep the impulse off
- increase damping
- increase `CollisionEnableDelaySeconds` if overlap kickout still appears to influence the first moments

If the collection does not break:

- raise `ExternalStrainMagnitude`
- check the Geometry Collection `Damage Threshold` array
- confirm the fracture asset is authored with the cluster hierarchy you expect

## Tree-chop authoring guidance

`AChunkWorldChaosTreeChopActor` assumes the Geometry Collection is authored for a simple two-part release:

- one lower stump region that remains anchored
- one upper trunk or canopy region that detaches
- a fracture slice positioned near the intended cut height

Recommended setup:

- keep shard count very low
- avoid multiple competing fracture layers near the base
- keep the damage threshold for the release level close to the actor's `ExternalStrainMagnitude`
- tune `ChopPlaneHeightFromBottom` so the field origin lines up with the authored cut plane
- tune `StumpAnchorHeightFromBottom` so the persistent anchor stays below the cut plane
- keep `ChopPlaneStrainRadius` tight around the trunk cross-section instead of scaling it from canopy bounds

If the top half still explodes outward, do not add impulse first. Tighten the radial strain radius around the cut plane before raising energy.

## Asset-side guidance

Actor tuning cannot fully compensate for unstable Geometry Collection authoring. If behavior is still inconsistent, inspect the asset for:

- overly aggressive top-level cluster release
- large threshold gaps between cluster levels
- tiny shards or thin sliver pieces
- poor chunk collision shapes
- chunk overlap with nearby environment collision at spawn

For heavy rock breakup, prefer collections that:

- have readable medium-sized chunks
- avoid extreme shard counts
- use thresholds that support one controlled top-level release

## Integration

Assign a Blueprint subclass of `AChunkWorldChaosGentleBreakApartActor` as the `DestructionActorClass` in the relevant block damage definition.

That class will be spawned by `AChunkWorldExtended` and triggered through `ChunkWorldDestructionActorInterface` when the block is authoritatively removed.

## Trigger contract

The destruction actor API has two different responsibilities that should stay separate:

- `TriggerBlockDestruction`
  - framework-owned trigger entry point
  - use this for authoritative trigger handling only
  - if a Blueprint subclass implements this event, it must call parent
  - do not put local-only cosmetic logic here unless the actor intentionally wants that logic coupled to the authoritative trigger path
- `ReceiveDestructionTriggered`
  - local per-process presentation hook on `AChunkWorldTimedCleanupDestructionActor`
  - use this for local visual, audio, Niagara, or other per-process effects after the request has already been accepted

Practical guidance:

- For `Local Only Per Client` destruction presentation, each process spawns its own actor after observing the authoritative block removal.
- In that mode, the local effect should be authored in `ReceiveDestructionTriggered` or another actor-specific local hook, not by replacing the interface event.
- For `Replicated Actor` destruction presentation, only the server spawns the actor and the actor's replicated trigger state replays the same local presentation start path on clients.

Current plugin-owned actor expectations:

- `AChunkWorldTimedCleanupDestructionActor`
  - keep `TriggerBlockDestruction` as infrastructure
  - author local effects in `ReceiveDestructionTriggered`
- `AChunkWorldChaosDestructionPresentationActor`
  - keep `TriggerBlockDestruction` as infrastructure
  - customize through the native class hooks and tuning surface instead of replacing the interface event in Blueprint

Potential future API cleanup:

- Consider renaming `TriggerBlockDestruction` to something more explicit like `HandleAuthoritativeDestructionTrigger`.
- Consider renaming `ReceiveDestructionTriggered` to something more explicit like `HandleLocalDestructionPresentation`.
- Do that only as a deliberate Blueprint API migration, because these names are already part of the current plugin-facing contract.
