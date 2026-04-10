# Server-Authoritative Block Damage

## Purpose
Use this flow when a locally controlled client can identify a chunk-world block immediately, but your project wants the server to compute the real damage amount before any authoritative block mutation happens.

This is the recommended integration for melee, interaction, projectile, or spell systems that need:
- local predicted health preview on the initiating client
- server-owned block health mutation and destruction
- project-owned damage calculation on the server

## Rule Of Thumb
- The client may resolve a block hit for local prediction and UI.
- The client should not send `FChunkWorldResolvedBlockHit` as authoritative truth.
- Send `FChunkWorldBlockHitAuthorityPayload` to the server instead.
- On the server, rebuild a fresh `FChunkWorldResolvedBlockHit`, compute damage in your project, and then call `ApplyAuthoritativeDamageRequest(...)`.

## Why A Separate Payload Exists
`FChunkWorldResolvedBlockHit` contains runtime-resolved context such as schema/component references and representation details. That data is useful locally, but authoritative gameplay should not trust the client's resolved context directly.

`FChunkWorldBlockHitAuthorityPayload` carries only the minimal block identity needed for the receiver to re-resolve the block:
- `ChunkWorld`
- `BlockWorldPos`
- `bHasBlock`

Use these helpers from `UChunkWorldBlockDamageBlueprintLibrary`:
- `TryBuildAuthorityPayloadFromResolvedBlockHit(...)`
- `TryResolveBlockHitContextFromAuthorityPayload(...)`

## Recommended Flow

### Client
1. Resolve the current block into `FChunkWorldResolvedBlockHit`.
2. If immediate response is needed, compute predicted damage locally in your project.
3. Call `UPorismPredictedBlockStateComponent::ApplyPredictedDamageRequest(...)` on the locally controlled client.
4. Convert the resolved hit into `FChunkWorldBlockHitAuthorityPayload`.
5. Send that payload to the server through your own RPC, along with any other project-specific context the server needs to recompute damage.

### Server
1. Receive `FChunkWorldBlockHitAuthorityPayload` and project-specific context.
2. Rebuild a fresh `FChunkWorldResolvedBlockHit` with `TryResolveBlockHitContextFromAuthorityPayload(...)`.
3. Compute the authoritative damage amount in your project from authoritative game state.
4. Build `FChunkWorldBlockDamageRequest`.
5. Call `UPorismPredictedBlockStateComponent::ApplyAuthoritativeDamageRequest(...)`.

## Blueprint-Style Example

### Client Side
```text
CurrentTraceResult -> ResolvedBlockHit
ResolvedBlockHit -> TryBuildAuthorityPayloadFromResolvedBlockHit -> AuthorityPayload
ResolvedBlockHit + LocalPredictedDamage -> ApplyPredictedDamageRequest
Send Server RPC(AuthorityPayload, ProjectSpecificContext)
```

### Server Side
```text
Server RPC(AuthorityPayload, ProjectSpecificContext)
AuthorityPayload -> TryResolveBlockHitContextFromAuthorityPayload -> ServerResolvedHit
ServerResolvedHit + AuthoritativeProjectDamage -> ApplyAuthoritativeDamageRequest
```

## C++ Example
```cpp
FChunkWorldBlockHitAuthorityPayload AuthorityPayload;
if (!UChunkWorldBlockDamageBlueprintLibrary::TryBuildAuthorityPayloadFromResolvedBlockHit(ResolvedHit, AuthorityPayload))
{
	return;
}

Server_RequestBlockDamage(AuthorityPayload);
```

```cpp
void AMyCharacter::Server_RequestBlockDamage_Implementation(const FChunkWorldBlockHitAuthorityPayload& AuthorityPayload)
{
	FChunkWorldResolvedBlockHit ServerResolvedHit;
	if (!UChunkWorldBlockDamageBlueprintLibrary::TryResolveBlockHitContextFromAuthorityPayload(AuthorityPayload, ServerResolvedHit))
	{
		return;
	}

	const int32 DamageAmount = ComputeAuthoritativeBlockDamage(ServerResolvedHit);

	FChunkWorldBlockDamageRequest DamageRequest;
	DamageRequest.ResolvedHit = ServerResolvedHit;
	DamageRequest.DamageAmount = DamageAmount;
	DamageRequest.RequestContextTag = TEXT("MyGameplayPath");

	FChunkWorldBlockDamageRequestResult DamageResult;
	PredictedBlockStateComponent->ApplyAuthoritativeDamageRequest(DamageRequest, DamageResult);
}
```

## Project Responsibilities
The extension plugin does not calculate your project-specific damage amount. Your project should still own:
- combat rules
- stat scaling
- equipment modifiers
- spell or tool-specific damage shaping
- server-side validation of whether the block should be damaged at all

The plugin owns:
- block hit re-resolution helpers
- schema-aligned block health mutation
- prediction storage and prediction invalidation
- shared block hit and destroy feedback

## Notes
- `ApplyPredictedDamageRequest(...)` is for locally controlled client prediction only.
- `ApplyAuthoritativeDamageRequest(...)` is for authority only.
- Block destruction is server-owned, even when the client predicts the block should reach zero health.
