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

## Result Consumer Flow

On `OnSpawnResolved`:

1. Match `Result.RequestId` to game request.
2. Create or reuse subject through game factory, pool, roster, or Blueprint path at `Result.ApprovedTransform`.
3. Apply project initialization data.
4. Possess player if needed.
5. Obtain subject `UChunkWorldReadinessFreezeComponent`.
6. Start runtime readiness with `Result.ApprovedCandidateRegion`.
7. Register actor for KillZ observation only if project needs generic out-of-world event.

Do not create actor in provider or spawn component. Do not treat provisional transform as final collision placement.

## Player Session

Player character needs registered `IChunkWorldWalker` before session settles.

Authority starts player session:

```cpp
const FGuid SessionId = Freeze->StartRuntimeReadinessSession(
    ChunkWorld,
    Result.ApprovedCandidateRegion,
    true);
```

Bind client-side `OnOwningClientRuntimeReady`. Send session id through game-owned reliable server RPC. Server RPC must validate its controller and call component acknowledgement path. Component rejects wrong session, wrong controller, wrong pawn, missing tracked walker, invalid region, or terminal state.

Bind `OnRuntimeReadinessSettled` for gameplay after server release. Bind `OnRuntimeReadinessFailed` for session recovery policy. Timeout, missing surface, invalid acknowledgement, and world teardown retain freeze.

Game owns disconnect and return-to-menu behavior. Extension does not kick players or teleport them elsewhere.

## Server-Only NPC Session

NPC has registered walker and readiness-freeze component. Start session with `false`:

```cpp
Freeze->StartRuntimeReadinessSession(
    ChunkWorld,
    Result.ApprovedCandidateRegion,
    false);
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
