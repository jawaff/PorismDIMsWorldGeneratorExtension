# Biome Strategy WorldGen Setup

## Purpose
Use this guide when you want one `UBiomeStrategyData` asset to drive Porism biome rows through shared FastNoiseEditor wrappers.

## Short Version
You need:
1. one `UBiomeStrategyData` asset
2. one `BiomeTag` per strategy-backed biome you want to expose
3. one `UBiomeFastNoiseEditor` for the `DomainNoise` slot of that tag
4. one `UBiomeFastNoiseEditor` for the `GenA` slot of that tag

## Create The Strategy Asset
Create a `UBiomeStrategyData` asset.

Configure:
- `RootFoundationProvider`
- provider `BiomeTag`
- provider payload and terrain profile
- provider-owned reservations
- reservation `BiomeTag`
- reservation payload and terrain payload

Use built-in tags such as:
- `Biome.Foundation.Main`
- `Biome.Foundation.Secondary`
- `Biome.Reservation.Center`

These are just the default tags provided by the plugin. You can add project-specific `Biome.*` gameplay tags to expand the biomes supported by the biome generator. The important thing is that the same tag is used by the strategy entry and the matching Porism biome row wrappers.

## Add The FastNoiseEditor Wrappers
For each strategy `BiomeTag` that should be exposed to Porism, create or configure two `UBiomeFastNoiseEditor` wrappers.

Domain wrapper:
- `Strategy = your UBiomeStrategyData`
- `NoiseSlot = DomainNoise`
- `BiomeTag = the strategy biome tag to expose`

Terrain wrapper:
- `Strategy = the same UBiomeStrategyData`
- `NoiseSlot = GenA`
- `BiomeTag = the same strategy biome tag`

Assign those wrappers to the corresponding Porism biome row fields using Porism's normal `WorldGenDef` workflow.

The extension binding comes from the wrapper's `Strategy` and `BiomeTag`. The Porism row name remains Porism's own row identity.

## Minimal Foundation And Reservation Example
Use one foundation row:
- strategy provider `BiomeTag = Biome.Foundation.Main`
- `DomainNoise` wrapper for `Biome.Foundation.Main`
- `GenA` wrapper for `Biome.Foundation.Main`

Use one reservation row:
- strategy reservation `BiomeTag = Biome.Reservation.Center`
- `DomainNoise` wrapper for `Biome.Reservation.Center`
- `GenA` wrapper for `Biome.Reservation.Center`

The reservation row can own a flatter pad or structure pocket while the foundation row owns the broader terrain around it.

## Child Provider Rows
If a child provider uses the same `BiomeTag` as its parent, it is added to that parent biome. You do not need another Porism row just for that child.

If a child provider uses a different `BiomeTag`, the strategy subtracts the child domain from the parent biome automatically. Expose the child tag with its own matching `DomainNoise` and `GenA` wrappers when that child biome should be visible to Porism.

If a child provider is `SubtractiveVoid`, it only carves from the parent biome. It does not need wrappers because it does not contribute a replacement biome.

## Things That Usually Go Wrong
- The `DomainNoise` wrapper and `GenA` wrapper point at different strategies or different tags.
- A provider or reservation has a tag in the strategy, but no matching wrapper pair exposes that tag.
- A child provider uses a different tag and is correctly carved from the parent, but the child tag is not exposed with wrappers.
- Expensive terrain detail is placed in `DomainNoise` instead of a terrain profile or reservation `GenA`.

## Useful Diagnostics
Use `UBiomeStrategyBindingLibrary::QueryBiomeStrategyRowBindings` to inspect the active `WorldGenDef`.

It reports:
- which rows use strategy wrappers
- whether both `DomainNoise` and `GenA` wrappers are present
- the selected `BiomeTag`

Use `QueryWorldGenReservationFields` when another system needs the resolved reservation or spawn fields that correspond to actual authored biome rows.

## Related Docs
- [../Design/BiomeStrategy.md](../Design/BiomeStrategy.md)
- [../Design/BiomeStrategy/FoundationProviders.md](../Design/BiomeStrategy/FoundationProviders.md)
- [../Design/BiomeStrategy/ReservationProviders.md](../Design/BiomeStrategy/ReservationProviders.md)
