# PorismDIMsWorldGeneratorExtension Docs

This plugin-local documentation is the home for the block schema, chunk-world interaction, damage, feedback, swap, layout, and biome strategy systems provided by the extension plugin.

## Documentation Areas
- `Docs/Design/`
  - stable design and reference documentation for the shared schema, chunk-world, damage, interaction, and feedback systems
- [Design/BlockTypeSchema.md](./Design/BlockTypeSchema.md)
- [Design/BlockTypeSchemaComponent.md](./Design/BlockTypeSchemaComponent.md)
- [Design/ChunkWorldBlockSwap.md](./Design/ChunkWorldBlockSwap.md)
- [Design/ChunkWorldDamage.md](./Design/ChunkWorldDamage.md)
- [Design/ChunkWorldExtended.md](./Design/ChunkWorldExtended.md)
- [Design/ChunkWorldSpawn.md](./Design/ChunkWorldSpawn.md)
- [Design/BiomeStrategy.md](./Design/BiomeStrategy.md)
- [Design/BiomeStrategy/FoundationProviders.md](./Design/BiomeStrategy/FoundationProviders.md)
- [Design/BiomeStrategy/ReservationProviders.md](./Design/BiomeStrategy/ReservationProviders.md)
- [Design/LayoutModuleSystem.md](./Design/LayoutModuleSystem.md)
- [Design/TraceInteractionComponent.md](./Design/TraceInteractionComponent.md)
- `Docs/Usage/`
  - setup guides and recommended component/library combinations for common gameplay routes
- [Usage/ChunkWorldGameplaySetup.md](./Usage/ChunkWorldGameplaySetup.md)
- [Usage/BiomeStrategyWorldGenSetup.md](./Usage/BiomeStrategyWorldGenSetup.md)
- [Usage/LayoutModuleCookbook.md](./Usage/LayoutModuleCookbook.md)
- [Usage/BlockTypeSchemaManagement.md](./Usage/BlockTypeSchemaManagement.md)
- [Usage/MeshActorSwapping.md](./Usage/MeshActorSwapping.md)
- [Usage/DestructionActorSwapping.md](./Usage/DestructionActorSwapping.md)
- [Usage/TraceBasedInteraction.md](./Usage/TraceBasedInteraction.md)
- [Usage/HealthManagement.md](./Usage/HealthManagement.md)
- [Usage/ServerAuthoritativeBlockDamage.md](./Usage/ServerAuthoritativeBlockDamage.md)
- [Usage/StartupFreezeAndWorldReady.md](./Usage/StartupFreezeAndWorldReady.md)
- [Usage/SpawnAndRuntimeReadiness.md](./Usage/SpawnAndRuntimeReadiness.md)

The plugin docs focus on stable public behavior. Project-level rewrites and migration notes should be tracked in the main repository `Docs/InProgress/` area until the behavior is settled enough to promote back into these plugin-local pages.

## Source Layout
- `Source/PorismDIMsWorldGeneratorExtension/Public/Block/`
  - block schema assets, payload types, and Blueprint schema helpers
- `Source/PorismDIMsWorldGeneratorExtension/Public/Actor/Interaction/`
  - reflected interaction result contracts shared by actor-attached trace components
- `Source/PorismDIMsWorldGeneratorExtension/Public/Actor/Components/`
  - actor-attached reusable interaction components
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Actors/`
  - chunk-world host actors for this feature set
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Components/`
  - chunk-world runtime components
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Spawn/`
  - location-ticket types, bounded source/origin provider interfaces, and reservation-field provider
- `Source/PorismDIMsWorldGeneratorExtension/Public/Biome/`
  - biome strategy assets, FastNoiseEditor wrappers, provider/reservation payloads, gameplay tags, and WorldGenDef binding helpers
- `Source/PorismDIMsWorldGeneratorExtension/Public/Layout/`
  - fixed-cell layout assets, planner/solver helpers, terrain helpers, preview actor, and streamed runtime realization
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Subsystems/`
  - world-scoped registration and coordination services for chunk-world runtime features
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Blueprint/`
  - Blueprint libraries centered on chunk-world/block hit resolution

The private source tree mirrors the same feature folders so implementations stay next to the feature area they belong to.
