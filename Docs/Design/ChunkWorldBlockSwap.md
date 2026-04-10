# Chunk World Block Swap

## Purpose
This page documents the full mesh/actor swap feature that lives in `PorismDIMsWorldGeneratorExtension`.

The swap feature exists to let a chunk-world voxel:
- stay authored as a normal Porism block in the world
- swap into an actor presentation when a relevant actor gets close enough
- hide and later restore the represented voxel through a shared replicated presentation-only path

The design splits the feature into:
- schema-authored swap data
- proximity-driven polling scan/orchestration
- replicated block hide/restore execution

## Main Types

### `FBlockDefinitionBase`
Owns the authored swap field:
- `SwapActorClass`

That field is resolved through `UBlockTypeSchemaComponent` for a specific `BlockWorldPos`.

### `UChunkWorldBlockSwapScannerComponent`
Server-side orchestration component that:
- tracks active proximity sources
- polls for nearby schema-authored swap candidates with a sphere sweep on the configured proximity trace channel
- preloads nearby authored swap actor classes before they become swap-in eligible
- acquires a pooled or freshly spawned actor when a block enters swap presentation
- requests the shared hide/restore transition through `UChunkWorldBlockSwapComponent`
- restores the block when the swap exits and releases pooled actors back into delayed recycle
- force-removes active or pending swaps when a swapped actor is destroyed or when `DestroyBlock(...)` removes the underlying voxel

This component owns the scan tuning:
- `SwapScanInterval`
- `MaxBlocksPerScan`
- pooling and preload tunables such as warm count, pool cap, recycle delay, and pooled parking offset

These settings intentionally live on the scanner component instead of `AChunkWorldExtended` so the chunk-world host stays slim.

### `UChunkWorldProximityComponent`
Actor-attached source component that provides:
- a chunk-world proximity radius
- an offset-adjusted scan origin
- the trace channel used by polling swap queries
- optional debug-drawn swap-in and swap-out spheres

The current implementation uses:
- `GetOwner()->GetActorLocation() + ScanOriginOffset`

This component is no longer an overlap-event cache. It is only a proximity query source for the scanner.

### `UChunkWorldSwapRegistrationSubsystem`
World-scoped registration layer that connects:
- active `UChunkWorldProximityComponent` instances
- active `UChunkWorldBlockSwapScannerComponent` instances

This exists so the swap feature does not rely on `GetAllActorsOfClass(...)` scans during `BeginPlay`/`EndPlay`.

### `UChunkWorldBlockSwapComponent`
Replicated execution component that:
- validates one swap request on the authority side
- replicates the active swap set through a fast array
- resolves the represented instanced mesh entry locally
- parks that instance in a deterministic hidden parking area without mutating chunk save data
- restores the cached transform when the swap exits
- reconciles replicated swap state and replicated actor arrival so clients only reveal the actor after the source mesh is parked locally

This component is not the scan policy layer.
Its responsibility is execution of the authoritative swap request.

### `FChunkWorldBlockSwapRequest`
Generic event payload emitted by the scanner when a swap enters or exits.

It includes:
- `BlockWorldPos`
- `BlockTypeName`
- `SwapActorClass`
- `bEntering`

### `UChunkWorldPooledSwapActorInterface`
Optional project-facing pooling contract for swap actors that need explicit lifecycle hooks.

It exposes:
- `PrepareForSwapUse(...)`
- `ResetForSwapPool()`

Projects can ignore the interface for simple stateless swap actors. The scanner still applies generic hide/collision/transform handling for pooled actors even when the actor does not implement the interface.

The intended design is still that project-owned swap actors should implement this interface once they carry any meaningful runtime state.
The object pool exists specifically so actors are reused instead of destroyed and recreated on every swap, which means reused actors need an explicit reset/reinitialize contract.
Treat the interface as the normal path for pooled gameplay actors, not as an edge-case extension hook.

## Runtime Flow

