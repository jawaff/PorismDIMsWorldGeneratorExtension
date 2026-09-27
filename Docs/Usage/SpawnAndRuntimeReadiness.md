# Spawn And Runtime Readiness

## Use This For

Use this flow for player join, player respawn, roster replacement, runtime NPC, relocation, or test placement when source comes from Porism reservation fields and destination may stream after actor creation.

## Level Setup

1. Use `AChunkWorldExtended`.
2. Add `UChunkWorldReservationSpawnSourceProvider` to same actor.
3. Author reservation fields with `EReservationFieldKind::Spawn`, finite bounds, source tags, and biome binding.
4. Confirm component has `UChunkWorldSpawnComponent` through `GetSpawnComponent()`.
5. Configure candidate limits. Leave query cache disabled unless source provider declares cache-safe data.

The reservation provider registers itself on authority during `BeginPlay`. Do not manually register same provider twice.

### Reservation Facing

Choose `Spawn Facing Mode` on each enabled spawn reservation. The editor installs its matching typed `Spawn Facing Payload`:

- **Fixed Rotation:** author yaw in strategy XY degrees. 0 faces +X; 90 faces +Y. World rotation applies; scale and mirroring do not change this heading. The default is 0.
- **Fixed Point:** author one target in shared biome-strategy block coordinates. Single and repeated reservations use the same point. Provider/reservation offsets and scales do not move it; conversion to world coordinates applies once. A ring can face its common center.

The exact selected field supplies facing, even when other fields share its biome tag. The provider publishes upright provisional rotation in `ApprovedTransform` and retains the target in `Result.Facing`. Settlement computes target-facing yaw from the final capsule center, preserves game-owned inverse-view rotation, and compensates the rotated capsule offset. Target elevation never introduces pitch. Coincident target XY retains the provisional heading; initial coincidence uses strategy +X.

Nonfinite values, mismatched payloads and world headings without a horizontal direction are rejected. Facing does not rotate terrain, reservation bounds or sampling positions.

## Game Request Flow

Game code or Blueprint builds `FChunkWorldSpawnRequest`.

Required:

- stable opaque subject id
- request reason
- `ReservationField` source family

Player request can leave `SubjectDefinitions` empty. NPC request can pass project-defined weighted `FInstancedStruct` definitions derived from `FChunkWorldSpawnSubjectDefinitionBase`.

```cpp
FChunkWorldSpawnRequest Request;
Request.SpawnReason = EChunkWorldSpawnReason::InitialMatchSpawn;
Request.SubjectBinding.SubjectKind = EChunkWorldSpawnSubjectKind::ControllerOwned;
Request.SubjectBinding.StableSubjectId = StableSubjectId;
Request.SubjectBinding.OwningController = Controller;
Request.AllowedSourceFamiliesInPriorityOrder = {
    EChunkWorldSpawnSourceFamily::ReservationField
};
Request.RequiredSpawnTags = SpawnTags;
Request.RequiredBiomeTags = BiomeTags;
Request.PreferredSearchOrigin = DesiredOrigin;
Request.PreferredSearchRadius = 50000.0f;
Request.bAllowLowerPrioritySourceFallback = false;

const FChunkWorldSpawnTicketHandle Ticket = ChunkWorld->GetSpawnComponent()->SubmitSpawnRequest(Request);
```

Keep ticket handle in game state that owns request. Poll `GetSpawnTicketState` and `GetSpawnTicketResult` when you need failure/cancel detail.

Bind game Blueprint or C++ to `OnSpawnResolved` before submitting requests. Event only reports approved locations. Match returned `RequestId` to game request state.

### Distinct reservation assignments

Set `bAssignUniqueReservation=true` to draw from unassigned reservation sources using existing weighted selection. Supply a valid `SubjectBinding.RequestOwner` and only the `ReservationField` family. The component commits the assignment after location approval and before result callbacks, so queued human/NPC requests cannot take the same source.

