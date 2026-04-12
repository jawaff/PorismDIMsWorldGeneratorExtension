# Startup Freeze And World Ready

## Purpose
This startup mechanic exists to solve two startup problems that show up together in chunk-world maps:

- prevent player-controlled actors from falling through the world while chunk collision is still being generated under them
- provide a first-class world-ready hook that can be used to close the loading screen once startup-safe chunks exist around the registered walkers

The feature is centered on `AChunkWorldExtended::OnWorldReady`.

## What `OnWorldReady` Means
`OnWorldReady` is a startup-only event exposed by `AChunkWorldExtended`.

During startup, the chunk world observes its currently registered chunk walkers and waits until each walker has reached the finest ready chunk around its tracked position.

Once all currently registered walkers are ready:
- `OnWorldReady` broadcasts
- `IsWorldReady()` returns true
- the temporary startup tracking stops running in `Tick`

This is intentionally a startup handshake, not a permanent world-streaming monitor.

## Intended Usage

### 1. Use `UPorismStartupFreezeComponent` on actors that own chunk walkers
The extension plugin now provides `UPorismStartupFreezeComponent` for actors that should stay pinned in place until their registered chunk walkers are truly startup-ready.

Recommended setup:
- make sure the actor exposes a chunk walker
- use `UPorismStartupFreezeComponent` on that actor
- let the startup freeze component auto-apply during `BeginPlay`

If you are using the plugin starter characters, the walker participation and startup freeze component are already part of the built-in setup.

The component:
- freezes movement and simulated physics during startup
- pins the actor transform in place while startup freeze is active
- binds only to chunk worlds that actually register one of the owner's walkers
- refuses to unfreeze if it detects that a walker registered after that chunk world had already reported startup ready

That last check is important because it prevents the false-positive case where a chunk world becomes ready before this actor was actually participating in the startup handshake.

### 2. Use the same signal for the loading screen
The same startup-ready event is also the right hook for level-loading UI.

Typical loading flow:
- map startup begins
- chunk walkers register with the chunk world
- `UPorismStartupFreezeComponent` keeps actors frozen in place
- loading screen waits for `OnWorldReady`
- `OnWorldReady` fires
- startup freeze releases the frozen actors
- startup/loading FSM closes the loading screen

This keeps the "safe to unfreeze the player" decision and the "safe to dismiss the loading screen" decision aligned to the same chunk-world state transition.

## Registration Requirement
`OnWorldReady` only reflects the walkers that are actually registered with `AChunkWorldExtended`.

If an actor should participate in startup readiness for a chunk world, it must register a chunk walker with that world.

If no walkers are registered, the startup-ready event should not be treated as meaningful gameplay readiness.

## Late Registration Failure Mode
One important startup bug is "my actor's walker registered too late."

That means:
- the chunk world finished its startup-ready handshake
- `OnWorldReady` already fired
- this actor's walker was not part of that handshake yet
- the actor registers afterward and tries to treat the already-ready world as if it had been included all along

This is not considered a valid startup-ready result for that actor.

Why this matters:
- `OnWorldReady` is only about the walkers that were actually registered when startup readiness was computed
- startup tracking stops after that first ready transition
- late registration does not restart the startup handshake

`UPorismStartupFreezeComponent` treats this as a startup ordering error, not as a successful ready state.

When it detects that one of the owner's walkers registered with a chunk world after that world had already reported startup ready:
- it logs an error
- it does not unfreeze the actor
- it keeps the startup hold active so the actor cannot incorrectly assume nearby chunks were prepared for it

If this happens in a project, the fix is to correct startup ordering so the actor's walker is registered before the chunk world reaches its startup-ready transition.

## Scope And Limits
- This mechanic is for startup only.
- It is designed to guard the first playable moment of the map.
- It should not be used as a general-purpose "the world is always currently ready" signal during ongoing gameplay streaming.
- Late runtime walker registration after startup does not restart the startup-ready handshake.

If a project later needs runtime re-freeze or streaming-safe reposition behavior, that should be implemented as a separate feature with its own contract.

## Recommended Project Integration
- Add `UPorismStartupFreezeComponent` to any actor that should remain immobilized until chunk-world startup is safe.
- Bind to `OnWorldReady` from the map startup/loading FSM when the loading screen should wait for chunk-world readiness.
- Use the component's startup freeze delegates when project-specific visuals or input gating need to follow the same startup hold.

## Related Docs
- [ChunkWorldGameplaySetup.md](./ChunkWorldGameplaySetup.md)
- [../Design/ChunkWorldExtended.md](../Design/ChunkWorldExtended.md)
