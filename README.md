# PorismDIMsWorldGeneratorExtension

## Overview

This plugin's purpose is to extend the existing [PorismDIMsWorldGenerator plugin](https://www.fab.com/listings/664e9fcc-3639-40bf-a3bd-ae9d4b5aa8ba) with useful utilities for implementing gameplay logic. The base plugin does a great job of offering the necessary tools for generating a world, but is missing useful features for actual gameplay and that's where this plugin comes in to help out.

## Features

### Block Type Schema Management

The Porism plugin offers the ability to set/get an custom data int32 array on a particular block in the world and manages the saving/replication of that data, but that's a very open-ended and low-level management of arbitrary data.

This plugin offers management for block type specific schemas, which includes a consistent custom data structure and definition structure that is associated with each block type. The plugin offers some base FInstancedStructs for these concepts and allows them to be extended so that every block type has the same structure or every block type has a different structure. Configure your world the way you want via this flexible management system.

#### Custom Data

Custom data is essentially the runtime data that is stored/replicated/saved with each block. This management solution offers default values and a consistent, type-safe structure that prevents you from needing to handle a raw int32 array. You can write your gameplay logic against this structure and let this manager handle the (de)serialization for you.

#### Definition

The definition is a separate structure that contains constant values that don't get stored with each block. You can look up these definitions based on the block type to get information about the block, like MaxHealth, sounds and whatever you need. The idea behind this is that you can define your definition alongside the custom data and utilize your custom definition struct for gameplay logic instead of having to manage that separately, which requires you to associate the values with the material/mesh indexes that Porism assigns to the blocks in your world definition.