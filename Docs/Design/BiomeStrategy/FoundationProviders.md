# Foundation Providers

## Purpose
Foundation providers are the terrain-body side of a biome strategy.

They create the broad domain that a Porism biome row can own, then provide `GenA` terrain density for visible solid terrain inside that domain. Providers can also own reservations and child providers.

## Shared Rules
- Additive providers need a valid `BiomeTag`.
- The provided `Biome.*` tags are defaults; projects can add more gameplay tags when they need additional generated biome categories.
- Subtractive providers carve their parent branch and do not need a biome row.
- Provider payload dimensions are authored in blocks.
- Terrain profile payloads affect additive `GenA`, not provider identity. Pit Void uses the same profile slot for its subtractive floor.

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

Terrain comes from the provider `TerrainProfile` slot. The island provider supports flat, noisy, rolling hills, eroded edge, crevice, bowl, caldera, ridged, and terraced profiles.

Set `IslandBody.bCylindrical` for a circular slab with vertical sides. `TopHeight + BottomDepth` sets thickness; `Center.Z` remains the top-face reference. Both faces follow the terrain profile. Flat terrain with integer thickness two produces two solid voxel layers across the disk, including spawn reservations. Noise can round fractional face positions to an extra sampled layer. Cylinder mode ignores floating-island silhouette/detail controls and clips reservation terrain to the slab, so a deep spawn box cannot thicken it.

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

## Arch Bridge Provider
`EFoundationProviderType::ArchBridge`, payload `FArchBridgeFoundationPayload`.

Author 2-32 `DeckPoints` in block coordinates. Each consecutive pair forms a span with a rounded horizontal footprint, a top that interpolates between endpoint heights, and a parabolic underside. `CrownThicknessBlocks` sets thickness at midspan; `ArchRiseBlocks` adds underside depth at the ends. Zero rise makes a slab.

The shared `TerrainProfile` shapes the deck. Negative offsets cannot remove the whole bridge: the builder retains two blocks above the underside, or the scaled crown thickness when smaller. Positive noise expands the analytic domain envelope without adding noise evaluation to ownership.

Example: points `(-120,0,40)`, `(0,0,40)`, `(120,80,55)`, half-width `12`, crown thickness `8`, arch rise `35`. This produces two joined arches, including a sloping second deck.

To join islands:
1. Put endpoint positions inside their solid terrain, not just inside their domain envelopes.
2. Use the same biome tag for a continuous material/terrain union.
3. Nest overlapping providers instead of overlapping siblings; sibling-overlap validation still applies.
4. Add another same-tag bridge child for a branch. Points are explicit coordinates; the provider does not discover, snap to, or reroute between generated islands.

## Mountain Provider
`EFoundationProviderType::Mountain`, payload `FMountainFoundationPayload`.

`BaseCenter` defines the finite base. `BaseRadiiBlocks` defines the elliptical footprint; `HeightBlocks` defines the summit. `SlopePower = 1` gives a cone; larger values narrow its upper slopes. `SummitRadiusFraction` truncates the peak into a flat summit.

The terrain profile adds detail with a falloff toward the base edge. A mountain does not extend underground indefinitely: overlap its base with a parent island or plane to join it to ground.

For a range, use a Multi-Instance Mountain prototype. Choose line, grid or ring arrangement, and disable `bRequireNoOverlap` when peaks should merge. The resolved same-tag terrain unions the overlapping mountains. Unequal X/Y base radii also make an elongated ridge.

## Column / Mesa Provider
`EFoundationProviderType::Column`, payload `FColumnFoundationPayload`.

The body interpolates between elliptical `BaseRadiiBlocks` and `TopRadiiBlocks` over `HeightBlocks`. Small radii make columns; broad top radii make buttes or mesas. Either radius pair may be larger, allowing tapered or overhanging silhouettes.

Wall and top detail are separate:
- `WallNoiseAmplitudeBlocks` controls side displacement along the shorter radius. Keep it below half the smallest radius.
- `WallNoiseScale` controls horizontal detail frequency in noise coordinates.
- `WallVerticalScale = 0` gives vertical flutes; a small positive value varies the walls with height.
- `TerrainProfile` controls the top surface. Large negative offsets retain a finite body instead of cutting through the base.

Starting examples:
- Column: base radii `(22,22)`, top radii `(15,15)`, height `75`, wall amplitude `4`.
- Mesa: base radii `(90,65)`, top radii `(75,50)`, height `60`, wall amplitude `6`.

Use Multi-Instance for groups of columns. Wall noise uses the world noise seed plus `WallSeedOffset`.

## Pit Void And Caldera Provider
`EFoundationProviderType::PitVoid`, payload `FPitFoundationPayload`.

