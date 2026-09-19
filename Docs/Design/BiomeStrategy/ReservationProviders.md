# Reservation Providers

## Purpose
Reservations are provider-owned pockets that can become their own Porism biome rows.

They are useful for build pads, structure hosts, spawn-readable subfields, and any surface area where a foundation biome should yield ownership to a more controlled biome.

## Shared Rules
- Each reservation needs a valid `BiomeTag`.
- The provided reservation biome tags are defaults; projects can add more `Biome.*` gameplay tags when they need additional reservation biome rows.
- A reservation is authored under the foundation provider it belongs to.
- Surface-bound reservations resolve against the owning provider surface before applying lift or offset.
- Reservations with the same `BiomeTag` are combined into the same Porism biome row.
- `FieldTags` and `SpawnFieldTags` are query metadata. They do not create Porism biome rows by themselves.

## Surface Anchor Reservation
`EReservationType::SurfaceAnchor`

Payload:
- `FSurfaceAnchorReservationPayload`

Use this for a shallow surface-local volume centered on an intended foundation surface.

Shape modes:
- sphere, using `SphereRadii`
- box, using `BoxHalfExtent`

Height controls:
- `SurfaceHeightSolveMode` chooses how the foundation surface is sampled under the footprint
- `SurfaceZLift` raises or lowers the anchor with foundation support or cutting
- `SurfaceZOffset` applies raw placement after lift

Terrain payloads:
- `FSurfaceAnchorFlatTerrainPayload`
- `FSurfaceAnchorBlendedSupportTerrainPayload`
- `FSurfaceAnchorNoisyTerrainPayload`

Flat terrain creates an exact support pad. Blended support keeps a flat center and eases the edge toward the solved foundation surface. Noisy terrain adds authored surface variation and optional terraces.

## Multi-Instance Surface Anchor Reservation
`EReservationType::MultiInstanceSurfaceAnchor`

Payload:
- `FMultiInstanceSurfaceAnchorReservationPayload`

Use this when one provider-local surface-anchor prototype should repeat deterministically.

Supported arrangement modes:
- line
- grid
- ring

Variation can apply deterministic extra `SurfaceZOffset` and uniform scale per reservation instance. Validation can reject overlapping finite reservation footprints when `bRequireNoOverlap` is enabled.

## Spawn Fields
A reservation can expose a spawn-readable subfield by enabling `bEnableSpawnReservation`.

Spawn fields are returned by query helpers and have their own `SpawnFieldTags`. They are metadata fields unless you intentionally add matching Porism biome rows for spawn-specific terrain, materials, or ownership.

## Query Fields
`UBiomeStrategyData::QueryTaggedReservationFields` returns resolved reservation and spawn field bounds in authored block coordinates.

`UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields` narrows those fields to the concrete `WorldGenDef` rows that expose the matching `BiomeTag`.

Use the binding-library query when systems need to know which reservation fields are actually represented by authored Porism biome rows.

## Related Source
- [BiomeStrategyData.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Strategy/BiomeStrategyData.h)
- [SurfaceAnchorReservationPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Reservation/SurfaceAnchorReservationPayloads.h)
- [BiomeStrategyBindingLibrary.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h)
