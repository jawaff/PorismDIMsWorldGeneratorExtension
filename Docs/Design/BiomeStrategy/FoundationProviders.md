# Foundation Providers

## Purpose
Foundation providers are the terrain-body side of a biome strategy.

They create the broad domain that a Porism biome row can own, then provide `GenA` terrain density for visible solid terrain inside that domain. Providers can also own reservations and child providers.

## Shared Rules
- Additive providers need a valid `BiomeTag`.
- The provided `Biome.*` tags are defaults; projects can add more gameplay tags when they need additional generated biome categories.
- Subtractive providers carve their parent branch and do not need a biome row.
- Provider payload dimensions are authored in blocks.
- Terrain profile payloads affect `GenA`, not provider identity.

## Contribution Types
`EFoundationContributionType::AdditiveBiome` adds provider terrain to the biome selected by `BiomeTag`.

Use it for terrain that should be represented by a Porism biome row. The row still needs matching `DomainNoise` and `GenA` wrappers for that tag.

`EFoundationContributionType::SubtractiveVoid` removes terrain from the parent branch without adding a replacement biome.

Use it for caves, trenches, cuts, voids, and other authored holes where no child biome should own the carved space.

## Child Provider Composition
Child providers are not independent islands of logic. Their domains are composed into or out of the parent provider's `DomainNoise`.

Same-tag additive children are added to the parent biome:
- parent `BiomeTag = Biome.Foundation.Main`
- child `BiomeTag = Biome.Foundation.Main`
- result: parent and child domains are unioned into the `Biome.Foundation.Main` row

Different-tag additive children replace parent ownership in their own domain:
- parent `BiomeTag = Biome.Foundation.Main`
- child `BiomeTag = Biome.Foundation.Secondary`
- result: the child's domain is subtracted from the parent row and emitted into the `Biome.Foundation.Secondary` row

Subtractive void children carve only:
- parent `BiomeTag = Biome.Foundation.Main`
- child `ContributionType = SubtractiveVoid`
- result: the child's domain is subtracted from the parent row and no replacement biome is emitted

This automatic parent-domain subtraction keeps surface ownership readable in the Porism biome table. A different-tag child only needs its own matching biome row; the parent row is already carved by the strategy.

## Island Provider
`EFoundationProviderType::Island`

Payload:
- `FIslandFoundationShapePayload`

Use this for finite floating island style terrain.

Important settings:
- `IslandBody.Center` is the authored visible top-center surface reference
- `TopRadius`, `TopHeight`, `BottomDepth`, and `RimThickness` describe the island body
- body detail can add rim, underside, and optional bottom-spike noise
- `DomainRadiusPadding` and `DomainVerticalPadding` expand the cheap domain envelope

Terrain comes from the provider `TerrainProfile` slot. The island provider supports reusable foundation terrain profiles such as flat, noisy, rolling hills, eroded edge, crevice, bowl, ridged, and terraced profiles.

## Infinite Plane Provider
`EFoundationProviderType::InfinitePlane`

Payload:
- `FInfinitePlaneFoundationPayload`

Use this for unbounded X/Y terrain with a provider-local flat surface.

Important settings:
- `SurfaceZ` is the authored visible top face
- `DomainTopPadding` keeps biome-owned air above the surface for support and top-layer rules
- `DomainBottomDepth` controls owned volume below the surface
- `DomainMask` can shape ownership without changing `GenA`

Domain mask payloads:
- `FInfiniteFoundationNoDomainMaskPayload`
- `FInfiniteFoundationNoisePatchDomainMaskPayload`
- `FInfiniteFoundationRepeatedHolesDomainMaskPayload`

Domain masks affect `DomainNoise` ownership only. Surface shape still comes from the provider `TerrainProfile`.

## V-Cut Void Provider
`EFoundationProviderType::VCutVoid`

Payload:
- `FVCutFoundationPayload`

Use this for finite subtractive ravine, trench, or V-shaped cuts.

Important settings:
- V-cut providers are subtractive voids
- they do not create terrain or own reservations
- `Center.Z` is the top opening of the cut
- `HalfLengthBlocks`, `SurfaceHalfWidthBlocks`, `BottomHalfWidthBlocks`, and `DepthBlocks` define the clean cut
- optional organic variation can add wall noise, centerline wander, domain warp, and stepped side walls

Organic variation belongs in the subtractive `DomainNoise` because the missing shape is ownership and carve behavior, not visible surface terrain.

## Multi-Instance Provider
`EFoundationProviderType::MultiInstance`

Payload:
- `FMultiInstanceFoundationPayload`

Use this when one prototype foundation should repeat deterministically in a line, grid, or ring.

The multi-instance provider owns the repeated instances' contribution type, biome tag, and debug identity. The prototype defines:
- provider type
- provider payload
- terrain profile
- provider-owned reservations
- repeated child foundations

Supported arrangement modes:
- line
- grid
- ring

Variation can apply deterministic Z offset and uniform scale per instance. Validation can reject overlapping finite instance footprints when `bRequireNoOverlap` is enabled.

## Terrain Profiles
Foundation terrain profiles are `GenA` surface-height profiles for additive providers.

Available payload families include:
- `FFlatFoundationTerrainProfilePayload`
- `FNoisyFoundationTerrainProfilePayload`
- `FRollingHillsFoundationTerrainProfilePayload`
- `FErodedEdgeFoundationTerrainProfilePayload`
- `FCreviceFoundationTerrainProfilePayload`
- `FBowlFoundationTerrainProfilePayload`
- `FRidgedFoundationTerrainProfilePayload`
- `FTerracedFoundationTerrainProfilePayload`

Terrain profile detail should stay out of `DomainNoise`. Provider domains only expand enough to contain the possible positive terrain height when a profile can raise visible terrain.

## Related Source
- [BiomeStrategyData.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Strategy/BiomeStrategyData.h)
- [IslandBiomeStrategyPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Island/IslandBiomeStrategyPayloads.h)
- [InfinitePlaneFoundationPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Foundation/InfinitePlaneFoundationPayloads.h)
- [VoidFoundationPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Foundation/VoidFoundationPayloads.h)
- [FoundationTerrainProfilePayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Foundation/FoundationTerrainProfilePayloads.h)
