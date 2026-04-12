# PorismDIMsWorldGeneratorExtension

## Overview

This plugin's purpose is to extend the existing [PorismDIMsWorldGenerator plugin](https://www.fab.com/listings/664e9fcc-3639-40bf-a3bd-ae9d4b5aa8ba) with useful utilities for implementing gameplay logic. The base plugin does a great job of offering the necessary tools for generating a world, but is missing useful features for actual gameplay and that's where this plugin comes in to help out.

## Docs

Check the [Docs/README.md](./Docs/README.md) direction for more implementation details and [Docs/Usage/](./Docs/Usage/) for helpful information on how to integrate and use this plugin.

## Features

### Block Type Schema Management

The Porism plugin offers the ability to set/get an custom data int32 array on a particular block in the world and manages the saving/replication of that data, but that's a very open-ended and low-level management of arbitrary data.

This plugin offers management for block type specific schemas, which includes a consistent custom data structure and definition structure that is associated with each block type. The plugin offers some base FInstancedStructs for these concepts and allows them to be extended so that every block type has the same structure or every block type has a different structure. Configure your world the way you want via this flexible management system.

More information can be found in [BlockTypeSchemaManagement.md](./Docs/Usage/BlockTypeSchemaManagement.md).

#### Custom Data

Custom data is essentially the runtime data that is stored/replicated/saved with each block. This management solution offers default values and a consistent, type-safe structure that prevents you from needing to handle a raw int32 array. You can write your gameplay logic against this structure and let this manager handle the (de)serialization for you.

#### Definition

The definition is a separate structure that contains constant values that don't get stored with each block. You can look up these definitions based on the block type to get information about the block, like MaxHealth, sounds and whatever you need. The idea behind this is that you can define your definition alongside the custom data and utilize your custom definition struct for gameplay logic instead of having to manage that separately, which requires you to associate the values with the material/mesh indexes that Porism assigns to the blocks in your world definition.

### Mesh/Actor Swapping Management

Porism supports meshes in the world, but sometimes you need actors with more complicated logic. This plugin provides a proximity component for the pawn, which will instruct the swap components on the chunk world to swap the mesh out for an actor that you've defined for that mesh's block type definition. This occurs when a pawn with the proximity component gets close to the mesh and will affect all other connected processes via replication.

More information can be found in [MeshActorSwapping.md](./Docs/Usage/MeshActorSwapping.md).

### Destruction Actor Swapping Management

This is separate from the Mesh/Actor Swapping Management. There is a destruction actor that can be configured in the block type definition. If the block is destroyed then this destruction actor is spawned in its place and will trigger destruction. This also is replicated, but supports a number of avenues. Sometimes destruction should be done locally and sometimes it should be replicated, but the destruction actor is spawned on each connected process via replication regardless.

More information can be found in [DestructionActorSwapping.md](./Docs/Usage/DestructionActorSwapping.md).

### Trace Based Interaction

It's common to be able to look at blocks and interact with them. This plugin provides a trace based interaction component that can be used for interacting with the chunk world or other actors unrelated to the chunk world. With this you can get updates about which block the player is looking at and do various things with it. There is also a health oriented subclass that provides health information related to the block so you can update your HUD with the health of the current block.

More information can be found in [TraceBasedInteraction.md](./Docs/Usage/TraceBasedInteraction.md).

### Health Management

This plugin provides built in health management for blocks via the block type custom data. This is optional based on the plugins you use and the schemas you choose for your block types. This management integrated into the interaction component to provide updates to health and is completely replicated via Porism's built in custom data replication strategy. In addition to knowing the health of a block, there is a PredictedBlockStateComponent that serves as an interface for modifying the health of a block and is designed to support prediction of health and server authoritative updates.

More information can be found in [HealthManagement.md](./Docs/Usage/HealthManagement.md).

### Startup Readiness Utilities

In `ChunkWorldExtended` class has functionality for informing when the chunks around your player or other pawns with a registered `ChunkWorldWalker` have loaded in and are ready. This is great for a loading screen to instruct your game when the world is ready enough for playing with.

In addition to a means for checking world readiness, there is a component that can be placed on actors in the scene that will freeze them until their `ChunkWorldWalker` is registered and the chunks near the actor have loaded in. This is meant to prevent the player from falling through the map before the chunks are loaded in and have collisions.

More information can be found in [StartupFreezeAndWorldReady.md](./Docs/Usage/StartupFreezeAndWorldReady.md). 
