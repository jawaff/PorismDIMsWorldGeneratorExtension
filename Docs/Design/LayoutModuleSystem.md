# Layout Module System

## Purpose
The layout module system assembles Porism templates into deterministic multi-cell layouts such as compounds, roads, nested site interiors, and other authored structures.

It is designed to:
- keep one module equal to one `UChunkStructureTemplate`
- solve on a fixed-size cell grid
- match neighboring cells through face tags and occupancy rules instead of collision checks
- reserve stable `Entry` cells so layouts can connect to other layouts
- support stacked vertical cells for multi-level structures
- conform placement to terrain where the selected profile allows it
- discover and solve lazily so infinite chunk worlds do not require a global startup pass

## Core pieces

### Template payload
- `UChunkStructureTemplate`
- Owns the actual block and mesh content
- Does not own layout semantics

### Module metadata
- `ULayoutModuleAsset`
- Wraps one Porism template
- Declares:
  - shared cell size
  - supported intents such as `Boundary`, `Entry`, `Core`, `Interior`, `Connector`, `ChildHost`
  - per-face tags and occupancy policy
  - exposed walkable areas
  - internal access links

### Module family
- `ULayoutModuleSetAsset`
- Holds all modules that can solve one family
- Owns face-tag compatibility rules
- Enforces a shared cell size across the set

### Layout behavior
- `ULayoutProfileAsset`
- Describes how one family should plan and solve
- Owns:
  - footprint size range
  - level count
  - required entry count
  - optional connector and child-host reservation counts
  - terrain-conforming rules
  - site-selection settings

### Runtime bridge
- `UChunkWorldLayoutRuntimeComponent`
- Discovers sites from loaded chunk windows
- Caches solved site, child, and connector records
- Realizes layouts only after Porism has already loaded the required chunks

## Authoring model
The system is intentionally split so Porism templates stay simple.

- geometry lives in `UChunkStructureTemplate`
- layout semantics live in `ULayoutModuleAsset`
- compatibility lives in `ULayoutModuleSetAsset`
- planning behavior lives in `ULayoutProfileAsset`
- world integration lives in project-owned data tables

This keeps the layout system generic and avoids pushing project-specific semantics into Porism template assets.

## Runtime flow
1. A loaded chunk window is observed by `UChunkWorldLayoutRuntimeComponent`.
2. Host-biome bindings decide whether the area can produce a layout site.
3. Site selection derives a deterministic site center and solve seed.
4. The planner emits broad intents such as `Boundary`, `Entry`, `Core`, `Interior`, `Connector`, and `ChildHost`.
5. The solver chooses compatible modules per occupied cell.
6. Terrain helpers validate the footprint and resolve anchor heights when terrain-conforming placement is enabled.
7. Child layouts and connector layouts are derived from the solved parent records when relevant.
8. The solved records are realized only after the required chunks have been observed as loaded.

## World integration tables

### Host-biome bindings
`FLayoutHostBiomeBindingRow`
- biome row name
- site layout profile
- site module set
- site-selection settings

### Connector bindings
`FLayoutConnectorBindingRow`
- connector family tag
- connector layout profile
- connector module set
- max per-site connections
- max connection distance

### Child bindings
`FLayoutChildBindingRow`
- child profile
- child module set

## Terrain model
Terrain-aware layouts do not reshape templates.

Instead, the system:
- samples actual chunk-world terrain
- resolves per-cell anchor heights
- rejects unsupported footprints
- allows limited support fill when enabled
- keeps reachability driven by authored walkable links rather than navmesh analysis

This is enough for hillside-friendly layouts without adding heavy per-template configuration.

## Connectors and child layouts
Connectors are their own layout family.

- sites export stable `Entry` cells and connector-family tags
- connector bindings map that exported family to a dedicated connector profile and module set
- child layouts solve inside reserved `ChildHost` regions after the parent solve completes

## Main constraints
- Every module in one module set must use the same cell size.
- One module always maps to one template and one occupied cell.
- Vertical complexity comes from stacked cells, not multi-cell templates.
- Face compatibility and occupancy rules are the main local contract.
- Reachability is graph-based through face walkable areas and internal access links.

## Recommended entry points
- `Source/PorismDIMsWorldGeneratorExtension/Public/Layout/Assets/`
- `Source/PorismDIMsWorldGeneratorExtension/Public/Layout/Components/ChunkWorldLayoutRuntimeComponent.h`
- `Source/PorismDIMsWorldGeneratorExtension/Public/Layout/Actors/LayoutPreviewActor.h`
- `Source/PorismDIMsWorldGeneratorExtension/Public/Layout/Types/LayoutTypes.h`
- `Source/PorismDIMsWorldGeneratorExtension/Public/Layout/Types/LayoutGameplayTags.h`