### Authoritative server flow
1. `UChunkWorldProximityComponent` registers itself with `UChunkWorldSwapRegistrationSubsystem`.
2. `UChunkWorldBlockSwapScannerComponent` registers itself with the same subsystem.
3. The subsystem binds active proximity sources to active scanners.
4. The scanner periodically runs one sphere sweep per active proximity source at `SwapScanInterval`, using the full swap-out radius as the broad-phase candidate query.
5. For each represented voxel:
   - the hit is resolved back into `BlockWorldPos` with the shared chunk-world hit helpers
   - `UBlockTypeSchemaComponent` resolves the block definition
   - the scanner ignores blocks with no `SwapActorClass`
   - the scanner compares current distance against `SwapInDistance` / `SwapOutDistance`
6. On swap-in:
   - the scanner captures the represented mesh instance transform
   - the scanner calls `UChunkWorldBlockSwapComponent::TryApplySwapRequest(BlockWorldPos, BlockTypeName, true)`
   - the swap component parks the represented ISMC instance and replicates the active swap entry
   - the scanner preloads the authored `SwapActorClass` when the block is first seen in the broad-phase candidate scan and warms a small parked pool once the class resolves
   - the scanner acquires a pooled actor when one is available, otherwise it spawns one fresh and keeps it eligible for later reuse
   - the acquired actor is explicitly kept hidden with collision disabled until the replicated swap item and actor are paired on each client
   - if the actor implements `UChunkWorldPooledSwapActorInterface`, the scanner calls `PrepareForSwapUse(...)` before the swap is associated
   - the scanner records the active swap and broadcasts `OnBlockSwapRequested`
7. On swap-out:
   - the scanner calls `ResetForSwapPool()` when the actor implements the pooling interface
   - the scanner hides and parks the actor, then returns it to a short recycle quarantine before it becomes available for reuse again
   - the scanner calls `UChunkWorldBlockSwapComponent::TryApplySwapRequest(BlockWorldPos, BlockTypeName, false)`
   - the swap component restores the parked ISMC instance transform and removes the replicated active swap entry
   - the scanner removes the active swap and broadcasts `OnBlockSwapRequested`
8. On block destruction:
   - `AChunkWorldExtended::DestroyBlock(...)` asks the scanner to force-remove any active or pending swap for that `BlockWorldPos`
   - the scanner removes pending block waits, releases the swap actor if present, removes replicated hide state, and then lets the normal block destruction continue

### Client flow
Clients do not run the authoritative scan.

Clients receive:
- replicated swap actors through normal actor replication
- the active swap fast-array deltas through `UChunkWorldBlockSwapComponent`

Clients do not assume those two replication paths arrive in lockstep. The swap component reconciles them by block position and only reveals the actor after the mesh hide has already been applied locally.

## Ownership Boundary

### Plugin-owned
The extension plugin owns:
- swap authoring fields on `FBlockDefinitionBase`
- proximity source component
- swap scanner component
- swap registration subsystem
- generic swap enter/exit event payload
- replicated hide/restore execution
- configurable parking-area policy for hidden mesh instances
- generic pooled-actor lifecycle hooks through `UChunkWorldPooledSwapActorInterface`

### Not owned by the plugin
The plugin does not own:
- project-specific actor classes referenced by `SwapActorClass`
- project-specific actor logic after spawn or per-reuse reset
- project-specific game rules for who should carry a proximity component

## Why The Feature Is Split

### Why not put scan settings on `AChunkWorldExtended`?
That would make the host actor carry more feature-specific policy than necessary.

Keeping scan tuning on `UChunkWorldBlockSwapScannerComponent`:
- keeps the actor host slimmer
- makes the swap runtime easier to disable/replace
- keeps swap tuning with the system that actually consumes it

### Why not merge the scanner into `UChunkWorldBlockSwapComponent`?
The scanner and the replicated execution layer solve different problems:
- scanner: "when should this block swap?"
- swap component: "apply this authoritative hide/restore transition consistently"

Keeping them separate makes the execution path reusable even if a future project wants a different trigger model.

### Why use a world subsystem?
The subsystem replaces the old project-side registration workaround that depended on world-wide actor scans.

