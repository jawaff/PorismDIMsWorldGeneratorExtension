# Chunk World Spawn And Runtime Readiness

## Purpose

`AChunkWorldExtended` provides server-authoritative location selection for games that create players, NPCs, or other subjects through their own systems. It also provides character-owned readiness settlement for destinations whose terrain has not finished streaming.

The extension chooses a bounded provisional location. Game code creates, reuses, initializes, possesses, and destroys actors. The extension has no project pawn, roster, UI, combat, or session dependency.

## Main Types

### `AChunkWorldExtended`

`AChunkWorldExtended` owns `UChunkWorldSpawnComponent` plus existing startup-ready tracking and `OnWorldReady`.

`UChunkWorldReadinessFreezeComponent` belongs on each spawned or repositioned subject, not on chunk world.

`OnWorldReady` remains startup-only. It does not restart for late joins, respawns, or later walker registration.

For runtime settlement, `AChunkWorldExtended` tracks one registered walker and one session id through `StartRuntimeReadinessTracking`. It broadcasts `OnRuntimeWalkerReady` once when that walker reaches finest detail. Repeated ready data does not emit another ready event for same walker/session pair.

### `UChunkWorldSpawnComponent`

`UChunkWorldSpawnComponent` accepts a request, queries registered source providers, selects one bounded candidate, and publishes a terminal ticket result.

It owns:

- authority/game-thread request validation
- bounded provider registration and candidate collection
- source-family priority and optional lower-priority fallback
- request-id-seeded weighted selection
- ticket lifecycle and bounded result retention
- resolved-location event publication
- optional tracking of game-registered actors that cross chunk world KillZ

It does not own actor creation, preload tickets, temporary walkers, terrain collision placement, project policy, or recovery consequences.

### `IChunkWorldSpawnSourceProvider`

A source provider supplies candidate regions for one `EChunkWorldSpawnSourceFamily`. It must return finite candidates and respect `QueryContext.MaximumCandidates`.

A candidate contains:

- source family, ids, and diagnostic label
- finite `SearchBounds`
- one theoretical pre-realization location
- source and biome tags
- optional score

`UChunkWorldReservationSpawnSourceProvider` is current built-in provider. Attach it to same `AChunkWorldExtended`; it registers during authority `BeginPlay`. It exposes only reservation fields authored with `EReservationFieldKind::Spawn`.

It samples deterministic XY from request id plus field path, then finds topmost theoretical `GenA` solid surface within authored field Z bounds. It does not load destination chunks or trace collision.

### `UChunkWorldReadinessFreezeComponent`

Put this component on each created subject that must remain safe while chunks stream.

Startup freeze handles map startup through `OnWorldReady`. Runtime readiness handles a new spawn or reposition:

1. Authority starts session with result `ApprovedCandidateRegion`.
2. Component freezes movement, simulated physics, and optional actor transform drift.
3. Chunk world reports tracked walker ready at finest detail.
4. Player-owned session asks owning client to acknowledge local readiness. Server-only subjects skip this gate.
5. Authority validates gates, sweeps current terrain inside candidate region, selects final transform, re-anchors frozen transform, then restores captured movement and physics.
6. Component replicates settled or failed terminal session state.

Failure keeps subject frozen. Gameplay consumes `OnRuntimeReadinessFailed` for project policy such as disconnect, UI, retry, or teardown.

## Ownership

| Concern | Owner |
| --- | --- |
| Source bounds and provisional location | extension source provider and spawn component |
| Actor class, factory, pool, roster, init, possession | game |
| Runtime collision sweep and final transform | authority readiness-freeze component |
| Player client-ready RPC transport | game controller |
| Session validation and release | authority readiness-freeze component |
| Generic KillZ threshold observation | spawn component for explicitly registered actors |
| Death, respawn, kick, UI, fallback location | game |

## Request And Ticket Contract

`FChunkWorldSpawnRequest` requires:

- `SubjectBinding.StableSubjectId`
- at least one source family in `AllowedSourceFamiliesInPriorityOrder`

Optional fields narrow source selection:

- required source and biome tags
- preferred origin, radius, and minimum distance
- lower-priority source fallback
- project-defined weighted `SubjectDefinitions`

Definitions derive from `FChunkWorldSpawnSubjectDefinitionBase`. Extension reads only `SelectionWeight` and `RequiredSpawnTags`, then returns selected `FInstancedStruct` unchanged in `FChunkWorldSpawnResult`.

Ticket states:

- `Pending`: source selection remains active.
- `Ready`: extension approved one provisional location.
- `Failed`: extension could not approve location. Read `FailureCategory` and `DebugReason`.
- `Canceled`: caller or owner canceled pending work.

Use `GetSpawnTicketResult` for any terminal result. `OnSpawnResolved` broadcasts only successful `Ready` results, once, on authority game thread.

Terminal results remain only until component reaches `MaximumRetainedTerminalTickets`. Consume results or copy required state before submitting enough future tickets to evict older terminal records.

## Location Semantics

`ApprovedTransform` is a provisional terrain-theory location. It is safe to create frozen actor there before destination chunks realize. It is not collision-safe final placement.

`ApprovedCandidateRegion` is finite world-space bounds inherited from source candidate. Pass it unchanged into runtime readiness. Runtime settlement must not search outside this region or fall back to random teleport.

Candidate query cache is disabled by default. Enabling it only caches a family when every contributing provider returns explicit cache-validity key. Provider registration changes invalidate cache.

## Runtime Readiness Contract

Server starts runtime session:

```cpp
const FGuid SessionId = Freeze->StartRuntimeReadinessSession(
    ChunkWorld,
    Result.ApprovedCandidateRegion,
    bPlayerOwned);
```

Reject invalid session id. Common causes: caller lacks authority, candidate region is invalid, owner lacks world match, or existing session has not reached terminal state.

For player-owned sessions, bind `OnOwningClientRuntimeReady`. Game controller sends its reliable RPC with session id. RPC implementation calls `AcknowledgeOwningClientRuntimeReady` on authority with authenticated controller. Client supplies readiness acknowledgement only. Client does not select transform or release freeze.

For server-only NPCs and other non-player subjects, pass `false`. Server walker-ready signal proceeds straight to settlement.

Terminal state is idempotent. Duplicate ready signals, acknowledgements, cancellation attempts, and replicated terminal observations do not double-snap or double-release.

## Out-Of-World Observation

Call `RegisterTrackedSpawnActor` only after game creates actor, when generic KillZ observation is useful. `OnSubjectOutOfWorld` emits one edge event per downward threshold crossing. Game chooses all consequences.

Call `UnregisterTrackedSpawnActor` before pooling or destruction when game no longer wants observation.

## Limits And Non-Goals

- No actor execution adapter exists.
- No plugin-owned NPC/player factory, UI, policy runner, origin polling loop, or fallback teleport exists.
- No unbounded provider output, source search, ticket retention, or tracked actor list exists.
- Origin-provider registration remains a future policy seam. Direct requests are current player/NPC entry path.

## Validation

Run after changes:

```bat
Scripts\Build\build_editor.bat
Plugins\PorismDIMsWorldGeneratorExtension\Scripts\run_tests.bat PorismExtension.ChunkWorld.Spawn --no-build
Plugins\PorismDIMsWorldGeneratorExtension\Scripts\run_tests.bat PorismExtension.ChunkWorld.ReadinessFreeze.Runtime --no-build
```

Run project tests when game-owned readiness or origin integration changes:

```bat
Scripts\Build\run_tests.bat --no-build
```
