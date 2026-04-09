# PorismDIMsWorldGeneratorExtension Docs

This plugin-local documentation is the home for the block schema, chunk-world interaction, damage, feedback, and swap systems provided by the extension plugin.

## Documentation Areas
- `Docs/Design/`
  - stable design and reference documentation for the shared schema, chunk-world, damage, interaction, and feedback systems
- [Design/BlockTypeSchema.md](./Design/BlockTypeSchema.md)
- [Design/BlockTypeSchemaComponent.md](./Design/BlockTypeSchemaComponent.md)
- [Design/ChunkWorldBlockSwap.md](./Design/ChunkWorldBlockSwap.md)
- [Design/ChunkWorldDamage.md](./Design/ChunkWorldDamage.md)
- [Design/ChunkWorldExtended.md](./Design/ChunkWorldExtended.md)
- [Design/TraceInteractionComponent.md](./Design/TraceInteractionComponent.md)
- `Docs/Usage/`
  - setup guides and recommended component/library combinations for common gameplay routes
- [Usage/ChunkWorldGameplaySetup.md](./Usage/ChunkWorldGameplaySetup.md)

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
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Subsystems/`
  - world-scoped registration and coordination services for chunk-world runtime features
- `Source/PorismDIMsWorldGeneratorExtension/Public/ChunkWorld/Blueprint/`
  - Blueprint libraries centered on chunk-world/block hit resolution

The private source tree mirrors the same feature folders so implementations stay next to the feature area they belong to.
