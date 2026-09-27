// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Spawn/ChunkWorldSpawnTypes.h"

bool FChunkWorldSpawnFacing::ResolveYaw(const FVector& Location, double& OutYaw) const
{
	if (!FMath::IsFinite(Yaw) || Location.ContainsNaN() || (bFaceTarget && Target.ContainsNaN())) return false;
	OutYaw = FRotator::NormalizeAxis(Yaw);
	if (bFaceTarget)
	{
		const FVector Direction = Target - Location;
		if (Direction.ContainsNaN()) return false;
		if (Direction.SizeSquared2D() > UE_SMALL_NUMBER)
		{
			OutYaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		}
	}
	return true;
}

DEFINE_LOG_CATEGORY(LogChunkWorldSpawn);
DEFINE_LOG_CATEGORY(LogChunkWorldSpawnOutOfWorld);

const TCHAR* ChunkWorldSpawn::ToString(const EChunkWorldSpawnTicketState State)
{
	switch (State)
	{
	case EChunkWorldSpawnTicketState::Pending:
		return TEXT("Pending");
	case EChunkWorldSpawnTicketState::Ready:
		return TEXT("Ready");
	case EChunkWorldSpawnTicketState::Failed:
		return TEXT("Failed");
	case EChunkWorldSpawnTicketState::Canceled:
		return TEXT("Canceled");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ChunkWorldSpawn::ToString(const EChunkWorldSpawnFailureCategory Category)
{
	switch (Category)
	{
	case EChunkWorldSpawnFailureCategory::None:
		return TEXT("None");
	case EChunkWorldSpawnFailureCategory::NoEligibleSource:
		return TEXT("NoEligibleSource");
	case EChunkWorldSpawnFailureCategory::NoEligibleSubjectDefinition:
		return TEXT("NoEligibleSubjectDefinition");
	case EChunkWorldSpawnFailureCategory::SubjectInvalid:
		return TEXT("SubjectInvalid");
	case EChunkWorldSpawnFailureCategory::OwnerInvalid:
		return TEXT("OwnerInvalid");
	case EChunkWorldSpawnFailureCategory::WorldInvalid:
		return TEXT("WorldInvalid");
	case EChunkWorldSpawnFailureCategory::UnsafePlacement:
		return TEXT("UnsafePlacement");
	case EChunkWorldSpawnFailureCategory::RequestCanceled:
		return TEXT("RequestCanceled");
	case EChunkWorldSpawnFailureCategory::ImplementationUnavailable:
		return TEXT("ImplementationUnavailable");
	case EChunkWorldSpawnFailureCategory::RequestInvalid:
		return TEXT("RequestInvalid");
	default:
		return TEXT("Unknown");
	}
}
