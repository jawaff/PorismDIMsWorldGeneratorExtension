# Trace Based Interaction

## Purpose
Use the trace interaction components when gameplay should react to the block or actor currently being looked at.

## How To Use It
1. Use one of these interaction paths on the relevant actor:
   - `UPorismTraceInteractionComponent` for generic interaction
   - `UPorismHealthInteractionComponent` when focused health information is also needed
2. Ensure the owner can provide a trace view, either through its actor setup or through the provided interaction view interface path.
3. Bind to the component's events in Blueprint or C++ for focus updates and interaction responses.
4. Use the resolved block or actor payloads to drive HUD, prompts, and interaction input.

If you are using the plugin starter characters, the interaction component is already included:
- `AChunkWorldPlayerCharacter` includes `UPorismTraceInteractionComponent`
- `AChunkWorldDamagePlayerCharacter` includes `UPorismHealthInteractionComponent`

## Choosing Between The Two Components
- use `UPorismTraceInteractionComponent` for generic block or actor targeting
- use `UPorismHealthInteractionComponent` when the focused block also needs health-aware UI or damage-oriented context

## Typical Uses
- showing the currently focused block name
- updating a HUD with block health
- enabling interaction prompts
- forwarding the focused hit into damage or utility helpers

## Related Docs
- [ChunkWorldGameplaySetup.md](./ChunkWorldGameplaySetup.md)
- [../Design/TraceInteractionComponent.md](../Design/TraceInteractionComponent.md)
