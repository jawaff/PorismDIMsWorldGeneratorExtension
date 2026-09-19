# Layout Module Cookbook

## What this is for
Use this guide when you want Porism templates to assemble into deterministic layouts such as:
- a walled site
- a road between two sites
- a parent layout with smaller child layouts inside it
- a hillside-friendly surface layout

## The short version
You need five things:
1. Porism templates
2. `ULayoutModuleAsset` wrappers for those templates
3. one `ULayoutModuleSetAsset`
4. one or more `ULayoutProfileAsset` assets
5. `LayoutWorldBindings` assets on `UChunkWorldLayoutRuntimeComponent`

If you want fast iteration before runtime, also use `ALayoutPreviewActor`.

## Where runtime stamping actually happens

Runtime layout stamping goes through `AChunkWorldExtended`, not through `ALayoutPreviewActor`.

- `AChunkWorldExtended` owns `UChunkWorldLayoutRuntimeComponent`
- that component discovers sites from eligible loaded chunk coverage
- it solves and caches layout records
- it realizes templates only after Porism has already loaded the required chunks

Use `ALayoutPreviewActor` only for offline preview of solve results.

- it does not stamp into the chunk world
- it does not participate in streamed runtime realization
- it is only an editor-facing preview helper

## Shared rules
- Keep every module in a module set at the same `CellSizeInBlocks`.
- Put layout semantics on the module asset, not on the Porism template.
- Reserve real `Entry` cells for anything that should connect to roads or other layouts.
- Prefer simple walkable tags such as `Ground`, `Upper`, and `Roof`.
- Use terrain-conforming placement only when the layout should react to the world surface.

## Built-in tags worth using
See [LayoutGameplayTags.h](../../Source/PorismDIMsWorldGeneratorExtension/Public/Layout/Types/LayoutGameplayTags.h).

Useful built-in tags:
- faces: `FaceSolid`, `FaceExterior`, `FaceOpen`, `FaceEntry`, `FaceWalkway`, `FaceStair`
- walkable: `WalkableGround`, `WalkableUpper`, `WalkableRoof`
- roles: `RoleBoundary`, `RoleEntry`, `RoleCore`, `RoleInterior`, `RoleConnector`
- connectors: `ConnectorRoad`, `ConnectorTrail`
- child-profile examples: `ProfileHouse`, `ProfileMarket`

## Recipe: Make one surface site

### 1. Author the templates
Create Porism templates that fit the same cell volume.

Good first set:
- boundary wall
- entry or gate
- core building
- interior filler
- stair or upper-access piece

### 2. Wrap each template in a module asset
For each template, create a `ULayoutModuleAsset`.

Set:
- `Template`
- `CellSizeInBlocks`
- `SupportedIntents`
- `FaceRules`
- `WalkableAreas`
- `InternalAccessLinks`

Minimum rule of thumb:
- boundary pieces should expose exterior-facing boundary tags
- entry pieces should support `Entry`
- upper-access pieces should link `WalkableGround -> WalkableUpper`

### 3. Build the module set
Create one `ULayoutModuleSetAsset`.

Add:
- all module assets for the family
- compatibility rules for the face tags

Keep this simple at first:
- `FaceExterior` can meet empty space
- `FaceOpen` can meet `FaceOpen`
- `FaceEntry` can meet `FaceOpen` or connector-facing faces
- `FaceWalkway` can meet `FaceWalkway`
- `FaceStair` can meet matching vertical access

### 4. Create the layout profile
Create one `ULayoutProfileAsset`.

Recommended first settings:
- `PlannerMode = FootprintFill`
- `MinimumFootprintInCells = 3x3`
- `MaximumFootprintInCells = 5x5`
- `LevelCount = 1` or `2`
- `RequiredEntryCount = 1`
- `RequestedConnectorCount = 0`
- `RequestedChildHostCount = 0`
- `bAllowEmptyInteriorCells = false`

If the layout should sit on terrain:
- enable `bUseTerrainConformingPlacement`
- keep `bAllowFoundationFill = true`
- start with a small `MaxNeighborHeightDelta`

### 5. Preview it
Place an `ALayoutPreviewActor` in the editor.

Assign:
- the module set
- the layout profile
- a seed

Use preview first to verify:
- stable shape
- valid entries
- reachable upper areas
- no obviously bad face pairings

For previews in the **Layout Generator** window, use **Viewport > Performance**:
- **Merge Terrain Preview Blocks** defaults on. Adjacent clear/fill blocks share row bounds; holes and color changes stay separate. Disable it to inspect individual voxel boxes.
- **Minimum Detailed Marker Radius Pixels** defaults to `4`. Small projected cells use intent-colored points when Cell Zone Markers is enabled. Zoom in for the detailed spheres, zones, boundaries, Entry and vertical-access markers, or set `0` to keep full detail at every distance.

