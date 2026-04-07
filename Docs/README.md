# PorismDIMsWorldGeneratorExtension Docs

This plugin-local documentation is the home for the v2 Block type schema and its derived project-specific extensions.

## Documentation Areas
- `Docs/Design/`
  - stable design and reference documentation for the shared schema, chunk-world, damage, and interaction systems
- [Design/BlockTypeSchema.md](./Design/BlockTypeSchema.md)
- [Design/BlockTypeSchemaComponent.md](./Design/BlockTypeSchemaComponent.md)
- [Design/ChunkWorldDamage.md](./Design/ChunkWorldDamage.md)
- [Design/ChunkWorldExtended.md](./Design/ChunkWorldExtended.md)
- [Design/TraceInteractionComponent.md](./Design/TraceInteractionComponent.md)
- `Docs/Usage/`
  - setup guides and recommended component/library combinations for common gameplay routes
- [Usage/ChunkWorldGameplaySetup.md](./Usage/ChunkWorldGameplaySetup.md)
- `Docs/InProgress/`
  - active implementation notes that are not yet stable reference documentation
- [InProgress/ChunkWorldBlockSwap.md](./InProgress/ChunkWorldBlockSwap.md)
- [InProgress/ChunkWorldDamage.md](./InProgress/ChunkWorldDamage.md)
- [InProgress/TraceInteractionComponentV2.md](./InProgress/TraceInteractionComponentV2.md)

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
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Blueprint/`
  - Blueprint libraries centered on chunk-world/block hit resolution

The private source tree mirrors the same feature folders so implementations stay next to the feature area they belong to.