Pit Void is subtractive. It has no biome row or reservations. `Center.Z` is the opening, `OpeningRadiiBlocks` defines its elliptical outline, and `DepthBlocks` defines the maximum downward extent. `BottomRadiusFraction` controls the flat floor radius before the bowl slopes rise toward the rim.

`RimNoiseAmplitudeBlocks` and `RimNoiseScale` roughen the outline. The provider's `TerrainProfile` shapes its floor; the builder clamps displacement between the maximum depth and the opening. Both belong in the subtractive domain because they determine which parent terrain disappears. `OpeningTopPaddingBlocks` extends the cut above the opening.

For a caldera, add a Pit Void child to a Mountain:
- Mountain base `(0,0,0)`, radii `(100,60)`, height `80`, slope power `1`.
- Pit center `(0,0,82)`, opening radii `(30,30)`, depth `40`, bottom fraction `0.25`.
- Start with Flat floor and zero rim noise; then add profile/rim detail.
- Keep the pit narrower than the mountain. Raise its opening or top padding when summit noise extends above the cut.

For a ground pit, put the same void beneath an island or plane and set its opening at the ground surface. The subtraction affects its parent branch; overlapping additive branches can still fill that space.

## Landform Queries And Limits
Bridge, Mountain, Column and Pit Void expose conservative finite bounds, including noise displacement. They do not implement direct surface-height queries or surface-anchor reservations. Validation rejects direct reservations rather than silently omitting them; use an island or plane child as the host for build pads or spawn fields.

Dimensions are authored in blocks. Multi-Instance scales positions, dimensions, wall/rim amplitudes and terrain-profile amplitudes, then applies instance offsets. Noise frequencies keep their existing world-noise-coordinate meaning. Finite input checks reject invalid payloads, degenerate spans and nonfinite values. Features smaller than a voxel may not survive terrain sampling.

No generated assets or material instructions are supplied by these provider types. Bind them through the existing strategy/tag/slot wrappers described in [WorldGen setup](../../Usage/BiomeStrategyWorldGenSetup.md). Existing rows and worlds remain unchanged until you author providers into their strategy.

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

Variation can apply deterministic Z offset and uniform scale per instance. Validation rejects overlapping finite island or landform footprints when `bRequireNoOverlap` is enabled. Landform checks use conservative XY bounds, including footprint-center movement from scaling an off-origin prototype. Disable this option for intentional overlapping mountain ranges or connected terrain.

## Terrain Profiles
Foundation terrain profiles are `GenA` surface-height profiles for additive providers, and floor-height profiles for Pit Void subtraction. The profile families below work with the landforms. Eroded Edge uses the provider's finite radius when its override is zero; Bridge uses half-width, Mountain uses its shorter base radius, Column uses its shorter top radius, and Pit uses its shorter opening radius. Radial profiles use the provider center, or the first deck point for Bridge. Selecting a provider preserves an already compatible profile; choose Flat explicitly for a clean initial shape.

Available payload families include:
- `FFlatFoundationTerrainProfilePayload`
- `FNoisyFoundationTerrainProfilePayload`
- `FRollingHillsFoundationTerrainProfilePayload`
- `FErodedEdgeFoundationTerrainProfilePayload`
- `FCreviceFoundationTerrainProfilePayload`
- `FBowlFoundationTerrainProfilePayload`
- `FCalderaFoundationTerrainProfilePayload`
- `FRidgedFoundationTerrainProfilePayload`
- `FTerracedFoundationTerrainProfilePayload`

Caldera supplies a flat crater floor, a linear climb to the rim, and a linear exterior descent to the provider surface. Keep reservation corners inside `FloorRadiusBlocks`; tune slope using `(RimHeightBlocks - FloorHeightBlocks) / (RimRadiusBlocks - FloorRadiusBlocks)` and `RimHeightBlocks / (BaseRadiusBlocks - RimRadiusBlocks)`. These are geometric grades, not a replacement for collision/navmesh checks. Unlike Pit Void, this profile adds terrain and keeps the island's surface-query/reservation support.

For additive providers, terrain profile detail stays out of `DomainNoise`. Their domains expand to contain possible positive terrain height. Pit Void evaluates its floor profile in `DomainNoise` because subtraction has no replacement `GenA` row.

## Related Source
- [BiomeStrategyData.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Strategy/BiomeStrategyData.h)
- [IslandBiomeStrategyPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Island/IslandBiomeStrategyPayloads.h)
- [InfinitePlaneFoundationPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Foundation/InfinitePlaneFoundationPayloads.h)
- [LandformFoundationPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Foundation/LandformFoundationPayloads.h)
- [VoidFoundationPayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Foundation/VoidFoundationPayloads.h)
- [FoundationTerrainProfilePayloads.h](../../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Foundation/FoundationTerrainProfilePayloads.h)
