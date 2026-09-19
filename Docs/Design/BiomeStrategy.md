# Biome Strategy

## Purpose
The biome strategy system is a reusable authoring layer for Porism biome rows.

It keeps the shape, terrain, and reservation settings in one `UBiomeStrategyData` asset, then exposes those settings to Porism through `UBiomeFastNoiseEditor` classes assigned on `WorldGenDef` biome rows.

The goal is to avoid duplicating noise settings across separate `DomainNoise` and `GenA` classes while still using Porism's normal biome table workflow.

## Core Model
`UBiomeStrategyData` owns a provider hierarchy.

- foundation providers create broad terrain bodies or subtract from parent bodies
- reservations create tagged surface pockets inside a provider
- each additive provider or reservation has a `BiomeTag`
- each Porism biome row selects one `BiomeTag`
- `UBiomeFastNoiseEditor` selects the Porism noise slot for that tag

The strategy asset does not replace the Porism biome table. The biome table still owns Porism-specific row behavior, while the extension owns the shared strategy data and wrapper-generated noise fields.

## Biome Tags
Biome tags are an intentional customization point.

The plugin provides default tags such as `Biome.Foundation.Main`, `Biome.Foundation.Secondary`, and `Biome.Reservation.Center`, but projects can add their own `Biome.*` gameplay tags to expand the set of biomes supported by the generator.

The important rule is consistency: the provider or reservation `BiomeTag` in the strategy asset must match the `BiomeTag` selected by the `UBiomeFastNoiseEditor` wrappers on the corresponding Porism biome row.

## Porism Slots
`DomainNoise` controls biome ownership. Positive output means the biome exists at a sample. When multiple rows are positive, Porism chooses by domain strength and row behavior.

`GenA` controls terrain density for positions owned by that biome. In Porism's terrain polarity, positive values are air and zero or negative values are solid.

The strategy intentionally keeps those jobs separate:
- provider and reservation domains should stay cheap and mostly analytic
- foundation `GenA` should create visible terrain fill and top surface
- reservation `GenA` should create controlled terrain inside the reservation domain

## FastNoiseEditor Wrapper
`UBiomeFastNoiseEditor` is the Porism-facing noise class.

Each wrapper has:
- `Strategy`
- `NoiseSlot`
- `BiomeTag`

A normal visible biome row needs two wrapper assets or classes using the same strategy and tag:
- one wrapper with `NoiseSlot = DomainNoise`
- one wrapper with `NoiseSlot = GenA`

This keeps `DomainNoise` and `GenA` synchronized without copying settings between unrelated FastNoiseEditor classes.

## Provider Hierarchy
Foundation providers are evaluated as a tree.

- provider-owned reservations are placed relative to that provider's surface or top-center frame

Each provider has a contribution type:
- `AdditiveBiome` means the provider can add terrain to the biome selected by its `BiomeTag`
- `SubtractiveVoid` means the provider only removes domain from its parent branch

Child providers automatically affect the parent's `DomainNoise` based on their contribution type and tag relationship:
- same-tag additive children are added to the parent provider's biome, so their domain is unioned into the same `BiomeTag`
- different-tag additive children are subtracted from the parent provider's biome and also emitted into their own `BiomeTag`
- subtractive void children are subtracted from the parent provider's biome and do not emit a replacement biome

That automatic subtraction is the main reason to model related terrain as a provider hierarchy instead of isolated biome rows. A child island, plane patch, or reservation-owning provider with a different tag can claim its own biome without leaving both parent and child domains fighting over the same surface.

For provider families, see [FoundationProviders.md](./BiomeStrategy/FoundationProviders.md).

## Reservations
Reservations are provider-owned fields for build pads, structure anchors, spawn-readable subfields, and similar biome-owned pockets.

A reservation contributes to the biome row selected by its `BiomeTag`. Multiple reservations with the same tag are combined into the same row. If reservations with the same tag use visibly different terrain payload families, validation reports a warning because one Porism `GenA` row now has mixed terrain behavior.

For reservation families, see [ReservationProviders.md](./BiomeStrategy/ReservationProviders.md).

## Scale And Seed
Runtime noise construction resolves scale and seed from the exact chunk-world creator context when possible.

`UBiomeStrategyData::ScaleOverride` and `FallbackStrategySeed` are fallback paths for editor previews, tests, or non-runtime builds. They should not be treated as a way to override the live `WorldGenDef` and chunk-world scale during normal generation.

Authored provider and reservation dimensions are in block units. The strategy converts those block units into Porism FastNoise coordinates during wrapper construction.

## Validation
`UBiomeStrategyData::ValidateStrategy()` reports structured validation issues with severity, debug path, message, and fix text.

Blocking errors stop runtime noise construction and return constant-zero noise. Warnings are tuning guidance and do not block construction.

The validation checks include:
- missing or invalid biome tags
- mismatched provider and reservation payloads
- invalid dimensions
- incompatible terrain profiles
- mixed reservation terrain families under one `BiomeTag`
- unsupported provider and reservation combinations

## Binding Queries
`UBiomeStrategyBindingLibrary` maps explicit Porism `WorldGenDef` biome rows back to strategy data.

- `QueryBiomeStrategyRowBindings` reports which rows use strategy wrappers, their selected strategy, their selected biome tag, and whether matching domain and `GenA` wrappers are present
- `QueryWorldGenReservationFields` resolves tagged reservation or spawn fields associated with the concrete biome rows that expose their `BiomeTag`

These queries are optional metadata and diagnostics helpers. Runtime terrain generation still flows through Porism's biome rows.

## Source Entry Points
- [BiomeStrategyData.h](../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Strategy/BiomeStrategyData.h)
- [BiomeFastNoiseEditor.h](../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Strategy/BiomeFastNoiseEditor.h)
- [BiomeStrategyBindingLibrary.h](../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Noise/Strategy/BiomeStrategyBindingLibrary.h)
- [BiomeGameplayTags.h](../../Source/PorismDIMsWorldGeneratorExtension/Public/Biome/Types/BiomeGameplayTags.h)