The tool culls off-screen cells and merged terrain rows. Footprint bounds, routes and interface diagnostics remain available. These options cover root and continuation previews in the editor tool, not `ALayoutPreviewActor`. They do not change solving, frozen terrain writes, or Apply.

### 6. Bind it to world generation
Create a `ULayoutWorldBindingAsset`. Set `BiomeRowNames`, weighted `Candidates` with their `LayoutProfile`, normal-cell dimensions and placement policy. Add continuation families on that binding when the roots should connect.

1. Use `AChunkWorldExtended` as the chunk-world actor.
2. Assign the binding assets to `LayoutRuntimeComponent` → `LayoutWorldBindings`.
3. Enable **Enable Automatic Layout Planning**. Possessed characters set priorities; **Follow Editor Camera** also includes the same-world perspective editor camera.
4. Start terrain generation and enable **Show Debug Stats** to inspect admission and placement.

Automatic planning considers eligible loaded terrain at every LOD. Centers set priority, not an admission radius. A loaded coarse chunk can become useful as a character approaches without another Created event. Discovery samples neighboring biome space across chunk boundaries; one chunk can hold several roots and one root can span several chunks.

`MaxCachedPlanningChunks` defaults to **256**, shared by the whole chunk world. It limits working chunks and, separately, unfinished automatic root/route owners, including solved-waiting results and canceled captures awaiting retirement. Compact current-loaded metadata is separate; this is not a RAM-byte limit. Under pressure, farther unstarted work yields before farther waiting results. Running work finishes. Eviction can lose an opportunity; revisiting can permit another solve. No centers retires automatic work while preserving loaded-root protection and explicit previews.

Fresh eligibility trusts native `Created` intent at every LOD. `Updated` alone neither grants fresh authority nor proves restoration. Realization requires full template/write/support coverage, rejects explicit restore marks and resolves support materials from actual available layer data before writing. The extension does not replace native save provenance. Accepted roots retain world-coordinate duplicate protection through overlapping LOD changes. Coarse visual output and finer-layer refresh still need validation in your world.

StopGen and ordinary StartGen reset transient planning state. Planning stores no disk metadata. Explicit Layout Generator solve/preview/Apply/Clear keeps its own lifetime, including stopped-world cached Apply. Removed radius/startup Blueprint calls need direct repair; no aliases, redirects or migrations replace them.

Realization consumes merged `SolveResult.Placements`, whose count may differ from regional artifact contributions. `SolvedArtifactPlacementCount` remains a compatibility diagnostic, not an acceptance checksum. Root-scoped active cells need not equal child-composed occupied cells; active-record validity and same-payload metadata still apply. An empty authored `BindingId` uses the asset name for planning and continuation lookup.

## Recipe: Add a road between layouts

Treat the road as its own layout family.

### 1. Export connector intent from the site profile
On the site `ULayoutProfileAsset`, add a connector family tag such as:
- `ConnectorRoad`

Make sure the solved site can export real entry cells.

### 2. Create road modules
Create a separate road module set.

### 3. Create a road profile
Create a `ULayoutProfileAsset` for the connector family.

Set:
- `PlannerMode = LinearConnector`
- `RequiredEntryCount = 2`

### 4. Bind the connector family
Create a connector binding row using `FLayoutConnectorBindingRow`.

Set:
- `ConnectorTypeTag = ConnectorRoad`
- the road profile
- the road module set

Assign that table to `LayoutConnectorBindings` on the runtime component.

## Recipe: Put child layouts inside a parent layout

### 1. Reserve child-host space on the parent profile
On the parent `ULayoutProfileAsset`, set:
- `RequestedChildHostCount`
- `AllowedChildProfileTags`
- optional `PreferredChildProfileTags`

### 2. Create child profiles and child module sets
Examples:
- house cluster
- market pocket
- courtyard structure

### 3. Bind child profiles to module sets
Create child binding rows using `FLayoutChildBindingRow`.

Assign that table to `LayoutChildBindings` on the runtime component.

The child solve happens after the parent solve. Child layouts use the reserved `ChildHost` space instead of resampling terrain as a brand new site.

## Recipe: Make a layout work on hills

Use this when you want a site to sit on real terrain without floating.

On the layout profile, enable `bSupportsSteppedTerrainSolve` when terrain should produce multiple stages.

On the world binding's placement policy (`DefaultPlacementPolicy.TerrainTransition`):
- enable `bAllowFoundationFill` for shallow support under the footprint
- set `MaxFoundationDepth` to the allowed support depth in blocks
- enable `bAllowPerimeterRampTransition` for perimeter joins; these use the same depth budget