The same stable subject and owner reuse their assigned source even after ticket eviction. Other subjects cannot use that source. Owner invalidation releases it on the next selection; component teardown clears all assignments. NPC callers can share one coordinator owner when each participant has a distinct stable subject ID. Assignment does not track pawn lifetime or physical occupancy.

Exhaustion fails with `NoEligibleSource`; it does not reuse another subject's area. Failed placement does not consume an assignment. Leave this setting false for shared team areas and use `RequiredSpawnTags` for the requesting team. Both tag filters are conjunctive: `RequiredSpawnTags` matches spawn-field tags and `RequiredBiomeTags` matches the bound biome.

### Terrain height prediction

Reservation fields define eligible regions, not a standalone terrain generator. The provider samples the live compiled world's base-block grid using the world seed, biome coordinate warps, domain/GenA/GenB composition, density-only rows, and world noise (or the configured generation-test override). Sampling uses the native density kernels. The approved Z is the top face of the highest solid cell with air above inside the field, rather than that cell's sample position. A missing compiled world, all-air column, or solid upper boundary supplies no candidate.

This prediction does not include saved voxel edits, layout writes, or collision-mesh reconstruction. Final bounded collision settlement remains required for those differences and for the character's capsule-center offset. Do not add a universal capsule half-height in the provider: the game may create either a centered capsule or a floor-origin capsule. On a missing-surface failure, the debug reason includes capsule center/offset, dimensions, scan endpoints, region bounds, and collision profile; keep that evidence rather than releasing freeze.

### Bounded collision placement

The reservation provider starts at the center of the field's allowed XY bounds. Area selection remains separate: distinct-area requests still randomize source identity, while team requests can share an area.

Runtime readiness tries that provisional center first, then bounded random alternatives seeded by the stable readiness session ID. `RuntimeSettlementCandidateCount` defaults to 32 and clamps to 1..64. Each accepted position must fit the full upright capsule inside the approved region, have walkable non-participant support, and pass final overlap checks. Explicit capsule occupancy includes live and frozen/pending pawns even when pawn collision responses ignore each other. Actor lifetime owns occupancy; a failed actor remains an obstacle until its owner removes it.

A floor hit does not prove empty headroom. The component checks native finest-collision cells throughout the final body volume and rejects queued terrain edits. An empty sweep remains pending until its whole scan has collision coverage. Worlds configured without voxel collision can settle on ordinary scene geometry. A busy native visibility pass defers the query without spinning. `RuntimeSettlementCollisionTimeoutSeconds` defaults to 20 after other readiness prerequisites finish; expiry retains freeze. Pending layout work keeps its existing retryable policy.

`RuntimeSettlementZOffset` requests contact clearance, with `UCharacterMovementComponent::MIN_FLOOR_DIST` as the minimum. Exact capsule contact can count as overlap in Chaos, so this uses the engine's standing clearance rather than changing terrain prediction. Larger authored clearance still must fit bounds and pass overlap checks. Target-facing then preserves the accepted capsule center while changing yaw.

Failures include bounds, capsule geometry, scan, profile, attempt count and `CollisionPending`. Do not widen bounds, reuse another participant's reservation or release freeze to hide a failure. Prediction still rejects a center column with no eligible compiled surface; bounded runtime fallback handles unsafe or occupied realized placement after provisional approval.

## Result Consumer Flow

On `OnSpawnResolved`:

1. Match `Result.RequestId` to game request.
2. Create or reuse subject through game factory, pool, roster, or Blueprint path at `Result.ApprovedTransform`.
3. Apply project initialization data.
4. Possess player if needed.
5. Obtain subject `UChunkWorldReadinessFreezeComponent`.
6. Start runtime readiness with `Result.ApprovedCandidateRegion` and `Result.Facing`.
7. Register actor for KillZ observation only if project needs generic out-of-world event.