That gives the plugin:
- explicit registration
- lower overhead during `BeginPlay` / `EndPlay`
- a cleaner long-term extension point if more chunk-world proximity-driven systems are added later

## Recommended Usage
- Put `AChunkWorldExtended` or a thin subclass in the world.
- Attach `UChunkWorldProximityComponent` to the actor that should drive swap relevance, usually the player character.
- Author `SwapActorClass` on schema rows for mesh-backed block types.
- Configure `SwapInDistance` and `SwapOutDistance` on `UChunkWorldProximityComponent`.
- Configure the hidden parking area on `UChunkWorldBlockSwapComponent` so parked instances stay outside meaningful world space. Projects can push that value from a subclass during startup if they want a centralized policy.
- Configure the pooled actor parking offset and warm-count settings on `UChunkWorldBlockSwapScannerComponent` conservatively, especially if many block types can swap into different actor classes.
- Use a proximity/query collision channel that matches the represented chunk-world collision bodies used for swap candidate discovery. Do not assume an overlap-only setup will work just because a trace on the same channel works.
- Configure swapped actor collision so interaction traces do not unintentionally pass through the actor and hit the parked or underlying chunk-world block behind it.
- Implement `UChunkWorldPooledSwapActorInterface` on project-owned swap actors by default when those actors participate in pooled swap presentation.
- At minimum, use `PrepareForSwapUse(...)` to rebuild any per-block state that used to live in construction or first-spawn code, and use `ResetForSwapPool()` to clear timers, detach transient children, stop effects, and scrub state that must not leak into the next block assignment.
- Only skip the interface when the swap actor is truly stateless and generic hide/collision/transform reset is enough.
- Treat parking-Z policy as project-owned.
- Let `UChunkWorldBlockSwapScannerComponent` on the chunk world drive the runtime.

## Pooling Contract

### Why The Interface Is Part Of The Design
The swap runtime now intentionally reuses actors through a pool.

That means the plugin no longer assumes:
- one actor lifetime equals one block presentation lifetime
- construction-time initialization is sufficient
- destroying the actor is the cleanup path

For any actor with meaningful state, the correct lifecycle becomes:
1. spawned once
2. parked in the pool
3. acquired for one block
4. reinitialized in `PrepareForSwapUse(...)`
5. released from that block
6. cleaned up in `ResetForSwapPool()`
7. reused later for another block

### What Belongs In `PrepareForSwapUse(...)`
- assign block-specific data
- rebuild visuals or child state derived from the represented block
- restart actor-local systems that should only run while active
- refresh project-specific interaction state

### What Belongs In `ResetForSwapPool()`
- clear timers and latent work
- stop looping VFX/audio
- detach transient children or helper actors
- clear cached references to the previous represented block
- reset project-owned components back to a safe parked baseline

### What The Plugin Still Handles Without The Interface
The scanner still applies generic pooled-actor handling:
- hidden state
- collision disable/enable
- parking transform assignment
- delayed recycle timing

The interface exists because that generic handling is not enough for most stateful gameplay actors.

For starter player-facing character composition, see:
- `Docs/Usage/ChunkWorldGameplaySetup.md`

## Current Constraints
- swap actor authoring is validated only for mesh-backed block types
- the scanner currently uses one sphere sweep per proximity source per poll and resolves per-hit distances from that single broad-phase query
- the scanner still relies on the broad-phase candidate query for preload timing; it does not yet expose a separate larger preload radius
- fresh fallback spawns still use `AlwaysSpawn` when the warm pool misses or pooling is disabled
- swap presentation assumes the parking volume remains outside meaningful generated space for the active world definition
- block-swap correctness still depends on collision/query setup that resolves the same represented block data path used by chunk-world hit helpers
- swap-in/out diagnostics are currently log-based

## Future Follow-Up Ideas
- optional alternate spawn policy hooks for projects that do not want direct actor spawning
- optional scanner strategy variants if another project needs a different relevance model
- optional richer event hooks around actor spawn/despawn if more shared post-spawn behavior emerges