Author at least one access-capable module if upper or offset cells must remain reachable:
- stair module
- ramp-like module
- stacked upper-access module

Prewarm uses the existing biome-noise sampler to plan support/ramp geometry and freeze support coordinates. After topology and lattice snapping settle, it computes support from the accepted cell bases rather than the initial sampling height. Only the lowest admitted cell in each footprint column owns foundation and perimeter-ramp writes; upper floors do not fill the building interior. Foundation writes include the template Z offset, while perimeter joins use the unoffset terrain base. The enabled policies, selected air intervals and existing depth budget still limit writes. Missing material during prewarm does not remove fill writes.

Realization waits on the existing fresh-chunk gate, including fill targets and every support-search chunk. Immediately before terrain writes, it searches downward from the predicted support block in the same XY column, using the first solid material within the frozen transition-depth allowance. A zero allowance retains exact-block lookup. This material-only search does not move or extend frozen geometry. Foundation and ramp fills inherit that material; ramp cuts remain explicit air. All support reads finish before any writes, so earlier excavation cannot change a later material sample. An unavailable support material fails the batch without partial terrain writes. No edited-terrain detection or reconciliation is added.

Automatic site admission enumerates normal-cell centers in bounded regions and retains overlapping alternatives until a root owns a spacing reservation. `SampleSpacing` caps region size, not final candidate stride. Retained maximum-footprint reservations can reject an entire region before capture only when every applicable candidate is excluded; partial regions remain searchable. Surface/cavity and cell-column/outer-corner biome checks still use the existing biome-noise utility across native chunk edges. Full selected-site preparation remains authoritative, so discovery does not guarantee solve or publication success.

Set binding **Occupancy Probability** to 1 for greedy admission, 0 to disable automatic roots for that binding, or an intermediate value for deterministic per-site rejection. The draw uses world seed, binding identity and snapped XYZ; retries, LOD changes and revisits do not reroll. Probability edits change the threshold, not the draw. This is not relative candidate Weight or a percentage of terrain coverage. Explicit Layout Generator, child layouts and continuations do not use this gate. The former candidate density policy was removed without migration; repair any Blueprint references to that removed property/type directly.

Restart the editor after loading new binaries, then re-solve cached previews. Older contracts retain their frozen geometry and material payloads; Apply does not repair an old zero-write plan.

## Recipe: Connect placed layouts

Automatic continuations require two placed, loaded roots with unused compatible exported entries. Each door must open toward the gap: inline doors face along their shared axis; diagonal doors can use orthogonal faces such as +X/-Y. Root centers alone do not prove clearance. Preparation rejects the full-width corridor when it intersects an actual solved root footprint, including a third root. It does not reroute around buildings. Publication and realization recheck current footprints before placement.

Corridor terrain sampling can cross other active biomes; root-binding biome rows do not restrict the space between roots. Surface, slope, bridge-gap, tunnel and support checks still apply.

Pairing prefers unconnected roots, then shorter routes, and continues through distinct unused pairs after failures. Failed pairs do not retry on unchanged inputs. There is no three-failed-alternatives cutoff. **Max Connections Per Site** still limits each root/family, including in-flight routes; set it above 1 when roots should connect to several neighbors. Each entry and each root pair/family supports one route. Existing async capacity limits concurrent work.

Connect Layouts shares these reservations with automatic continuations. A reserved endpoint reports an in-flight or waiting route rather than claiming the entry does not exist. Failed/canceled reservations release their entries; committed entries leave the selectable endpoint ledger. Choosing other doors on the same two roots does not bypass the one-route-per-family rule. The Generation Attempt panel shows continuation source selection even before either endpoint has been selected.

## Recipe: Debug a layout that fails

Check these in order:
1. Run asset validation on the module asset, module set, and profile.
2. Verify every module in the set uses the same `CellSizeInBlocks`.
3. Verify the `Entry` requirement is achievable with your modules.
4. Verify face tags and allowed-neighbor tags agree with the module-set compatibility rules.
5. Verify upper walkable areas have an internal access path from ground.
6. If terrain-conforming placement is enabled, relax `MaxNeighborHeightDelta` or enlarge the cell size only if the terrain fit is too strict for the intended shape.

## Chunk World Extended debug controls

Select the actor and open **Porism Extension - Shared Diagnostics**:

- **Show Debug Stats** controls both terrain and layout text using the existing `ShowDebugData` setting (native default: on). It does not enable ownership probes. Editor layout text requires **On Screen Debug**, not **Show Stats**; native terrain text still follows engine screen-message settings.
  - Layout stats separate loaded-directory size, working `used/N`, unfinished owners, frontier/backlog, solves, waiting results, placements, exhausted areas and eviction/cancellation. Stage-success totals are cumulative and do not count placements. Saturation means admission must wait or evict; inspect the waiting reason before changing capacity.