Do not create actor in provider or spawn component. Do not treat provisional transform as final collision placement. Preserve the approved rotation when creating the actor. Fixed-rotation settlement retains it; fixed-point settlement applies only the change from provisional to final facing. Settlement accounts for the capsule's collision-center offset from the actor origin, including tracking-origin capsules.

## Player Session

Player character needs registered `IChunkWorldWalker` before session settles.

Authority starts player session:

```cpp
const FGuid SessionId = Freeze->StartRuntimeReadinessSession(
    ChunkWorld,
    Result.ApprovedCandidateRegion,
    true,
    Result.Facing);
```

Bind client-side `OnOwningClientRuntimeReady`. The event waits for the local native walker receipt, layout readiness and collision coverage throughout the approved candidate region, including possible sideways fallback positions. Missing cells or queued edits defer publication; repeated ticks publish only once per readiness generation. This conservative check can reach the existing client timeout when part of the region remains unloaded, even if a nearer server candidate would be safe.

Send session and readiness ids through the game-owned reliable server RPC. Server RPC must validate its controller and call component acknowledgement path. Component rejects wrong session, wrong controller, wrong pawn, missing tracked walker, invalid region, or terminal state.

Bind `OnRuntimeReadinessSettled` for gameplay after server release. Bind `OnRuntimeReadinessFailed` for session recovery policy. Timeout, missing surface, invalid acknowledgement, and world teardown retain freeze.

Game owns disconnect and return-to-menu behavior. Extension does not kick players or teleport them elsewhere.

## Server-Only NPC Session

NPC has registered walker and readiness-freeze component. Start session with `false`:

```cpp
Freeze->StartRuntimeReadinessSession(
    ChunkWorld,
    Result.ApprovedCandidateRegion,
    false,
    Result.Facing);
```

Server waits for walker finest detail, traces current terrain inside source region, re-anchors, releases. Do not use client acknowledgement for NPC.

Start NPC movement/AI after `OnRuntimeReadinessSettled`, not after provisional actor creation.

## Startup Versus Runtime

`OnWorldReady` only covers startup walkers. Use startup freeze for first map load and loading UI.

Use runtime readiness for join, respawn, and runtime spawn. Late registrations do not restart `OnWorldReady`.

## Failure Handling

| Signal | Game response |
| --- | --- |
| Failed/canceled ticket | log request id/category; clear game pending state; choose project retry policy |
| `OnRuntimeReadinessFailed` | keep subject frozen; inspect `Session.Failure` and `DebugReason`; apply game recovery policy |
| Ticket/result no longer available | game retained it beyond terminal retention; submit new request or retain needed result state locally |

Do not hide failure with unbounded search, local client snap, or fallback teleport.

## Generic KillZ

After creation:

```cpp
ChunkWorld->GetSpawnComponent()->RegisterTrackedSpawnActor(Subject);
```

Bind native `OnSubjectOutOfWorld` in C++. Game chooses death, respawn request, relocation, spectate, or elimination. Unregister pooled or retired actor.

## Checklist

- [ ] `AChunkWorldExtended` owns reservation source provider.
- [ ] Spawn reservation field has finite bounds and correct tags.
- [ ] Game keeps stable subject id and result correlation state.
- [ ] `OnSpawnResolved` creates/reuses actor through game path.
- [ ] Subject has registered walker and readiness-freeze component.
- [ ] Player client acknowledgement uses game-owned reliable RPC.
- [ ] NPC session uses server-only readiness.
- [ ] Game binds settled and failure terminal events.
- [ ] Game registers actor for KillZ only when needed.

## Related Docs

- [../Design/ChunkWorldSpawn.md](../Design/ChunkWorldSpawn.md)
- [StartupFreezeAndWorldReady.md](./StartupFreezeAndWorldReady.md)
- [ChunkWorldGameplaySetup.md](./ChunkWorldGameplaySetup.md)
- [../Design/ChunkWorldExtended.md](../Design/ChunkWorldExtended.md)