- **Detailed Diagnostics** controls editor/planning traces, biome-noise ownership messages, and request-scoped timeout diagnostics. Default: off; saved overrides remain. Routine diagnostic skip messages follow this flag. Existing solver/validation warnings and errors remain available without it.
  - Enable before starting an automatic or Layout Generator root solve, then filter the editor log for `[LayoutSolveDiag]`. **Show Debug Stats** is not required for these logs.
  - Correlate automatic requests by `descriptor`, explicit requests by `request`. Each includes origin, profile, site and seed. `event=request` records requested mode and limits, not the final fallback mode.
  - Phase rows separate site preparation, prewarm, preflight and solve. `queueWaitMs` measures time from stage binding to worker entry; `remainingMs`, work totals/deltas and publication rows distinguish spent allowance, queue delay and worker failure. `budgetBound=0` means this phase has no operation ledger; `deadlineEnabled=0` means no wall-clock deadline.
  - `event=terrain-writes` records effective fill/ramp toggles, depth limit, template offset, cell/interval/perimeter counts, accepted base-Z and interval-floor ranges, and generated write count. It runs at ordinary-root write production, before publication. Zero writes here means realization has no fill geometry to apply. Ranges include all supplied cells/intervals; empty collections retain integer-extreme sentinels and report zero counts.
  - `event=budget-stop` captures the first stop of each class before optional scopes unwind: `kind=1` cancellation, `2` hard work limit, `4` hard deadline, `8` optional allowance. An optional stop alone is not a terminal failure; inspect the later phase/publication result.
  - Negotiation summaries distinguish parent and nested child regions, normalization, entry negotiation, validation and memo reuse. Times are inclusive; do not add nested rows. At most 16 region rows plus one suppression notice are emitted per operation; stage work rows retain the slowest observed region summary.
  - Diagnostics use captured strings and execution-only state, not frozen requests or persistent caches. Logging adds overhead; enable for reproduction, then disable for normal play. No solve limits or search rules change.
- **Advanced Visualizations** groups independent **Chunk Boundaries**, **Collision Shapes**, **Template Candidates**, and **Chunk Material Colors** controls, plus the active template-marker distance and minimum far-marker size settings. The unused **Template Marker Lifetime** is hidden on the extended actor; its third-party declaration is untouched.

Stats and detailed-diagnostics edits do not restart terrain. Other actor edits retain native refresh behavior and restore component registration afterward so planning ticks, async completion processing, and the editor HUD resume. They do not rerun actor construction, which would discard runtime generator state. Generation controls, camera following, and Layout Generator preview controls remain separate.

`GetDebugGenerationStats` / `SetDebugGenerationStats` access the owner's shared stats switch; ownerless components report disabled and retain no separate state. Use the actor's `GetDetailedDiagnostics` / `SetDetailedDiagnostics` for traces (`bDetailedDiagnostics`). Deprecated planning-log aliases and the layout-only serialized stats flag have been removed. There is no load migration or property redirect; update old Blueprint references directly.

## Finding actor and component settings

- Select the actor for **Porism Extension - Chunk World Extended** (the authoritative block schema registry) and **Porism Extension - Shared Diagnostics** (world-wide terrain/layout diagnostics).
- In the Components tree, select **LayoutRuntimeComponent**, **BlockFeedbackComponent**, **BlockSwapComponent**, or **BlockSwapScannerComponent**. Their settings use **Porism Extension - Layout Runtime**, **Block Feedback**, **Block Swap**, and **Block Swap Scanner** headings respectively. Planning, Async, and Pooling remain separate subsections where applicable. Block Schema has no separate editable registry; set it on the actor.
- Component settings are not repeated inline on actor selection. Selecting a component remains the editing path for both placed actors and Blueprint component defaults. Existing values and Blueprint APIs are unchanged; no migration is needed.
- Inherited terrain categories are labeled **Porism Terrain - World Definition**, **Features**, **Save and Cache**, **Memory**, **Workers**, and **Streaming**. Standard Unreal categories retain their usual labels.

## Recommended setup order
If you are starting from zero, do this in order:
1. Build one small surface site with preview only.
2. Bind it to one biome row on `AChunkWorldExtended` and verify runtime realization.
3. Add a connector family.
4. Add child layouts.
5. Add terrain-conforming behavior.

That order keeps failures small and makes it obvious whether the problem is:
- module authoring
- profile planning
- world binding
- terrain fit
- connector or child-layout integration
