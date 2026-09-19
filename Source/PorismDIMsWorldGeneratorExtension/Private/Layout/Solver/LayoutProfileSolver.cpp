// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutPlacementOccupancy.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Misc/ScopeExit.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Solver/LayoutZoneFeatureDemand.h"
#include "Layout/Types/LayoutGameplayTags.h"

#include "Algo/Rotate.h"
#include "HAL/PlatformProcess.h"

using LayoutProfileSolverInternal::FSolveCandidate;
using LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier;
using LayoutProfileSolverInternal::FPreparedSearchBranchWorkItem;
using LayoutProfileSolverInternal::FSolveContext;
using LayoutProfileSolverInternal::FSolveCellFaceInterface;
using LayoutProfileSolverInternal::FSolveCellFaceInterfaceSet;
using LayoutProfileSolverInternal::FSolverTraceEvent;
using LayoutProfileSolverInternal::FTraversalNodeKey;
using LayoutProfileSolverInternal::FWalkableNodeKey;
using LayoutProfileSolverInternal::ESolveCellFaceNeighborKind;
using LayoutProfileSolverInternal::CellHasExternalPlannedNeighborFace;
using LayoutProfileSolverInternal::CellHasExternalReservedNeighborFace;
using LayoutProfileSolverInternal::CellFaceHasAnyPlannedNeighborCarrier;
using LayoutProfileSolverInternal::DoesFaceMaskContainDirection;
using LayoutProfileSolverInternal::FindIncomingBoundaryPointForCellFace;
using LayoutProfileSolverInternal::FindPlacementOrFixedNeighbor;

bool LayoutProfileSolverInternal::DoesFaceMaskContainDirection(
	const uint8 FaceMask,
	const ELayoutFaceDirection Direction)
{
	return (FaceMask & static_cast<uint8>(
		1u << static_cast<uint8>(Direction))) != 0;
}

bool LayoutProfileSolverInternal::CellHasExternalPlannedNeighborFace(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const ELayoutFaceDirection Direction)
{
	if (const uint8* FaceMask =
			Context.ExternalPlannedNeighborFaceMasks.Find(Cell))
	{
		return DoesFaceMaskContainDirection(*FaceMask, Direction);
	}

	return false;
}

bool LayoutProfileSolverInternal::CellFaceHasAnyPlannedNeighborCarrier(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const ELayoutFaceDirection Direction)
{
	if (const FSolveCellFaceInterfaceSet* InterfaceSet =
			Context.CompiledFaceInterfaces.Find(Cell))
	{
		const int32 DirectionIndex = static_cast<int32>(Direction);
		if (DirectionIndex >= 0
			&& DirectionIndex < UE_ARRAY_COUNT(InterfaceSet->Faces))
		{
			const ESolveCellFaceNeighborKind NeighborKind =
				InterfaceSet->Faces[DirectionIndex].NeighborKind;
			return NeighborKind ==
					ESolveCellFaceNeighborKind::InternalPlannedNeighbor
				|| NeighborKind ==
					ESolveCellFaceNeighborKind::ExternalPlannedNeighbor
				|| NeighborKind ==
					ESolveCellFaceNeighborKind::DeferredExternalContract;
		}
	}

	return false;
}

/** Joint sparse projection shared by direct requests and an exact selected host assignment. */
static bool TrySolveSparseStructuralProjection(
	const FLayoutRegionSolveRequest& Request,
	LayoutProfileSolverInternal::FSolveContext& OwningContext,
	LayoutProfileSolverInternal::FSolveContext& OutSolvedContext,
	FLayoutRegionSolveRequest& OutAuthority,
	bool bValidateLiveCompositePlacementGuard);

bool LayoutProfileSolverInternal::CellHasExternalReservedNeighborFace(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const ELayoutFaceDirection Direction)
{
	if (const uint8* FaceMask =
			Context.ExternalReservedNeighborFaceMasks.Find(Cell))
	{
		return DoesFaceMaskContainDirection(*FaceMask, Direction);
	}

	return false;
}

const FSolveContext::FSolvePlacement*
LayoutProfileSolverInternal::FindPlacementOrFixedNeighbor(
	const FSolveContext& Context,
	const FIntVector& Cell)
{
	if (const FSolveContext::FSolvePlacement* Placement =
			Context.Placements.Find(Cell))
	{
		return Placement;
	}

	return Context.FixedNeighborPlacements.Find(Cell);
}

const FLayoutSolveBoundaryPoint*
LayoutProfileSolverInternal::FindIncomingBoundaryPointForCellFace(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const ELayoutFaceDirection Direction)
{
	const FIntVector NeighborCell =
		Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
	const ELayoutFaceDirection BoundaryDirection =
		FLayoutDirectionUtils::GetOpposite(Direction);
	for (const FLayoutSolveBoundaryPoint& BoundaryPoint :
		Context.IncomingBoundaryPoints)
	{
		if (BoundaryPoint.LocalCell == NeighborCell
			&& BoundaryPoint.FaceDirection == BoundaryDirection)
		{
			return &BoundaryPoint;
		}
	}

	return nullptr;
}

namespace
{
	constexpr int32 MaxSolverTraceMessageChars = 1024;
	constexpr int32 MaxSolverFailureDetailChars = 1536;
	constexpr int32 ExpensiveForwardCheckInterval = 8;
	constexpr int32 FullPropagationCellThreshold =
		ExpensiveForwardCheckInterval * ExpensiveForwardCheckInterval;
	constexpr int32 ReachabilityBranchFaceScoreBacktrackThreshold = 32;
#if WITH_AUTOMATION_TESTS
	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> GCancellationCheckpointGateForTesting;
	TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> GCancellationCheckpointCounterForTesting;

	void WaitOnCancellationCheckpointGateForTesting()
	{
		if (GCancellationCheckpointCounterForTesting.IsValid())
		{
			GCancellationCheckpointCounterForTesting->Increment();
		}
		while (GCancellationCheckpointGateForTesting.IsValid()
			&& !static_cast<bool>(*GCancellationCheckpointGateForTesting)
			&& !LayoutSolveCancellation::IsCurrentThreadCancellationRequested())
		{
			FPlatformProcess::Sleep(0.005f);
		}
	}
#else
	void WaitOnCancellationCheckpointGateForTesting()
	{
	}
#endif

	uint8 GetIntentBit(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Entry:
			return 1u << 0;
		case ELayoutCellIntent::Boundary:
			return 1u << 1;
		case ELayoutCellIntent::Connector:
			return 1u << 2;
		case ELayoutCellIntent::VerticalAccess:
			return 1u << 3;
		case ELayoutCellIntent::Core:
			return 1u << 4;
		case ELayoutCellIntent::Interior:
			return 1u << 5;
		default:
			return 0;
		}
	}

	bool DoesIntentMaskContain(
		const uint8 IntentMask,
		const ELayoutCellIntent Intent)
	{
		return (IntentMask & GetIntentBit(Intent)) != 0;
	}

	const ELayoutCellIntent SeedIntentIterationOrder[] = {
		ELayoutCellIntent::Entry,
		ELayoutCellIntent::Boundary,
		ELayoutCellIntent::Connector,
		ELayoutCellIntent::VerticalAccess,
		ELayoutCellIntent::Core,
		ELayoutCellIntent::Interior};

	FLayoutWorldBindingPlacementPolicy BuildStandaloneConveniencePlacementPolicy()
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy;
		// Public convenience solves intentionally bypass world-facing terrain policy.
		// Keep the request carrier explicitly neutral so these callers can share the
		// standalone runtime-view builder without inheriting ordinary-root defaults.
		PlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 0;
		PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 0;
		return PlacementPolicy;
	}

	FLayoutRootSolveBudgetSettings BuildStandaloneConvenienceSolveBudget(
		const FLayoutSolverExecutionSettings& ExecutionSettings)
	{
		FLayoutRootSolveBudgetSettings SolveBudget;
		SolveBudget.MaxSolveDurationSeconds = ExecutionSettings.MaxSolveDurationSeconds;
		return SolveBudget;
	}

	bool TryBuildStandaloneConvenienceRequest(
		const ULayoutProfileAsset* const Profile,
		const ULayoutRegionContentSetAsset* const ExplicitContentSet,
		const int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		FLayoutRegionSolveRequest& OutRequest,
		FString& OutFailureReason)
	{
		const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
			BuildStandaloneConveniencePlacementPolicy();
		FLayoutWorldBindingRuntimeView RuntimeView;
		RuntimeView.PlacementKind = ELayoutWorldBindingPlacementKind::None;
		RuntimeView.LayoutProfile = const_cast<ULayoutProfileAsset*>(Profile);
		RuntimeView.ContentSet = const_cast<ULayoutRegionContentSetAsset*>(ExplicitContentSet);
		if (RuntimeView.ContentSet == nullptr
			&& Profile != nullptr)
		{
			RuntimeView.ContentSet =
				LayoutWorldBindingRuntimeHelpers::ResolveRuntimePreferredContentSet(Profile);
		}
		RuntimeView.SharedCellSizeInBlocks =
			LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(
				RuntimeView.ContentSet);
		RuntimeView.PlacementPolicy = PlacementPolicy;
		RuntimeView.SolveBudget =
			BuildStandaloneConvenienceSolveBudget(ExecutionSettings);
		const FLayoutId RootRegionId =
			!RegionDebugPath.IsEmpty() ? FLayoutId(*RegionDebugPath) : NAME_None;
		return LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			Seed,
			RegionDebugPath,
			NAME_None,
			RootRegionId,
			RootRegionId,
			OutRequest,
			OutFailureReason);
	}

	FLayoutSolveResult BuildFailedStandaloneConvenienceSolveResult(
		const FString& FailureReason)
	{
		FLayoutSolveResult Result;
		Result.bSucceeded = false;
		Result.FailureReason = FailureReason;
		return Result;
	}

	const FLayoutValidationAssertionRecord* FindFailedBlockingStandaloneRequestContractAssertion(
		const FLayoutRegionSolveRequest& Request)
	{
		return Request.ValidationAssertions.FindByPredicate(
			[](const FLayoutValidationAssertionRecord& Assertion)
			{
				return (Assertion.AssertionId == TEXT("RegionRequest.WorldPlacementLatticeContractValid")
					|| Assertion.AssertionId == TEXT("RegionRequest.ProfileContentSetBindingValid"))
					&& !Assertion.bPassed;
			});
	}

	struct FPropagationArc
	{
		FIntVector Cell = FIntVector::ZeroValue;
		FIntVector NeighborCell = FIntVector::ZeroValue;
		ELayoutFaceDirection Direction = ELayoutFaceDirection::PosX;
	};

	struct FScopedPropagationStatsTimer
	{
		explicit FScopedPropagationStatsTimer(FLayoutSolverPropagationStats& InStats)
			: Stats(InStats)
			, StartTimeSeconds(FPlatformTime::Seconds())
		{
			++Stats.PropagationRunCount;
		}

		~FScopedPropagationStatsTimer()
		{
			Stats.PropagationSeconds += FPlatformTime::Seconds() - StartTimeSeconds;
		}

		FLayoutSolverPropagationStats& Stats;
		double StartTimeSeconds = 0.0;
	};

	FString TruncateSolverMessage(const FString& Message, const int32 MaxChars)
	{
		if (MaxChars <= 0 || Message.Len() <= MaxChars)
		{
			return Message;
		}

		return Message.Left(MaxChars) + TEXT("... [truncated]");
	}

	bool IsTracingEnabled(const FSolveContext& Context)
	{
		return Context.TraceMode != ELayoutSolverTraceMode::Disabled && Context.MaxTraceEvents > 0;
	}

	FIntVector ResolveRequestSharedCellSizeInBlocks(const FLayoutRegionSolveRequest& Request)
	{
		const FIntVector ContentSetSharedCellSize = Request.ContentSetSnapshot.SharedCellSizeInBlocks;
		if (ContentSetSharedCellSize != FIntVector::ZeroValue)
		{
			return ContentSetSharedCellSize;
		}

		return Request.ModuleCatalog.SharedCellSizeInBlocks;
	}

	TArray<FIntVector> ResolvePlacementOccupiedLocalCellsForTerrainAlignment(const FLayoutPlacedModule& Placement)
	{
		if (!Placement.OccupiedLocalCells.IsEmpty())
		{
			return Placement.OccupiedLocalCells;
		}

		if (Placement.CompositeModule != nullptr)
		{
			return Placement.CompositeModule->GetOccupiedLocalCells();
		}

		if (Placement.Module != nullptr)
		{
			return Placement.Module->GetOccupiedLocalCells();
		}

		return {FIntVector::ZeroValue};
	}

	int32 ResolveLowestRealizedTerrainAlignmentLevel(const FLayoutSolveResult& SolveResult)
	{
		int32 LowestResolvedLevel = TNumericLimits<int32>::Max();
		bool bFoundAnyOccupiedLevel = false;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			const TArray<FIntVector> OccupiedLocalCells =
				ResolvePlacementOccupiedLocalCellsForTerrainAlignment(Placement);
			for (const FIntVector& OccupiedLocalCell : OccupiedLocalCells)
			{
				LowestResolvedLevel = FMath::Min(
					LowestResolvedLevel,
					Placement.Cell.Z + OccupiedLocalCell.Z);
				bFoundAnyOccupiedLevel = true;
			}
		}

		return bFoundAnyOccupiedLevel ? LowestResolvedLevel : INDEX_NONE;
	}

	bool IsContinuationRootPlacementKind(const ELayoutWorldBindingPlacementKind PlacementKind)
	{
		return PlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
			|| PlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation
			|| PlacementKind == ELayoutWorldBindingPlacementKind::TunnelContinuation;
	}

	void PopulateResolvedTerrainAlignmentLevel(
		const FLayoutRegionSolveRequest& Request,
		FLayoutSolveResult& SolveResult)
	{
		SolveResult.ResolvedTerrainAlignmentLevel = INDEX_NONE;
		if (SolveResult.RootPlacementKind == ELayoutWorldBindingPlacementKind::None)
		{
			return;
		}

		if (IsContinuationRootPlacementKind(SolveResult.RootPlacementKind)
			&& Request.RootContinuationSelection.ResolvedEntryLevel != INDEX_NONE)
		{
			SolveResult.ResolvedTerrainAlignmentLevel =
				Request.RootContinuationSelection.ResolvedEntryLevel;
			return;
		}

		SolveResult.ResolvedTerrainAlignmentLevel =
			ResolveLowestRealizedTerrainAlignmentLevel(SolveResult);
	}

	const FLayoutProfileSolveSnapshot& GetEffectiveProfileSnapshot(const FSolveContext& Context)
	{
		return Context.ProfileSnapshot;
	}

	void AddTraceEvent(FSolveContext& Context, const FString& Message)
	{
		if (!IsTracingEnabled(Context))
		{
			return;
		}

		if (Context.TraceEvents.Num() >= Context.MaxTraceEvents)
		{
			++Context.TraceEventsDropped;
			Context.TraceEvents[Context.TraceNextWriteIndex] = FSolverTraceEvent{TruncateSolverMessage(Message, MaxSolverTraceMessageChars)};
			Context.TraceNextWriteIndex = (Context.TraceNextWriteIndex + 1) % Context.MaxTraceEvents;
			Context.bTraceWrapped = true;
			return;
		}

		Context.TraceEvents.Add(FSolverTraceEvent{TruncateSolverMessage(Message, MaxSolverTraceMessageChars)});
		Context.TraceNextWriteIndex = Context.TraceEvents.Num() % Context.MaxTraceEvents;
	}

	void AddSolveWarning(FSolveContext& Context, const FString& MessageText)
	{
		FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
		Message.Severity = ELayoutValidationSeverity::Warning;
		Message.Message = MessageText;
	}

	void AddSolveError(FSolveContext& Context, const FString& MessageText)
	{
		FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
		Message.Severity = ELayoutValidationSeverity::Error;
		Message.Message = MessageText;
	}

	bool ValidateLiveLeafSolverModuleSnapshots(FSolveContext& Context)
	{
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : Context.ModuleSnapshots)
		{
			if (ModuleSnapshot.SourceCompositeModule == nullptr)
			{
				continue;
			}

			if (!ModuleSnapshot.OccupiedLocalCells.Contains(FIntVector::ZeroValue)
				|| ModuleSnapshot.RootSupportedCellIntents.IsEmpty())
			{
				const FString FailureText = FString::Printf(
					TEXT("Live profile-solver placement requires composite module snapshot '%s' to preserve an occupied local root cell at (0,0,0) plus non-empty RootSupportedCellIntents before bundle-aware placement can proceed."),
					*ModuleSnapshot.DebugName.ToString());
				AddSolveError(Context, FailureText);
				Context.Result.FailureReason = FailureText;
				return false;
			}
		}

		return true;
	}

	const FLayoutCommittedEndpointAnchor* FindCommittedEndpointAnchorForCellFace(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection Direction);

	const FLayoutCommittedEndpointAnchor* ResolveCompiledEndpointAnchor(
		const FSolveContext& Context,
		const FSolveCellFaceInterface& FaceInterface)
	{
		return Context.CommittedEndpointAnchors.IsValidIndex(
				FaceInterface.CommittedEndpointAnchorIndex)
			? &Context.CommittedEndpointAnchors[
				FaceInterface.CommittedEndpointAnchorIndex]
			: nullptr;
	}

	bool HasAnyExternalContinuationCarrier(const FSolveContext& Context)
	{
		for (const TPair<FIntVector, FSolveCellFaceInterfaceSet>& InterfacePair :
			Context.CompiledFaceInterfaces)
		{
			for (const FSolveCellFaceInterface& FaceInterface :
				InterfacePair.Value.Faces)
			{
				if (FaceInterface.NeighborKind ==
						ESolveCellFaceNeighborKind::ExternalPlannedNeighbor
					|| FaceInterface.NeighborKind ==
						ESolveCellFaceNeighborKind::ReservedExternalCell)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool DoesContextCarryCompiledPlannedCell(
		const FSolveContext& Context,
		const FIntVector& Cell)
	{
		return Context.CompiledFaceInterfaces.Contains(Cell);
	}

	bool DoesContextAdmitProjectedOccupiedWorldCell(
		const FSolveContext& Context,
		const FIntVector& Cell)
	{
		if (DoesContextCarryCompiledPlannedCell(Context, Cell))
		{
			return true;
		}

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction =
				static_cast<ELayoutFaceDirection>(DirectionIndex);
			const ELayoutFaceDirection OppositeDirection =
				FLayoutDirectionUtils::GetOpposite(Direction);
			const FIntVector AdjacentPlannedCell =
				Cell + FLayoutDirectionUtils::ToCellDelta(OppositeDirection);
			const FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(
					Context,
					AdjacentPlannedCell,
					Direction);
			if (FaceInterface != nullptr
				&& (FaceInterface->NeighborKind
						== ESolveCellFaceNeighborKind::ExternalPlannedNeighbor
					|| FaceInterface->NeighborKind
						== ESolveCellFaceNeighborKind::ReservedExternalCell))
			{
				return true;
			}
		}

		return false;
	}

	/** Returns true when a compiled incoming boundary is only providing vertical filled support. */
	bool IsVerticalSupportBoundaryPoint(const FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		return BoundaryPoint.bRepresentsFilledNeighbor
			&& BoundaryPoint.CommitmentId == NAME_None
			&& !BoundaryPoint.bRequiresBoundaryFacing
			&& (BoundaryPoint.FaceDirection == ELayoutFaceDirection::PosZ
				|| BoundaryPoint.FaceDirection == ELayoutFaceDirection::NegZ);
	}

	struct FBoundaryPointFaceKey
	{
		FIntVector LocalCell = FIntVector::ZeroValue;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

		friend bool operator==(
			const FBoundaryPointFaceKey& Left,
			const FBoundaryPointFaceKey& Right)
		{
			return Left.LocalCell == Right.LocalCell
				&& Left.FaceDirection == Right.FaceDirection;
		}

		friend uint32 GetTypeHash(const FBoundaryPointFaceKey& Key)
		{
			return HashCombineFast(
				GetTypeHash(Key.LocalCell),
				static_cast<uint32>(Key.FaceDirection));
		}
	};

	int32 GetBoundaryPointSpecificityScore(const FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		int32 Score = BoundaryPoint.CommitmentId == NAME_None ? 0 : 1000;
		Score += BoundaryPoint.bUsesCertifiedReciprocalDomain ? 2000 : 0;
		Score += BoundaryPoint.bRepresentsFilledNeighbor ? 100 : 0;
		Score += BoundaryPoint.bRequiresBoundaryFacing ? 50 : 0;
		Score += BoundaryPoint.bRequireMatchingYawWithFilledNeighbor ? 50 : 0;
		Score += BoundaryPoint.ConnectionTag.IsValid() ? 25 : 0;
		Score += BoundaryPoint.AllowedConnectionTags.IsEmpty() ? 0 : 25;
		Score += BoundaryPoint.ConnectedTraversalChannels.IsEmpty() ? 0 : 25;
		Score += BoundaryPoint.SourceYawRotationSteps != 0 ? 10 : 0;
		Score += BoundaryPoint.SourceRegionDebugPath.IsEmpty() ? 0 : 1;
		return Score;
	}

	void MergeBoundaryPointIntoCanonical(
		FLayoutSolveBoundaryPoint& InOutCanonicalPoint,
		const FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		const int32 CanonicalScore =
			GetBoundaryPointSpecificityScore(InOutCanonicalPoint);
		const int32 BoundaryScore =
			GetBoundaryPointSpecificityScore(BoundaryPoint);
		if (BoundaryScore > CanonicalScore)
		{
			FLayoutSolveBoundaryPoint PreviousCanonicalPoint =
				InOutCanonicalPoint;
			InOutCanonicalPoint = BoundaryPoint;
			MergeBoundaryPointIntoCanonical(
				InOutCanonicalPoint,
				PreviousCanonicalPoint);
			return;
		}

		InOutCanonicalPoint.AllowedConnectionTags.AppendTags(
			BoundaryPoint.AllowedConnectionTags);
		InOutCanonicalPoint.ConnectedTraversalChannels.AppendTags(
			BoundaryPoint.ConnectedTraversalChannels);
		InOutCanonicalPoint.bRepresentsFilledNeighbor =
			InOutCanonicalPoint.bRepresentsFilledNeighbor
			|| BoundaryPoint.bRepresentsFilledNeighbor;
		InOutCanonicalPoint.bRequiresBoundaryFacing =
			InOutCanonicalPoint.bRequiresBoundaryFacing
			|| BoundaryPoint.bRequiresBoundaryFacing;
		InOutCanonicalPoint.bRequireMatchingYawWithFilledNeighbor =
			InOutCanonicalPoint.bRequireMatchingYawWithFilledNeighbor
			|| BoundaryPoint.bRequireMatchingYawWithFilledNeighbor;
		if (!InOutCanonicalPoint.ConnectionTag.IsValid()
			&& BoundaryPoint.ConnectionTag.IsValid())
		{
			InOutCanonicalPoint.ConnectionTag = BoundaryPoint.ConnectionTag;
		}
		if (InOutCanonicalPoint.CommitmentId == NAME_None
			&& BoundaryPoint.CommitmentId != NAME_None)
		{
			InOutCanonicalPoint.CommitmentId = BoundaryPoint.CommitmentId;
		}
		if (!InOutCanonicalPoint.bUsesCertifiedReciprocalDomain
			&& BoundaryPoint.bUsesCertifiedReciprocalDomain)
		{
			InOutCanonicalPoint.bUsesCertifiedReciprocalDomain = true;
			InOutCanonicalPoint.CertifiedDomainCertificateId =
				BoundaryPoint.CertifiedDomainCertificateId;
			InOutCanonicalPoint.CertifiedDomainRestrictionId =
				BoundaryPoint.CertifiedDomainRestrictionId;
		}
		if (InOutCanonicalPoint.SourceRegionDebugPath.IsEmpty()
			&& !BoundaryPoint.SourceRegionDebugPath.IsEmpty())
		{
			InOutCanonicalPoint.SourceRegionDebugPath =
				BoundaryPoint.SourceRegionDebugPath;
		}
		if (InOutCanonicalPoint.SourceCell == FIntVector::ZeroValue
			&& BoundaryPoint.SourceCell != FIntVector::ZeroValue)
		{
			InOutCanonicalPoint.SourceCell = BoundaryPoint.SourceCell;
		}
		if ((InOutCanonicalPoint.SourceYawRotationSteps == 0
				&& BoundaryPoint.SourceYawRotationSteps != 0)
			|| (!InOutCanonicalPoint.bRequireMatchingYawWithFilledNeighbor
				&& BoundaryPoint.bRequireMatchingYawWithFilledNeighbor))
		{
			InOutCanonicalPoint.SourceYawRotationSteps =
				BoundaryPoint.SourceYawRotationSteps;
		}
	}

	void CanonicalizeIncomingBoundaryPointsByFace(
		TArray<FLayoutSolveBoundaryPoint>& InOutBoundaryPoints)
	{
		if (InOutBoundaryPoints.Num() <= 1)
		{
			return;
		}

		TMap<FBoundaryPointFaceKey, int32> CanonicalIndexByFace;
		TArray<FLayoutSolveBoundaryPoint> CanonicalBoundaryPoints;
		CanonicalBoundaryPoints.Reserve(InOutBoundaryPoints.Num());
		for (const FLayoutSolveBoundaryPoint& BoundaryPoint :
			InOutBoundaryPoints)
		{
			const FBoundaryPointFaceKey FaceKey
			{
				.LocalCell = BoundaryPoint.LocalCell,
				.FaceDirection = BoundaryPoint.FaceDirection
			};
			if (const int32* ExistingIndex =
					CanonicalIndexByFace.Find(FaceKey))
			{
				MergeBoundaryPointIntoCanonical(
					CanonicalBoundaryPoints[*ExistingIndex],
					BoundaryPoint);
				continue;
			}

			CanonicalIndexByFace.Add(FaceKey, CanonicalBoundaryPoints.Num());
			CanonicalBoundaryPoints.Add(BoundaryPoint);
		}

		InOutBoundaryPoints = MoveTemp(CanonicalBoundaryPoints);
	}

	/** Installs validated occupancy-only terrain proof before candidate face compilation. */
	void InstallTerrainBackedNeighborFaces(
		FSolveContext& Context,
		const FLayoutFrozenTerrainContract& TerrainContract)
	{
		Context.TerrainBackedNeighborFaces = TerrainContract.TerrainBackedNeighborFaces;
		Context.TerrainBackedFilledFaceMasksByCell.Reset();
		for (const FLayoutTerrainBackedNeighborFaceRecord& FaceRecord : Context.TerrainBackedNeighborFaces)
		{
			const bool bHorizontalFace = FaceRecord.FaceDirection == ELayoutFaceDirection::PosX
				|| FaceRecord.FaceDirection == ELayoutFaceDirection::NegX
				|| FaceRecord.FaceDirection == ELayoutFaceDirection::PosY
				|| FaceRecord.FaceDirection == ELayoutFaceDirection::NegY;
			if (bHorizontalFace
				&& FaceRecord.NeighborCell == FaceRecord.Cell + FLayoutDirectionUtils::ToCellDelta(FaceRecord.FaceDirection)
				&& !FaceRecord.SourceEvidenceId.IsNone())
			{
				Context.TerrainBackedFilledFaceMasksByCell.FindOrAdd(FaceRecord.Cell) |=
					LayoutFaceDirectionMask(FaceRecord.FaceDirection);
			}
		}
	}

	bool ShouldAuditAdjacencyBetweenCells(const FSolveContext& Context, const FIntVector& Cell, const FIntVector& NeighborCell);

	/**
	 * Compiles one authoritative per-face interface view from the current
	 * planned-cell set plus fixed-neighbor and incoming-boundary carriers so
	 * domain admission and live legality stop rediscovering the same seam.
	 */
	void RefreshCompiledFaceInterfaces(FSolveContext& Context)
	{
		Context.CompiledFaceInterfaces.Reset();
		Context.CompiledSeedIntentMasks.Reset();
		TMap<FIntVector, ELayoutEntryOrigin> EntryOriginsByCell;
		EntryOriginsByCell.Reserve(Context.Result.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (PlannedCell.EntryOrigin != ELayoutEntryOrigin::None)
			{
				EntryOriginsByCell.Add(PlannedCell.Cell, PlannedCell.EntryOrigin);
			}
		}
		for (const TPair<FIntVector, ELayoutCellIntent>& PlannedCellPair :
			Context.PlannedCellIntents)
		{
			const FIntVector Cell = PlannedCellPair.Key;
			const ELayoutEntryOrigin EntryOrigin = EntryOriginsByCell.FindRef(Cell);
			const bool bInternalContractEntry = PlannedCellPair.Value == ELayoutCellIntent::Entry
				&& (EntryOrigin == ELayoutEntryOrigin::TerrainSeam
					|| EntryOrigin == ELayoutEntryOrigin::ChildContract
					|| EntryOrigin == ELayoutEntryOrigin::Continuation);
			uint8 SeedIntentMask =
				GetIntentBit(PlannedCellPair.Value);
			const ELayoutCellIntent* LowerIntent =
				Context.PlannedCellIntents.Find(Cell - FIntVector(0, 0, 1));
			if (LowerIntent != nullptr
				&& *LowerIntent == ELayoutCellIntent::VerticalAccess)
			{
				// Upper landing cells retain their topology intent while also admitting
				// ordinary floor candidates whose vertical face contract matches below.
				SeedIntentMask |= GetIntentBit(ELayoutCellIntent::Interior);
			}
			FSolveCellFaceInterfaceSet& InterfaceSet =
				Context.CompiledFaceInterfaces.FindOrAdd(Cell);
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction =
					static_cast<ELayoutFaceDirection>(DirectionIndex);
				FSolveCellFaceInterface& FaceInterface =
					InterfaceSet.Faces[DirectionIndex];
				FaceInterface = FSolveCellFaceInterface{};
				FaceInterface.Direction = Direction;

				if (const FLayoutCommittedEndpointAnchor* EndpointAnchor =
						FindCommittedEndpointAnchorForCellFace(
							Context,
							Cell,
							Direction))
				{
					SeedIntentMask |=
						GetIntentBit(ELayoutCellIntent::Entry);
					FaceInterface.CommittedEndpointAnchorIndex =
						static_cast<int32>(
							EndpointAnchor
							- Context.CommittedEndpointAnchors.GetData());
				}

				const FIntVector NeighborCell =
					Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (const FSolveContext::FSolvePlacement* NeighborPlacement =
						FindPlacementOrFixedNeighbor(Context, NeighborCell))
				{
					FaceInterface.NeighborKind =
						FSolveContext::IsOccupiedPlacement(*NeighborPlacement)
							? ESolveCellFaceNeighborKind::FixedFilledNeighbor
							: Context.TerrainResidualRuleIdByCell.Contains(NeighborCell)
								? ESolveCellFaceNeighborKind::TerrainResidualNeighbor
								: ESolveCellFaceNeighborKind::FixedEmptyNeighbor;
					continue;
				}

				if (Context.ChildReservationCells.Contains(NeighborCell))
				{
					FaceInterface.NeighborKind =
						ESolveCellFaceNeighborKind::ChildRegionContact;
					continue;
				}

				if (const FLayoutSolveBoundaryPoint* BoundaryPoint =
						FindIncomingBoundaryPointForCellFace(
							Context,
							Cell,
							Direction))
				{
					FaceInterface.NeighborKind =
						IsVerticalSupportBoundaryPoint(*BoundaryPoint)
							? ESolveCellFaceNeighborKind::SupportBoundary
							: ESolveCellFaceNeighborKind::IncomingBoundary;
					const bool bVerticalFace =
						Direction == ELayoutFaceDirection::PosZ
						|| Direction == ELayoutFaceDirection::NegZ;
					if (bVerticalFace)
					{
						SeedIntentMask |= GetIntentBit(
							ELayoutCellIntent::VerticalAccess);
					}
					else if (BoundaryPoint->bRequiresBoundaryFacing)
					{
						SeedIntentMask |= GetIntentBit(
							ELayoutCellIntent::Boundary);
					}
					FaceInterface.IncomingBoundaryPointIndex =
						static_cast<int32>(
							BoundaryPoint
							- Context.IncomingBoundaryPoints.GetData());
					continue;
				}

				if (Context.PlannedCellIntents.Contains(NeighborCell))
				{
					const bool bVerticalFace = Direction == ELayoutFaceDirection::PosZ
						|| Direction == ELayoutFaceDirection::NegZ;
					FaceInterface.NeighborKind = Context.bTreatTerrainResidualsAsSettledEmpty
						&& Context.TerrainResidualRuleIdByCell.Contains(NeighborCell)
						? ESolveCellFaceNeighborKind::TerrainResidualNeighbor
						: bVerticalFace && !ShouldAuditAdjacencyBetweenCells(Context, Cell, NeighborCell)
							? ESolveCellFaceNeighborKind::StructuralVerticalOverlap
							: ESolveCellFaceNeighborKind::InternalPlannedNeighbor;
					continue;
				}

				if (Context.OwningTopologyCellsByPhysicalCell.Contains(NeighborCell))
				{
					FaceInterface.NeighborKind = Context.TerrainResidualRuleIdByCell.Contains(NeighborCell)
						? ESolveCellFaceNeighborKind::TerrainResidualNeighbor
						: ESolveCellFaceNeighborKind::ExternalPlannedNeighbor;
					continue;
				}

				if (CellHasExternalPlannedNeighborFace(
						Context,
						Cell,
						Direction))
				{
					FaceInterface.NeighborKind =
						ESolveCellFaceNeighborKind::ExternalPlannedNeighbor;
					continue;
				}

				if (Context.bDeferUnresolvedExternalFaceValidation)
				{
					FaceInterface.NeighborKind =
						ESolveCellFaceNeighborKind::DeferredExternalContract;
					continue;
				}

				if (LayoutFaceMaskContainsDirection(
						Context.TerrainBackedFilledFaceMasksByCell.FindRef(Cell),
						Direction))
				{
					FaceInterface.NeighborKind =
						ESolveCellFaceNeighborKind::TerrainBackedFilledNeighbor;
					continue;
				}

				FaceInterface.NeighborKind =
					CellHasExternalReservedNeighborFace(
						Context,
						Cell,
						Direction)
						? ESolveCellFaceNeighborKind::ReservedExternalCell
						: ESolveCellFaceNeighborKind::OuterBoundary;
				const bool bLateralFace =
					Direction == ELayoutFaceDirection::PosX
					|| Direction == ELayoutFaceDirection::NegX
					|| Direction == ELayoutFaceDirection::PosY
					|| Direction == ELayoutFaceDirection::NegY;
				if (bLateralFace
					&& FaceInterface.NeighborKind
						== ESolveCellFaceNeighborKind::OuterBoundary
					&& (PlannedCellPair.Value != ELayoutCellIntent::Entry
						|| bInternalContractEntry))
				{
					SeedIntentMask |= GetIntentBit(
						ELayoutCellIntent::Boundary);
				}
			}

			Context.CompiledSeedIntentMasks.Add(Cell, SeedIntentMask);
		}
	}

	int32 GetIntentPriority(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Entry:
			return 0;
		case ELayoutCellIntent::Boundary:
			return 1;
		case ELayoutCellIntent::Connector:
			return 2;
		case ELayoutCellIntent::VerticalAccess:
			return 3;
		case ELayoutCellIntent::Core:
			return 4;
		case ELayoutCellIntent::Interior:
			return 5;
		default:
			return 6;
		}
	}

	bool IsBoundaryCell(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		return Cell.X == 0
			|| Cell.Y == 0
			|| Cell.X == FootprintSize.X - 1
			|| Cell.Y == FootprintSize.Y - 1;
	}

	bool IsCornerBoundaryCell(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		if (FootprintSize.X <= 1 || FootprintSize.Y <= 1)
		{
			return false;
		}

		const bool bOnXEdge = Cell.X == 0 || Cell.X == FootprintSize.X - 1;
		const bool bOnYEdge = Cell.Y == 0 || Cell.Y == FootprintSize.Y - 1;
		return bOnXEdge && bOnYEdge;
	}

	FIntVector RotateCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		const int32 NormalizedYawSteps = ((YawRotationSteps % 4) + 4) % 4;
		switch (NormalizedYawSteps)
		{
		case 1:
			return FIntVector(FootprintSize.Y - 1 - Cell.Y, Cell.X, Cell.Z);
		case 2:
			return FIntVector(FootprintSize.X - 1 - Cell.X, FootprintSize.Y - 1 - Cell.Y, Cell.Z);
		case 3:
			return FIntVector(Cell.Y, FootprintSize.X - 1 - Cell.X, Cell.Z);
		case 0:
		default:
			return Cell;
		}
	}

	bool IsHorizontalDirection(const ELayoutFaceDirection Direction)
	{
		return Direction == ELayoutFaceDirection::PosX
			|| Direction == ELayoutFaceDirection::NegX
			|| Direction == ELayoutFaceDirection::PosY
			|| Direction == ELayoutFaceDirection::NegY;
	}

	bool DoesCellMatchAuthoredLevelScope(
		const FIntVector& Cell,
		const int32 LevelCount,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel)
	{
		switch (LevelPlacementPolicy)
		{
		case ELayoutLevelPlacementPolicy::AnyLevel:
			return true;
		case ELayoutLevelPlacementPolicy::GroundOnly:
			return Cell.Z == 0;
		case ELayoutLevelPlacementPolicy::SpecificLevel:
			return Cell.Z == SpecificLevel;
		case ELayoutLevelPlacementPolicy::TopLevelOnly:
			return Cell.Z == FMath::Max(0, LevelCount - 1);
		case ELayoutLevelPlacementPolicy::AboveGroundLevel:
			return Cell.Z > 0;
		case ELayoutLevelPlacementPolicy::BelowTopLevel:
			return Cell.Z < FMath::Max(0, LevelCount - 1);
		default:
			return true;
		}
	}

	ELayoutLevelFillMode GetLevelFillModeForCell(const FLayoutProfileSolveSnapshot& Profile, const FIntVector& Cell)
	{
		for (const FLayoutLevelFillRule& Rule : Profile.LevelFillRules)
		{
			if (DoesCellMatchAuthoredLevelScope(Cell, Profile.LevelCount, Rule.LevelPlacementPolicy, Rule.SpecificLevel))
			{
				return Rule.FillMode;
			}
		}

		return ELayoutLevelFillMode::Default;
	}

	bool DoesTransitionPreferBoundaryVerticalAccess(const FLayoutProfileSolveSnapshot& Profile, const int32 TransitionLevel)
	{
		return Profile.bRequireAllTraversalChannelsReachable
			&& GetLevelFillModeForCell(Profile, FIntVector(0, 0, TransitionLevel + 1)) == ELayoutLevelFillMode::BoundaryOnly;
	}

	uint8 BuildDirectionMask(const ELayoutFaceDirection Direction)
	{
		return static_cast<uint8>(1u << static_cast<uint8>(Direction));
	}

	bool TryGetHorizontalDirectionBetweenCells(
		const FIntVector& FromCell,
		const FIntVector& ToCell,
		ELayoutFaceDirection& OutDirection)
	{
		const FIntVector Delta = ToCell - FromCell;
		if (Delta == FIntVector(1, 0, 0))
		{
			OutDirection = ELayoutFaceDirection::PosX;
			return true;
		}

		if (Delta == FIntVector(-1, 0, 0))
		{
			OutDirection = ELayoutFaceDirection::NegX;
			return true;
		}

		if (Delta == FIntVector(0, 1, 0))
		{
			OutDirection = ELayoutFaceDirection::PosY;
			return true;
		}

		if (Delta == FIntVector(0, -1, 0))
		{
			OutDirection = ELayoutFaceDirection::NegY;
			return true;
		}

		return false;
	}

	FLayoutProofRecord MakeSnapshotProofRecord(
		const FLayoutId ProofId,
		const ELayoutProofKind ProofKind,
		const FLayoutId TargetId,
		const TArray<FLayoutId>& SourceIds,
		const FString& ProofSummary)
	{
		FLayoutProofRecord Proof;
		Proof.ProofId = ProofId;
		Proof.ProofKind = ProofKind;
		Proof.TargetId = TargetId;
		Proof.SourceIds = SourceIds;
		Proof.ProofSummary = ProofSummary;
		return Proof;
	}

	FLayoutValidationAssertionRecord MakeSnapshotAssertionRecord(
		const FLayoutId AssertionId,
		const ELayoutValidationAssertionKind AssertionKind,
		const bool bPassed,
		const TArray<FLayoutId>& RelatedIds,
		const FString& FailureReason = FString())
	{
		FLayoutValidationAssertionRecord Assertion;
		Assertion.AssertionId = AssertionId;
		Assertion.AssertionKind = AssertionKind;
		Assertion.bPassed = bPassed;
		Assertion.RelatedIds = RelatedIds;
		Assertion.FailureReason = FailureReason;
		return Assertion;
	}

	void AppendFailedAssertionsToValidation(
		const TArray<FLayoutValidationAssertionRecord>& Assertions,
		FLayoutValidationResult& Validation)
	{
		for (const FLayoutValidationAssertionRecord& Assertion : Assertions)
		{
			if (!Assertion.bPassed)
			{
				Validation.AddError(
					Assertion.FailureReason.IsEmpty()
						? FString::Printf(TEXT("Snapshot assertion '%s' failed."), *Assertion.AssertionId.ToString())
						: Assertion.FailureReason);
			}
		}
	}

	FString TagsToStableKey(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		TArray<FString> Parts;
		Parts.Reserve(SortedTags.Num());
		for (const FGameplayTag& Tag : SortedTags)
		{
			Parts.Add(Tag.ToString());
		}

		return FString::Join(Parts, TEXT("|"));
	}

	bool CanDeriveEndpointOfferFromFaceRule(const FLayoutFaceRule& FaceRule)
	{
		if (!FaceRule.GetEffectiveConnectionTag().IsValid() || FaceRule.ConnectedTraversalChannels.IsEmpty())
		{
			return false;
		}

		// Endpoint offers are consumed later with root-vs-child context filters, so filled-neighbor
		// child-facing traversal interfaces still need to survive immutable snapshot derivation.
		return FaceRule.OccupancyPolicy != ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	}

	bool CanDeriveSpanOfferFromFaceRule(const FLayoutFaceRule& FaceRule)
	{
		if (!FaceRule.GetEffectiveConnectionTag().IsValid())
		{
			return false;
		}

		// Span offers likewise preserve child-facing sealing contracts; downstream consumers decide
		// whether a filled-neighbor span is root-usable or only valid when attached.
		return FaceRule.OccupancyPolicy != ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	}

	void DeriveModuleVerticalAccessContracts(FLayoutModuleSolveSnapshot& Snapshot)
	{
		Snapshot.DerivedVerticalAccessContracts.Reset();

		if (!Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess))
		{
			return;
		}

		const FLayoutFaceRule* TopFaceRule = Snapshot.EffectiveFaceRules.FindRule(ELayoutFaceDirection::PosZ);
		if (TopFaceRule == nullptr || TopFaceRule->ConnectedTraversalChannels.IsEmpty())
		{
			return;
		}

		FGameplayTagContainer SourceTraversalChannels;
		FGameplayTagContainer ExitTraversalChannels = TopFaceRule->ConnectedTraversalChannels;
		for (const FLayoutDerivedEndpointOffer& Offer : Snapshot.DerivedEndpointOffers)
		{
			if (Offer.FaceDirection == ELayoutFaceDirection::PosZ)
			{
				continue;
			}

			for (const FGameplayTag& TraversalChannel : Offer.TraversalChannels)
			{
				if (TraversalChannel.IsValid() && TopFaceRule->ConnectedTraversalChannels.HasTagExact(TraversalChannel))
				{
					SourceTraversalChannels.AddTag(TraversalChannel);
				}
			}
		}

		for (const FLayoutInternalAccessLink& Link : Snapshot.InternalAccessLinks)
		{
			if (!Link.FromTraversalChannel.IsValid() || !Link.ToTraversalChannel.IsValid())
			{
				continue;
			}

			if (TopFaceRule->ConnectedTraversalChannels.HasTagExact(Link.ToTraversalChannel))
			{
				SourceTraversalChannels.AddTag(Link.FromTraversalChannel);
				ExitTraversalChannels.AddTag(Link.ToTraversalChannel);
			}

			if (Link.bBidirectional && TopFaceRule->ConnectedTraversalChannels.HasTagExact(Link.FromTraversalChannel))
			{
				SourceTraversalChannels.AddTag(Link.ToTraversalChannel);
				ExitTraversalChannels.AddTag(Link.FromTraversalChannel);
			}
		}

		if (SourceTraversalChannels.IsEmpty() || ExitTraversalChannels.IsEmpty())
		{
			return;
		}

		FLayoutDerivedVerticalAccessContract& Contract = Snapshot.DerivedVerticalAccessContracts.AddDefaulted_GetRef();
		Contract.ContractId = FLayoutId(*FString::Printf(TEXT("%s.VerticalAccess.PosZ"), *Snapshot.SnapshotId.ToString()));
		Contract.LocalCell = FIntVector::ZeroValue;
		Contract.SourceTraversalChannels = SourceTraversalChannels;
		Contract.ExitTraversalChannels = ExitTraversalChannels;
		Contract.ExitFaceDirection = ELayoutFaceDirection::PosZ;
		Contract.bRequireMatchingYawAtExit = TopFaceRule->bRequireMatchingYawWithFilledNeighbor;
		Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
			FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Contract.ContractId.ToString())),
			ELayoutProofKind::DerivedVerticalAccess,
			Contract.ContractId,
			{Snapshot.SnapshotId},
			TEXT("Derived vertical-access contract from canonical PosZ face traversal and internal access links.")));
	}

	void DeriveModuleInterfaceContracts(FLayoutModuleSolveSnapshot& Snapshot)
	{
		Snapshot.DerivedEndpointOffers.Reset();
		Snapshot.DerivedSpanOffers.Reset();

		for (const FLayoutFaceRule& FaceRule : Snapshot.EffectiveFaceRules.ToArray())
		{
			if (CanDeriveEndpointOfferFromFaceRule(FaceRule))
			{
				FLayoutDerivedEndpointOffer& Offer = Snapshot.DerivedEndpointOffers.AddDefaulted_GetRef();
				Offer.OfferId = FLayoutId(*FString::Printf(TEXT("%s.Endpoint.%s"), *Snapshot.SnapshotId.ToString(), *StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction))));
				Offer.LocalCell = FIntVector::ZeroValue;
				Offer.FaceDirection = FaceRule.Direction;
				Offer.ConnectionTag = FaceRule.GetEffectiveConnectionTag();
				Offer.AllowedConnectionTags = FaceRule.GetEffectiveAllowedConnectionTags();
				Offer.TraversalChannels = FaceRule.ConnectedTraversalChannels;
				Offer.Roles = Snapshot.Roles;
				Offer.OccupancyPolicy = FaceRule.OccupancyPolicy;
				Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
					FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Offer.OfferId.ToString())),
					ELayoutProofKind::DerivedOffer,
					Offer.OfferId,
					{Snapshot.SnapshotId},
					FString::Printf(TEXT("Derived endpoint offer from canonical face rule %s."), *StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction)))));
			}

			const bool bBoundaryCapableRole = Snapshot.Roles.Contains(ELayoutModuleRole::Boundary)
				|| Snapshot.Roles.Contains(ELayoutModuleRole::Entry)
				|| Snapshot.Roles.Contains(ELayoutModuleRole::VerticalAccess);
			if (bBoundaryCapableRole && CanDeriveSpanOfferFromFaceRule(FaceRule))
			{
				FLayoutDerivedSpanOffer& SpanOffer = Snapshot.DerivedSpanOffers.AddDefaulted_GetRef();
				SpanOffer.SpanOfferId = FLayoutId(*FString::Printf(TEXT("%s.Span.%s"), *Snapshot.SnapshotId.ToString(), *StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction))));
				SpanOffer.ClosureId = NAME_None;
				SpanOffer.LocalCell = FIntVector::ZeroValue;
				SpanOffer.FaceDirection = FaceRule.Direction;
				SpanOffer.ConnectionTag = FaceRule.GetEffectiveConnectionTag();
				SpanOffer.AllowedConnectionTags = FaceRule.GetEffectiveAllowedConnectionTags();
				SpanOffer.Roles = Snapshot.Roles;
				SpanOffer.ThicknessCells = 1;
				SpanOffer.bSealsBoundary = FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor
					|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
					|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
					|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor;
				Snapshot.ProofRecords.Add(MakeSnapshotProofRecord(
					FLayoutId(*FString::Printf(TEXT("%s.Proof"), *SpanOffer.SpanOfferId.ToString())),
					ELayoutProofKind::DerivedSpan,
					SpanOffer.SpanOfferId,
					{Snapshot.SnapshotId},
					FString::Printf(TEXT("Derived boundary span offer from canonical face rule %s."), *StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(FaceRule.Direction)))));
			}
		}

		const bool bRequiresEndpointOffer = Snapshot.SupportsIntent(ELayoutCellIntent::Entry)
			|| Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess);
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.DerivedEndpointOfferContract"),
			ELayoutValidationAssertionKind::DerivedOfferContractValid,
			!bRequiresEndpointOffer || Snapshot.DerivedEndpointOffers.Num() > 0,
			{Snapshot.SnapshotId},
			!bRequiresEndpointOffer || Snapshot.DerivedEndpointOffers.Num() > 0
				? FString()
				: FString::Printf(TEXT("Module '%s' supports Entry or VerticalAccess but does not derive any endpoint offers from canonical face rules."), *Snapshot.DebugName.ToString())));

		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.DerivedSpanOfferContract"),
			ELayoutValidationAssertionKind::DerivedSpanContractValid,
			!Snapshot.SupportsIntent(ELayoutCellIntent::Boundary) || Snapshot.DerivedSpanOffers.Num() > 0,
			{Snapshot.SnapshotId},
			!Snapshot.SupportsIntent(ELayoutCellIntent::Boundary) || Snapshot.DerivedSpanOffers.Num() > 0
				? FString()
				: FString::Printf(TEXT("Module '%s' supports Boundary but does not derive any boundary span offers from canonical face rules."), *Snapshot.DebugName.ToString())));

		DeriveModuleVerticalAccessContracts(Snapshot);
		Snapshot.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("ModuleSnapshot.DerivedVerticalAccessContract"),
			ELayoutValidationAssertionKind::DerivedVerticalAccessContractValid,
			!Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess) || Snapshot.DerivedVerticalAccessContracts.Num() > 0,
			{Snapshot.SnapshotId},
			!Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess) || Snapshot.DerivedVerticalAccessContracts.Num() > 0
				? FString()
				: FString::Printf(TEXT("Module '%s' supports VerticalAccess but does not derive any vertical-access contracts from canonical face rules and internal access links."), *Snapshot.DebugName.ToString())));
	}

	TArray<FIntVector> BuildBoundaryLoop(const FIntPoint& FootprintSize)
	{
		TArray<FIntVector> BoundaryLoop;
		if (FootprintSize.X <= 0 || FootprintSize.Y <= 0)
		{
			return BoundaryLoop;
		}

		for (int32 X = 0; X < FootprintSize.X; ++X)
		{
			BoundaryLoop.AddUnique(FIntVector(X, 0, 0));
		}

		for (int32 Y = 1; Y < FootprintSize.Y; ++Y)
		{
			BoundaryLoop.AddUnique(FIntVector(FootprintSize.X - 1, Y, 0));
		}

		for (int32 X = FootprintSize.X - 2; X >= 0; --X)
		{
			BoundaryLoop.AddUnique(FIntVector(X, FootprintSize.Y - 1, 0));
		}

		for (int32 Y = FootprintSize.Y - 2; Y > 0; --Y)
		{
			BoundaryLoop.AddUnique(FIntVector(0, Y, 0));
		}

		return BoundaryLoop;
	}

	int32 ResolveProfileCount(
		const ELayoutCountConstraintMode Mode,
		const int32 ExactCount,
		const int32 MinCount,
		const int32 MaxCount,
		const int32 Capacity,
		const uint32 SelectionSeed)
	{
		if (Capacity <= 0 || Mode == ELayoutCountConstraintMode::None)
		{
			return 0;
		}

		if (Mode == ELayoutCountConstraintMode::Exact)
		{
			return FMath::Clamp(ExactCount, 0, Capacity);
		}

		const int32 ClampedMin = FMath::Clamp(MinCount, 0, Capacity);
		const int32 ClampedMax = FMath::Clamp(MaxCount, ClampedMin, Capacity);
		if (ClampedMax <= ClampedMin)
		{
			return ClampedMin;
		}

		return ClampedMin + static_cast<int32>(SelectionSeed % static_cast<uint32>(ClampedMax - ClampedMin + 1));
	}

	/** Adds seeded boundary cells, preferring unused sides before nearest-neighbor separation. */
	void AddMaximallySeparatedCells(
		const TArray<FIntVector>& SourceCells,
		const FIntPoint& FootprintSize,
		const int32 RequestedCount,
		const uint32 SelectionSeed,
		TArray<FIntVector>& OutCells)
	{
		if (SourceCells.IsEmpty())
		{
			return;
		}
		const auto ResolveSide = [&FootprintSize](const FIntVector& Cell) -> int32
		{
			if (Cell.X == 0) { return 0; }
			if (Cell.X == FootprintSize.X - 1) { return 1; }
			if (Cell.Y == 0) { return 2; }
			if (Cell.Y == FootprintSize.Y - 1) { return 3; }
			return INDEX_NONE;
		};

		for (int32 SelectionIndex = 0; SelectionIndex < RequestedCount; ++SelectionIndex)
		{
			TSet<int32> UsedSides;
			for (const FIntVector& SelectedCell : OutCells)
			{
				const int32 Side = ResolveSide(SelectedCell);
				if (Side != INDEX_NONE) { UsedSides.Add(Side); }
			}
			int32 BestIndex = INDEX_NONE;
			bool bBestUsesNewSide = false;
			int32 BestNearestDistance = -1;
			uint32 BestTieBreaker = MAX_uint32;
			for (int32 CandidateIndex = 0; CandidateIndex < SourceCells.Num(); ++CandidateIndex)
			{
				const FIntVector& Candidate = SourceCells[CandidateIndex];
				if (OutCells.Contains(Candidate))
				{
					continue;
				}

				const int32 CandidateSide = ResolveSide(Candidate);
				const bool bUsesNewSide = CandidateSide != INDEX_NONE && !UsedSides.Contains(CandidateSide);
				int32 NearestDistance = OutCells.IsEmpty() ? 0 : MAX_int32;
				for (const FIntVector& SelectedCell : OutCells)
				{
					NearestDistance = FMath::Min(
						NearestDistance,
						FMath::Abs(Candidate.X - SelectedCell.X)
							+ FMath::Abs(Candidate.Y - SelectedCell.Y)
							+ FMath::Abs(Candidate.Z - SelectedCell.Z));
				}
				const uint32 TieBreaker = HashCombineFast(SelectionSeed, GetTypeHash(Candidate));
				if (BestIndex == INDEX_NONE
					|| (bUsesNewSide && !bBestUsesNewSide)
					|| (bUsesNewSide == bBestUsesNewSide && NearestDistance > BestNearestDistance)
					|| (bUsesNewSide == bBestUsesNewSide && NearestDistance == BestNearestDistance && TieBreaker < BestTieBreaker))
				{
					BestIndex = CandidateIndex;
					bBestUsesNewSide = bUsesNewSide;
					BestNearestDistance = NearestDistance;
					BestTieBreaker = TieBreaker;
				}
			}
			if (BestIndex == INDEX_NONE)
			{
				return;
			}
			OutCells.Add(SourceCells[BestIndex]);
		}
	}

	/** Returns whether immutable profile-owned module snapshots expose an Entry orientation for one exterior topology zone. */
	bool DoesCatalogSupportExteriorEntryZone(
		const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots,
		const ELayoutPlacementZone CellPlacementZone)
	{
		return ModuleSnapshots.ContainsByPredicate([CellPlacementZone](const FLayoutModuleSolveSnapshot& ModuleSnapshot)
		{
			if (!ModuleSnapshot.SupportsIntent(ELayoutCellIntent::Entry)
				|| ModuleSnapshot.AllowedYawRotationSteps.IsEmpty())
			{
				return false;
			}
			return ModuleSnapshot.PlacementZone == ELayoutPlacementZone::Any
				|| ModuleSnapshot.PlacementZone == ELayoutPlacementZone::Perimeter
				|| ModuleSnapshot.PlacementZone == CellPlacementZone;
		});
	}

	/** Resolves deterministic exterior Entry cells without weakening authored exact/range counts to footprint capacity. */
	bool TryBuildEntryCells(
		const FLayoutProfileSolveSnapshot& Profile,
		const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots,
		const FIntPoint& FootprintSize,
		const int32 Seed,
		TArray<FIntVector>& OutEntryCells,
		FString& OutFailureReason)
	{
		OutEntryCells.Reset();
		OutFailureReason.Reset();
		if (FootprintSize.X <= 0 || FootprintSize.Y <= 0)
		{
			return Profile.EntryCountMode == ELayoutCountConstraintMode::None;
		}

		const TArray<FIntVector> BoundaryLoop = BuildBoundaryLoop(FootprintSize);
		const uint32 SelectionSeed = HashCombine(GetTypeHash(FootprintSize), static_cast<uint32>(Seed));
		const int32 EntryCount = ResolveProfileCount(
			Profile.EntryCountMode,
			Profile.EntryCount,
			Profile.MinEntryCount,
			Profile.MaxEntryCount,
			MAX_int32,
			SelectionSeed);
		if (EntryCount <= 0)
		{
			return true;
		}
		if (EntryCount > BoundaryLoop.Num())
		{
			OutFailureReason = FString::Printf(
				TEXT("Entry count mode '%s' requires exactly %d exterior Entry cells, but selected footprint %dx%d has only %d boundary cells."),
				*StaticEnum<ELayoutCountConstraintMode>()->GetNameStringByValue(static_cast<int64>(Profile.EntryCountMode)),
				EntryCount,
				FootprintSize.X,
				FootprintSize.Y,
				BoundaryLoop.Num());
			return false;
		}

		TArray<FIntVector> NonCornerBoundaryCells;
		TArray<FIntVector> CornerBoundaryCells;
		for (const FIntVector& BoundaryCell : BoundaryLoop)
		{
			const ELayoutPlacementZone PlacementZone = IsCornerBoundaryCell(BoundaryCell, FootprintSize)
				? ELayoutPlacementZone::Corner
				: ELayoutPlacementZone::Edge;
			if (!DoesCatalogSupportExteriorEntryZone(ModuleSnapshots, PlacementZone))
			{
				continue;
			}
			if (PlacementZone == ELayoutPlacementZone::Corner)
			{
				CornerBoundaryCells.Add(BoundaryCell);
			}
			else
			{
				NonCornerBoundaryCells.Add(BoundaryCell);
			}
		}
		if (EntryCount > NonCornerBoundaryCells.Num() + CornerBoundaryCells.Num())
		{
			OutFailureReason = FString::Printf(
				TEXT("Entry count mode '%s' requires exactly %d exterior Entry cells, but selected footprint %dx%d has only %d catalog-supported exterior Entry cells."),
				*StaticEnum<ELayoutCountConstraintMode>()->GetNameStringByValue(static_cast<int64>(Profile.EntryCountMode)),
				EntryCount,
				FootprintSize.X,
				FootprintSize.Y,
				NonCornerBoundaryCells.Num() + CornerBoundaryCells.Num());
			return false;
		}

		const int32 NonCornerCount = FMath::Min(EntryCount, NonCornerBoundaryCells.Num());
		AddMaximallySeparatedCells(NonCornerBoundaryCells, FootprintSize, NonCornerCount, SelectionSeed, OutEntryCells);
		AddMaximallySeparatedCells(CornerBoundaryCells, FootprintSize, EntryCount - OutEntryCells.Num(), HashCombineFast(SelectionSeed, GetTypeHash(FName(TEXT("CornerEntryFallback")))), OutEntryCells);
		return true;
	}

	/** Selects stepped-root entries from the adapter-approved ground-level boundary cells. */
	bool TryBuildQualifiedSteppedRootEntryCells(
		const FLayoutProfileSolveSnapshot& Profile,
		const FIntPoint& FootprintSize,
		const int32 Seed,
		const TArray<FIntVector>& QualifiedEntryCells,
		TArray<FIntVector>& OutEntryCells,
		FString& OutFailureReason)
	{
		OutEntryCells.Reset();
		OutFailureReason.Reset();

		const TArray<FIntVector> BoundaryLoop = BuildBoundaryLoop(FootprintSize);
		if (BoundaryLoop.IsEmpty())
		{
			OutFailureReason = TEXT("Stepped root entry planning requires a non-empty footprint boundary.");
			return false;
		}

		TSet<FIntVector> QualifiedCellSet;
		for (const FIntVector& QualifiedCell : QualifiedEntryCells)
		{
			if (QualifiedCell.Z == 0)
			{
				QualifiedCellSet.Add(QualifiedCell);
			}
		}

		TArray<FIntVector> NonCornerCandidates;
		TArray<FIntVector> CornerCandidates;
		for (const FIntVector& BoundaryCell : BoundaryLoop)
		{
			if (!QualifiedCellSet.Contains(BoundaryCell))
			{
				continue;
			}

			if (IsCornerBoundaryCell(BoundaryCell, FootprintSize))
			{
				CornerCandidates.Add(BoundaryCell);
			}
			else
			{
				NonCornerCandidates.Add(BoundaryCell);
			}
		}

		const uint32 SelectionSeed = HashCombine(GetTypeHash(FootprintSize), static_cast<uint32>(Seed));
		const int32 RequestedCount = ResolveProfileCount(
			Profile.EntryCountMode,
			Profile.EntryCount,
			Profile.MinEntryCount,
			Profile.MaxEntryCount,
			BoundaryLoop.Num(),
			SelectionSeed);
		if (RequestedCount <= 0)
		{
			return true;
		}

		if (NonCornerCandidates.Num() + CornerCandidates.Num() < RequestedCount)
		{
			OutFailureReason = FString::Printf(
				TEXT("Stepped root entry planning requires %d qualified Z=0 boundary cells but only %d are available."),
				RequestedCount,
				NonCornerCandidates.Num() + CornerCandidates.Num());
			return false;
		}

		const int32 NonCornerCount = FMath::Min(RequestedCount, NonCornerCandidates.Num());
		AddMaximallySeparatedCells(NonCornerCandidates, FootprintSize, NonCornerCount, SelectionSeed, OutEntryCells);
		AddMaximallySeparatedCells(CornerCandidates, FootprintSize, RequestedCount - OutEntryCells.Num(), HashCombineFast(SelectionSeed, GetTypeHash(FName(TEXT("CornerEntryFallback")))), OutEntryCells);

		if (OutEntryCells.Num() != RequestedCount)
		{
			OutFailureReason = TEXT("Stepped root entry planning could not select the requested number of qualified Z=0 boundary cells.");
			return false;
		}

		return true;
	}

	bool TryGetWorldFaceRule(
		const ULayoutModuleAsset* Module,
		ELayoutFaceDirection WorldDirection,
		int32 YawRotationSteps,
		FLayoutFaceRule& OutFaceRule);

	bool AreConnectionTagsCompatible(const FLayoutFaceRule& SourceFaceRule, const FLayoutFaceRule& TargetFaceRule);

	bool AreSolverYawRotationsCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		int32 SourceYawRotationSteps,
		const FLayoutFaceRule& TargetFaceRule,
		int32 TargetYawRotationSteps);

	bool HasSharedConnectedWalkableArea(const FLayoutFaceRule& LeftFaceRule, const FLayoutFaceRule& RightFaceRule);

	bool IsFaceCompatibleWithOccupancy(const FLayoutFaceRule& FaceRule, bool bNeighborPlanned, bool bNeighborFilled);

	bool RequiresWalkableFilledCompatibility(const FLayoutFaceRule& FaceRule);

	FLayoutModuleFaceRules BuildWorldFaceRulesForYaw(const FLayoutModuleFaceRules& EffectiveRules, int32 YawRotationSteps);

	ELayoutCellIntent DetermineProvisionalIntentWithoutVerticalAccess(
		const FIntPoint& FootprintSize,
		const TSet<FIntVector>& EntryCells,
		const FIntVector& Cell)
	{
		if (EntryCells.Contains(Cell))
		{
			return ELayoutCellIntent::Entry;
		}

		if (IsBoundaryCell(Cell, FootprintSize))
		{
			return ELayoutCellIntent::Boundary;
		}

		const float CenterX = static_cast<float>(FootprintSize.X - 1) * 0.5f;
		const float CenterY = static_cast<float>(FootprintSize.Y - 1) * 0.5f;
		const float DistanceX = FMath::Abs(static_cast<float>(Cell.X) - CenterX);
		const float DistanceY = FMath::Abs(static_cast<float>(Cell.Y) - CenterY);
		if (Cell.Z == 0 && DistanceX <= 0.5f && DistanceY <= 0.5f)
		{
			return ELayoutCellIntent::Core;
		}

		return ELayoutCellIntent::Interior;
	}

	bool DoesNeighborIntentProvideSupportForVerticalFace(
		const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots,
		const ELayoutCellIntent NeighborIntent,
		const FLayoutFaceRule& VerticalFaceRule,
		const int32 VerticalYawSteps,
		const ELayoutFaceDirection Direction)
	{
		if (ModuleSnapshots.IsEmpty())
		{
			return false;
		}

		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ModuleSnapshots)
		{
			if (!ModuleSnapshot.SupportsIntent(NeighborIntent))
			{
				continue;
			}

			for (const int32 NeighborYawSteps : ModuleSnapshot.AllowedYawRotationSteps)
			{
				const FLayoutModuleFaceRules NeighborWorldFaceRules = BuildWorldFaceRulesForYaw(ModuleSnapshot.EffectiveFaceRules, NeighborYawSteps);
				const FLayoutFaceRule* NeighborFaceRule = NeighborWorldFaceRules.FindRule(FLayoutDirectionUtils::GetOpposite(Direction));
				if (NeighborFaceRule == nullptr)
				{
					continue;
				}

				if (IsFaceCompatibleWithOccupancy(VerticalFaceRule, true, true)
					&& IsFaceCompatibleWithOccupancy(*NeighborFaceRule, true, true)
					&& AreConnectionTagsCompatible(VerticalFaceRule, *NeighborFaceRule)
					&& AreConnectionTagsCompatible(*NeighborFaceRule, VerticalFaceRule)
					&& AreSolverYawRotationsCompatible(VerticalFaceRule, VerticalYawSteps, *NeighborFaceRule, NeighborYawSteps)
					&& (!(RequiresWalkableFilledCompatibility(VerticalFaceRule) || RequiresWalkableFilledCompatibility(*NeighborFaceRule))
						|| HasSharedConnectedWalkableArea(VerticalFaceRule, *NeighborFaceRule)))
				{
					return true;
				}
			}
		}

		return false;
	}

	int32 CountViableVerticalAccessOrientationsAtCell(
		const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots,
		const FLayoutProfileSolveSnapshot& Profile,
		const FIntPoint& FootprintSize,
		const TSet<FIntVector>& EntryCells,
		const FIntVector& Cell)
	{
		if (ModuleSnapshots.IsEmpty())
		{
			return 0;
		}

		int32 ViableOrientationCount = 0;
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ModuleSnapshots)
		{
			if (!ModuleSnapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess))
			{
				continue;
			}

			for (const int32 YawSteps : ModuleSnapshot.AllowedYawRotationSteps)
			{
				const FLayoutModuleFaceRules WorldFaceRules = BuildWorldFaceRulesForYaw(ModuleSnapshot.EffectiveFaceRules, YawSteps);
				bool bAllHorizontalFacesSupported = true;
				bool bHasSupportedWalkableHorizontalFace = false;
				for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
				{
					const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
					if (!IsHorizontalDirection(Direction))
					{
						continue;
					}

					const FLayoutFaceRule* FaceRule = WorldFaceRules.FindRule(Direction);
					if (FaceRule == nullptr)
					{
						bAllHorizontalFacesSupported = false;
						break;
					}

					const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (NeighborCell.X < 0 || NeighborCell.X >= FootprintSize.X || NeighborCell.Y < 0 || NeighborCell.Y >= FootprintSize.Y)
					{
						if (!IsFaceCompatibleWithOccupancy(*FaceRule, false, false))
						{
							bAllHorizontalFacesSupported = false;
							break;
						}

						continue;
					}

						const ELayoutCellIntent NeighborIntent = DetermineProvisionalIntentWithoutVerticalAccess(
							FootprintSize,
							EntryCells,
							NeighborCell);
						if (!DoesNeighborIntentProvideSupportForVerticalFace(ModuleSnapshots, NeighborIntent, *FaceRule, YawSteps, Direction))
						{
							bAllHorizontalFacesSupported = false;
							break;
					}

					if (!FaceRule->ConnectedTraversalChannels.IsEmpty())
					{
						bHasSupportedWalkableHorizontalFace = true;
					}
				}

				if (bAllHorizontalFacesSupported && bHasSupportedWalkableHorizontalFace)
				{
					++ViableOrientationCount;
				}
			}
		}

		return ViableOrientationCount;
	}

	bool IsHorizontallyAdjacentCell(const FIntVector& Left, const FIntVector& Right)
	{
		return Left.Z == Right.Z
			&& FMath::Abs(Left.X - Right.X) + FMath::Abs(Left.Y - Right.Y) == 1;
	}

	uint32 GetVerticalAccessSelectionSeed(const FIntPoint& FootprintSize, const int32 Seed)
	{
		return HashCombineFast(
			HashCombine(GetTypeHash(FootprintSize), static_cast<uint32>(Seed)),
			GetTypeHash(FName(TEXT("VerticalAccess"))));
	}

	TSet<FIntVector> BuildVerticalAccessCells(
		const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots,
		const FLayoutProfileSolveSnapshot& Profile,
		const FIntPoint& FootprintSize,
		const TSet<FIntVector>& EntryCells,
		const TSet<FIntPoint>& DisfavoredColumns,
		const int32 TransitionLevel,
		const int32 Seed)
	{
		TSet<FIntVector> Result;
		if (Profile.VerticalAccessCountMode == ELayoutCountConstraintMode::None)
		{
			return Result;
		}

		TArray<FIntVector> Candidates;
		{
			const bool bAllowBoundaryVerticalAccess = DoesTransitionPreferBoundaryVerticalAccess(Profile, TransitionLevel);
			const int32 MinCandidateX = bAllowBoundaryVerticalAccess ? 0 : 1;
			const int32 MinCandidateY = bAllowBoundaryVerticalAccess ? 0 : 1;
			const int32 MaxCandidateX = bAllowBoundaryVerticalAccess ? FootprintSize.X : FootprintSize.X - 1;
			const int32 MaxCandidateY = bAllowBoundaryVerticalAccess ? FootprintSize.Y : FootprintSize.Y - 1;
			for (int32 Y = MinCandidateY; Y < MaxCandidateY; ++Y)
			{
				for (int32 X = MinCandidateX; X < MaxCandidateX; ++X)
				{
					const FIntVector Cell(X, Y, TransitionLevel);
					if (EntryCells.Contains(Cell))
					{
						continue;
					}

					Candidates.Add(Cell);
				}
			}
		}

		if (Candidates.IsEmpty())
		{
			return Result;
		}

		const uint32 SelectionSeed = GetVerticalAccessSelectionSeed(FootprintSize, Seed);
			const int32 RequestedCount = ResolveProfileCount(
			Profile.VerticalAccessCountMode,
			Profile.VerticalAccessCount,
			Profile.MinVerticalAccessCount,
			Profile.MaxVerticalAccessCount,
			Candidates.Num(),
			SelectionSeed);
		if (RequestedCount <= 0)
		{
			return Result;
		}

		struct FScoredVerticalAccessCell
		{
			FIntVector Cell = FIntVector::ZeroValue;
			int32 ViableOrientationCount = 0;
			int32 DistanceToClosestBoundary = 0;
			bool bDisfavoredColumn = false;
			uint32 TieBreakHash = 0;
		};

		TArray<FScoredVerticalAccessCell> ScoredCandidates;
		for (const FIntVector& Candidate : Candidates)
		{
			FScoredVerticalAccessCell& Scored = ScoredCandidates.AddDefaulted_GetRef();
			Scored.Cell = Candidate;
			Scored.ViableOrientationCount = CountViableVerticalAccessOrientationsAtCell(
				ModuleSnapshots,
				Profile,
				FootprintSize,
				EntryCells,
				Candidate);
			Scored.DistanceToClosestBoundary = FMath::Min(
				FMath::Min(Candidate.X, Candidate.Y),
				FMath::Min(FootprintSize.X - 1 - Candidate.X, FootprintSize.Y - 1 - Candidate.Y));
			Scored.bDisfavoredColumn = DisfavoredColumns.Contains(FIntPoint(Candidate.X, Candidate.Y));
			Scored.TieBreakHash = HashCombineFast(GetTypeHash(Candidate), SelectionSeed);
		}

		const bool bHasConstraintAwareCandidate = ScoredCandidates.ContainsByPredicate([](const FScoredVerticalAccessCell& Candidate)
		{
			return Candidate.ViableOrientationCount > 0;
		});
		const bool bHasPreferredCandidate = ScoredCandidates.ContainsByPredicate([](const FScoredVerticalAccessCell& Candidate)
		{
			return !Candidate.bDisfavoredColumn;
		});
		const bool bPreferBoundaryVerticalAccess = DoesTransitionPreferBoundaryVerticalAccess(Profile, TransitionLevel);

		ScoredCandidates.Sort([bHasConstraintAwareCandidate, bHasPreferredCandidate, bPreferBoundaryVerticalAccess](const FScoredVerticalAccessCell& Left, const FScoredVerticalAccessCell& Right)
		{
			if (bHasPreferredCandidate && Left.bDisfavoredColumn != Right.bDisfavoredColumn)
			{
				return !Left.bDisfavoredColumn;
			}

			if (bHasConstraintAwareCandidate && Left.ViableOrientationCount != Right.ViableOrientationCount)
			{
				return Left.ViableOrientationCount > Right.ViableOrientationCount;
			}

			if (bPreferBoundaryVerticalAccess && Left.DistanceToClosestBoundary != Right.DistanceToClosestBoundary)
			{
				return Left.DistanceToClosestBoundary < Right.DistanceToClosestBoundary;
			}

			if (Left.TieBreakHash != Right.TieBreakHash)
			{
				return Left.TieBreakHash < Right.TieBreakHash;
			}

			if (!bPreferBoundaryVerticalAccess && Left.DistanceToClosestBoundary != Right.DistanceToClosestBoundary)
			{
				return Left.DistanceToClosestBoundary > Right.DistanceToClosestBoundary;
			}

			if (Left.Cell.Y != Right.Cell.Y)
			{
				return Left.Cell.Y < Right.Cell.Y;
			}

			return Left.Cell.X < Right.Cell.X;
		});

		for (const FScoredVerticalAccessCell& Candidate : ScoredCandidates)
		{
			bool bTouchesSelectedAccess = false;
			for (const FIntVector& SelectedCell : Result)
			{
				if (IsHorizontallyAdjacentCell(Candidate.Cell, SelectedCell))
				{
					bTouchesSelectedAccess = true;
					break;
				}
			}

			if (bTouchesSelectedAccess)
			{
				continue;
			}

			Result.Add(Candidate.Cell);
			if (Result.Num() >= RequestedCount)
			{
				return Result;
			}
		}

		for (const FScoredVerticalAccessCell& Candidate : ScoredCandidates)
		{
			if (!Result.Contains(Candidate.Cell))
			{
				Result.Add(Candidate.Cell);
			}

			if (Result.Num() >= RequestedCount)
			{
				break;
			}
		}

		return Result;
	}

	ELayoutCellIntent DetermineIntent(
		const FLayoutProfileSolveSnapshot& Profile,
		const FIntPoint& FootprintSize,
		const TSet<FIntVector>& EntryCells,
		const TSet<FIntVector>& VerticalAccessCells,
		const FIntVector& Cell)
	{
		if (EntryCells.Contains(Cell))
		{
			return ELayoutCellIntent::Entry;
		}

		if (VerticalAccessCells.Contains(Cell))
		{
			return ELayoutCellIntent::VerticalAccess;
		}

		if (IsBoundaryCell(Cell, FootprintSize))
		{
			return ELayoutCellIntent::Boundary;
		}

		const float CenterX = static_cast<float>(FootprintSize.X - 1) * 0.5f;
		const float CenterY = static_cast<float>(FootprintSize.Y - 1) * 0.5f;
		const float DistanceX = FMath::Abs(static_cast<float>(Cell.X) - CenterX);
		const float DistanceY = FMath::Abs(static_cast<float>(Cell.Y) - CenterY);
		if (Cell.Z == 0 && DistanceX <= 0.5f && DistanceY <= 0.5f)
		{
			return ELayoutCellIntent::Core;
		}

		return ELayoutCellIntent::Interior;
	}

	ELayoutWorldBindingPlacementKind ResolveEffectiveRootPlacementKind(
		const FLayoutRegionSolveRequest& Request)
	{
		return Request.bHasSelectedModePlan
			? Request.SelectedModePlan.PlacementKind
			: Request.RootPlacementKind;
	}

	/** Runs stepped VerticalAccess planning only when traversal or an authored count requires ascent. */
	bool ShouldUseSteppedTerrainAwareVerticalAccessPlanning(
		const FLayoutRegionSolveRequest& Request)
	{
		const bool bHasAuthoredVerticalAccessRequirement =
			(Request.ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
				&& Request.ProfileSnapshot.VerticalAccessCount > 0)
			|| (Request.ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Range
				&& Request.ProfileSnapshot.MaxVerticalAccessCount > 0);
		return ResolveEffectiveRootPlacementKind(Request) != ELayoutWorldBindingPlacementKind::None
			&& Request.ProfileSnapshot.bSupportsSteppedTerrainSolve
			&& (Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable
				|| bHasAuthoredVerticalAccessRequirement)
			&& Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0
			&& !Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty()
			&& Request.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta > 0;
	}

	bool ShouldUseSteppedTerrainAwareEntryPlanning(
		const FLayoutRegionSolveRequest& Request)
	{
		return ResolveEffectiveRootPlacementKind(Request) != ELayoutWorldBindingPlacementKind::None
			&& Request.ProfileSnapshot.bSupportsSteppedTerrainSolve
			&& Request.ProfileSnapshot.EntryCountMode != ELayoutCountConstraintMode::None
			&& Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0
			&& !Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty()
			&& Request.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta > 0
;
	}

	/** Tests whether final topology admits an occupied Entry-role module at this cell. */
	bool DoesEntryCellAdmitCandidate(
		const FLayoutRegionSolveRequest& Request,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& SourceCells,
		const FIntVector& EntryCell,
		FString& OutFailureReason);

	int32 ResolveSteppedTerrainEntrySnappedLevel(
		const FLayoutSteppedTerrainSupportMap& SupportMap,
		const FLayoutSteppedTerrainSupportSample& SupportSample)
	{
		if (SupportMap.SharedCellHeightInBlocks <= 0)
		{
			return 0;
		}

		const int32 SnappedCeilingZ = SupportSample.SnappedSupportCeilingZ != 0
			? SupportSample.SnappedSupportCeilingZ
			: SupportSample.SupportSurfaceZ;
		return FMath::FloorToInt(static_cast<float>(SnappedCeilingZ) / static_cast<float>(SupportMap.SharedCellHeightInBlocks));
	}

	bool ApplySteppedTerrainAwareEntryPlanning(
		const FLayoutRegionSolveRequest& Request,
		const FIntPoint& FootprintSize,
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		if (!ShouldUseSteppedTerrainAwareEntryPlanning(Request))
		{
			return true;
		}

	// Snapped level is a column property — key by XY so post-shift
		// cells at higher Z still find their column's snapped level.
		TMap<FIntPoint, int32> SnappedLevelByColumn;
		SnappedLevelByColumn.Reserve(Request.SteppedTerrainSupportMap.SupportSamples.Num());
		for (const FLayoutSteppedTerrainSupportSample& SupportSample : Request.SteppedTerrainSupportMap.SupportSamples)
		{
			SnappedLevelByColumn.Add(
				FIntPoint(SupportSample.LocalCell.X, SupportSample.LocalCell.Y),
				ResolveSteppedTerrainEntrySnappedLevel(Request.SteppedTerrainSupportMap, SupportSample));
		}

		int32 EntryCount = 0;
		for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				++EntryCount;
			}
		}
		if (EntryCount <= 0)
		{
			return true;
		}

		const bool bIsContinuationRoot = IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request));
		const int32 RequiredContinuationEntryLevel =
			bIsContinuationRoot
				? Request.RootContinuationSelection.ResolvedEntryLevel
				: INDEX_NONE;
		const bool bRequiresQualifiedGroundEntries =
			Request.bHasQualifiedEntryCells
			&& ResolveEffectiveRootPlacementKind(Request) != ELayoutWorldBindingPlacementKind::None
			&& Request.ProfileSnapshot.bSupportsSteppedTerrainSolve
			&& !bIsContinuationRoot
			&& Request.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta > 0;
		TSet<FIntVector> QualifiedGroundEntryCells;
		if (bRequiresQualifiedGroundEntries)
		{
			for (const FIntVector& QualifiedCell : Request.QualifiedEntryCells)
			{
				if (QualifiedCell.Z == 0)
				{
					QualifiedGroundEntryCells.Add(QualifiedCell);
				}
			}
		}

		TMap<int32, int32> PlateauSizeByLevel;
		TMap<int32, int32> TransitionCountByLevel;
	for (const TPair<FIntPoint, int32>& Pair : SnappedLevelByColumn)
		{
			int32& PlateauSize = PlateauSizeByLevel.FindOrAdd(Pair.Value);
			++PlateauSize;
		}
		for (const FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep : Request.SteppedTerrainSupportMap.AdjacencySteps)
		{
			if (AdjacencyStep.SnappedLevelDelta <= 0)
			{
				continue;
			}

			const int32* FromLevel = SnappedLevelByColumn.Find(FIntPoint(AdjacencyStep.FromCell.X, AdjacencyStep.FromCell.Y));
			const int32* ToLevel = SnappedLevelByColumn.Find(FIntPoint(AdjacencyStep.ToCell.X, AdjacencyStep.ToCell.Y));
			if (FromLevel == nullptr || ToLevel == nullptr || *FromLevel == *ToLevel)
			{
				continue;
			}
			++TransitionCountByLevel.FindOrAdd(*FromLevel);
			++TransitionCountByLevel.FindOrAdd(*ToLevel);
		}

		struct FScoredTerrainAwareEntryCell
		{
			FIntVector Cell = FIntVector::ZeroValue;
			int32 SnappedLevel = 0;
			int32 TransitionCount = 0;
			int32 PlateauSize = 0;
			bool bCurrentEntry = false;
			uint32 TieBreakHash = 0;
		};

		TArray<FScoredTerrainAwareEntryCell> Candidates;
		for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			const bool bCurrentEntry = PlannedCell.Intent == ELayoutCellIntent::Entry;
			const bool bAuthoredEntryCapableCandidate =
				IsBoundaryCell(PlannedCell.Cell, FootprintSize);
			if (!bAuthoredEntryCapableCandidate || PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
				|| PlannedCell.bIsBridgeCell
				|| (bRequiresQualifiedGroundEntries
					&& (PlannedCell.Cell.Z != 0 || !QualifiedGroundEntryCells.Contains(PlannedCell.Cell))))
			{
				continue;
			}

			const int32* SnappedLevel = SnappedLevelByColumn.Find(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
			if (SnappedLevel == nullptr)
			{
				continue;
			}

			if (RequiredContinuationEntryLevel != INDEX_NONE
				&& *SnappedLevel != RequiredContinuationEntryLevel)
			{
				continue;
			}

			FScoredTerrainAwareEntryCell& Candidate = Candidates.AddDefaulted_GetRef();
			Candidate.Cell = PlannedCell.Cell;
			Candidate.SnappedLevel = *SnappedLevel;
			Candidate.TransitionCount = TransitionCountByLevel.FindRef(*SnappedLevel);
			Candidate.PlateauSize = PlateauSizeByLevel.FindRef(*SnappedLevel);
			Candidate.bCurrentEntry = bCurrentEntry;
			Candidate.TieBreakHash = HashCombineFast(
				GetTypeHash(Candidate.Cell),
				HashCombine(Request.Seed, GetTypeHash(FName(TEXT("TerrainAwareEntry")))));
		}

			// Fail fast when stepped terrain solve requires entries but no authored entry-capable
		// candidate can seed a legal first stage. Zero candidates means every snapped support
		// cell either lacks terrain data, is a vertical-access cell, or violates the continuation
		// alignment contract — none are eligible to seed a staged plateau.
		if (Candidates.Num() < EntryCount)
		{
			OutFailureReason = TEXT("Stepped terrain entry planning found fewer topology-valid candidates than required entries.");
			return false;
		}


		Candidates.Sort([](const FScoredTerrainAwareEntryCell& Left, const FScoredTerrainAwareEntryCell& Right)
		{
			if (Left.TransitionCount != Right.TransitionCount)
			{
				return Left.TransitionCount < Right.TransitionCount;
			}
			if (Left.PlateauSize != Right.PlateauSize)
			{
				return Left.PlateauSize > Right.PlateauSize;
			}
			if (Left.bCurrentEntry != Right.bCurrentEntry)
			{
				return Left.bCurrentEntry;
			}
			if (Left.TieBreakHash != Right.TieBreakHash)
			{
				return Left.TieBreakHash < Right.TieBreakHash;
			}
			if (Left.Cell.Z != Right.Cell.Z)
			{
				return Left.Cell.Z < Right.Cell.Z;
			}
			if (Left.Cell.Y != Right.Cell.Y)
			{
				return Left.Cell.Y < Right.Cell.Y;
			}
			return Left.Cell.X < Right.Cell.X;
		});

		TSet<FIntVector> SelectedEntries;
		FString FirstAdmissionFailure;
		for (const FScoredTerrainAwareEntryCell& Candidate : Candidates)
		{
			FString AdmissionFailure;
			if (!DoesEntryCellAdmitCandidate(
				Request,
				FootprintSize,
				InOutPlannedCells,
				Candidate.Cell,
				AdmissionFailure))
			{
				if (FirstAdmissionFailure.IsEmpty())
				{
					FirstAdmissionFailure = AdmissionFailure;
				}
				continue;
			}

			SelectedEntries.Add(Candidate.Cell);
			if (SelectedEntries.Num() == EntryCount)
			{
				break;
			}
		}
		if (SelectedEntries.Num() != EntryCount)
		{
			OutFailureReason = FString::Printf(
				TEXT("Stepped terrain entry planning could not find %d module-admitted Entry candidates after final topology resolution. First rejected candidate: %s"),
				EntryCount,
				FirstAdmissionFailure.IsEmpty() ? TEXT("none") : *FirstAdmissionFailure);
			return false;
		}

		// Refine entry placement on the existing planned-cell carrier so downstream request paths keep one source of truth.
		TSet<FIntVector> EmptyVerticalAccessCells;
		for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				PlannedCell.EntryOrigin = ELayoutEntryOrigin::None;
				PlannedCell.Intent = DetermineIntent(
					Request.ProfileSnapshot,
					FootprintSize,
					SelectedEntries,
					EmptyVerticalAccessCells,
					PlannedCell.Cell);
			}
			if (SelectedEntries.Contains(PlannedCell.Cell))
			{
				PlannedCell.Intent = ELayoutCellIntent::Entry;
				PlannedCell.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
			}
		}

		return true;
	}

	/** Resolves preserved authored-level authority, falling back only for legacy cells. */
	int32 ResolvePlannedCellModuleLevel(const FLayoutPlannedCell& PlannedCell)
	{
		return PlannedCell.ModuleLevelIndex != INDEX_NONE
			? PlannedCell.ModuleLevelIndex
			: PlannedCell.Cell.Z;
	}

	/** Returns whether a VerticalAccess cell has one clear opposing lateral traversal axis. */
	bool HasLateralVerticalAccessClearance(
		const FIntVector& Cell,
		const TMap<FIntVector, int32>& PlannedCellIndexByCell,
		const TArray<FLayoutPlannedCell>& PlannedCells)
	{
		auto IsTopShellBoundary = [&PlannedCells](const FLayoutPlannedCell& NeighborCell)
		{
			if (NeighborCell.Intent != ELayoutCellIntent::Boundary)
			{
				return false;
			}

			int32 HighestModuleLevel = INDEX_NONE;
			for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
			{
				if (PlannedCell.Cell.X == NeighborCell.Cell.X
					&& PlannedCell.Cell.Y == NeighborCell.Cell.Y)
				{
					HighestModuleLevel = FMath::Max(
						HighestModuleLevel,
						ResolvePlannedCellModuleLevel(PlannedCell));
				}
			}
			return ResolvePlannedCellModuleLevel(NeighborCell) == HighestModuleLevel;
		};
		auto IsClearLateralNeighbor = [&PlannedCellIndexByCell, &PlannedCells, &IsTopShellBoundary](
		const FIntVector& NeighborCell)
		{
			const int32* NeighborIndex = PlannedCellIndexByCell.Find(NeighborCell);
			return NeighborIndex != nullptr
				&& PlannedCells.IsValidIndex(*NeighborIndex)
				&& !IsTopShellBoundary(PlannedCells[*NeighborIndex]);
		};

		return (IsClearLateralNeighbor(Cell + FIntVector(1, 0, 0))
				&& IsClearLateralNeighbor(Cell + FIntVector(-1, 0, 0)))
			|| (IsClearLateralNeighbor(Cell + FIntVector(0, 1, 0))
				&& IsClearLateralNeighbor(Cell + FIntVector(0, -1, 0)));
	}

	/** Returns whether both levels of a VerticalAccess pair are clear Interior terrain. */
	bool IsCleanVerticalAccessHostPair(
		const FIntVector& LowerCell,
		const TMap<FIntVector, int32>& PlannedCellIndexByCell,
		const TArray<FLayoutPlannedCell>& PlannedCells)
	{
		for (const int32 ZOffset : { 0, 1 })
		{
			const FIntVector Cell = LowerCell + FIntVector(0, 0, ZOffset);
			const int32* CellIndex = PlannedCellIndexByCell.Find(Cell);
			if (CellIndex == nullptr || !PlannedCells.IsValidIndex(*CellIndex))
			{
				return false;
			}

			const FLayoutPlannedCell& PlannedCell = PlannedCells[*CellIndex];
			if (PlannedCell.bIsBridgeCell
				|| PlannedCell.Intent != ELayoutCellIntent::Interior
				|| PlannedCell.PlacementZone != ELayoutPlacementZone::Interior
				|| PlannedCell.TerrainSeamFaceMask != 0
				|| PlannedCell.VerticalAccessLandingContactMask != 0
				|| !HasLateralVerticalAccessClearance(Cell, PlannedCellIndexByCell, PlannedCells))
			{
				return false;
			}

			for (const FIntVector& LateralDelta : {
				FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
				FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
			{
				const int32* NeighborIndex = PlannedCellIndexByCell.Find(Cell + LateralDelta);
				if (NeighborIndex == nullptr || !PlannedCells.IsValidIndex(*NeighborIndex)
					|| PlannedCells[*NeighborIndex].TerrainSeamFaceMask != 0
					|| PlannedCells[*NeighborIndex].Intent == ELayoutCellIntent::Boundary)
				{
					return false;
				}
			}
		}
		return true;
	}

	/** Builds a stable seeded rank for one VerticalAccess host option. */
	uint32 GetVerticalAccessHostSeedRank(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutId GroupId,
		const FIntVector& LowerCell)
	{
		return HashCombineFast(
			GetTypeHash(LowerCell),
			HashCombineFast(GetTypeHash(GroupId), static_cast<uint32>(Request.Seed)));
	}

	/** Admits local host contracts, retaining prospective joint support for the later atomic proof.
	 * InvocationVariants may retain only catalog-derived variants within one freeze call's immutable
	 * module catalog. Entry/topology/support domains and proofs are always rebuilt, never borrowed.
	 */
	bool DoesVerticalAccessHostAdmitCandidatePair(
		const FLayoutRegionSolveRequest& Request,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& SourceCells,
		const FIntVector& LowerCell,
		FString& OutFailureReason,
		const TArray<FIntVector>* PotentialHostRoots = nullptr,
		FLayoutVerticalAccessHostOption* OutExactWitness = nullptr,
		TArray<FLayoutVerticalAccessHostOption>* OutExactWitnesses = nullptr,
		FSolveContext* InvocationVariants = nullptr);

	/** Ranks spacing without removing close alternatives; footprint-scaled bands retain seeded variation. */
	int32 GetVerticalAccessHostSeparationBand(const FLayoutVerticalAccessHostOption& Option,
		const TSet<FIntVector>& EntryCells, const TSet<FIntPoint>& SelectedColumns,
		const FIntPoint& FootprintSize)
	{
		int32 Distance = MAX_int32;
		for (const FIntPoint& Column : SelectedColumns)
		{
			Distance = FMath::Min(Distance, FMath::Abs(Option.LowerCell.X - Column.X)
				+ FMath::Abs(Option.LowerCell.Y - Column.Y));
		}
		for (const FIntVector& Entry : EntryCells)
		{
			for (const FIntVector& Cell : Option.OccupiedCells)
			{
				Distance = FMath::Min(Distance, FMath::Abs(Cell.X - Entry.X)
					+ FMath::Abs(Cell.Y - Entry.Y) + FMath::Abs(Cell.Z - Entry.Z));
			}
		}
		return Distance <= 1 ? -1 : Distance / FMath::Max(1, FMath::Max(FootprintSize.X, FootprintSize.Y) / 4);
	}

	/** Sparse upper ports name one exact adjacent landing domain (normal structure or ground support). */
	bool TryGetSparseHostDestination(const FLayoutVerticalAccessHostOption& Option, FIntVector& OutCell)
	{
		for (const auto Direction : { ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			const FIntVector Cell = Option.UpperCell + FLayoutDirectionUtils::ToCellDelta(Direction);
			if (Option.UpperTraversalPortFaceMask == LayoutFaceDirectionMask(Direction)
				&& Option.RequiredFilledSupportCells.Contains(Cell))
			{
				OutCell = Cell;
				return true;
			}
		}
		return false;
	}

	/** Builds separate normal-module and preserved ground-plane components from one prepared topology. */
	bool BuildNormalDestinationComponents(const FLayoutRegionSolveRequest& Request,
		const TArray<FLayoutPlannedCell>& Cells, TMap<FIntVector, int32>& OutComponents,
		TMap<FIntVector, int32>& OutGroundComponents, FString& OutFailureReason);

	/** Selects authored ascent once per logical transition across all mapped fragments of this region. */
	bool RebuildVerticalAccessIntentsByAuthoredTransition(
		const FLayoutRegionSolveRequest& Request,
		const TMap<FIntVector, int32>& NormalDestinationComponents,
		const FIntPoint& FootprintSize,
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		TArray<FLayoutVerticalAccessHostGroup>& OutHostGroups,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		const bool bPreservesTerrain = Request.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate(
			[](const FLayoutSparsePlacementRuleSolveSnapshot& Rule)
			{
				return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
					|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
			});
		const bool bPreferStructuralDistribution = bPreservesTerrain
			&& !Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable;
		TMap<FIntVector, int32> PlannedCellIndexByCell;
		TSet<FIntVector> EntryCells;
		for (int32 CellIndex = 0; CellIndex < InOutPlannedCells.Num(); ++CellIndex)
		{
			const FLayoutPlannedCell& PlannedCell = InOutPlannedCells[CellIndex];
			PlannedCellIndexByCell.Add(PlannedCell.Cell, CellIndex);
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				EntryCells.Add(PlannedCell.Cell);
			}
		}

		for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				PlannedCell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
					FootprintSize,
					EntryCells,
					PlannedCell.Cell);
			}
		}

		if (Request.ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::None)
		{
			return true;
		}
		const int32 MinimumCount = Request.ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
			? Request.ProfileSnapshot.VerticalAccessCount : Request.ProfileSnapshot.MinVerticalAccessCount;
		const int32 MaximumCount = Request.ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
			? Request.ProfileSnapshot.VerticalAccessCount : Request.ProfileSnapshot.MaxVerticalAccessCount;
		FSolveContext InvocationVariants;
		TSet<FIntPoint> PreviouslySelectedColumns;
		for (int32 TransitionLevel = 0; TransitionLevel < Request.ProfileSnapshot.LevelCount - 1; ++TransitionLevel)
		{
			// Terrain shifts change physical Z, not the authored level that owns this count.
			TArray<FIntVector> TransitionCells;
			for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
			{
				if (!PlannedCell.bIsBridgeCell && ResolvePlannedCellModuleLevel(PlannedCell) == TransitionLevel)
				{
					TransitionCells.Add(PlannedCell.Cell);
				}
			}

			TArray<FIntVector> Candidates;
			// First-failing eligibility filters explain why a host never reaches exact admission.
			int32 NoDestinationCount = 0;
			int32 NoUpperCount = 0;
			int32 UpperBridgeCount = 0;
			int32 EntryCount = 0;
			int32 LateralClearanceCount = 0;
			int32 BoundaryPreferenceCount = 0;
			for (const FIntVector& ComponentCell : TransitionCells)
			{
				const FLayoutPlannedCell& PlannedCell = InOutPlannedCells[PlannedCellIndexByCell.FindChecked(ComponentCell)];
				const int32* UpperCellIndex = PlannedCellIndexByCell.Find(ComponentCell + FIntVector(0, 0, 1));
				if (UpperCellIndex == nullptr
					|| ResolvePlannedCellModuleLevel(InOutPlannedCells[*UpperCellIndex]) != TransitionLevel + 1)
				{
					++NoUpperCount;
				}
				else if (InOutPlannedCells[*UpperCellIndex].bIsBridgeCell)
				{
					++UpperBridgeCount;
				}
				else if (EntryCells.Contains(PlannedCell.Cell))
				{
					++EntryCount;
				}
				else if (!bPreservesTerrain && !HasLateralVerticalAccessClearance(
					PlannedCell.Cell, PlannedCellIndexByCell, InOutPlannedCells))
				{
					++LateralClearanceCount;
				}
				// Sparse provider eligibility belongs to content zones, not a
				// blanket Interior preference. Exact admission still checks every zone.
				else if (!bPreservesTerrain && !DoesTransitionPreferBoundaryVerticalAccess(
					Request.ProfileSnapshot, ResolvePlannedCellModuleLevel(PlannedCell))
					&& IsBoundaryCell(PlannedCell.Cell, FootprintSize))
				{
					++BoundaryPreferenceCount;
				}
				else
				{
					bool bHasDestination = !bPreservesTerrain;
					const FIntVector Upper = PlannedCell.Cell + FIntVector(0, 0, 1);
					for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
						bHasDestination |= NormalDestinationComponents.Contains(Upper + Delta);
					if (bHasDestination) Candidates.Add(PlannedCell.Cell);
					else ++NoDestinationCount;
				}
			}

			// Keep the seeded target as preference; groups above the authored minimum
			// retain an omission choice until a complete structural assignment is proved.
			const int32 RequestedCount = ResolveProfileCount(
				Request.ProfileSnapshot.VerticalAccessCountMode,
				Request.ProfileSnapshot.VerticalAccessCount,
				Request.ProfileSnapshot.MinVerticalAccessCount,
				Request.ProfileSnapshot.MaxVerticalAccessCount,
				Candidates.Num(),
				GetVerticalAccessSelectionSeed(
					FootprintSize,
					HashCombine(Request.Seed, HashCombine(TransitionLevel + 1, 1))));
			if (Candidates.Num() < MinimumCount)
			{
				OutFailureReason = FString::Printf(
					TEXT("Authored VerticalAccess transition level %d requires at least %d cells but only %d direct upper pairs remain across its mapped fragments."),
					TransitionLevel,
					MinimumCount,
					Candidates.Num());
				return false;
			}

			if (MaximumCount <= 0 || Candidates.IsEmpty()) continue;

			Candidates.Sort([&EntryCells, FootprintSize, &Request, bPreferStructuralDistribution](const FIntVector& Left, const FIntVector& Right)
			{
				const auto RouteCost = [&EntryCells, FootprintSize](const FIntVector& Cell)
				{
					int32 EntryDistance = EntryCells.IsEmpty() ? 0 : MAX_int32;
					for (const FIntVector& EntryCell : EntryCells)
					{
						if (EntryCell.Z == Cell.Z)
						{
							EntryDistance = FMath::Min(
								EntryDistance,
								FMath::Abs(EntryCell.X - Cell.X) + FMath::Abs(EntryCell.Y - Cell.Y));
						}
					}
					if (EntryDistance == MAX_int32) EntryDistance = 0;
					const int32 BoundaryDistance = FMath::Min(
						FMath::Min(Cell.X, FootprintSize.X - 1 - Cell.X),
						FMath::Min(Cell.Y, FootprintSize.Y - 1 - Cell.Y));
					return EntryDistance + BoundaryDistance;
				};
				if (!bPreferStructuralDistribution)
				{
					const int32 LeftCost = RouteCost(Left);
					const int32 RightCost = RouteCost(Right);
					if (LeftCost != RightCost) return LeftCost < RightCost;
				}
				const uint32 LeftRank = HashCombineFast(static_cast<uint32>(Request.Seed), GetTypeHash(Left));
				const uint32 RightRank = HashCombineFast(static_cast<uint32>(Request.Seed), GetTypeHash(Right));
				if (LeftRank != RightRank) return LeftRank < RightRank;
				if (Left.Y != Right.Y) return Left.Y < Right.Y;
				return Left.X < Right.X;
			});
			// Admit the complete geometric domain: early entry-distance cells can all
			// lack legal upper endpoints while distant perimeter hosts remain viable.

			TArray<FLayoutVerticalAccessHostOption> FeasibleOptions;
			FString FirstAdmissionFailure;
			TArray<FString> BoundedCleanAdmissionFailures;
			TArray<FString> BoundedConstrainedAdmissionFailures;
			for (const ELayoutVerticalAccessHostTier Tier : {
				ELayoutVerticalAccessHostTier::CleanInterior,
				ELayoutVerticalAccessHostTier::Constrained })
			{
				for (const FIntVector& Candidate : Candidates)
				{
					const bool bClean = !bPreservesTerrain && IsCleanVerticalAccessHostPair(
						Candidate, PlannedCellIndexByCell, InOutPlannedCells);
					if ((Tier == ELayoutVerticalAccessHostTier::CleanInterior) != bClean)
					{
						continue;
					}
					FString AdmissionFailure;
					TArray<FLayoutVerticalAccessHostOption> ExactWitnesses;
					if (!DoesVerticalAccessHostAdmitCandidatePair(
							Request, FootprintSize, InOutPlannedCells, Candidate, AdmissionFailure,
							MaximumCount > 1 ? &Candidates : nullptr, nullptr, &ExactWitnesses, &InvocationVariants))
					{
						if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
						if (FirstAdmissionFailure.IsEmpty())
						{
							FirstAdmissionFailure = AdmissionFailure;
						}
						TArray<FString>& TierFailures =
							Tier == ELayoutVerticalAccessHostTier::CleanInterior
								? BoundedCleanAdmissionFailures
								: BoundedConstrainedAdmissionFailures;
						if (TierFailures.Num() < 6)
						{
							TierFailures.Add(FString::Printf(
								TEXT("%s tier=%s: %s"),
								*Candidate.ToString(),
								*StaticEnum<ELayoutVerticalAccessHostTier>()->GetNameStringByValue(static_cast<int64>(Tier)),
								*AdmissionFailure));
						}
						continue;
					}
					for (FLayoutVerticalAccessHostOption& ExactWitness : ExactWitnesses)
					{
						FIntVector Destination;
						if (bPreservesTerrain && (!TryGetSparseHostDestination(ExactWitness, Destination)
							|| !NormalDestinationComponents.Contains(Destination))) continue;
						ExactWitness.LowerCell = Candidate;
						ExactWitness.UpperCell = Candidate + FIntVector(0, 0, 1);
						ExactWitness.Tier = Tier;
						FeasibleOptions.Add(MoveTemp(ExactWitness));
					}
				}
			}
			TSet<FIntVector> FeasibleRoots;
			for (const FLayoutVerticalAccessHostOption& Option : FeasibleOptions) FeasibleRoots.Add(Option.LowerCell);
			if (FeasibleRoots.Num() < MinimumCount)
			{
				TArray<FString> BoundedAdmissionFailures =
					MoveTemp(BoundedCleanAdmissionFailures);
				BoundedAdmissionFailures.Append(BoundedConstrainedAdmissionFailures);
				FIntVector ComponentMin = TransitionCells.IsEmpty() ? FIntVector::ZeroValue : TransitionCells[0];
				FIntVector ComponentMax = ComponentMin;
				int32 FootprintInteriorCount = 0;
				for (const FIntVector& Cell : TransitionCells)
				{
					ComponentMin.X = FMath::Min(ComponentMin.X, Cell.X);
					ComponentMin.Y = FMath::Min(ComponentMin.Y, Cell.Y);
					ComponentMax.X = FMath::Max(ComponentMax.X, Cell.X);
					ComponentMax.Y = FMath::Max(ComponentMax.Y, Cell.Y);
					ComponentMin.Z = FMath::Min(ComponentMin.Z, Cell.Z);
					ComponentMax.Z = FMath::Max(ComponentMax.Z, Cell.Z);
					FootprintInteriorCount += !IsBoundaryCell(Cell, FootprintSize);
				}
				TArray<FString> ContentDiagnostics;
				int32 VerticalAccessContentCount = 0;
				for (const FLayoutModuleSolveSnapshot& Module : Request.ModuleCatalog.Modules)
				{
					if (!Module.Roles.Contains(ELayoutModuleRole::VerticalAccess)) continue;
					++VerticalAccessContentCount;
					if (ContentDiagnostics.Num() >= 8) continue;
					ContentDiagnostics.Add(FString::Printf(
						TEXT("%s/%s zone=%s levelPolicy=%s"),
						*Module.SourceContentEntryId.ToString().Left(128), *Module.SnapshotId.ToString().Left(128),
						*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(Module.PlacementZone)),
						*StaticEnum<ELayoutLevelPlacementPolicy>()->GetNameStringByValue(static_cast<int64>(Module.LevelPlacementPolicy))));
				}
				OutFailureReason = FString::Printf(
					TEXT("Authored VerticalAccess transition level %d has %d module-compatible host cells after testing %d candidate cells across all mapped fragments. "
						"HostDemand[profile=%s seed=%d footprint=%s preserve=%d countMode=%s exact=%d min=%d max=%d requested=%d scopeCells=%d bounds=[%s]-[%s] footprintInterior=%d] "
						"Filters[noUpper=%d upperBridge=%d entry=%d lateralClearance=%d boundaryPreference=%d noNormalDestination=%d] VAContent=[%s] omittedVAContent=%d. "
						"First rejection: %s\nBounded candidate rejections (up to six per host tier):\n%s"),
					TransitionLevel,
					FeasibleRoots.Num(),
					Candidates.Num(),
					*Request.ProfileSnapshot.SnapshotId.ToString().Left(128), Request.Seed, *FootprintSize.ToString(), bPreservesTerrain,
					*StaticEnum<ELayoutCountConstraintMode>()->GetNameStringByValue(static_cast<int64>(Request.ProfileSnapshot.VerticalAccessCountMode)),
					Request.ProfileSnapshot.VerticalAccessCount, Request.ProfileSnapshot.MinVerticalAccessCount,
					Request.ProfileSnapshot.MaxVerticalAccessCount, RequestedCount, TransitionCells.Num(),
					*ComponentMin.ToString(), *ComponentMax.ToString(), FootprintInteriorCount,
					NoUpperCount, UpperBridgeCount, EntryCount, LateralClearanceCount, BoundaryPreferenceCount, NoDestinationCount,
					*FString::Join(ContentDiagnostics, TEXT(" | ")), VerticalAccessContentCount - ContentDiagnostics.Num(),
					*FirstAdmissionFailure,
					BoundedAdmissionFailures.IsEmpty()
						? TEXT("  <none recorded>")
						: *FString::Join(BoundedAdmissionFailures, TEXT("\n")));
				return false;
			}

			TArray<int32> DestinationComponents;
			if (bPreservesTerrain)
			{
				for (const auto& Option : FeasibleOptions)
				{
					FIntVector Destination;
					if (TryGetSparseHostDestination(Option, Destination))
						DestinationComponents.AddUnique(NormalDestinationComponents.FindChecked(Destination));
				}
				DestinationComponents.Sort();
			}
			const int32 AdmittedMaximumCount = FMath::Min(MaximumCount, FeasibleRoots.Num());
			const int32 CoverageCount = FMath::Min(DestinationComponents.Num(), AdmittedMaximumCount);
			// Component coverage orders alternatives; it must not resample authored demand.
			const int32 AdmittedTargetCount = FMath::Min(RequestedCount, FeasibleRoots.Num());
			TArray<FIntVector> PreferredSeparatedHosts;
			if (!bPreservesTerrain && AdmittedTargetCount > 1)
			{
				const ELayoutVerticalAccessHostTier PreferredTier = FeasibleOptions[0].Tier;
				int32 BestPairDistance = -1;
				for (int32 LeftIndex = 0; LeftIndex < FeasibleOptions.Num(); ++LeftIndex)
				{
					if (FeasibleOptions[LeftIndex].Tier != PreferredTier) continue;
					for (int32 RightIndex = LeftIndex + 1; RightIndex < FeasibleOptions.Num(); ++RightIndex)
					{
						if (FeasibleOptions[RightIndex].Tier != PreferredTier) continue;
						const int32 Distance =
							FMath::Abs(FeasibleOptions[LeftIndex].LowerCell.X - FeasibleOptions[RightIndex].LowerCell.X)
							+ FMath::Abs(FeasibleOptions[LeftIndex].LowerCell.Y - FeasibleOptions[RightIndex].LowerCell.Y);
						if (Distance > BestPairDistance)
						{
							BestPairDistance = Distance;
							PreferredSeparatedHosts = {
								FeasibleOptions[LeftIndex].LowerCell,
								FeasibleOptions[RightIndex].LowerCell};
						}
					}
				}
			}

			for (int32 CandidateOrdinal = 0; CandidateOrdinal < AdmittedMaximumCount; ++CandidateOrdinal)
			{
				FLayoutVerticalAccessHostGroup HostGroup;
				HostGroup.GroupId = FLayoutId(*FString::Printf(
					TEXT("AuthoredVerticalAccess_%d_%d"), TransitionLevel, CandidateOrdinal));
				HostGroup.bAllowOmission = CandidateOrdinal >= MinimumCount;
				HostGroup.bPreferOmission = CandidateOrdinal >= AdmittedTargetCount;
				HostGroup.Options = FeasibleOptions;
				const int32 PreferredComponent = CandidateOrdinal < CoverageCount
					? DestinationComponents[CandidateOrdinal] : INDEX_NONE;
				HostGroup.DeckCell = HostGroup.Options[0].UpperCell;
				HostGroup.FirstRejectedAdmission = FirstAdmissionFailure;
				for (FLayoutVerticalAccessHostOption& Option : HostGroup.Options)
				{
					Option.SeedRank = HashCombineFast(
						GetVerticalAccessHostSeedRank(Request, HostGroup.GroupId, Option.LowerCell),
						HashCombineFast(
							GetTypeHash(Option.LowerModuleSnapshotId),
							bPreservesTerrain
								? HashCombineFast(GetTypeHash(Option.LowerYawRotationSteps), GetTypeHash(Option.UpperTraversalPortFaceMask))
								: GetTypeHash(Option.LowerYawRotationSteps)));
				}
				HostGroup.Options.Sort([&EntryCells, &PreviouslySelectedColumns, FootprintSize, &NormalDestinationComponents, PreferredComponent, bPreferStructuralDistribution](const FLayoutVerticalAccessHostOption& Left, const FLayoutVerticalAccessHostOption& Right)
				{
					if (bPreferStructuralDistribution)
					{
						const int32 LeftBand = GetVerticalAccessHostSeparationBand(Left, EntryCells, PreviouslySelectedColumns, FootprintSize);
						const int32 RightBand = GetVerticalAccessHostSeparationBand(Right, EntryCells, PreviouslySelectedColumns, FootprintSize);
						if (LeftBand != RightBand) return LeftBand > RightBand;
					}
					// Coverage is a preference, not permission to delete otherwise legal
					// count alternatives when this component cannot survive the joint proof.
					if (PreferredComponent != INDEX_NONE)
					{
						FIntVector LeftDestination, RightDestination;
						const bool bLeftPreferred = TryGetSparseHostDestination(Left, LeftDestination)
							&& NormalDestinationComponents.FindRef(LeftDestination) == PreferredComponent;
						const bool bRightPreferred = TryGetSparseHostDestination(Right, RightDestination)
							&& NormalDestinationComponents.FindRef(RightDestination) == PreferredComponent;
						if (bLeftPreferred != bRightPreferred) return bLeftPreferred;
					}
					const auto RouteCost = [&EntryCells, FootprintSize](const FLayoutVerticalAccessHostOption& Option)
					{
						int32 LowerDistance = EntryCells.IsEmpty() ? 0 : MAX_int32;
						for (const FIntVector& EntryCell : EntryCells)
						{
							if (EntryCell.Z == Option.LowerCell.Z)
							{
								LowerDistance = FMath::Min(
									LowerDistance,
									FMath::Abs(EntryCell.X - Option.LowerCell.X)
										+ FMath::Abs(EntryCell.Y - Option.LowerCell.Y));
							}
						}
						if (LowerDistance == MAX_int32)
						{
							LowerDistance = 0;
						}
						const int32 UpperBoundaryDistance = FMath::Min(
							FMath::Min(Option.UpperCell.X, FootprintSize.X - 1 - Option.UpperCell.X),
							FMath::Min(Option.UpperCell.Y, FootprintSize.Y - 1 - Option.UpperCell.Y));
						return LowerDistance + UpperBoundaryDistance;
					};
					if (Left.Tier != Right.Tier) return Left.Tier < Right.Tier;
					// Optional paths must not pull structural hosts toward same-height Entries.
					if (!bPreferStructuralDistribution)
					{
						const int32 LeftCost = RouteCost(Left);
						const int32 RightCost = RouteCost(Right);
						if (LeftCost != RightCost) return LeftCost < RightCost;
					}
					if (Left.SeedRank != Right.SeedRank) return Left.SeedRank < Right.SeedRank;
					if (Left.LowerCell.Z != Right.LowerCell.Z) return Left.LowerCell.Z < Right.LowerCell.Z;
					if (Left.LowerCell.Y != Right.LowerCell.Y) return Left.LowerCell.Y < Right.LowerCell.Y;
					return Left.LowerCell.X < Right.LowerCell.X;
				});
				const FLayoutVerticalAccessHostOption* SelectedOption = nullptr;
				if (PreferredSeparatedHosts.IsValidIndex(CandidateOrdinal))
				{
					SelectedOption = HostGroup.Options.FindByPredicate(
						[&PreferredSeparatedHosts, CandidateOrdinal](const FLayoutVerticalAccessHostOption& Option)
						{
							return Option.LowerCell == PreferredSeparatedHosts[CandidateOrdinal];
						});
				}
				const ELayoutVerticalAccessHostTier PreferredTier = HostGroup.Options[0].Tier;
				for (const FLayoutVerticalAccessHostOption& Option : HostGroup.Options)
				{
					if (SelectedOption != nullptr)
					{
						break;
					}
					if (Option.Tier != PreferredTier)
					{
						continue;
					}
					const FIntPoint Column(Option.LowerCell.X, Option.LowerCell.Y);
					if (PreviouslySelectedColumns.Contains(Column)
						|| PreviouslySelectedColumns.Contains(Column + FIntPoint(1, 0))
						|| PreviouslySelectedColumns.Contains(Column + FIntPoint(-1, 0))
						|| PreviouslySelectedColumns.Contains(Column + FIntPoint(0, 1))
						|| PreviouslySelectedColumns.Contains(Column + FIntPoint(0, -1)))
					{
						continue;
					}
					// Keep the first seeded legal column, not a second coordinate-based ranking.
					SelectedOption = &Option;
				}
				if (SelectedOption == nullptr)
				{
					SelectedOption = &HostGroup.Options[0];
				}
				const FIntVector SelectedHostCell = SelectedOption->LowerCell;
				const int32 SelectedOptionIndex = HostGroup.Options.IndexOfByPredicate(
					[&SelectedHostCell](const FLayoutVerticalAccessHostOption& Option)
					{
						return Option.LowerCell == SelectedHostCell;
					});
				if (SelectedOptionIndex > 0)
				{
					HostGroup.Options.Swap(0, SelectedOptionIndex);
				}
				if (!HostGroup.bPreferOmission)
				{
					InOutPlannedCells[PlannedCellIndexByCell.FindChecked(SelectedHostCell)].Intent =
						ELayoutCellIntent::VerticalAccess;
					PreviouslySelectedColumns.Add(FIntPoint(SelectedHostCell.X, SelectedHostCell.Y));
				}
				OutHostGroups.Add(MoveTemp(HostGroup));
			}
		}
		return true;
	}

	bool ApplyVerticalAccessPlanning(
		const FLayoutRegionSolveRequest& Request,
		const FIntPoint& FootprintSize,
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		TArray<FLayoutVerticalAccessHostGroup>& OutHostGroups,
		FString& OutFailureReason,
		ELayoutSteppedTerrainFinalizationFailureKind* const OutFailureKind)
	{
		OutFailureReason.Reset();
		const bool bUsesSteppedTerrainPlanning =
			ShouldUseSteppedTerrainAwareVerticalAccessPlanning(Request);
		const bool bPreservesTerrain = Request.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate([](const auto& Rule)
		{
			return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
				|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
		});
		TMap<FIntVector, int32> NormalDestinationComponents;
		TMap<FIntVector, int32> GroundDestinationComponents;
		if (bPreservesTerrain && (bUsesSteppedTerrainPlanning || Request.ProfileSnapshot.VerticalAccessCountMode != ELayoutCountConstraintMode::None)
			&& !BuildNormalDestinationComponents(Request, InOutPlannedCells, NormalDestinationComponents,
				GroundDestinationComponents, OutFailureReason))
		{
			return false;
		}
		// Multi-level authored transitions need the same module-compatible retry
		// carrier in flat and stepped modes. Preserve one-level flat supplied intents.
		if ((bUsesSteppedTerrainPlanning || Request.ProfileSnapshot.LevelCount > 1)
			&& !RebuildVerticalAccessIntentsByAuthoredTransition(
				Request,
				NormalDestinationComponents,
				FootprintSize,
				InOutPlannedCells,
				OutHostGroups,
				OutFailureReason))
		{
			return false;
		}
		if (!bUsesSteppedTerrainPlanning)
		{
			return true;
		}

		// Ground-cluster access is a local structural obligation, not optional Entry
		// routing or a count for each provisional upper deck. Ordinary deck offers
		// retain their existing optional policy.
		const bool bOptionalCoverage = !bPreservesTerrain
			&& !Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable;
		TOptional<LayoutSolveExecution::FOptionalImprovementScope> OptionalCoverageScope;
		if (bOptionalCoverage)
		{
			OptionalCoverageScope.Emplace();
		}
		FSolveContext CoverageVariants;

		TMap<FIntVector, int32> PlannedCellIndexByCell;
		TSet<FIntVector> EntryCells;
		TSet<FIntPoint> VerticalAccessColumns;
		TMap<int32, TArray<FIntVector>> EntryCellsByLevel;
		PlannedCellIndexByCell.Reserve(InOutPlannedCells.Num());
		for (int32 PlannedCellIndex = 0; PlannedCellIndex < InOutPlannedCells.Num(); ++PlannedCellIndex)
		{
			const FLayoutPlannedCell& PlannedCell = InOutPlannedCells[PlannedCellIndex];
			PlannedCellIndexByCell.Add(PlannedCell.Cell, PlannedCellIndex);
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				EntryCells.Add(PlannedCell.Cell);
				EntryCellsByLevel.FindOrAdd(PlannedCell.Cell.Z).Add(PlannedCell.Cell);
			}
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				VerticalAccessColumns.Add(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
			}
		}

		// Resolve source levels within one column, rather than scanning the full
		// footprint for every seam endpoint. Physical ground need not be Z=0.
		TMap<FIntPoint, TArray<FIntVector>> NonBridgeCellsByColumn;
		for (const FLayoutPlannedCell& Cell : InOutPlannedCells)
			if (!Cell.bIsBridgeCell)
				NonBridgeCellsByColumn.FindOrAdd(FIntPoint(Cell.Cell.X, Cell.Cell.Y)).Add(Cell.Cell);
		auto FindFirstNonBridgeAtXY = [&NonBridgeCellsByColumn](const FIntVector& Probe) -> FIntVector
		{
			FIntVector BestCell(Probe.X, Probe.Y, -1);
			if (const auto* Column = NonBridgeCellsByColumn.Find(FIntPoint(Probe.X, Probe.Y)))
				for (const FIntVector& Cell : *Column)
					if (Cell.Z >= Probe.Z && (BestCell.Z < 0 || Cell.Z < BestCell.Z)) BestCell = Cell;
			return BestCell;
		};

		TMap<int32, TMap<FIntVector, int32>> CandidateDegreesByLevel;

			// Build stage-index map by XY column from the frozen terrain StageMap
			// and flood-fill same-stage 4-connected columns into components.
			// The StageMap is the authoritative cluster grouping — one VA per
			// distinct (lower-stage, higher-stage) component pair per level.
			TMap<FIntPoint, int32> SupportStageByColumn;
			int32 BaseSupportFloorZ = MAX_int32;
			for (const FLayoutSteppedTerrainSupportSample& Sample : Request.SteppedTerrainSupportMap.SupportSamples)
			{
				BaseSupportFloorZ = FMath::Min(BaseSupportFloorZ, Sample.SnappedSupportFloorZ);
			}
			if (BaseSupportFloorZ != MAX_int32 && Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0)
			{
				for (const FLayoutSteppedTerrainSupportSample& Sample : Request.SteppedTerrainSupportMap.SupportSamples)
				{
					SupportStageByColumn.Add(
						FIntPoint(Sample.LocalCell.X, Sample.LocalCell.Y),
						(Sample.SnappedSupportFloorZ - BaseSupportFloorZ)
							/ Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks);
				}
			}

			TMap<FIntPoint, int32> StageIndexByColumn;
			if (!Request.PrecomputedFrozenTerrainContract.StageMap.IsEmpty())
			{
				StageIndexByColumn.Reserve(Request.PrecomputedFrozenTerrainContract.StageMap.Num());
				for (const FLayoutFrozenTerrainStageCellRecord& Record : Request.PrecomputedFrozenTerrainContract.StageMap)
				{
					const int32* SupportStage = SupportStageByColumn.Find(Record.FootprintCellXY);
					if (SupportStage != nullptr && *SupportStage != Record.TerrainStageIndex)
					{
						OutFailureReason = FString::Printf(
							TEXT("Stepped terrain StageMap mismatch at X=%d Y=%d: contract stage %d, support stage %d."),
							Record.FootprintCellXY.X,
							Record.FootprintCellXY.Y,
							Record.TerrainStageIndex,
							*SupportStage);
						return false;
					}
					StageIndexByColumn.Add(Record.FootprintCellXY, Record.TerrainStageIndex);
				}
				if (!SupportStageByColumn.IsEmpty() && StageIndexByColumn.Num() != SupportStageByColumn.Num())
				{
					OutFailureReason = TEXT("Stepped terrain StageMap does not cover every sampled support column.");
					return false;
				}
			}
			else
			{
				StageIndexByColumn = MoveTemp(SupportStageByColumn);
			}

			// Source terrain shifts identify bridge contacts; physical upper-deck
			// connectivity decides supplemental ascent coverage below.
			TSet<FIntPoint> ShiftedColumns;
			for (const TPair<FIntPoint, int32>& StagePair : StageIndexByColumn)
			{
				if (StagePair.Value != 0)
				{
					ShiftedColumns.Add(StagePair.Key);
				}
			}

			for (const TPair<FIntPoint, int32>& StagePair : StageIndexByColumn)
			{
				const FIntPoint NeighborOffsets[] = { FIntPoint(1, 0), FIntPoint(0, 1) };
				for (const FIntPoint& NeighborOffset : NeighborOffsets)
				{
					const FIntPoint NeighborXY = StagePair.Key + NeighborOffset;
					const int32* NeighborStage = StageIndexByColumn.Find(NeighborXY);
					if (NeighborStage == nullptr || *NeighborStage == StagePair.Value)
					{
						continue;
					}
					if (FMath::Abs(*NeighborStage - StagePair.Value) != 1)
					{
						OutFailureReason = FString::Printf(
							TEXT("Stepped terrain StageMap has non-adjacent stages %d and %d at (%d,%d) and (%d,%d)."),
							StagePair.Value, *NeighborStage, StagePair.Key.X, StagePair.Key.Y, NeighborXY.X, NeighborXY.Y);
						return false;
					}

					// Verify planned cells provide walkable Z coverage
					// for every transition so no required level silently
					// receives zero candidates.
					{
						const int32 LowerStage = FMath::Min(StagePair.Value, *NeighborStage);
						const FIntPoint LowerXY = StagePair.Value == LowerStage ? StagePair.Key : NeighborXY;
						const FIntPoint HigherXY = StagePair.Value == LowerStage ? NeighborXY : StagePair.Key;
						const FIntVector LowerWalkable = FindFirstNonBridgeAtXY(FIntVector(LowerXY.X, LowerXY.Y, 0));
						const FIntVector HigherWalkable = FindFirstNonBridgeAtXY(FIntVector(HigherXY.X, HigherXY.Y, 0));
						if (LowerWalkable.Z < 0 || HigherWalkable.Z < 0 || LowerWalkable.Z >= HigherWalkable.Z)
						{
							if (OutFailureKind != nullptr)
							{
								*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible;
							}
							OutFailureReason = FString::Printf(
								TEXT("Stepped terrain StageMap boundary from stage %d at (%d,%d) to stage %d at (%d,%d) requires walkable cells at Z=%d and Z>=%d but planned cells are missing or misconfigured."),
								LowerStage, LowerXY.X, LowerXY.Y,
								FMath::Max(StagePair.Value, *NeighborStage), HigherXY.X, HigherXY.Y,
								LowerWalkable.Z, LowerWalkable.Z + 1);
							return false;
						}
					}
				}
			}

			auto TryNormalizeSteppedSeam = [&StageIndexByColumn, &FindFirstNonBridgeAtXY](
				const FLayoutSteppedTerrainAdjacencyStep& Step,
				FIntVector& OutLowerWalkable,
				FIntVector& OutHigherWalkable) -> bool
			{
				const FIntPoint FromXY(Step.FromCell.X, Step.FromCell.Y);
				const FIntPoint ToXY(Step.ToCell.X, Step.ToCell.Y);
				const int32* FromStage = StageIndexByColumn.Find(FromXY);
				const int32* ToStage = StageIndexByColumn.Find(ToXY);
				if (FromStage == nullptr || ToStage == nullptr || *FromStage == *ToStage)
				{
					return false;
				}

				const FIntVector FromWalkable = FindFirstNonBridgeAtXY(Step.FromCell);
				const FIntVector ToWalkable = FindFirstNonBridgeAtXY(Step.ToCell);
				if (FromWalkable.Z < 0 || ToWalkable.Z < 0)
				{
					return false;
				}

				const bool bFromIsLower = *FromStage < *ToStage;
				OutLowerWalkable = bFromIsLower ? FromWalkable : ToWalkable;
				OutHigherWalkable = bFromIsLower ? ToWalkable : FromWalkable;
				return OutLowerWalkable.Z < OutHigherWalkable.Z;
			};

			for (const FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep : Request.SteppedTerrainSupportMap.AdjacencySteps)
		{
			if (AdjacencyStep.SnappedLevelDelta <= 0)
			{
				continue;
			}

			// When a column is shifted up, its Z=0 cell is a bridge cell.
			// Walk up to the first non-bridge cell so the adjacency step
			// connects at the actual walkable Z level instead of the
			// enclosure wall.  Lateral neighbours follow the same walk-up.

			FIntVector LowerWalkable;
			FIntVector HigherWalkable;
			if (!TryNormalizeSteppedSeam(
				AdjacencyStep,
				LowerWalkable,
				HigherWalkable))
			{
				continue;
			}

			auto AddCandidateCell =
				[&CandidateDegreesByLevel, &EntryCells, &InOutPlannedCells, &PlannedCellIndexByCell](const int32 PlannedCellIndex)
			{
				const FLayoutPlannedCell& PlannedCell = InOutPlannedCells[PlannedCellIndex];
				if (EntryCells.Contains(PlannedCell.Cell)
					|| !PlannedCellIndexByCell.Contains(PlannedCell.Cell + FIntVector(0, 0, 1))
					|| !HasLateralVerticalAccessClearance(
						PlannedCell.Cell,
						PlannedCellIndexByCell,
						InOutPlannedCells))
				{
					return;
				}

				// Bridge cells are enclosure walls, not walkable VA hosts.
				if (PlannedCell.bIsBridgeCell)
				{
					return;
				}

				if (PlannedCell.Intent != ELayoutCellIntent::Boundary
					&& PlannedCell.Intent != ELayoutCellIntent::Interior
					&& PlannedCell.Intent != ELayoutCellIntent::Core
					&& PlannedCell.Intent != ELayoutCellIntent::VerticalAccess)
				{
					return;
				}

				// Generated seam coverage follows physical bridge interfaces. Perimeter
				// endpoints remain eligible; candidate admission decides module fit.

				TMap<FIntVector, int32>& CandidateDegrees =
					CandidateDegreesByLevel.FindOrAdd(PlannedCell.Cell.Z);
				int32& Degree = CandidateDegrees.FindOrAdd(PlannedCell.Cell);
				++Degree;
			};

			auto AddCandidateCellByCoordinate =
				[&PlannedCellIndexByCell, &AddCandidateCell](const FIntVector& CandidateCell)
			{
				if (const int32* PlannedCellIndex = PlannedCellIndexByCell.Find(CandidateCell))
				{
					AddCandidateCell(*PlannedCellIndex);
				}
			};

			const FIntVector LowerColumnAtTransition = LowerWalkable;
			const FIntVector HigherColumnAtTransition(
				HigherWalkable.X,
				HigherWalkable.Y,
				LowerWalkable.Z);
			AddCandidateCellByCoordinate(LowerColumnAtTransition);
			AddCandidateCellByCoordinate(HigherColumnAtTransition);

			const FIntVector Delta = HigherWalkable - LowerWalkable;
			if (Delta.X != 0)
			{
				AddCandidateCellByCoordinate(LowerColumnAtTransition + FIntVector(0, 1, 0));
				AddCandidateCellByCoordinate(LowerColumnAtTransition + FIntVector(0, -1, 0));
				AddCandidateCellByCoordinate(HigherColumnAtTransition + FIntVector(0, 1, 0));
				AddCandidateCellByCoordinate(HigherColumnAtTransition + FIntVector(0, -1, 0));
			}
			else if (Delta.Y != 0)
			{
				AddCandidateCellByCoordinate(LowerColumnAtTransition + FIntVector(1, 0, 0));
				AddCandidateCellByCoordinate(LowerColumnAtTransition + FIntVector(-1, 0, 0));
				AddCandidateCellByCoordinate(HigherColumnAtTransition + FIntVector(1, 0, 0));
				AddCandidateCellByCoordinate(HigherColumnAtTransition + FIntVector(-1, 0, 0));
			}
		}

		// Sparse coverage follows ground components; ordinary layouts retain deck
		// coverage. Authored ascents and structural wall replacements stay separate.
		const bool bHasSteppedAdjacencyData =
			!Request.SteppedTerrainSupportMap.AdjacencySteps.IsEmpty();
		if (!bHasSteppedAdjacencyData && !bPreservesTerrain)
		{
			return true;
		}

		int32 MaxPlannedZ = 0;
		for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			MaxPlannedZ = FMath::Max(MaxPlannedZ, PlannedCell.Cell.Z);
		}

		for (int32 TransitionLevel = 0; TransitionLevel < MaxPlannedZ; ++TransitionLevel)
		{
			TSet<FIntVector> UpperDeckCells;
			TSet<FIntVector> TransitionBridgeSeedCells;
			for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
			{
				if (PlannedCell.Cell.Z != TransitionLevel + 1
					|| (bPreservesTerrain && !GroundDestinationComponents.Contains(PlannedCell.Cell)))
				{
					continue;
				}
				UpperDeckCells.Add(PlannedCell.Cell);
				if (bPreservesTerrain)
				{
					// A raised ground component needs access where it borders lower ground,
					// regardless of whether the adapter generated an upper bridge offer.
					for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
						if (GroundDestinationComponents.Contains(PlannedCell.Cell + Delta - FIntVector(0,0,1)))
							TransitionBridgeSeedCells.Add(PlannedCell.Cell);
					continue;
				}
				if (!PlannedCell.bIsBridgeCell)
				{
					continue;
				}

				const int32* LowerCellIndex = PlannedCellIndexByCell.Find(PlannedCell.Cell - FIntVector(0, 0, 1));
				if (LowerCellIndex == nullptr)
				{
					continue;
				}

				for (const FIntPoint& Delta : {
					FIntPoint(-1, -1), FIntPoint(0, -1), FIntPoint(1, -1),
					FIntPoint(-1, 0),                     FIntPoint(1, 0),
					FIntPoint(-1, 1),  FIntPoint(0, 1),  FIntPoint(1, 1) })
				{
					if (ShiftedColumns.Contains(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y) + Delta))
					{
						TransitionBridgeSeedCells.Add(PlannedCell.Cell);
						break;
					}
				}
			}
			if (TransitionBridgeSeedCells.IsEmpty())
			{
				continue;
			}

			// Shared root/continuation rule: seed terrain-transition landings, then
			// flood through every physically connected upper-deck cell. Sparse paths
			// naturally limit reachability; rectangles retain their existing behavior.
			TMap<FIntVector, int32> UpperComponentByCell;
			if (bPreservesTerrain)
			{
				for (const FIntVector& Cell : UpperDeckCells)
					UpperComponentByCell.Add(Cell, GroundDestinationComponents.FindChecked(Cell));
			}
			int32 NextComponentId = 0;
			for (const FIntVector& SeedCell : TransitionBridgeSeedCells)
			{
				if (UpperComponentByCell.Contains(SeedCell))
				{
					continue;
				}

				TArray<FIntVector> PendingCells = { SeedCell };
				UpperComponentByCell.Add(SeedCell, NextComponentId);
				for (int32 PendingIndex = 0; PendingIndex < PendingCells.Num(); ++PendingIndex)
				{
					const FIntVector CurrentCell = PendingCells[PendingIndex];
					for (const FIntVector& LateralDelta : {
						FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
						FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
					{
						const FIntVector NeighborCell = CurrentCell + LateralDelta;
						if (UpperDeckCells.Contains(NeighborCell)
							&& !UpperComponentByCell.Contains(NeighborCell))
						{
							UpperComponentByCell.Add(NeighborCell, NextComponentId);
							PendingCells.Add(NeighborCell);
						}
					}
					for (const FIntVector& DiagonalDelta : {
						FIntVector(1, 1, 0), FIntVector(1, -1, 0),
						FIntVector(-1, 1, 0), FIntVector(-1, -1, 0) })
					{
						const FIntVector NeighborCell = CurrentCell + DiagonalDelta;
						const int32* CurrentLowerIndex = PlannedCellIndexByCell.Find(CurrentCell - FIntVector(0, 0, 1));
						const int32* NeighborLowerIndex = PlannedCellIndexByCell.Find(NeighborCell - FIntVector(0, 0, 1));
						if (UpperDeckCells.Contains(NeighborCell)
							&& !UpperComponentByCell.Contains(NeighborCell)
							&& CurrentLowerIndex != nullptr
							&& NeighborLowerIndex != nullptr
							&& InOutPlannedCells[*CurrentLowerIndex].TerrainSeamFaceMask != 0
							&& InOutPlannedCells[*NeighborLowerIndex].TerrainSeamFaceMask != 0)
						{
							UpperComponentByCell.Add(NeighborCell, NextComponentId);
							PendingCells.Add(NeighborCell);
						}
					}
				}
				++NextComponentId;
			}

			const auto HostTouchesComponent = [&UpperComponentByCell, bPreservesTerrain](const FIntVector& Lower, int32 Component)
			{
				const FIntVector Upper = Lower + FIntVector(0, 0, 1);
				if (const int32* Existing = UpperComponentByCell.Find(Upper))
					if (*Existing == Component) return true;
				if (bPreservesTerrain)
				{
					for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
						if (const int32* Existing = UpperComponentByCell.Find(Upper + Delta))
							if (*Existing == Component) return true;
				}
				return false;
			};
			// Distinct lower islands sharing an upper plateau need distinct access.
			// Ordinary deck coverage has no lower ground component and keeps its old key.
			TSet<FIntPoint> RelevantComponentPairs;
			for (const FIntVector& UpperCell : TransitionBridgeSeedCells)
			{
				const int32 UpperComponent = UpperComponentByCell.FindChecked(UpperCell);
				if (!bPreservesTerrain)
				{
					RelevantComponentPairs.Add(FIntPoint(INDEX_NONE, UpperComponent));
					continue;
				}
				for (const FIntVector Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
				{
					if (const int32* LowerComponent = GroundDestinationComponents.Find(UpperCell + Delta - FIntVector(0,0,1)))
						RelevantComponentPairs.Add(FIntPoint(*LowerComponent, UpperComponent));
				}
			}
			TArray<FIntPoint> ClusterPairs = RelevantComponentPairs.Array();
			ClusterPairs.Sort([](const FIntPoint& Left, const FIntPoint& Right)
			{
				return Left.Y != Right.Y ? Left.Y < Right.Y : Left.X < Right.X;
			});

			TMap<FIntVector, int32>& CandidateDegrees =
				CandidateDegreesByLevel.FindOrAdd(TransitionLevel);
			for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
			{
				if (PlannedCell.Cell.Z != TransitionLevel
					|| (!bPreservesTerrain && VerticalAccessColumns.Contains(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y)))
					|| EntryCells.Contains(PlannedCell.Cell)
					|| !PlannedCellIndexByCell.Contains(PlannedCell.Cell + FIntVector(0, 0, 1))
					|| !HasLateralVerticalAccessClearance(
						PlannedCell.Cell,
						PlannedCellIndexByCell,
						InOutPlannedCells)
					|| PlannedCell.bIsBridgeCell
					|| (PlannedCell.Intent != ELayoutCellIntent::Boundary
						&& PlannedCell.Intent != ELayoutCellIntent::Interior
						&& PlannedCell.Intent != ELayoutCellIntent::Core
						&& PlannedCell.Intent != ELayoutCellIntent::VerticalAccess))
				{
					continue;
				}
				CandidateDegrees.FindOrAdd(PlannedCell.Cell) = FMath::Max(
					1,
					CandidateDegrees.FindRef(PlannedCell.Cell));
			}

			TArray<FIntVector> CandidateCells;
			CandidateDegrees.GetKeys(CandidateCells);
			auto TouchesTerrainSeam = [&PlannedCellIndexByCell, &InOutPlannedCells](const FIntVector& Cell)
			{
				const int32* LowerIndex = PlannedCellIndexByCell.Find(Cell);
				const int32* UpperIndex = PlannedCellIndexByCell.Find(Cell + FIntVector(0, 0, 1));
				return (LowerIndex != nullptr && InOutPlannedCells[*LowerIndex].TerrainSeamFaceMask != 0)
					|| (UpperIndex != nullptr && InOutPlannedCells[*UpperIndex].TerrainSeamFaceMask != 0);
			};
			CandidateCells.Sort([&CandidateDegrees, &FootprintSize, &TouchesTerrainSeam](const FIntVector& Left, const FIntVector& Right)
			{
				const bool bLeftTouchesTerrainSeam = TouchesTerrainSeam(Left);
				const bool bRightTouchesTerrainSeam = TouchesTerrainSeam(Right);
				if (bLeftTouchesTerrainSeam != bRightTouchesTerrainSeam)
				{
					return !bLeftTouchesTerrainSeam;
				}
				const bool bLeftBoundary = IsBoundaryCell(Left, FootprintSize);
				const bool bRightBoundary = IsBoundaryCell(Right, FootprintSize);
				if (bLeftBoundary != bRightBoundary)
				{
					return !bLeftBoundary;
				}

				const int32 LeftDegree = CandidateDegrees.FindChecked(Left);
				const int32 RightDegree = CandidateDegrees.FindChecked(Right);
				if (LeftDegree != RightDegree)
				{
					return LeftDegree > RightDegree;
				}

				const float CenterX = static_cast<float>(FootprintSize.X - 1) * 0.5f;
				const float CenterY = static_cast<float>(FootprintSize.Y - 1) * 0.5f;
				const float LeftDistance =
					FMath::Abs(static_cast<float>(Left.X) - CenterX)
					+ FMath::Abs(static_cast<float>(Left.Y) - CenterY);
				const float RightDistance =
					FMath::Abs(static_cast<float>(Right.X) - CenterX)
					+ FMath::Abs(static_cast<float>(Right.Y) - CenterY);
				if (!FMath::IsNearlyEqual(LeftDistance, RightDistance))
				{
					return LeftDistance < RightDistance;
				}
				if (Left.Y != Right.Y)
				{
					return Left.Y < Right.Y;
				}
				return Left.X < Right.X;
			});

			// Authored ascents survive terrain translation. Supplemental ascents only
			// cover generated decks that no authored ascent already reaches.
			TSet<FIntVector> SelectedVA;
			for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
			{
				if (PlannedCell.Cell.Z == TransitionLevel
					&& PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
					&& !PlannedCell.bIsBridgeCell
					&& UpperDeckCells.Contains(PlannedCell.Cell + FIntVector(0, 0, 1)))
				{
					SelectedVA.Add(PlannedCell.Cell);
				}
			}

			for (const FIntPoint& ComponentPair : ClusterPairs)
			{
				const int32 ComponentId = ComponentPair.Y;
				const auto IsLowerComponent = [&](const FIntVector& Cell)
				{
					const int32* LowerComponent = GroundDestinationComponents.Find(Cell);
					return !bPreservesTerrain || (LowerComponent != nullptr && *LowerComponent == ComponentPair.X);
				};
				// Credit an existing mandatory group only if every retained alternative
				// proves this pair. A provisional first option is not coverage authority.
				bool bAlreadyCovered = bPreservesTerrain && OutHostGroups.ContainsByPredicate(
					[&](const FLayoutVerticalAccessHostGroup& Group)
					{
						if (Group.bAllowOmission || Group.Options.IsEmpty()) return false;
						for (const FLayoutVerticalAccessHostOption& Option : Group.Options)
						{
							FIntVector Destination;
							if (!IsLowerComponent(Option.LowerCell) || Option.LowerTraversalPortFaceMask == 0
								|| !TryGetSparseHostDestination(Option, Destination)) return false;
							const int32* UpperComponent = UpperComponentByCell.Find(Destination);
							if (UpperComponent == nullptr || *UpperComponent != ComponentId) return false;
						}
						return true;
					});
				for (const FIntVector& ExistingVA : SelectedVA)
				{
					if (bPreservesTerrain) break;
					const int32* ExistingComponent = UpperComponentByCell.Find(
						ExistingVA + FIntVector(0, 0, 1));
					if (ExistingComponent != nullptr && *ExistingComponent == ComponentId)
					{
						bAlreadyCovered = true;
						break;
					}
				}
				if (bAlreadyCovered)
				{
					continue;
				}

				TArray<FIntVector> ComponentCandidates;
				for (const FIntVector& Candidate : CandidateCells)
				{
					const int32* CandidateIndex = PlannedCellIndexByCell.Find(Candidate);
					if (CandidateIndex == nullptr
						|| InOutPlannedCells[*CandidateIndex].bIsBridgeCell
						|| !IsLowerComponent(Candidate))
					{
						continue;
					}

					if (HostTouchesComponent(Candidate, ComponentId))
					{
						ComponentCandidates.Add(Candidate);
					}
				}
				for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
				{
					if (PlannedCell.Cell.Z != TransitionLevel
						|| !IsLowerComponent(PlannedCell.Cell)
						|| (!bPreservesTerrain && VerticalAccessColumns.Contains(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y)))
						|| EntryCells.Contains(PlannedCell.Cell)
						|| PlannedCell.bIsBridgeCell
						|| !PlannedCellIndexByCell.Contains(PlannedCell.Cell + FIntVector(0, 0, 1))
						|| (PlannedCell.Intent != ELayoutCellIntent::Boundary
							&& PlannedCell.Intent != ELayoutCellIntent::Interior
							&& PlannedCell.Intent != ELayoutCellIntent::Core
							&& PlannedCell.Intent != ELayoutCellIntent::VerticalAccess))
					{
						continue;
					}

					if (HostTouchesComponent(PlannedCell.Cell, ComponentId))
					{
						ComponentCandidates.AddUnique(PlannedCell.Cell);
					}
				}
				if (ComponentCandidates.IsEmpty())
				{
					if (bOptionalCoverage)
					{
						continue;
					}
					if (OutFailureKind != nullptr)
					{
						*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible;
					}
					OutFailureReason = FString::Printf(
						TEXT("Stepped terrain access component %d at level Z=%d has no paired VerticalAccess candidate at Z=%d."),
						ComponentId,
						TransitionLevel + 1,
						TransitionLevel);
					return false;
				}

				FLayoutVerticalAccessHostGroup HostGroup;
				HostGroup.GroupId = bPreservesTerrain
					? FLayoutId(*FString::Printf(TEXT("GeneratedGroundVerticalAccess_%d_%d_%d"), TransitionLevel, ComponentPair.X, ComponentId))
					: FLayoutId(*FString::Printf(TEXT("GeneratedDeckVerticalAccess_%d_%d"), TransitionLevel, ComponentId));
				HostGroup.bAllowOmission = bOptionalCoverage;
				HostGroup.bIsSupplemental = true;
				FString LastAdmissionFailure;
				TArray<FString> AdmissionFailures;
				for (const ELayoutVerticalAccessHostTier Tier : {
					ELayoutVerticalAccessHostTier::CleanInterior,
					ELayoutVerticalAccessHostTier::Constrained })
				{
					for (const FIntVector& Candidate : ComponentCandidates)
					{
						if (LayoutSolveExecution::ShouldStop())
						{
							break;
						}
						const FIntVector UpperCell = Candidate + FIntVector(0, 0, 1);
						const int32* CandidateIndex = PlannedCellIndexByCell.Find(Candidate);
						if (CandidateIndex == nullptr || InOutPlannedCells[*CandidateIndex].bIsBridgeCell)
						{
							continue;
						}
						const ELayoutVerticalAccessHostTier CandidateTier =
							IsCleanVerticalAccessHostPair(Candidate, PlannedCellIndexByCell, InOutPlannedCells)
								? ELayoutVerticalAccessHostTier::CleanInterior
								: ELayoutVerticalAccessHostTier::Constrained;
						if (CandidateTier != Tier)
						{
							continue;
						}
						FString AdmissionFailure;
						TArray<FLayoutVerticalAccessHostOption> ExactWitnesses;
						if (!DoesVerticalAccessHostAdmitCandidatePair(
								Request, FootprintSize, InOutPlannedCells, Candidate, AdmissionFailure,
								nullptr, nullptr, &ExactWitnesses, &CoverageVariants))
						{
							if (LastAdmissionFailure.IsEmpty())
							{
								LastAdmissionFailure = AdmissionFailure;
							}
							if (AdmissionFailures.Num() < 8)
							{
								AdmissionFailures.AddUnique(FString::Printf(
									TEXT("host=%s tier=%d: %s"),
									*Candidate.ToString(),
									static_cast<int32>(Tier),
									*AdmissionFailure));
							}
							continue;
						}
						for (FLayoutVerticalAccessHostOption& ExactWitness : ExactWitnesses)
						{
							FIntVector Destination;
							if (bPreservesTerrain && (ExactWitness.LowerTraversalPortFaceMask == 0
								|| !TryGetSparseHostDestination(ExactWitness, Destination)
								|| !UpperComponentByCell.Contains(Destination)
								|| UpperComponentByCell.FindChecked(Destination) != ComponentId)) continue;
							ExactWitness.LowerCell = Candidate;
							ExactWitness.UpperCell = UpperCell;
							ExactWitness.Tier = Tier;
							ExactWitness.SeedRank = HashCombineFast(
								GetVerticalAccessHostSeedRank(Request, HostGroup.GroupId, Candidate),
								HashCombineFast(
									GetTypeHash(ExactWitness.LowerModuleSnapshotId),
									GetTypeHash(ExactWitness.LowerYawRotationSteps)));
							HostGroup.Options.Add(MoveTemp(ExactWitness));
						}
					}
					if (LayoutSolveExecution::ShouldStop())
					{
						break;
					}
				}
				const bool bCoverageStopped = LayoutSolveExecution::ShouldStop();
				if (bCoverageStopped)
				{
					OptionalCoverageScope.Reset();
					// Keep admitted offers on a soft stop, but never mask cancellation or
					// the owning deadline/work limit by accepting optional omissions.
					if (!LayoutSolveExecution::Checkpoint(OutFailureReason))
					{
						return false;
					}
					if (HostGroup.Options.IsEmpty())
					{
						return true;
					}
				}
				if (HostGroup.Options.IsEmpty())
				{
					if (bOptionalCoverage)
					{
						continue;
					}
					if (OutFailureKind != nullptr)
					{
						*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible;
					}
					OutFailureReason = FString::Printf(
						TEXT("Terrain access component %d at Z=%d has no module-compatible VerticalAccess host. %s"),
						ComponentId,
						TransitionLevel + 1,
					AdmissionFailures.IsEmpty()
						? *LastAdmissionFailure
						: *FString::Join(AdmissionFailures, TEXT(" | ")));
					return false;
				}
				// Local terrain policy: avoid seam hosts when a module-admitted deck host exists.
				const bool bHasNonSeamHost = HostGroup.Options.ContainsByPredicate(
					[&TouchesTerrainSeam](const FLayoutVerticalAccessHostOption& Option)
					{
						return !TouchesTerrainSeam(Option.LowerCell);
					});
				if (bHasNonSeamHost && !bPreservesTerrain)
				{
					HostGroup.Options.RemoveAll([&TouchesTerrainSeam](const FLayoutVerticalAccessHostOption& Option)
					{
						return TouchesTerrainSeam(Option.LowerCell);
					});
				}
				HostGroup.Options.Sort([bPreservesTerrain, &EntryCells, &VerticalAccessColumns, &Request](const FLayoutVerticalAccessHostOption& Left, const FLayoutVerticalAccessHostOption& Right)
				{
					// A provisional authored choice may move during joint proof. Prefer unused
					// columns without deleting valid ground alternatives behind that marker.
					if (bPreservesTerrain)
					{
						const int32 LeftBand = GetVerticalAccessHostSeparationBand(Left, EntryCells, VerticalAccessColumns, Request.FootprintSize);
						const int32 RightBand = GetVerticalAccessHostSeparationBand(Right, EntryCells, VerticalAccessColumns, Request.FootprintSize);
						if (LeftBand != RightBand) return LeftBand > RightBand;
					}
					if (Left.Tier != Right.Tier) return Left.Tier < Right.Tier;
					if (Left.SeedRank != Right.SeedRank) return Left.SeedRank < Right.SeedRank;
					if (Left.LowerCell.Y != Right.LowerCell.Y) return Left.LowerCell.Y < Right.LowerCell.Y;
					return Left.LowerCell.X < Right.LowerCell.X;
				});
				HostGroup.DeckCell = HostGroup.Options[0].UpperCell;
				HostGroup.FirstRejectedAdmission = AdmissionFailures.IsEmpty()
					? LastAdmissionFailure
					: AdmissionFailures[0];
				SelectedVA.Add(HostGroup.Options[0].LowerCell);
				InOutPlannedCells[PlannedCellIndexByCell.FindChecked(HostGroup.Options[0].LowerCell)].Intent =
					ELayoutCellIntent::VerticalAccess;
				VerticalAccessColumns.Add(FIntPoint(
					HostGroup.Options[0].LowerCell.X,
					HostGroup.Options[0].LowerCell.Y));
				OutHostGroups.Add(MoveTemp(HostGroup));
				if (bCoverageStopped)
				{
					return true;
				}
			}

		}

		return true;
	}

	void RebuildTopPlannedLevelByXY(FSolveContext& Context);
	bool CompileReservedOpenSpaceRules(FSolveContext& Context);
	void BuildOrientedVariants(FSolveContext& Context);
	TArray<FSolveCandidate> BuildOrderedCandidates(
		FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutCellIntent Intent);
	void BuildInitialDomains(FSolveContext& Context);
	bool ValidatePreparedInitialDomainsNonEmpty(FSolveContext& Context);
	void InitializeIndexedCandidateUniverse(FSolveContext& Context);
	void BuildIndexedCompatibilityRows(FSolveContext& Context);
	bool PrepareSolveContextThroughRouteDomainStageImpl(FSolveContext& Context, bool bReusePreparedVariants = false);
	void FinalizePreparedSolveContextForIndexedSearchImpl(FSolveContext& Context);
	bool PromoteRouteTerrainSeamCellsToEntries(FSolveContext& Context);
	bool DoesReservedOpenMaskKeepStaticDomains(
		const FSolveContext& SourceContext,
		const TSet<FIntVector>& ProspectiveReservedCells,
		const bool bRequirePreparedPairSupport,
		FIntVector& OutUnsupportedCell,
		FString& OutFailureReason);
	bool FinalizeAuthoredFlatEntries(FSolveContext& Context);
	bool ValidateAuthoredFlatVerticalAccessCount(FSolveContext& Context);

	bool DoesCellMatchLevelPlacementPolicy(
		const FIntVector& Cell,
		const int32 ModuleLevel,
		const TMap<FIntPoint, int32>& TopPlannedLevelByXY,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel);

	void BuildPlan(FSolveContext& Context)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_PlanBuild, STAT_PorismLayout_PlanBuild);
		Context.Result.PlannedCells.Reset();
		Context.PlannedCellIntents.Reset();
		Context.PlacementZonesByCell.Reset();
		Context.TerrainSeamFaceMasksByCell.Reset();
		Context.ModuleLevelByCell.Reset();
		Context.SteppedTraversalDeckCells.Reset();
		Context.SolveOrder.Reset();
		Context.RouteConstraintIndexByCell.Reset();
		Context.ReservationFlagsByCell.Reset();
		Context.ReservedOpenCells.Reset();
		Context.Result.RouteConstraints.Reset();
		Context.Result.CompiledReservations.Reset();

		const FLayoutProfileSolveSnapshot& ProfileSnapshot = GetEffectiveProfileSnapshot(Context);
		if (Context.FootprintSize == FIntPoint::ZeroValue)
		{
			FRandomStream Random(Context.Seed);
			Context.FootprintSize.X = Random.RandRange(ProfileSnapshot.MinimumFootprintInCells.X, ProfileSnapshot.MaximumFootprintInCells.X);
			Context.FootprintSize.Y = Random.RandRange(ProfileSnapshot.MinimumFootprintInCells.Y, ProfileSnapshot.MaximumFootprintInCells.Y);
		}

		TSet<FIntVector> EntryCells;
		if (!Context.QualifiedEntryCells.IsEmpty())
		{
			// Prewarm pre-qualified entry cells — use them directly.
			for (const FIntVector& QualifiedCell : Context.QualifiedEntryCells)
			{
				EntryCells.Add(QualifiedCell);
			}
			UE_LOG(LogTemp, Display,
				TEXT("[LayoutPipeline] Using %d pre-qualified entry cells from prewarm."),
				EntryCells.Num());
		}
		else
		{
			TArray<FIntVector> BuiltEntryCells;
			FString EntryFailureReason;
			if (!TryBuildEntryCells(ProfileSnapshot, Context.ModuleSnapshots, Context.FootprintSize, Context.Seed, BuiltEntryCells, EntryFailureReason))
			{
				Context.Result.FailureReason = EntryFailureReason;
				return;
			}
			for (const FIntVector& EntryCell : BuiltEntryCells)
			{
				EntryCells.Add(EntryCell);
			}

			// Exclude non-walkable boundary cells from entry candidacy based on terrain traversability verdicts.
			if (!Context.TerrainCellContracts.IsEmpty())
			{
				TMap<FIntPoint, bool> WalkableXY;
				for (const FLayoutTerrainCellContractRecord& Contract : Context.TerrainCellContracts)
				{
					WalkableXY.Add(FIntPoint(Contract.Cell.X, Contract.Cell.Y), Contract.bEntryWalkable);
				}
				for (auto It = EntryCells.CreateIterator(); It; ++It)
				{
					if (const bool* bWalkable = WalkableXY.Find(FIntPoint(It->X, It->Y)))
					{
						if (!*bWalkable)
						{
							const FIntVector ExcludedCell = *It;
							It.RemoveCurrent();
							UE_LOG(LogTemp, Display,
								TEXT("[LayoutPipeline] Entry cell %s excluded: terrain traversability is not walkable."),
								*ExcludedCell.ToString());
						}
					}
				}
			}
		}

		TSet<FIntVector> VerticalAccessCells;
		TSet<FIntPoint> PreviousVerticalAccessColumns;
		const int32 TransitionLevelCount = FMath::Max(0, ProfileSnapshot.LevelCount - 1);
		for (int32 TransitionLevel = 0; TransitionLevel < TransitionLevelCount; ++TransitionLevel)
		{
			const TSet<FIntVector> TransitionVerticalAccessCells = BuildVerticalAccessCells(
				Context.ModuleSnapshots,
				ProfileSnapshot,
				Context.FootprintSize,
				EntryCells,
				PreviousVerticalAccessColumns,
				TransitionLevel,
				HashCombine(Context.Seed, TransitionLevel + 1));

			for (const FIntVector& VerticalAccessCell : TransitionVerticalAccessCells)
			{
				VerticalAccessCells.Add(VerticalAccessCell);
			}

			PreviousVerticalAccessColumns.Reset();
			for (const FIntVector& VerticalAccessCell : TransitionVerticalAccessCells)
			{
				PreviousVerticalAccessColumns.Add(FIntPoint(VerticalAccessCell.X, VerticalAccessCell.Y));
			}
		}

		const int32 RequestedVerticalAccessCount = ResolveProfileCount(
			ProfileSnapshot.VerticalAccessCountMode,
			ProfileSnapshot.VerticalAccessCount,
			ProfileSnapshot.MinVerticalAccessCount,
			ProfileSnapshot.MaxVerticalAccessCount,
			MAX_int32,
			GetVerticalAccessSelectionSeed(Context.FootprintSize, Context.Seed));
		if (RequestedVerticalAccessCount * TransitionLevelCount > VerticalAccessCells.Num())
		{
			FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
			Message.Severity = ELayoutValidationSeverity::Warning;
			Message.Message = FString::Printf(
				TEXT("Requested %d vertical-access cells per transition across %d transitions, requiring %d total reserved transition cells, but the footprint only reserved %d."),
				RequestedVerticalAccessCount,
				TransitionLevelCount,
				RequestedVerticalAccessCount * TransitionLevelCount,
				VerticalAccessCells.Num());
		}

		for (int32 Z = 0; Z < ProfileSnapshot.LevelCount; ++Z)
		{
			for (int32 Y = 0; Y < Context.FootprintSize.Y; ++Y)
			{
				for (int32 X = 0; X < Context.FootprintSize.X; ++X)
				{
					const FIntVector Cell(X, Y, Z);
					const bool bVerticalAccessRelatedCell = VerticalAccessCells.Contains(Cell)
						|| VerticalAccessCells.Contains(Cell - FIntVector(0, 0, 1));
					if (GetLevelFillModeForCell(ProfileSnapshot, Cell) == ELayoutLevelFillMode::BoundaryOnly
						&& !IsBoundaryCell(Cell, Context.FootprintSize)
						&& !bVerticalAccessRelatedCell)
					{
						continue;
					}

					const ELayoutCellIntent Intent = DetermineIntent(ProfileSnapshot, Context.FootprintSize, EntryCells, VerticalAccessCells, Cell);
					Context.PlannedCellIntents.Add(Cell, Intent);

					FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
					PlannedCell.Cell = Cell;
					PlannedCell.Intent = Intent;
					PlannedCell.EntryOrigin = Intent == ELayoutCellIntent::Entry
						? ELayoutEntryOrigin::AuthoredBoundary
						: ELayoutEntryOrigin::None;
					PlannedCell.ModuleLevelIndex = Cell.Z;
					Context.ModuleLevelByCell.Add(Cell, Cell.Z);
				}
			}
		}

		// Authored flat plans have no bridge/shift topology. Keep this empty so ordinary
		// Boundary shells never acquire stepped-deck route eligibility.
		Context.SteppedTraversalDeckCells.Reset();
		if (!FinalizeAuthoredFlatEntries(Context)
			|| !CompileReservedOpenSpaceRules(Context)
			|| !ValidateAuthoredFlatVerticalAccessCount(Context))
		{
			return;
		}
		TSet<FIntVector> UniquePlannedCells;
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			const bool bDuplicateCell = UniquePlannedCells.Contains(PlannedCell.Cell);
			UniquePlannedCells.Add(PlannedCell.Cell);
			if (bDuplicateCell || !Context.PlannedCellIntents.Contains(PlannedCell.Cell))
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Finalized plan lost its intent carrier at %s after reserved-open compilation. PlannedCells=%d Intents=%d."),
					*PlannedCell.Cell.ToString(),
					Context.Result.PlannedCells.Num(),
					Context.PlannedCellIntents.Num());
				return;
			}
		}
		RebuildTopPlannedLevelByXY(Context);
		Context.SolveOrder.Reserve(Context.Result.PlannedCells.Num());
		TMap<FIntVector, int32> FinalIntentPriorityByCell;
		FinalIntentPriorityByCell.Reserve(Context.Result.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			Context.SolveOrder.Add(PlannedCell.Cell);
			FinalIntentPriorityByCell.Add(
				PlannedCell.Cell,
				GetIntentPriority(PlannedCell.Intent));
		}

		Context.SolveOrder.Sort([&FinalIntentPriorityByCell](const FIntVector& Left, const FIntVector& Right)
		{
			if (Left.Z != Right.Z)
			{
				return Left.Z < Right.Z;
			}

			const int32 LeftIntentPriority = FinalIntentPriorityByCell.FindRef(Left);
			const int32 RightIntentPriority = FinalIntentPriorityByCell.FindRef(Right);
			if (LeftIntentPriority != RightIntentPriority)
			{
				return LeftIntentPriority < RightIntentPriority;
			}

			if (Left.Y != Right.Y)
			{
				return Left.Y < Right.Y;
			}

			return Left.X < Right.X;
		});

		Context.Result.Seed = Context.Seed;
		Context.Result.FootprintSize = Context.FootprintSize;
	}

	/** Derives local placement zones from final same-Z spatial topology while preserving explicit intent. */
	void CompileFinalSurfaceTopology(
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		const TMap<FIntVector, uint8>* ExternalPlannedNeighborFaceMasks = nullptr)
	{
		TSet<FIntVector> PlannedCellSet;
		PlannedCellSet.Reserve(InOutPlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			PlannedCellSet.Add(PlannedCell.Cell);
		}

		for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			uint8 MissingLateralFaceMask = 0;
			for (const ELayoutFaceDirection Direction : {
				ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
			{
				const FIntVector NeighborCell =
					PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				const bool bHasExternalPlannedNeighbor =
					ExternalPlannedNeighborFaceMasks != nullptr
					&& DoesFaceMaskContainDirection(
						ExternalPlannedNeighborFaceMasks->FindRef(PlannedCell.Cell),
						Direction);
				if (!PlannedCellSet.Contains(NeighborCell) && !bHasExternalPlannedNeighbor)
				{
					MissingLateralFaceMask |= LayoutFaceDirectionMask(Direction);
				}
			}

			PlannedCell.PlacementZone = ResolveLayoutPlacementZoneFromLateralFaceMask(
				MissingLateralFaceMask | PlannedCell.TerrainSeamFaceMask);

		}
	}

	/** Builds one indexed finalized-cell view without duplicating planned-cell metadata. */
	bool TryBuildFinalizedCellView(
		FSolveContext& Context,
		FString& OutFailureReason)
	{
		Context.FinalizedCellsByPhysicalCell.Reset();
		Context.OwningTopologyCellsByPhysicalCell.Reset();
		Context.ReservedOpenReservationIndexByCell.Reset();
		Context.RemovedFinalizedCells.Reset();
		OutFailureReason.Reset();

		TMap<FIntPoint, int32> TerrainStageByXY;
		TSet<FIntVector> AuthoredSourceCells;
		TSet<FIntVector> ActiveCells;
		if (Context.FrozenTerrainContract != nullptr)
		{
			for (const FLayoutFrozenTerrainStageCellRecord& Stage : Context.FrozenTerrainContract->StageMap)
			{
				if (TerrainStageByXY.Contains(Stage.FootprintCellXY))
				{
					OutFailureReason = FString::Printf(
						TEXT("Finalized-cell view rejected duplicate frozen terrain-stage column (%d,%d)."),
						Stage.FootprintCellXY.X,
						Stage.FootprintCellXY.Y);
					return false;
				}
				TerrainStageByXY.Add(Stage.FootprintCellXY, Stage.TerrainStageIndex);
			}
			for (const FLayoutContractActiveCellRecord& ActiveCell : Context.FrozenTerrainContract->ActiveCells)
			{
				if (ActiveCells.Contains(ActiveCell.Cell))
				{
					OutFailureReason = FString::Printf(
						TEXT("Finalized-cell view rejected duplicate frozen active cell %s."),
						*ActiveCell.Cell.ToString());
					return false;
				}
				ActiveCells.Add(ActiveCell.Cell);
			}
			for (int32 ReservationIndex = 0;
				ReservationIndex < Context.FrozenTerrainContract->ReservedOpenTerrainReservations.Num();
				++ReservationIndex)
			{
				const FLayoutCellReservationRecord& Reservation =
					Context.FrozenTerrainContract->ReservedOpenTerrainReservations[ReservationIndex];
				if (Reservation.ReservationKind != ELayoutCellReservationKind::ReservedEmpty
					|| Reservation.ReservationId.IsNone()
					|| Context.ReservedOpenReservationIndexByCell.Contains(Reservation.Cell))
				{
					OutFailureReason = TEXT("Finalized-cell view rejected invalid or duplicate reserved-open terrain authority.");
					return false;
				}
				Context.ReservedOpenReservationIndexByCell.Add(Reservation.Cell, ReservationIndex);
			}
			Context.RemovedFinalizedCells.Append(Context.FrozenTerrainContract->RemovedCells);
		}

		if (Context.bUsesSteppedTerrainContract
			&& (Context.FrozenTerrainContract == nullptr
				|| TerrainStageByXY.IsEmpty()
				|| ActiveCells.IsEmpty()))
		{
			OutFailureReason = TEXT("Finalized stepped-cell view requires frozen stage-map and active-cell authority.");
			return false;
		}

		for (int32 PlannedCellIndex = 0; PlannedCellIndex < Context.Result.PlannedCells.Num(); ++PlannedCellIndex)
		{
			const FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells[PlannedCellIndex];
			if (Context.FinalizedCellsByPhysicalCell.Contains(PlannedCell.Cell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Finalized-cell view rejected duplicate physical planned cell %s."),
					*PlannedCell.Cell.ToString());
				return false;
			}
			if (Context.bUsesSteppedTerrainContract && !ActiveCells.Contains(PlannedCell.Cell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Finalized stepped-cell view rejected planned cell %s without frozen active-cell authority."),
					*PlannedCell.Cell.ToString());
				return false;
			}
			if (Context.ReservedOpenReservationIndexByCell.Contains(PlannedCell.Cell)
				|| Context.RemovedFinalizedCells.Contains(PlannedCell.Cell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Finalized-cell view rejected removed or reserved-open cell %s in active planned topology."),
					*PlannedCell.Cell.ToString());
				return false;
			}

			Context.OwningTopologyCellsByPhysicalCell.Add(PlannedCell.Cell, PlannedCell);
			LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord& ViewRecord =
				Context.FinalizedCellsByPhysicalCell.Add(PlannedCell.Cell);
			ViewRecord.PlannedCellIndex = PlannedCellIndex;
			ViewRecord.bHasAuthoredSource = !PlannedCell.bIsBridgeCell;
			if (ViewRecord.bHasAuthoredSource)
			{
				const int32 ModuleLevel = PlannedCell.ModuleLevelIndex != INDEX_NONE
					? PlannedCell.ModuleLevelIndex
					: PlannedCell.Cell.Z;
				ViewRecord.SourceAuthoredCell = FIntVector(
					PlannedCell.Cell.X,
					PlannedCell.Cell.Y,
					ModuleLevel);
				if (AuthoredSourceCells.Contains(ViewRecord.SourceAuthoredCell))
				{
					OutFailureReason = FString::Printf(
						TEXT("Finalized-cell view rejected ambiguous authored source cell %s."),
						*ViewRecord.SourceAuthoredCell.ToString());
					return false;
				}
				AuthoredSourceCells.Add(ViewRecord.SourceAuthoredCell);
				if (Context.bUsesSteppedTerrainContract && PlannedCell.ModuleLevelIndex == INDEX_NONE)
				{
					OutFailureReason = FString::Printf(
						TEXT("Finalized stepped-cell view rejected real cell %s without preserved ModuleLevelIndex."),
						*PlannedCell.Cell.ToString());
					return false;
				}
			}
			else if (Context.bUsesSteppedTerrainContract && PlannedCell.ModuleLevelIndex == INDEX_NONE)
			{
				OutFailureReason = FString::Printf(
					TEXT("Finalized stepped-cell view rejected generated bridge cell %s without module-level metadata."),
					*PlannedCell.Cell.ToString());
				return false;
			}

			if (const int32* TerrainStage = TerrainStageByXY.Find(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y)))
			{
				ViewRecord.TerrainStageIndex = *TerrainStage;
				ViewRecord.bHasTerrainStage = true;
			}
			else if (Context.bUsesSteppedTerrainContract)
			{
				OutFailureReason = FString::Printf(
					TEXT("Finalized stepped-cell view rejected cell %s without frozen terrain-stage mapping."),
					*PlannedCell.Cell.ToString());
				return false;
			}
		}

		if (Context.bUsesSteppedTerrainContract && ActiveCells.Num() != Context.FinalizedCellsByPhysicalCell.Num())
		{
			OutFailureReason = TEXT("Finalized stepped-cell view rejected frozen active cells without matching planned-cell metadata.");
			return false;
		}
		return true;
	}

	/** Compiles preserve-producing sparse rules against canonical finalized topology without removing claimable cells. */
	void CompilePreservedTerrainSparseRules(FSolveContext& Context)
	{
		Context.TerrainResidualRuleIdByCell.Reset();
		Context.Result.ResidualUnoccupiedCells.RemoveAll([](const FLayoutResidualCellRecord& ResidualCell)
		{
			return ResidualCell.Source == ELayoutResidualCellSource::TerrainBackedRule;
		});

		for (const FLayoutSparsePlacementRuleSolveSnapshot& Rule : Context.ProfileSnapshot.SparsePlacementRules)
		{
			const bool bPreservesTerrain =
				Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
				|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
			if (!bPreservesTerrain)
			{
				continue;
			}

			for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
			{
				if (Context.TerrainResidualRuleIdByCell.Contains(PlannedCell.Cell)
					|| PlannedCell.Intent == ELayoutCellIntent::Entry
					|| PlannedCell.Intent == ELayoutCellIntent::Connector
					|| PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
					|| PlannedCell.bIsBridgeCell
					|| PlannedCell.TerrainSeamFaceMask != 0
					|| Context.VerticalAccessReservedCells.Contains(PlannedCell.Cell)
					|| !LayoutProfileSolverInternal::DoesCellMatchResolvedPlacementZone(Context, PlannedCell.Cell, Rule.PlacementZone)
					|| !DoesCellMatchAuthoredLevelScope(
						FIntVector(PlannedCell.Cell.X, PlannedCell.Cell.Y, LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(Context, PlannedCell.Cell)),
						Context.ProfileSnapshot.LevelCount,
						Rule.LevelPlacementPolicy,
						Rule.SpecificLevel))
				{
					continue;
				}

				Context.TerrainResidualRuleIdByCell.Add(PlannedCell.Cell, Rule.RuleId);
				FLayoutResidualCellRecord& ResidualCell = Context.Result.ResidualUnoccupiedCells.AddDefaulted_GetRef();
				ResidualCell.Cell = PlannedCell.Cell;
				ResidualCell.Intent = PlannedCell.Intent;
				ResidualCell.Source = ELayoutResidualCellSource::TerrainBackedRule;
				ResidualCell.SourceRegionDebugPath = Context.RegionDebugPath;
				ResidualCell.SourceSparsePlacementRuleId = Rule.RuleId;
				ResidualCell.ModuleLevelIndex = LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(Context, PlannedCell.Cell);
				ResidualCell.PlacementZone = Rule.PlacementZone == ELayoutPlacementZone::Core
					? ELayoutPlacementZone::Core
					: PlannedCell.PlacementZone;
				if (const LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord* ViewRecord = Context.FinalizedCellsByPhysicalCell.Find(PlannedCell.Cell))
				{
					ResidualCell.TerrainStageIndex = ViewRecord->bHasTerrainStage
						? ViewRecord->TerrainStageIndex
						: INDEX_NONE;
				}
			}
		}
	}

	void BuildOverridePlan(
		FSolveContext& Context,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells)
	{
		Context.Result.PlannedCells = PlannedCells;
		if (!Context.ProfileSnapshot.bSupportsSteppedTerrainSolve
			|| !Context.ProfileSnapshot.bEnableTerrainSeams)
		{
			TSet<FIntVector> OrdinaryEntryCells;
			for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
			{
				if (PlannedCell.Intent == ELayoutCellIntent::Entry
					&& PlannedCell.EntryOrigin != ELayoutEntryOrigin::TerrainSeam)
				{
					OrdinaryEntryCells.Add(PlannedCell.Cell);
				}
			}
			for (FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
			{
				PlannedCell.TerrainSeamFaceMask = 0;
				PlannedCell.VerticalAccessLandingContactMask = 0;
				if (PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam)
				{
					PlannedCell.EntryOrigin = ELayoutEntryOrigin::None;
					if (PlannedCell.Intent == ELayoutCellIntent::Entry)
					{
						PlannedCell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
							FootprintSize,
							OrdinaryEntryCells,
							PlannedCell.Cell);
					}
				}
			}
		}
		CompileFinalSurfaceTopology(
			Context.Result.PlannedCells,
			&Context.ExternalPlannedNeighborFaceMasks);
		FString FinalizedCellViewFailureReason;
		if (!TryBuildFinalizedCellView(Context, FinalizedCellViewFailureReason))
		{
			Context.Result.FailureReason = FinalizedCellViewFailureReason;
			return;
		}
		Context.PlannedCellIntents.Reset();
		Context.PlacementZonesByCell.Reset();
		Context.TerrainSeamFaceMasksByCell.Reset();
		Context.ModuleLevelByCell.Reset();
		Context.SteppedTraversalDeckCells.Reset();
		Context.bHasPreparedInitialDomainFailure = false;
		Context.PreparedInitialDomainFailureCell = FIntVector::ZeroValue;
		Context.SolveOrder.Reset();
		Context.RouteConstraintIndexByCell.Reset();
		Context.ReservationFlagsByCell.Reset();
		Context.ReservedOpenCells.Reset();
		Context.Result.RouteConstraints.Reset();
		Context.Result.CompiledReservations.Reset();
		Context.FootprintSize = FootprintSize;

		for (const TPair<FIntVector, LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord>& ViewPair : Context.FinalizedCellsByPhysicalCell)
		{
			const LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord& ViewRecord = ViewPair.Value;
			const FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells[ViewRecord.PlannedCellIndex];
			Context.PlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
			const int32 ModuleLevel = ViewRecord.bHasAuthoredSource
				? ViewRecord.SourceAuthoredCell.Z
				: (PlannedCell.ModuleLevelIndex != INDEX_NONE
					? PlannedCell.ModuleLevelIndex
					: PlannedCell.Cell.Z);
			Context.ModuleLevelByCell.Add(PlannedCell.Cell, ModuleLevel);
			if (Context.ProfileSnapshot.bSupportsSteppedTerrainSolve
				&& (PlannedCell.bIsBridgeCell || ModuleLevel != PlannedCell.Cell.Z))
			{
				Context.SteppedTraversalDeckCells.Add(PlannedCell.Cell);
			}
			Context.PlacementZonesByCell.Add(PlannedCell.Cell, PlannedCell.PlacementZone);
			if (PlannedCell.TerrainSeamFaceMask != 0)
			{
				Context.TerrainSeamFaceMasksByCell.Add(
					PlannedCell.Cell,
					PlannedCell.TerrainSeamFaceMask);
			}
			Context.SolveOrder.Add(PlannedCell.Cell);
		}

		RebuildTopPlannedLevelByXY(Context);
		CompilePreservedTerrainSparseRules(Context);

		Context.SolveOrder.Sort([&Context](const FIntVector& Left, const FIntVector& Right)
		{
			if (Left.Z != Right.Z)
			{
				return Left.Z < Right.Z;
			}

			const int32 LeftIntentPriority = GetIntentPriority(Context.PlannedCellIntents[Left]);
			const int32 RightIntentPriority = GetIntentPriority(Context.PlannedCellIntents[Right]);
			if (LeftIntentPriority != RightIntentPriority)
			{
				return LeftIntentPriority < RightIntentPriority;
			}

			if (Left.Y != Right.Y)
			{
				return Left.Y < Right.Y;
			}

			return Left.X < Right.X;
		});

		Context.Result.Seed = Context.Seed;
		Context.Result.FootprintSize = Context.FootprintSize;
		CanonicalizeIncomingBoundaryPointsByFace(
			Context.IncomingBoundaryPoints);
		RefreshCompiledFaceInterfaces(Context);
	}

	bool IsFaceCompatibleWithOccupancy(
		const FLayoutFaceRule& FaceRule,
		const bool bNeighborPlanned,
		const bool bNeighborFilled)
	{
		if (FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		{
			return true;
		}

		if (!bNeighborPlanned || !bNeighborFilled)
		{
			return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
				|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
		}

		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
	}

	bool RequiresWalkableFilledCompatibility(const FLayoutFaceRule& FaceRule)
	{
		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor;
	}

	bool RequiresFilledNeighborOccupancy(const FLayoutFaceRule& FaceRule)
	{
		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
	}


	bool TryGetWorldFaceRule(
		const ULayoutModuleAsset* Module,
		ELayoutFaceDirection WorldDirection,
		int32 YawRotationSteps,
		FLayoutFaceRule& OutFaceRule);

	bool HasSharedConnectedWalkableArea(const FLayoutFaceRule& LeftFaceRule, const FLayoutFaceRule& RightFaceRule);

	bool TryGetPlacementFaceRule(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement,
		ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule);

	FLayoutFaceRule BuildIncomingBoundaryFaceRule(
		const FLayoutSolveBoundaryPoint& BoundaryPoint);

	bool TryBuildPlacementWorldFaceRules(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement,
		FLayoutModuleFaceRules& OutWorldFaceRules);

	bool TryGetCandidateFaceRule(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate,
		ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule);
	uint8 GetCompiledSeedIntentMaskForCell(
		const FSolveContext& Context,
		const FIntVector& Cell);

	bool AreAdjacentCandidatesCompatible(
		FSolveContext& Context,
		const FSolveCandidate& Candidate,
		const FSolveCandidate& NeighborCandidate,
		ELayoutFaceDirection Direction);

	TArray<FSolveCandidate> BuildOrderedCandidates(
		FSolveContext& Context,
		const FIntVector& Cell,
		ELayoutCellIntent Intent);
	bool HasAnyLegalCandidateForCell(FSolveContext& Context, const FIntVector& Cell, TArray<FString>* OutFailureReasons = nullptr);
	bool IsPlannedCellDischargedByForeignBundleCandidate(
		FSolveContext& Context,
		const FIntVector& Cell,
		const TSet<FWalkableNodeKey>* ReachableNodesOverride = nullptr);
	bool ShouldRunExpensiveForwardChecks(FSolveContext& Context);
	bool PropagateUnsolvedDomains(FSolveContext& Context, FString& OutFailureReason);
	TArray<FSolveCandidate> GetLegalCandidatesForCellIndexed(
		FSolveContext& Context,
		const FIntVector& Cell,
		TArray<FString>* OutFailureReasons = nullptr,
		const TSet<FWalkableNodeKey>* ReachableNodesOverride = nullptr,
		bool bSortReachabilityCandidates = true);
	TArray<FSolveCandidate>
	GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
		FSolveContext& Context,
		const FIntVector& Cell,
		TArray<FString>* OutFailureReasons = nullptr,
		const TSet<FWalkableNodeKey>* ReachableNodesOverride = nullptr,
		bool bSortReachabilityCandidates = true);
	bool SelectNextCellByMRV(FSolveContext& Context, FIntVector& OutCell, TArray<FSolveCandidate>& OutLegalCandidates);
	void AddCandidateFailureDetail(
		FSolveContext& Context,
		TArray<FString>& CandidateFailureReasons,
		const FSolveCandidate& Candidate,
		const FString& FailureReason);
	bool IsPlacementCompatibleWithCompletedNeighborMap(
		FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveContext::FSolvePlacement& Placement,
		FString* OutFailureReason = nullptr);
	bool ForwardCheckPlacedCells(FSolveContext& Context, const TArray<FIntVector>& PlacedCells, FString& OutFailureReason);

	FLayoutModuleFaceRules BuildWorldFaceRulesForYaw(const FLayoutModuleFaceRules& EffectiveRules, int32 YawRotationSteps);
	TArray<FLayoutDerivedSpanOffer> BuildWorldSpanOffersForYaw(const TArray<FLayoutDerivedSpanOffer>& DerivedSpanOffers, int32 YawRotationSteps);

	FString BuildVariantSignature(
		const FLayoutModuleFaceRules& WorldFaceRules,
		const TArray<FLayoutDerivedInternalTraversalLink>& DerivedInternalTraversalLinks);

	bool DoesChildModuleEntryMatchPlannedCell(
		const FLayoutRegionContentEntrySolveSnapshot& Entry,
		const FLayoutModuleSolveSnapshot& Module,
		const FLayoutPlannedCell& PlannedCell,
		const int32 ChildTopModuleLevel)
	{
		if (!Module.SupportsIntent(PlannedCell.Intent))
		{
			return false;
		}

		const ELayoutPlacementZone ModuleZone = Entry.ModulePlacementZone;
		const bool bZoneMatches = ModuleZone == ELayoutPlacementZone::Any
			|| (ModuleZone == ELayoutPlacementZone::Perimeter
				&& (PlannedCell.PlacementZone == ELayoutPlacementZone::Edge
					|| PlannedCell.PlacementZone == ELayoutPlacementZone::Corner))
			|| ModuleZone == PlannedCell.PlacementZone
			|| (ModuleZone == ELayoutPlacementZone::Core
				&& PlannedCell.Intent == ELayoutCellIntent::Core);
		if (!bZoneMatches)
		{
			return false;
		}

		switch (Entry.ModuleLevelPlacementPolicy)
		{
		case ELayoutLevelPlacementPolicy::GroundOnly:
			return PlannedCell.ModuleLevelIndex == 0;
		case ELayoutLevelPlacementPolicy::SpecificLevel:
			return PlannedCell.ModuleLevelIndex == Entry.ModuleSpecificLevel;
		case ELayoutLevelPlacementPolicy::TopLevelOnly:
			return PlannedCell.ModuleLevelIndex == ChildTopModuleLevel;
		case ELayoutLevelPlacementPolicy::AnyLevel:
		default:
			return true;
		}
	}

	bool ShouldAuditAdjacencyBetweenCells(const FSolveContext& Context, const FIntVector& Cell, const FIntVector& NeighborCell);

	/** Admits empty cells only when each fixed or child-domain face permits empty occupancy. */
	bool DoesCellSatisfyCompiledEmptyInterfaces(
		const FSolveContext& Context,
		const FIntVector& Cell,
		FString* OutFailureReason = nullptr)
	{
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction =
				static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface == nullptr)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("missing compiled face-interface row for empty-candidate legality on planned cell %s."),
						*Cell.ToString());
				}
				return false;
			}

			const FIntVector NeighborCell =
				Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			switch (FaceInterface->NeighborKind)
			{
			case ESolveCellFaceNeighborKind::FixedFilledNeighbor:
			case ESolveCellFaceNeighborKind::FixedEmptyNeighbor:
			{
				const FSolveContext::FSolvePlacement* NeighborPlacement =
					FindPlacementOrFixedNeighbor(Context, NeighborCell);
				if (NeighborPlacement == nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("compiled fixed-neighbor interface for empty-candidate legality on planned cell %s did not resolve a live fixed placement."),
							*Cell.ToString());
					}
					return false;
				}

				if (!FSolveContext::IsOccupiedPlacement(*NeighborPlacement))
				{
					continue;
				}

				FLayoutFaceRule NeighborFaceRule;
				if (!TryGetPlacementFaceRule(
						Context,
						*NeighborPlacement,
						FLayoutDirectionUtils::GetOpposite(Direction),
						NeighborFaceRule))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("filled neighbor at cell %s is missing a face rule while evaluating whether planned cell %s may stay empty."),
							*NeighborCell.ToString(),
							*Cell.ToString());
					}
					return false;
				}

				if (!IsFaceCompatibleWithOccupancy(
						NeighborFaceRule,
						false,
						false))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("filled neighbor at cell %s forbids planned cell %s from staying empty."),
							*NeighborCell.ToString(),
							*Cell.ToString());
					}
					return false;
				}

				continue;
			}
			case ESolveCellFaceNeighborKind::IncomingBoundary:
			case ESolveCellFaceNeighborKind::SupportBoundary:
			{
				const FLayoutSolveBoundaryPoint* BoundaryPoint =
					ResolveCompiledIncomingBoundaryPoint(
						Context,
						*FaceInterface);
				if (BoundaryPoint == nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("compiled incoming-boundary interface for empty-candidate legality on planned cell %s lost its source boundary point."),
							*Cell.ToString());
					}
					return false;
				}

				const FLayoutFaceRule BoundaryFaceRule =
					BuildIncomingBoundaryFaceRule(*BoundaryPoint);
				if (!IsFaceCompatibleWithOccupancy(
						BoundaryFaceRule,
						false,
						false))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("incoming boundary from region '%s' forbids planned cell %s from staying empty."),
							*BoundaryPoint->SourceRegionDebugPath,
							*Cell.ToString());
					}
					return false;
				}

				continue;
			}
			case ESolveCellFaceNeighborKind::ChildRegionContact:
			{
				// Sparse parent space must consume the same child module/yaw domain as filled admission.
				const FLayoutRegionContentSetSolveSnapshot* const* Content = Context.ChildContentSetsByCell.Find(NeighborCell);
				const FLayoutModuleCatalog* const* Catalog = Context.ChildModuleCatalogsByCell.Find(NeighborCell);
				const FLayoutPlannedCell* ChildCell = Context.ChildPlannedCellsByCell.Find(NeighborCell);
				const int32* TopLevel = Context.ChildTopModuleLevelByCell.Find(NeighborCell);
				TMap<ELayoutFaceDirection, FLayoutFaceRule> Faces;
				Faces.Add(Direction, FLayoutFaceRule{});
				if (Content == nullptr || *Content == nullptr || Catalog == nullptr || *Catalog == nullptr
					|| ChildCell == nullptr || TopLevel == nullptr
					|| !LayoutProfileSolverInternal::HasCompatibleChildBoundaryModule(
						**Content, **Catalog, *ChildCell, *TopLevel, Faces, nullptr, false))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("child domain at %s does not admit empty parent cell %s."),
							*NeighborCell.ToString(), *Cell.ToString());
					}
					return false;
				}
				continue;
			}
			case ESolveCellFaceNeighborKind::StructuralVerticalOverlap:
			case ESolveCellFaceNeighborKind::TerrainResidualNeighbor:
			case ESolveCellFaceNeighborKind::InternalPlannedNeighbor:
			case ESolveCellFaceNeighborKind::ExternalPlannedNeighbor:
			case ESolveCellFaceNeighborKind::DeferredExternalContract:
			case ESolveCellFaceNeighborKind::OuterBoundary:
			case ESolveCellFaceNeighborKind::ReservedExternalCell:
				continue;
			default:
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("unknown compiled face-interface kind for empty-candidate legality on planned cell %s."),
						*Cell.ToString());
				}
				return false;
			}
		}

		return true;
	}


	int32 ResolveOpenCellCountFromPercent(const int32 EligibleCellCount, const float Percent)
	{
		if (EligibleCellCount <= 0 || Percent <= 0.0f)
		{
			return 0;
		}

		return FMath::Clamp(
			FMath::RoundToInt(static_cast<float>(EligibleCellCount) * Percent / 100.0f),
			0,
			EligibleCellCount);
	}

	void ResolveOpenCellRange(
		const int32 EligibleCellCount,
		const float MinPercent,
		const float MaxPercent,
		const int32 MinCells,
		const int32 MaxCells,
		int32& OutMinOpenCells,
		int32& OutMaxOpenCells)
	{
		const int32 MinFromPercent = ResolveOpenCellCountFromPercent(EligibleCellCount, MinPercent);
		const int32 MaxFromPercent = ResolveOpenCellCountFromPercent(EligibleCellCount, MaxPercent);

		OutMinOpenCells = FMath::Max(MinFromPercent, FMath::Max(0, MinCells));
		OutMaxOpenCells = MaxPercent > 0.0f || MaxCells > 0
			? FMath::Min(
				EligibleCellCount,
				MaxCells > 0 ? FMath::Min(EligibleCellCount, MaxCells) : EligibleCellCount)
			: EligibleCellCount;

		if (MaxPercent > 0.0f)
		{
			OutMaxOpenCells = FMath::Min(OutMaxOpenCells, MaxFromPercent);
		}

		if (OutMaxOpenCells < OutMinOpenCells)
		{
			OutMaxOpenCells = OutMinOpenCells;
		}
	}

	void SortCellsDeterministicallyForOpenSelection(TArray<FIntVector>& Cells, const int32 Seed)
	{
		Cells.Sort([Seed](const FIntVector& Left, const FIntVector& Right)
		{
			const uint32 LeftHash = HashCombineFast(static_cast<uint32>(Seed), GetTypeHash(Left));
			const uint32 RightHash = HashCombineFast(static_cast<uint32>(Seed), GetTypeHash(Right));
			if (LeftHash != RightHash)
			{
				return LeftHash < RightHash;
			}

			if (Left.Z != Right.Z)
			{
				return Left.Z < Right.Z;
			}
			if (Left.Y != Right.Y)
			{
				return Left.Y < Right.Y;
			}
			return Left.X < Right.X;
		});
	}

	bool CompileReservedOpenSpaceRules(FSolveContext& Context)
	{
		Context.ReservedOpenCells.Reset();
		Context.bReservedOpenPrewarmInfeasible = false;
		const FLayoutProfileSolveSnapshot& Profile = GetEffectiveProfileSnapshot(Context);
		const bool bHasPreparedPairTopology = Profile.bSupportsSteppedTerrainSolve
			&& (!Context.VerticalAccessReservedCells.IsEmpty()
				|| Context.Result.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
				{
					return Cell.Intent == ELayoutCellIntent::VerticalAccess
						|| Cell.bIsBridgeCell || Cell.TerrainSeamFaceMask != 0;
				}));
		bool bRequirePreparedPairSupportAfterReservation = bHasPreparedPairTopology;
		if (bHasPreparedPairTopology)
		{
			// Compare against the same unmodified topology so a pre-existing host
			// failure does not get misattributed to one reserved-open alternative.
			const TSet<FIntVector> NoReservedCells;
			FIntVector BaselineUnsupportedCell = FIntVector::ZeroValue;
			FString BaselineFailureReason;
			bRequirePreparedPairSupportAfterReservation = DoesReservedOpenMaskKeepStaticDomains(
				Context,
				NoReservedCells,
				true,
				BaselineUnsupportedCell,
				BaselineFailureReason);
		}
		for (const FLayoutReservedOpenSpaceRule& Rule : Profile.ReservedOpenSpaceRules)
		{
			TArray<FIntVector> EligibleCells;
			for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
			{
				if (Context.ReservedOpenCells.Contains(PlannedCell.Cell))
				{
					continue;
				}
				if (PlannedCell.Intent == ELayoutCellIntent::Entry
					|| PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
					|| PlannedCell.Intent == ELayoutCellIntent::Connector
					|| PlannedCell.TerrainSeamFaceMask != 0
					|| Context.VerticalAccessReservedCells.Contains(PlannedCell.Cell))
				{
					continue;
				}
				if (!LayoutProfileSolverInternal::DoesCellMatchResolvedPlacementZone(
					Context,
					PlannedCell.Cell,
					Rule.PlacementZone))
				{
					continue;
				}
				if (!DoesCellMatchAuthoredLevelScope(
					FIntVector(PlannedCell.Cell.X, PlannedCell.Cell.Y, PlannedCell.ModuleLevelIndex),
					Profile.LevelCount,
					Rule.LevelPlacementPolicy,
					Rule.SpecificLevel))
				{
					continue;
				}

				EligibleCells.Add(PlannedCell.Cell);
			}

			int32 MinimumReservedCount = 0;
			int32 MaximumReservedCount = 0;
			ResolveOpenCellRange(
				EligibleCells.Num(),
				Rule.ReservedPercent,
				Rule.ReservedPercent,
				Rule.MinReservedCells,
				Rule.MaxReservedCells,
				MinimumReservedCount,
				MaximumReservedCount);

			if (MinimumReservedCount > EligibleCells.Num())
			{
				Context.bReservedOpenPrewarmInfeasible = true;
				Context.Result.FailureReason = FString::Printf(
					TEXT("Reserved open-space rule '%s' requires at least %d cells, but only %d cells remain after protected Entry, VerticalAccess, and Connector cells are excluded."),
					*Rule.RuleId.ToString(),
					MinimumReservedCount,
					EligibleCells.Num());
				return false;
			}

			const int32 ReservedCount = MaximumReservedCount > 0
				? FMath::Clamp(MaximumReservedCount, MinimumReservedCount, EligibleCells.Num())
				: FMath::Clamp(MinimumReservedCount, 0, EligibleCells.Num());
			if (ReservedCount <= 0)
			{
				continue;
			}

			SortCellsDeterministicallyForOpenSelection(EligibleCells, HashCombine(GetTypeHash(Rule.RuleId), Context.Seed));
			for (int32 ReservationIndex = 0; ReservationIndex < ReservedCount; ++ReservationIndex)
			{
				bool bReservedCellSelected = false;
				FIntVector FirstUnsupportedCell = FIntVector::ZeroValue;
				FIntVector FirstRejectedCell = FIntVector::ZeroValue;
				FString FirstFailureReason;
				int32 AlternativesConsidered = 0;
				for (const FIntVector& CandidateCell : EligibleCells)
				{
					if (Context.ReservedOpenCells.Contains(CandidateCell))
					{
						continue;
					}

					++AlternativesConsidered;
					TSet<FIntVector> ProspectiveReservedCells = Context.ReservedOpenCells;
					ProspectiveReservedCells.Add(CandidateCell);
					FIntVector UnsupportedCell = FIntVector::ZeroValue;
					FString FailureReason;
					if (DoesReservedOpenMaskKeepStaticDomains(
						Context,
						ProspectiveReservedCells,
						bRequirePreparedPairSupportAfterReservation,
						UnsupportedCell,
						FailureReason))
					{
						Context.ReservedOpenCells.Add(CandidateCell);
						FLayoutCellReservationRecord& Reservation = Context.Result.CompiledReservations.AddDefaulted_GetRef();
						Reservation.ReservationId = FLayoutId(*FString::Printf(
							TEXT("ReservedOpen.%s.%d.%d.%d"),
							*Rule.RuleId.ToString(),
							CandidateCell.X,
							CandidateCell.Y,
							CandidateCell.Z));
						Reservation.Cell = CandidateCell;
						Reservation.Intent = Context.PlannedCellIntents.FindRef(CandidateCell);
						Reservation.ReservationKind = ELayoutCellReservationKind::ReservedEmpty;
						Reservation.TerrainBehavior = Rule.TerrainBehavior;
						bReservedCellSelected = true;
						break;
					}

					if (FirstFailureReason.IsEmpty())
					{
						FirstRejectedCell = CandidateCell;
						FirstUnsupportedCell = UnsupportedCell;
						FirstFailureReason = MoveTemp(FailureReason);
					}
				}

				if (!bReservedCellSelected)
				{
					Context.bReservedOpenPrewarmInfeasible = true;
					Context.Result.FailureReason = FString::Printf(
						TEXT("Reserved open-space rule '%s' cannot select statically feasible cell %d of %d. First rejected cell=%s; first unsupported domain=%s; alternatives considered=%d. %s"),
						*Rule.RuleId.ToString(),
						ReservationIndex + 1,
						ReservedCount,
						*FirstRejectedCell.ToString(),
						*FirstUnsupportedCell.ToString(),
						AlternativesConsidered,
						FirstFailureReason.IsEmpty() ? TEXT("No candidate retained local static module support.") : *FirstFailureReason);
					return false;
				}
			}
		}

		if (Context.ReservedOpenCells.IsEmpty())
		{
			return true;
		}

		Context.Result.PlannedCells.RemoveAll([&Context](const FLayoutPlannedCell& PlannedCell)
		{
			return Context.ReservedOpenCells.Contains(PlannedCell.Cell);
		});
		for (const FIntVector& ReservedCell : Context.ReservedOpenCells)
		{
			Context.PlannedCellIntents.Remove(ReservedCell);
		}
		return true;
	}


	/** Rebuilds derived tops without changing the authored level contract of delegated columns. */
	void RebuildTopPlannedLevelByXY(FSolveContext& Context)
	{
		Context.TopPlannedLevelByXY = Context.OwningTopModuleLevelByXY;
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (PlannedCell.bIsBridgeCell)
			{
				continue;
			}

			const FIntPoint ColumnKey(PlannedCell.Cell.X, PlannedCell.Cell.Y);
			if (Context.OwningTopModuleLevelByXY.Contains(ColumnKey)) continue;
			const int32 ModuleLevel = LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(Context, PlannedCell.Cell);
			int32& TopLevel = Context.TopPlannedLevelByXY.FindOrAdd(ColumnKey);
			TopLevel = FMath::Max(TopLevel, ModuleLevel);
		}
	}

	bool DoesCellMatchLevelPlacementPolicy(
		const FIntVector& Cell,
		const int32 ModuleLevel,
		const TMap<FIntPoint, int32>& TopPlannedLevelByXY,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel)
	{
		switch (LevelPlacementPolicy)
		{
		case ELayoutLevelPlacementPolicy::AnyLevel:
			return true;
		case ELayoutLevelPlacementPolicy::GroundOnly:
			return ModuleLevel == 0;
		case ELayoutLevelPlacementPolicy::SpecificLevel:
			return ModuleLevel == SpecificLevel;
		case ELayoutLevelPlacementPolicy::TopLevelOnly:
		{
			const int32* TopLevel = TopPlannedLevelByXY.Find(FIntPoint(Cell.X, Cell.Y));
			return TopLevel != nullptr && ModuleLevel == *TopLevel;
		}
		case ELayoutLevelPlacementPolicy::AboveGroundLevel:
			return ModuleLevel > 0;
		case ELayoutLevelPlacementPolicy::BelowTopLevel:
		{
			const int32* TopLevel = TopPlannedLevelByXY.Find(FIntPoint(Cell.X, Cell.Y));
			return TopLevel != nullptr && ModuleLevel < *TopLevel;
		}
		default:
			return true;
		}
	}

	void BuildOrientedVariants(FSolveContext& Context)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_VariantBuild, STAT_PorismLayout_VariantBuild);
		if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::VariantBuilds, Context.Result.FailureReason))
		{
			Context.bTimeBudgetExceeded = true;
			return;
		}
		Context.Variants.Reset();
		Context.VariantIndicesByIntent.Reset();

		if (Context.ModuleSnapshots.IsEmpty())
		{
			return;
		}

		TMap<FString, int32> SignatureToVariantIndex;
		for (int32 ModuleSnapshotIndex = 0; ModuleSnapshotIndex < Context.ModuleSnapshots.Num(); ++ModuleSnapshotIndex)
		{
			if (!LayoutSolveExecution::Checkpoint(Context.Result.FailureReason))
			{
				Context.bTimeBudgetExceeded = true;
				return;
			}
			const FLayoutModuleSolveSnapshot& ModuleSnapshot = Context.ModuleSnapshots[ModuleSnapshotIndex];
			for (const int32 YawStep : ModuleSnapshot.AllowedYawRotationSteps)
			{
				FSolveContext::FOrientedModuleVariant Variant;
				Variant.ModuleSnapshotIndex = ModuleSnapshotIndex;
				Variant.ModuleSnapshotId = ModuleSnapshot.SnapshotId;
				Variant.ModuleDebugName = ModuleSnapshot.DebugName;
				Variant.SourceContentEntryId = ModuleSnapshot.SourceContentEntryId;
				Variant.ProvidedZoneFeatures = ModuleSnapshot.ProvidedZoneFeatures;
				Variant.PlacementZone = ModuleSnapshot.PlacementZone;
				Variant.LevelPlacementPolicy = ModuleSnapshot.LevelPlacementPolicy;
				Variant.SpecificLevel = ModuleSnapshot.SpecificLevel;
				Variant.bOptional = ModuleSnapshot.bOptional;
				Variant.YawRotationSteps = YawStep;
				Variant.WorldFaceRules = BuildWorldFaceRulesForYaw(ModuleSnapshot.EffectiveFaceRules, YawStep);
				Variant.WorldSpanOffers = BuildWorldSpanOffersForYaw(ModuleSnapshot.DerivedSpanOffers, YawStep);
				Variant.ClosureProviderIntents = ModuleSnapshot.ClosureProviderIntents;
				Variant.TraversalChannels = ModuleSnapshot.TraversalChannels;
				Variant.InternalAccessLinks = ModuleSnapshot.InternalAccessLinks;
				Variant.DerivedInternalTraversalLinks = ModuleSnapshot.DerivedInternalTraversalLinks;
				Variant.VerticalAccessContracts = ModuleSnapshot.DerivedVerticalAccessContracts;
				Variant.Roles = ModuleSnapshot.Roles;
				Variant.SupportedCellIntents = ModuleSnapshot.SupportedCellIntents;
				Variant.MinTraversableNeighborFaces = ModuleSnapshot.MinTraversableNeighborFaces;
				Variant.Weight = FMath::Max(1, ModuleSnapshot.Weight);
				Variant.Signature = FString::Printf(
					TEXT("%s||%s||%d||%d||%d||%d%s"),
					ModuleSnapshot.SourceContentEntryId.IsNone() ? *ModuleSnapshot.DebugName.ToString() : *ModuleSnapshot.SourceContentEntryId.ToString(),
					*BuildVariantSignature(Variant.WorldFaceRules, Variant.DerivedInternalTraversalLinks),
					static_cast<int32>(ModuleSnapshot.PlacementZone),
					static_cast<int32>(ModuleSnapshot.LevelPlacementPolicy),
					ModuleSnapshot.SpecificLevel,
					ModuleSnapshot.bOptional ? 1 : 0,
					ModuleSnapshot.OccupiedLocalCells.Num() > 1
						? *FString::Printf(TEXT("||LocalYaw=%d"), YawStep)
						: TEXT(""));

				const bool bFixedOnly = Context.FixedOnlyModuleSnapshotIds.Contains(ModuleSnapshot.SnapshotId);
				if (bFixedOnly) Variant.Signature += FString::Printf(TEXT("||Fixed=%s"), *ModuleSnapshot.SnapshotId.ToString());
				if (SignatureToVariantIndex.Contains(Variant.Signature))
				{
					continue;
				}

				const int32 VariantIndex = Context.Variants.Add(Variant);
				SignatureToVariantIndex.Add(Variant.Signature, VariantIndex);
				if (bFixedOnly) continue;
				for (const ELayoutCellIntent Intent : ModuleSnapshot.RootSupportedCellIntents)
				{
					Context.VariantIndicesByIntent.FindOrAdd(Intent).Add(VariantIndex);
				}
			}
		}
	}

	const FLayoutModuleSolveSnapshot* FindVariantModuleSnapshot(
		const FSolveContext& Context,
		const int32 VariantIndex)
	{
		if (!Context.Variants.IsValidIndex(VariantIndex))
		{
			return nullptr;
		}

		const int32 ModuleSnapshotIndex = Context.Variants[VariantIndex].ModuleSnapshotIndex;
		return Context.ModuleSnapshots.IsValidIndex(ModuleSnapshotIndex)
			? &Context.ModuleSnapshots[ModuleSnapshotIndex]
			: nullptr;
	}

	FLayoutId BuildVariantPlacementBundleId(
		const FSolveContext& Context,
		const int32 VariantIndex)
	{
		if (!Context.Variants.IsValidIndex(VariantIndex))
		{
			return NAME_None;
		}

		const int32 ModuleSnapshotIndex = Context.Variants[VariantIndex].ModuleSnapshotIndex;
		const FLayoutModuleSolveSnapshot* ModuleSnapshot = FindVariantModuleSnapshot(Context, VariantIndex);
		if (ModuleSnapshot == nullptr || Context.EffectiveSnapshotId == NAME_None)
		{
			return NAME_None;
		}

		return FLayoutId(*FString::Printf(
			TEXT("%s.Bundle.%d.%s"),
			*Context.EffectiveSnapshotId.ToString(),
			ModuleSnapshotIndex,
			*ModuleSnapshot->SnapshotId.ToString()));
	}

	FSolveContext::FSolvePlacement MakeRootSolvePlacement(
		const FSolveContext& Context,
		const FIntVector& RootCell,
		const int32 YawRotationSteps,
		const int32 VariantIndex)
	{
		FSolveContext::FSolvePlacement Placement;
		Placement.YawRotationSteps = YawRotationSteps;
		Placement.VariantIndex = VariantIndex;
		if (Context.Variants.IsValidIndex(VariantIndex))
		{
			const FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[VariantIndex];
			Placement.ModuleSnapshotIndex = Variant.ModuleSnapshotIndex;
			Placement.ModuleSnapshotId = Variant.ModuleSnapshotId;
			Placement.bEmpty = false;
		}
		else
		{
			Placement.bEmpty = true;
		}
		Placement.BundleRootCell = RootCell;
		Placement.LocalBundleCell = FIntVector::ZeroValue;
		Placement.bBundleRoot = true;
		return Placement;
	}

	bool IsBundleRootSolvePlacement(
		const FIntVector& Cell,
		const FSolveContext::FSolvePlacement& Placement)
	{
		return Placement.bBundleRoot || Placement.BundleRootCell == Cell;
	}

	bool AreSameOccupiedBundlePlacement(
		const FSolveContext::FSolvePlacement& Left,
		const FSolveContext::FSolvePlacement& Right)
	{
		return FSolveContext::IsOccupiedPlacement(Left)
			&& FSolveContext::IsOccupiedPlacement(Right)
			&& Left.BundleRootCell == Right.BundleRootCell
			&& Left.VariantIndex == Right.VariantIndex
			&& Left.YawRotationSteps == Right.YawRotationSteps;
	}

	TArray<FIntVector> BuildCandidateWorldOccupiedCells(
		const FSolveContext& Context,
		const FIntVector& RootCell,
		const FSolveCandidate& Candidate);

	bool DoesCandidateMatchForcedPlacementBundleInsertion(
		const FSolveContext& Context,
		const FLayoutForcedPlacementBundleInsertion& Insertion,
		const FSolveCandidate& Candidate)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			return false;
		}

		if (BuildVariantPlacementBundleId(Context, Candidate.VariantIndex) != Insertion.BundleId)
		{
			return false;
		}

		return BuildCandidateWorldOccupiedCells(Context, Insertion.AnchorCell, Candidate).Contains(Insertion.ProvingCell);
	}

	bool IsForcedPlacementBundleInsertionAlreadySatisfied(
		const FSolveContext& Context,
		const FLayoutForcedPlacementBundleInsertion& Insertion)
	{
		const FSolveContext::FSolvePlacement* AnchorPlacement = Context.Placements.Find(Insertion.AnchorCell);
		const FSolveContext::FSolvePlacement* ProvingPlacement = Context.Placements.Find(Insertion.ProvingCell);
		if (AnchorPlacement == nullptr
			|| ProvingPlacement == nullptr
			|| !FSolveContext::IsOccupiedPlacement(*AnchorPlacement)
			|| !FSolveContext::IsOccupiedPlacement(*ProvingPlacement))
		{
			return false;
		}

		return AnchorPlacement->BundleRootCell == Insertion.AnchorCell
			&& AreSameOccupiedBundlePlacement(*AnchorPlacement, *ProvingPlacement)
			&& BuildVariantPlacementBundleId(Context, AnchorPlacement->VariantIndex) == Insertion.BundleId;
	}

	TArray<FIntVector> BuildCandidateWorldOccupiedCells(
		const FSolveContext& Context,
		const FIntVector& RootCell,
		const FSolveCandidate& Candidate)
	{
		const FLayoutModuleSolveSnapshot* ModuleSnapshot = FindVariantModuleSnapshot(Context, Candidate.VariantIndex);
		return LayoutPlacementOccupancy::BuildSnapshotWorldOccupiedCells(
			ModuleSnapshot,
			RootCell,
			Candidate.YawRotationSteps);
	}

	/** Tests one exact bundle-root candidate against one compiled hard demand. */
	bool DoesCandidateProvideHardZoneFeatureDemand(
		const FSolveContext& Context,
		const FIntVector& RootCell,
		const int32 VariantIndex,
		const int32 DemandIndex)
	{
		if (!Context.HardZoneFeatureDemands.IsValidIndex(DemandIndex)
			|| !Context.Variants.IsValidIndex(VariantIndex))
		{
			return false;
		}

		const FLayoutPlannedCell* FinalizedCell =
			LayoutZoneFeatureDemand::FindFinalizedPlannedCell(
				Context.Result.PlannedCells,
				RootCell);
		if (FinalizedCell == nullptr)
		{
			return false;
		}

		const FSolveContext::FOrientedModuleVariant& Variant =
			Context.Variants[VariantIndex];
		const LayoutZoneFeatureDemand::FHardDemand& Demand =
			Context.HardZoneFeatureDemands[DemandIndex];
		return !Variant.SourceContentEntryId.IsNone()
			&& LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchDemand(
				Variant.ProvidedZoneFeatures,
				Demand)
			&& LayoutZoneFeatureDemand::DoesFinalizedCellMatchRequirementZone(
				*FinalizedCell,
				Context.FootprintSize,
				Demand.Zone);
	}

	/** Returns hard demands credited once by this exact bundle-root candidate. */
	void CollectMatchingHardZoneFeatureDemandIndices(
		const FSolveContext& Context,
		const FIntVector& RootCell,
		const int32 VariantIndex,
		TArray<int32>& OutDemandIndices)
	{
		OutDemandIndices.Reset();
		if (!Context.bHardZoneFeatureDemandsCompiled)
		{
			return;
		}

		for (int32 DemandIndex = 0;
			 DemandIndex < Context.HardZoneFeatureDemands.Num();
			 ++DemandIndex)
		{
			if (DoesCandidateProvideHardZoneFeatureDemand(
					Context,
					RootCell,
					VariantIndex,
					DemandIndex))
			{
				OutDemandIndices.Add(DemandIndex);
			}
		}
	}

	/** Applies one count per matching requirement for one physical bundle root. */
	void ApplyHardZoneFeatureBundleCommitment(
		FSolveContext& Context,
		const FIntVector& RootCell,
		const int32 VariantIndex,
		const int32 Delta)
	{
		TArray<int32> DemandIndices;
		CollectMatchingHardZoneFeatureDemandIndices(
			Context,
			RootCell,
			VariantIndex,
			DemandIndices);
		for (const int32 DemandIndex : DemandIndices)
		{
			if (Context.HardZoneFeatureCommittedCounts.IsValidIndex(DemandIndex))
			{
				Context.HardZoneFeatureCommittedCounts[DemandIndex] = FMath::Max(
					0,
					Context.HardZoneFeatureCommittedCounts[DemandIndex] + Delta);
			}
		}
	}

	TArray<FIntVector> CommitOccupiedCandidateBundle(
		FSolveContext& Context,
		const FIntVector& RootCell,
		const FSolveCandidate& Candidate)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_CandidateBundleCommit, STAT_PorismLayout_CandidateBundleCommit);
		TArray<FIntVector> PlacedCells;
		const FLayoutModuleSolveSnapshot* ModuleSnapshot = FindVariantModuleSnapshot(Context, Candidate.VariantIndex);
		const TArray<FIntVector> OccupiedLocalCells = LayoutPlacementOccupancy::ResolveSnapshotOccupiedLocalCells(ModuleSnapshot);
		if (OccupiedLocalCells.IsEmpty())
		{
			Context.Placements.Add(
				RootCell,
				MakeRootSolvePlacement(Context, RootCell, Candidate.YawRotationSteps, Candidate.VariantIndex));
			PlacedCells.Add(RootCell);
			ApplyHardZoneFeatureBundleCommitment(
				Context,
				RootCell,
				Candidate.VariantIndex,
				1);
			return PlacedCells;
		}

		PlacedCells.Reserve(OccupiedLocalCells.Num());
		for (const FIntVector& LocalCell : OccupiedLocalCells)
		{
			const FIntVector WorldCell = LayoutPlacementOccupancy::ProjectLocalCellToWorld(
				RootCell,
				LocalCell,
				ModuleSnapshot->BoundsCells,
				Candidate.YawRotationSteps);
			FSolveContext::FSolvePlacement Placement = MakeRootSolvePlacement(
				Context,
				RootCell,
				Candidate.YawRotationSteps,
				Candidate.VariantIndex);
			Placement.LocalBundleCell = LocalCell;
			Placement.bBundleRoot = WorldCell == RootCell;

			Context.Placements.Add(WorldCell, Placement);
			PlacedCells.Add(WorldCell);
		}

		ApplyHardZoneFeatureBundleCommitment(
			Context,
			RootCell,
			Candidate.VariantIndex,
			1);
		return PlacedCells;
	}

	/** Rolls back each bundle-root count once before removing its occupied cells. */
	void RollbackHardZoneFeatureBundleCommitments(
		FSolveContext& Context,
		const TArray<FIntVector>& PlacedCells)
	{
		TSet<FIntVector> BundleRoots;
		for (const FIntVector& Cell : PlacedCells)
		{
			if (const FSolveContext::FSolvePlacement* Placement = Context.Placements.Find(Cell))
			{
				if (FSolveContext::IsOccupiedPlacement(*Placement))
				{
					BundleRoots.Add(Placement->BundleRootCell);
				}
			}
		}

		for (const FIntVector& BundleRoot : BundleRoots)
		{
			const FSolveContext::FSolvePlacement* RootPlacement =
				Context.Placements.Find(BundleRoot);
			if (RootPlacement != nullptr)
			{
				ApplyHardZoneFeatureBundleCommitment(
					Context,
					BundleRoot,
					RootPlacement->VariantIndex,
					-1);
			}
		}
	}

	void RollbackOccupiedCandidateBundle(
		FSolveContext& Context,
		const TArray<FIntVector>& PlacedCells)
	{
		RollbackHardZoneFeatureBundleCommitments(Context, PlacedCells);
		for (int32 Index = PlacedCells.Num() - 1; Index >= 0; --Index)
		{
			const FIntVector& Cell = PlacedCells[Index];
			Context.Placements.Remove(Cell);
		}
	}

	void PopulatePlacedModuleBundleMetadata(
		const FSolveContext& Context,
		const int32 VariantIndex,
		FLayoutPlacedModule& Placement)
	{
		// Preserve rigid bundle ownership on live results while bundle-aware
		// placement remains separate from the older SourceModule-centered solve
		// path.
		Placement.CompositeModule = nullptr;
		Placement.BundleBoundsCells = FIntVector(1, 1, 1);
		Placement.OccupiedLocalCells.Reset();
		Placement.LocalCellFaceRules.Reset();
		Placement.DerivedInternalTraversalLinks.Reset();

		if (const FLayoutModuleSolveSnapshot* ModuleSnapshot = FindVariantModuleSnapshot(Context, VariantIndex))
		{
			Placement.ModuleSnapshotIndex = Context.Variants.IsValidIndex(VariantIndex) ? Context.Variants[VariantIndex].ModuleSnapshotIndex : INDEX_NONE;
			Placement.ModuleSnapshotId = ModuleSnapshot->SnapshotId;
			Placement.TemplatePath = ModuleSnapshot->Template.ToSoftObjectPath();
			Placement.OccupiedLocalCells = LayoutPlacementOccupancy::ResolveOccupiedLocalCells(Placement, ModuleSnapshot);
			Placement.BundleBoundsCells = !Placement.OccupiedLocalCells.IsEmpty()
				? LayoutPlacementOccupancy::BuildOccupiedLocalCellBounds(Placement.OccupiedLocalCells)
				: ModuleSnapshot->BoundsCells;
			for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : ModuleSnapshot->GeneratedLocalCellFaceRules)
			{
				FLayoutPlacedLocalCellFaceRuleSnapshot& PlacedCellSnapshot =
					Placement.LocalCellFaceRules.AddDefaulted_GetRef();
				PlacedCellSnapshot.LocalCell = CellSnapshot.LocalCell;
				PlacedCellSnapshot.TemplatePath = CellSnapshot.TemplatePath;
				PlacedCellSnapshot.RelativeYawRotationSteps = CellSnapshot.RelativeYawRotationSteps;
				PlacedCellSnapshot.Roles = CellSnapshot.Roles;
				PlacedCellSnapshot.SupportedCellIntents = CellSnapshot.SupportedCellIntents;
				PlacedCellSnapshot.ExposedFaceRules = CellSnapshot.ExposedFaceRules;
			}
			for (const FLayoutDerivedInternalTraversalLink& DerivedLink : ModuleSnapshot->DerivedInternalTraversalLinks)
			{
				FLayoutPlacedDerivedInternalTraversalLink& PlacedDerivedLink =
					Placement.DerivedInternalTraversalLinks.AddDefaulted_GetRef();
				PlacedDerivedLink.LinkId = DerivedLink.LinkId;
				PlacedDerivedLink.FromLocalCell = DerivedLink.FromLocalCell;
				PlacedDerivedLink.FromTraversalChannel = DerivedLink.FromTraversalChannel;
				PlacedDerivedLink.ToLocalCell = DerivedLink.ToLocalCell;
				PlacedDerivedLink.ToTraversalChannel = DerivedLink.ToTraversalChannel;
				PlacedDerivedLink.bBidirectional = DerivedLink.bBidirectional;
			}
			return;
		}

	}

	bool TryGetVariantFaceRule(
		const FSolveContext& Context,
		const int32 VariantIndex,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule)
	{
		if (!Context.Variants.IsValidIndex(VariantIndex))
		{
			return false;
		}

		const FLayoutFaceRule* Rule = Context.Variants[VariantIndex].WorldFaceRules.FindRule(Direction);
		if (Rule == nullptr)
		{
			return false;
		}

		OutFaceRule = *Rule;
		return true;
	}

	const FSolveContext::FOrientedModuleVariant* FindCandidateVariant(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		return Context.Variants.IsValidIndex(Candidate.VariantIndex) ? &Context.Variants[Candidate.VariantIndex] : nullptr;
	}

	const FSolveContext::FOrientedModuleVariant* FindPlacementVariant(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		return Context.Variants.IsValidIndex(Placement.VariantIndex) ? &Context.Variants[Placement.VariantIndex] : nullptr;
	}

	FLayoutId GetCandidateDebugName(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return Variant->ModuleDebugName;
		}

		return Candidate.ModuleSnapshotId;
	}

	/** Keep authored module-name and empty-candidate seed hashes separate from owned diagnostic text. */
	uint32 GetCandidateSeedHash(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return GetTypeHash(Variant->ModuleDebugName);
		}
		return Candidate.ModuleSnapshotId.IsNone() ? GetTypeHash(NAME_None) : GetTypeHash(Candidate.ModuleSnapshotId);
	}

	FLayoutId GetPlacementDebugName(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindPlacementVariant(Context, Placement))
		{
			return Variant->ModuleDebugName;
		}

		return Placement.ModuleSnapshotId;
	}

	FString BuildCandidateDebugName(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate);
		const FString ModuleName = GetCandidateDebugName(Context, Candidate).ToString();
		const FString EntryPrefix = (Variant != nullptr && !Variant->SourceContentEntryId.IsNone())
			? FString::Printf(TEXT("%s -> "), *Variant->SourceContentEntryId.ToString())
			: FString();
		return FString::Printf(
			TEXT("%s%s[yawSteps=%d yawDegrees=%d]"),
			*EntryPrefix,
			*ModuleName,
			Candidate.YawRotationSteps,
			Candidate.YawRotationSteps * 90);
	}

	FString BuildPlacementDebugName(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		const FSolveContext::FOrientedModuleVariant* Variant = FindPlacementVariant(Context, Placement);
		const FString ModuleName = GetPlacementDebugName(Context, Placement).ToString();
		const FString EntryPrefix = (Variant != nullptr && !Variant->SourceContentEntryId.IsNone())
			? FString::Printf(TEXT("%s -> "), *Variant->SourceContentEntryId.ToString())
			: FString();
		return FString::Printf(
			TEXT("%s%s[yawSteps=%d yawDegrees=%d]"),
			*EntryPrefix,
			*ModuleName,
			Placement.YawRotationSteps,
			Placement.YawRotationSteps * 90);
	}

	const FGameplayTagContainer& GetCandidateTraversalChannels(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return Variant->TraversalChannels;
		}

		static const FGameplayTagContainer Empty;
		return Empty;
	}

	const FGameplayTagContainer& GetPlacementTraversalChannels(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindPlacementVariant(Context, Placement))
		{
			return Variant->TraversalChannels;
		}

		static const FGameplayTagContainer Empty;
		return Empty;
	}

	const FGameplayTagContainer& GetCandidateWalkableAreas(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		return GetCandidateTraversalChannels(Context, Candidate);
	}

	const FGameplayTagContainer& GetPlacementWalkableAreas(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		return GetPlacementTraversalChannels(Context, Placement);
	}

	const TArray<FLayoutInternalAccessLink>& GetCandidateInternalAccessLinks(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return Variant->InternalAccessLinks;
		}

		static const TArray<FLayoutInternalAccessLink> Empty;
		return Empty;
	}

	const TArray<FLayoutInternalAccessLink>& GetPlacementInternalAccessLinks(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindPlacementVariant(Context, Placement))
		{
			return Variant->InternalAccessLinks;
		}

		static const TArray<FLayoutInternalAccessLink> Empty;
		return Empty;
	}

	const FLayoutCommittedEndpointAnchor* FindCommittedEndpointAnchorForCellFace(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection Direction)
	{
		return Context.CommittedEndpointAnchors.FindByPredicate(
			[&](const FLayoutCommittedEndpointAnchor& Anchor)
			{
				return Anchor.LocalCell == Cell && Anchor.FaceDirection == Direction;
			});
	}

	const TArray<FLayoutDerivedVerticalAccessContract>& GetPlacementVerticalAccessContracts(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindPlacementVariant(Context, Placement))
		{
			return Variant->VerticalAccessContracts;
		}

		static const TArray<FLayoutDerivedVerticalAccessContract> Empty;
		return Empty;
	}

	int32 GetCandidateMinTraversableNeighborFaces(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return Variant->MinTraversableNeighborFaces;
		}

		return 0;
	}

	int32 GetCandidateWeight(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return Variant->Weight;
		}

		return 1;
	}

bool CandidateExposesEntryFace(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate);
		if (Variant == nullptr)
		{
			return false;
		}

		for (const FLayoutFaceRule& FaceRule : Variant->WorldFaceRules.ToArray())
		{
			if (FaceRule.GetEffectiveConnectionTags().HasTagExact(LayoutGameplayTags::FaceEntry))
			{
				return true;
			}
		}

		return false;
	}

	/** Returns true when the face requires region-boundary placement — the solver will reject
	    placements where this face faces an interior neighbor. See FLayoutFaceRule::BoundaryRequirement. */
	bool FaceMustFaceRegionBoundary(const FLayoutFaceRule& FaceRule)
	{
		return FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceExterior
			|| FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceExteriorOrTerrainSeam;
	}

	bool FaceAllowsEmptyNeighbor(const FLayoutFaceRule& FaceRule)
	{
		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
	}

	bool CandidateHasRole(const FSolveContext& Context, const FSolveCandidate& Candidate, const ELayoutModuleRole Role)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return Variant->Roles.Contains(Role);
		}

		return false;
	}

	bool DoesCandidatePassIntentRoleRestrictions(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutCellIntent Intent,
		const FSolveCandidate& Candidate,
		FString* OutFailureReason = nullptr)
	{
		const uint8 SeedIntentMask =
			GetCompiledSeedIntentMaskForCell(
				Context,
				Cell);
		const bool bCellAcceptsEntrySeed =
			DoesIntentMaskContain(
				SeedIntentMask,
				ELayoutCellIntent::Entry)
			|| Intent == ELayoutCellIntent::Entry;
		const bool bIsTerrainSeamEntry =
			Intent == ELayoutCellIntent::Entry
			&& GetFinalizedTerrainSeamFaceMask(Context, Cell) != 0;
		if (bIsTerrainSeamEntry && !CandidateHasRole(Context, Candidate, ELayoutModuleRole::Entry))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = TEXT("terrain seam entry requires Entry-role content.");
			}
			return false;
		}

		const ELayoutCellIntent* const PlannedIntent = Context.PlannedCellIntents.Find(Cell);
		if (PlannedIntent != nullptr
			&& *PlannedIntent == ELayoutCellIntent::VerticalAccess
			&& !CandidateHasRole(Context, Candidate, ELayoutModuleRole::VerticalAccess))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = TEXT("selected VerticalAccess lower cell requires VerticalAccess-role content.");
			}
			return false;
		}

		// When the profile has an entry count constraint, entry-face modules are
		// only legal at Entry-intent cells.  When EntryCountMode is None, entry
		// modules may appear anywhere (interior doors, unrestricted perimeter).
		const bool bProfileRestrictsEntries =
			GetEffectiveProfileSnapshot(Context).EntryCountMode != ELayoutCountConstraintMode::None;
		if (bProfileRestrictsEntries && !bCellAcceptsEntrySeed && CandidateExposesEntryFace(Context, Candidate))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = TEXT("entry-face module is restricted to Entry cells while the profile entry count is active.");
			}
			return false;
		}

		const bool bCellAcceptsVerticalAccessSeed =
			DoesIntentMaskContain(
				SeedIntentMask,
				ELayoutCellIntent::VerticalAccess)
			|| Intent == ELayoutCellIntent::VerticalAccess;
		if (GetEffectiveProfileSnapshot(Context)
				.bRestrictVerticalAccessModulesToVerticalAccessCells
			&& GetEffectiveProfileSnapshot(Context).VerticalAccessCountMode !=
				ELayoutCountConstraintMode::None
			&& !bCellAcceptsVerticalAccessSeed
			&& CandidateHasRole(
				Context,
				Candidate,
				ELayoutModuleRole::VerticalAccess))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("module has a VerticalAccess role but this cell's effective seed intents do not include VerticalAccess. Profile Restrict Vertical Access Modules To Vertical Access Cells is enabled, so vertical-access modules are only legal when the planned cell or compiled carrier admits VerticalAccess on this cell."),
					static_cast<int32>(Intent));
			}
			return false;
		}

		// When the profile enforces a VerticalAccess count constraint, also reject
		// VerticalAccess-role modules on cells whose planned intent is not VerticalAccess,
		// unless the cell carries a committed endpoint anchor (continuation cells need
		// VerticalAccess regardless of planned intent). The compiled seed intent mask may
		// carry VerticalAccess from boundary-point augmentation (stepped-terrain support),
		// but the count constraint is driven by planned intent — not by seed-mask augmentation.
		if (GetEffectiveProfileSnapshot(Context)
				.bRestrictVerticalAccessModulesToVerticalAccessCells
			&& GetEffectiveProfileSnapshot(Context).VerticalAccessCountMode !=
				ELayoutCountConstraintMode::None
			&& CandidateHasRole(
				Context,
				Candidate,
				ELayoutModuleRole::VerticalAccess))
		{
			const bool bCellHasPlannedVerticalAccess =
				PlannedIntent != nullptr
				&& *PlannedIntent == ELayoutCellIntent::VerticalAccess;
			const bool bCellHasCommittedEndpoint =
				FindCommittedEndpointAnchorForCellFace(
					Context,
					Cell,
					ELayoutFaceDirection::PosX) != nullptr
				|| FindCommittedEndpointAnchorForCellFace(
					Context,
					Cell,
					ELayoutFaceDirection::NegX) != nullptr
				|| FindCommittedEndpointAnchorForCellFace(
					Context,
					Cell,
					ELayoutFaceDirection::PosY) != nullptr
				|| FindCommittedEndpointAnchorForCellFace(
					Context,
					Cell,
					ELayoutFaceDirection::NegY) != nullptr
				|| FindCommittedEndpointAnchorForCellFace(
					Context,
					Cell,
					ELayoutFaceDirection::PosZ) != nullptr
				|| FindCommittedEndpointAnchorForCellFace(
					Context,
					Cell,
					ELayoutFaceDirection::NegZ) != nullptr;
			if (!bCellHasPlannedVerticalAccess && !bCellHasCommittedEndpoint)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("module has a VerticalAccess role but this cell's planned intent is %d, not VerticalAccess, and the cell has no committed endpoint anchor. Profile VerticalAccess count constraint limits VerticalAccess-role modules to cells whose planned intent is VerticalAccess."),
						static_cast<int32>(PlannedIntent != nullptr ? *PlannedIntent : ELayoutCellIntent::Interior));
				}
				return false;
			}
		}

		const ELayoutCellIntent* LowerPlannedIntent =
			Context.PlannedCellIntents.Find(Cell - FIntVector(0, 0, 1));
		const bool bOwnsSelectedVerticalInterface =
			Intent == ELayoutCellIntent::VerticalAccess
			|| (LowerPlannedIntent != nullptr
				&& *LowerPlannedIntent == ELayoutCellIntent::VerticalAccess);
		if (!bOwnsSelectedVerticalInterface)
		{
			for (const ELayoutFaceDirection VerticalDirection :
				{ ELayoutFaceDirection::PosZ, ELayoutFaceDirection::NegZ })
			{
				FLayoutFaceRule VerticalFaceRule;
				if (TryGetCandidateFaceRule(Context, Candidate, VerticalDirection, VerticalFaceRule)
					&& !VerticalFaceRule.ConnectedTraversalChannels.IsEmpty())
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = TEXT("candidate exposes vertical traversal on a cell without a selected VerticalAccess interface.");
					}
					return false;
				}
			}
		}

		return true;
	}

	/** Validates every projected composite cell against frozen finalized-cell authority before atomic placement. */
	bool DoesCandidateProjectedOccupiedBundleFitContext(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate,
		FString* OutFailureReason = nullptr)
	{
		const FSolveContext::FOrientedModuleVariant* const CandidateVariant =
			FindCandidateVariant(Context, Candidate);
		const FLayoutModuleSolveSnapshot* const ModuleSnapshot =
			FindVariantModuleSnapshot(Context, Candidate.VariantIndex);
		if (ModuleSnapshot == nullptr)
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = TEXT("candidate is missing its module snapshot while projecting occupied bundle cells.");
			}
			return false;
		}

		TArray<FIntVector> OccupiedLocalCells =
			LayoutPlacementOccupancy::ResolveSnapshotOccupiedLocalCells(ModuleSnapshot);
		if (OccupiedLocalCells.IsEmpty())
		{
			OccupiedLocalCells = {FIntVector::ZeroValue};
		}

		TOptional<int32> BundleTerrainStage;
		for (const FIntVector& LocalCell : OccupiedLocalCells)
		{
			const FIntVector OccupiedWorldCell =
				LayoutPlacementOccupancy::ProjectLocalCellToWorld(
					Cell,
					LocalCell,
					ModuleSnapshot->BoundsCells,
					Candidate.YawRotationSteps);
			const FLayoutLocalCellFaceRuleSnapshot* const LocalCellSnapshot =
				ModuleSnapshot->GeneratedLocalCellFaceRules.FindByPredicate(
					[&LocalCell](const FLayoutLocalCellFaceRuleSnapshot& CandidateCellSnapshot)
					{
						return CandidateCellSnapshot.LocalCell == LocalCell;
					});
			if (OccupiedLocalCells.Num() > 1 && LocalCellSnapshot == nullptr)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("projected composite local cell %s is missing its frozen local-cell descriptor."),
						*LocalCell.ToString());
				}
				return false;
			}

			if (Context.ReservedOpenReservationIndexByCell.Contains(OccupiedWorldCell)
				|| Context.RemovedFinalizedCells.Contains(OccupiedWorldCell))
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("projected occupied bundle local cell %s resolves to reserved or removed finalized cell %s for root %s."),
						*LocalCell.ToString(),
						*OccupiedWorldCell.ToString(),
						*Cell.ToString());
				}
				return false;
			}

			if (!DoesContextAdmitProjectedOccupiedWorldCell(Context, OccupiedWorldCell))
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("projected occupied bundle local cell %s resolves outside the current planned-cell set at %s for root %s."),
						*LocalCell.ToString(),
						*OccupiedWorldCell.ToString(),
						*Cell.ToString());
				}
				return false;
			}

			const ELayoutCellIntent* const ProjectedIntent =
				Context.PlannedCellIntents.Find(OccupiedWorldCell);
			if (ProjectedIntent == nullptr)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("projected occupied bundle local cell %s lacks frozen finalized policy authority at external cell %s."),
						*LocalCell.ToString(),
						*OccupiedWorldCell.ToString());
				}
				return false;
			}
			// Every admitted projected cell has a current finalized intent record.
			if (LocalCellSnapshot != nullptr
					&& !LocalCellSnapshot->Roles.IsEmpty()
					&& *ProjectedIntent == ELayoutCellIntent::VerticalAccess
					&& !LocalCellSnapshot->Roles.Contains(ELayoutModuleRole::VerticalAccess))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle local cell %s lacks the VerticalAccess role required at finalized cell %s."),
							*LocalCell.ToString(),
							*OccupiedWorldCell.ToString());
					}
					return false;
				}
				if (LocalCellSnapshot != nullptr
					&& !LocalCellSnapshot->Roles.IsEmpty()
					&& *ProjectedIntent == ELayoutCellIntent::Entry
					&& !LocalCellSnapshot->Roles.Contains(ELayoutModuleRole::Entry))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle local cell %s lacks the Entry role required at committed cell %s."),
							*LocalCell.ToString(),
							*OccupiedWorldCell.ToString());
					}
					return false;
				}

				const uint8 ProjectedSeedIntentMask = GetCompiledSeedIntentMaskForCell(Context, OccupiedWorldCell);
				const bool bLocalCellSupportsProjectedSeed = LocalCellSnapshot == nullptr
					|| LocalCellSnapshot->SupportedCellIntents.IsEmpty()
					|| LocalCellSnapshot->SupportedCellIntents.ContainsByPredicate(
						[ProjectedSeedIntentMask](const ELayoutCellIntent LocalIntent)
						{
							return DoesIntentMaskContain(ProjectedSeedIntentMask, LocalIntent);
						});
				if (!bLocalCellSupportsProjectedSeed)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle local cell %s does not support compiled intent mask 0x%02x for finalized intent %d at %s."),
							*LocalCell.ToString(),
							ProjectedSeedIntentMask,
							static_cast<int32>(*ProjectedIntent),
							*OccupiedWorldCell.ToString());
					}
					return false;
				}
				if (!LayoutProfileSolverInternal::DoesModuleMatchResolvedPlacementZone(
						Context,
						OccupiedWorldCell,
						CandidateVariant != nullptr
							? CandidateVariant->PlacementZone
							: ModuleSnapshot->PlacementZone))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle local cell %s violates placement zone at finalized cell %s."),
							*LocalCell.ToString(),
							*OccupiedWorldCell.ToString());
					}
					return false;
				}
				const bool bProjectedCellMatchesLevelPolicy = DoesCellMatchLevelPlacementPolicy(
					OccupiedWorldCell,
					LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(Context, OccupiedWorldCell),
					Context.TopPlannedLevelByXY,
					ModuleSnapshot->LevelPlacementPolicy,
					ModuleSnapshot->SpecificLevel);
				if (!bProjectedCellMatchesLevelPolicy)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle local cell %s violates authored level policy at finalized cell %s."),
							*LocalCell.ToString(),
							*OccupiedWorldCell.ToString());
					}
					return false;
				}

			if (Context.bUsesSteppedTerrainContract)
			{
				const LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord* const ViewRecord =
					Context.FinalizedCellsByPhysicalCell.Find(OccupiedWorldCell);
				if (ViewRecord == nullptr || !ViewRecord->bHasTerrainStage)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle local cell %s lacks frozen terrain-stage authority at %s."),
							*LocalCell.ToString(),
							*OccupiedWorldCell.ToString());
					}
					return false;
				}
				if (!BundleTerrainStage.IsSet())
				{
					BundleTerrainStage = ViewRecord->TerrainStageIndex;
				}
				else if (BundleTerrainStage.GetValue() != ViewRecord->TerrainStageIndex)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle local cell %s crosses frozen terrain stages %d and %d at %s."),
							*LocalCell.ToString(),
							BundleTerrainStage.GetValue(),
							ViewRecord->TerrainStageIndex,
							*OccupiedWorldCell.ToString());
					}
					return false;
				}
			}

			if (const FSolveContext::FSolvePlacement* ExistingOccupiedCell =
					FindPlacementOrFixedNeighbor(Context, OccupiedWorldCell))
			{
				if (FSolveContext::IsOccupiedPlacement(*ExistingOccupiedCell))
				{
					if (AreSameOccupiedBundlePlacement(
						MakeRootSolvePlacement(
							Context,
							Cell,
							Candidate.YawRotationSteps,
							Candidate.VariantIndex),
						*ExistingOccupiedCell))
					{
						continue;
					}

					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("projected occupied bundle cell %s is already owned by solved placement root %s."),
							*OccupiedWorldCell.ToString(),
							*ExistingOccupiedCell->BundleRootCell.ToString());
					}
					return false;
				}

				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("projected occupied bundle cell %s already has an explicit empty placement."),
						*OccupiedWorldCell.ToString());
				}
				return false;
			}
		}

		return true;
	}

	bool TryGetCandidateFaceRule(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule)
	{
		return LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
			Context,
			Candidate,
			FIntVector::ZeroValue,
			Direction,
			OutFaceRule);
	}

	bool DoesFaceRuleSatisfyCommittedEndpointAnchor(
		const FLayoutFaceRule& FaceRule,
		const ELayoutFaceDirection Direction,
		const int32 YawRotationSteps,
		const FLayoutCommittedEndpointAnchor& EndpointAnchor,
		FString* OutFailureReason = nullptr)
	{
		auto BuildFaceDebugStringForAnchor = [Direction, YawRotationSteps]()
		{
			const UEnum* FaceDirectionEnum = StaticEnum<ELayoutFaceDirection>();
			const FString DirectionText = FaceDirectionEnum != nullptr
				? FaceDirectionEnum->GetNameStringByValue(
					static_cast<int64>(Direction))
				: FString::Printf(TEXT("%d"), static_cast<int32>(Direction));
			return FString::Printf(
				TEXT("world %s yawSteps=%d yawDegrees=%d"),
				*DirectionText,
				YawRotationSteps,
				YawRotationSteps * 90);
		};
		auto BuildTagsDebugString = [](const FGameplayTagContainer& Tags)
		{
			TArray<FString> TagStrings;
			for (const FGameplayTag& Tag : Tags)
			{
				TagStrings.Add(Tag.ToString());
			}
			TagStrings.Sort();
			return TagStrings.IsEmpty()
				? FString(TEXT("<none>"))
				: FString::Join(TagStrings, TEXT(","));
		};

		if (!FaceRule.GetEffectiveConnectionTags().HasTagExact(
			EndpointAnchor.ConnectionTag))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("%s face does not satisfy committed endpoint anchor '%s'. RequiredConnection=[%s] CandidateConnection=[%s]"),
					*BuildFaceDebugStringForAnchor(),
					*EndpointAnchor.CommitmentId.ToString(),
					*BuildTagsDebugString(
						FGameplayTagContainer(EndpointAnchor.ConnectionTag)),
					*BuildTagsDebugString(
						FaceRule.GetEffectiveConnectionTags()));
			}
			return false;
		}

		if (!FaceRule.GetEffectiveAllowedConnectionTags().HasAnyExact(
			EndpointAnchor.AllowedConnectionTags))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("%s face does not allow any committed endpoint neighbor tags for anchor '%s'. RequiredAllowed=[%s] CandidateAllowed=[%s]"),
					*BuildFaceDebugStringForAnchor(),
					*EndpointAnchor.CommitmentId.ToString(),
					*BuildTagsDebugString(
						EndpointAnchor.AllowedConnectionTags),
					*BuildTagsDebugString(
						FaceRule.GetEffectiveAllowedConnectionTags()));
			}
			return false;
		}

		if (!EndpointAnchor.TraversalChannels.IsEmpty()
			&& !FaceRule.ConnectedTraversalChannels.HasAnyExact(
				EndpointAnchor.TraversalChannels))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("%s face does not expose the committed traversal channels for anchor '%s'. RequiredTraversal=[%s] CandidateTraversal=[%s]"),
					*BuildFaceDebugStringForAnchor(),
					*EndpointAnchor.CommitmentId.ToString(),
					*BuildTagsDebugString(
						EndpointAnchor.TraversalChannels),
					*BuildTagsDebugString(
						FaceRule.ConnectedTraversalChannels));
			}
			return false;
		}

		return true;
	}

	enum class EFilledNeighborFaceCompatibilityFailure : uint8
	{
		None,
		SourceOccupancy,
		NeighborOccupancy,
		ConnectionTags,
		TraversalChannels,
		YawMismatch
	};

	bool DoesFilledNeighborFacePairSatisfyInterfaceContract(
		const FLayoutFaceRule& SourceFaceRule,
		const int32 SourceYawRotationSteps,
		const FLayoutFaceRule& NeighborFaceRule,
		const int32 NeighborYawRotationSteps,
		EFilledNeighborFaceCompatibilityFailure* OutFailure = nullptr)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = EFilledNeighborFaceCompatibilityFailure::None;
		}

		if (!IsFaceCompatibleWithOccupancy(SourceFaceRule, true, true))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EFilledNeighborFaceCompatibilityFailure::SourceOccupancy;
			}
			return false;
		}

		if (!IsFaceCompatibleWithOccupancy(NeighborFaceRule, true, true))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EFilledNeighborFaceCompatibilityFailure::NeighborOccupancy;
			}
			return false;
		}

		if (!AreConnectionTagsCompatible(SourceFaceRule, NeighborFaceRule)
			|| !AreConnectionTagsCompatible(NeighborFaceRule, SourceFaceRule))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EFilledNeighborFaceCompatibilityFailure::ConnectionTags;
			}
			return false;
		}

		if ((RequiresWalkableFilledCompatibility(SourceFaceRule)
				|| RequiresWalkableFilledCompatibility(NeighborFaceRule))
			&& !HasSharedConnectedWalkableArea(
				SourceFaceRule,
				NeighborFaceRule))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure =
					EFilledNeighborFaceCompatibilityFailure::TraversalChannels;
			}
			return false;
		}

		if (!AreSolverYawRotationsCompatible(
				SourceFaceRule,
				SourceYawRotationSteps,
				NeighborFaceRule,
				NeighborYawRotationSteps))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EFilledNeighborFaceCompatibilityFailure::YawMismatch;
			}
			return false;
		}

		return true;
	}

	const TCHAR* ToDebugString(const EFilledNeighborFaceCompatibilityFailure Failure)
	{
		switch (Failure)
		{
		case EFilledNeighborFaceCompatibilityFailure::None:
			return TEXT("None");
		case EFilledNeighborFaceCompatibilityFailure::SourceOccupancy:
			return TEXT("SourceOccupancy");
		case EFilledNeighborFaceCompatibilityFailure::NeighborOccupancy:
			return TEXT("NeighborOccupancy");
		case EFilledNeighborFaceCompatibilityFailure::ConnectionTags:
			return TEXT("ConnectionTags");
		case EFilledNeighborFaceCompatibilityFailure::TraversalChannels:
			return TEXT("TraversalChannels");
		case EFilledNeighborFaceCompatibilityFailure::YawMismatch:
			return TEXT("YawMismatch");
		default:
			return TEXT("Unknown");
		}
	}

	enum class EIncomingBoundaryFaceCompatibilityFailure : uint8
	{
		None,
		BoundaryFacing,
		SourceOccupancy,
		ConnectionTags,
		TraversalChannels,
		YawMismatch
	};

	bool DoesIncomingBoundaryFaceSatisfyInterfaceContract(
		const FLayoutFaceRule& SourceFaceRule,
		const int32 SourceYawRotationSteps,
		const FLayoutFaceRule& BoundaryFaceRule,
		const int32 BoundaryYawRotationSteps,
		const bool bBoundaryFilled,
		const bool bBoundaryRequiresBoundaryFacing,
		const bool bBoundaryIsSupportOnly,
		const bool bSourceMustFaceExterior,
		EIncomingBoundaryFaceCompatibilityFailure* OutFailure = nullptr)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = EIncomingBoundaryFaceCompatibilityFailure::None;
		}

		if (bBoundaryRequiresBoundaryFacing
			&& !bBoundaryIsSupportOnly
			&& !bSourceMustFaceExterior)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EIncomingBoundaryFaceCompatibilityFailure::BoundaryFacing;
			}
			return false;
		}

		if (!IsFaceCompatibleWithOccupancy(
				SourceFaceRule,
				bBoundaryFilled,
				bBoundaryFilled))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EIncomingBoundaryFaceCompatibilityFailure::SourceOccupancy;
			}
			return false;
		}

		if (bBoundaryFilled
			&& !bBoundaryIsSupportOnly
			&& (!AreConnectionTagsCompatible(SourceFaceRule, BoundaryFaceRule)
				|| !AreConnectionTagsCompatible(
					BoundaryFaceRule,
					SourceFaceRule)))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EIncomingBoundaryFaceCompatibilityFailure::ConnectionTags;
			}
			return false;
		}

		if (bBoundaryFilled
			&& (RequiresWalkableFilledCompatibility(SourceFaceRule)
				|| RequiresWalkableFilledCompatibility(BoundaryFaceRule))
			&& !HasSharedConnectedWalkableArea(
				SourceFaceRule,
				BoundaryFaceRule))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure =
					EIncomingBoundaryFaceCompatibilityFailure::TraversalChannels;
			}
			return false;
		}

		if (bBoundaryFilled
			&& !AreSolverYawRotationsCompatible(
				SourceFaceRule,
				SourceYawRotationSteps,
				BoundaryFaceRule,
				BoundaryYawRotationSteps))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EIncomingBoundaryFaceCompatibilityFailure::YawMismatch;
			}
			return false;
		}

		return true;
	}

	enum class EOpenSpaceFaceCompatibilityFailure : uint8
	{
		None,
		BoundaryFacing,
		RequiresEmptyReservedExternal,
		SourceOccupancy
	};

	bool DoesOpenSpaceFaceSatisfyInterfaceContract(
		const FLayoutFaceRule& SourceFaceRule,
		const bool bSourceMustFaceExterior,
		const bool bTreatAsReservedExternal,
		const ELayoutFaceDirection Direction,
		EOpenSpaceFaceCompatibilityFailure* OutFailure = nullptr)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = EOpenSpaceFaceCompatibilityFailure::None;
		}

		if (bTreatAsReservedExternal
			&& Direction == ELayoutFaceDirection::PosZ
			&& RequiresFilledNeighborOccupancy(SourceFaceRule))
		{
			return true;
		}

		// Occupancy policy handles optional boundary support — BoundaryRequirement = false
		// is not a rejection (the face can still face open space if occupancy allows it).

		if (bTreatAsReservedExternal
			&& SourceFaceRule.OccupancyPolicy ==
				ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure =
					EOpenSpaceFaceCompatibilityFailure::
						RequiresEmptyReservedExternal;
			}
			return false;
		}

		if (!IsFaceCompatibleWithOccupancy(SourceFaceRule, false, false))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = EOpenSpaceFaceCompatibilityFailure::SourceOccupancy;
			}
			return false;
		}

		return true;
	}

	enum class EPlannedNeighborFaceCompatibilityFailure : uint8
	{
		None,
		RequiresEmptyButNeighborMustStayFilled,
		RequiresEmptyButExternalContinuationCannotBeProvenEmpty
	};

	/** Authored ground remains terrain after physical shifting; upper residual air is not ground access. */
	bool IsPreservedGroundDestination(const FSolveContext& Context, const FIntVector& Cell)
	{
		const FLayoutPlannedCell* Planned = Context.OwningTopologyCellsByPhysicalCell.Find(Cell);
		return Planned != nullptr && !Planned->bIsBridgeCell
			&& Context.TerrainResidualRuleIdByCell.Contains(Cell)
			&& LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(Context, Cell) == 0;
	}

	/** Generated extensions of preserved air are not independent ascent destinations; real ground and normal structure remain eligible. */
	bool IsSparseHostLandingDestination(const FSolveContext& Context, const FIntVector& Cell)
	{
		if (Context.TerrainResidualRuleIdByCell.Contains(Cell))
		{
			return IsPreservedGroundDestination(Context, Cell);
		}
		const FLayoutPlannedCell* Planned = Context.OwningTopologyCellsByPhysicalCell.Find(Cell);
		if (Planned == nullptr || !Planned->bIsBridgeCell || Planned->VerticalAccessLandingContactMask != 0)
		{
			return Planned != nullptr;
		}
		// Bridge cells are intentionally absent from the residual map. Resolve the
		// authored column source instead of treating that absence as normal-zone authority.
		for (const int32 Step : {-1, 1})
		{
			FIntVector Source = Cell + FIntVector(0, 0, Step);
			while (const FLayoutPlannedCell* Neighbor = Context.OwningTopologyCellsByPhysicalCell.Find(Source))
			{
				if (!Neighbor->bIsBridgeCell)
				{
					if (ResolvePlannedCellModuleLevel(*Neighbor) == ResolvePlannedCellModuleLevel(*Planned)
						&& Context.TerrainResidualRuleIdByCell.Contains(Source)) return false;
					break;
				}
				Source.Z += Step;
			}
		}
		return true;
	}

	/** Only uncommitted generated deck offers admit empty occupancy; exact support and route authority wins. */
	bool CanTopBridgeOfferRemainEmpty(const FSolveContext& Context, const FIntVector& Cell)
	{
		const FLayoutPlannedCell* Planned = Context.OwningTopologyCellsByPhysicalCell.Find(Cell);
		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		return Planned != nullptr && Intent != nullptr
			&& LayoutProfileSolverInternal::CanTopBridgeOfferRemainEmpty(*Planned, *Intent,
				Context.CandidateDomainRestrictionsByCell.Contains(Cell)
					|| LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, Cell));
	}

	bool DoesPlannedNeighborFaceSatisfyInterfaceContract(
		const FSolveContext& Context,
		const FLayoutFaceRule& SourceFaceRule,
		const FIntVector& NeighborCell,
		const bool bNeighborIsExternalContinuation,
		EPlannedNeighborFaceCompatibilityFailure* OutFailure = nullptr)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = EPlannedNeighborFaceCompatibilityFailure::None;
		}

		if (SourceFaceRule.OccupancyPolicy !=
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		{
			return true;
		}

		if (bNeighborIsExternalContinuation)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure =
					EPlannedNeighborFaceCompatibilityFailure::
						RequiresEmptyButExternalContinuationCannotBeProvenEmpty;
			}
			return false;
		}

		// Preserved cells remain in owning topology before sparse projection settles occupancy.
		// They admit empty occupancy; selected support/routes lose this residual attribution,
		// and fixed-neighbor admission still enforces the actual placed face contract.
		if (Context.TerrainResidualRuleIdByCell.Contains(NeighborCell)
			|| CanTopBridgeOfferRemainEmpty(Context, NeighborCell))
		{
			return true;
		}

		if (OutFailure != nullptr)
		{
			*OutFailure =
				EPlannedNeighborFaceCompatibilityFailure::
					RequiresEmptyButNeighborMustStayFilled;
		}
		return false;
	}

	const TCHAR* ToDebugString(const ELayoutCellIntent Intent);
	const TCHAR* ToDebugString(const ELayoutFaceOccupancyPolicy OccupancyPolicy);
	FString TagsToDebugString(const FGameplayTagContainer& Tags);
	FString BuildFaceDebugString(
		const ELayoutFaceDirection WorldDirection,
		const int32 YawRotationSteps);
	int32 NormalizeSolverYawRotationSteps(const int32 YawRotationSteps);

	/** Shared compiled-face evaluator for occupied candidates and committed placements. */
	template<typename TryGetSubjectFaceRuleFn, typename ShouldIgnoreNeighborPlacementFn>
	bool DoesOccupiedSubjectSatisfyCompiledFaceInterfaces(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const int32 SubjectYawRotationSteps,
		const FLayoutModuleFaceRules* SubjectWorldFaceRules,
		const FString& SubjectDebugName,
		TryGetSubjectFaceRuleFn&& TryGetSubjectFaceRule,
		ShouldIgnoreNeighborPlacementFn&& ShouldIgnoreNeighborPlacement,
		FString* OutFailureReason = nullptr)
	{
		// Collect child cell -> (parent face direction -> parent face rule) for
		// collective boundary validation after the per-face loop.
		TMap<FIntVector, TMap<ELayoutFaceDirection, FLayoutFaceRule>> ChildFaceChecksByCell;

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction =
				static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface == nullptr)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("missing compiled face-interface row for occupied cell %s %s."),
						*Cell.ToString(),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps));
				}
				return false;
			}

			const FIntVector NeighborCell =
				Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const FSolveContext::FSolvePlacement* ExistingNeighborPlacement =
				FindPlacementOrFixedNeighbor(Context, NeighborCell);
			if (ExistingNeighborPlacement != nullptr
				&& ShouldIgnoreNeighborPlacement(*ExistingNeighborPlacement))
			{
				continue;
			}

			FLayoutFaceRule SubjectFaceRule;
			if (!TryGetSubjectFaceRule(Direction, SubjectFaceRule))
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("missing %s face rule on occupied cell %s for %s"),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
						*Cell.ToString(),
						SubjectDebugName.IsEmpty()
							? TEXT("<unknown>")
							: *SubjectDebugName);
				}
				return false;
			}

			if (const FLayoutCommittedEndpointAnchor* EndpointAnchor =
				ResolveCompiledEndpointAnchor(Context, *FaceInterface))
			{
				if (!DoesFaceRuleSatisfyCommittedEndpointAnchor(
					SubjectFaceRule,
					Direction,
					SubjectYawRotationSteps,
					*EndpointAnchor,
					OutFailureReason))
				{
					return false;
				}
			}

			const bool bSubjectRequiresExterior =
				SubjectFaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceExterior;
			const bool bSubjectAllowsExteriorOrTerrainSeam =
				SubjectFaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceExteriorOrTerrainSeam;
			const bool bSubjectAllowsTerrainSeam =
				SubjectFaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam
				|| bSubjectAllowsExteriorOrTerrainSeam;
			const bool bCellHasTerrainSeamFace = LayoutFaceMaskContainsDirection(
				LayoutProfileSolverInternal::GetFinalizedTerrainSeamFaceMask(Context, Cell),
				Direction);
			const bool bSubjectMustFaceExterior = bSubjectRequiresExterior
				|| (bSubjectAllowsExteriorOrTerrainSeam && !bCellHasTerrainSeamFace);
			if (bSubjectAllowsTerrainSeam && bCellHasTerrainSeamFace)
			{
				const bool bRequiresFilledNeighbor =
					SubjectFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
				const bool bAllowsOptionalFilledNeighbor =
					SubjectFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
					|| SubjectFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor;
				const bool bFacesPlannedNeighbor =
					FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::InternalPlannedNeighbor
					|| FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::ExternalPlannedNeighbor
					|| FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::FixedFilledNeighbor;
				if (!IsHorizontalDirection(Direction)
					|| (!bRequiresFilledNeighbor && !bAllowsOptionalFilledNeighbor)
					|| !bFacesPlannedNeighbor)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("%s face requires a horizontal planned terrain seam with compatible filled-neighbor occupancy at cell %s."),
							*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
							*Cell.ToString());
					}
					return false;
				}
			}
			else if (bCellHasTerrainSeamFace
				&& SubjectFaceRule.BoundaryRequirement != ELayoutFaceBoundaryRequirement::Any)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("%s face at terrain seam cell %s must use Any, MustFaceTerrainSeam, or MustFaceExteriorOrTerrainSeam."),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
						*Cell.ToString());
				}
				return false;
			}

			// A generated extension is not owning interior when omitted. Admission may
			// choose that empty alternative; completed-map validation sees actual occupancy
			// and still rejects a filled deck over an exterior-facing cap. Preserved
			// courtyard cells never acquire exterior authority through empty occupancy.
			const FLayoutPlannedCell* NeighborTopology = Context.OwningTopologyCellsByPhysicalCell.Find(NeighborCell);
			const bool bFacesOmittedTopBridge = Direction == ELayoutFaceDirection::PosZ
				&& NeighborTopology != nullptr && NeighborTopology->bIsBridgeCell && NeighborTopology->bIsTopBridgeOffer
				&& ExistingNeighborPlacement != nullptr && !FSolveContext::IsOccupiedPlacement(*ExistingNeighborPlacement);
			const bool bMayOmitTopBridge = Direction == ELayoutFaceDirection::PosZ
				&& ExistingNeighborPlacement == nullptr
				&& FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::InternalPlannedNeighbor
				&& CanTopBridgeOfferRemainEmpty(Context, NeighborCell);
			if (bSubjectMustFaceExterior && (bFacesOmittedTopBridge || bMayOmitTopBridge)
				&& IsFaceCompatibleWithOccupancy(SubjectFaceRule, false, false))
			{
				continue;
			}
			const bool bFacesTrueExterior = FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::OuterBoundary
				|| FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::ReservedExternalCell
				|| bFacesOmittedTopBridge;
			if (SubjectFaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceInterior
				&& bFacesTrueExterior)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("%s face requires owning-region interior but faces true exterior at %s."),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
						*NeighborCell.ToString());
				}
				return false;
			}
			if (SubjectFaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam
				&& !bCellHasTerrainSeamFace)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("%s face requires a terrain seam at cell %s."),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
						*Cell.ToString());
				}
				return false;
			}

			// Negotiation applies to its incoming/contact faces, not every interior face
			// in a request carrying child or endpoint authority. Interior remains interior.
			if (bSubjectMustFaceExterior
				&& (FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::FixedFilledNeighbor
					|| (FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::ExternalPlannedNeighbor
						&& Direction != ELayoutFaceDirection::PosZ
						&& Direction != ELayoutFaceDirection::NegZ)
					|| FaceInterface->NeighborKind == ESolveCellFaceNeighborKind::InternalPlannedNeighbor))
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("%s face requires region-boundary placement but faces an interior filled neighbor at %s. Modules with BoundaryRequirement can only be placed where this face faces the region perimeter (OuterBoundary, ReservedExternal, or IncomingBoundary)."),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
						*NeighborCell.ToString());
				}
				return false;
			}

			switch (FaceInterface->NeighborKind)
			{
			case ESolveCellFaceNeighborKind::FixedFilledNeighbor:
			case ESolveCellFaceNeighborKind::FixedEmptyNeighbor:
			{
				if (ExistingNeighborPlacement == nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("compiled fixed-neighbor interface for occupied cell %s %s did not resolve a live fixed placement."),
							*Cell.ToString(),
							*BuildFaceDebugString(Direction, SubjectYawRotationSteps));
					}
					return false;
				}

				if (Cell.Z != NeighborCell.Z
					&& FSolveContext::IsOccupiedPlacement(*ExistingNeighborPlacement)
					&& !ShouldAuditAdjacencyBetweenCells(
						Context,
						Cell,
						NeighborCell))
				{
					if (Direction != ELayoutFaceDirection::PosZ
						|| !RequiresFilledNeighborOccupancy(SubjectFaceRule))
					{
						continue;
					}
				}

				if (!FSolveContext::IsOccupiedPlacement(*ExistingNeighborPlacement))
				{
					if (!IsFaceCompatibleWithOccupancy(
						SubjectFaceRule,
						false,
						false))
					{
						if (OutFailureReason != nullptr)
						{
							*OutFailureReason = FString::Printf(
								TEXT("%s face cannot touch an explicitly empty neighbor. Occupancy=%s ConnectionTag=[%s] AllowedConnectionTags=[%s]"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								ToDebugString(
									SubjectFaceRule.OccupancyPolicy),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveConnectionTags()),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveAllowedConnectionTags()));
						}
						return false;
					}

					continue;
				}

				if (!IsFaceCompatibleWithOccupancy(
					SubjectFaceRule,
					true,
					true))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("%s face cannot touch filled neighbor module '%s'. Occupancy=%s ConnectionTag=[%s] AllowedConnectionTags=[%s]"),
							*BuildFaceDebugString(
								Direction,
								SubjectYawRotationSteps),
							*BuildPlacementDebugName(
								Context,
								*ExistingNeighborPlacement),
							ToDebugString(
								SubjectFaceRule.OccupancyPolicy),
							*TagsToDebugString(
								SubjectFaceRule.GetEffectiveConnectionTags()),
							*TagsToDebugString(
								SubjectFaceRule.GetEffectiveAllowedConnectionTags()));
					}
					return false;
				}

				FLayoutFaceRule NeighborFaceRule;
				if (!TryGetPlacementFaceRule(
						Context,
						*ExistingNeighborPlacement,
						FLayoutDirectionUtils::GetOpposite(Direction),
						NeighborFaceRule))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("filled neighbor module '%s' is missing opposite %s face rule"),
							*BuildPlacementDebugName(
								Context,
								*ExistingNeighborPlacement),
							*BuildFaceDebugString(
								FLayoutDirectionUtils::GetOpposite(Direction),
								ExistingNeighborPlacement->YawRotationSteps));
					}
					return false;
				}

				if (!IsFaceCompatibleWithOccupancy(
					NeighborFaceRule,
					true,
					true))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("filled neighbor module '%s' opposite %s face cannot touch this filled candidate. NeighborOccupancy=%s NeighborConnectionTag=[%s] NeighborAllowedConnectionTags=[%s]"),
							*BuildPlacementDebugName(
								Context,
								*ExistingNeighborPlacement),
							*BuildFaceDebugString(
								FLayoutDirectionUtils::GetOpposite(Direction),
								ExistingNeighborPlacement->YawRotationSteps),
							ToDebugString(
								NeighborFaceRule.OccupancyPolicy),
							*TagsToDebugString(
								NeighborFaceRule.GetEffectiveConnectionTags()),
							*TagsToDebugString(
								NeighborFaceRule.GetEffectiveAllowedConnectionTags()));
					}
					return false;
				}

				EFilledNeighborFaceCompatibilityFailure FilledNeighborFailure =
					EFilledNeighborFaceCompatibilityFailure::None;
				if (!DoesFilledNeighborFacePairSatisfyInterfaceContract(
					SubjectFaceRule,
					SubjectYawRotationSteps,
					NeighborFaceRule,
					ExistingNeighborPlacement->YawRotationSteps,
					&FilledNeighborFailure))
				{
					if (OutFailureReason != nullptr)
					{
						switch (FilledNeighborFailure)
						{
						case EFilledNeighborFaceCompatibilityFailure::ConnectionTags:
							*OutFailureReason = FString::Printf(
								TEXT("%s connection tag compatibility failed with neighbor '%s' opposite %s. CandidateAllowed=[%s] NeighborConnection=[%s] NeighborAllowed=[%s] CandidateConnection=[%s]"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*BuildPlacementDebugName(
									Context,
									*ExistingNeighborPlacement),
								*BuildFaceDebugString(
									FLayoutDirectionUtils::GetOpposite(Direction),
									ExistingNeighborPlacement->YawRotationSteps),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveAllowedConnectionTags()),
								*TagsToDebugString(
									NeighborFaceRule.GetEffectiveConnectionTags()),
								*TagsToDebugString(
									NeighborFaceRule.GetEffectiveAllowedConnectionTags()),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveConnectionTags()));
							break;
						case EFilledNeighborFaceCompatibilityFailure::TraversalChannels:
							*OutFailureReason = FString::Printf(
								TEXT("%s face requires a walkable filled neighbor with '%s' opposite %s, but the two faces share no ConnectedTraversalChannels. CandidateTraversal=[%s] NeighborTraversal=[%s]"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*BuildPlacementDebugName(
									Context,
									*ExistingNeighborPlacement),
								*BuildFaceDebugString(
									FLayoutDirectionUtils::GetOpposite(Direction),
									ExistingNeighborPlacement->YawRotationSteps),
								*TagsToDebugString(
									SubjectFaceRule.ConnectedTraversalChannels),
								*TagsToDebugString(
									NeighborFaceRule.ConnectedTraversalChannels));
							break;
						case EFilledNeighborFaceCompatibilityFailure::YawMismatch:
							*OutFailureReason = FString::Printf(
								TEXT("%s face requires matching yaw with neighbor '%s' opposite %s. CandidateYaw=%d NeighborYaw=%d"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*BuildPlacementDebugName(
									Context,
									*ExistingNeighborPlacement),
								*BuildFaceDebugString(
									FLayoutDirectionUtils::GetOpposite(Direction),
									ExistingNeighborPlacement->YawRotationSteps),
								NormalizeSolverYawRotationSteps(
									SubjectYawRotationSteps),
								NormalizeSolverYawRotationSteps(
									ExistingNeighborPlacement->YawRotationSteps));
							break;
						default:
							break;
						}
					}
					return false;
				}

				continue;
			}
			case ESolveCellFaceNeighborKind::TerrainBackedFilledNeighbor:
			{
				const bool bOccupancyOnlyFace = SubjectFaceRule.ConnectedTraversalChannels.IsEmpty()
					&& (SubjectFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
						|| SubjectFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
						|| SubjectFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor);
				if (SubjectFaceRule.BoundaryRequirement != ELayoutFaceBoundaryRequirement::Any
					|| !bOccupancyOnlyFace)
				{
					if (OutFailureReason != nullptr)
					{
						const FLayoutTerrainBackedNeighborFaceRecord* const TerrainRecord =
							Context.TerrainBackedNeighborFaces.FindByPredicate(
								[Cell, Direction](const FLayoutTerrainBackedNeighborFaceRecord& Record)
								{
									return Record.Cell == Cell && Record.FaceDirection == Direction;
								});
						const FLayoutId SourceEvidenceId = TerrainRecord != nullptr
							? TerrainRecord->SourceEvidenceId
							: NAME_None;
						*OutFailureReason = FString::Printf(
							TEXT("%s cannot consume terrain-backed occupancy at %s. Retained terrain only satisfies non-traversable filled-neighbor occupancy with BoundaryRequirement=Any. Occupancy=%s Traversal=[%s] Evidence=%s"),
							*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
							*NeighborCell.ToString(),
							ToDebugString(SubjectFaceRule.OccupancyPolicy),
							*TagsToDebugString(SubjectFaceRule.ConnectedTraversalChannels),
							*SourceEvidenceId.ToString());
					}
					return false;
				}
				continue;
			}
			case ESolveCellFaceNeighborKind::ChildRegionContact:
			{
				const FIntVector CN = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (Context.ChildContentSetsByCell.Contains(CN)
					&& Context.ChildModuleCatalogsByCell.Contains(CN)
					&& Context.ChildPlannedCellsByCell.Contains(CN))
				{
					ChildFaceChecksByCell.FindOrAdd(CN).Add(Direction, SubjectFaceRule);
				}
				continue;
			}
			case ESolveCellFaceNeighborKind::IncomingBoundary:
			case ESolveCellFaceNeighborKind::SupportBoundary:
			{
				const FLayoutSolveBoundaryPoint* BoundaryPoint =
					ResolveCompiledIncomingBoundaryPoint(
						Context,
						*FaceInterface);
				if (BoundaryPoint == nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("compiled incoming-boundary interface for occupied cell %s %s lost its source boundary point."),
							*Cell.ToString(),
							*BuildFaceDebugString(
								Direction,
								SubjectYawRotationSteps));
					}
					return false;
				}

				const bool bIncomingVerticalSupport =
					FaceInterface->NeighborKind ==
					ESolveCellFaceNeighborKind::SupportBoundary;
				if (BoundaryPoint->bUsesCertifiedReciprocalDomain)
				{
					const FLayoutCellCandidateDomainRestriction* Restriction =
						Context.CandidateDomainRestrictionsByCell.Find(Cell);
					if (Restriction == nullptr
						|| Restriction->RestrictionId != BoundaryPoint->CertifiedDomainRestrictionId)
					{
						if (OutFailureReason != nullptr)
						{
							*OutFailureReason = FString::Printf(
								TEXT("%s lost certified reciprocal domain '%s' from certificate '%s' (request certificate '%s', cell restriction '%s')."),
								*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
								*BoundaryPoint->CertifiedDomainRestrictionId.ToString(),
								*BoundaryPoint->CertifiedDomainCertificateId.ToString(),
								*Context.CandidateDomainCertificateId.ToString(),
								Restriction != nullptr ? *Restriction->RestrictionId.ToString() : TEXT("<none>"));
						}
						return false;
					}
					if (!IsFaceCompatibleWithOccupancy(
						SubjectFaceRule,
						BoundaryPoint->bRepresentsFilledNeighbor,
						BoundaryPoint->bRepresentsFilledNeighbor)
						|| (BoundaryPoint->bRepresentsFilledNeighbor
							&& FaceMustFaceRegionBoundary(SubjectFaceRule))
						|| (!BoundaryPoint->bRepresentsFilledNeighbor
							&& (SubjectFaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceInterior
								|| SubjectFaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam)))
					{
						if (OutFailureReason != nullptr)
						{
							*OutFailureReason = FString::Printf(
								TEXT("%s violates certified reciprocal occupancy at %s."),
								*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
								*NeighborCell.ToString());
						}
						return false;
					}
					continue;
				}
				if (BoundaryPoint->bRequiresBoundaryFacing
					&& !bIncomingVerticalSupport
					&& !bSubjectMustFaceExterior)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("%s face cannot consume incoming boundary from region '%s' because it is not marked Must Face Region Boundary."),
							*BuildFaceDebugString(
								Direction,
								SubjectYawRotationSteps),
							*BoundaryPoint->SourceRegionDebugPath);
					}
					return false;
				}

				const bool bBoundaryFilled =
					BoundaryPoint->bRepresentsFilledNeighbor;
				const FLayoutFaceRule BoundaryFaceRule =
					BuildIncomingBoundaryFaceRule(*BoundaryPoint);
				EIncomingBoundaryFaceCompatibilityFailure BoundaryFailure =
					EIncomingBoundaryFaceCompatibilityFailure::None;
				if (!DoesIncomingBoundaryFaceSatisfyInterfaceContract(
					SubjectFaceRule,
					SubjectYawRotationSteps,
					BoundaryFaceRule,
					BoundaryPoint->SourceYawRotationSteps,
					bBoundaryFilled,
					BoundaryPoint->bRequiresBoundaryFacing,
					bIncomingVerticalSupport,
					bSubjectMustFaceExterior,
					&BoundaryFailure))
				{
					if (OutFailureReason != nullptr)
					{
						switch (BoundaryFailure)
						{
						case EIncomingBoundaryFaceCompatibilityFailure::SourceOccupancy:
							*OutFailureReason = FString::Printf(
								TEXT("%s face cannot consume incoming boundary from region '%s'. Occupancy=%s ConnectionTag=[%s] AllowedConnectionTags=[%s]"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*BoundaryPoint->SourceRegionDebugPath,
								ToDebugString(
									SubjectFaceRule.OccupancyPolicy),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveConnectionTags()),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveAllowedConnectionTags()));
							break;
						case EIncomingBoundaryFaceCompatibilityFailure::ConnectionTags:
							*OutFailureReason = FString::Printf(
								TEXT("%s connection tag compatibility failed with incoming boundary from region '%s'. CandidateAllowed=[%s] BoundaryConnection=[%s] BoundaryAllowed=[%s] CandidateConnection=[%s]"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*BoundaryPoint->SourceRegionDebugPath,
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveAllowedConnectionTags()),
								*TagsToDebugString(
									BoundaryFaceRule.GetEffectiveConnectionTags()),
								*TagsToDebugString(
									BoundaryFaceRule.GetEffectiveAllowedConnectionTags()),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveConnectionTags()));
							break;
						case EIncomingBoundaryFaceCompatibilityFailure::TraversalChannels:
							*OutFailureReason = FString::Printf(
								TEXT("%s face requires a walkable incoming boundary from region '%s', but the two faces share no ConnectedTraversalChannels. CandidateTraversal=[%s] BoundaryTraversal=[%s]"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*BoundaryPoint->SourceRegionDebugPath,
								*TagsToDebugString(
									SubjectFaceRule.ConnectedTraversalChannels),
								*TagsToDebugString(
									BoundaryFaceRule.ConnectedTraversalChannels));
							break;
						case EIncomingBoundaryFaceCompatibilityFailure::YawMismatch:
							*OutFailureReason = FString::Printf(
								TEXT("%s face requires matching yaw with incoming boundary from region '%s'. CandidateYaw=%d BoundaryYaw=%d"),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*BoundaryPoint->SourceRegionDebugPath,
								NormalizeSolverYawRotationSteps(
									SubjectYawRotationSteps),
								NormalizeSolverYawRotationSteps(
									BoundaryPoint->SourceYawRotationSteps));
							break;
						default:
							break;
						}
					}
					return false;
				}

				continue;
			}
			case ESolveCellFaceNeighborKind::DeferredExternalContract:
				// Parent coordination certifies this face after child transform mapping.
				continue;
			case ESolveCellFaceNeighborKind::TerrainResidualNeighbor:
				if (bSubjectMustFaceExterior
					|| !IsFaceCompatibleWithOccupancy(SubjectFaceRule, false, false))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("%s face cannot consume intentional empty terrain inside the owning region at %s."),
							*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
							*NeighborCell.ToString());
					}
					return false;
				}
				continue;
			case ESolveCellFaceNeighborKind::StructuralVerticalOverlap:
				if (!IsFaceCompatibleWithOccupancy(SubjectFaceRule, true, true)
					|| !SubjectFaceRule.ConnectedTraversalChannels.IsEmpty())
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("%s cannot occupy an unselected structural vertical overlap: the face requires open occupancy or traversal. Occupancy=%s; neighbor=%s."),
							*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
							ToDebugString(SubjectFaceRule.OccupancyPolicy),
							*NeighborCell.ToString());
					}
					return false;
				}
				continue;
			case ESolveCellFaceNeighborKind::OuterBoundary:
			case ESolveCellFaceNeighborKind::ReservedExternalCell:
			{
				EOpenSpaceFaceCompatibilityFailure OpenSpaceFailure =
					EOpenSpaceFaceCompatibilityFailure::None;
				const bool bTreatAsReservedExternal =
					FaceInterface->NeighborKind ==
					ESolveCellFaceNeighborKind::ReservedExternalCell;
				if (!DoesOpenSpaceFaceSatisfyInterfaceContract(
					SubjectFaceRule,
					bSubjectMustFaceExterior,
					bTreatAsReservedExternal,
					Direction,
					&OpenSpaceFailure))
				{
					if (OutFailureReason != nullptr)
					{
						switch (OpenSpaceFailure)
						{
						case EOpenSpaceFaceCompatibilityFailure::BoundaryFacing:
							*OutFailureReason = FString::Printf(
								TEXT("Problem: boundary-facing side is not allowed on the region boundary.\nFace: %s\nNeighbor cell: %s\nFix: Enable Can Face Region Boundary on the intended outward face, or use a module whose outward face is marked for region-boundary use."),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*NeighborCell.ToString());
							break;
						case EOpenSpaceFaceCompatibilityFailure::RequiresEmptyReservedExternal:
							*OutFailureReason = FString::Printf(
								TEXT("Problem: face requires empty space, but this neighbor is reserved for another vertical solve region.\nFace: %s\nNeighbor cell: %s\nOccupancy: %s\nConnectionTag: [%s]\nAllowedConnectionTags: [%s]\nFix: Use a face policy that can tolerate the planned vertical neighbor, or move this module out of the stacked location."),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*NeighborCell.ToString(),
								ToDebugString(
									SubjectFaceRule.OccupancyPolicy),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveConnectionTags()),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveAllowedConnectionTags()));
							break;
						case EOpenSpaceFaceCompatibilityFailure::SourceOccupancy:
							*OutFailureReason = FString::Printf(
								TEXT("Problem: face cannot touch outside or unplanned space.\nFace: %s\nNeighbor cell: %s\nOccupancy: %s\nConnectionTag: [%s]\nAllowedConnectionTags: [%s]\nFix: Use an occupancy policy that allows boundary exposure, or place this module where the face meets a filled neighbor instead."),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*NeighborCell.ToString(),
								ToDebugString(
									SubjectFaceRule.OccupancyPolicy),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveConnectionTags()),
								*TagsToDebugString(
									SubjectFaceRule.GetEffectiveAllowedConnectionTags()));
							break;
						default:
							break;
						}
					}
					return false;
				}

				continue;
			}
			case ESolveCellFaceNeighborKind::InternalPlannedNeighbor:
			case ESolveCellFaceNeighborKind::ExternalPlannedNeighbor:
			{
				if (ExistingNeighborPlacement != nullptr)
				{
					if (!FSolveContext::IsOccupiedPlacement(*ExistingNeighborPlacement))
					{
						if (!IsFaceCompatibleWithOccupancy(SubjectFaceRule, false, false))
						{
							if (OutFailureReason != nullptr)
							{
								*OutFailureReason = FString::Printf(
									TEXT("%s face cannot touch an empty placed neighbor at %s."),
									*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
									*NeighborCell.ToString());
							}
							return false;
						}
						continue;
					}

					FLayoutFaceRule NeighborFaceRule;
					if (!TryGetPlacementFaceRule(
							Context,
							*ExistingNeighborPlacement,
							FLayoutDirectionUtils::GetOpposite(Direction),
							NeighborFaceRule)
						|| !DoesFilledNeighborFacePairSatisfyInterfaceContract(
							SubjectFaceRule,
							SubjectYawRotationSteps,
							NeighborFaceRule,
							ExistingNeighborPlacement->YawRotationSteps))
					{
						if (OutFailureReason != nullptr)
						{
							*OutFailureReason = FString::Printf(
								TEXT("%s face is incompatible with placed neighbor '%s' opposite %s."),
								*BuildFaceDebugString(Direction, SubjectYawRotationSteps),
								*BuildPlacementDebugName(Context, *ExistingNeighborPlacement),
								*BuildFaceDebugString(
									FLayoutDirectionUtils::GetOpposite(Direction),
									ExistingNeighborPlacement->YawRotationSteps));
						}
						return false;
					}
					continue;
				}

				EPlannedNeighborFaceCompatibilityFailure
					PlannedNeighborFailure =
						EPlannedNeighborFaceCompatibilityFailure::None;
				if (!DoesPlannedNeighborFaceSatisfyInterfaceContract(
					Context,
					SubjectFaceRule,
					NeighborCell,
					FaceInterface->NeighborKind
						== ESolveCellFaceNeighborKind::ExternalPlannedNeighbor,
					&PlannedNeighborFailure))
				{
					if (OutFailureReason != nullptr)
					{
						if (PlannedNeighborFailure
							== EPlannedNeighborFaceCompatibilityFailure::
								RequiresEmptyButExternalContinuationCannotBeProvenEmpty)
						{
							*OutFailureReason = FString::Printf(
								TEXT("Problem: face requires an empty neighbor, but this face continues into another unresolved planned level.\nFace: %s\nNeighbor cell: %s\nFix: Use a filled-neighbor-compatible face here, or keep the empty-neighbor requirement on a face that stays inside the current solve context."),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*NeighborCell.ToString());
						}
						else
						{
							*OutFailureReason = FString::Printf(
								TEXT("Problem: face requires an empty neighbor, but the planned neighbor must stay filled.\nFace: %s\nNeighbor cell: %s\nNeighbor intent: %s\nFix: Use a filled-neighbor-compatible face here, or change the plan so the neighboring cell is allowed to stay empty."),
								*BuildFaceDebugString(
									Direction,
									SubjectYawRotationSteps),
								*NeighborCell.ToString(),
								Context.PlannedCellIntents.Contains(NeighborCell)
									? ToDebugString(
										Context.PlannedCellIntents[NeighborCell])
									: TEXT("<none>"));
						}
					}
					return false;
				}

				continue;
			}
			default:
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("unknown compiled face-interface kind for occupied cell %s %s."),
						*Cell.ToString(),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps));
				}
				return false;
			}
		}

		// Collective child-boundary check: for each child cell adjacent to this
		// parent cell, at least one child module must satisfy ALL parent faces
		// simultaneously.  This replaces the old per-face pairwise check and
		// prevents layouts where every parent-face/child-face pair passes
		// individually but no single child module works for all faces at once.
		for (const auto& ChildCellFaces : ChildFaceChecksByCell)
		{
			const FIntVector& ChildCell = ChildCellFaces.Key;
			const FLayoutRegionContentSetSolveSnapshot* const* CS = Context.ChildContentSetsByCell.Find(ChildCell);
			const FLayoutModuleCatalog* const* ChildCatalog = Context.ChildModuleCatalogsByCell.Find(ChildCell);
			const FLayoutPlannedCell* ChildPlannedCell = Context.ChildPlannedCellsByCell.Find(ChildCell);
			const int32* ChildTopModuleLevel = Context.ChildTopModuleLevelByCell.Find(ChildCell);
			if (CS == nullptr || *CS == nullptr
				|| ChildCatalog == nullptr || *ChildCatalog == nullptr
				|| ChildPlannedCell == nullptr || ChildTopModuleLevel == nullptr)
			{
				if (OutFailureReason) { *OutFailureReason = FString::Printf(TEXT("child cell %s lost its frozen content, module, or planned-cell contract after face collection."), *ChildCell.ToString()); }
				return false;
			}
			if (!LayoutProfileSolverInternal::HasCompatibleChildBoundaryModule(
				**CS,
				**ChildCatalog,
				*ChildPlannedCell,
				*ChildTopModuleLevel,
				ChildCellFaces.Value))
			{
				if (OutFailureReason) { *OutFailureReason = FString::Printf(TEXT("no child module at %s satisfies all %d parent boundary faces simultaneously."), *ChildCell.ToString(), ChildCellFaces.Value.Num()); }
				return false;
			}
		}

		// Parent admission remains face-scoped at child boundary. Child-internal
		// adjacency belongs to the independently solved child CSP, which consumes
		// exact inherited boundary domains after descendants and bundles settle.

		return true;
	}

	/**
	 * Prunes candidates that are already impossible from static plan geometry
	 * before the indexed CSP domains and compatibility rows are built.
	 */
	bool DoesCandidateSatisfyCarrierBackedAdmission(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate,
		FString* OutFailureReason = nullptr)
	{
		const FSolveContext::FOrientedModuleVariant* CandidateVariant =
			FindCandidateVariant(Context, Candidate);
		if (CandidateVariant == nullptr)
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("candidate at cell %s is missing its oriented variant snapshot."),
					*Cell.ToString());
			}
			return false;
		}

		const ELayoutCellIntent Intent = Context.PlannedCellIntents[Cell];
		if (!DoesCandidatePassIntentRoleRestrictions(
			Context,
			Cell,
			Intent,
			Candidate,
			OutFailureReason))
		{
			return false;
		}

		if (!DoesCandidateProjectedOccupiedBundleFitContext(
			Context,
			Cell,
			Candidate,
			OutFailureReason))
		{
			return false;
		}

		const FSolveContext::FSolvePlacement CandidatePlacement = MakeRootSolvePlacement(
			Context,
			Cell,
			Candidate.YawRotationSteps,
			Candidate.VariantIndex);
		return DoesOccupiedSubjectSatisfyCompiledFaceInterfaces(
			Context,
			Cell,
			Candidate.YawRotationSteps,
			&CandidateVariant->WorldFaceRules,
			BuildCandidateDebugName(Context, Candidate),
			[&Context, &Candidate](
				const ELayoutFaceDirection Direction,
				FLayoutFaceRule& OutFaceRule)
			{
				return TryGetCandidateFaceRule(
					Context,
					Candidate,
					Direction,
					OutFaceRule);
			},
			[&CandidatePlacement](const FSolveContext::FSolvePlacement& NeighborPlacement)
			{
				return AreSameOccupiedBundlePlacement(
					CandidatePlacement,
					NeighborPlacement);
			},
			OutFailureReason);
	}

	/**
	 * Prunes candidates that are already impossible from static plan geometry
	 * before the indexed CSP domains and compatibility rows are built.
	 */
	bool IsCandidateCompatibleWithStaticPlannedGeometry(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			return true;
		}

		return DoesCandidateSatisfyCarrierBackedAdmission(
			Context,
			Cell,
			Candidate);
	}

	bool TryBuildPlacementWorldFaceRules(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement,
		FLayoutModuleFaceRules& OutWorldFaceRules)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindPlacementVariant(Context, Placement))
		{
			OutWorldFaceRules = Variant->WorldFaceRules;
			return true;
		}

		return false;
	}

	bool TryGetPlacementFaceRule(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule)
	{
		if (Placement.VariantIndex != INDEX_NONE)
		{
			if (const FLayoutModuleSolveSnapshot* ModuleSnapshot = FindVariantModuleSnapshot(Context, Placement.VariantIndex))
			{
				if (ModuleSnapshot->OccupiedLocalCells.Num() > 1)
				{
					const FLayoutLocalCellFaceRuleSnapshot* CellSnapshot = ModuleSnapshot->GeneratedLocalCellFaceRules.FindByPredicate(
						[&Placement](const FLayoutLocalCellFaceRuleSnapshot& CandidateCellSnapshot)
						{
							return CandidateCellSnapshot.LocalCell == Placement.LocalBundleCell;
						});
					if (CellSnapshot != nullptr)
					{
						if (const FLayoutFaceRule* Rule = CellSnapshot->ExposedFaceRules.FindByPredicate(
							[&Placement, Direction](const FLayoutFaceRule& CandidateRule)
							{
								return FLayoutDirectionUtils::RotateYaw(CandidateRule.Direction, Placement.YawRotationSteps) == Direction;
							}))
						{
							OutFaceRule = *Rule;
							OutFaceRule.Direction = Direction;
							return true;
						}
					}
				}
			}

			return TryGetVariantFaceRule(Context, Placement.VariantIndex, Direction, OutFaceRule);
		}

		return false;
	}

	void ExpandCandidateReachableAreasThroughInternalLinks(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate,
		TSet<FGameplayTag>& ReachableAreas);

	bool MustGrowTraversalChannelFromEntryRoot(const FSolveContext& Context, const FIntVector& Cell, const FGameplayTag& TraversalChannel);
	int32 GetCandidateReachableHorizontalFaceScore(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate,
		const int32 MaxScore = MAX_int32)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || !LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(Context))
		{
			return 0;
		}

		int32 Score = 0;
		for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
		{
			FLayoutFaceRule FaceRule;
			if (!TryGetCandidateFaceRule(Context, Candidate, Direction, FaceRule)
				|| !IsFaceCompatibleWithOccupancy(FaceRule, true, true))
			{
				continue;
			}

			for (const FGameplayTag& TraversalChannel : GetCandidateTraversalChannels(Context, Candidate))
			{
				if (MustGrowTraversalChannelFromEntryRoot(Context, Cell, TraversalChannel)
					&& FaceRule.ConnectedTraversalChannels.HasTagExact(TraversalChannel))
				{
					++Score;
				}
			}
		}

		return MaxScore >= 0 ? FMath::Min(Score, MaxScore) : Score;
	}

	int32 GetCandidateHorizontalTraversalExposureScore(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			return 0;
		}

		int32 Score = 0;
		for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
		{
			FLayoutFaceRule FaceRule;
			if (!TryGetCandidateFaceRule(Context, Candidate, Direction, FaceRule)
				|| !IsFaceCompatibleWithOccupancy(FaceRule, true, true))
			{
				continue;
			}

			for (const FGameplayTag& TraversalChannel : GetCandidateTraversalChannels(Context, Candidate))
			{
				if (FaceRule.ConnectedTraversalChannels.HasTagExact(TraversalChannel))
				{
					++Score;
					break;
				}
			}
		}

		return Score;
	}

	bool ShouldUseReachableHorizontalFaceScore(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const int32 ReachabilityPreference)
	{
		if (LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, Cell))
		{
			return true;
		}

		if (ReachabilityPreference <= 1)
		{
			return false;
		}

		const bool bRepairMode = Context.bForceReachabilityBranchFaceScore
			|| Context.Result.PropagationStats.BacktrackCount >= ReachabilityBranchFaceScoreBacktrackThreshold;
		if (!bRepairMode)
		{
			return false;
		}

		return true;
	}

	void AppendVariantIndicesForSupportedIntent(
		const FSolveContext& Context,
		const ELayoutCellIntent Intent,
		TSet<int32>& InOutVariantIndices)
	{
		if (const TArray<int32>* VariantIndices =
				Context.VariantIndicesByIntent.Find(Intent))
		{
			for (const int32 VariantIndex : *VariantIndices)
			{
				InOutVariantIndices.Add(VariantIndex);
			}
		}
	}

	uint8 GetCompiledSeedIntentMaskForCell(
		const FSolveContext& Context,
		const FIntVector& Cell)
	{
		if (const uint8* CompiledMask =
				Context.CompiledSeedIntentMasks.Find(Cell))
		{
			return *CompiledMask;
		}

		return 0;
	}

	bool DoesCellCarryBoundaryExposure(
		const FSolveContext& Context,
		const FIntVector& Cell)
	{
		return DoesIntentMaskContain(
			GetCompiledSeedIntentMaskForCell(
				Context,
				Cell),
			ELayoutCellIntent::Boundary);
	}

	bool CellHasCompiledBoundaryConstraint(
		const FSolveContext& Context,
		const FIntVector& Cell)
	{
		const FSolveCellFaceInterfaceSet* InterfaceSet =
			Context.CompiledFaceInterfaces.Find(Cell);
		if (InterfaceSet == nullptr)
		{
			return false;
		}

		for (const FSolveCellFaceInterface& FaceInterface :
			InterfaceSet->Faces)
		{
			if (FaceInterface.NeighborKind ==
					ESolveCellFaceNeighborKind::IncomingBoundary
				|| FaceInterface.NeighborKind ==
					ESolveCellFaceNeighborKind::SupportBoundary)
			{
				return true;
			}
		}

		return false;
	}

	bool HasAnyCompiledBoundaryConstraint(const FSolveContext& Context)
	{
		for (const TPair<FIntVector, FSolveCellFaceInterfaceSet>& InterfacePair :
			Context.CompiledFaceInterfaces)
		{
			if (CellHasCompiledBoundaryConstraint(Context, InterfacePair.Key))
			{
				return true;
			}
		}

		return false;
	}

	bool FaceCarriesLivePlannedNeighborSupport(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutFaceDirection Direction)
	{
		const FSolveCellFaceInterface* FaceInterface =
			FindCompiledCellFaceInterface(
				Context,
				Cell,
				Direction);
		if (FaceInterface == nullptr
			|| FaceInterface->NeighborKind
				!= ESolveCellFaceNeighborKind::InternalPlannedNeighbor)
		{
			return false;
		}
		return true;
	}

	/** Prefers structural fill when every remaining adjacent VerticalAccess bundle candidate requires this neighbor occupied. */
	bool ShouldPreferOccupiedVerticalAccessSupport(
		const FSolveContext& Context,
		const FIntVector& Cell)
	{
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			const FIntVector EndpointCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			for (const int32 EndpointLocalZ : {0, 1})
			{
				const FIntVector HostRootCell = EndpointCell - FIntVector(0, 0, EndpointLocalZ);
				const ELayoutCellIntent* HostIntent = Context.PlannedCellIntents.Find(HostRootCell);
				if (HostIntent == nullptr || *HostIntent != ELayoutCellIntent::VerticalAccess)
				{
					continue;
				}

				const TArray<FSolveCandidate>* HostDomain = Context.InitialDomains.Find(HostRootCell);
				if (HostDomain == nullptr || HostDomain->IsEmpty())
				{
					continue;
				}
				bool bFoundHostCandidate = false;
				bool bEveryHostCandidateRequiresFilled = true;
				for (const FSolveCandidate& Candidate : *HostDomain)
				{
					if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
					{
						continue;
					}
					FLayoutFaceRule HostFaceRule;
					if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
							Context,
							Candidate,
							FIntVector(0, 0, EndpointLocalZ),
							FLayoutDirectionUtils::GetOpposite(Direction),
							HostFaceRule))
					{
						continue;
					}
					bFoundHostCandidate = true;
					if (IsFaceCompatibleWithOccupancy(HostFaceRule, true, false))
					{
						bEveryHostCandidateRequiresFilled = false;
						break;
					}
				}
				if (bFoundHostCandidate && bEveryHostCandidateRequiresFilled)
				{
					return true;
				}
			}
		}
		return false;
	}

	TArray<FSolveCandidate> BuildOrderedCandidates(
		FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutCellIntent Intent)
	{
		TArray<FSolveCandidate> Candidates;
		TArray<FString> AdmissionFailures;
		auto RecordAdmissionFailure = [&AdmissionFailures](const FString& Failure)
		{
			// Empty-domain diagnostics must cover every Road catalog yaw so a pair
			// of rejected stair modules cannot hide ordinary-floor admission failure.
			if (AdmissionFailures.Num() < 32)
			{
				AdmissionFailures.Add(Failure);
			}
		};
		TSet<int32> CandidateVariantIndices;
		const uint8 SeedIntentMask =
			GetCompiledSeedIntentMaskForCell(
				Context,
				Cell);
		for (const ELayoutCellIntent SeedIntent : SeedIntentIterationOrder)
		{
			if (!DoesIntentMaskContain(SeedIntentMask, SeedIntent))
			{
				continue;
			}

			AppendVariantIndicesForSupportedIntent(
				Context,
				SeedIntent,
				CandidateVariantIndices);
		}

		auto AppendAdmittedCandidates = [&]()
		{
			for (const int32 VariantIndex : CandidateVariantIndices)
			{
				if (!LayoutSolveExecution::Checkpoint(Context.Result.FailureReason))
				{
					Context.bTimeBudgetExceeded = true;
					return;
				}

				if (!Context.Variants.IsValidIndex(VariantIndex))
				{
					continue;
				}

				const FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[VariantIndex];
				FSolveCandidate Candidate;
				Candidate.YawRotationSteps = Variant.YawRotationSteps;
				Candidate.VariantIndex = VariantIndex;
				Candidate.ModuleSnapshotIndex = Variant.ModuleSnapshotIndex;
				Candidate.ModuleSnapshotId = Variant.ModuleSnapshotId;
				Candidate.bEmpty = false;
				const FString CandidateName = BuildCandidateDebugName(Context, Candidate);
				if (const FLayoutCellCandidateDomainRestriction* Restriction =
						Context.CandidateDomainRestrictionsByCell.Find(Cell))
				{
					const bool bAllowedByCertificate =
						Restriction->AllowedCandidates.ContainsByPredicate(
							[&Variant](const FLayoutCandidateVariantIdentity& AllowedCandidate)
							{
								return AllowedCandidate.ModuleSnapshotId == Variant.ModuleSnapshotId
									&& AllowedCandidate.YawRotationSteps == Variant.YawRotationSteps;
							});
					if (!bAllowedByCertificate)
					{
						RecordAdmissionFailure(FString::Printf(
							TEXT("%s[yaw=%d]: rejected by candidate-domain restriction '%s'."),
							*CandidateName,
							Variant.YawRotationSteps,
							*Restriction->RestrictionId.ToString()));
						continue;
					}
				}
				if (!LayoutProfileSolverInternal::DoesModuleMatchResolvedPlacementZone(
					Context,
					Cell,
					Variant.PlacementZone))
				{
					RecordAdmissionFailure(FString::Printf(
						TEXT("%s[yaw=%d]: placement zone %s does not match cell topology."),
						*CandidateName,
						Variant.YawRotationSteps,
						*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(Variant.PlacementZone))));
					continue;
				}
				const bool bMatchesAuthoredLevelPolicy = DoesCellMatchLevelPlacementPolicy(
					Cell,
					LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(Context, Cell),
					Context.TopPlannedLevelByXY,
					Variant.LevelPlacementPolicy,
					Variant.SpecificLevel);
				// Structural route claims consume sparse authority, not authored level restrictions.
				if (!bMatchesAuthoredLevelPolicy)
				{
					RecordAdmissionFailure(FString::Printf(
						TEXT("%s[yaw=%d]: level policy %s rejects Z=%d."),
						*CandidateName,
						Variant.YawRotationSteps,
						*StaticEnum<ELayoutLevelPlacementPolicy>()->GetNameStringByValue(static_cast<int64>(Variant.LevelPlacementPolicy)),
						Cell.Z));
					continue;
				}

				FString StaticGeometryFailure;
				if (!DoesCandidateSatisfyCarrierBackedAdmission(
					Context,
					Cell,
					Candidate,
					&StaticGeometryFailure))
				{
					RecordAdmissionFailure(FString::Printf(
						TEXT("%s[yaw=%d]: %s"),
						*CandidateName,
						Variant.YawRotationSteps,
						StaticGeometryFailure.IsEmpty() ? TEXT("static planned geometry rejected candidate.") : *StaticGeometryFailure));
					continue;
				}

				Candidates.Add(Candidate);
			}
		};

		AppendAdmittedCandidates();
		if (Context.bTimeBudgetExceeded)
		{
			return Candidates;
		}

		const bool bCanRemainTerrainResidual = Context.TerrainResidualRuleIdByCell.Contains(Cell)
			&& Intent != ELayoutCellIntent::Entry
			&& Intent != ELayoutCellIntent::Connector
			&& Intent != ELayoutCellIntent::VerticalAccess
			&& !LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, Cell);
		const bool bCanRemainEmpty = bCanRemainTerrainResidual || CanTopBridgeOfferRemainEmpty(Context, Cell);
		if (bCanRemainEmpty && DoesCellSatisfyCompiledEmptyInterfaces(Context, Cell))
		{
			Candidates.Add(FSolveCandidate{});
		}

		const bool bPreferOccupiedVerticalAccessSupport =
			bCanRemainTerrainResidual
			&& ShouldPreferOccupiedVerticalAccessSupport(Context, Cell);
		Candidates.Sort([&Context, &Cell, bCanRemainEmpty, bPreferOccupiedVerticalAccessSupport](const FSolveCandidate& Left, const FSolveCandidate& Right)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Left) || !LayoutProfileSolverInternal::IsOccupiedCandidate(Right))
			{
				return bCanRemainEmpty && !bPreferOccupiedVerticalAccessSupport
					? !LayoutProfileSolverInternal::IsOccupiedCandidate(Left)
					: LayoutProfileSolverInternal::IsOccupiedCandidate(Left);
			}

			const int32 MaxRouteScore = LayoutProfileSolverInternal::ShouldUseFullReachableHorizontalFaceScore(Context, Cell) ? MAX_int32 : 2;
			const int32 LeftRouteScore = FMath::Min(LayoutProfileSolverInternal::GetCandidateRequiredTraversalRouteScore(Context, Cell, Left), MaxRouteScore);
			const int32 RightRouteScore = FMath::Min(LayoutProfileSolverInternal::GetCandidateRequiredTraversalRouteScore(Context, Cell, Right), MaxRouteScore);
			if (LeftRouteScore != RightRouteScore)
			{
				return LeftRouteScore > RightRouteScore;
			}

			if (LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, Cell))
			{
				const int32 MaxReachableFaceScore = LayoutProfileSolverInternal::ShouldUseFullReachableHorizontalFaceScore(Context, Cell) ? MAX_int32 : 2;
				const int32 LeftReachableFaceScore = GetCandidateReachableHorizontalFaceScore(Context, Cell, Left, MaxReachableFaceScore);
				const int32 RightReachableFaceScore = GetCandidateReachableHorizontalFaceScore(Context, Cell, Right, MaxReachableFaceScore);
				if (LeftReachableFaceScore != RightReachableFaceScore)
				{
					return LeftReachableFaceScore > RightReachableFaceScore;
				}
			}

			const int32 LeftWeight = GetCandidateWeight(Context, Left);
			const int32 RightWeight = GetCandidateWeight(Context, Right);
			if (LeftWeight != RightWeight)
			{
				return LeftWeight > RightWeight;
			}

			if (LayoutProfileSolverInternal::ShouldPreferStructuredFillTraversalExposure(Context, Cell))
			{
				bool bHasStructuredFillPreference = false;
				const bool bPreferLeftStructuredFill = LayoutProfileSolverInternal::ShouldPreferLeftStructuredFillTraversalExposure(
					Context,
					Left,
					Right,
					bHasStructuredFillPreference);
				if (bHasStructuredFillPreference)
				{
					return bPreferLeftStructuredFill;
				}
			}

			const uint32 LeftHash = HashCombineFast(
				HashCombine(GetTypeHash(Cell), static_cast<uint32>(Context.Seed)),
				HashCombine(GetCandidateSeedHash(Context, Left), static_cast<uint32>(Left.YawRotationSteps)));
			const uint32 RightHash = HashCombineFast(
				HashCombine(GetTypeHash(Cell), static_cast<uint32>(Context.Seed)),
				HashCombine(GetCandidateSeedHash(Context, Right), static_cast<uint32>(Right.YawRotationSteps)));
			if (LeftHash != RightHash)
			{
				return LeftHash < RightHash;
			}

			const FLayoutId LeftDebugName = GetCandidateDebugName(Context, Left);
			const FLayoutId RightDebugName = GetCandidateDebugName(Context, Right);
			if (LeftDebugName != RightDebugName)
			{
				return LeftDebugName.LexicalLess(RightDebugName);
			}

			return Left.YawRotationSteps < Right.YawRotationSteps;
		});


		const bool bHasOccupiedCandidate = Candidates.ContainsByPredicate([](const FSolveCandidate& Candidate)
		{
			return LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate);
		});
		if (!bHasOccupiedCandidate)
		{
			if (CandidateVariantIndices.IsEmpty())
			{
				AdmissionFailures.Add(FString::Printf(
					TEXT("No catalog variants support compiled intent mask 0x%02x."),
					SeedIntentMask));
			}
			Context.InitialDomainAdmissionFailuresByCell.Add(Cell, MoveTemp(AdmissionFailures));
		}
		else
		{
			Context.InitialDomainAdmissionFailuresByCell.Remove(Cell);
		}

		return Candidates;
	}

	ELayoutFaceDirection GetAuthoredDirectionForWorldDirection(const ELayoutFaceDirection WorldDirection, const int32 YawRotationSteps)
	{
		return FLayoutDirectionUtils::RotateYaw(WorldDirection, -YawRotationSteps);
	}

	bool DoesCandidateCoverHardClosureSegment(
		const FSolveContext& Context,
		const FIntVector& RootCell,
		const FSolveCandidate& Candidate,
		const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate)
			|| !Context.Variants.IsValidIndex(Candidate.VariantIndex))
		{
			return false;
		}

		const FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[Candidate.VariantIndex];
		const FLayoutModuleSolveSnapshot* const ModuleSnapshot =
			Context.ModuleSnapshots.IsValidIndex(Variant.ModuleSnapshotIndex)
				? &Context.ModuleSnapshots[Variant.ModuleSnapshotIndex]
				: nullptr;
		if (ModuleSnapshot == nullptr)
		{
			return false;
		}

		const auto DoesSpanOfferCover = [&RootCell, &Candidate, ModuleSnapshot, &Segment](const FLayoutDerivedSpanOffer& SpanOffer)
		{
			return LayoutProfileSolverInternal::DoesProjectedSpanOfferCoverClosureSegment(
				SpanOffer,
				RootCell,
				ModuleSnapshot->BoundsCells,
				Candidate.YawRotationSteps,
				Segment);
		};
		if (Variant.ClosureProviderIntents.IsEmpty())
		{
			return Variant.WorldSpanOffers.ContainsByPredicate(DoesSpanOfferCover);
		}

		for (const FLayoutClosureProviderIntent& ProviderIntent : Variant.ClosureProviderIntents)
		{
			if (ProviderIntent.Zone != ELayoutPlacementZone::Perimeter)
			{
				continue;
			}

			for (const FLayoutDerivedSpanOffer& SpanOffer : Variant.WorldSpanOffers)
			{
				if (!SpanOffer.ClosureId.IsNone()
					&& !ProviderIntent.ClosureId.IsNone()
					&& SpanOffer.ClosureId != ProviderIntent.ClosureId)
				{
					continue;
				}

				FLayoutDerivedSpanOffer EffectiveSpanOffer = SpanOffer;
				EffectiveSpanOffer.ClosureId = !SpanOffer.ClosureId.IsNone()
					? SpanOffer.ClosureId
					: ProviderIntent.ClosureId;
				if (DoesSpanOfferCover(EffectiveSpanOffer))
				{
					return true;
				}
			}
		}

		return false;
	}

	/** Compiles hard closure ownership once before indexed search removes static non-provider choices. */
	void CompileHardClosureDomainFeasibility(FSolveContext& Context)
	{
		LayoutProfileSolverInternal::CompileHardClosureSegmentsForContext(
			Context,
			Context.CompiledHardClosureSegments);
		Context.HardClosureSegmentIndicesByPhysicalCell.Reset();
		Context.HardClosureProviderRootsBySegment.Reset();
		Context.HardClosureSegmentIndicesByProviderRoot.Reset();
		Context.HardClosureProviderRootsBySegment.SetNum(Context.CompiledHardClosureSegments.Num());

		for (int32 SegmentIndex = 0; SegmentIndex < Context.CompiledHardClosureSegments.Num(); ++SegmentIndex)
		{
			Context.HardClosureSegmentIndicesByPhysicalCell.FindOrAdd(
				Context.CompiledHardClosureSegments[SegmentIndex].Cell).Add(SegmentIndex);
		}

		for (TPair<FIntVector, TArray<FSolveCandidate>>& DomainPair : Context.InitialDomains)
		{
			TArray<FSolveCandidate> FilteredCandidates;
			FilteredCandidates.Reserve(DomainPair.Value.Num());
			for (const FSolveCandidate& Candidate : DomainPair.Value)
			{
				if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
				{
					FilteredCandidates.Add(Candidate);
					continue;
				}

				const TArray<FIntVector> OccupiedCells =
					BuildCandidateWorldOccupiedCells(Context, DomainPair.Key, Candidate);
				bool bRejectCandidate = false;
				TArray<int32> CoveredSegmentIndices;
				for (const FIntVector& OccupiedCell : OccupiedCells)
				{
					const TArray<int32>* SegmentIndices =
						Context.HardClosureSegmentIndicesByPhysicalCell.Find(OccupiedCell);
					if (SegmentIndices == nullptr)
					{
						continue;
					}

					for (const int32 SegmentIndex : *SegmentIndices)
					{
						const FLayoutClosureCoverageSegmentRecord& Segment =
							Context.CompiledHardClosureSegments[SegmentIndex];
						if (!DoesCandidateCoverHardClosureSegment(Context, DomainPair.Key, Candidate, Segment))
						{
							bRejectCandidate = true;
							break;
						}
						CoveredSegmentIndices.Add(SegmentIndex);
					}
					if (bRejectCandidate)
					{
						break;
					}
				}

				if (bRejectCandidate)
				{
					continue;
				}

				FilteredCandidates.Add(Candidate);
				for (const int32 SegmentIndex : CoveredSegmentIndices)
				{
					Context.HardClosureProviderRootsBySegment[SegmentIndex].AddUnique(DomainPair.Key);
					Context.HardClosureSegmentIndicesByProviderRoot.FindOrAdd(DomainPair.Key).AddUnique(SegmentIndex);
				}
			}
			DomainPair.Value = MoveTemp(FilteredCandidates);
		}
	}

	/** Compiles hard feature demands and static provider roots once per immutable request context. */
	// Local project fix: hard counted features must steer structural search instead of rejecting only after proof.
	void CompileHardZoneFeatureDemandModel(FSolveContext& Context)
	{
		if (Context.bHardZoneFeatureDemandsCompiled)
		{
			return;
		}

		LayoutZoneFeatureDemand::CompileHardDemands(
			GetEffectiveProfileSnapshot(Context).ZoneFeatureRequirements,
			Context.HardZoneFeatureDemands);
		Context.HardZoneFeatureCommittedCounts.Init(
			0,
			Context.HardZoneFeatureDemands.Num());
		Context.HardZoneFeatureProviderRootsByDemand.SetNum(
			Context.HardZoneFeatureDemands.Num());
		Context.bHardZoneFeatureDemandsCompiled = true;
		for (const FLayoutZoneFeatureProviderCommitment& Commitment :
			Context.PrecommittedZoneFeatureProviderCommitments)
		{
			const int32 DemandIndex = Context.HardZoneFeatureDemands.IndexOfByPredicate(
				[&Commitment](const LayoutZoneFeatureDemand::FHardDemand& Demand)
				{
					return Demand.RequirementId == Commitment.RequirementId;
				});
			if (Context.HardZoneFeatureCommittedCounts.IsValidIndex(DemandIndex))
			{
				++Context.HardZoneFeatureCommittedCounts[DemandIndex];
			}
		}

		for (const TPair<FIntVector, TArray<FSolveCandidate>>& DomainPair :
			Context.InitialDomains)
		{
			for (int32 DemandIndex = 0;
				 DemandIndex < Context.HardZoneFeatureDemands.Num();
				 ++DemandIndex)
			{
				if (DomainPair.Value.ContainsByPredicate(
					[&Context, &DomainPair, DemandIndex](const FSolveCandidate& Candidate)
					{
						return LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate)
							&& DoesCandidateProvideHardZoneFeatureDemand(
								Context,
								DomainPair.Key,
								Candidate.VariantIndex,
								DemandIndex);
					}))
				{
					Context.HardZoneFeatureProviderRootsByDemand[DemandIndex].Add(
						DomainPair.Key);
				}
			}
		}

		int32 ProviderRootCount = 0;
		for (TArray<FIntVector>& ProviderRoots :
			Context.HardZoneFeatureProviderRootsByDemand)
		{
			ProviderRoots.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				if (Left.Z != Right.Z) return Left.Z < Right.Z;
				if (Left.Y != Right.Y) return Left.Y < Right.Y;
				return Left.X < Right.X;
			});
			ProviderRootCount += ProviderRoots.Num();
		}

		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair :
			Context.Placements)
		{
			if (FSolveContext::IsOccupiedPlacement(PlacementPair.Value)
				&& IsBundleRootSolvePlacement(PlacementPair.Key, PlacementPair.Value))
			{
				ApplyHardZoneFeatureBundleCommitment(
					Context,
					PlacementPair.Key,
					PlacementPair.Value.VariantIndex,
					1);
			}
		}

		Context.Result.PropagationStats.HardZoneFeatureDemandCount =
			Context.HardZoneFeatureDemands.Num();
		Context.Result.PropagationStats.HardZoneFeatureProviderRootCount =
			ProviderRootCount;
	}

	/** Rejects overflow immediately and minimum shortfall once no live provider capacity remains. */
	bool ValidateHardZoneFeatureDemandFeasibility(
		FSolveContext& Context,
		FString& OutFailureReason)
	{
		if (!Context.bHardZoneFeatureDemandsCompiled)
		{
			return true;
		}

		for (int32 DemandIndex = 0;
			 DemandIndex < Context.HardZoneFeatureDemands.Num();
			 ++DemandIndex)
		{
			const LayoutZoneFeatureDemand::FHardDemand& Demand =
				Context.HardZoneFeatureDemands[DemandIndex];
			const int32 CommittedCount =
				Context.HardZoneFeatureCommittedCounts[DemandIndex];
			if (Demand.MaxCount > 0 && CommittedCount > Demand.MaxCount)
			{
				++Context.Result.PropagationStats.HardZoneFeatureCountBoundPruneCount;
				OutFailureReason = FString::Printf(
					TEXT("Hard zone-feature requirement '%s' exceeds MaxCount=%d after committing %d providers."),
					*Demand.RequirementId.ToString(),
					Demand.MaxCount,
					CommittedCount);
				return false;
			}

			if (CommittedCount >= Demand.MinCount)
			{
				continue;
			}

			int32 RemainingProviderCapacity =
				Context.HardZoneFeatureExternalProviderCapacityByRequirementId.FindRef(
					Demand.RequirementId);
			for (const FIntVector& ProviderRoot :
				Context.HardZoneFeatureProviderRootsByDemand[DemandIndex])
			{
				// Feasibility needs a sufficient lower bound, not the entire domain's
				// capacity on every placement. Later mutations recheck this bound.
				if (CommittedCount + RemainingProviderCapacity >= Demand.MinCount) break;
				if (Context.Placements.Contains(ProviderRoot))
				{
					continue;
				}

				const TArray<FSolveCandidate> LegalCandidates =
					GetLegalCandidatesForCellIndexed(
						Context,
						ProviderRoot,
						nullptr,
						nullptr,
						false);
				if (LegalCandidates.ContainsByPredicate(
					[&Context, ProviderRoot, DemandIndex](const FSolveCandidate& Candidate)
					{
						return DoesCandidateProvideHardZoneFeatureDemand(
							Context,
							ProviderRoot,
							Candidate.VariantIndex,
							DemandIndex);
					}))
				{
					++RemainingProviderCapacity;
				}
			}

			if (CommittedCount + RemainingProviderCapacity < Demand.MinCount)
			{
				++Context.Result.PropagationStats.HardZoneFeatureCountBoundPruneCount;
				OutFailureReason = FString::Printf(
					TEXT("Hard zone-feature requirement '%s' needs MinCount=%d, but only %d committed plus %d remaining legal provider roots are available."),
					*Demand.RequirementId.ToString(),
					Demand.MinCount,
					CommittedCount,
					RemainingProviderCapacity);
				return false;
			}
		}

		return true;
	}

	bool DoesHardClosureSegmentHaveLiveProvider(
		FSolveContext& Context,
		const int32 SegmentIndex)
	{
		if (!Context.CompiledHardClosureSegments.IsValidIndex(SegmentIndex))
		{
			return true;
		}

		const FLayoutClosureCoverageSegmentRecord& Segment =
			Context.CompiledHardClosureSegments[SegmentIndex];
		if (const FSolveContext::FSolvePlacement* SegmentPlacement = Context.Placements.Find(Segment.Cell))
		{
			if (FSolveContext::IsOccupiedPlacement(*SegmentPlacement))
			{
				const FSolveContext::FSolvePlacement* const RootPlacement =
					Context.Placements.Find(SegmentPlacement->BundleRootCell);
				if (RootPlacement != nullptr)
				{
					FSolveCandidate SettledCandidate;
					SettledCandidate.YawRotationSteps = RootPlacement->YawRotationSteps;
					SettledCandidate.VariantIndex = RootPlacement->VariantIndex;
					SettledCandidate.ModuleSnapshotIndex = RootPlacement->ModuleSnapshotIndex;
					SettledCandidate.ModuleSnapshotId = RootPlacement->ModuleSnapshotId;
					SettledCandidate.bEmpty = !FSolveContext::IsOccupiedPlacement(*RootPlacement);
					if (DoesCandidateCoverHardClosureSegment(
						Context,
						RootPlacement->BundleRootCell,
						SettledCandidate,
						Segment))
					{
						return true;
					}
				}
			}
		}

		if (!Context.HardClosureProviderRootsBySegment.IsValidIndex(SegmentIndex))
		{
			return false;
		}

		for (const FIntVector& ProviderRoot : Context.HardClosureProviderRootsBySegment[SegmentIndex])
		{
			if (Context.Placements.Contains(ProviderRoot))
			{
				continue;
			}

			const TArray<FSolveCandidate> LegalCandidates = GetLegalCandidatesForCellIndexed(
				Context,
				ProviderRoot,
				nullptr,
				nullptr,
				false);
			if (LegalCandidates.ContainsByPredicate(
				[&Context, &ProviderRoot, &Segment](const FSolveCandidate& Candidate)
				{
					return DoesCandidateCoverHardClosureSegment(
						Context,
						ProviderRoot,
						Candidate,
						Segment);
				}))
			{
				return true;
			}
		}

		return false;
	}

	/** Rechecks only segments touched by changed occupancy or adjacent provider domains. */
	bool ValidateAffectedHardClosureFeasibility(
		FSolveContext& Context,
		const TArray<FIntVector>& ChangedCells,
		FString& OutFailureReason)
	{
		if (Context.CompiledHardClosureSegments.IsEmpty())
		{
			return true;
		}

		TSet<int32> AffectedSegmentIndices;
		if (ChangedCells.IsEmpty())
		{
			for (int32 SegmentIndex = 0; SegmentIndex < Context.CompiledHardClosureSegments.Num(); ++SegmentIndex)
			{
				AffectedSegmentIndices.Add(SegmentIndex);
			}
		}
		else
		{
			for (const FIntVector& ChangedCell : ChangedCells)
			{
				const auto AddAffectedSegmentsForRoot = [&Context, &AffectedSegmentIndices](const FIntVector& RootCell)
				{
					if (const TArray<int32>* SegmentIndices = Context.HardClosureSegmentIndicesByProviderRoot.Find(RootCell))
					{
						AffectedSegmentIndices.Append(*SegmentIndices);
					}
				};
				if (const TArray<int32>* SegmentIndices = Context.HardClosureSegmentIndicesByPhysicalCell.Find(ChangedCell))
				{
					AffectedSegmentIndices.Append(*SegmentIndices);
				}
				AddAffectedSegmentsForRoot(ChangedCell);
				for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
				{
					AddAffectedSegmentsForRoot(
						ChangedCell + FLayoutDirectionUtils::ToCellDelta(static_cast<ELayoutFaceDirection>(DirectionIndex)));
				}
			}
		}

		for (const int32 SegmentIndex : AffectedSegmentIndices)
		{
			if (DoesHardClosureSegmentHaveLiveProvider(Context, SegmentIndex))
			{
				continue;
			}

			const FLayoutClosureCoverageSegmentRecord& Segment = Context.CompiledHardClosureSegments[SegmentIndex];
			OutFailureReason = FString::Printf(
				TEXT("Hard closure '%s' has no remaining provider for segment %s face %d."),
				*Segment.ClosureId.ToString(),
				*Segment.Cell.ToString(),
				static_cast<int32>(Segment.FaceDirection));
			return false;
		}

		return true;
	}

	void BuildInitialDomains(FSolveContext& Context)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_InitialDomains, STAT_PorismLayout_InitialDomains);
		if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::DomainBuilds, Context.Result.FailureReason))
		{
			Context.bTimeBudgetExceeded = true;
			return;
		}
		Context.InitialDomains.Reset();
		Context.InitialDomainAdmissionFailuresByCell.Reset();
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (!LayoutSolveExecution::Checkpoint(Context.Result.FailureReason))
			{
				Context.bTimeBudgetExceeded = true;
				return;
			}

			Context.InitialDomains.Add(
				PlannedCell.Cell,
				BuildOrderedCandidates(Context, PlannedCell.Cell, PlannedCell.Intent));
		}
	}

	/** Probes changed planned cells through production static candidate admission without entering CSP. */
	bool DoesMutatedPlanKeepStaticDomains(
		const FSolveContext& SourceContext,
		const TArray<FLayoutPlannedCell>& MutatedPlannedCells,
		const TSet<FIntVector>& AffectedCells,
		FIntVector& OutUnsupportedCell,
		FString& OutFailureReason,
		const bool bRequirePreparedPairSupport = false,
		const TSet<FIntVector>* AdmittedHostCells = nullptr,
		TSet<FIntVector>* OutHostRequiredCells = nullptr)
	{
		OutUnsupportedCell = FIntVector::ZeroValue;
		OutFailureReason.Reset();
		FSolveContext ProbeContext = SourceContext;
		ProbeContext.Result.FailureReason.Reset();
		ProbeContext.Result.Messages.Reset();
		FLayoutFrozenTerrainContract ProbeFrozenTerrainContract;
		if (SourceContext.FrozenTerrainContract != nullptr)
		{
			// Local project fix: reservation probes remove planned cells before
			// production reconciliation. Keep copied frozen authority aligned so
			// static admission measures topology rather than stale carrier counts.
			ProbeFrozenTerrainContract = *SourceContext.FrozenTerrainContract;
			TSet<FIntVector> MutatedCellSet;
			MutatedCellSet.Reserve(MutatedPlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : MutatedPlannedCells)
			{
				MutatedCellSet.Add(PlannedCell.Cell);
			}
			ProbeFrozenTerrainContract.ActiveCells.RemoveAll([&MutatedCellSet](const FLayoutContractActiveCellRecord& ActiveCell)
			{
				return !MutatedCellSet.Contains(ActiveCell.Cell);
			});
			ProbeFrozenTerrainContract.CellContracts.RemoveAll([&MutatedCellSet](const FLayoutTerrainCellContractRecord& CellContract)
			{
				return !MutatedCellSet.Contains(CellContract.Cell);
			});
			ProbeFrozenTerrainContract.TerrainBackedNeighborFaces.RemoveAll([&MutatedCellSet](const FLayoutTerrainBackedNeighborFaceRecord& FaceRecord)
			{
				return !MutatedCellSet.Contains(FaceRecord.Cell);
			});
			ProbeContext.FrozenTerrainContract = &ProbeFrozenTerrainContract;
		}
		BuildOverridePlan(ProbeContext, SourceContext.FootprintSize, MutatedPlannedCells);
		if (!ProbeContext.Result.FailureReason.IsEmpty())
		{
			OutFailureReason = ProbeContext.Result.FailureReason;
			return false;
		}
		if (bRequirePreparedPairSupport)
		{
			// Validate one concrete host selection while the outer reservation selector
			// still protects every frozen alternative from removal. Use the production
			// prepared prefix so composite shadows and route-driven neighbor occupancy
			// are proven before a void candidate is admitted.
			ProbeContext.VerticalAccessReservedCells.Reset();
			for (const FLayoutPlannedCell& PlannedCell : ProbeContext.Result.PlannedCells)
			{
				if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
				{
					ProbeContext.VerticalAccessReservedCells.Add(PlannedCell.Cell);
					ProbeContext.VerticalAccessReservedCells.Add(PlannedCell.Cell + FIntVector(0, 0, 1));
				}
			}
			if (!PrepareSolveContextThroughRouteDomainStageImpl(ProbeContext))
			{
				OutUnsupportedCell = ProbeContext.PreparedInitialDomainFailureCell;
				OutFailureReason = ProbeContext.Result.FailureReason;
				return false;
			}
			FinalizePreparedSolveContextForIndexedSearchImpl(ProbeContext);
			if (!ProbeContext.Result.FailureReason.IsEmpty()
				|| !PropagateUnsolvedDomains(ProbeContext, OutFailureReason))
			{
				OutUnsupportedCell = ProbeContext.PreparedInitialDomainFailureCell;
				if (OutFailureReason.IsEmpty())
				{
					OutFailureReason = ProbeContext.Result.FailureReason;
				}
				return false;
			}
			return true;
		}
		// Only topology changes in this probe; catalog variants remain source-owned.
		if (ProbeContext.Variants.IsEmpty()) BuildOrientedVariants(ProbeContext);
		else if (auto* Ledger = LayoutSolveExecution::CurrentThreadLedger()) ++Ledger->VariantReuses;

		TArray<FIntVector> OrderedAffectedCells = AffectedCells.Array();
		OrderedAffectedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			if (Left.Z != Right.Z) return Left.Z < Right.Z;
			if (Left.Y != Right.Y) return Left.Y < Right.Y;
			return Left.X < Right.X;
		});
		for (const FIntVector& AffectedCell : OrderedAffectedCells)
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			const ELayoutCellIntent* const Intent = ProbeContext.PlannedCellIntents.Find(AffectedCell);
			if (Intent == nullptr) continue;
			const bool bHasDomain = !BuildOrderedCandidates(ProbeContext, AffectedCell, *Intent).IsEmpty();
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			if (bHasDomain) continue;
			// Finalized topology may replace a provisional wall with an already admitted
			// exact stair bundle. This is a static domain alternative, not a placed proof;
			// selected-host preparation and joint CSP still have to discharge the cell.
			if (AdmittedHostCells != nullptr && AdmittedHostCells->Contains(AffectedCell))
			{
				if (OutHostRequiredCells != nullptr) OutHostRequiredCells->Add(AffectedCell);
				continue;
			}
			OutUnsupportedCell = AffectedCell;
			// Admission messages name intent, not sparse eligibility or mapped level authority.
			// Keep the failing cell and its six neighbors so a dense-probe rejection can be
			// distinguished from a real occupied overlap without relaxing either contract.
			TArray<FString> TopologyDetails;
			for (const FIntVector& Delta : {
				FIntVector::ZeroValue, FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
				FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) })
			{
				const FIntVector Cell = AffectedCell + Delta;
				const FLayoutPlannedCell* Planned = ProbeContext.Result.PlannedCells.FindByPredicate(
					[&Cell](const FLayoutPlannedCell& Candidate) { return Candidate.Cell == Cell; });
				const FLayoutId* ResidualRule = ProbeContext.TerrainResidualRuleIdByCell.Find(Cell);
				TopologyDetails.Add(FString::Printf(
					TEXT("%s planned=%d owning=%d intent=%s sourceLevel=%d resolvedLevel=%d bridge=%d topOffer=%d seam=%u residual=%s"),
					*Cell.ToString(), Planned != nullptr, ProbeContext.OwningTopologyCellsByPhysicalCell.Contains(Cell),
					Planned != nullptr ? ToDebugString(Planned->Intent) : TEXT("<none>"),
					Planned != nullptr ? Planned->ModuleLevelIndex : INDEX_NONE,
					LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(ProbeContext, Cell),
					Planned != nullptr && Planned->bIsBridgeCell,
					Planned != nullptr && Planned->bIsTopBridgeOffer,
					Planned != nullptr ? static_cast<uint32>(Planned->TerrainSeamFaceMask) : 0u,
					ResidualRule != nullptr ? *ResidualRule->ToString() : TEXT("<none>")));
			}
			const TArray<FString>* const AdmissionFailures = ProbeContext.InitialDomainAdmissionFailuresByCell.Find(AffectedCell);
			OutFailureReason = FString::Printf(
				TEXT("Static module domain at %s is empty after planned-cell mutation. ProbeTopology[seed=%d settledResiduals=%d cells=[%s]]. AdmissionFailures=[%s]"),
				*AffectedCell.ToString(), ProbeContext.Seed, ProbeContext.bTreatTerrainResidualsAsSettledEmpty,
				*FString::Join(TopologyDetails, TEXT(" | ")),
				AdmissionFailures == nullptr || AdmissionFailures->IsEmpty() ? TEXT("<none>") : *FString::Join(*AdmissionFailures, TEXT(" | ")));
			return false;
		}
		return LayoutSolveExecution::Checkpoint(OutFailureReason);
	}

	/** Probes only cells whose static admission can change when hard-open cells are removed. */
	bool DoesReservedOpenMaskKeepStaticDomains(
		const FSolveContext& SourceContext,
		const TSet<FIntVector>& ProspectiveReservedCells,
		const bool bRequirePreparedPairSupport,
		FIntVector& OutUnsupportedCell,
		FString& OutFailureReason)
	{
		TArray<FLayoutPlannedCell> ProbePlannedCells = SourceContext.Result.PlannedCells;
		ProbePlannedCells.RemoveAll([&ProspectiveReservedCells](const FLayoutPlannedCell& PlannedCell)
		{
			return ProspectiveReservedCells.Contains(PlannedCell.Cell);
		});
		TSet<FIntVector> AffectedCells;
		for (const FIntVector& ReservedCell : ProspectiveReservedCells)
		{
			for (const ELayoutFaceDirection Direction : { ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY, ELayoutFaceDirection::PosZ, ELayoutFaceDirection::NegZ })
			{
				AffectedCells.Add(ReservedCell + FLayoutDirectionUtils::ToCellDelta(Direction));
			}
			for (const FLayoutPlannedCell& PlannedCell : ProbePlannedCells)
			{
				if (PlannedCell.Cell.X == ReservedCell.X && PlannedCell.Cell.Y == ReservedCell.Y)
				{
					AffectedCells.Add(PlannedCell.Cell);
				}
			}
		}
		return DoesMutatedPlanKeepStaticDomains(
			SourceContext,
			ProbePlannedCells,
			AffectedCells,
			OutUnsupportedCell,
			OutFailureReason,
			bRequirePreparedPairSupport);
	}

	/** Finalizes flat authored Entry slots after current VA planning and before hard-open selection. */
	bool FinalizeAuthoredFlatEntries(FSolveContext& Context)
	{
		const FLayoutProfileSolveSnapshot& Profile = GetEffectiveProfileSnapshot(Context);
		const bool bHasSteppedOrTerrainSeamTopology = Context.Result.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.bIsBridgeCell
				|| Cell.TerrainSeamFaceMask != 0
				|| Cell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam;
		});
		if (bHasSteppedOrTerrainSeamTopology || Profile.EntryCountMode == ELayoutCountConstraintMode::None)
		{
			return true;
		}

		TArray<FLayoutPlannedCell> BaseCells = Context.Result.PlannedCells;
		const TSet<FIntVector> NoEntryCells;
		for (FLayoutPlannedCell& Cell : BaseCells)
		{
			if (Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary)
			{
				Cell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(Context.FootprintSize, NoEntryCells, Cell.Cell);
				Cell.EntryOrigin = ELayoutEntryOrigin::None;
			}
		}
		FSolveContext BaseContext = Context;
		BuildOverridePlan(BaseContext, Context.FootprintSize, BaseCells);
		if (BaseContext.Variants.IsEmpty()) BuildOrientedVariants(BaseContext);
		if (!BaseContext.Result.FailureReason.IsEmpty())
		{
			Context.Result.FailureReason = BaseContext.Result.FailureReason;
			return false;
		}

		TArray<FIntVector> EdgeSlots;
		TArray<FIntVector> CornerSlots;
		int32 EdgeTopologyCellCount = 0;
		int32 CornerTopologyCellCount = 0;
		FIntVector FirstRejectedEdgeCell = FIntVector::ZeroValue;
		FIntVector FirstRejectedCornerCell = FIntVector::ZeroValue;
		FString FirstEdgeFailureReason;
		FString FirstCornerFailureReason;
		for (const FLayoutPlannedCell& Slot : BaseContext.Result.PlannedCells)
		{
			if (Slot.Cell.Z != 0 || Slot.Intent != ELayoutCellIntent::Boundary || Slot.bIsBridgeCell
				|| (!Context.QualifiedEntryCells.IsEmpty() && !Context.QualifiedEntryCells.Contains(Slot.Cell)))
			{
				continue;
			}
			const bool bCornerSlot = Slot.PlacementZone == ELayoutPlacementZone::Corner;
			(bCornerSlot ? CornerTopologyCellCount : EdgeTopologyCellCount)++;
			TArray<FLayoutPlannedCell> CandidateCells = BaseCells;
			FLayoutPlannedCell* const Candidate = CandidateCells.FindByPredicate([SlotCell = Slot.Cell](const FLayoutPlannedCell& CandidateCell)
			{
				return CandidateCell.Cell == SlotCell;
			});
			if (Candidate == nullptr)
			{
				continue;
			}
			Candidate->Intent = ELayoutCellIntent::Entry;
			Candidate->EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
			TSet<FIntVector> AffectedCells = { Slot.Cell };
			for (const ELayoutFaceDirection Direction : { ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY, ELayoutFaceDirection::PosZ, ELayoutFaceDirection::NegZ })
			{
				AffectedCells.Add(Slot.Cell + FLayoutDirectionUtils::ToCellDelta(Direction));
			}
			FIntVector UnsupportedCell = FIntVector::ZeroValue;
			FString FailureReason;
			if (!DoesMutatedPlanKeepStaticDomains(BaseContext, CandidateCells, AffectedCells, UnsupportedCell, FailureReason))
			{
				FString& FirstZoneFailureReason = bCornerSlot ? FirstCornerFailureReason : FirstEdgeFailureReason;
				FIntVector& FirstRejectedZoneCell = bCornerSlot ? FirstRejectedCornerCell : FirstRejectedEdgeCell;
				if (FirstZoneFailureReason.IsEmpty())
				{
					FirstRejectedZoneCell = Slot.Cell;
					FirstZoneFailureReason = MoveTemp(FailureReason);
				}
				continue;
			}
			(bCornerSlot ? CornerSlots : EdgeSlots).Add(Slot.Cell);
		}

		const uint32 SelectionSeed = HashCombine(GetTypeHash(Context.FootprintSize), static_cast<uint32>(Context.Seed));
		const int32 RequestedCount = ResolveProfileCount(Profile.EntryCountMode, Profile.EntryCount, Profile.MinEntryCount, Profile.MaxEntryCount, MAX_int32, SelectionSeed);
		if (RequestedCount > EdgeSlots.Num() + CornerSlots.Num())
		{
			const bool bCatalogSupportsEdge = DoesCatalogSupportExteriorEntryZone(Context.ModuleSnapshots, ELayoutPlacementZone::Edge);
			const bool bCatalogSupportsCorner = DoesCatalogSupportExteriorEntryZone(Context.ModuleSnapshots, ELayoutPlacementZone::Corner);
			const bool bReportEdgeRejection = bCatalogSupportsEdge
				&& (EdgeTopologyCellCount > 0 || !bCatalogSupportsCorner || CornerTopologyCellCount == 0);
			const ELayoutPlacementZone ReportedZone = bReportEdgeRejection
				? ELayoutPlacementZone::Edge
				: ELayoutPlacementZone::Corner;
			const int32 ReportedTopologyCellCount = bReportEdgeRejection
				? EdgeTopologyCellCount
				: CornerTopologyCellCount;
			const FIntVector& FirstRejectedZoneCell = bReportEdgeRejection
				? FirstRejectedEdgeCell
				: FirstRejectedCornerCell;
			const FString& FirstZoneFailureReason = bReportEdgeRejection
				? FirstEdgeFailureReason
				: FirstCornerFailureReason;
			const FString RejectionDetail = ReportedTopologyCellCount == 0
				? FString::Printf(
					TEXT("No %s topology cells exist in the selected footprint."),
					*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(ReportedZone)))
				: FString::Printf(
					TEXT("First %s rejection=cell %s. %s"),
					*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(ReportedZone)),
					*FirstRejectedZoneCell.ToString(),
					FirstZoneFailureReason.IsEmpty() ? TEXT("No admitted slot.") : *FirstZoneFailureReason);
			Context.Result.PreparationFailureKind = ELayoutSolvePreparationFailureKind::TraversalTopologyInfeasible;
			Context.Result.FailureReason = FString::Printf(
				TEXT("Entry count mode '%s' requires exactly %d feasible exterior Entry cells, but finalized flat plan has %d Edge and %d Corner slots. Footprint=%dx%d EdgeTopologyCells=%d CornerTopologyCells=%d CatalogSupportsEdge=%s CatalogSupportsCorner=%s. %s"),
				*StaticEnum<ELayoutCountConstraintMode>()->GetNameStringByValue(static_cast<int64>(Profile.EntryCountMode)),
				RequestedCount,
				EdgeSlots.Num(),
				CornerSlots.Num(),
				Context.FootprintSize.X,
				Context.FootprintSize.Y,
				EdgeTopologyCellCount,
				CornerTopologyCellCount,
				bCatalogSupportsEdge ? TEXT("true") : TEXT("false"),
				bCatalogSupportsCorner ? TEXT("true") : TEXT("false"),
				*RejectionDetail);
			return false;
		}
		TArray<FIntVector> SelectedSlots;
		AddMaximallySeparatedCells(EdgeSlots, Context.FootprintSize, FMath::Min(RequestedCount, EdgeSlots.Num()), SelectionSeed, SelectedSlots);
		AddMaximallySeparatedCells(CornerSlots, Context.FootprintSize, RequestedCount - SelectedSlots.Num(), HashCombineFast(SelectionSeed, GetTypeHash(FName(TEXT("CornerEntryFallback")))), SelectedSlots);
		for (FLayoutPlannedCell& Cell : BaseCells)
		{
			if (SelectedSlots.Contains(Cell.Cell))
			{
				Cell.Intent = ELayoutCellIntent::Entry;
				Cell.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
			}
		}
		BuildOverridePlan(Context, Context.FootprintSize, BaseCells);
		return true;
	}

	/** Verifies flat root planning retained every authored VerticalAccess obligation before CSP. */
	bool ValidateAuthoredFlatVerticalAccessCount(FSolveContext& Context)
	{
		if (Context.RegionScope != ELayoutContractRegionScope::Root)
		{
			return true;
		}

		const FLayoutProfileSolveSnapshot& Profile = GetEffectiveProfileSnapshot(Context);
		if (Profile.VerticalAccessCountMode == ELayoutCountConstraintMode::None
			|| Context.Result.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
			{
				return Cell.bIsBridgeCell
					|| Cell.TerrainSeamFaceMask != 0
					|| Cell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam;
			}))
		{
			return true;
		}

		const int32 MinimumCount = Profile.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
			? Profile.VerticalAccessCount
			: Profile.MinVerticalAccessCount;
		const int32 MaximumCount = Profile.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
			? Profile.VerticalAccessCount
			: Profile.MaxVerticalAccessCount;
		for (int32 TransitionLevel = 0; TransitionLevel < FMath::Max(0, Profile.LevelCount - 1); ++TransitionLevel)
		{
			int32 ActualCount = 0;
			for (const FLayoutPlannedCell& Cell : Context.Result.PlannedCells)
			{
				ActualCount += Cell.Cell.Z == TransitionLevel && Cell.Intent == ELayoutCellIntent::VerticalAccess;
			}
			if (ActualCount < MinimumCount || ActualCount > MaximumCount)
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Flat root VerticalAccess transition Z=%d requires %d..%d authored cells, but final planning reserved %d. Rejecting before CSP."),
					TransitionLevel,
					MinimumCount,
					MaximumCount,
					ActualCount);
				return false;
			}
		}
		return true;
	}

	/** Verifies later internal Entry origins cannot weaken deterministic authored exterior count. */
	bool ValidateAuthoredExteriorEntryCount(FSolveContext& Context)
	{
		if (Context.RegionScope == ELayoutContractRegionScope::Continuation)
		{
			return true;
		}

		const FLayoutProfileSolveSnapshot& Profile = GetEffectiveProfileSnapshot(Context);
		const int32 ExpectedCount = ResolveProfileCount(
			Profile.EntryCountMode,
			Profile.EntryCount,
			Profile.MinEntryCount,
			Profile.MaxEntryCount,
			MAX_int32,
			HashCombine(GetTypeHash(Context.FootprintSize), static_cast<uint32>(Context.Seed)));
		int32 AuthoredEntryCount = 0;
		int32 ChildContractEntryCount = 0;
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			AuthoredEntryCount += PlannedCell.Intent == ELayoutCellIntent::Entry
				&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary;
			ChildContractEntryCount += PlannedCell.Intent == ELayoutCellIntent::Entry
				&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::ChildContract;
		}
		const bool bCountSatisfied = Context.bUsesChildSolveContext
			? AuthoredEntryCount <= ExpectedCount
				&& AuthoredEntryCount + ChildContractEntryCount >= ExpectedCount
			: AuthoredEntryCount == ExpectedCount;
		if (bCountSatisfied)
		{
			return true;
		}
		const FString PlannedEntryState = FString::JoinBy(
			Context.Result.PlannedCells,
			TEXT(","),
			[](const FLayoutPlannedCell& PlannedCell)
			{
				return PlannedCell.Intent == ELayoutCellIntent::Entry
					? FString::Printf(
						TEXT("%s/origin=%d"),
						*PlannedCell.Cell.ToString(),
						static_cast<int32>(PlannedCell.EntryOrigin))
					: FString();
			});
		Context.Result.FailureReason = FString::Printf(
			TEXT("Authored exterior Entry count mismatch: expected %d, found authored=%d childContract=%d. Planned Entry cells=[%s]. Qualified Entry cells=[%s]. Continuation and terrain-seam Entry origins do not satisfy authored exterior count."),
			ExpectedCount,
			AuthoredEntryCount,
			ChildContractEntryCount,
			*PlannedEntryState,
			*FString::JoinBy(Context.QualifiedEntryCells, TEXT(","), [](const FIntVector& Cell)
			{
				return Cell.ToString();
			}));
		return false;
	}

	/** Fails preparation at first stable empty domain instead of deferring to secondary neighbor diagnostics. */
	bool ValidatePreparedInitialDomainsNonEmpty(FSolveContext& Context)
	{
		TArray<const FLayoutPlannedCell*> SortedCells;
		SortedCells.Reserve(Context.Result.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			SortedCells.Add(&PlannedCell);
		}
		SortedCells.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
		{
			if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
			if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
			return Left.Cell.X < Right.Cell.X;
		});

		for (const FLayoutPlannedCell* const PlannedCell : SortedCells)
		{
			const TArray<FSolveCandidate>* const Domain = Context.InitialDomains.Find(PlannedCell->Cell);
			if (Domain != nullptr && !Domain->IsEmpty())
			{
				continue;
			}

			// A composite shadow cell needs no anchor domain when a separately anchored,
			// already-admitted initial candidate atomically covers it.
			bool bCoveredByInitialCompositeCandidate = false;
			for (const TPair<FIntVector, TArray<FSolveCandidate>>& DomainPair : Context.InitialDomains)
			{
				if (DomainPair.Key == PlannedCell->Cell)
				{
					continue;
				}

				for (const FSolveCandidate& Candidate : DomainPair.Value)
				{
					if (LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate)
						&& BuildCandidateWorldOccupiedCells(Context, DomainPair.Key, Candidate).Contains(PlannedCell->Cell))
					{
						bCoveredByInitialCompositeCandidate = true;
						break;
					}
				}
				if (bCoveredByInitialCompositeCandidate)
				{
					break;
				}
			}
			if (bCoveredByInitialCompositeCandidate)
			{
				continue;
			}

			const FIntVector UpperCell = PlannedCell->Cell + FIntVector(0, 0, 1);
			const FLayoutPlannedCell* const DirectUpperCell = Context.Result.PlannedCells.FindByPredicate(
				[UpperCell](const FLayoutPlannedCell& Candidate)
				{
					return Candidate.Cell == UpperCell;
				});
			const TArray<FString>* const AdmissionFailures =
				Context.InitialDomainAdmissionFailuresByCell.Find(PlannedCell->Cell);
			Context.bHasPreparedInitialDomainFailure = true;
			Context.PreparedInitialDomainFailureCell = PlannedCell->Cell;
			if (PlannedCell->bIsBridgeCell)
			{
				Context.Result.PreparationFailureKind =
					ELayoutSolvePreparationFailureKind::SteppedTerrainModuleDomainInfeasible;
			}
			Context.Result.FailureReason = FString::Printf(
				TEXT("Prepared solve has no initial candidates at %s. Intent=%s PlacementZone=%s Bridge=%d DirectUpperBridge=%s Endpoint=%d RouteRequired=%d. AdmissionFailures=[%s]"),
				*PlannedCell->Cell.ToString(),
				ToDebugString(PlannedCell->Intent),
				*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
					static_cast<int64>(Context.PlacementZonesByCell.FindRef(PlannedCell->Cell))),
				PlannedCell->bIsBridgeCell ? 1 : 0,
				DirectUpperCell != nullptr && DirectUpperCell->bIsBridgeCell
					? *UpperCell.ToString()
					: TEXT("None"),
				Context.CommittedEndpointAnchors.ContainsByPredicate([PlannedCell](const FLayoutCommittedEndpointAnchor& Anchor)
				{
					return Anchor.LocalCell == PlannedCell->Cell;
				}) ? 1 : 0,
				Context.RouteConstraintIndexByCell.Contains(PlannedCell->Cell)
					|| Context.RequiredRouteConstraints.ContainsByPredicate([PlannedCell](const FLayoutRouteConstraintRecord& Constraint)
					{
						return Constraint.Cell == PlannedCell->Cell;
					}) ? 1 : 0,
				AdmissionFailures == nullptr || AdmissionFailures->IsEmpty()
					? TEXT("<none>")
					: *FString::Join(*AdmissionFailures, TEXT(" | ")));
			return false;
		}
		return true;
	}

	int32 GetIndexedCandidateWordCount(const int32 CandidateCount)
	{
		return CandidateCount > 0 ? ((CandidateCount + 63) / 64) : 0;
	}

	void SetIndexedCandidateBit(TArray<uint64>& Bits, const int32 CandidateIndex)
	{
		if (CandidateIndex < 0)
		{
			return;
		}

		const int32 WordIndex = CandidateIndex / 64;
		const int32 BitIndex = CandidateIndex % 64;
		if (!Bits.IsValidIndex(WordIndex))
		{
			return;
		}

		Bits[WordIndex] |= (uint64(1) << BitIndex);
	}

	void ClearIndexedCandidateBit(TArray<uint64>& Bits, const int32 CandidateIndex)
	{
		if (CandidateIndex < 0)
		{
			return;
		}

		const int32 WordIndex = CandidateIndex / 64;
		const int32 BitIndex = CandidateIndex % 64;
		if (!Bits.IsValidIndex(WordIndex))
		{
			return;
		}

		Bits[WordIndex] &= ~(uint64(1) << BitIndex);
	}

	int32 CountIndexedCandidateBits(const TArray<uint64>& Bits)
	{
		int32 Count = 0;
		for (const uint64 Word : Bits)
		{
			Count += FMath::CountBits(Word);
		}

		return Count;
	}

	uint64 BuildIndexedCompatibilityKey(const int32 CandidateIndex, const ELayoutFaceDirection Direction)
	{
		return (static_cast<uint64>(static_cast<uint32>(CandidateIndex)) << 3)
			| static_cast<uint64>(static_cast<uint8>(Direction));
	}

	FSolveCandidate BuildSolveCandidateFromIndexedCandidate(const FLayoutIndexedDomainCandidate& IndexedCandidate)
	{
		FSolveCandidate Candidate;
		Candidate.YawRotationSteps = IndexedCandidate.YawRotationSteps;
		Candidate.VariantIndex = IndexedCandidate.VariantIndex;
		Candidate.ModuleSnapshotIndex = IndexedCandidate.ModuleSnapshotIndex;
		Candidate.ModuleSnapshotId = IndexedCandidate.ModuleSnapshotId;
		Candidate.bEmpty = IndexedCandidate.bEmpty;
		return Candidate;
	}

	void BuildIndexedCellDomains(FSolveContext& Context);

	void InitializeIndexedCandidateUniverse(FSolveContext& Context)
	{
		Context.IndexedCandidates.Reset();
		Context.EmptyIndexedCandidateIndex = INDEX_NONE;
		Context.IndexedCandidateWordCount = 0;

		Context.IndexedCandidates.Reserve(Context.Variants.Num() + 1);
		for (int32 VariantIndex = 0; VariantIndex < Context.Variants.Num(); ++VariantIndex)
		{
			const FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[VariantIndex];
			FLayoutIndexedDomainCandidate& Candidate = Context.IndexedCandidates.AddDefaulted_GetRef();
			Candidate.CandidateIndex = VariantIndex;
			Candidate.VariantIndex = VariantIndex;
			Candidate.ModuleSnapshotIndex = Variant.ModuleSnapshotIndex;
			Candidate.ModuleSnapshotId = Variant.ModuleSnapshotId;
			Candidate.ModuleDebugName = Variant.ModuleDebugName;
			Candidate.YawRotationSteps = Variant.YawRotationSteps;
			Candidate.bEmpty = false;
		}

		bool bUsesEmptyCandidate = false;
		for (const TPair<FIntVector, TArray<FSolveCandidate>>& DomainPair : Context.InitialDomains)
		{
			for (const FSolveCandidate& Candidate : DomainPair.Value)
			{
				if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
				{
					bUsesEmptyCandidate = true;
					break;
				}
			}

			if (bUsesEmptyCandidate)
			{
				break;
			}
		}

		if (bUsesEmptyCandidate)
		{
			Context.EmptyIndexedCandidateIndex = Context.IndexedCandidates.Num();
			FLayoutIndexedDomainCandidate& EmptyCandidate = Context.IndexedCandidates.AddDefaulted_GetRef();
			EmptyCandidate.CandidateIndex = Context.EmptyIndexedCandidateIndex;
			EmptyCandidate.VariantIndex = INDEX_NONE;
			EmptyCandidate.ModuleDebugName = TEXT("Empty");
			EmptyCandidate.bEmpty = true;
		}

		Context.IndexedCandidateWordCount = GetIndexedCandidateWordCount(Context.IndexedCandidates.Num());
		BuildIndexedCellDomains(Context);
	}

	void BuildIndexedCompatibilityRows(FSolveContext& Context)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_CompatibilityIndex, STAT_PorismLayout_CompatibilityIndex);
		// Indexed compatibility rows are intentionally context-free. They answer
		// only whether two candidate faces could ever coexist across one shared
		// edge, leaving live compiled-carrier facts and branch-dependent checks
		// to the per-cell admission / legality path.
		Context.IndexedCompatibilityRows.Reset();
		Context.IndexedCompatibilityRowByKey.Reset();
		Context.IndexedCompatibilityRows.Reserve(Context.IndexedCandidates.Num() * 6);
		Context.IndexedCompatibilityRowByKey.Reserve(Context.IndexedCandidates.Num() * 6);

		for (const FLayoutIndexedDomainCandidate& SourceIndexedCandidate : Context.IndexedCandidates)
		{
			const FSolveCandidate SourceCandidate = BuildSolveCandidateFromIndexedCandidate(SourceIndexedCandidate);
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
				const int32 RowIndex = Context.IndexedCompatibilityRows.Num();
				FLayoutIndexedCandidateCompatibility& Row = Context.IndexedCompatibilityRows.AddDefaulted_GetRef();
				Row.SourceCandidateIndex = SourceIndexedCandidate.CandidateIndex;
				Row.Direction = Direction;
				Row.CompatibleCandidateBits.Init(0, Context.IndexedCandidateWordCount);
				Context.IndexedCompatibilityRowByKey.Add(BuildIndexedCompatibilityKey(SourceIndexedCandidate.CandidateIndex, Direction), RowIndex);

				for (const FLayoutIndexedDomainCandidate& TargetIndexedCandidate : Context.IndexedCandidates)
				{
					const FSolveCandidate TargetCandidate = BuildSolveCandidateFromIndexedCandidate(TargetIndexedCandidate);
					if (AreAdjacentCandidatesCompatible(Context, SourceCandidate, TargetCandidate, Direction))
					{
						SetIndexedCandidateBit(Row.CompatibleCandidateBits, TargetIndexedCandidate.CandidateIndex);
					}
				}
			}
		}
	}

	int32 GetIndexedCandidateIndex(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		return LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) ? Candidate.VariantIndex : Context.EmptyIndexedCandidateIndex;
	}

	const FLayoutIndexedCandidateCompatibility* FindIndexedCompatibilityRow(
		const FSolveContext& Context,
		const int32 CandidateIndex,
		const ELayoutFaceDirection Direction)
	{
		if (const int32* RowIndex = Context.IndexedCompatibilityRowByKey.Find(BuildIndexedCompatibilityKey(CandidateIndex, Direction)))
		{
			return Context.IndexedCompatibilityRows.IsValidIndex(*RowIndex)
				? &Context.IndexedCompatibilityRows[*RowIndex]
				: nullptr;
		}

		return Context.IndexedCompatibilityRows.FindByPredicate([CandidateIndex, Direction](const FLayoutIndexedCandidateCompatibility& Row)
		{
			return Row.SourceCandidateIndex == CandidateIndex && Row.Direction == Direction;
		});
	}

	void BuildIndexedDomainBitsForCandidates(const FSolveContext& Context, const TArray<FSolveCandidate>& Candidates, TArray<uint64>& OutBits)
	{
		OutBits.Init(0, Context.IndexedCandidateWordCount);
		for (const FSolveCandidate& Candidate : Candidates)
		{
			SetIndexedCandidateBit(OutBits, GetIndexedCandidateIndex(Context, Candidate));
		}
	}

	void IntersectIndexedCandidateBits(TArray<uint64>& Bits, const TArray<uint64>& Mask)
	{
		const int32 SharedWordCount = FMath::Min(Bits.Num(), Mask.Num());
		for (int32 WordIndex = 0; WordIndex < SharedWordCount; ++WordIndex)
		{
			Bits[WordIndex] &= Mask[WordIndex];
		}

		for (int32 WordIndex = SharedWordCount; WordIndex < Bits.Num(); ++WordIndex)
		{
			Bits[WordIndex] = 0;
		}
	}

	bool HasAnySharedIndexedCandidateBit(const TArray<uint64>& LeftBits, const TArray<uint64>& RightBits)
	{
		const int32 SharedWordCount = FMath::Min(LeftBits.Num(), RightBits.Num());
		for (int32 WordIndex = 0; WordIndex < SharedWordCount; ++WordIndex)
		{
			if ((LeftBits[WordIndex] & RightBits[WordIndex]) != 0)
			{
				return true;
			}
		}

		return false;
	}

	bool IsIndexedCandidateBitSet(const TArray<uint64>& Bits, const int32 CandidateIndex)
	{
		if (CandidateIndex < 0)
		{
			return false;
		}

		const int32 WordIndex = CandidateIndex / 64;
		const int32 BitIndex = CandidateIndex % 64;
		return Bits.IsValidIndex(WordIndex) && ((Bits[WordIndex] & (uint64(1) << BitIndex)) != 0);
	}

	void BuildIndexedCellDomains(FSolveContext& Context)
	{
		Context.IndexedOrderedDomainCandidatesByCell.Reset();
		Context.IndexedDomainBitsByCell.Reset();

		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested())
			{
				Context.bTimeBudgetExceeded = true;
				if (Context.Result.FailureReason.IsEmpty())
				{
					Context.Result.FailureReason = TEXT("Layout solve canceled while building indexed domains.");
				}
				return;
			}

			const TArray<FSolveCandidate>* CurrentDomain = Context.InitialDomains.Find(PlannedCell.Cell);
			if (CurrentDomain == nullptr)
			{
				continue;
			}

			TArray<int32>& OrderedCandidateIndices = Context.IndexedOrderedDomainCandidatesByCell.FindOrAdd(PlannedCell.Cell);
			OrderedCandidateIndices.Reset();
			OrderedCandidateIndices.Reserve(CurrentDomain->Num());

			TArray<uint64>& DomainBits = Context.IndexedDomainBitsByCell.FindOrAdd(PlannedCell.Cell);
			DomainBits.Init(0, Context.IndexedCandidateWordCount);

			for (const FSolveCandidate& Candidate : *CurrentDomain)
			{
				const int32 CandidateIndex = GetIndexedCandidateIndex(Context, Candidate);
				OrderedCandidateIndices.Add(CandidateIndex);
				SetIndexedCandidateBit(DomainBits, CandidateIndex);
			}
		}
	}

	FSolveCandidate BuildSolveCandidateFromIndex(const FSolveContext& Context, const int32 CandidateIndex)
	{
		if (!Context.IndexedCandidates.IsValidIndex(CandidateIndex))
		{
			return FSolveCandidate();
		}

		return BuildSolveCandidateFromIndexedCandidate(Context.IndexedCandidates[CandidateIndex]);
	}

	bool PrepareSolveContextThroughRouteDomainStageImpl(FSolveContext& Context, bool bReusePreparedVariants);
	void FinalizePreparedSolveContextForIndexedSearchImpl(FSolveContext& Context);
	struct FPreparedSearchFrontier
	{
		TArray<FIntVector> ForcedCells;
		FIntVector SelectedCell = FIntVector::ZeroValue;
		TArray<FSolveCandidate> SelectedCandidates;
		bool bSolvedAfterForcedPlacements = false;
	};

	enum class EPreparedSearchBranchOutcome : uint8
	{
		Solved,
		Rejected,
		AbortSolve
	};

	enum class EPreparedSearchBranchApplyOutcome : uint8
	{
		ReadyForRecursiveContinuation,
		Rejected,
		AbortSolve
	};

	enum class EPreparedSearchBranchExactReductionOutcome : uint8
	{
		ReadyForNormalBranchSolve,
		ReadyForUniqueCandidateContinuation,
		RejectedAllCandidates,
		AbortSolve
	};

	/**
	 * Mutable branch-local result for the immediate post-prefix placement and
	 * forward-check phase before deeper recursion begins.
	 *
	 * This stage stays explicit so later threading work can separate branch
	 * application / propagation feasibility from recursive continuation and
	 * rollback ownership.
	 */
	struct FPreparedSearchBranchApplyStageResult
	{
		FPreparedSearchBranchWorkItem WorkItem;
		bool bOccupiedCandidate = false;
		TArray<FIntVector> PlacedCells;
		FString ForwardCheckFailure;
	};

	const TCHAR* ToDebugString(const ELayoutCellIntent Intent);
	bool ForwardCheckNeighbors(
		FSolveContext& Context,
		const FIntVector& Cell,
		FString& OutFailureReason);
	void ClearBacktrackableFailure(FSolveContext& Context);
	bool HasExceededSolverTimeBudget(
		FSolveContext& Context,
		const int32 SolveDepth);
	FString BuildCellDebugString(
		const FSolveContext& Context,
		const FIntVector& Cell);
	void MemoizeFailedState(FSolveContext& Context);
	void RollbackForcedPlacements(
		FSolveContext& Context,
		const TArray<FIntVector>& ForcedCells);

	bool TryPrepareSolveContextThroughSearchPrefixStageInternal(
		FSolveContext& Context,
		const int32 SolveDepth,
		FPreparedSearchFrontier& OutSearchFrontier);

	bool ContinueSolveFromPreparedSearchBranchStage(
		FSolveContext& Context,
		const int32 SolveDepth,
		const FPreparedSearchBranchStageCarrier& SearchBranchStage);

	bool ContinueSolveFromPreparedSearchFrontier(
		FSolveContext& Context,
		const int32 SolveDepth,
		FPreparedSearchFrontier& SearchFrontier);

	static bool ContinuePreparedSolveContextAfterDeterministicSingleCandidateStagesImpl(
		FSolveContext& Context,
		const int32 InitialSolveDepth);

	bool SolveRecursive(FSolveContext& Context, const int32 SolveDepth);
	bool ContinuePreparedSolveContextAfterRouteDomainStageImpl(
		FSolveContext& Context,
		const bool bValidateClosureContracts);
	static bool TryValidatePostBranchContinuationFeasibility(
		FSolveContext& Context,
		const int32 NextSolveDepth,
		const FIntVector& BranchCell,
		FString& OutFailureReason);

	static void BuildPreparedSearchBranchWorkItems(
		const FPreparedSearchFrontier& SearchFrontier,
		const ELayoutCellIntent Intent,
		TArray<FPreparedSearchBranchWorkItem>& OutWorkItems)
	{
		OutWorkItems.Reset();
		OutWorkItems.Reserve(SearchFrontier.SelectedCandidates.Num());
		for (const FSolveCandidate& Candidate : SearchFrontier.SelectedCandidates)
		{
			FPreparedSearchBranchWorkItem& WorkItem = OutWorkItems.Emplace_GetRef();
			WorkItem.Cell = SearchFrontier.SelectedCell;
			WorkItem.Intent = Intent;
			WorkItem.Candidate = Candidate;
		}
	}

	static void PopulatePreparedSearchBranchStageCarrier(
		const LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier& SearchPrefixStage,
		LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier& OutCarrier)
	{
		OutCarrier = LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier{};
		OutCarrier.SearchPrefixStage = SearchPrefixStage;
		OutCarrier.WorkItems.Reserve(SearchPrefixStage.SelectedCandidates.Num());
		for (const FSolveCandidate& Candidate :
			SearchPrefixStage.SelectedCandidates)
		{
			FPreparedSearchBranchWorkItem& WorkItem =
				OutCarrier.WorkItems.Emplace_GetRef();
			WorkItem.Cell = SearchPrefixStage.SelectedCell;
			WorkItem.Intent = SearchPrefixStage.Summary.SelectedIntent;
			WorkItem.Candidate = Candidate;
		}
	}

	static void PopulatePreparedSearchBranchApplyStageCarrier(
		const LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier&
			SearchBranchStage,
		const FPreparedSearchBranchApplyStageResult& ApplyStage,
		const int32 SolveDepth,
		LayoutProfileSolverInternal::FPreparedSearchBranchApplyStageCarrier&
			OutCarrier)
	{
		OutCarrier = LayoutProfileSolverInternal::FPreparedSearchBranchApplyStageCarrier{};
		OutCarrier.SearchBranchStage = SearchBranchStage;
		OutCarrier.WorkItem = ApplyStage.WorkItem;
		OutCarrier.bOccupiedCandidate = ApplyStage.bOccupiedCandidate;
		OutCarrier.PlacedCells = ApplyStage.PlacedCells;
		OutCarrier.NextSolveDepth = SolveDepth + 1;
	}

	static void RollbackPreparedSearchBranchApplyStage(
		FSolveContext& Context,
		const FPreparedSearchBranchApplyStageResult& ApplyStage)
	{
		if (ApplyStage.bOccupiedCandidate)
		{
			RollbackOccupiedCandidateBundle(Context, ApplyStage.PlacedCells);
			return;
		}

		Context.Placements.Remove(ApplyStage.WorkItem.Cell);
	}

	static void RollbackPreparedSearchBranchApplyStageCarrier(
		FSolveContext& Context,
		const LayoutProfileSolverInternal::FPreparedSearchBranchApplyStageCarrier&
			ApplyStageCarrier)
	{
		FPreparedSearchBranchApplyStageResult ApplyStage;
		ApplyStage.WorkItem = ApplyStageCarrier.WorkItem;
		ApplyStage.bOccupiedCandidate = ApplyStageCarrier.bOccupiedCandidate;
		ApplyStage.PlacedCells = ApplyStageCarrier.PlacedCells;
		RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
	}

	static EPreparedSearchBranchApplyOutcome TryApplyPreparedSearchBranchWorkItemStage(
		FSolveContext& Context,
		const int32 SolveDepth,
		const FPreparedSearchBranchWorkItem& WorkItem,
		FPreparedSearchBranchApplyStageResult& OutApplyStage,
		const bool bConsumeCandidateAttemptBudget = true)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_BranchApply, STAT_PorismLayout_BranchApply);
		OutApplyStage = FPreparedSearchBranchApplyStageResult{};
		OutApplyStage.WorkItem = WorkItem;
		OutApplyStage.bOccupiedCandidate =
			LayoutProfileSolverInternal::IsOccupiedCandidate(WorkItem.Candidate);

		if (HasExceededSolverTimeBudget(Context, SolveDepth))
		{
			return EPreparedSearchBranchApplyOutcome::AbortSolve;
		}

		if (bConsumeCandidateAttemptBudget)
		{
			if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::CandidateAttempts, Context.Result.FailureReason))
			{
				Context.bTimeBudgetExceeded = true;
				return EPreparedSearchBranchApplyOutcome::AbortSolve;
			}
			++Context.CandidateAttemptCount;
			if (Context.CandidateAttemptCount > Context.MaxCandidateAttempts)
			{
				if (Context.Result.FailureReason.IsEmpty())
				{
					Context.Result.FailureReason = FString::Printf(
						TEXT("Layout solve exceeded MaxSolverCandidateAttempts=%d while evaluating intent %s(%d) at cell %s after placing %d of %d planned cells."),
						Context.MaxCandidateAttempts,
						ToDebugString(WorkItem.Intent),
						static_cast<int32>(WorkItem.Intent),
						*WorkItem.Cell.ToString(),
						SolveDepth,
						Context.SolveOrder.Num());
				}
				AddTraceEvent(Context, FString::Printf(
					TEXT("attempt-budget-exceeded depth=%d cell=%s attempts=%d"),
					SolveDepth,
					*WorkItem.Cell.ToString(),
					Context.CandidateAttemptCount));
				return EPreparedSearchBranchApplyOutcome::AbortSolve;
			}
		}

		if (Context.bIncludeTraceCandidateDetails
			&& bConsumeCandidateAttemptBudget)
		{
			AddTraceEvent(Context, FString::Printf(
				TEXT("try-candidate depth=%d cell=%s candidate=%s attempt=%d"),
				SolveDepth,
				*WorkItem.Cell.ToString(),
				OutApplyStage.bOccupiedCandidate
					? *BuildCandidateDebugName(Context, WorkItem.Candidate)
					: TEXT("<empty>"),
				Context.CandidateAttemptCount));
		}

		if (!OutApplyStage.bOccupiedCandidate)
		{
			Context.Placements.Add(
				WorkItem.Cell,
				MakeRootSolvePlacement(Context, WorkItem.Cell, 0, INDEX_NONE));

			if (!ForwardCheckNeighbors(
					Context,
					WorkItem.Cell,
					OutApplyStage.ForwardCheckFailure))
			{
				return EPreparedSearchBranchApplyOutcome::Rejected;
			}

		return EPreparedSearchBranchApplyOutcome::
				ReadyForRecursiveContinuation;
	}

		OutApplyStage.PlacedCells = CommitOccupiedCandidateBundle(
			Context,
			WorkItem.Cell,
			WorkItem.Candidate);
		if (!ForwardCheckPlacedCells(
				Context,
				OutApplyStage.PlacedCells,
				OutApplyStage.ForwardCheckFailure))
		{
			return EPreparedSearchBranchApplyOutcome::Rejected;
		}

		if (bConsumeCandidateAttemptBudget
			&& !TryValidatePostBranchContinuationFeasibility(
				Context,
				SolveDepth + 1,
				WorkItem.Cell,
				OutApplyStage.ForwardCheckFailure))
		{
			return EPreparedSearchBranchApplyOutcome::Rejected;
		}

		return EPreparedSearchBranchApplyOutcome::ReadyForRecursiveContinuation;
	}

	/**
	 * Evaluates one post-prefix candidate branch after the deterministic search
	 * frontier has chosen the next cell.
	 *
	 * This branch-local stage stays explicit so future threading work can fan
	 * out immutable selected-cell candidate work items instead of reopening the
	 * whole search-frontier loop.
	 */
	static EPreparedSearchBranchOutcome TrySolvePreparedSearchBranchWorkItem(
		FSolveContext& Context,
		const int32 SolveDepth,
		const FPreparedSearchBranchWorkItem& WorkItem,
		TArray<FString>& InOutCandidateFailureReasons)
	{
		FPreparedSearchBranchApplyStageResult ApplyStage;
			const EPreparedSearchBranchApplyOutcome ApplyOutcome =
				TryApplyPreparedSearchBranchWorkItemStage(
					Context,
					SolveDepth,
					WorkItem,
					ApplyStage);
		if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::AbortSolve)
		{
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty()
				&& Context.PlannedCellIntents.FindRef(WorkItem.Cell)
					== ELayoutCellIntent::VerticalAccess)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[VerticalAccessCandidateAbort] region=%s cell=%s attempts=%d limit=%d reason=%s"),
					*Context.RegionDebugPath,
					*WorkItem.Cell.ToString(),
					Context.CandidateAttemptCount,
					Context.MaxCandidateAttempts,
					*Context.Result.FailureReason);
			}
			return EPreparedSearchBranchOutcome::AbortSolve;
		}
		if (ApplyOutcome
			== EPreparedSearchBranchApplyOutcome::ReadyForRecursiveContinuation)
		{
			if (ContinuePreparedSolveContextAfterDeterministicSingleCandidateStagesImpl(
					Context,
					SolveDepth + 1))
			{
				return EPreparedSearchBranchOutcome::Solved;
			}
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
			{
				UE_LOG(LogTemp, Display,
					TEXT("[CandidateContinuationFailure] region=%s cell=%s intent=%s attempts=%d limit=%d reason=%s forward=%s"),
					*Context.RegionDebugPath,
					*WorkItem.Cell.ToString(),
					ToDebugString(Context.PlannedCellIntents.FindRef(WorkItem.Cell)),
					Context.CandidateAttemptCount,
					Context.MaxCandidateAttempts,
					*Context.Result.FailureReason,
					*ApplyStage.ForwardCheckFailure);
			}
		}

		if (!ApplyStage.ForwardCheckFailure.IsEmpty()
			&& !FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty()
			&& Context.PlannedCellIntents.FindRef(ApplyStage.WorkItem.Cell)
				== ELayoutCellIntent::VerticalAccess)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[VerticalAccessCandidatePruned] region=%s cell=%s candidate=%s reason=%s"),
				*Context.RegionDebugPath,
				*ApplyStage.WorkItem.Cell.ToString(),
				*BuildCandidateDebugName(Context, ApplyStage.WorkItem.Candidate),
				*ApplyStage.ForwardCheckFailure);
		}
		if (!ApplyStage.ForwardCheckFailure.IsEmpty()
			&& InOutCandidateFailureReasons.Num() < Context.MaxFailureDetails)
		{
			if (ApplyStage.bOccupiedCandidate)
			{
				InOutCandidateFailureReasons.Add(FString::Printf(
					TEXT("%s: %s"),
					*BuildCandidateDebugName(Context, ApplyStage.WorkItem.Candidate),
					*ApplyStage.ForwardCheckFailure));
				AddTraceEvent(Context, FString::Printf(
					TEXT("pruned-candidate cell=%s candidate=%s reason=%s"),
					*ApplyStage.WorkItem.Cell.ToString(),
					*BuildCandidateDebugName(
						Context,
						ApplyStage.WorkItem.Candidate),
					*ApplyStage.ForwardCheckFailure));
			}
			else
			{
				InOutCandidateFailureReasons.Add(ApplyStage.ForwardCheckFailure);
				AddTraceEvent(Context, FString::Printf(
					TEXT("pruned-empty cell=%s reason=%s"),
					*ApplyStage.WorkItem.Cell.ToString(),
					*ApplyStage.ForwardCheckFailure));
			}
		}
		else if (!Context.Result.FailureReason.IsEmpty())
		{
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty()
				&& Context.PlannedCellIntents.FindRef(ApplyStage.WorkItem.Cell)
					== ELayoutCellIntent::VerticalAccess)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[VerticalAccessCandidateBacktrack] region=%s cell=%s candidate=%s reason=%s"),
					*Context.RegionDebugPath,
					*ApplyStage.WorkItem.Cell.ToString(),
					*BuildCandidateDebugName(Context, ApplyStage.WorkItem.Candidate),
					*Context.Result.FailureReason);
			}
			if (ApplyStage.bOccupiedCandidate)
			{
				AddTraceEvent(Context, FString::Printf(
					TEXT("backtrack-candidate cell=%s candidate=%s reason=%s"),
					*ApplyStage.WorkItem.Cell.ToString(),
					*BuildCandidateDebugName(
						Context,
						ApplyStage.WorkItem.Candidate),
					*Context.Result.FailureReason));
			}
			else
			{
				AddTraceEvent(Context, FString::Printf(
					TEXT("backtrack-empty cell=%s reason=%s"),
					*ApplyStage.WorkItem.Cell.ToString(),
					*Context.Result.FailureReason));
			}
			AddCandidateFailureDetail(
				Context,
				InOutCandidateFailureReasons,
				ApplyStage.WorkItem.Candidate,
				Context.Result.FailureReason);
			ClearBacktrackableFailure(Context);
		}

		++Context.Result.PropagationStats.BacktrackCount;
		++Context.Result.PropagationStats.TerrainStageBacktrackCount;
		RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
		return EPreparedSearchBranchOutcome::Rejected;
	}

	/**
	 * Finalizes the selected post-prefix branch frontier after every viable
	 * candidate has been exhausted.
	 *
	 * This keeps the exact failure surface shared between the normal recursive
	 * branch loop and the exact unique-branch precheck.
	 */
	static bool FinalizePreparedSearchBranchStageFailure(
		FSolveContext& Context,
		const int32 SolveDepth,
		const FPreparedSearchBranchStageCarrier& SearchBranchStage,
		const TArray<FString>& CandidateFailureReasons)
	{
		const LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier&
			SearchPrefixStage = SearchBranchStage.SearchPrefixStage;
		const FIntVector& Cell = SearchPrefixStage.SelectedCell;
		const ELayoutCellIntent Intent = SearchPrefixStage.Summary.SelectedIntent;
		if (Context.Result.FailureReason.IsEmpty())
		{
			TArray<FString> EffectiveCandidateFailureReasons = CandidateFailureReasons;
			if (EffectiveCandidateFailureReasons.IsEmpty())
			{
				GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
					Context,
					Cell,
					&EffectiveCandidateFailureReasons,
					nullptr,
					false);
			}
			const FString IntentText = FString::Printf(
				TEXT("%s(%d)"),
				ToDebugString(Intent),
				static_cast<int32>(Intent));
			if (!EffectiveCandidateFailureReasons.IsEmpty())
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("No compatible module candidate could satisfy intent %s at cell %s. Cell context: %s. Candidate failures: %s"),
					*IntentText,
					*Cell.ToString(),
					*BuildCellDebugString(Context, Cell),
					*FString::Join(EffectiveCandidateFailureReasons, TEXT(" | ")));
			}
			else
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("No compatible module candidate could satisfy intent %s at cell %s. Cell context: %s."),
					*IntentText,
					*Cell.ToString(),
					*BuildCellDebugString(Context, Cell));
			}
		}

		MemoizeFailedState(Context);
		AddTraceEvent(Context, FString::Printf(
			TEXT("memoize-failed-state depth=%d cell=%s placements=%d attempts=%d"),
			SolveDepth,
			*Cell.ToString(),
			Context.Placements.Num(),
			Context.CandidateAttemptCount));
		RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
		return false;
	}

	/**
	 * Checks whether the exact post-prefix branch frontier already collapses to
	 * zero or one viable candidate under immediate apply plus propagation.
	 *
	 * The check stops as soon as a second viable candidate appears, so it
	 * removes full branch-loop work only when the live solver state proves that
	 * the frontier is already empty or effectively deterministic.
	 */
	static EPreparedSearchBranchExactReductionOutcome
		TryReducePreparedSearchBranchStageToExactUniqueContinuation(
			FSolveContext& Context,
			const int32 SolveDepth,
			const FPreparedSearchBranchStageCarrier& SearchBranchStage,
			TArray<FString>& OutCandidateFailureReasons,
			int32& OutUniqueWorkItemIndex)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ExactBranchReduction, STAT_PorismLayout_ExactBranchReduction);
		OutUniqueWorkItemIndex = INDEX_NONE;
		if (SearchBranchStage.SearchPrefixStage.bSolvedAfterForcedPlacements
			|| SearchBranchStage.WorkItems.Num() <= 1)
		{
			return EPreparedSearchBranchExactReductionOutcome::
				ReadyForNormalBranchSolve;
		}

		TArray<FString> ReducedCandidateFailureReasons;
		int32 SurvivingCandidateCount = 0;
		int32 SurvivingWorkItemIndex = INDEX_NONE;
		for (int32 WorkItemIndex = 0;
			 WorkItemIndex < SearchBranchStage.WorkItems.Num();
			 ++WorkItemIndex)
		{
			const int32 SavedCandidateAttemptCount = Context.CandidateAttemptCount;
			FPreparedSearchBranchApplyStageResult ApplyStage;
			const EPreparedSearchBranchApplyOutcome ApplyOutcome =
				TryApplyPreparedSearchBranchWorkItemStage(
					Context,
					SolveDepth,
					SearchBranchStage.WorkItems[WorkItemIndex],
					ApplyStage,
					false);
			Context.CandidateAttemptCount = SavedCandidateAttemptCount;
			if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::AbortSolve)
			{
				return EPreparedSearchBranchExactReductionOutcome::AbortSolve;
			}

			if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::Rejected)
			{
				const FString CandidateFailureReason =
					!ApplyStage.ForwardCheckFailure.IsEmpty()
						? ApplyStage.ForwardCheckFailure
						: Context.Result.FailureReason;
				if (!CandidateFailureReason.IsEmpty())
				{
					AddCandidateFailureDetail(
						Context,
						ReducedCandidateFailureReasons,
						ApplyStage.WorkItem.Candidate,
						CandidateFailureReason);
				}
				ClearBacktrackableFailure(Context);
				RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
				continue;
			}

			RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
			++SurvivingCandidateCount;
			if (SurvivingCandidateCount > 1)
			{
				return EPreparedSearchBranchExactReductionOutcome::
					ReadyForNormalBranchSolve;
			}

			SurvivingWorkItemIndex = WorkItemIndex;
		}

		OutCandidateFailureReasons = MoveTemp(ReducedCandidateFailureReasons);
		if (SurvivingCandidateCount <= 0)
		{
			AddTraceEvent(Context, FString::Printf(
				TEXT("prepared-branch-reduced-empty depth=%d cell=%s candidates=%d"),
				SolveDepth,
				*SearchBranchStage.SearchPrefixStage.SelectedCell.ToString(),
				SearchBranchStage.WorkItems.Num()));
			return EPreparedSearchBranchExactReductionOutcome::
				RejectedAllCandidates;
		}

		OutUniqueWorkItemIndex = SurvivingWorkItemIndex;
		AddTraceEvent(Context, FString::Printf(
			TEXT("prepared-branch-reduced-unique depth=%d cell=%s candidates=%d uniqueIndex=%d"),
			SolveDepth,
			*SearchBranchStage.SearchPrefixStage.SelectedCell.ToString(),
			SearchBranchStage.WorkItems.Num(),
			SurvivingWorkItemIndex));
		return EPreparedSearchBranchExactReductionOutcome::
			ReadyForUniqueCandidateContinuation;
	}

	static bool TryPrepareSolveContextThroughSearchBranchApplyStageImpl(
		FSolveContext& Context,
		const int32 SolveDepth,
		const LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier&
			SearchBranchStage,
		const int32 WorkItemIndex,
		LayoutProfileSolverInternal::FPreparedSearchBranchApplyStageCarrier&
			OutStageCarrier)
	{
		OutStageCarrier =
			LayoutProfileSolverInternal::FPreparedSearchBranchApplyStageCarrier{};

		if (!SearchBranchStage.WorkItems.IsValidIndex(WorkItemIndex))
		{
			Context.Result.FailureReason = FString::Printf(
				TEXT("Prepared search branch apply stage found invalid work item index %d for cell %s with %d available candidates."),
				WorkItemIndex,
				*SearchBranchStage.SearchPrefixStage.SelectedCell.ToString(),
				SearchBranchStage.WorkItems.Num());
			AddTraceEvent(
				Context,
				FString::Printf(
					TEXT("prepared-branch-apply-invalid-index cell=%s index=%d candidates=%d"),
					*SearchBranchStage.SearchPrefixStage.SelectedCell.ToString(),
					WorkItemIndex,
					SearchBranchStage.WorkItems.Num()));
			return false;
		}

		FPreparedSearchBranchApplyStageResult ApplyStage;
		const EPreparedSearchBranchApplyOutcome ApplyOutcome =
			TryApplyPreparedSearchBranchWorkItemStage(
				Context,
				SolveDepth,
				SearchBranchStage.WorkItems[WorkItemIndex],
				ApplyStage);
		if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::AbortSolve)
		{
			return false;
		}

		if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::Rejected)
		{
			if (!ApplyStage.ForwardCheckFailure.IsEmpty())
			{
				Context.Result.FailureReason = ApplyStage.ForwardCheckFailure;
			}
			else if (Context.Result.FailureReason.IsEmpty())
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Prepared search branch apply stage rejected candidate at cell %s without a detailed failure reason."),
					*ApplyStage.WorkItem.Cell.ToString());
			}

			if (ApplyStage.bOccupiedCandidate)
			{
				AddTraceEvent(Context, FString::Printf(
					TEXT("prepared-branch-apply-rejected cell=%s candidate=%s reason=%s"),
					*ApplyStage.WorkItem.Cell.ToString(),
					*BuildCandidateDebugName(
						Context,
						ApplyStage.WorkItem.Candidate),
					*Context.Result.FailureReason));
			}
			else
			{
				AddTraceEvent(Context, FString::Printf(
					TEXT("prepared-branch-apply-rejected-empty cell=%s reason=%s"),
					*ApplyStage.WorkItem.Cell.ToString(),
					*Context.Result.FailureReason));
			}

			++Context.Result.PropagationStats.BacktrackCount;
			++Context.Result.PropagationStats.TerrainStageBacktrackCount;
			RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
			return false;
		}

		PopulatePreparedSearchBranchApplyStageCarrier(
			SearchBranchStage,
			ApplyStage,
			SolveDepth,
			OutStageCarrier);
		AddTraceEvent(Context, FString::Printf(
			TEXT("prepared-branch-apply-ready cell=%s candidate=%s nextDepth=%d"),
			*ApplyStage.WorkItem.Cell.ToString(),
			ApplyStage.bOccupiedCandidate
				? *BuildCandidateDebugName(
					Context,
					ApplyStage.WorkItem.Candidate)
				: TEXT("<empty>"),
			OutStageCarrier.NextSolveDepth));
		return true;
	}

	FLayoutIndexedDomainSnapshot BuildIndexedDomainSnapshotFromPreparedContext(FSolveContext& Context)
	{
		FLayoutIndexedDomainSnapshot Snapshot;
		const auto FinalizeSnapshot = [&Context, &Snapshot]()
		{
			Snapshot.PreparationFailureKind =
				Context.Result.PreparationFailureKind;
			Snapshot.CandidateAttemptCount = Context.CandidateAttemptCount;
			return Snapshot;
		};

		if (!PrepareSolveContextThroughRouteDomainStageImpl(Context))
		{
			Snapshot.Messages = Context.Result.Messages;
			Snapshot.FailureReason = Context.Result.FailureReason;
			return FinalizeSnapshot();
		}
		FinalizePreparedSolveContextForIndexedSearchImpl(Context);
		if (!Context.Result.FailureReason.IsEmpty())
		{
			Snapshot.Messages = Context.Result.Messages;
			Snapshot.FailureReason = Context.Result.FailureReason;
			return FinalizeSnapshot();
		}
		Snapshot.Candidates = Context.IndexedCandidates;
		Snapshot.CellDomains.Reserve(Context.Result.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			const TArray<FSolveCandidate>* CurrentDomain = Context.InitialDomains.Find(PlannedCell.Cell);
			if (CurrentDomain == nullptr)
			{
				Snapshot.FailureReason = FString::Printf(TEXT("Indexed domain snapshot is missing candidate domain for planned cell %s."), *PlannedCell.Cell.ToString());
				return FinalizeSnapshot();
			}

			FLayoutIndexedCellDomain& IndexedDomain = Snapshot.CellDomains.AddDefaulted_GetRef();
			IndexedDomain.Cell = PlannedCell.Cell;
			IndexedDomain.Intent = PlannedCell.Intent;
			IndexedDomain.CandidateBits.Init(0, Context.IndexedCandidateWordCount);
			IndexedDomain.OrderedCandidateIndices.Reserve(CurrentDomain->Num());

			for (const FSolveCandidate& Candidate : *CurrentDomain)
			{
				const int32 CandidateIndex = GetIndexedCandidateIndex(Context, Candidate);
				if (!Snapshot.Candidates.IsValidIndex(CandidateIndex))
				{
					Snapshot.FailureReason = FString::Printf(
						TEXT("Indexed domain snapshot found invalid candidate index %d for planned cell %s."),
						CandidateIndex,
						*PlannedCell.Cell.ToString());
					return FinalizeSnapshot();
				}

				IndexedDomain.OrderedCandidateIndices.Add(CandidateIndex);
				SetIndexedCandidateBit(IndexedDomain.CandidateBits, CandidateIndex);
			}

			if (CountIndexedCandidateBits(IndexedDomain.CandidateBits) != IndexedDomain.OrderedCandidateIndices.Num())
			{
				Snapshot.FailureReason = FString::Printf(
					TEXT("Indexed domain snapshot lost candidate parity for planned cell %s. ordered=%d bits=%d"),
					*PlannedCell.Cell.ToString(),
					IndexedDomain.OrderedCandidateIndices.Num(),
					CountIndexedCandidateBits(IndexedDomain.CandidateBits));
				return FinalizeSnapshot();
			}
		}

		Snapshot.CompatibilityRows = Context.IndexedCompatibilityRows;
		Snapshot.bSucceeded = true;
		return FinalizeSnapshot();
	}

	const TCHAR* ToDebugString(const ELayoutFaceDirection Direction)
	{
		switch (Direction)
		{
		case ELayoutFaceDirection::PosX:
			return TEXT("PosX");
		case ELayoutFaceDirection::NegX:
			return TEXT("NegX");
		case ELayoutFaceDirection::PosY:
			return TEXT("PosY");
		case ELayoutFaceDirection::NegY:
			return TEXT("NegY");
		case ELayoutFaceDirection::PosZ:
			return TEXT("PosZ");
		case ELayoutFaceDirection::NegZ:
			return TEXT("NegZ");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* ToDebugString(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Boundary:
			return TEXT("Boundary");
		case ELayoutCellIntent::Entry:
			return TEXT("Entry");
		case ELayoutCellIntent::Core:
			return TEXT("Core");
		case ELayoutCellIntent::Interior:
			return TEXT("Interior");
			case ELayoutCellIntent::Connector:
				return TEXT("Connector");
			case ELayoutCellIntent::VerticalAccess:
				return TEXT("VerticalAccess");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* ToDebugString(const ELayoutFaceOccupancyPolicy OccupancyPolicy)
	{
		switch (OccupancyPolicy)
		{
		case ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor:
			return TEXT("RequiresFilledNeighbor");
		case ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor:
			return TEXT("RequiresWalkableFilledNeighbor");
		case ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor:
			return TEXT("AllowsEmptyOrFilledNeighbor");
		case ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor:
			return TEXT("AllowsEmptyOrWalkableFilledNeighbor");
		case ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor:
			return TEXT("RequiresEmptyNeighbor");
		case ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor:
			return TEXT("AllowsAnyNeighbor");
		default:
			return TEXT("Unknown");
		}
	}

	FString TagsToDebugString(const FGameplayTagContainer& Tags)
	{
		TArray<FString> TagStrings;
		for (const FGameplayTag& Tag : Tags)
		{
			TagStrings.Add(Tag.ToString());
		}
		TagStrings.Sort();
		return TagStrings.IsEmpty() ? TEXT("<none>") : FString::Join(TagStrings, TEXT(","));
	}

	FGameplayTag ChooseBoundaryConnectionTag(const FLayoutFaceRule& FaceRule)
	{
		const FGameplayTagContainer EffectiveConnectionTags = FaceRule.GetEffectiveConnectionTags();
		const FGameplayTag EntryTags[] = {
			LayoutGameplayTags::FaceEntry,
		};
		for (const FGameplayTag& EntryTag : EntryTags)
		{
			if (EffectiveConnectionTags.HasTagExact(EntryTag))
			{
				return EntryTag;
			}
		}

		return FaceRule.GetEffectiveConnectionTag();
	}

	FString BuildFaceRuleSignature(const FLayoutFaceRule& Rule)
	{
		return FString::Printf(
			TEXT("%d|%s|%s|%s|%s"),
			static_cast<int32>(Rule.Direction),
			*TagsToDebugString(Rule.GetEffectiveConnectionTags()),
			*TagsToDebugString(Rule.GetEffectiveAllowedConnectionTags()),
			ToDebugString(Rule.OccupancyPolicy),
			*TagsToDebugString(Rule.ConnectedTraversalChannels));
	}

	FString BuildDerivedInternalTraversalLinkSignature(const FLayoutDerivedInternalTraversalLink& Link)
	{
		return FString::Printf(
			TEXT("%s|%s|%s|%s|%d"),
			*Link.FromLocalCell.ToString(),
			*Link.FromTraversalChannel.ToString(),
			*Link.ToLocalCell.ToString(),
			*Link.ToTraversalChannel.ToString(),
			Link.bBidirectional ? 1 : 0);
	}

	FString BuildVariantSignature(
		const FLayoutModuleFaceRules& WorldFaceRules,
		const TArray<FLayoutDerivedInternalTraversalLink>& DerivedInternalTraversalLinks)
	{
		TArray<FString> FaceSignatures;
		for (const FLayoutFaceRule& Rule : WorldFaceRules.ToArray())
		{
			FaceSignatures.Add(BuildFaceRuleSignature(Rule));
		}

		TArray<FString> DerivedTraversalSignatures;
		DerivedTraversalSignatures.Reserve(DerivedInternalTraversalLinks.Num());
		for (const FLayoutDerivedInternalTraversalLink& Link : DerivedInternalTraversalLinks)
		{
			DerivedTraversalSignatures.Add(BuildDerivedInternalTraversalLinkSignature(Link));
		}

		return FString::Printf(
			TEXT("%s##%s"),
			*FString::Join(FaceSignatures, TEXT("||")),
			*FString::Join(DerivedTraversalSignatures, TEXT("||")));
	}

	FString BuildCandidateDebugName(const ULayoutModuleAsset* Module, const int32 YawRotationSteps)
	{
		return FString::Printf(
			TEXT("%s[yawSteps=%d yawDegrees=%d]"),
			*GetNameSafe(Module),
			YawRotationSteps,
			YawRotationSteps * 90);
	}

	FString BuildFaceDebugString(const ELayoutFaceDirection WorldDirection, const int32 YawRotationSteps)
	{
		const ELayoutFaceDirection AuthoredDirection = GetAuthoredDirectionForWorldDirection(WorldDirection, YawRotationSteps);
		if (AuthoredDirection == WorldDirection)
		{
			return FString::Printf(TEXT("world %s / authored %s"), ToDebugString(WorldDirection), ToDebugString(AuthoredDirection));
		}

		return FString::Printf(
			TEXT("world %s / authored %s after yawSteps=%d yawDegrees=%d"),
			ToDebugString(WorldDirection),
			ToDebugString(AuthoredDirection),
			YawRotationSteps,
			YawRotationSteps * 90);
	}

	FString BuildCellDebugString(const FSolveContext& Context, const FIntVector& Cell)
	{
		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		TArray<FString> OutsideFaces;
		if (Cell.X == 0)
		{
			OutsideFaces.Add(ToDebugString(ELayoutFaceDirection::NegX));
		}
		if (Cell.X == Context.FootprintSize.X - 1)
		{
			OutsideFaces.Add(ToDebugString(ELayoutFaceDirection::PosX));
		}
		if (Cell.Y == 0)
		{
			OutsideFaces.Add(ToDebugString(ELayoutFaceDirection::NegY));
		}
		if (Cell.Y == Context.FootprintSize.Y - 1)
		{
			OutsideFaces.Add(ToDebugString(ELayoutFaceDirection::PosY));
		}

		TArray<FString> NeighborContexts;
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction =
				static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FIntVector NeighborCell =
				Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const ELayoutCellIntent* NeighborIntent =
				Context.PlannedCellIntents.Find(NeighborCell);
			const FSolveContext::FSolvePlacement* NeighborPlacement =
				FindPlacementOrFixedNeighbor(Context, NeighborCell);
			NeighborContexts.Add(FString::Printf(
				TEXT("%s:%s:%s"),
				ToDebugString(Direction),
				NeighborIntent != nullptr ? ToDebugString(*NeighborIntent) : TEXT("<none>"),
				NeighborPlacement == nullptr
					? (Context.ChildReservationCells.Contains(NeighborCell)
						? TEXT("child")
						: TEXT("open"))
					: (FSolveContext::IsOccupiedPlacement(*NeighborPlacement)
						? TEXT("filled")
						: TEXT("empty"))));
		}

		return FString::Printf(
			TEXT("%s intent=%s outsideFaces=[%s] neighbors=[%s]"),
			*Cell.ToString(),
			Intent != nullptr ? ToDebugString(*Intent) : TEXT("<none>"),
			OutsideFaces.IsEmpty() ? TEXT("<none>") : *FString::Join(OutsideFaces, TEXT(",")),
			*FString::Join(NeighborContexts, TEXT(",")));
	}

	FString BuildDomainDebugString(const FSolveContext& Context, const FIntVector& Cell)
	{
		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		if (Intent == nullptr)
		{
			return TEXT("domainDebug=intent=<none>");
		}

		const TArray<FSolveCandidate>* InitialDomain = Context.InitialDomains.Find(Cell);
		const TArray<int32>* VariantIndices = Context.VariantIndicesByIntent.Find(*Intent);
		TArray<FString> SupportingModules;
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : Context.ModuleSnapshots)
		{
			if (!ModuleSnapshot.SupportsRootIntent(*Intent))
			{
				continue;
			}

			TArray<FString> YawStepStrings;
			for (const int32 YawStep : ModuleSnapshot.AllowedYawRotationSteps)
			{
				YawStepStrings.Add(FString::FromInt(YawStep));
			}

			SupportingModules.Add(FString::Printf(
				TEXT("%s yawSteps=[%s]"),
				*ModuleSnapshot.DebugName.ToString(),
				YawStepStrings.IsEmpty() ? TEXT("<none>") : *FString::Join(YawStepStrings, TEXT(","))));
			if (SupportingModules.Num() >= 8)
			{
				break;
			}
		}

		TArray<FString> NeighborSummaries;
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const ELayoutCellIntent* NeighborIntent = Context.PlannedCellIntents.Find(NeighborCell);
			const FSolveContext::FSolvePlacement* NeighborPlacement = FindPlacementOrFixedNeighbor(Context, NeighborCell);
			NeighborSummaries.Add(FString::Printf(
				TEXT("%s:%s/%s"),
				ToDebugString(Direction),
				NeighborIntent != nullptr
					? ToDebugString(*NeighborIntent)
					: (CellFaceHasAnyPlannedNeighborCarrier(
							Context,
							Cell,
							Direction)
						? TEXT("PlannedNeighbor")
						: TEXT("Unplanned")),
				NeighborPlacement != nullptr
					? (FSolveContext::IsOccupiedPlacement(*NeighborPlacement) ? *BuildPlacementDebugName(Context, *NeighborPlacement) : TEXT("<empty>"))
					: TEXT("<unplaced>")));
		}

		return FString::Printf(
			TEXT("domainDebug=intent=%s initialDomain=%s variantIndices=%s supportingModules=[%s] neighbors=[%s]"),
			ToDebugString(*Intent),
			InitialDomain != nullptr ? *FString::FromInt(InitialDomain->Num()) : TEXT("<missing>"),
			VariantIndices != nullptr ? *FString::FromInt(VariantIndices->Num()) : TEXT("<missing>"),
			SupportingModules.IsEmpty() ? TEXT("<none>") : *FString::Join(SupportingModules, TEXT(" | ")),
			*FString::Join(NeighborSummaries, TEXT(" | ")));
	}

	FLayoutModuleFaceRules BuildWorldFaceRulesForYaw(const FLayoutModuleFaceRules& EffectiveRules, const int32 YawRotationSteps)
	{
		FLayoutModuleFaceRules WorldFaceRules;
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection WorldDirection = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const ELayoutFaceDirection AuthoredDirection = FLayoutDirectionUtils::RotateYaw(WorldDirection, -YawRotationSteps);
			if (const FLayoutFaceRule* Rule = EffectiveRules.FindRule(AuthoredDirection))
			{
				FLayoutFaceRule WorldRule = *Rule;
				WorldRule.Direction = WorldDirection;
				WorldFaceRules.SetRule(WorldRule);
			}
		}

		WorldFaceRules.NormalizeDirections();
		return WorldFaceRules;
	}

	TArray<FLayoutDerivedSpanOffer> BuildWorldSpanOffersForYaw(const TArray<FLayoutDerivedSpanOffer>& DerivedSpanOffers, const int32 YawRotationSteps)
	{
		TArray<FLayoutDerivedSpanOffer> WorldSpanOffers;
		WorldSpanOffers.Reserve(DerivedSpanOffers.Num());
		for (const FLayoutDerivedSpanOffer& DerivedSpanOffer : DerivedSpanOffers)
		{
			FLayoutDerivedSpanOffer& WorldSpanOffer = WorldSpanOffers.AddDefaulted_GetRef();
			WorldSpanOffer = DerivedSpanOffer;
			WorldSpanOffer.FaceDirection = FLayoutDirectionUtils::RotateYaw(DerivedSpanOffer.FaceDirection, YawRotationSteps);
		}

		return WorldSpanOffers;
	}

	bool TryGetWorldFaceRule(
		const ULayoutModuleAsset* Module,
		const ELayoutFaceDirection WorldDirection,
		const int32 YawRotationSteps,
		FLayoutFaceRule& OutFaceRule)
	{
		if (Module == nullptr)
		{
			return false;
		}

		const ELayoutFaceDirection AuthoredDirection = FLayoutDirectionUtils::RotateYaw(WorldDirection, -YawRotationSteps);
		FLayoutFaceRule EffectiveRule;
		if (!Module->GetEffectiveFaceRule(AuthoredDirection, EffectiveRule))
		{
			return false;
		}

		OutFaceRule = EffectiveRule;
		OutFaceRule.Direction = WorldDirection;
		return true;
	}

	bool AreConnectionTagsCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		const FLayoutFaceRule& TargetFaceRule)
	{
		return SourceFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| SourceFaceRule.GetEffectiveAllowedConnectionTags().HasAnyExact(TargetFaceRule.GetEffectiveConnectionTags());
	}

	FLayoutFaceRule BuildIncomingBoundaryFaceRule(const FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		FLayoutFaceRule BoundaryFaceRule;
		BoundaryFaceRule.Direction = BoundaryPoint.FaceDirection;
		BoundaryFaceRule.ConnectionTag = BoundaryPoint.ConnectionTag;
		BoundaryFaceRule.AllowedConnectionTags = BoundaryPoint.AllowedConnectionTags;
		BoundaryFaceRule.OccupancyPolicy = BoundaryPoint.bRepresentsFilledNeighbor
			? ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
			: ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
		BoundaryFaceRule.ConnectedTraversalChannels = BoundaryPoint.ConnectedTraversalChannels;
		BoundaryFaceRule.bRequireMatchingYawWithFilledNeighbor =
			(BoundaryPoint.FaceDirection == ELayoutFaceDirection::PosZ || BoundaryPoint.FaceDirection == ELayoutFaceDirection::NegZ)
			&& BoundaryPoint.bRequireMatchingYawWithFilledNeighbor;
		return BoundaryFaceRule;
	}

	int32 NormalizeSolverYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	bool DoSolverFaceRulesRequireMatchingYaw(const FLayoutFaceRule& SourceFaceRule, const FLayoutFaceRule& TargetFaceRule)
	{
		const bool bVerticalPair =
			(SourceFaceRule.Direction == ELayoutFaceDirection::PosZ || SourceFaceRule.Direction == ELayoutFaceDirection::NegZ)
			&& (TargetFaceRule.Direction == ELayoutFaceDirection::PosZ || TargetFaceRule.Direction == ELayoutFaceDirection::NegZ);
		return bVerticalPair
			&& (SourceFaceRule.bRequireMatchingYawWithFilledNeighbor
				|| TargetFaceRule.bRequireMatchingYawWithFilledNeighbor);
	}

	bool AreSolverYawRotationsCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		const int32 SourceYawRotationSteps,
		const FLayoutFaceRule& TargetFaceRule,
		const int32 TargetYawRotationSteps)
	{
		return !DoSolverFaceRulesRequireMatchingYaw(SourceFaceRule, TargetFaceRule)
			|| NormalizeSolverYawRotationSteps(SourceYawRotationSteps) == NormalizeSolverYawRotationSteps(TargetYawRotationSteps);
	}

	bool CanFaceContributeWalkableNeighbor(const FLayoutFaceRule& FaceRule)
	{
		return !FaceRule.ConnectedTraversalChannels.IsEmpty()
			&& IsFaceCompatibleWithOccupancy(FaceRule, true, true);
	}

	int32 GetPlacementMinTraversableNeighborFaces(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindPlacementVariant(Context, Placement))
		{
			return Variant->MinTraversableNeighborFaces;
		}

		return 0;
	}
	bool IsFilledWalkableNeighborConnectionSatisfied(
		const FLayoutFaceRule& SourceFaceRule,
		const int32 SourceYawRotationSteps,
		const FLayoutFaceRule& TargetFaceRule,
		const int32 TargetYawRotationSteps)
	{
		return CanFaceContributeWalkableNeighbor(SourceFaceRule)
			&& IsFaceCompatibleWithOccupancy(TargetFaceRule, true, true)
			&& AreConnectionTagsCompatible(SourceFaceRule, TargetFaceRule)
			&& AreConnectionTagsCompatible(TargetFaceRule, SourceFaceRule)
			&& HasSharedConnectedWalkableArea(SourceFaceRule, TargetFaceRule)
			&& AreSolverYawRotationsCompatible(SourceFaceRule, SourceYawRotationSteps, TargetFaceRule, TargetYawRotationSteps);
	}

	template<typename TryGetSubjectFaceRuleFn, typename ShouldIgnoreNeighborPlacementFn>
	bool DoesOccupiedSubjectSatisfyMinimumWalkableFaces(
		FSolveContext& Context,
		const FIntVector& Cell,
		const int32 SubjectYawRotationSteps,
		const int32 ConfiguredMinTraversableNeighborFaces,
		TryGetSubjectFaceRuleFn&& TryGetSubjectFaceRule,
		ShouldIgnoreNeighborPlacementFn&& ShouldIgnoreNeighborPlacement,
		FString* OutFailureReason)
	{
		if (ConfiguredMinTraversableNeighborFaces <= 0)
		{
			return true;
		}

		int32 EligibleTraversableNeighborFaceCount = 0;
		int32 SatisfiedWalkableFaceCount = 0;
		int32 PossibleFutureWalkableFaceCount = 0;
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction =
				static_cast<ELayoutFaceDirection>(DirectionIndex);
			FLayoutFaceRule SubjectFaceRule;
			if (!TryGetSubjectFaceRule(Direction, SubjectFaceRule)
				|| !CanFaceContributeWalkableNeighbor(SubjectFaceRule))
			{
				continue;
			}

			const FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface == nullptr)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("missing compiled face-interface row for late walkable-face audit on occupied cell %s %s."),
						*Cell.ToString(),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps));
				}
				return false;
			}

			const FIntVector NeighborCell =
				Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			switch (FaceInterface->NeighborKind)
			{
			case ESolveCellFaceNeighborKind::FixedFilledNeighbor:
			case ESolveCellFaceNeighborKind::FixedEmptyNeighbor:
			{
				const FSolveContext::FSolvePlacement* ExistingNeighborPlacement =
					FindPlacementOrFixedNeighbor(Context, NeighborCell);
				if (ExistingNeighborPlacement == nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("compiled fixed-neighbor interface for late walkable-face audit on occupied cell %s %s did not resolve a live fixed placement."),
							*Cell.ToString(),
							*BuildFaceDebugString(Direction, SubjectYawRotationSteps));
					}
					return false;
				}

				if (ShouldIgnoreNeighborPlacement(*ExistingNeighborPlacement))
				{
					continue;
				}

				if (!FSolveContext::IsOccupiedPlacement(*ExistingNeighborPlacement))
				{
					continue;
				}

				++EligibleTraversableNeighborFaceCount;
				FLayoutFaceRule NeighborFaceRule;
				if (TryGetPlacementFaceRule(
						Context,
						*ExistingNeighborPlacement,
						FLayoutDirectionUtils::GetOpposite(Direction),
						NeighborFaceRule)
					&& IsFilledWalkableNeighborConnectionSatisfied(
						SubjectFaceRule,
						SubjectYawRotationSteps,
						NeighborFaceRule,
						ExistingNeighborPlacement->YawRotationSteps))
				{
					++SatisfiedWalkableFaceCount;
				}

				continue;
			}
			case ESolveCellFaceNeighborKind::IncomingBoundary:
			case ESolveCellFaceNeighborKind::SupportBoundary:
			{
				const FLayoutSolveBoundaryPoint* BoundaryPoint =
					ResolveCompiledIncomingBoundaryPoint(
						Context,
						*FaceInterface);
				if (BoundaryPoint == nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("compiled incoming-boundary interface for late walkable-face audit on occupied cell %s %s lost its source boundary point."),
							*Cell.ToString(),
							*BuildFaceDebugString(Direction, SubjectYawRotationSteps));
					}
					return false;
				}

				if (!BoundaryPoint->bRepresentsFilledNeighbor)
				{
					continue;
				}

				++EligibleTraversableNeighborFaceCount;
				if (BoundaryPoint->bUsesCertifiedReciprocalDomain)
				{
					const FLayoutCellCandidateDomainRestriction* Restriction =
						Context.CandidateDomainRestrictionsByCell.Find(Cell);
					if (Restriction == nullptr
						|| Restriction->RestrictionId != BoundaryPoint->CertifiedDomainRestrictionId)
					{
						if (OutFailureReason != nullptr)
						{
							*OutFailureReason = FString::Printf(
								TEXT("late walkable-face audit lost certified reciprocal domain '%s' from certificate '%s' at %s."),
								*BoundaryPoint->CertifiedDomainRestrictionId.ToString(),
								*BoundaryPoint->CertifiedDomainCertificateId.ToString(),
								*Cell.ToString());
						}
						return false;
					}
					// Candidate-domain certification already proved every retained parent
					// variant reciprocally supports this fixed child face.
					++SatisfiedWalkableFaceCount;
					continue;
				}

				const FLayoutFaceRule BoundaryFaceRule =
					BuildIncomingBoundaryFaceRule(*BoundaryPoint);
				if (IsFilledWalkableNeighborConnectionSatisfied(
						SubjectFaceRule,
						SubjectYawRotationSteps,
						BoundaryFaceRule,
						BoundaryPoint->SourceYawRotationSteps))
				{
					++SatisfiedWalkableFaceCount;
				}

				continue;
			}
			case ESolveCellFaceNeighborKind::InternalPlannedNeighbor:
			case ESolveCellFaceNeighborKind::ExternalPlannedNeighbor:
			case ESolveCellFaceNeighborKind::DeferredExternalContract:
			case ESolveCellFaceNeighborKind::ChildRegionContact:
				// Settled external contracts are validated by their dedicated face audit;
				// until then they remain possible neighbors for minimum-walkable admission.
				++EligibleTraversableNeighborFaceCount;
				++PossibleFutureWalkableFaceCount;
				continue;
			case ESolveCellFaceNeighborKind::StructuralVerticalOverlap:
			case ESolveCellFaceNeighborKind::TerrainBackedFilledNeighbor:
			case ESolveCellFaceNeighborKind::TerrainResidualNeighbor:
			case ESolveCellFaceNeighborKind::OuterBoundary:
			case ESolveCellFaceNeighborKind::ReservedExternalCell:
				continue;
			default:
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("unknown compiled face-interface kind for late walkable-face audit on occupied cell %s %s."),
						*Cell.ToString(),
						*BuildFaceDebugString(Direction, SubjectYawRotationSteps));
				}
				return false;
			}
		}

		const int32 EffectiveMinTraversableNeighborFaces = FMath::Min(
			ConfiguredMinTraversableNeighborFaces,
			EligibleTraversableNeighborFaceCount);
		if (EffectiveMinTraversableNeighborFaces <= 0
			|| SatisfiedWalkableFaceCount + PossibleFutureWalkableFaceCount >=
				EffectiveMinTraversableNeighborFaces)
		{
			return true;
		}

		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = FString::Printf(
				TEXT("module requires at least %d traversable face connection(s), capped from configured minimum %d by %d eligible traversable neighbor face(s), but only %d are satisfied and %d more are still possible from unsolved neighbors."),
				EffectiveMinTraversableNeighborFaces,
				ConfiguredMinTraversableNeighborFaces,
				EligibleTraversableNeighborFaceCount,
				SatisfiedWalkableFaceCount,
				PossibleFutureWalkableFaceCount);
		}

		return false;
	}

	bool DoesCandidateSatisfyMinimumWalkableFaces(
		FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate,
		FString* OutFailureReason)
	{
		// This stays late because the effective minimum depends on the live mix
		// of already-satisfied versus still-possible neighbor faces.
		const int32 ConfiguredMinTraversableNeighborFaces = GetCandidateMinTraversableNeighborFaces(Context, Candidate);
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || ConfiguredMinTraversableNeighborFaces <= 0)
		{
			return true;
		}

		const FSolveContext::FSolvePlacement CandidatePlacement = MakeRootSolvePlacement(
			Context,
			Cell,
			Candidate.YawRotationSteps,
			Candidate.VariantIndex);
		return DoesOccupiedSubjectSatisfyMinimumWalkableFaces(
			Context,
			Cell,
			Candidate.YawRotationSteps,
			ConfiguredMinTraversableNeighborFaces,
			[&Context, &Candidate](
				const ELayoutFaceDirection Direction,
				FLayoutFaceRule& OutFaceRule)
			{
				return TryGetCandidateFaceRule(
					Context,
					Candidate,
					Direction,
					OutFaceRule);
			},
			[&CandidatePlacement](const FSolveContext::FSolvePlacement& NeighborPlacement)
			{
				return AreSameOccupiedBundlePlacement(
					CandidatePlacement,
					NeighborPlacement);
			},
			OutFailureReason);
	}

	bool DoesPlacementSatisfyMinimumWalkableFaces(
		FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveContext::FSolvePlacement& Placement,
		FString* OutFailureReason)
	{
		// Final solved-placement audit uses the same late walkable-face rule as
		// live candidate legality, but with committed neighbors instead of
		// merely possible future neighbors.
		const int32 ConfiguredMinTraversableNeighborFaces = GetPlacementMinTraversableNeighborFaces(Context, Placement);
		if (ConfiguredMinTraversableNeighborFaces <= 0)
		{
			return true;
		}

		return DoesOccupiedSubjectSatisfyMinimumWalkableFaces(
			Context,
			Cell,
			Placement.YawRotationSteps,
			ConfiguredMinTraversableNeighborFaces,
			[&Context, &Placement](
				const ELayoutFaceDirection Direction,
				FLayoutFaceRule& OutFaceRule)
			{
				return TryGetPlacementFaceRule(
					Context,
					Placement,
					Direction,
					OutFaceRule);
			},
			[&Placement](const FSolveContext::FSolvePlacement& NeighborPlacement)
			{
				return AreSameOccupiedBundlePlacement(
					Placement,
					NeighborPlacement);
			},
			OutFailureReason);
	}

	bool IsCandidateCompatible(
		FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate,
		FString* OutFailureReason = nullptr)
	{
		// Structural/interface legality is shared through the compiled face
		// carrier above; only the branch-dependent walkable-neighbor minimum
		// remains in this late candidate pass.
		check(LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate));
		if (!DoesCandidateSatisfyCarrierBackedAdmission(
			Context,
			Cell,
			Candidate,
			OutFailureReason))
		{
			return false;
		}

		return DoesCandidateSatisfyMinimumWalkableFaces(Context, Cell, Candidate, OutFailureReason);
	}

	bool IsPlacementCompatibleWithCompletedNeighborMap(
		FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveContext::FSolvePlacement& Placement,
		FString* OutFailureReason)
	{
		check(FSolveContext::IsOccupiedPlacement(Placement));

		FLayoutModuleFaceRules PlacementWorldFaceRules;
		const bool bHasPlacementWorldFaceRules = TryBuildPlacementWorldFaceRules(Context, Placement, PlacementWorldFaceRules);
		if (!DoesOccupiedSubjectSatisfyCompiledFaceInterfaces(
			Context,
			Cell,
			Placement.YawRotationSteps,
			bHasPlacementWorldFaceRules ? &PlacementWorldFaceRules : nullptr,
			BuildPlacementDebugName(Context, Placement),
			[&Context, &Placement](
				const ELayoutFaceDirection Direction,
				FLayoutFaceRule& OutFaceRule)
			{
				return TryGetPlacementFaceRule(
					Context,
					Placement,
					Direction,
					OutFaceRule);
			},
			[&Placement](const FSolveContext::FSolvePlacement& NeighborPlacement)
			{
				return AreSameOccupiedBundlePlacement(
					Placement,
					NeighborPlacement);
			},
			OutFailureReason))
		{
			return false;
		}

		return DoesPlacementSatisfyMinimumWalkableFaces(Context, Cell, Placement, OutFailureReason);
	}

	bool MustGrowTraversalChannelFromEntryRoot(const FSolveContext& Context, const FIntVector& Cell, const FGameplayTag& TraversalChannel)
	{
		const TSet<FGameplayTag>* ActiveTraversalChannels = Context.ActiveTraversalChannelsByLevel.Find(Cell.Z);
		if (ActiveTraversalChannels != nullptr && !ActiveTraversalChannels->IsEmpty())
		{
			return TraversalChannel.IsValid()
				&& ActiveTraversalChannels->Contains(TraversalChannel);
		}

		return TraversalChannel.IsValid()
			&& GetEffectiveProfileSnapshot(Context).bRequireAllTraversalChannelsReachable;
	}

	bool HasSharedConnectedWalkableArea(const FLayoutFaceRule& LeftFaceRule, const FLayoutFaceRule& RightFaceRule)
	{
		for (const FGameplayTag& TraversalChannel : LeftFaceRule.ConnectedTraversalChannels)
		{
			if (TraversalChannel.IsValid() && RightFaceRule.ConnectedTraversalChannels.HasTagExact(TraversalChannel))
			{
				return true;
			}
		}

		return false;
	}

	bool AreAdjacentCandidatesCompatible(
		FSolveContext& Context,
		const FSolveCandidate& Candidate,
		const FSolveCandidate& NeighborCandidate,
		const ELayoutFaceDirection Direction)
	{
		// This pairwise row builder is intentionally narrower than live
		// candidate legality: it only models local candidate-vs-candidate face
		// coexistence and must stay independent of per-cell compiled carrier
		// context such as incoming boundaries, fixed neighbors, or reserved
		// external faces.
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) && !LayoutProfileSolverInternal::IsOccupiedCandidate(NeighborCandidate))
		{
			return true;
		}

		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			FLayoutFaceRule NeighborFaceRule;
			return TryGetCandidateFaceRule(Context, NeighborCandidate, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule)
				&& IsFaceCompatibleWithOccupancy(NeighborFaceRule, false, false);
		}

		FLayoutFaceRule CandidateFaceRule;
		if (!TryGetCandidateFaceRule(Context, Candidate, Direction, CandidateFaceRule))
		{
			return false;
		}

		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(NeighborCandidate))
		{
			return IsFaceCompatibleWithOccupancy(CandidateFaceRule, false, false);
		}

		FLayoutFaceRule NeighborFaceRule;
		if (!TryGetCandidateFaceRule(Context, NeighborCandidate, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule))
		{
			return false;
		}

		return DoesFilledNeighborFacePairSatisfyInterfaceContract(
			CandidateFaceRule,
			Candidate.YawRotationSteps,
			NeighborFaceRule,
			NeighborCandidate.YawRotationSteps);
	}

	bool HasAnyLegalCandidateForCell(FSolveContext& Context, const FIntVector& Cell, TArray<FString>* OutFailureReasons)
	{
		return GetLegalCandidatesForCellIndexed(Context, Cell, OutFailureReasons).Num() > 0;
	}

	bool IsPlannedCellDischargedByForeignBundleCandidate(
		FSolveContext& Context,
		const FIntVector& Cell,
		const TSet<FWalkableNodeKey>* ReachableNodesOverride)
	{
		for (const FIntVector& CandidateRootCell : Context.SolveOrder)
		{
			if (CandidateRootCell == Cell || Context.Placements.Contains(CandidateRootCell))
			{
				continue;
			}

			const TArray<FSolveCandidate> CoveringCandidates = GetLegalCandidatesForCellIndexed(
				Context,
				CandidateRootCell,
				nullptr,
				ReachableNodesOverride,
				false);
			for (const FSolveCandidate& Candidate : CoveringCandidates)
			{
				if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
				{
					continue;
				}

				const TArray<FIntVector> OccupiedWorldCells = BuildCandidateWorldOccupiedCells(Context, CandidateRootCell, Candidate);
				if (OccupiedWorldCells.Contains(Cell))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool ShouldRunExpensiveForwardChecks(FSolveContext& Context)
	{
		return Context.bForceExpensiveForwardChecks
			|| Context.Placements.Num() <= 1
			|| (Context.Placements.Num() % ExpensiveForwardCheckInterval) == 0;
	}

	/**
	 * Upper-level boundary intent cells can appear "discharged" when a still
	 * unresolved foreign root candidate might cover them, but that broad skip is
	 * only safe if at least one covering root candidate also survives immediate
	 * apply plus forward checking.
	 *
	 * This keeps the post-prefix feasibility reduction exact: the later parent
	 * solve only continues when there is still one live covering occupied
	 * candidate, not merely because one exists in principle on a foreign root.
	 */
	static bool TryValidateDischargedUpperBoundaryIntentCellFeasibility(
		FSolveContext& Context,
		const int32 SolveDepth,
		const FIntVector& Cell,
		const TSet<FWalkableNodeKey>* ReachableNodesOverride)
	{
		const ELayoutCellIntent Intent =
			Context.PlannedCellIntents.FindRef(Cell);
		bool bFoundCoveringCandidate = false;
		TArray<FString> CoveringFailureReasons;

		for (const FIntVector& CandidateRootCell : Context.SolveOrder)
		{
			if (CandidateRootCell == Cell
				|| Context.Placements.Contains(CandidateRootCell))
			{
				continue;
			}

			const ELayoutCellIntent CandidateRootIntent =
				Context.PlannedCellIntents.FindRef(CandidateRootCell);
			const TArray<FSolveCandidate> LegalCandidates =
				GetLegalCandidatesForCellIndexed(
					Context,
					CandidateRootCell,
					nullptr,
					ReachableNodesOverride,
					true);
			for (const FSolveCandidate& Candidate : LegalCandidates)
			{
				if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
				{
					continue;
				}

				const TArray<FIntVector> OccupiedWorldCells =
					BuildCandidateWorldOccupiedCells(
						Context,
						CandidateRootCell,
						Candidate);
				if (!OccupiedWorldCells.Contains(Cell))
				{
					continue;
				}

				bFoundCoveringCandidate = true;

				const int32 SavedCandidateAttemptCount =
					Context.CandidateAttemptCount;
				FPreparedSearchBranchApplyStageResult ApplyStage;
				const EPreparedSearchBranchApplyOutcome ApplyOutcome =
					TryApplyPreparedSearchBranchWorkItemStage(
						Context,
						SolveDepth,
						FPreparedSearchBranchWorkItem{
							CandidateRootCell,
							CandidateRootIntent,
							Candidate},
						ApplyStage,
						false);
				Context.CandidateAttemptCount = SavedCandidateAttemptCount;

				if (ApplyOutcome
					== EPreparedSearchBranchApplyOutcome::
						ReadyForRecursiveContinuation)
				{
					RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
					return true;
				}

				if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::Rejected)
				{
					const FString FailureReason =
						!ApplyStage.ForwardCheckFailure.IsEmpty()
							? ApplyStage.ForwardCheckFailure
							: Context.Result.FailureReason;
					if (!FailureReason.IsEmpty())
					{
						AddCandidateFailureDetail(
							Context,
							CoveringFailureReasons,
							Candidate,
							FailureReason);
					}

					RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
					ClearBacktrackableFailure(Context);
					continue;
				}

				return false;
			}
		}

		Context.Result.FailureReason = FString::Printf(
			TEXT("Post-prefix constrained-cell feasibility rejected planned cell %s with intent %s. UpperBoundaryIntentConstrained=1. %s Candidate failures: %s"),
			*Cell.ToString(),
			ToDebugString(Intent),
			bFoundCoveringCandidate
				? TEXT("No unresolved occupied covering candidate survives immediate placement and forward checking before recursion.")
				: TEXT("No unresolved occupied covering candidate remains to discharge the cell before recursion."),
			CoveringFailureReasons.IsEmpty()
				? TEXT("<none>")
				: *FString::Join(CoveringFailureReasons, TEXT(" | ")));
		AddTraceEvent(Context, FString::Printf(
			TEXT("post-prefix-discharged-upper-boundary-immediate-fail depth=%d cell=%s coveringCandidates=%d"),
			SolveDepth,
			*Cell.ToString(),
			bFoundCoveringCandidate ? 1 : 0));
		return false;
	}

	TArray<FSolveCandidate> GetLegalCandidatesForCellIndexed(
		FSolveContext& Context,
		const FIntVector& Cell,
		TArray<FString>* OutFailureReasons,
		const TSet<FWalkableNodeKey>* ReachableNodesOverride,
		const bool bSortReachabilityCandidates)
	{
		// After static domain seeding and compiled-face legality, this pass only
		// spends branch-dependent checks: live domain-bit neighbor support,
		// reachability preference, and deferred forward-check context.
		TArray<FSolveCandidate> LegalCandidates;

		if (const FSolveContext::FSolvePlacement* ExistingPlacement = Context.Placements.Find(Cell))
		{
			FSolveCandidate ExistingCandidate;
			ExistingCandidate.YawRotationSteps = ExistingPlacement->YawRotationSteps;
			ExistingCandidate.VariantIndex = ExistingPlacement->VariantIndex;
			ExistingCandidate.ModuleSnapshotIndex = ExistingPlacement->ModuleSnapshotIndex;
			ExistingCandidate.ModuleSnapshotId = ExistingPlacement->ModuleSnapshotId;
			ExistingCandidate.bEmpty = !FSolveContext::IsOccupiedPlacement(*ExistingPlacement);
			LegalCandidates.Add(ExistingCandidate);
			return LegalCandidates;
		}

		const TArray<int32>* OrderedCandidateIndices = Context.IndexedOrderedDomainCandidatesByCell.Find(Cell);
		if (OrderedCandidateIndices == nullptr)
		{
			if (OutFailureReasons != nullptr)
			{
				OutFailureReasons->AddUnique(FString::Printf(TEXT("Indexed domain is missing ordered candidates for planned cell %s."), *Cell.ToString()));
			}
			return LegalCandidates;
		}

		const TArray<uint64>* DomainBits = Context.IndexedDomainBitsByCell.Find(Cell);
		if (DomainBits == nullptr)
		{
			if (OutFailureReasons != nullptr)
			{
				OutFailureReasons->AddUnique(FString::Printf(TEXT("Indexed domain is missing candidate bits for planned cell %s."), *Cell.ToString()));
			}
			return LegalCandidates;
		}

		for (const int32 CandidateIndex : *OrderedCandidateIndices)
		{
			if (!IsIndexedCandidateBitSet(*DomainBits, CandidateIndex))
			{
				continue;
			}

			const FSolveCandidate Candidate = BuildSolveCandidateFromIndex(Context, CandidateIndex);
			if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
			{
				if (DoesCellSatisfyCompiledEmptyInterfaces(Context, Cell))
				{
					LegalCandidates.Add(Candidate);
				}
				continue;
			}

			FString FailureReason;
			if (!IsCandidateCompatible(Context, Cell, Candidate, &FailureReason))
			{
				if (OutFailureReasons != nullptr && !FailureReason.IsEmpty())
				{
					AddCandidateFailureDetail(Context, *OutFailureReasons, Candidate, FailureReason);
				}
				continue;
			}

			bool bHasNeighborSupport = true;
			const TArray<FIntVector> OccupiedWorldCells = BuildCandidateWorldOccupiedCells(Context, Cell, Candidate);
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
				const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (NeighborCell != Cell && OccupiedWorldCells.Contains(NeighborCell))
				{
					continue;
				}

				if (!FaceCarriesLivePlannedNeighborSupport(
						Context,
						Cell,
						Direction)
					|| !ShouldAuditAdjacencyBetweenCells(Context, Cell, NeighborCell))
				{
					continue;
				}

				const FLayoutIndexedCandidateCompatibility* CompatibilityRow =
					FindIndexedCompatibilityRow(
						Context,
						GetIndexedCandidateIndex(Context, Candidate),
						Direction);
				bool bFoundCompatibleNeighbor = false;
				if (Context.Placements.Contains(NeighborCell))
				{
					// IsCandidateCompatible already validated this exact settled placement,
					// including composite shadow-local faces. Indexed rows only prove unsolved domains.
					bFoundCompatibleNeighbor = true;
				}
				else
				{
					const TArray<int32>* NeighborOrderedCandidateIndices =
						Context.IndexedOrderedDomainCandidatesByCell.Find(NeighborCell);
					const TArray<uint64>* NeighborDomainBits =
						Context.IndexedDomainBitsByCell.Find(NeighborCell);
					if (NeighborOrderedCandidateIndices == nullptr || NeighborDomainBits == nullptr)
					{
						bHasNeighborSupport = false;
						FailureReason = FString::Printf(
							TEXT("Indexed domain is missing neighbor support data for planned cell %s while checking candidate support from %s."),
							*NeighborCell.ToString(),
							*Cell.ToString());
						break;
					}
					bFoundCompatibleNeighbor = CompatibilityRow != nullptr
						&& HasAnySharedIndexedCandidateBit(
							CompatibilityRow->CompatibleCandidateBits,
							*NeighborDomainBits);
				}

				if (!bFoundCompatibleNeighbor)
				{
					bHasNeighborSupport = false;
					FailureReason = FString::Printf(
						TEXT("No compatible candidate remains on neighboring planned cell %s for %s."),
						*NeighborCell.ToString(),
						*BuildCandidateDebugName(Context, Candidate));
					if (OutFailureReasons != nullptr)
					{
						FLayoutFaceRule SourceFaceRule;
						const TArray<int32>* NeighborCandidateIndices =
							Context.IndexedOrderedDomainCandidatesByCell.Find(NeighborCell);
						const TArray<uint64>* NeighborBits =
							Context.IndexedDomainBitsByCell.Find(NeighborCell);
						TArray<FString> PairFailureDetails;
						if (TryGetCandidateFaceRule(Context, Candidate, Direction, SourceFaceRule)
							&& NeighborCandidateIndices != nullptr
							&& NeighborBits != nullptr)
						{
							for (const int32 NeighborCandidateIndex : *NeighborCandidateIndices)
							{
								if (PairFailureDetails.Num() >= 4
									|| !IsIndexedCandidateBitSet(*NeighborBits, NeighborCandidateIndex))
								{
									continue;
								}

								const FSolveCandidate NeighborCandidate =
									BuildSolveCandidateFromIndex(Context, NeighborCandidateIndex);
								if (!LayoutProfileSolverInternal::IsOccupiedCandidate(NeighborCandidate))
								{
									PairFailureDetails.Add(TEXT("Empty[SourceOccupancy]"));
									continue;
								}

								FLayoutFaceRule NeighborFaceRule;
								EFilledNeighborFaceCompatibilityFailure PairFailure =
									EFilledNeighborFaceCompatibilityFailure::None;
								const bool bHasNeighborFace = TryGetCandidateFaceRule(
									Context,
									NeighborCandidate,
									FLayoutDirectionUtils::GetOpposite(Direction),
									NeighborFaceRule);
								if (bHasNeighborFace)
								{
									DoesFilledNeighborFacePairSatisfyInterfaceContract(
										SourceFaceRule,
										Candidate.YawRotationSteps,
										NeighborFaceRule,
										NeighborCandidate.YawRotationSteps,
										&PairFailure);
								}
								PairFailureDetails.Add(FString::Printf(
									TEXT("%s[yaw=%d failure=%s sourceTag=%s sourceAllowed=%s neighborTag=%s neighborAllowed=%s sourceTraversal=%s neighborTraversal=%s]"),
									*BuildCandidateDebugName(Context, NeighborCandidate),
									NeighborCandidate.YawRotationSteps,
									bHasNeighborFace ? ToDebugString(PairFailure) : TEXT("MissingOppositeFace"),
									*TagsToDebugString(SourceFaceRule.GetEffectiveConnectionTags()),
									*TagsToDebugString(SourceFaceRule.GetEffectiveAllowedConnectionTags()),
									bHasNeighborFace ? *TagsToDebugString(NeighborFaceRule.GetEffectiveConnectionTags()) : TEXT("<none>"),
									bHasNeighborFace ? *TagsToDebugString(NeighborFaceRule.GetEffectiveAllowedConnectionTags()) : TEXT("<none>"),
									*TagsToDebugString(SourceFaceRule.ConnectedTraversalChannels),
									bHasNeighborFace ? *TagsToDebugString(NeighborFaceRule.ConnectedTraversalChannels) : TEXT("<none>")));
							}
						}
						FailureReason += FString::Printf(
							TEXT(" Pair failures: [%s]"),
							PairFailureDetails.IsEmpty()
								? TEXT("<no active neighbor candidates>")
								: *FString::Join(PairFailureDetails, TEXT(" | ")));
					}
					break;
				}
			}

			if (!bHasNeighborSupport)
			{
				if (OutFailureReasons != nullptr && !FailureReason.IsEmpty())
				{
					OutFailureReasons->AddUnique(TruncateSolverMessage(FailureReason, MaxSolverFailureDetailChars));
				}
				continue;
			}

			LegalCandidates.Add(Candidate);
		}

		if (LegalCandidates.IsEmpty()
			&& OutFailureReasons != nullptr
			&& OutFailureReasons->IsEmpty())
		{
			int32 ActiveCandidateCount = 0;
			for (const int32 CandidateIndex : *OrderedCandidateIndices)
			{
				ActiveCandidateCount += IsIndexedCandidateBitSet(*DomainBits, CandidateIndex) ? 1 : 0;
			}
			OutFailureReasons->Add(FString::Printf(
				TEXT("Indexed domain produced no legal candidate without a detailed rejection: ordered=%d active=%d."),
				OrderedCandidateIndices->Num(),
				ActiveCandidateCount));
		}

		const ELayoutCellIntent* CellIntent = Context.PlannedCellIntents.Find(Cell);
		const bool bNeedsLiveReachabilityOrdering =
			LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, Cell)
			|| (CellIntent != nullptr && *CellIntent == ELayoutCellIntent::Entry);
		if (bNeedsLiveReachabilityOrdering
			&& LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(Context))
		{
			TSet<FWalkableNodeKey> LocalReachableNodes;
			const TSet<FWalkableNodeKey>* ReachableNodes = ReachableNodesOverride;
			if (ReachableNodes == nullptr)
			{
				LocalReachableNodes = LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(Context);
				ReachableNodes = &LocalReachableNodes;
			}

			LayoutProfileSolverInternal::PruneDetachedReachabilityCandidatesWhenAttachable(Context, Cell, *ReachableNodes, LegalCandidates);

			if (bSortReachabilityCandidates && LegalCandidates.Num() > 1)
			{
				TMap<int32, int32> OriginalOrderByCandidateIndex;
				for (int32 OrderIndex = 0; OrderIndex < OrderedCandidateIndices->Num(); ++OrderIndex)
				{
					OriginalOrderByCandidateIndex.Add((*OrderedCandidateIndices)[OrderIndex], OrderIndex);
				}

				LegalCandidates.StableSort([&Context, &Cell, ReachableNodes, &OriginalOrderByCandidateIndex](const FSolveCandidate& Left, const FSolveCandidate& Right)
				{
					const int32 LeftPreference = LayoutProfileSolverInternal::GetCandidateReachabilityPreference(Context, Cell, Left, *ReachableNodes);
					const int32 RightPreference = LayoutProfileSolverInternal::GetCandidateReachabilityPreference(Context, Cell, Right, *ReachableNodes);
					if (LeftPreference != RightPreference)
					{
						return LeftPreference > RightPreference;
					}

					const int32 LeftOrder = OriginalOrderByCandidateIndex.FindRef(GetIndexedCandidateIndex(Context, Left));
					const int32 RightOrder = OriginalOrderByCandidateIndex.FindRef(GetIndexedCandidateIndex(Context, Right));
					return LeftOrder < RightOrder;
				});
			}
		}

		return LegalCandidates;
	}

	/**
	 * Avoids building candidate-failure strings on the hot success path.
	 *
	 * This keeps the exact legality proof unchanged, but only materializes
	 * detailed failure text after a cell has already proven empty.
	 */
	TArray<FSolveCandidate> GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
		FSolveContext& Context,
		const FIntVector& Cell,
		TArray<FString>* OutFailureReasons,
		const TSet<FWalkableNodeKey>* ReachableNodesOverride,
		const bool bSortReachabilityCandidates)
	{
		TArray<FSolveCandidate> LegalCandidates = GetLegalCandidatesForCellIndexed(
			Context,
			Cell,
			nullptr,
			ReachableNodesOverride,
			bSortReachabilityCandidates);
		if (!LegalCandidates.IsEmpty() || OutFailureReasons == nullptr)
		{
			return LegalCandidates;
		}

		OutFailureReasons->Reset();
		return GetLegalCandidatesForCellIndexed(
			Context,
			Cell,
			OutFailureReasons,
			ReachableNodesOverride,
			bSortReachabilityCandidates);
	}

	bool PropagateUnsolvedDomains(FSolveContext& Context, FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_Propagation, STAT_PorismLayout_Propagation);
		FScopedPropagationStatsTimer ScopedTimer(Context.Result.PropagationStats);
		TSet<FWalkableNodeKey> ReachableNodes;
		const TSet<FWalkableNodeKey>* ReachableNodesPtr = nullptr;
		if (LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(Context))
		{
			ReachableNodes = LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(Context);
			ReachableNodesPtr = &ReachableNodes;
		}

		++Context.Result.PropagationStats.PropagationPassCount;
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			WaitOnCancellationCheckpointGateForTesting();
			if (HasExceededSolverTimeBudget(Context, Context.Placements.Num()))
			{
				return false;
			}
			if (Context.Placements.Contains(PlannedCell.Cell))
			{
				continue;
			}

			++Context.Result.PropagationStats.ArcQueuePopCount;
			++Context.Result.PropagationStats.SupportCheckCount;
			TArray<FString> FailureReasons;
			TArray<FSolveCandidate> LegalCandidates =
				GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
					Context,
					PlannedCell.Cell,
					&FailureReasons,
					ReachableNodesPtr,
					false);
			if (!LegalCandidates.IsEmpty())
			{
				continue;
			}

			if (IsPlannedCellDischargedByForeignBundleCandidate(Context, PlannedCell.Cell, ReachableNodesPtr))
			{
				continue;
			}

			++Context.Result.PropagationStats.FailedCellCount;
			OutFailureReason = FString::Printf(
				TEXT("Propagation found no legal candidates for planned cell %s with intent %s. Candidate failures: %s"),
				*PlannedCell.Cell.ToString(),
				ToDebugString(PlannedCell.Intent),
				FailureReasons.IsEmpty() ? TEXT("<none>") : *FString::Join(FailureReasons, TEXT(" | ")));
			return false;
		}

		return true;
	}

	bool SelectNextCellByMRV(FSolveContext& Context, FIntVector& OutCell, TArray<FSolveCandidate>& OutLegalCandidates)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_MRVSelection, STAT_PorismLayout_MRVSelection);
		TSet<FWalkableNodeKey> ReachableNodes;
		const bool bRequiresReachability = LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(Context);
		if (bRequiresReachability)
		{
			ReachableNodes = LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(Context);
		}

		// Boost the first entry cell so the solver establishes an entry root before placing
		// interior modules that depend on reachability. Once one entry is placed, the
		// solver's own entry-count range logic handles the rest via normal MRV.
		bool bHasPlacedEntry = false;
		for (const auto& PlacementPair : Context.Placements)
		{
			if (const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(PlacementPair.Key))
			{
				if (*Intent == ELayoutCellIntent::Entry)
				{
					bHasPlacedEntry = true;
					break;
				}
			}
		}

		bool bFoundCell = false;
		int32 BestDomainSize = MAX_int32;
		int32 BestReservationPriority = MIN_int32;
		int32 BestReachabilityRank = MIN_int32;
		ELayoutCellIntent BestCandidateIntent = ELayoutCellIntent::Interior;

		for (const FIntVector& CandidateCell : Context.SolveOrder)
		{
			WaitOnCancellationCheckpointGateForTesting();
			if (HasExceededSolverTimeBudget(Context, Context.Placements.Num()))
			{
				return false;
			}
			if (Context.Placements.Contains(CandidateCell))
			{
				continue;
			}

			TArray<FSolveCandidate> LegalCandidates = GetLegalCandidatesForCellIndexed(
				Context,
				CandidateCell,
				nullptr,
				bRequiresReachability ? &ReachableNodes : nullptr,
				true);
			if (LegalCandidates.IsEmpty())
			{
				const ELayoutCellIntent* DiagIntent = Context.PlannedCellIntents.Find(CandidateCell);
				if (DiagIntent != nullptr && *DiagIntent == ELayoutCellIntent::Entry)
				{
					TArray<FString> DiagReasons;
					GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
						Context, CandidateCell, &DiagReasons,
						bRequiresReachability ? &ReachableNodes : nullptr, false);
				}
			}
			if (LegalCandidates.IsEmpty()
				&& IsPlannedCellDischargedByForeignBundleCandidate(
					Context,
					CandidateCell,
					bRequiresReachability ? &ReachableNodes : nullptr))
			{
				continue;
			}

			const int32 DomainSize = LegalCandidates.Num();
			const int32 ReservationPriority = LayoutProfileSolverInternal::GetReservationSelectionPriority(Context, CandidateCell);
			const ELayoutCellIntent CandidateCellIntent =
				Context.PlannedCellIntents.FindRef(CandidateCell);
			const bool bPrioritizeLiveReachability =
				LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, CandidateCell)
				|| CandidateCellIntent == ELayoutCellIntent::Entry;
			const int32 ReachabilityRank = bRequiresReachability
				&& bPrioritizeLiveReachability
				? LayoutProfileSolverInternal::GetCellReachabilitySelectionRank(Context, CandidateCell, LegalCandidates, ReachableNodes)
				: 0;

			// Entry cells get priority placement while we still need entries to satisfy
			// the profile. Once enough entries are placed, entry cells compete normally via MRV.
			const ELayoutCellIntent* CandidateIntent = Context.PlannedCellIntents.Find(CandidateCell);
			const bool bIsEntry = CandidateIntent != nullptr && *CandidateIntent == ELayoutCellIntent::Entry;
			const bool bBestIsEntry = bFoundCell && BestCandidateIntent == ELayoutCellIntent::Entry;

			bool bPreferCell = !bFoundCell;
			if (!bPreferCell)
			{
				if (!bHasPlacedEntry && bIsEntry && !bBestIsEntry)
				{
					bPreferCell = true;
				}
				else if (!bHasPlacedEntry && !bIsEntry && bBestIsEntry)
				{
					bPreferCell = false;
				}
				else
				{
					bPreferCell = DomainSize < BestDomainSize
						|| (DomainSize == BestDomainSize && ReservationPriority > BestReservationPriority)
						|| (DomainSize == BestDomainSize && ReservationPriority == BestReservationPriority && ReachabilityRank > BestReachabilityRank);
				}
			}
			if (!bPreferCell)
			{
				continue;
			}

			bFoundCell = true;
			BestDomainSize = DomainSize;
			BestReservationPriority = ReservationPriority;
			BestReachabilityRank = ReachabilityRank;
			BestCandidateIntent = CandidateIntent != nullptr ? *CandidateIntent : ELayoutCellIntent::Interior;
			OutCell = CandidateCell;
			OutLegalCandidates = MoveTemp(LegalCandidates);

			if (BestDomainSize == 0)
			{
				break;
			}
		}

		return bFoundCell;
	}

	bool ForwardCheckNeighbors(FSolveContext& Context, const FIntVector& Cell, FString& OutFailureReason)
	{
		return ForwardCheckPlacedCells(Context, {Cell}, OutFailureReason);
	}

	bool ForwardCheckPlacedCells(FSolveContext& Context, const TArray<FIntVector>& PlacedCells, FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ForwardCheck, STAT_PorismLayout_ForwardCheck);
		for (const FIntVector& PlacedCell : PlacedCells)
		{
			const FSolveContext::FSolvePlacement* Placement =
				Context.Placements.Find(PlacedCell);
			if (Placement == nullptr || !FSolveContext::IsOccupiedPlacement(*Placement))
			{
				continue;
			}

			FString PlacementFailureReason;
			if (!IsPlacementCompatibleWithCompletedNeighborMap(
					Context,
					PlacedCell,
					*Placement,
					&PlacementFailureReason))
			{
				OutFailureReason = FString::Printf(
					TEXT("Placed bundle cell %s is not compatible with the current boundary/neighbor map after immediate placement. %s"),
					*PlacedCell.ToString(),
					PlacementFailureReason.IsEmpty()
						? TEXT("No additional placement-compatibility detail was reported.")
						: *PlacementFailureReason);
				return false;
			}
		}

		if (!ValidateAffectedHardClosureFeasibility(Context, PlacedCells, OutFailureReason)
			|| !ValidateHardZoneFeatureDemandFeasibility(Context, OutFailureReason))
		{
			return false;
		}

		TSet<FIntVector> NeighborCellsToCheck;
		for (const FIntVector& PlacedCell : PlacedCells)
		{
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
				const FIntVector NeighborCell = PlacedCell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (!FaceCarriesLivePlannedNeighborSupport(
						Context,
						PlacedCell,
						Direction)
					|| Context.Placements.Contains(NeighborCell))
				{
					continue;
				}

				NeighborCellsToCheck.Add(NeighborCell);
			}
		}

		for (const FIntVector& NeighborCell : NeighborCellsToCheck)
		{
			WaitOnCancellationCheckpointGateForTesting();
			if (HasExceededSolverTimeBudget(Context, Context.Placements.Num()))
			{
				return false;
			}
			if (!HasAnyLegalCandidateForCell(Context, NeighborCell))
			{
				if (IsPlannedCellDischargedByForeignBundleCandidate(Context, NeighborCell))
				{
					continue;
				}

				TArray<FString> NeighborFailureReasons;
				HasAnyLegalCandidateForCell(Context, NeighborCell, &NeighborFailureReasons);
				OutFailureReason = FString::Printf(
					TEXT("Forward checking rejected neighboring planned cell %s with intent %s after placing cells [%s]. Neighbor candidate failures: %s"),
					*NeighborCell.ToString(),
					ToDebugString(Context.PlannedCellIntents[NeighborCell]),
					*FString::JoinBy(PlacedCells, TEXT(", "), [](const FIntVector& Cell) { return Cell.ToString(); }),
					NeighborFailureReasons.IsEmpty() ? TEXT("<none>") : *FString::Join(NeighborFailureReasons, TEXT(" | ")));
				return false;
			}
		}

		if (!ShouldRunExpensiveForwardChecks(Context))
		{
			return true;
		}

		// Partial reachability is a useful ordering signal, but it is not a
		// sound hard prune while the shell and interior are still being solved.
		// A normal floor may only become reachable after later cells complete
		// the path to an entry, so strict reachability is enforced once the
		// placement map is complete.
		if (!PropagateUnsolvedDomains(Context, OutFailureReason))
		{
			return false;
		}

		const bool bRepairMode = Context.bForceReachabilityBranchFaceScore
			|| Context.Result.PropagationStats.BacktrackCount >= ReachabilityBranchFaceScoreBacktrackThreshold;
		return !bRepairMode || LayoutProfileSolverInternal::ValidatePartialReachabilityFeasibility(Context, OutFailureReason);
	}

	bool HasExceededSolverTimeBudget(FSolveContext& Context, const int32 SolveDepth);

	void RollbackForcedPlacements(FSolveContext& Context, const TArray<FIntVector>& ForcedCells)
	{
		RollbackHardZoneFeatureBundleCommitments(Context, ForcedCells);
		for (int32 Index = ForcedCells.Num() - 1; Index >= 0; --Index)
		{
			Context.Placements.Remove(ForcedCells[Index]);
		}
	}

	bool ApplyForcedPlacements(FSolveContext& Context, const int32 SolveDepth, TArray<FIntVector>& OutForcedCells)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ForcedPlacements, STAT_PorismLayout_ForcedPlacements);
		if (!Context.ForcedPlacementBundleInsertions.IsEmpty())
		{
			PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ForcedBundlePlacements, STAT_PorismLayout_ForcedBundlePlacements);
			bool bChanged = true;
			while (bChanged)
			{
				if (HasExceededSolverTimeBudget(Context, SolveDepth))
				{
					return false;
				}

				bChanged = false;
				const bool bRequiresReachability = LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(Context);
				TSet<FWalkableNodeKey> ReachableNodes;
				if (bRequiresReachability)
				{
					PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ForcedPlacementReachability, STAT_PorismLayout_ForcedPlacementReachability);
					ReachableNodes = LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(Context);
				}
				const TSet<FWalkableNodeKey>* ReachableNodesPtr = bRequiresReachability ? &ReachableNodes : nullptr;
				for (const FLayoutForcedPlacementBundleInsertion& Insertion : Context.ForcedPlacementBundleInsertions)
				{
					WaitOnCancellationCheckpointGateForTesting();
					if (HasExceededSolverTimeBudget(Context, SolveDepth))
					{
						return false;
					}
					if (IsForcedPlacementBundleInsertionAlreadySatisfied(Context, Insertion))
					{
						continue;
					}

					if (Context.Placements.Contains(Insertion.AnchorCell))
					{
						Context.Result.FailureReason = FString::Printf(
							TEXT("Forced placement bundle insertion '%s' expected an occupied bundle rooted at %s that proves planned cell %s, but the current placement map already holds an incompatible placement there."),
							*Insertion.BundleId.ToString(),
							*Insertion.AnchorCell.ToString(),
							*Insertion.ProvingCell.ToString());
						AddTraceEvent(Context, FString::Printf(
							TEXT("forced-bundle-insertion-conflict depth=%d bundle=%s anchor=%s proving=%s"),
							SolveDepth,
							*Insertion.BundleId.ToString(),
							*Insertion.AnchorCell.ToString(),
							*Insertion.ProvingCell.ToString()));
						return false;
					}

					TArray<FString> FailureReasons;
					const TArray<FSolveCandidate> LegalCandidates =
						GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
						Context,
						Insertion.AnchorCell,
						&FailureReasons,
						ReachableNodesPtr,
						true);
					TArray<FSolveCandidate> MatchingCandidates;
					for (const FSolveCandidate& Candidate : LegalCandidates)
					{
						if (DoesCandidateMatchForcedPlacementBundleInsertion(Context, Insertion, Candidate))
						{
							MatchingCandidates.Add(Candidate);
						}
					}

					if (MatchingCandidates.IsEmpty())
					{
						const FString FailureDetail = FailureReasons.IsEmpty()
							? TEXT("No legal candidate satisfies the anchored bundle requirement.")
							: FString::Join(FailureReasons, TEXT(" | "));
						Context.Result.FailureReason = FString::Printf(
							TEXT("Forced placement bundle insertion '%s' anchored at %s cannot be satisfied while proving cell %s. %s"),
							*Insertion.BundleId.ToString(),
							*Insertion.AnchorCell.ToString(),
							*Insertion.ProvingCell.ToString(),
							*FailureDetail);
						AddTraceEvent(Context, FString::Printf(
							TEXT("forced-bundle-insertion-failed depth=%d bundle=%s anchor=%s proving=%s reason=%s"),
							SolveDepth,
							*Insertion.BundleId.ToString(),
							*Insertion.AnchorCell.ToString(),
							*Insertion.ProvingCell.ToString(),
							*Context.Result.FailureReason));
						return false;
					}

					bool bPlacedMatchingCandidate = false;
					FString LastForwardCheckFailure;
					for (const FSolveCandidate& Candidate : MatchingCandidates)
					{
						WaitOnCancellationCheckpointGateForTesting();
						if (HasExceededSolverTimeBudget(Context, SolveDepth))
						{
							return false;
						}
						if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::CandidateAttempts, Context.Result.FailureReason))
						{
							Context.bTimeBudgetExceeded = true;
							return false;
						}
						++Context.CandidateAttemptCount;
						if (Context.CandidateAttemptCount > Context.MaxCandidateAttempts)
						{
							Context.Result.FailureReason = FString::Printf(
								TEXT("Layout solve exceeded MaxSolverCandidateAttempts=%d while applying forced bundle insertion '%s' at anchor %s after placing %d of %d planned cells."),
								Context.MaxCandidateAttempts,
								*Insertion.BundleId.ToString(),
								*Insertion.AnchorCell.ToString(),
								SolveDepth,
								Context.SolveOrder.Num());
							AddTraceEvent(Context, FString::Printf(
								TEXT("attempt-budget-exceeded-forced-bundle depth=%d bundle=%s anchor=%s attempts=%d"),
								SolveDepth,
								*Insertion.BundleId.ToString(),
								*Insertion.AnchorCell.ToString(),
								Context.CandidateAttemptCount));
							return false;
						}

						const TArray<FIntVector> PlacedCells =
							CommitOccupiedCandidateBundle(Context, Insertion.AnchorCell, Candidate);
						FString ForwardCheckFailure;
						if (!ForwardCheckPlacedCells(Context, PlacedCells, ForwardCheckFailure))
						{
							LastForwardCheckFailure = ForwardCheckFailure.IsEmpty()
								? FString::Printf(
									TEXT("Forced placement bundle insertion '%s' rooted at %s failed forward-check validation after placement."),
									*Insertion.BundleId.ToString(),
									*Insertion.AnchorCell.ToString())
								: ForwardCheckFailure;
							AddTraceEvent(Context, FString::Printf(
								TEXT("forced-bundle-insertion-pruned depth=%d bundle=%s anchor=%s proving=%s reason=%s"),
								SolveDepth,
								*Insertion.BundleId.ToString(),
								*Insertion.AnchorCell.ToString(),
								*Insertion.ProvingCell.ToString(),
								*LastForwardCheckFailure));
							RollbackForcedPlacements(Context, PlacedCells);
							continue;
						}

						OutForcedCells.Append(PlacedCells);
						bChanged = true;
						bPlacedMatchingCandidate = true;
						AddTraceEvent(Context, FString::Printf(
							TEXT("forced-bundle-insertion depth=%d bundle=%s anchor=%s proving=%s placements=%d"),
							SolveDepth,
							*Insertion.BundleId.ToString(),
							*Insertion.AnchorCell.ToString(),
							*Insertion.ProvingCell.ToString(),
							Context.Placements.Num()));
						break;
					}

					if (!bPlacedMatchingCandidate)
					{
						Context.Result.FailureReason = LastForwardCheckFailure.IsEmpty()
							? FString::Printf(
								TEXT("Forced placement bundle insertion '%s' anchored at %s cannot be satisfied while proving cell %s because every matching candidate is pruned before ordinary search can continue."),
								*Insertion.BundleId.ToString(),
								*Insertion.AnchorCell.ToString(),
								*Insertion.ProvingCell.ToString())
							: LastForwardCheckFailure;
						return false;
					}

					break;
				}
			}
		}

		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ForcedSingletonResolution, STAT_PorismLayout_ForcedSingletonResolution);
		bool bChanged = true;
		while (bChanged)
		{
			if (HasExceededSolverTimeBudget(Context, SolveDepth))
			{
				return false;
			}

			bChanged = false;
			const bool bRequiresReachability = LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(Context);
			TSet<FWalkableNodeKey> ReachableNodes;
			if (bRequiresReachability)
			{
				PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ForcedPlacementReachability, STAT_PorismLayout_ForcedPlacementReachability);
				ReachableNodes = LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(Context);
			}
			const TSet<FWalkableNodeKey>* ReachableNodesPtr = bRequiresReachability ? &ReachableNodes : nullptr;
			{
				PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ForcedCandidateScan, STAT_PorismLayout_ForcedCandidateScan);
				for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
				{
					WaitOnCancellationCheckpointGateForTesting();
					if (HasExceededSolverTimeBudget(Context, SolveDepth))
					{
						return false;
					}
					if (Context.Placements.Contains(PlannedCell.Cell))
					{
						continue;
					}

					// Reuse this pass's immutable reachability snapshot instead of rebuilding the same graph for every scanned cell.
					TArray<FSolveCandidate> LegalCandidates = GetLegalCandidatesForCellIndexed(
						Context,
						PlannedCell.Cell,
						nullptr,
						ReachableNodesPtr,
						false);
					if (LegalCandidates.Num() != 1)
					{
						continue;
					}

					const FSolveCandidate& Candidate = LegalCandidates[0];

					if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::CandidateAttempts, Context.Result.FailureReason))
					{
						Context.bTimeBudgetExceeded = true;
						return false;
					}
					++Context.CandidateAttemptCount;
					if (Context.CandidateAttemptCount > Context.MaxCandidateAttempts)
					{
						Context.Result.FailureReason = FString::Printf(
							TEXT("Layout solve exceeded MaxSolverCandidateAttempts=%d while applying forced placement for intent %s(%d) at cell %s after placing %d of %d planned cells."),
							Context.MaxCandidateAttempts,
							ToDebugString(PlannedCell.Intent),
							static_cast<int32>(PlannedCell.Intent),
							*PlannedCell.Cell.ToString(),
							SolveDepth,
							Context.SolveOrder.Num());
						AddTraceEvent(Context, FString::Printf(
							TEXT("attempt-budget-exceeded-forced depth=%d cell=%s attempts=%d"),
							SolveDepth,
							*PlannedCell.Cell.ToString(),
							Context.CandidateAttemptCount));
						return false;
					}

					TArray<FIntVector> PlacedCells;
					if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
					{
						Context.Placements.Add(PlannedCell.Cell, MakeRootSolvePlacement(Context, PlannedCell.Cell, 0, INDEX_NONE));
						PlacedCells.Add(PlannedCell.Cell);
					}
					else
					{
						PlacedCells = CommitOccupiedCandidateBundle(Context, PlannedCell.Cell, Candidate);
					}

					OutForcedCells.Append(PlacedCells);
					bChanged = true;

					FString ForwardCheckFailure;
					if (!ForwardCheckPlacedCells(Context, PlacedCells, ForwardCheckFailure))
					{
						Context.Result.FailureReason = ForwardCheckFailure.IsEmpty()
							? FString::Printf(TEXT("Forced placement rejected cell %s after choosing its only legal candidate."), *PlannedCell.Cell.ToString())
							: ForwardCheckFailure;
						AddTraceEvent(Context, FString::Printf(
							TEXT("forced-placement-pruned depth=%d cell=%s candidate=%s reason=%s"),
							SolveDepth,
							*PlannedCell.Cell.ToString(),
							LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) ? *BuildCandidateDebugName(Context, Candidate) : TEXT("<empty>"),
							*Context.Result.FailureReason));
						return false;
					}

					AddTraceEvent(Context, FString::Printf(
						TEXT("forced-placement depth=%d cell=%s intent=%s candidate=%s placements=%d"),
						SolveDepth,
						*PlannedCell.Cell.ToString(),
						ToDebugString(PlannedCell.Intent),
						LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) ? *BuildCandidateDebugName(Context, Candidate) : TEXT("<empty>"),
						Context.Placements.Num()));
					break;
				}
			}
		}

		return true;
	}

	bool AllCellsSolved(FSolveContext& Context)
	{
		for (const TPair<FIntVector, FSolveCellFaceInterfaceSet>& Pair : Context.CompiledFaceInterfaces)
		{
			if (!Context.Placements.Contains(Pair.Key)
				&& !IsPlannedCellDischargedByForeignBundleCandidate(Context, Pair.Key, nullptr))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateSolvedPlacementAdjacency(FSolveContext& Context, FString& OutFailureReason)
	{
		int32 InScopePlacementCount = 0;
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
		{
			if (DoesContextCarryCompiledPlannedCell(
					Context,
					PlacementPair.Key))
			{
				++InScopePlacementCount;
				continue;
			}

			if (!DoesContextAdmitProjectedOccupiedWorldCell(
					Context,
					PlacementPair.Key))
			{
				OutFailureReason = FString::Printf(
					TEXT("Solved layout contains unexpected placement outside the current or external planned-cell sets at %s."),
					*PlacementPair.Key.ToString());
				return false;
			}
		}

		if (InScopePlacementCount != Context.CompiledFaceInterfaces.Num())
		{
			OutFailureReason = FString::Printf(
				TEXT("Solved layout placement count does not match planned cell count. placements=%d planned=%d"),
				InScopePlacementCount,
				Context.CompiledFaceInterfaces.Num());
			return false;
		}

		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			const FSolveContext::FSolvePlacement* Placement = Context.Placements.Find(PlannedCell.Cell);
			if (Placement == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Solved layout is missing a placement for planned cell %s with intent %s."),
					*PlannedCell.Cell.ToString(),
					ToDebugString(PlannedCell.Intent));
				return false;
			}

			if (FSolveContext::IsOccupiedPlacement(*Placement))
			{
				FString PlacementFailureReason;
				if (!IsPlacementCompatibleWithCompletedNeighborMap(Context, PlannedCell.Cell, *Placement, &PlacementFailureReason))
				{
					OutFailureReason = FString::Printf(
						TEXT("Solved layout failed the final adjacency audit.\nCell: %s\nIntent: %s\nModule: %s\nYaw: %d\nProblem: The placed module is not compatible with the completed neighbor map.\nDetails:\n%s"),
						*PlannedCell.Cell.ToString(),
						ToDebugString(PlannedCell.Intent),
						*BuildPlacementDebugName(Context, *Placement),
						Placement->YawRotationSteps,
						PlacementFailureReason.IsEmpty() ? TEXT("Placement is not compatible with the completed placement map.") : *PlacementFailureReason);
					return false;
				}

				continue;
			}

			// Admission and publication share the same guarded offer contract. An omitted
			// generated deck is not a terrain residual, but still owes all empty-face checks.
			if ((Context.TerrainResidualRuleIdByCell.Contains(PlannedCell.Cell)
				|| CanTopBridgeOfferRemainEmpty(Context, PlannedCell.Cell))
				&& DoesCellSatisfyCompiledEmptyInterfaces(Context, PlannedCell.Cell, &OutFailureReason))
			{
				continue;
			}

			OutFailureReason = FString::Printf(
				TEXT("Solved layout contains unsupported empty placement at planned cell %s."),
				*PlannedCell.Cell.ToString());
			return false;
		}


		// Fixed children are outside parent search scope. Audit their external occupancy against the
		// settled map too; cropping must not erase a non-egress support or clearance obligation.
		for (const TSharedPtr<const FLayoutRegionSolveResult>& Child : Context.SparseStructuralChildProofs)
		{
			TSet<FIntVector> ChildCells;
			for (const FLayoutPlannedCell& Cell : Child->SolveResult.PlannedCells)
			{
				if (!ConsumeSparseStructuralWork(Context)) { OutFailureReason = Context.Result.FailureReason; return false; }
				ChildCells.Add(Cell.Cell + Child->RegionCellOffset);
			}
			for (const FIntVector& Cell : ChildCells)
			{
				const FSolveContext::FSolvePlacement* Placement = FindPlacementOrFixedNeighbor(Context, Cell);
				if (Placement == nullptr || !FSolveContext::IsOccupiedPlacement(*Placement)) continue;
				for (int32 Index = 0; Index < 6; ++Index)
				{
					if (!ConsumeSparseStructuralWork(Context)) { OutFailureReason = Context.Result.FailureReason; return false; }
					const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(Index);
					const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (ChildCells.Contains(NeighborCell)) continue; // Child-internal proof remains immutable.
					FLayoutFaceRule Face;
					if (!TryGetSolvePlacementFaceRule(Context, *Placement, Direction, Face)) continue;
					const FSolveContext::FSolvePlacement* Neighbor = FindPlacementOrFixedNeighbor(Context, NeighborCell);
					FLayoutFaceRule NeighborFace;
					const bool bCompatible = Neighbor != nullptr && FSolveContext::IsOccupiedPlacement(*Neighbor)
						? TryGetSolvePlacementFaceRule(Context, *Neighbor, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFace)
							&& DoesFilledNeighborFacePairSatisfyInterfaceContract(Face, Placement->YawRotationSteps,
								NeighborFace, Neighbor->YawRotationSteps)
						: FaceAllowsEmptyNeighbor(Face);
					if (!bCompatible)
					{
						OutFailureReason = FString::Printf(TEXT("Sparse child %s face at %s toward %s violates settled support/clearance or reciprocal face contract."),
							*Child->RegionDebugPath, *Cell.ToString(), *NeighborCell.ToString());
						return false;
					}
				}
			}
		}
		return true;
	}

	void AddAcceptedPlacementTraceEvents(FSolveContext& Context)
	{
		if (!IsTracingEnabled(Context))
		{
			return;
		}

		TArray<FIntVector> Cells;
		Context.Placements.GenerateKeyArray(Cells);
		Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			if (Left.Z != Right.Z)
			{
				return Left.Z < Right.Z;
			}
			if (Left.Y != Right.Y)
			{
				return Left.Y < Right.Y;
			}
			return Left.X < Right.X;
		});

		for (const FIntVector& Cell : Cells)
		{
			const FSolveContext::FSolvePlacement* Placement = Context.Placements.Find(Cell);
			if (Placement == nullptr)
			{
				continue;
			}

			const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
			AddTraceEvent(Context, FString::Printf(
				TEXT("accepted-placement cell=%s intent=%s module=%s yawSteps=%d yawDegrees=%d variant=%d"),
				*Cell.ToString(),
				Intent != nullptr ? ToDebugString(*Intent) : TEXT("<none>"),
				FSolveContext::IsOccupiedPlacement(*Placement) ? *BuildPlacementDebugName(Context, *Placement) : TEXT("<empty>"),
				Placement->YawRotationSteps,
				Placement->YawRotationSteps * 90,
				Placement->VariantIndex));
		}

		if (!Context.bIncludeTraceCandidateDetails)
		{
			return;
		}

		static const ELayoutFaceDirection PositiveDirections[] = {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::PosZ
		};

		for (const FIntVector& Cell : Cells)
		{
			const FSolveContext::FSolvePlacement* Placement = Context.Placements.Find(Cell);
			if (Placement == nullptr || !FSolveContext::IsOccupiedPlacement(*Placement))
			{
				continue;
			}

			for (const ELayoutFaceDirection Direction : PositiveDirections)
			{
				const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				const FSolveContext::FSolvePlacement* NeighborPlacement = FindPlacementOrFixedNeighbor(Context, NeighborCell);
				if (NeighborPlacement == nullptr || !FSolveContext::IsOccupiedPlacement(*NeighborPlacement))
				{
					continue;
				}

				FLayoutFaceRule FaceRule;
				FLayoutFaceRule NeighborFaceRule;
				if (!TryGetPlacementFaceRule(Context, *Placement, Direction, FaceRule)
					|| !TryGetPlacementFaceRule(Context, *NeighborPlacement, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule))
				{
					continue;
				}

				const ELayoutFaceDirection AuthoredDirection = GetAuthoredDirectionForWorldDirection(Direction, Placement->YawRotationSteps);
				const ELayoutFaceDirection NeighborWorldDirection = FLayoutDirectionUtils::GetOpposite(Direction);
				const ELayoutFaceDirection NeighborAuthoredDirection = GetAuthoredDirectionForWorldDirection(NeighborWorldDirection, NeighborPlacement->YawRotationSteps);
				AddTraceEvent(Context, FString::Printf(
					TEXT("accepted-adjacency cell=%s module=%s yawSteps=%d worldFace=%s authoredFace=%s policy=%s connection=[%s] allowed=[%s] walkable=[%s] -> cell=%s module=%s yawSteps=%d worldFace=%s authoredFace=%s policy=%s connection=[%s] allowed=[%s] walkable=[%s] sharedWalkable=%s"),
					*Cell.ToString(),
					*BuildPlacementDebugName(Context, *Placement),
					Placement->YawRotationSteps,
					ToDebugString(Direction),
					ToDebugString(AuthoredDirection),
					ToDebugString(FaceRule.OccupancyPolicy),
					*TagsToDebugString(FaceRule.GetEffectiveConnectionTags()),
					*TagsToDebugString(FaceRule.GetEffectiveAllowedConnectionTags()),
					*TagsToDebugString(FaceRule.ConnectedTraversalChannels),
					*NeighborCell.ToString(),
					*BuildPlacementDebugName(Context, *NeighborPlacement),
					NeighborPlacement->YawRotationSteps,
					ToDebugString(NeighborWorldDirection),
					ToDebugString(NeighborAuthoredDirection),
					ToDebugString(NeighborFaceRule.OccupancyPolicy),
					*TagsToDebugString(NeighborFaceRule.GetEffectiveConnectionTags()),
					*TagsToDebugString(NeighborFaceRule.GetEffectiveAllowedConnectionTags()),
					*TagsToDebugString(NeighborFaceRule.ConnectedTraversalChannels),
					HasSharedConnectedWalkableArea(FaceRule, NeighborFaceRule) ? TEXT("true") : TEXT("false")));
			}
		}
	}

	uint64 BuildFailedStateHash(const FSolveContext& Context)
	{
		uint64 Hash = HashCombineFast(GetTypeHash(Context.FootprintSize), static_cast<uint32>(Context.Placements.Num()));
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			const FSolveContext::FSolvePlacement* Placement = Context.Placements.Find(PlannedCell.Cell);
			if (Placement == nullptr)
			{
				continue;
			}

			const uint32 PlacementValue = !FSolveContext::IsOccupiedPlacement(*Placement)
				? 0x7ffffffeu
				: static_cast<uint32>(FMath::Max(0, Placement->VariantIndex) + 1);
			Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(PlannedCell.Cell), PlacementValue));
		}

		return Hash;
	}

	void MemoizeFailedState(FSolveContext& Context)
	{
		if (Context.CandidateAttemptCount < Context.MaxCandidateAttempts)
		{
			Context.FailedStateHashes.Add(BuildFailedStateHash(Context));
		}
	}

	void AddCandidateFailureDetail(
		FSolveContext& Context,
		TArray<FString>& CandidateFailureReasons,
		const FSolveCandidate& Candidate,
		const FString& FailureReason)
	{
		if (FailureReason.IsEmpty() || CandidateFailureReasons.Num() >= Context.MaxFailureDetails)
		{
			return;
		}

		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			const FString Detail = TruncateSolverMessage(FailureReason, MaxSolverFailureDetailChars);
			if (CandidateFailureReasons.Contains(Detail))
			{
				return;
			}

			CandidateFailureReasons.Add(Detail);
			return;
		}

		const FString Detail = FString::Printf(
			TEXT("%s: %s"),
			*BuildCandidateDebugName(Context, Candidate),
			*TruncateSolverMessage(FailureReason, MaxSolverFailureDetailChars));
		if (CandidateFailureReasons.Contains(Detail))
		{
			return;
		}

		CandidateFailureReasons.Add(Detail);
	}

	void ClearBacktrackableFailure(FSolveContext& Context)
	{
		if (!Context.bTimeBudgetExceeded && Context.CandidateAttemptCount < Context.MaxCandidateAttempts)
		{
			Context.Result.FailureReason.Reset();
		}
	}

	bool HasExceededSolverTimeBudget(FSolveContext& Context, const int32 SolveDepth)
	{
		if (Context.bTimeBudgetExceeded)
		{
			return true;
		}

		if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested())
		{
			if (Context.Result.FailureReason.IsEmpty())
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Layout solve canceled after %d candidate attempts and %d of %d placed cells."),
					Context.CandidateAttemptCount,
					SolveDepth,
					Context.SolveOrder.Num());
			}
			Context.bTimeBudgetExceeded = true;
			AddTraceEvent(Context, FString::Printf(
				TEXT("canceled depth=%d placements=%d attempts=%d"),
				SolveDepth,
				Context.Placements.Num(),
				Context.CandidateAttemptCount));
			return true;
		}

		if (!LayoutSolveExecution::Checkpoint(Context.Result.FailureReason))
		{
			Context.bTimeBudgetExceeded = true;
			return true;
		}

		if (Context.MaxSolveDurationSeconds <= 0.0)
		{
			return false;
		}

		const double ElapsedSeconds = FPlatformTime::Seconds() - Context.SolveStartTimeSeconds;
		if (ElapsedSeconds < Context.MaxSolveDurationSeconds)
		{
			return false;
		}

		if (Context.Result.FailureReason.IsEmpty())
		{
			Context.Result.FailureReason = FString::Printf(
				TEXT("Layout solve exceeded MaxSolverDurationSeconds=%.2f after %.2f seconds, %d candidate attempts, and %d of %d placed cells."),
				Context.MaxSolveDurationSeconds,
				ElapsedSeconds,
				Context.CandidateAttemptCount,
				SolveDepth,
				Context.SolveOrder.Num());
		}

		Context.bTimeBudgetExceeded = true;
			{
				Context.Result.Placements.Reset();
				for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
				{
					if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value)
						|| !IsBundleRootSolvePlacement(PlacementPair.Key, PlacementPair.Value))
					{
						continue;
					}
					FLayoutPlacedModule& Placement = Context.Result.Placements.AddDefaulted_GetRef();
					Placement.Cell = PlacementPair.Key;
					Placement.Intent = Context.PlannedCellIntents[PlacementPair.Key];
					Placement.YawRotationSteps = PlacementPair.Value.YawRotationSteps;
					Placement.ModuleSnapshotIndex = PlacementPair.Value.ModuleSnapshotIndex;
					Placement.ModuleSnapshotId = PlacementPair.Value.ModuleSnapshotId;
					if (PlacementPair.Value.VariantIndex != INDEX_NONE
						&& Context.Variants.IsValidIndex(PlacementPair.Value.VariantIndex))
					{
						Placement.SourceContentEntryId = Context.Variants[PlacementPair.Value.VariantIndex].SourceContentEntryId;
					}
				}
			}
		AddTraceEvent(Context, FString::Printf(
			TEXT("time-budget-exceeded depth=%d placements=%d attempts=%d elapsedSeconds=%.3f maxSeconds=%.3f"),
			SolveDepth,
			Context.Placements.Num(),
			Context.CandidateAttemptCount,
			ElapsedSeconds,
			Context.MaxSolveDurationSeconds));
		return true;
	}

	/**
	 * Spend one exact later-than-prefix feasibility pass on boundary- and
	 * traversal-anchor-constrained unsolved cells before the generic recursive
	 * branch loop resumes.
	 *
	 * This only runs on request-local staged-validation solves that already
	 * opted into expensive forward checks. It rejects one constrained cell only
	 * when every surviving legal candidate already dies on immediate apply plus
	 * forward checking, so it removes later parent-proof work without using a
	 * score or ordering heuristic.
	 */
	static bool TryValidatePostPrefixConstrainedCellFeasibility(
		FSolveContext& Context,
		const int32 SolveDepth,
		const FIntVector& SelectedCell)
	{
		if (!Context.bForceExpensiveForwardChecks
			|| (!HasAnyCompiledBoundaryConstraint(Context)
				&& Context.CommittedTraversalAnchors.IsEmpty()))
		{
			return true;
		}

		TSet<FIntVector> BoundaryConstrainedCells;
		TSet<FIntVector> TraversalAnchorConstrainedCells;
		TSet<FIntVector> UpperBoundaryIntentCells;
		for (const TPair<FIntVector, ELayoutCellIntent>& PlannedCellPair :
			Context.PlannedCellIntents)
		{
			if (!Context.Placements.Contains(PlannedCellPair.Key)
				&& CellHasCompiledBoundaryConstraint(
					Context,
					PlannedCellPair.Key))
			{
				BoundaryConstrainedCells.Add(PlannedCellPair.Key);
			}

			if (PlannedCellPair.Key.Z > 0
				&& DoesCellCarryBoundaryExposure(
					Context,
					PlannedCellPair.Key)
				&& !Context.Placements.Contains(PlannedCellPair.Key))
			{
				UpperBoundaryIntentCells.Add(PlannedCellPair.Key);
			}
		}
		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
			Context.CommittedTraversalAnchors)
		{
			if (DoesContextCarryCompiledPlannedCell(
					Context,
					TraversalAnchor.Cell)
				&& !Context.Placements.Contains(TraversalAnchor.Cell))
			{
				TraversalAnchorConstrainedCells.Add(TraversalAnchor.Cell);
			}
		}

		if (BoundaryConstrainedCells.IsEmpty()
			&& TraversalAnchorConstrainedCells.IsEmpty()
			&& UpperBoundaryIntentCells.IsEmpty())
		{
			return true;
		}

		const bool bRequiresReachability =
			LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(
				Context);
		const TSet<FWalkableNodeKey> ReachableNodes =
			bRequiresReachability
				? LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(
					Context)
				: TSet<FWalkableNodeKey>();
		const TSet<FWalkableNodeKey>* ReachableNodesPtr =
			bRequiresReachability ? &ReachableNodes : nullptr;

		for (const FIntVector& CandidateCell : Context.SolveOrder)
		{
			if (CandidateCell == SelectedCell
				|| Context.Placements.Contains(CandidateCell))
			{
				continue;
			}

			const bool bBoundaryConstrained =
				BoundaryConstrainedCells.Contains(CandidateCell);
			const bool bTraversalAnchorConstrained =
				TraversalAnchorConstrainedCells.Contains(CandidateCell);
			const bool bUpperBoundaryIntentConstrained =
				UpperBoundaryIntentCells.Contains(CandidateCell);
			if (!bBoundaryConstrained
				&& !bTraversalAnchorConstrained
				&& !bUpperBoundaryIntentConstrained)
			{
				continue;
			}

			if (IsPlannedCellDischargedByForeignBundleCandidate(
					Context,
					CandidateCell,
					ReachableNodesPtr))
			{
				if (bUpperBoundaryIntentConstrained
					&& !TryValidateDischargedUpperBoundaryIntentCellFeasibility(
						Context,
						SolveDepth,
						CandidateCell,
						ReachableNodesPtr))
				{
					return false;
				}

				continue;
			}

			TArray<FString> CandidateFailureReasons;
			const TArray<FSolveCandidate> LegalCandidates =
				GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
					Context,
					CandidateCell,
					&CandidateFailureReasons,
					ReachableNodesPtr,
					true);
			if (LegalCandidates.IsEmpty())
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Post-prefix constrained-cell feasibility rejected planned cell %s with intent %s. BoundaryConstrained=%d TraversalAnchorConstrained=%d UpperBoundaryIntentConstrained=%d. No legal candidates remain before recursion. Candidate failures: %s"),
					*CandidateCell.ToString(),
					ToDebugString(Context.PlannedCellIntents.FindRef(CandidateCell)),
					bBoundaryConstrained ? 1 : 0,
					bTraversalAnchorConstrained ? 1 : 0,
					bUpperBoundaryIntentConstrained ? 1 : 0,
					CandidateFailureReasons.IsEmpty()
						? TEXT("<none>")
						: *FString::Join(CandidateFailureReasons, TEXT(" | ")));
				AddTraceEvent(Context, FString::Printf(
					TEXT("post-prefix-constrained-domain-empty depth=%d cell=%s boundary=%d anchor=%d upperBoundary=%d"),
					SolveDepth,
					*CandidateCell.ToString(),
					bBoundaryConstrained ? 1 : 0,
					bTraversalAnchorConstrained ? 1 : 0,
					bUpperBoundaryIntentConstrained ? 1 : 0));
				return false;
			}

			bool bFoundImmediateContinuation = false;
			TArray<FString> ImmediateFailureReasons;
			const ELayoutCellIntent Intent =
				Context.PlannedCellIntents.FindRef(CandidateCell);
			for (const FSolveCandidate& Candidate : LegalCandidates)
			{
				const int32 SavedCandidateAttemptCount =
					Context.CandidateAttemptCount;
				FPreparedSearchBranchApplyStageResult ApplyStage;
				const EPreparedSearchBranchApplyOutcome ApplyOutcome =
					TryApplyPreparedSearchBranchWorkItemStage(
						Context,
						SolveDepth,
						FPreparedSearchBranchWorkItem{
							CandidateCell,
							Intent,
							Candidate},
						ApplyStage,
						false);
				Context.CandidateAttemptCount = SavedCandidateAttemptCount;

				if (ApplyOutcome
					== EPreparedSearchBranchApplyOutcome::
						ReadyForRecursiveContinuation)
				{
					RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
					bFoundImmediateContinuation = true;
					break;
				}

				if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::Rejected)
				{
					const FString FailureReason =
						!ApplyStage.ForwardCheckFailure.IsEmpty()
							? ApplyStage.ForwardCheckFailure
							: Context.Result.FailureReason;
					if (!FailureReason.IsEmpty())
					{
						AddCandidateFailureDetail(
							Context,
							ImmediateFailureReasons,
							Candidate,
							FailureReason);
					}

					RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
					ClearBacktrackableFailure(Context);
					continue;
				}

				return false;
			}

			if (bFoundImmediateContinuation)
			{
				continue;
			}

			Context.Result.FailureReason = FString::Printf(
				TEXT("Post-prefix constrained-cell feasibility rejected planned cell %s with intent %s. BoundaryConstrained=%d TraversalAnchorConstrained=%d UpperBoundaryIntentConstrained=%d. No legal candidate survives immediate placement and forward checking. Candidate failures: %s"),
				*CandidateCell.ToString(),
				ToDebugString(Intent),
				bBoundaryConstrained ? 1 : 0,
				bTraversalAnchorConstrained ? 1 : 0,
				bUpperBoundaryIntentConstrained ? 1 : 0,
				ImmediateFailureReasons.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(ImmediateFailureReasons, TEXT(" | ")));
			AddTraceEvent(Context, FString::Printf(
				TEXT("post-prefix-constrained-immediate-fail depth=%d cell=%s boundary=%d anchor=%d upperBoundary=%d candidates=%d"),
				SolveDepth,
				*CandidateCell.ToString(),
				bBoundaryConstrained ? 1 : 0,
				bTraversalAnchorConstrained ? 1 : 0,
				bUpperBoundaryIntentConstrained ? 1 : 0,
				LegalCandidates.Num()));
			return false;
		}

		return true;
	}

	/**
	 * Spend one exact later-than-prefix feasibility pass after a real upper-level
	 * branch apply has settled more of a multilevel continuation frontier.
	 *
	 * This reuses the same constrained-cell proof as the staged prefix seam, but
	 * only on actual continuation branches. Speculative inner candidate checks
	 * intentionally skip this stage so the solver does not recurse into another
	 * broad feasibility sweep while it is already proving one constrained cell.
	 */
	static bool TryValidatePostBranchContinuationFeasibility(
		FSolveContext& Context,
		const int32 NextSolveDepth,
		const FIntVector& BranchCell,
		FString& OutFailureReason)
	{
		if (BranchCell.Z <= 0
			|| !HasAnyExternalContinuationCarrier(Context)
			|| !Context.bForceExpensiveForwardChecks
			|| (!HasAnyCompiledBoundaryConstraint(Context)
				&& Context.CommittedTraversalAnchors.IsEmpty()))
		{
			return true;
		}

		if (TryValidatePostPrefixConstrainedCellFeasibility(
				Context,
				NextSolveDepth,
				BranchCell))
		{
			return true;
		}

		OutFailureReason = Context.Result.FailureReason;
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[VerticalAccessPostBranchFeasibilityFailure] region=%s cell=%s reason=%s"),
				*Context.RegionDebugPath,
				*BranchCell.ToString(),
				*OutFailureReason);
		}
		return false;
	}

	/**
	 * Checks only the upper-level boundary cells directly impacted by the latest
	 * settled continuation placements before generic branching reopens.
	 *
	 * This keeps the later continuation cut local to the newest live frontier
	 * change set instead of sweeping every upper boundary cell again.
	 */
	static bool TryValidateLocallyImpactedUpperBoundaryDomainFeasibility(
		FSolveContext& Context,
		const int32 SolveDepth,
		const TArray<FIntVector>& SeedCells)
	{
		if (SeedCells.IsEmpty()
			|| !HasAnyExternalContinuationCarrier(Context)
			|| !Context.bForceExpensiveForwardChecks)
		{
			return true;
		}

		TSet<FIntVector> CandidateCells;
		static const ELayoutFaceDirection LateralDirections[] = {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY};
		for (const FIntVector& SeedCell : SeedCells)
		{
			for (const ELayoutFaceDirection Direction : LateralDirections)
			{
				const FIntVector NeighborCell =
					SeedCell + FLayoutDirectionUtils::ToCellDelta(Direction);
				const ELayoutCellIntent* Intent =
					Context.PlannedCellIntents.Find(NeighborCell);
				if (Intent == nullptr
					|| !DoesCellCarryBoundaryExposure(
						Context,
						NeighborCell)
					|| NeighborCell.Z <= 0
					|| Context.Placements.Contains(NeighborCell))
				{
					continue;
				}

				CandidateCells.Add(NeighborCell);
			}
		}

		if (CandidateCells.IsEmpty())
		{
			return true;
		}

		const bool bRequiresReachability =
			LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(
				Context);
		const TSet<FWalkableNodeKey> ReachableNodes =
			bRequiresReachability
				? LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(
					Context)
				: TSet<FWalkableNodeKey>();
		const TSet<FWalkableNodeKey>* ReachableNodesPtr =
			bRequiresReachability ? &ReachableNodes : nullptr;

		for (const FIntVector& CandidateCell : CandidateCells)
		{
			if (IsPlannedCellDischargedByForeignBundleCandidate(
					Context,
					CandidateCell,
					ReachableNodesPtr))
			{
				if (!TryValidateDischargedUpperBoundaryIntentCellFeasibility(
						Context,
						SolveDepth,
						CandidateCell,
						ReachableNodesPtr))
				{
					return false;
				}

				continue;
			}

			TArray<FString> CandidateFailureReasons;
			const TArray<FSolveCandidate> LegalCandidates =
				GetLegalCandidatesForCellIndexedWithDeferredFailureReasons(
					Context,
					CandidateCell,
					&CandidateFailureReasons,
					ReachableNodesPtr,
					true);
			if (!LegalCandidates.IsEmpty())
			{
				continue;
			}

			Context.Result.FailureReason = FString::Printf(
				TEXT("Locally impacted upper-boundary feasibility rejected planned cell %s with intent %s after deterministic continuation settled nearby placements. Candidate failures: %s"),
				*CandidateCell.ToString(),
				ToDebugString(
					Context.PlannedCellIntents.FindRef(CandidateCell)),
				CandidateFailureReasons.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(CandidateFailureReasons, TEXT(" | ")));
			AddTraceEvent(Context, FString::Printf(
				TEXT("local-upper-boundary-domain-empty depth=%d cell=%s seeds=%d"),
				SolveDepth,
				*CandidateCell.ToString(),
				SeedCells.Num()));
			return false;
		}

		return true;
	}

	/**
	 * Centralizes the deterministic pre-branch prefix so runtime recursion and
	 * diagnostic replay inspect the same forced-placement and MRV frontier.
	 */
	bool TryPrepareSearchFrontier(
		FSolveContext& Context,
		const int32 SolveDepth,
		FPreparedSearchFrontier& OutFrontier)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_SearchFrontier, STAT_PorismLayout_SearchFrontier);
		if (!ApplyForcedPlacements(Context, SolveDepth, OutFrontier.ForcedCells))
		{
			return false;
		}

		if (AllCellsSolved(Context))
		{
			OutFrontier.bSolvedAfterForcedPlacements = true;
			return true;
		}

		if (!SelectNextCellByMRV(
			Context,
			OutFrontier.SelectedCell,
			OutFrontier.SelectedCandidates))
		{
			Context.Result.FailureReason =
				TEXT("Layout solve could not select an unsolved planned cell.");
			AddTraceEvent(
				Context,
				TEXT("select-cell failed: no unsolved planned cell could be selected"));
			return false;
		}

		if (!TryValidatePostPrefixConstrainedCellFeasibility(
				Context,
				SolveDepth,
				OutFrontier.SelectedCell))
		{
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
			{
				UE_LOG(LogTemp, Display,
					TEXT("[VerticalAccessPostPrefixFeasibilityFailure] region=%s cell=%s intent=%s reason=%s"),
					*Context.RegionDebugPath,
					*OutFrontier.SelectedCell.ToString(),
					ToDebugString(Context.PlannedCellIntents.FindRef(OutFrontier.SelectedCell)),
					*Context.Result.FailureReason);
			}
			return false;
		}

		return true;
	}

	bool TryPrepareSolveContextThroughSearchPrefixStageInternal(
		FSolveContext& Context,
		const int32 SolveDepth,
		FPreparedSearchFrontier& OutSearchFrontier)
	{
		FinalizePreparedSolveContextForIndexedSearchImpl(Context);
		if (!Context.Result.FailureReason.IsEmpty())
		{
			return false;
		}
		LayoutProfileSolverInternal::SeedActiveWalkableAreasFromEntryCandidates(
			Context);
		return TryPrepareSearchFrontier(
			Context,
			SolveDepth,
			OutSearchFrontier);
	}

	/**
	 * Temporary request-local state for collapsing deterministic single-candidate
	 * continuation after one branch apply has already succeeded.
	 *
	 * This exact live solver seam removes recursive work on later unambiguous
	 * frontiers instead of adding another ranking heuristic.
	 */
	struct FPreparedDeterministicSingleCandidateContinuation
	{
		TArray<TArray<FIntVector>> PrefixForcedCells;
		TArray<FPreparedSearchBranchApplyStageResult> ApplyStages;
	};

	static void RollbackPreparedDeterministicSingleCandidateContinuation(
		FSolveContext& Context,
		const FPreparedDeterministicSingleCandidateContinuation& Continuation,
		const bool bSkipTerminalPrefixStage)
	{
		int32 PrefixIndex = Continuation.PrefixForcedCells.Num() - 1;
		if (bSkipTerminalPrefixStage)
		{
			--PrefixIndex;
		}

		int32 ApplyIndex = Continuation.ApplyStages.Num() - 1;
		while (PrefixIndex >= 0 || ApplyIndex >= 0)
		{
			if (PrefixIndex >= 0)
			{
				RollbackForcedPlacements(
					Context,
					Continuation.PrefixForcedCells[PrefixIndex]);
				--PrefixIndex;
			}

			if (ApplyIndex >= 0)
			{
				RollbackPreparedSearchBranchApplyStage(
					Context,
					Continuation.ApplyStages[ApplyIndex]);
				--ApplyIndex;
			}
		}
	}

	/** Captures largest incrementally legal placement frontier for partial artifact recovery. */
	void CaptureBestPartialPlacements(FSolveContext& Context)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_BestPartialCapture, STAT_PorismLayout_BestPartialCapture);
		int32 OccupiedPlacementCount = 0;
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
		{
			OccupiedPlacementCount += FSolveContext::IsOccupiedPlacement(PlacementPair.Value) ? 1 : 0;
		}
		if (OccupiedPlacementCount <= Context.BestPartialOccupiedPlacementCount)
		{
			return;
		}

		Context.BestPartialOccupiedPlacementCount = OccupiedPlacementCount;
		Context.BestPartialPlacements = Context.Placements;
	}

	static bool ContinuePreparedSolveContextAfterDeterministicSingleCandidateStagesImpl(
		FSolveContext& Context,
		const int32 InitialSolveDepth)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_BranchContinuation, STAT_PorismLayout_BranchContinuation);
		FPreparedDeterministicSingleCandidateContinuation Continuation;
		int32 CurrentSolveDepth = InitialSolveDepth;
		while (true)
		{
			FPreparedSearchFrontier SearchFrontier;
			if (!TryPrepareSearchFrontier(
					Context,
					CurrentSolveDepth,
					SearchFrontier))
			{
				if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
				{
					UE_LOG(LogTemp, Display,
						TEXT("[VerticalAccessDeterministicPrefixFailure] region=%s attempts=%d limit=%d reason=%s"),
						*Context.RegionDebugPath,
						Context.CandidateAttemptCount,
						Context.MaxCandidateAttempts,
						*Context.Result.FailureReason);
				}
				Context.Result.bSucceeded = false;
				RollbackPreparedDeterministicSingleCandidateContinuation(
					Context,
					Continuation,
					false);
				return false;
			}

			CaptureBestPartialPlacements(Context);

			LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier
				SearchPrefixStage;
			SearchPrefixStage.ForcedCells = SearchFrontier.ForcedCells;
			SearchPrefixStage.SelectedCell = SearchFrontier.SelectedCell;
			SearchPrefixStage.SelectedCandidates =
				SearchFrontier.SelectedCandidates;
			SearchPrefixStage.bSolvedAfterForcedPlacements =
				SearchFrontier.bSolvedAfterForcedPlacements
				|| AllCellsSolved(Context);
			SearchPrefixStage.Summary.PlacedCellCount = Context.Placements.Num();
			SearchPrefixStage.Summary.ForcedPlacementCellCount =
				SearchFrontier.ForcedCells.Num();
			SearchPrefixStage.Summary.bSolvedAfterForcedPlacements =
				SearchPrefixStage.bSolvedAfterForcedPlacements;
			SearchPrefixStage.Summary.SelectedCell =
				SearchFrontier.SelectedCell;
			SearchPrefixStage.Summary.SelectedDomainSize =
				SearchFrontier.SelectedCandidates.Num();
			if (const ELayoutCellIntent* SelectedIntent =
					Context.PlannedCellIntents.Find(SearchFrontier.SelectedCell))
			{
				SearchPrefixStage.Summary.SelectedIntent = *SelectedIntent;
			}
			Continuation.PrefixForcedCells.Add(SearchFrontier.ForcedCells);

			LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier
				SearchBranchStage;
			PopulatePreparedSearchBranchStageCarrier(
				SearchPrefixStage,
				SearchBranchStage);
			if (SearchBranchStage.SearchPrefixStage.bSolvedAfterForcedPlacements
				|| SearchBranchStage.WorkItems.Num() != 1)
			{
				if (!SearchBranchStage.SearchPrefixStage
						 .bSolvedAfterForcedPlacements
					&& !Continuation.ApplyStages.IsEmpty())
				{
					TArray<FIntVector> LocalSeedCells =
						Continuation.ApplyStages.Last().PlacedCells;
					if (LocalSeedCells.IsEmpty())
					{
						LocalSeedCells.Add(
							Continuation.ApplyStages.Last().WorkItem.Cell);
					}
					LocalSeedCells.Append(SearchFrontier.ForcedCells);
					if (!TryValidateLocallyImpactedUpperBoundaryDomainFeasibility(
							Context,
							CurrentSolveDepth,
							LocalSeedCells))
					{
						if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
						{
							UE_LOG(LogTemp, Display,
								TEXT("[VerticalAccessLocalUpperFeasibilityFailure] region=%s cell=%s reason=%s"),
								*Context.RegionDebugPath,
								*Continuation.ApplyStages.Last().WorkItem.Cell.ToString(),
								*Context.Result.FailureReason);
						}
						Context.Result.bSucceeded = false;
						RollbackPreparedDeterministicSingleCandidateContinuation(
							Context,
							Continuation,
							false);
						return false;
					}
				}

				const bool bSolved = ContinueSolveFromPreparedSearchBranchStage(
					Context,
					CurrentSolveDepth,
					SearchBranchStage);
				Context.Result.bSucceeded = bSolved;
				if (bSolved)
				{
					Context.Result.FailureReason.Reset();
					return true;
				}

				RollbackPreparedDeterministicSingleCandidateContinuation(
					Context,
					Continuation,
					true);
				return false;
			}

			FPreparedSearchBranchApplyStageResult ApplyStage;
			const EPreparedSearchBranchApplyOutcome ApplyOutcome =
				TryApplyPreparedSearchBranchWorkItemStage(
					Context,
					CurrentSolveDepth,
					SearchBranchStage.WorkItems[0],
					ApplyStage);
			if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::AbortSolve)
			{
				Context.Result.bSucceeded = false;
				RollbackPreparedDeterministicSingleCandidateContinuation(
					Context,
					Continuation,
					false);
				return false;
			}
			if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::Rejected)
			{
				Context.Result.bSucceeded = false;
				if (!ApplyStage.ForwardCheckFailure.IsEmpty())
				{
					Context.Result.FailureReason = ApplyStage.ForwardCheckFailure;
				}
				else if (Context.Result.FailureReason.IsEmpty())
				{
					Context.Result.FailureReason = FString::Printf(
						TEXT("Prepared deterministic continuation rejected candidate at cell %s without a detailed failure reason."),
						*ApplyStage.WorkItem.Cell.ToString());
				}

				++Context.Result.PropagationStats.BacktrackCount;
				++Context.Result.PropagationStats.TerrainStageBacktrackCount;
				RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
				RollbackPreparedDeterministicSingleCandidateContinuation(
					Context,
					Continuation,
					false);
				return false;
			}

			CurrentSolveDepth += 1;
			Continuation.ApplyStages.Add(MoveTemp(ApplyStage));
		}
	}

	bool ContinueSolveFromPreparedSearchBranchStage(
		FSolveContext& Context,
		const int32 SolveDepth,
		const FPreparedSearchBranchStageCarrier& SearchBranchStage)
	{
		const LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier&
			SearchPrefixStage = SearchBranchStage.SearchPrefixStage;
		CaptureBestPartialPlacements(Context);
		if (SearchPrefixStage.bSolvedAfterForcedPlacements)
		{
			FString AdjacencyAuditFailure;
			if (!ValidateSolvedPlacementAdjacency(Context, AdjacencyAuditFailure))
			{
				Context.Result.FailureReason = AdjacencyAuditFailure;
				if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
				{
					UE_LOG(LogTemp, Display,
						TEXT("[VerticalAccessCompleteAdjacencyFailure] region=%s reason=%s"),
						*Context.RegionDebugPath,
						*AdjacencyAuditFailure);
				}
				AddTraceEvent(Context, FString::Printf(
					TEXT("final-adjacency-audit-failed depth=%d placements=%d reason=%s"),
					SolveDepth,
					Context.Placements.Num(),
					*AdjacencyAuditFailure));
				MemoizeFailedState(Context);
				RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
				return false;
			}

			FString ReachabilityFailure;
			if (Context.bDeferFinalReachabilityAudit
				|| LayoutProfileSolverInternal::ValidateReachability(
					Context,
					ReachabilityFailure))
			{
				AddAcceptedPlacementTraceEvents(Context);
				AddTraceEvent(Context, FString::Printf(
					TEXT("accepted complete layout placements=%d attempts=%d memoHits=%d"),
					Context.Placements.Num(),
					Context.CandidateAttemptCount,
					Context.FailedStateMemoHits));
				return true;
			}

			Context.Result.FailureReason = ReachabilityFailure;
			AddTraceEvent(Context, FString::Printf(
				TEXT("reachability-failed depth=%d placements=%d reason=%s"),
				SolveDepth,
				Context.Placements.Num(),
				*ReachabilityFailure));
			MemoizeFailedState(Context);
			RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
			return false;
		}

		const FIntVector& Cell = SearchPrefixStage.SelectedCell;
		const ELayoutCellIntent Intent = SearchPrefixStage.Summary.SelectedIntent;
		TArray<FString> CandidateFailureReasons;
		AddTraceEvent(Context, FString::Printf(
			TEXT("select-cell depth=%d cell=%s intent=%s candidates=%d placements=%d attempts=%d"),
			SolveDepth,
			*Cell.ToString(),
			ToDebugString(Intent),
			SearchBranchStage.WorkItems.Num(),
			Context.Placements.Num(),
			Context.CandidateAttemptCount));
		if (Context.bIncludeTraceCandidateDetails && IsTracingEnabled(Context))
		{
			TArray<FString> CandidateNames;
			const int32 MaxCandidateNames =
				FMath::Min(SearchBranchStage.WorkItems.Num(), 12);
			CandidateNames.Reserve(MaxCandidateNames);
			for (int32 CandidateIndex = 0; CandidateIndex < MaxCandidateNames;
				++CandidateIndex)
			{
				const FSolveCandidate& Candidate =
					SearchBranchStage.WorkItems[CandidateIndex].Candidate;
				CandidateNames.Add(
					LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate)
						? BuildCandidateDebugName(Context, Candidate)
						: TEXT("<empty>"));
			}

			AddTraceEvent(Context, FString::Printf(
				TEXT("candidate-order cell=%s intent=%s firstCandidates=%s%s"),
				*Cell.ToString(),
				ToDebugString(Intent),
				CandidateNames.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(CandidateNames, TEXT(" | ")),
				SearchBranchStage.WorkItems.Num() > MaxCandidateNames
					? TEXT(" | ...")
					: TEXT("")));
		}
		if (SearchBranchStage.WorkItems.IsEmpty())
		{
			GetLegalCandidatesForCellIndexed(Context, Cell, &CandidateFailureReasons);
			if (CandidateFailureReasons.IsEmpty())
			{
				if (const TArray<FString>* InitialAdmissionFailures =
						Context.InitialDomainAdmissionFailuresByCell.Find(Cell))
				{
					CandidateFailureReasons = *InitialAdmissionFailures;
				}
			}
			Context.Result.FailureReason = FString::Printf(
				TEXT("No legal module candidates remain for intent %s(%d) at cell %s after CSP propagation. Cell context: %s. %s. Candidate failures: %s"),
				ToDebugString(Intent),
				static_cast<int32>(Intent),
				*Cell.ToString(),
				*BuildCellDebugString(Context, Cell),
				*BuildDomainDebugString(Context, Cell),
				CandidateFailureReasons.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(CandidateFailureReasons, TEXT(" | ")));
			AddTraceEvent(Context, FString::Printf(
				TEXT("domain-empty cell=%s reason=%s"),
				*Cell.ToString(),
				*Context.Result.FailureReason));
			MemoizeFailedState(Context);
			RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
			return false;
		}

		int32 ExactUniqueWorkItemIndex = INDEX_NONE;
		switch (
			TryReducePreparedSearchBranchStageToExactUniqueContinuation(
				Context,
				SolveDepth,
				SearchBranchStage,
				CandidateFailureReasons,
				ExactUniqueWorkItemIndex))
		{
		case EPreparedSearchBranchExactReductionOutcome::AbortSolve:
			RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
			return false;
		case EPreparedSearchBranchExactReductionOutcome::RejectedAllCandidates:
			return FinalizePreparedSearchBranchStageFailure(
				Context,
				SolveDepth,
				SearchBranchStage,
				CandidateFailureReasons);
		case EPreparedSearchBranchExactReductionOutcome::
			ReadyForUniqueCandidateContinuation:
		{
			const EPreparedSearchBranchOutcome BranchOutcome =
				TrySolvePreparedSearchBranchWorkItem(
					Context,
					SolveDepth,
					SearchBranchStage.WorkItems[ExactUniqueWorkItemIndex],
					CandidateFailureReasons);
			if (BranchOutcome == EPreparedSearchBranchOutcome::Solved)
			{
				return true;
			}
			if (BranchOutcome == EPreparedSearchBranchOutcome::AbortSolve)
			{
				RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
				return false;
			}
			return FinalizePreparedSearchBranchStageFailure(
				Context,
				SolveDepth,
				SearchBranchStage,
				CandidateFailureReasons);
		}
		case EPreparedSearchBranchExactReductionOutcome::ReadyForNormalBranchSolve:
		default:
			break;
		}

		for (const FPreparedSearchBranchWorkItem& BranchWorkItem :
			SearchBranchStage.WorkItems)
		{
			WaitOnCancellationCheckpointGateForTesting();
			if (HasExceededSolverTimeBudget(Context, SolveDepth))
			{
				RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
				return false;
			}
			const EPreparedSearchBranchOutcome BranchOutcome =
				TrySolvePreparedSearchBranchWorkItem(
					Context,
					SolveDepth,
					BranchWorkItem,
					CandidateFailureReasons);
			if (BranchOutcome == EPreparedSearchBranchOutcome::Solved)
			{
				return true;
			}
			if (BranchOutcome == EPreparedSearchBranchOutcome::AbortSolve)
			{
				RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
				return false;
			}
		}

		return FinalizePreparedSearchBranchStageFailure(
			Context,
			SolveDepth,
			SearchBranchStage,
			CandidateFailureReasons);
	}

	bool ContinueSolveFromPreparedSearchFrontier(
		FSolveContext& Context,
		const int32 SolveDepth,
		FPreparedSearchFrontier& SearchFrontier)
	{
		LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier
			SearchPrefixStage;
		SearchPrefixStage.ForcedCells = SearchFrontier.ForcedCells;
		SearchPrefixStage.SelectedCell = SearchFrontier.SelectedCell;
		SearchPrefixStage.SelectedCandidates = SearchFrontier.SelectedCandidates;
		SearchPrefixStage.bSolvedAfterForcedPlacements =
			SearchFrontier.bSolvedAfterForcedPlacements || AllCellsSolved(Context);
		SearchPrefixStage.Summary.PlacedCellCount = Context.Placements.Num();
		SearchPrefixStage.Summary.ForcedPlacementCellCount =
			SearchFrontier.ForcedCells.Num();
		SearchPrefixStage.Summary.bSolvedAfterForcedPlacements =
			SearchPrefixStage.bSolvedAfterForcedPlacements;
		SearchPrefixStage.Summary.SelectedCell = SearchFrontier.SelectedCell;
		SearchPrefixStage.Summary.SelectedDomainSize =
			SearchFrontier.SelectedCandidates.Num();
		if (const ELayoutCellIntent* SelectedIntent =
				Context.PlannedCellIntents.Find(SearchFrontier.SelectedCell))
		{
			SearchPrefixStage.Summary.SelectedIntent = *SelectedIntent;
		}
		LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier
			SearchBranchStage;
		PopulatePreparedSearchBranchStageCarrier(
			SearchPrefixStage,
			SearchBranchStage);
		return ContinueSolveFromPreparedSearchBranchStage(
			Context,
			SolveDepth,
			SearchBranchStage);
	}

	bool SolveRecursive(FSolveContext& Context, const int32 SolveDepth)
	{
		if (HasExceededSolverTimeBudget(Context, SolveDepth))
		{
			return false;
		}

		if (Context.FailedStateHashes.Contains(BuildFailedStateHash(Context)))
		{
			++Context.FailedStateMemoHits;
			AddTraceEvent(Context, FString::Printf(
				TEXT("memo-hit depth=%d placements=%d attempts=%d"),
				SolveDepth,
				Context.Placements.Num(),
				Context.CandidateAttemptCount));
			return false;
		}

		if (Context.CandidateAttemptCount >= Context.MaxCandidateAttempts)
		{
			if (Context.Result.FailureReason.IsEmpty())
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Layout solve exceeded MaxSolverCandidateAttempts=%d after placing %d of %d planned cells. Reduce footprint/level count, add more restrictive face rules, or raise the profile's solver budget."),
					Context.MaxCandidateAttempts,
					SolveDepth,
					Context.SolveOrder.Num());
			}
			return false;
		}

		FPreparedSearchFrontier SearchFrontier;
		if (!TryPrepareSearchFrontier(Context, SolveDepth, SearchFrontier))
		{
			MemoizeFailedState(Context);
			RollbackForcedPlacements(Context, SearchFrontier.ForcedCells);
			return false;
		}
		return ContinueSolveFromPreparedSearchFrontier(
			Context,
			SolveDepth,
			SearchFrontier);
	}

	void ApplyTraceSettings(FSolveContext& Context, const FLayoutSolverExecutionSettings& ExecutionSettings)
	{
		Context.TraceMode = ExecutionSettings.TraceMode;
		Context.MaxTraceEvents = Context.TraceMode == ELayoutSolverTraceMode::Disabled
			? 0
			: FMath::Max(1, ExecutionSettings.MaxTraceEvents);
		Context.bIncludeTraceCandidateDetails = ExecutionSettings.bIncludeTraceCandidateDetails;
	}

	void PublishTraceIfNeeded(FSolveContext& Context)
	{
		const bool bShouldPublish = Context.TraceMode == ELayoutSolverTraceMode::Always
			|| (Context.TraceMode == ELayoutSolverTraceMode::OnFailure && !Context.Result.bSucceeded);
		if (!bShouldPublish)
		{
			return;
		}

		FLayoutValidationMessage& Summary = Context.Result.Messages.AddDefaulted_GetRef();
		Summary.Severity = ELayoutValidationSeverity::Warning;
		Summary.Message = FString::Printf(
			TEXT("Solver trace summary: succeeded=%s attempts=%d placements=%d failedStates=%d memoHits=%d traceEvents=%d overwrittenEvents=%d."),
			Context.Result.bSucceeded ? TEXT("true") : TEXT("false"),
			Context.CandidateAttemptCount,
			Context.Placements.Num(),
			Context.FailedStateHashes.Num(),
			Context.FailedStateMemoHits,
			Context.TraceEvents.Num(),
			Context.TraceEventsDropped);

		const auto PublishEvent = [&Context](const FSolverTraceEvent& Event)
		{
			FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
			Message.Severity = ELayoutValidationSeverity::Warning;
			Message.Message = FString::Printf(TEXT("Solver trace: %s"), *Event.Message);
		};

		if (Context.bTraceWrapped)
		{
			for (int32 EventIndex = Context.TraceNextWriteIndex; EventIndex < Context.TraceEvents.Num(); ++EventIndex)
			{
				PublishEvent(Context.TraceEvents[EventIndex]);
			}

			for (int32 EventIndex = 0; EventIndex < Context.TraceNextWriteIndex; ++EventIndex)
			{
				PublishEvent(Context.TraceEvents[EventIndex]);
			}
			return;
		}

		for (const FSolverTraceEvent& Event : Context.TraceEvents)
		{
			PublishEvent(Event);
		}
	}

	void SortResultPlacements(FLayoutSolveResult& Result);
	void ApplySolveSettings(
		FSolveContext& Context,
		const ULayoutProfileAsset* Profile,
		const FLayoutProfileSolveSnapshot* ProfileSnapshot,
		const FLayoutSolverExecutionSettings* ExecutionSettings,
		const int32 Seed,
		const FLayoutSolveResult& BaseResult,
		const bool bUseIndexedCandidateFiltering,
		const FLayoutModuleCatalog* ModuleCatalog);

	struct FSparseResidualCandidateCell
	{
		FIntVector Cell = FIntVector::ZeroValue;
		ELayoutCellIntent Intent = ELayoutCellIntent::Interior;
		int32 ModuleLevelIndex = INDEX_NONE;
		ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Interior;
		int32 TerrainStageIndex = INDEX_NONE;
		FName SourceSparsePlacementRuleId;
		uint32 TieBreakHash = 0;
	};

	bool DoesCellMatchPlacementZone(
		const FLayoutSolveResult& SolveResult,
		const FLayoutResidualCellRecord& ResidualCell,
		const ELayoutPlacementZone PlacementZone)
	{
		if (ResidualCell.Source == ELayoutResidualCellSource::TerrainBackedRule)
		{
			if (PlacementZone == ELayoutPlacementZone::Core)
			{
				const float CenterX = static_cast<float>(SolveResult.FootprintSize.X - 1) * 0.5f;
				const float CenterY = static_cast<float>(SolveResult.FootprintSize.Y - 1) * 0.5f;
				return ResidualCell.ModuleLevelIndex == 0
					&& FMath::Abs(static_cast<float>(ResidualCell.Cell.X) - CenterX) <= 0.5f
					&& FMath::Abs(static_cast<float>(ResidualCell.Cell.Y) - CenterY) <= 0.5f;
			}
			return PlacementZone == ELayoutPlacementZone::Any
				|| PlacementZone == ResidualCell.PlacementZone
				|| (PlacementZone == ELayoutPlacementZone::Perimeter
					&& (ResidualCell.PlacementZone == ELayoutPlacementZone::Edge
						|| ResidualCell.PlacementZone == ELayoutPlacementZone::Corner));
		}

		const FIntVector& Cell = ResidualCell.Cell;
		switch (PlacementZone)
		{
		case ELayoutPlacementZone::Any:
			return true;
		case ELayoutPlacementZone::Perimeter:
			return IsBoundaryCell(Cell, SolveResult.FootprintSize);
		case ELayoutPlacementZone::Edge:
			return IsBoundaryCell(Cell, SolveResult.FootprintSize) && !IsCornerBoundaryCell(Cell, SolveResult.FootprintSize);
		case ELayoutPlacementZone::Corner:
			return IsCornerBoundaryCell(Cell, SolveResult.FootprintSize);
		case ELayoutPlacementZone::Interior:
			return !IsBoundaryCell(Cell, SolveResult.FootprintSize);
		case ELayoutPlacementZone::Core:
		{
			if (Cell.Z != 0)
			{
				return false;
			}

			const float CenterX = static_cast<float>(SolveResult.FootprintSize.X - 1) * 0.5f;
			const float CenterY = static_cast<float>(SolveResult.FootprintSize.Y - 1) * 0.5f;
			return FMath::Abs(static_cast<float>(Cell.X) - CenterX) <= 0.5f
				&& FMath::Abs(static_cast<float>(Cell.Y) - CenterY) <= 0.5f;
		}
		default:
			return true;
		}
	}

	FString FormatZoneFeatureTags(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		if (SortedTags.IsEmpty())
		{
			return TEXT("<none>");
		}

		TArray<FString> TagStrings;
		TagStrings.Reserve(SortedTags.Num());
		for (const FGameplayTag& Tag : SortedTags)
		{
			TagStrings.Add(Tag.ToString());
		}

		return FString::Printf(TEXT("[%s]"), *FString::Join(TagStrings, TEXT(", ")));
	}

	FString DescribeZoneFeatureCountExpectation(const FLayoutZoneFeatureRequirement& Requirement)
	{
		if (Requirement.MaxCount > 0)
		{
			if (Requirement.MinCount == Requirement.MaxCount)
			{
				return FString::Printf(TEXT("exactly %d"), Requirement.MinCount);
			}

			return FString::Printf(TEXT("between %d and %d"), Requirement.MinCount, Requirement.MaxCount);
		}

		if (Requirement.MinCount > 0)
		{
			return FString::Printf(TEXT("at least %d"), Requirement.MinCount);
		}

		return TEXT("any count");
	}

	const FLayoutRegionContentEntrySolveSnapshot* FindContentSetEntrySnapshotById(
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FName EntryId)
	{
		return ContentSetSnapshot.Entries.FindByPredicate([EntryId](const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
		{
			return EntrySnapshot.EntryId == EntryId;
		});
	}

	TArray<FIntVector> BuildPlacementWorldOccupiedCells(
		const FLayoutPlacedModule& Placement,
		const FLayoutModuleSolveSnapshot* ModuleSnapshot)
	{
		return LayoutPlacementOccupancy::BuildWorldOccupiedCells(Placement, ModuleSnapshot);
	}

	TSet<FIntVector> BuildSolveResultOccupiedWorldCells(const FLayoutSolveResult& SolveResult)
	{
		TSet<FIntVector> OccupiedCells;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			const TArray<FIntVector> WorldCells = BuildPlacementWorldOccupiedCells(Placement, nullptr);
			for (const FIntVector& WorldCell : WorldCells)
			{
				OccupiedCells.Add(WorldCell);
			}
		}

		return OccupiedCells;
	}

	struct FZoneFeatureRequirementCountDetails
	{
		int32 MatchCount = 0;
		TArray<FString> MatchingPlacements;
	};

	FString BuildZoneFeatureRequirementOutcomeMessage(
		const FString& RegionDebugPath,
		const FLayoutZoneFeatureRequirement& Requirement,
		const FZoneFeatureRequirementCountDetails& CountDetails,
		const TArray<FString>& AvailableProviders,
		const FString& Problem)
	{
		const FString ZoneName = StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(Requirement.Zone));
		const FString MatchModeName = StaticEnum<ELayoutZoneFeatureMatchMode>()->GetNameStringByValue(static_cast<int64>(Requirement.MatchMode));
		const FString MatchingPlacementsText = CountDetails.MatchingPlacements.IsEmpty()
			? TEXT("<none>")
			: FString::Join(CountDetails.MatchingPlacements, TEXT(", "));
		const FString ProviderText = AvailableProviders.IsEmpty()
			? TEXT("<none>")
			: FString::Join(AvailableProviders, TEXT(", "));

		return FString::Printf(
			TEXT("Zone feature requirement '%s' was not satisfied.\n")
			TEXT("Region: %s\n")
			TEXT("Zone: %s\n")
			TEXT("Match mode: %s\n")
			TEXT("Required features: %s\n")
			TEXT("Expected count: %s\n")
			TEXT("Observed count: %d\n")
			TEXT("Matching placements: %s\n")
			TEXT("Available providers: %s\n")
			TEXT("Problem: %s"),
			*Requirement.RequirementId.ToString(),
			*RegionDebugPath,
			*ZoneName,
			*MatchModeName,
			*FormatZoneFeatureTags(Requirement.RequiredFeatures),
			*DescribeZoneFeatureCountExpectation(Requirement),
			CountDetails.MatchCount,
			*MatchingPlacementsText,
			*ProviderText,
			*Problem);
	}

	bool EvaluateLeafZoneFeatureRequirementsForResult(
		const FLayoutRegionSolveRequest& Request,
		FLayoutSolveResult& SolveResult,
		FString& OutFailureReason)
	{
		if (Request.ProfileSnapshot.ZoneFeatureRequirements.IsEmpty()
			|| Request.ContentSetSnapshot.Entries.IsEmpty()
			|| Request.ContentSetSnapshot.Entries.ContainsByPredicate([](const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
				{
					return EntrySnapshot.ContentKind == ELayoutRegionContentKind::ChildRegion;
				}))
		{
			return true;
		}

		TArray<const FLayoutZoneFeatureRequirement*> SortedRequirements;
		SortedRequirements.Reserve(Request.ProfileSnapshot.ZoneFeatureRequirements.Num());
		for (const FLayoutZoneFeatureRequirement& Requirement : Request.ProfileSnapshot.ZoneFeatureRequirements)
		{
			SortedRequirements.Add(&Requirement);
		}

		SortedRequirements.Sort([](const FLayoutZoneFeatureRequirement& Left, const FLayoutZoneFeatureRequirement& Right)
		{
			return Left.RequirementId.LexicalLess(Right.RequirementId);
		});

		for (const FLayoutZoneFeatureRequirement* RequirementPtr : SortedRequirements)
		{
			check(RequirementPtr != nullptr);
			const FLayoutZoneFeatureRequirement& Requirement = *RequirementPtr;
			if (Requirement.RequiredFeatures.IsEmpty())
			{
				continue;
			}

			TArray<FString> AvailableProviders;
			for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : Request.ContentSetSnapshot.Entries)
			{
				if (LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchRequirement(EntrySnapshot.ProvidedZoneFeatures, Requirement))
				{
					AvailableProviders.Add(EntrySnapshot.EntryId.IsNone() ? TEXT("<none>") : EntrySnapshot.EntryId.ToString());
				}
			}
			AvailableProviders.Sort();

			FZoneFeatureRequirementCountDetails CountDetails;
			TSet<FLayoutId> CountedCommitmentIds;
			const auto CountCommitments = [&](const TArray<FLayoutZoneFeatureProviderCommitment>& Commitments)
			{
				for (const FLayoutZoneFeatureProviderCommitment& Commitment : Commitments)
				{
					if (Commitment.RequirementId != Requirement.RequirementId
						|| CountedCommitmentIds.Contains(Commitment.ProviderCommitmentId))
					{
						continue;
					}
					CountedCommitmentIds.Add(Commitment.ProviderCommitmentId);
					++CountDetails.MatchCount;
					CountDetails.MatchingPlacements.Add(FString::Printf(
						TEXT("%s@(%d,%d,%d)[%s]"),
						*Commitment.SourceContentEntryId.ToString(),
						Commitment.Cell.X,
						Commitment.Cell.Y,
						Commitment.Cell.Z,
						*Commitment.ProviderCommitmentId.ToString()));
				}
			};
			CountCommitments(SolveResult.ZoneFeatureProviderCommitments);
			CountCommitments(Request.PrecommittedZoneFeatureProviderCommitments);

			const bool bBelowMinimum = CountDetails.MatchCount < Requirement.MinCount;
			const bool bAboveMaximum = Requirement.MaxCount > 0 && CountDetails.MatchCount > Requirement.MaxCount;
			if (!bBelowMinimum && !bAboveMaximum)
			{
				continue;
			}

			const FString Problem = bBelowMinimum
				? FString::Printf(TEXT("Only %d matching placement(s) were found, but the requirement needs %d."), CountDetails.MatchCount, Requirement.MinCount)
				: FString::Printf(TEXT("Found %d matching placement(s), which exceeds the allowed maximum of %d."), CountDetails.MatchCount, Requirement.MaxCount);
			const FString Message = BuildZoneFeatureRequirementOutcomeMessage(
				Request.RegionDebugPath,
				Requirement,
				CountDetails,
				AvailableProviders,
				Problem);

			++SolveResult.PropagationStats.HardZoneFeatureLateAuditFailureCount;
			FLayoutValidationMessage& Error = SolveResult.Messages.AddDefaulted_GetRef();
			Error.Severity = ELayoutValidationSeverity::Error;
			Error.Message = Message;
			OutFailureReason = Message;
			return false;
		}

		return true;
	}

	int32 ResolveSparsePlacementCount(
		const FLayoutSparsePlacementRuleSolveSnapshot& Rule,
		const int32 Capacity,
		const int32 Seed)
	{
		if (Rule.RuleKind == ELayoutSparsePlacementRuleKind::FillAvailable)
		{
			return Capacity;
		}

		const uint32 SelectionSeed = HashCombineFast(
			static_cast<uint32>(Seed),
			GetTypeHash(Rule.RuleId));
		switch (Rule.RuleKind)
		{
		case ELayoutSparsePlacementRuleKind::Exact:
			return FMath::Clamp(Rule.Count, 0, Capacity);
		case ELayoutSparsePlacementRuleKind::Range:
		{
			FRandomStream Random(SelectionSeed);
			return FMath::Clamp(Random.RandRange(Rule.MinCount, Rule.MaxCount), 0, Capacity);
		}
		case ELayoutSparsePlacementRuleKind::PreserveTerrain:
		case ELayoutSparsePlacementRuleKind::FillAvailable:
		default:
			return 0;
		}
	}

	int32 ResolveSparsePlacementMinimum(const FLayoutSparsePlacementRuleSolveSnapshot& Rule)
	{
		switch (Rule.RuleKind)
		{
		case ELayoutSparsePlacementRuleKind::Exact:
			return Rule.Count;
		case ELayoutSparsePlacementRuleKind::Range:
			return Rule.MinCount;
		case ELayoutSparsePlacementRuleKind::PreserveTerrain:
		case ELayoutSparsePlacementRuleKind::FillAvailable:
		default:
			return 0;
		}
	}

	TArray<FSparseResidualCandidateCell> BuildSparseResidualCandidateCells(
		const FLayoutSparsePlacementRuleSolveSnapshot& Rule,
		const FLayoutSolveResult& SolveResult,
		const int32 LevelCount,
		const int32 Seed)
	{
		TArray<FSparseResidualCandidateCell> CandidateCells;
		const TSet<FIntVector> OccupiedCells = BuildSolveResultOccupiedWorldCells(SolveResult);
		for (const FLayoutResidualCellRecord& ResidualCell : SolveResult.ResidualUnoccupiedCells)
		{
			if (OccupiedCells.Contains(ResidualCell.Cell))
			{
				continue;
			}

			if (!DoesCellMatchPlacementZone(SolveResult, ResidualCell, Rule.PlacementZone)
				|| !DoesCellMatchAuthoredLevelScope(
					FIntVector(ResidualCell.Cell.X, ResidualCell.Cell.Y,
						ResidualCell.ModuleLevelIndex != INDEX_NONE ? ResidualCell.ModuleLevelIndex : ResidualCell.Cell.Z),
					LevelCount,
					Rule.LevelPlacementPolicy,
					Rule.SpecificLevel)
				|| (Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain
					&& (ResidualCell.Source != ELayoutResidualCellSource::TerrainBackedRule
						|| ResidualCell.SourceSparsePlacementRuleId != Rule.RuleId)))
			{
				continue;
			}

			FSparseResidualCandidateCell& CandidateCell = CandidateCells.AddDefaulted_GetRef();
			CandidateCell.Cell = ResidualCell.Cell;
			CandidateCell.Intent = ResidualCell.Intent;
			CandidateCell.ModuleLevelIndex = ResidualCell.ModuleLevelIndex;
			CandidateCell.PlacementZone = ResidualCell.PlacementZone;
			CandidateCell.TerrainStageIndex = ResidualCell.TerrainStageIndex;
			CandidateCell.SourceSparsePlacementRuleId = ResidualCell.SourceSparsePlacementRuleId;
			CandidateCell.TieBreakHash = HashCombineFast(
				HashCombineFast(static_cast<uint32>(Seed), GetTypeHash(Rule.RuleId)),
				GetTypeHash(ResidualCell.Cell));
		}

		CandidateCells.Sort([](const FSparseResidualCandidateCell& Left, const FSparseResidualCandidateCell& Right)
		{
			if (Left.TieBreakHash != Right.TieBreakHash)
			{
				return Left.TieBreakHash < Right.TieBreakHash;
			}

			if (Left.Cell.Z != Right.Cell.Z)
			{
				return Left.Cell.Z < Right.Cell.Z;
			}

			if (Left.Cell.Y != Right.Cell.Y)
			{
				return Left.Cell.Y < Right.Cell.Y;
			}

			return Left.Cell.X < Right.Cell.X;
		});

		return CandidateCells;
	}

	bool IsFarEnoughFromAcceptedSparseCells(
		const TArray<FLayoutSparsePlacementCommitment>& ExistingCommitments,
		const FName RuleId,
		const int32 MinSpacingCells,
		const FIntVector& Cell)
	{
		if (MinSpacingCells <= 0)
		{
			return true;
		}

		for (const FLayoutSparsePlacementCommitment& Commitment : ExistingCommitments)
		{
			if (Commitment.RuleId != RuleId)
			{
				continue;
			}

			const int32 ManhattanDistance = FMath::Abs(Commitment.Cell.X - Cell.X)
				+ FMath::Abs(Commitment.Cell.Y - Cell.Y)
				+ FMath::Abs(Commitment.Cell.Z - Cell.Z);
			if (ManhattanDistance < MinSpacingCells)
			{
				return false;
			}
		}

		return true;
	}

	void SeedSparseFixedNeighborPlacements(
		const FLayoutSolveResult& SolveResult,
		FSolveContext& Context)
	{
		Context.FixedNeighborPlacements.Reset();
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			TArray<FIntVector> OccupiedLocalCells = LayoutPlacementOccupancy::ResolveOccupiedLocalCells(Placement, nullptr);
			if (OccupiedLocalCells.IsEmpty())
			{
				OccupiedLocalCells = {FIntVector::ZeroValue};
			}

			const FIntVector OccupiedBoundsCells = LayoutPlacementOccupancy::BuildOccupiedLocalCellBounds(OccupiedLocalCells);

			for (const FIntVector& LocalCell : OccupiedLocalCells)
			{
				const FIntVector WorldCell = LayoutPlacementOccupancy::ProjectOccupiedLocalCellToWorld(
					Placement,
					LocalCell,
					OccupiedBoundsCells);
				FSolveContext::FSolvePlacement& FixedPlacement = Context.FixedNeighborPlacements.Add(WorldCell);
				FixedPlacement.YawRotationSteps = Placement.YawRotationSteps;
				FixedPlacement.VariantIndex = INDEX_NONE;
				FixedPlacement.ModuleSnapshotIndex = Placement.ModuleSnapshotIndex;
				FixedPlacement.ModuleSnapshotId = Placement.ModuleSnapshotId;
				FixedPlacement.bEmpty = false;
				FixedPlacement.BundleRootCell = Placement.Cell;
				FixedPlacement.LocalBundleCell = LocalCell;
				FixedPlacement.bBundleRoot = WorldCell == Placement.Cell;
			}
		}
	}

	FName GetSparseCandidateSourceContentEntryId(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindCandidateVariant(Context, Candidate))
		{
			return Variant->SourceContentEntryId;
		}

		return NAME_None;
	}

	/** Selects one weighted content entry, then one legal yaw, without multiplying weight by yaw count. */
	bool TryChooseWeightedSparseCandidate(
		const FSolveContext& Context,
		const TArray<FSolveCandidate>& LegalCandidates,
		const int32 Seed,
		const FName RuleId,
		const FIntVector& Cell,
		FSolveCandidate& OutCandidate)
	{
		struct FEntryCandidates
		{
			FName EntryId;
			int32 Weight = 1;
			TArray<FSolveCandidate> Candidates;
		};

		TArray<FEntryCandidates> Groups;
		for (const FSolveCandidate& Candidate : LegalCandidates)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate)
				|| !Context.Variants.IsValidIndex(Candidate.VariantIndex))
			{
				continue;
			}
			const FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[Candidate.VariantIndex];
			FEntryCandidates* Group = Groups.FindByPredicate([&Variant](const FEntryCandidates& Existing)
			{
				return Existing.EntryId == Variant.SourceContentEntryId;
			});
			if (Group == nullptr)
			{
				FEntryCandidates& Added = Groups.AddDefaulted_GetRef();
				Added.EntryId = Variant.SourceContentEntryId;
				Added.Weight = FMath::Max(1, Variant.Weight);
				Group = &Added;
			}
			Group->Candidates.Add(Candidate);
		}
		if (Groups.IsEmpty())
		{
			return false;
		}

		Groups.Sort([](const FEntryCandidates& Left, const FEntryCandidates& Right)
		{
			return Left.EntryId.LexicalLess(Right.EntryId);
		});
		int64 TotalWeight = 0;
		for (const FEntryCandidates& Group : Groups)
		{
			TotalWeight += Group.Weight;
		}
		const uint32 CellSeed = HashCombineFast(
			HashCombineFast(static_cast<uint32>(Seed), GetTypeHash(RuleId)),
			GetTypeHash(Cell));
		const uint64 SelectionBits = (static_cast<uint64>(CellSeed) << 32)
			| HashCombineFast(CellSeed, GetTypeHash(RuleId));
		int64 Selection = static_cast<int64>(SelectionBits % static_cast<uint64>(TotalWeight));
		FEntryCandidates* ChosenGroup = &Groups.Last();
		for (FEntryCandidates& Group : Groups)
		{
			if (Selection < Group.Weight)
			{
				ChosenGroup = &Group;
				break;
			}
			Selection -= Group.Weight;
		}
		ChosenGroup->Candidates.Sort([](const FSolveCandidate& Left, const FSolveCandidate& Right)
		{
			return Left.YawRotationSteps != Right.YawRotationSteps
				? Left.YawRotationSteps < Right.YawRotationSteps
				: Left.ModuleSnapshotId.LexicalLess(Right.ModuleSnapshotId);
		});
		OutCandidate = ChosenGroup->Candidates[HashCombineFast(CellSeed, GetTypeHash(ChosenGroup->EntryId)) % ChosenGroup->Candidates.Num()];
		return true;
	}

	bool ApplySparsePlacementRulesToResultInternal(
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const FString& RegionDebugPath,
		const int32 Seed,
		FLayoutSolveResult& SolveResult,
		FString& OutFailureReason)
	{
		SolveResult.SparsePlacementStats = FLayoutSparsePlacementStats();
		if (ProfileSnapshot.SparsePlacementRules.IsEmpty())
		{
			return true;
		}

		const double SparseStartTimeSeconds = FPlatformTime::Seconds();
		for (const FLayoutSparsePlacementRuleSolveSnapshot& Rule : ProfileSnapshot.SparsePlacementRules)
		{
			if (Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain)
			{
				continue;
			}
			++SolveResult.SparsePlacementStats.RuleCount;
			TArray<FSparseResidualCandidateCell> CandidateCells = BuildSparseResidualCandidateCells(
				Rule,
				SolveResult,
				ProfileSnapshot.LevelCount,
				Seed);
			SolveResult.SparsePlacementStats.EligibleCellCount += CandidateCells.Num();
			const int32 RequestedPlacementCount = ResolveSparsePlacementCount(Rule, CandidateCells.Num(), Seed);
			const int32 MinimumPlacementCount = ResolveSparsePlacementMinimum(Rule);
			if (RequestedPlacementCount <= 0)
			{
				if (MinimumPlacementCount > 0)
				{
					FLayoutValidationMessage& Warning = SolveResult.Messages.AddDefaulted_GetRef();
					Warning.Severity = ELayoutValidationSeverity::Warning;
					Warning.Message = FString::Printf(
						TEXT("Sparse placement rule '%s' for region '%s' requested at least %d placements but found no eligible residual capacity; structural proof remains accepted."),
						*Rule.RuleId.ToString(),
						*RegionDebugPath,
						MinimumPlacementCount);
				}
				continue;
			}

			FLayoutProfileSolveSnapshot SparseProfileSnapshot = ProfileSnapshot;
			SparseProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
			SparseProfileSnapshot.SparsePlacementRules.Reset();
			FLayoutSolverExecutionSettings SparseExecutionSettings = FLayoutSolverExecutionSettings();
			SparseExecutionSettings.TraceMode = ELayoutSolverTraceMode::Disabled;

			FSolveContext SparseContext;
			FLayoutSolveResult SparseBaseResult;
			SparseBaseResult.Seed = Seed;
			SparseBaseResult.FootprintSize = SolveResult.FootprintSize;
				ApplySolveSettings(
					SparseContext,
					nullptr,
					&SparseProfileSnapshot,
					&SparseExecutionSettings,
					Seed,
					SparseBaseResult,
					true,
					&Rule.ModuleCatalog);
			SeedSparseFixedNeighborPlacements(SolveResult, SparseContext);

			TArray<FLayoutPlannedCell> PlannedResidualCells;
			PlannedResidualCells.Reserve(SolveResult.ResidualUnoccupiedCells.Num());
			for (const FLayoutResidualCellRecord& ResidualCell : SolveResult.ResidualUnoccupiedCells)
			{
				FLayoutPlannedCell& PlannedCell = PlannedResidualCells.AddDefaulted_GetRef();
				PlannedCell.Cell = ResidualCell.Cell;
				PlannedCell.Intent = ResidualCell.Intent;
				PlannedCell.ModuleLevelIndex = ResidualCell.ModuleLevelIndex;
				PlannedCell.PlacementZone = ResidualCell.PlacementZone;
			}

			SparseContext.bTreatTerrainResidualsAsSettledEmpty = true;
			BuildOverridePlan(SparseContext, SolveResult.FootprintSize, PlannedResidualCells);
			for (int32 ResidualIndex = 0; ResidualIndex < SolveResult.ResidualUnoccupiedCells.Num(); ++ResidualIndex)
			{
				const FLayoutResidualCellRecord& ResidualCell = SolveResult.ResidualUnoccupiedCells[ResidualIndex];
				SparseContext.TerrainResidualRuleIdByCell.FindOrAdd(ResidualCell.Cell) = ResidualCell.SourceSparsePlacementRuleId;
				SparseContext.ModuleLevelByCell.FindOrAdd(ResidualCell.Cell) = ResidualCell.ModuleLevelIndex != INDEX_NONE
					? ResidualCell.ModuleLevelIndex
					: ResidualCell.Cell.Z;
				SparseContext.PlacementZonesByCell.FindOrAdd(ResidualCell.Cell) = ResidualCell.PlacementZone;
				if (FLayoutPlannedCell* PlannedResidual =
						SparseContext.Result.PlannedCells.FindByPredicate(
							[&ResidualCell](const FLayoutPlannedCell& PlannedCell)
							{
								return PlannedCell.Cell == ResidualCell.Cell;
							}))
				{
					PlannedResidual->ModuleLevelIndex = ResidualCell.ModuleLevelIndex;
					PlannedResidual->PlacementZone = ResidualCell.PlacementZone;
					SparseContext.OwningTopologyCellsByPhysicalCell.FindOrAdd(
						ResidualCell.Cell) = *PlannedResidual;
				}
			}
			RefreshCompiledFaceInterfaces(SparseContext);
			BuildOrientedVariants(SparseContext);
			BuildInitialDomains(SparseContext);
			InitializeIndexedCandidateUniverse(SparseContext);
			BuildIndexedCompatibilityRows(SparseContext);

			TSet<FIntVector> AvailableResidualCells;
			for (const FLayoutResidualCellRecord& ResidualCell : SolveResult.ResidualUnoccupiedCells)
			{
				AvailableResidualCells.Add(ResidualCell.Cell);
			}
			TArray<FIntVector> ConsumedCells;
			FString FirstSparseCandidateFailure;
			int32 AcceptedPlacementCount = 0;
			for (const FSparseResidualCandidateCell& CandidateCell : CandidateCells)
			{
				if (AcceptedPlacementCount >= RequestedPlacementCount)
				{
					break;
				}

				if (!IsFarEnoughFromAcceptedSparseCells(
					SolveResult.SparsePlacementCommitments,
					Rule.RuleId,
					Rule.MinSpacingCells,
					CandidateCell.Cell))
				{
					continue;
				}

				++SolveResult.SparsePlacementStats.LegalCandidateCheckCount;
				TArray<FString> CandidateFailures;
				TArray<FSolveCandidate> LegalCandidates = GetLegalCandidatesForCellIndexed(
					SparseContext,
					CandidateCell.Cell,
					&CandidateFailures,
					nullptr,
					false);
				if (FirstSparseCandidateFailure.IsEmpty() && !CandidateFailures.IsEmpty())
				{
					FirstSparseCandidateFailure = CandidateFailures[0];
				}
				if (FirstSparseCandidateFailure.IsEmpty()
					&& !LegalCandidates.ContainsByPredicate([](const FSolveCandidate& Candidate)
					{
						return LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate);
					}))
				{
					if (const TArray<FString>* AdmissionFailures = SparseContext.InitialDomainAdmissionFailuresByCell.Find(CandidateCell.Cell);
						AdmissionFailures != nullptr && !AdmissionFailures->IsEmpty())
					{
						FirstSparseCandidateFailure = (*AdmissionFailures)[0];
					}
				}
				FSolveCandidate ChosenCandidate;
				if (!TryChooseWeightedSparseCandidate(
					SparseContext,
					LegalCandidates,
					Seed,
					Rule.RuleId,
					CandidateCell.Cell,
					ChosenCandidate))
				{
					continue;
				}

				const TArray<FIntVector> OccupiedBundleCells = BuildCandidateWorldOccupiedCells(
					SparseContext,
					CandidateCell.Cell,
					ChosenCandidate);
				SolveResult.SparsePlacementStats.OccupiedCellCheckCount += OccupiedBundleCells.Num();
				if (OccupiedBundleCells.IsEmpty()
					|| OccupiedBundleCells.ContainsByPredicate([&AvailableResidualCells](const FIntVector& OccupiedCell)
					{
						return !AvailableResidualCells.Contains(OccupiedCell);
					}))
				{
					if (FirstSparseCandidateFailure.IsEmpty())
					{
						FirstSparseCandidateFailure = OccupiedBundleCells.IsEmpty()
							? TEXT("Selected sparse candidate has no occupied bundle cells.")
							: TEXT("Selected sparse bundle crosses outside remaining residual authority.");
					}
					continue;
				}

				CommitOccupiedCandidateBundle(SparseContext, CandidateCell.Cell, ChosenCandidate);

				FLayoutSparsePlacementCommitment& Commitment = SolveResult.SparsePlacementCommitments.AddDefaulted_GetRef();
				Commitment.RuleId = Rule.RuleId;
				Commitment.SourceContentEntryId = GetSparseCandidateSourceContentEntryId(SparseContext, ChosenCandidate);
				Commitment.SourceRegionDebugPath = RegionDebugPath;
				Commitment.Cell = CandidateCell.Cell;
				Commitment.Intent = CandidateCell.Intent;
				Commitment.Module = nullptr;
				Commitment.YawRotationSteps = ChosenCandidate.YawRotationSteps;
				Commitment.OccupiedCells = OccupiedBundleCells;
				++AcceptedPlacementCount;
				++SolveResult.SparsePlacementStats.AcceptedPlacementCount;

				FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
				Placement.Cell = CandidateCell.Cell;
				Placement.Intent = CandidateCell.Intent;
				Placement.Module = nullptr;
				Placement.YawRotationSteps = ChosenCandidate.YawRotationSteps;
				Placement.SourceContentEntryId = Commitment.SourceContentEntryId;
				Placement.ModuleSnapshotIndex = ChosenCandidate.ModuleSnapshotIndex;
				Placement.ModuleSnapshotId = ChosenCandidate.ModuleSnapshotId;
				PopulatePlacedModuleBundleMetadata(SparseContext, ChosenCandidate.VariantIndex, Placement);

				for (const FIntVector& OccupiedCell : OccupiedBundleCells)
				{
					AvailableResidualCells.Remove(OccupiedCell);
					ConsumedCells.Add(OccupiedCell);
				}
				RefreshCompiledFaceInterfaces(SparseContext);
			}

			if (AcceptedPlacementCount < MinimumPlacementCount)
			{
				FLayoutValidationMessage& Warning = SolveResult.Messages.AddDefaulted_GetRef();
				Warning.Severity = ELayoutValidationSeverity::Warning;
				Warning.Message = FString::Printf(
					TEXT("Sparse placement rule '%s' for region '%s' requested at least %d placements but found less legal capacity; structural proof remains accepted. First rejection: %s"),
					*Rule.RuleId.ToString(),
					*RegionDebugPath,
					MinimumPlacementCount,
					FirstSparseCandidateFailure.IsEmpty() ? TEXT("<none>") : *FirstSparseCandidateFailure);
			}

			if (!ConsumedCells.IsEmpty())
			{
				SolveResult.ResidualUnoccupiedCells.RemoveAll([&ConsumedCells](const FLayoutResidualCellRecord& ResidualCell)
				{
					return ConsumedCells.Contains(ResidualCell.Cell);
				});
			}
		}

		SortResultPlacements(SolveResult);
		SolveResult.SparsePlacementStats.DurationSeconds = FPlatformTime::Seconds() - SparseStartTimeSeconds;
		return true;
	}

	void ApplySolveSettings(
		FSolveContext& Context,
		const ULayoutProfileAsset* Profile,
		const FLayoutProfileSolveSnapshot* ProfileSnapshot,
		const FLayoutSolverExecutionSettings* ExecutionSettings,
		const int32 Seed,
		const FLayoutSolveResult& BaseResult,
		const bool bUseIndexedCandidateFiltering = true,
		const FLayoutModuleCatalog* ModuleCatalog = nullptr)
	{
		Context.Profile = Profile;
		Context.ProfileSnapshot = ProfileSnapshot != nullptr
			? *ProfileSnapshot
			: FLayoutProfileSolver::BuildProfileSnapshot(Profile);
		Context.ModuleSnapshots.Reset();
		if (ModuleCatalog != nullptr)
		{
			Context.ModuleSnapshots = ModuleCatalog->Modules;
		}
		Context.Seed = Seed;
		const FLayoutSolverExecutionSettings EffectiveExecutionSettings = ExecutionSettings != nullptr
			? *ExecutionSettings
			: FLayoutSolverExecutionSettings();
		Context.MaxCandidateAttempts = FMath::Max(1, EffectiveExecutionSettings.MaxCandidateAttempts);
		Context.SolveStartTimeSeconds = FPlatformTime::Seconds();
		Context.MaxSolveDurationSeconds = FMath::Max(0.0f, EffectiveExecutionSettings.MaxSolveDurationSeconds);
		Context.MaxFailureDetails = FMath::Max(0, EffectiveExecutionSettings.MaxFailureDetails);
		ApplyTraceSettings(Context, EffectiveExecutionSettings);
		Context.Result = BaseResult;
		Context.bUseIndexedCandidateFiltering = bUseIndexedCandidateFiltering;
	}

	bool BuildAuthoredPlanImpl(
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const FLayoutModuleCatalog& ModuleCatalog,
		const int32 Seed,
		const FIntPoint& FootprintSize,
		TArray<FLayoutPlannedCell>& OutPlannedCells,
		FString& OutFailureReason,
		TArray<FLayoutCellReservationRecord>* const OutCompiledReservations)
	{
		OutPlannedCells.Reset();
		if (OutCompiledReservations != nullptr)
		{
			OutCompiledReservations->Reset();
		}
		OutFailureReason.Reset();
		if (FootprintSize.X <= 0 || FootprintSize.Y <= 0)
		{
			OutFailureReason = TEXT("Authored layout planning requires a positive footprint size.");
			return false;
		}

		FLayoutProfileSolveSnapshot FixedFootprintProfile = ProfileSnapshot;
		FixedFootprintProfile.MinimumFootprintInCells = FootprintSize;
		FixedFootprintProfile.MaximumFootprintInCells = FootprintSize;
		FLayoutSolverExecutionSettings ExecutionSettings;
		FLayoutSolveResult BaseResult;
		BaseResult.Seed = Seed;
		BaseResult.FootprintSize = FootprintSize;
		FSolveContext Context;
		ApplySolveSettings(
			Context,
			nullptr,
			&FixedFootprintProfile,
			&ExecutionSettings,
			Seed,
			BaseResult,
			true,
			&ModuleCatalog);
		BuildPlan(Context);
		if (!Context.Result.FailureReason.IsEmpty())
		{
			OutFailureReason = Context.Result.FailureReason;
			return false;
		}

		OutPlannedCells = MoveTemp(Context.Result.PlannedCells);
		if (OutCompiledReservations != nullptr)
		{
			*OutCompiledReservations = MoveTemp(Context.Result.CompiledReservations);
		}
		return true;
	}

	/** Promotes one route-safe Entry per connected terrain-seam face group. */
	bool PromoteRouteTerrainSeamCellsToEntries(FSolveContext& Context)
	{
		if (!Context.ProfileSnapshot.bSupportsSteppedTerrainSolve
			|| !Context.ProfileSnapshot.bEnableTerrainSeams)
		{
			return false;
		}

		TSet<FIntVector> SeamCells;
		for (const TPair<FIntVector, uint8>& TerrainSeamPair : Context.TerrainSeamFaceMasksByCell)
		{
			const FIntVector& SeamCell = TerrainSeamPair.Key;
			if (Context.PlannedCellIntents.Contains(SeamCell + FIntVector(0, 0, 1)))
			{
				SeamCells.Add(SeamCell);
			}
		}
		if (SeamCells.IsEmpty())
		{
			return false;
		}

		TMap<FIntVector, int32> ComponentBySeamCell;
		int32 NextComponentId = 0;
		for (const FIntVector& SeedCell : SeamCells)
		{
			if (ComponentBySeamCell.Contains(SeedCell))
			{
				continue;
			}

			TArray<FIntVector> PendingCells = { SeedCell };
			ComponentBySeamCell.Add(SeedCell, NextComponentId);
			for (int32 PendingIndex = 0; PendingIndex < PendingCells.Num(); ++PendingIndex)
			{
				const FIntVector CurrentCell = PendingCells[PendingIndex];
				for (const FIntVector& NeighborOffset : {
					FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
					FIntVector(0, 1, 0), FIntVector(0, -1, 0),
					FIntVector(1, 1, 0), FIntVector(1, -1, 0),
					FIntVector(-1, 1, 0), FIntVector(-1, -1, 0) })
				{
					const FIntVector NeighborCell = CurrentCell + NeighborOffset;
					if (SeamCells.Contains(NeighborCell)
						&& !ComponentBySeamCell.Contains(NeighborCell))
					{
						ComponentBySeamCell.Add(NeighborCell, NextComponentId);
						PendingCells.Add(NeighborCell);
					}
				}
			}
			++NextComponentId;
		}

		auto IsRouteSafeEntryCell = [&Context](const FIntVector& Cell)
		{
			if (Context.VerticalAccessReservedCells.Contains(Cell))
			{
				return false;
			}
			for (const FIntVector& Delta : {
				FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
				FIntVector(0, 1, 0), FIntVector(0, -1, 0),
				FIntVector(0, 0, 1), FIntVector(0, 0, -1) })
			{
				if (Context.PlannedCellIntents.FindRef(Cell + Delta) == ELayoutCellIntent::VerticalAccess
					|| Context.VerticalAccessReservedCells.Contains(Cell + Delta))
				{
					return false;
				}
			}
			return true;
		};
		auto IsEarlierCell = [](const FLayoutPlannedCell* Left, const FLayoutPlannedCell& Right)
		{
			return Left == nullptr
				|| Right.Cell.Z < Left->Cell.Z
				|| (Right.Cell.Z == Left->Cell.Z
					&& (Right.Cell.Y < Left->Cell.Y
						|| (Right.Cell.Y == Left->Cell.Y && Right.Cell.X < Left->Cell.X)));
		};
		auto DoesCellAdmitTerrainSeamEntry = [&Context](FLayoutPlannedCell& CandidateCell)
		{
			const ELayoutCellIntent OriginalIntent = CandidateCell.Intent;
			const ELayoutEntryOrigin OriginalEntryOrigin = CandidateCell.EntryOrigin;
			CandidateCell.Intent = ELayoutCellIntent::Entry;
			CandidateCell.EntryOrigin = ELayoutEntryOrigin::TerrainSeam;
			Context.PlannedCellIntents.Add(CandidateCell.Cell, CandidateCell.Intent);
			RefreshCompiledFaceInterfaces(Context);
			const bool bAdmitsEntryModule = !BuildOrderedCandidates(
				Context,
				CandidateCell.Cell,
				ELayoutCellIntent::Entry).IsEmpty()
				&& Context.Result.FailureReason.IsEmpty();
			CandidateCell.Intent = OriginalIntent;
			CandidateCell.EntryOrigin = OriginalEntryOrigin;
			Context.PlannedCellIntents.Add(CandidateCell.Cell, CandidateCell.Intent);
			Context.InitialDomainAdmissionFailuresByCell.Remove(CandidateCell.Cell);
			RefreshCompiledFaceInterfaces(Context);
			return bAdmitsEntryModule;
		};

		TMap<int32, FLayoutPlannedCell*> SelectedEntryByComponent;
		TSet<int32> PreselectedEntryComponents;
		for (FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			const int32* ComponentId = ComponentBySeamCell.Find(PlannedCell.Cell);
			if (ComponentId == nullptr)
			{
				continue;
			}

			const bool bPreselectedTerrainSeamEntry =
				PlannedCell.Intent == ELayoutCellIntent::Entry
				&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam;
			if (bPreselectedTerrainSeamEntry)
			{
				// Preserve an existing terrain-seam gate for idempotent finalization.
				if (!PreselectedEntryComponents.Contains(*ComponentId))
				{
					SelectedEntryByComponent.Add(*ComponentId, &PlannedCell);
				}
				PreselectedEntryComponents.Add(*ComponentId);
				continue;
			}

			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				// Authored and contracted Entries connect to an exterior or another
				// layout; they must not suppress a separate interior terrain-seam gate.
				continue;
			}

			// Adapter-authored seam faces, not sparse corridor hull topology, own
			// terrain-seam Entry eligibility. Rank route-safe seam cells and prefer
			// Interior over Boundary.
			if (PreselectedEntryComponents.Contains(*ComponentId)
				|| (PlannedCell.Intent != ELayoutCellIntent::Interior
					&& PlannedCell.Intent != ELayoutCellIntent::Boundary)
				|| !IsRouteSafeEntryCell(PlannedCell.Cell)
				|| !DoesCellAdmitTerrainSeamEntry(PlannedCell))
			{
				continue;
			}

			FLayoutPlannedCell* ExistingEntry = SelectedEntryByComponent.FindRef(*ComponentId);
			const bool bCandidatePreservesExistingInteriorPreference =
				PlannedCell.Intent == ELayoutCellIntent::Interior
				&& (ExistingEntry == nullptr || ExistingEntry->Intent != ELayoutCellIntent::Interior);
			if (ExistingEntry == nullptr
				|| bCandidatePreservesExistingInteriorPreference
				|| (ExistingEntry != nullptr
					&& ExistingEntry->Intent == PlannedCell.Intent
					&& IsEarlierCell(ExistingEntry, PlannedCell)))
			{
				SelectedEntryByComponent.Add(*ComponentId, &PlannedCell);
			}
		}

		for (int32 ComponentId = 0; ComponentId < NextComponentId; ++ComponentId)
		{
			if (!SelectedEntryByComponent.Contains(ComponentId))
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Terrain-seam face group %d has no route-safe eligible Entry cell."),
					ComponentId);
				return false;
			}
		}

		bool bChanged = false;
		for (const TPair<int32, FLayoutPlannedCell*>& EntryPair : SelectedEntryByComponent)
		{
			FLayoutPlannedCell* SelectedEntry = EntryPair.Value;
			if (SelectedEntry != nullptr)
			{
				Context.PlannedCellIntents[SelectedEntry->Cell] = ELayoutCellIntent::Entry;
				SelectedEntry->Intent = ELayoutCellIntent::Entry;
				SelectedEntry->EntryOrigin = ELayoutEntryOrigin::TerrainSeam;
				bChanged = true;
			}
		}
		return bChanged;
	}

	/** Rebuilds domains while promoting only sparse cells whose exact interfaces reject empty occupancy. */
	bool RebuildInitialDomainsWithSparseStructuralClosure(FSolveContext& Context)
	{
		for (int32 ClosureStep = 0;
			ClosureStep <= Context.Result.PlannedCells.Num();
			++ClosureStep)
		{
			if (!LayoutSolveExecution::Checkpoint(Context.Result.FailureReason)) return false;
			Context.Result.FailureReason.Reset();
			Context.bHasPreparedInitialDomainFailure = false;
			BuildInitialDomains(Context);
			if (Context.bTimeBudgetExceeded) return false;
			const bool bDomainsNonEmpty = ValidatePreparedInitialDomainsNonEmpty(Context)
				&& Context.Result.FailureReason.IsEmpty();
			if (bDomainsNonEmpty)
			{
				bool bCommittedStructuralSupport = false;
				for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
				{
					if (PlannedCell.Intent != ELayoutCellIntent::Connector
						|| !Context.TerrainResidualRuleIdByCell.Contains(PlannedCell.Cell))
					{
						continue;
					}
					if (const FSolveContext::FSolvePlacement* ExistingPlacement =
							Context.Placements.Find(PlannedCell.Cell))
					{
						if (FSolveContext::IsOccupiedPlacement(*ExistingPlacement))
						{
							continue;
						}
						Context.Placements.Remove(PlannedCell.Cell);
					}
					const TArray<FSolveCandidate>* Domain = Context.InitialDomains.Find(PlannedCell.Cell);
					if (Domain != nullptr
						&& Domain->Num() == 1
						&& LayoutProfileSolverInternal::IsOccupiedCandidate((*Domain)[0]))
					{
						CommitOccupiedCandidateBundle(Context, PlannedCell.Cell, (*Domain)[0]);
						bCommittedStructuralSupport = true;
					}
				}
				if (!bCommittedStructuralSupport)
				{
					return true;
				}
				continue;
			}

			int32 PromotedCellCount = 0;
			for (FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
			{
				const TArray<FSolveCandidate>* Domain = Context.InitialDomains.Find(PlannedCell.Cell);
				const FSolveContext::FSolvePlacement* ExistingPlacement =
					Context.Placements.Find(PlannedCell.Cell);
				if (Domain != nullptr && !Domain->IsEmpty())
				{
					continue;
				}
				if (!Context.TerrainResidualRuleIdByCell.Contains(PlannedCell.Cell)
					|| PlannedCell.Intent == ELayoutCellIntent::Connector
					|| (ExistingPlacement != nullptr
						&& FSolveContext::IsOccupiedPlacement(*ExistingPlacement)))
				{
					continue;
				}
				PlannedCell.Intent = ELayoutCellIntent::Connector;
				Context.PlannedCellIntents.FindOrAdd(PlannedCell.Cell) =
					ELayoutCellIntent::Connector;
				Context.Placements.Remove(PlannedCell.Cell);
				++PromotedCellCount;
			}
			if (PromotedCellCount > 0)
			{
				continue;
			}

			const FIntVector FailedCell = Context.PreparedInitialDomainFailureCell;
			const FLayoutPlannedCell* FailedPlannedCell = Context.Result.PlannedCells.FindByPredicate(
				[&FailedCell](const FLayoutPlannedCell& PlannedCell)
				{
					return PlannedCell.Cell == FailedCell;
				});
			const FString FailedIntent = FailedPlannedCell != nullptr
				? ToDebugString(FailedPlannedCell->Intent)
				: FString(TEXT("<none>"));
			Context.Result.FailureReason += FString::Printf(
				TEXT(" Structural-support closure stopped: hasPreparedFailure=%s failedCell=%s plannedCell=%s sparseAuthority=%s intent=%s."),
				Context.bHasPreparedInitialDomainFailure ? TEXT("true") : TEXT("false"),
				*FailedCell.ToString(),
				FailedPlannedCell != nullptr ? TEXT("true") : TEXT("false"),
				Context.TerrainResidualRuleIdByCell.Contains(FailedCell) ? TEXT("true") : TEXT("false"),
				*FailedIntent);
			return false;
		}
		Context.Result.FailureReason = TEXT("Sparse structural-support domain closure did not settle within the planned-cell count.");
		return false;
	}

	/**
	 * Seeds unambiguous multi-cell occupancy before routes freeze neighboring domains.
	 * Cross-level shadows are settled structure, so later level cells must solve around them.
	 */
	bool SeedDeterministicMultiCellPreparedPrefix(FSolveContext& Context)
	{
		TArray<const FLayoutPlannedCell*> SortedCells;
		SortedCells.Reserve(Context.Result.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			SortedCells.Add(&PlannedCell);
		}
		SortedCells.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
		{
			if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
			if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
			return Left.Cell.X < Right.Cell.X;
		});

		while (true)
		{
			const FLayoutPlannedCell* SelectedRoot = nullptr;
			FSolveCandidate SelectedCandidate;
			for (const FLayoutPlannedCell* const PlannedCell : SortedCells)
			{
				if (Context.Placements.Contains(PlannedCell->Cell))
				{
					continue;
				}
				const TArray<FSolveCandidate>* const Domain = Context.InitialDomains.Find(PlannedCell->Cell);
				if (Domain == nullptr || Domain->Num() != 1
					|| !LayoutProfileSolverInternal::IsOccupiedCandidate((*Domain)[0])
					|| BuildCandidateWorldOccupiedCells(Context, PlannedCell->Cell, (*Domain)[0]).Num() <= 1)
				{
					continue;
				}

				SelectedRoot = PlannedCell;
				SelectedCandidate = (*Domain)[0];
				break;
			}
			if (SelectedRoot == nullptr)
			{
				return true;
			}

			const TArray<FIntVector> PlacedCells = CommitOccupiedCandidateBundle(
				Context,
				SelectedRoot->Cell,
				SelectedCandidate);
			AddTraceEvent(Context, FString::Printf(
				TEXT("prepared-prefix-multi-cell root=%s candidate=%s occupied=%d"),
				*SelectedRoot->Cell.ToString(),
				*BuildCandidateDebugName(Context, SelectedCandidate),
				PlacedCells.Num()));

			if (!RebuildInitialDomainsWithSparseStructuralClosure(Context))
			{
				return false;
			}
		}
	}

	bool PrepareSolveContextThroughRouteDomainStageImpl(
		FSolveContext& Context, const bool bReusePreparedVariants)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_RouteDomain, STAT_PorismLayout_RouteDomain);
		// A rejected plan has no domains to rebuild. Preserve its failure kind and
		// diagnostic instead of replacing them with downstream admission failures.
		if (!Context.Result.FailureReason.IsEmpty())
		{
			return false;
		}
		if (!bReusePreparedVariants || Context.Variants.IsEmpty()) BuildOrientedVariants(Context);
		else if (auto* Ledger = LayoutSolveExecution::CurrentThreadLedger()) ++Ledger->VariantReuses;
		// Route discovery and CSP both retain authored module level scope.
		if (!RebuildInitialDomainsWithSparseStructuralClosure(Context))
		{
			return false;
		}

		const bool bPromotedTerrainSeamEntries = PromoteRouteTerrainSeamCellsToEntries(Context);
		if (!Context.Result.FailureReason.IsEmpty())
		{
			return false;
		}
		if (bPromotedTerrainSeamEntries)
		{
			RefreshCompiledFaceInterfaces(Context);
			if (!RebuildInitialDomainsWithSparseStructuralClosure(Context))
			{
				return false;
			}
		}

		if (!ValidateAuthoredExteriorEntryCount(Context))
		{
			return false;
		}
		// Local project fix: compile counted providers before deterministic
		// composite prefix placement so shifted bundle roots update the same ledger.
		CompileHardZoneFeatureDemandModel(Context);
		if (!SeedDeterministicMultiCellPreparedPrefix(Context))
		{
			return false;
		}

		LayoutProfileSolverInternal::BuildRequiredTraversalRoutesForPreparedPlan(Context);
		if (!Context.Result.FailureReason.IsEmpty())
		{
			Context.Result.PreparationFailureKind =
				ELayoutSolvePreparationFailureKind::TraversalTopologyInfeasible;
			return false;
		}
		// Promote sparse cells only when exact discovery domains prove empty
		// occupancy impossible. These are structural supports, not ambient fill.
		for (FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (Context.TerrainResidualRuleIdByCell.Contains(PlannedCell.Cell)
				&& !DoesCellSatisfyCompiledEmptyInterfaces(Context, PlannedCell.Cell))
			{
				PlannedCell.Intent = ELayoutCellIntent::Connector;
				Context.PlannedCellIntents.FindOrAdd(PlannedCell.Cell) =
					ELayoutCellIntent::Connector;
				if (const FSolveContext::FSolvePlacement* ExistingPlacement =
						Context.Placements.Find(PlannedCell.Cell);
					ExistingPlacement != nullptr
					&& !FSolveContext::IsOccupiedPlacement(*ExistingPlacement))
				{
					Context.Placements.Remove(PlannedCell.Cell);
				}
			}
		}
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (!LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, PlannedCell.Cell))
			{
				continue;
			}
			if (const FSolveContext::FSolvePlacement* ExistingPlacement =
					Context.Placements.Find(PlannedCell.Cell);
				ExistingPlacement != nullptr
				&& !FSolveContext::IsOccupiedPlacement(*ExistingPlacement))
			{
				Context.Placements.Remove(PlannedCell.Cell);
			}
		}

		// Rebuild exact claimed domains after discovery before indexed search.
		if (!RebuildInitialDomainsWithSparseStructuralClosure(Context))
		{
			return false;
		}
		return LayoutProfileSolverInternal::ApplyRequiredTraversalRouteDomainConstraints(Context)
			&& ValidatePreparedInitialDomainsNonEmpty(Context);
	}

	void FinalizePreparedSolveContextForIndexedSearchImpl(
		FSolveContext& Context)
	{
		CompileHardClosureDomainFeasibility(Context);
		CompileHardZoneFeatureDemandModel(Context);
		if (!ValidatePreparedInitialDomainsNonEmpty(Context))
		{
			Context.Result.FailureReason.Reset();
			LayoutProfileSolverInternal::ValidateClosureCoverageForContext(Context);
			return;
		}

		InitializeIndexedCandidateUniverse(Context);
		BuildIndexedCompatibilityRows(Context);
		FString ClosureFailureReason;
		if (!ValidateAffectedHardClosureFeasibility(Context, {}, ClosureFailureReason))
		{
			Context.Result.FailureReason.Reset();
			LayoutProfileSolverInternal::ValidateClosureCoverageForContext(Context);
			return;
		}

		FString ZoneFeatureFailureReason;
		if (!ValidateHardZoneFeatureDemandFeasibility(
				Context,
				ZoneFeatureFailureReason))
		{
			Context.Result.FailureReason = MoveTemp(ZoneFeatureFailureReason);
			return;
		}

		// Local project change: settle ordinary terrain-residual cells as empty
		// after routes and hard providers freeze, avoiding pointless CSP search
		// while preserving them in canonical topology and realization authority.
		TSet<FIntVector> PotentialForeignBundleCells;
		for (const TPair<FIntVector, TArray<FSolveCandidate>>& DomainPair : Context.InitialDomains)
		{
			for (const FSolveCandidate& Candidate : DomainPair.Value)
			{
				if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
				{
					continue;
				}
				const TArray<FIntVector> BundleCells = BuildCandidateWorldOccupiedCells(Context, DomainPair.Key, Candidate);
				if (BundleCells.Num() <= 1)
				{
					continue;
				}
				for (const FIntVector& BundleCell : BundleCells)
				{
					if (BundleCell != DomainPair.Key)
					{
						PotentialForeignBundleCells.Add(BundleCell);
					}
				}
			}
		}
		TSet<FIntVector> PotentialVerticalAccessSupportCells;
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (PlannedCell.Intent != ELayoutCellIntent::VerticalAccess)
			{
				continue;
			}
			for (const int32 ZOffset : {0, 1})
			{
				const FIntVector EndpointCell = PlannedCell.Cell + FIntVector(0, 0, ZOffset);
				for (const FIntVector& Delta : {
					FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
					FIntVector(0, 1, 0), FIntVector(0, -1, 0)})
				{
					PotentialVerticalAccessSupportCells.Add(EndpointCell + Delta);
				}
			}
		}
		for (FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			if (PotentialVerticalAccessSupportCells.Contains(PlannedCell.Cell)
				&& Context.TerrainResidualRuleIdByCell.Contains(PlannedCell.Cell)
				&& ShouldPreferOccupiedVerticalAccessSupport(Context, PlannedCell.Cell))
			{
				// Every admitted adjacent stair root requires this face occupied, so
				// settle support as structural Connector authority before sparse empty
				// preference can create a deterministic dead branch.
				PlannedCell.Intent = ELayoutCellIntent::Connector;
				Context.PlannedCellIntents.FindOrAdd(PlannedCell.Cell) =
					ELayoutCellIntent::Connector;
				if (TArray<FSolveCandidate>* Domain = Context.InitialDomains.Find(PlannedCell.Cell))
				{
					Domain->RemoveAll([](const FSolveCandidate& Candidate)
					{
						return !LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate);
					});
				}
			}
		}
		for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
		{
			const bool bPotentialHardZoneProvider = Context.HardZoneFeatureProviderRootsByDemand.ContainsByPredicate(
				[&PlannedCell](const TArray<FIntVector>& Roots)
				{
					return Roots.Contains(PlannedCell.Cell);
				});
			const bool bPotentialClosureProvider = Context.HardClosureProviderRootsBySegment.ContainsByPredicate(
				[&PlannedCell](const TArray<FIntVector>& Roots)
				{
					return Roots.Contains(PlannedCell.Cell);
				});
			if (!Context.TerrainResidualRuleIdByCell.Contains(PlannedCell.Cell)
				|| Context.Placements.Contains(PlannedCell.Cell)
				|| PlannedCell.Intent == ELayoutCellIntent::Entry
				|| PlannedCell.Intent == ELayoutCellIntent::Connector
				|| PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
				|| LayoutProfileSolverInternal::IsRequiredTraversalRouteCell(Context, PlannedCell.Cell)
				|| bPotentialHardZoneProvider
				|| bPotentialClosureProvider
				|| PotentialForeignBundleCells.Contains(PlannedCell.Cell)
				|| PotentialVerticalAccessSupportCells.Contains(PlannedCell.Cell)
				|| !DoesCellSatisfyCompiledEmptyInterfaces(Context, PlannedCell.Cell))
			{
				continue;
			}
			Context.Placements.Add(
				PlannedCell.Cell,
				MakeRootSolvePlacement(Context, PlannedCell.Cell, 0, INDEX_NONE));
		}
		RefreshCompiledFaceInterfaces(Context);
	}

	bool ContinuePreparedSolveContextAfterRouteDomainStageImpl(
		FSolveContext& Context,
		const bool bValidateClosureContracts)
	{
		FPreparedSearchFrontier SearchFrontier;
		if (!TryPrepareSolveContextThroughSearchPrefixStageInternal(
			Context,
			0,
			SearchFrontier))
		{
			Context.Result.bSucceeded = false;
			return false;
		}

		const bool bSolved =
			ContinueSolveFromPreparedSearchFrontier(
				Context,
				0,
				SearchFrontier)
			&& (!bValidateClosureContracts
				|| LayoutProfileSolverInternal::ValidateClosureCoverageForContext(
					Context));
		Context.Result.bSucceeded = bSolved;
		if (bSolved)
		{
			Context.Result.FailureReason.Reset();
		}

		return bSolved;
	}

	bool SolvePreparedContext(
		FSolveContext& Context,
		const bool bValidateClosureContracts = true,
		const bool bValidateLiveCompositePlacementGuard = true)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_Search, STAT_PorismLayout_Search);
		if (bValidateLiveCompositePlacementGuard && !ValidateLiveLeafSolverModuleSnapshots(Context))
		{
			PublishTraceIfNeeded(Context);
			Context.Result.bSucceeded = false;
			return false;
		}

		if (!PrepareSolveContextThroughRouteDomainStageImpl(Context))
		{
			PublishTraceIfNeeded(Context);
			Context.Result.bSucceeded = false;
			return false;
		}

		LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier
			SearchPrefixStage;
		if (!LayoutProfileSolverInternal::TryPrepareSolveContextThroughSearchPrefixStage(
			Context,
			SearchPrefixStage))
		{
			PublishTraceIfNeeded(Context);
			Context.Result.bSucceeded = false;
			return false;
		}

		return LayoutProfileSolverInternal::ContinuePreparedSolveContextAfterSearchPrefixStage(
			Context,
			SearchPrefixStage,
			bValidateClosureContracts);
	}

	/** Materializes exact hard-provider commitments from accepted bundle-root ledger identity. */
	void PopulateHardZoneFeatureProviderCommitments(
		const FSolveContext& Context,
		FLayoutSolveResult& Result)
	{
		Result.ZoneFeatureProviderCommitments =
			Context.PrecommittedZoneFeatureProviderCommitments;
		if (!Context.bHardZoneFeatureDemandsCompiled)
		{
			return;
		}

		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair :
			Context.Placements)
		{
			if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value)
				|| !IsBundleRootSolvePlacement(PlacementPair.Key, PlacementPair.Value)
				|| !Context.Variants.IsValidIndex(PlacementPair.Value.VariantIndex))
			{
				continue;
			}

			TArray<int32> DemandIndices;
			CollectMatchingHardZoneFeatureDemandIndices(
				Context,
				PlacementPair.Key,
				PlacementPair.Value.VariantIndex,
				DemandIndices);
			const FSolveContext::FOrientedModuleVariant& Variant =
				Context.Variants[PlacementPair.Value.VariantIndex];
			for (const int32 DemandIndex : DemandIndices)
			{
				FLayoutZoneFeatureProviderCommitment& Commitment =
					Result.ZoneFeatureProviderCommitments.AddDefaulted_GetRef();
				Commitment.RequirementId =
					Context.HardZoneFeatureDemands[DemandIndex].RequirementId;
				Commitment.SourceRegionDebugPath = Context.RegionDebugPath;
				Commitment.SourceContentEntryId = Variant.SourceContentEntryId;
				Commitment.Cell = PlacementPair.Key;
				Commitment.ModuleSnapshotId = Variant.ModuleSnapshotId;
				Commitment.ModuleLevelIndex =
					LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(
						Context,
						PlacementPair.Key);
				if (const LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord* ViewRecord =
						Context.FinalizedCellsByPhysicalCell.Find(PlacementPair.Key))
				{
					Commitment.TerrainStageIndex = ViewRecord->bHasTerrainStage
						? ViewRecord->TerrainStageIndex
						: INDEX_NONE;
				}
				Commitment.ProviderCommitmentId =
					LayoutZoneFeatureDemand::BuildProviderCommitmentId(
						Context.EffectiveSnapshotId,
						Context.RegionDebugPath,
						Commitment.RequirementId,
						Variant.SourceContentEntryId,
						PlacementPair.Key,
						Commitment.ModuleLevelIndex,
						Commitment.TerrainStageIndex);
			}
		}

		Result.ZoneFeatureProviderCommitments.Sort(
			[](const FLayoutZoneFeatureProviderCommitment& Left,
				const FLayoutZoneFeatureProviderCommitment& Right)
			{
				return Left.ProviderCommitmentId.LexicalLess(
					Right.ProviderCommitmentId);
			});
	}

	void AppendContextPlacementsToResult(const FSolveContext& Context, FLayoutSolveResult& Result)
	{
		PopulateHardZoneFeatureProviderCommitments(Context, Result);
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
		{
			if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value)
				|| !IsBundleRootSolvePlacement(PlacementPair.Key, PlacementPair.Value))
			{
				continue;
			}

			FLayoutPlacedModule& Placement = Result.Placements.AddDefaulted_GetRef();
			Placement.Cell = PlacementPair.Key;
			Placement.Intent = Context.PlannedCellIntents[PlacementPair.Key];
			Placement.YawRotationSteps = PlacementPair.Value.YawRotationSteps;
			Placement.ModuleSnapshotIndex = PlacementPair.Value.ModuleSnapshotIndex;
			Placement.ModuleSnapshotId = PlacementPair.Value.ModuleSnapshotId;
			if (PlacementPair.Value.VariantIndex != INDEX_NONE
				&& Context.Variants.IsValidIndex(PlacementPair.Value.VariantIndex))
			{
				Placement.SourceContentEntryId = Context.Variants[PlacementPair.Value.VariantIndex].SourceContentEntryId;
			}
			PopulatePlacedModuleBundleMetadata(Context, PlacementPair.Value.VariantIndex, Placement);

			if (Placement.Intent == ELayoutCellIntent::Entry || Placement.Intent == ELayoutCellIntent::Connector)
			{
				Result.ExportedEntryCells.AddUnique(Placement.Cell);
			}
		}
	}

	/** Restores solver's largest legal frontier when no complete layout exists. */
	void RestoreBestPartialPlacements(FSolveContext& Context)
	{
		if (Context.BestPartialOccupiedPlacementCount <= Context.Result.Placements.Num())
		{
			return;
		}

		Context.Placements = Context.BestPartialPlacements;
		Context.Result.Placements.Reset();
		Context.Result.ExportedEntryCells.Reset();
		AppendContextPlacementsToResult(Context, Context.Result);
	}

	void SortResultPlacements(FLayoutSolveResult& Result)
	{
		Result.Placements.Sort([](const FLayoutPlacedModule& Left, const FLayoutPlacedModule& Right)
		{
			if (Left.Cell.Z != Right.Cell.Z)
			{
				return Left.Cell.Z < Right.Cell.Z;
			}

			if (Left.Cell.Y != Right.Cell.Y)
			{
				return Left.Cell.Y < Right.Cell.Y;
			}

			return Left.Cell.X < Right.Cell.X;
		});
	}

	TArray<FLayoutPlannedCell> FilterPlannedCellsForLevel(
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const int32 Level)
	{
		TArray<FLayoutPlannedCell> Result;
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			if (PlannedCell.Cell.Z == Level)
			{
				Result.Add(PlannedCell);
			}
		}

		return Result;
	}

	TSet<FIntVector> BuildPlannedCellSet(
		const TArray<FLayoutPlannedCell>& PlannedCells)
	{
		TSet<FIntVector> Result;
		Result.Reserve(PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			Result.Add(PlannedCell.Cell);
		}

		return Result;
	}

	void AddVerticalContinuationExternalPlannedNeighborFaces(
		const TSet<FIntVector>& LevelPlannedCellSet,
		const TSet<FIntVector>& PlannedCellSet,
		TMap<FIntVector, uint8>& OutExternalPlannedNeighborFaceMasks)
	{
		for (const FIntVector& PlannedCell : LevelPlannedCellSet)
		{
			const FIntVector ContinuationCell = PlannedCell + FIntVector(0, 0, 1);
			if (PlannedCellSet.Contains(ContinuationCell))
			{
				OutExternalPlannedNeighborFaceMasks.FindOrAdd(
					PlannedCell) |= BuildDirectionMask(
						ELayoutFaceDirection::PosZ);
			}
		}
	}

	TMap<FIntVector, FSolveContext::FSolvePlacement> BuildVerticalContinuationFixedNeighbors(
		const TSet<FIntVector>& FullPlannedCellSet,
		const TSet<FIntVector>& LevelPlannedCellSet,
		const TMap<FIntVector, FSolveContext::FSolvePlacement>& SolvedPlacements,
		const int32 CurrentLevel,
		const TArray<FLayoutModuleSolveSnapshot>* ModuleSnapshots);

	/** Marks selected-cell faces whose planned neighbors remain outside a level-scoped solve. */
	TMap<FIntVector, uint8> BuildExternalPlannedNeighborFaceMasks(
		const TArray<FLayoutPlannedCell>& SelectedCells,
		const TSet<FIntVector>& FullPlannedCellSet)
	{
		TSet<FIntVector> SelectedCellSet;
		SelectedCellSet.Reserve(SelectedCells.Num());
		for (const FLayoutPlannedCell& SelectedCell : SelectedCells)
		{
			SelectedCellSet.Add(SelectedCell.Cell);
		}

		TMap<FIntVector, uint8> Result;
		for (const FLayoutPlannedCell& SelectedCell : SelectedCells)
		{
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
				const FIntVector NeighborCell =
					SelectedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (FullPlannedCellSet.Contains(NeighborCell)
					&& !SelectedCellSet.Contains(NeighborCell))
				{
					Result.FindOrAdd(SelectedCell.Cell) |= BuildDirectionMask(Direction);
				}
			}
		}
		return Result;
	}

	bool PlacementExportsUpwardVerticalContinuationContract(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		for (const FLayoutDerivedVerticalAccessContract& Contract :
			GetPlacementVerticalAccessContracts(Context, Placement))
		{
			if (Contract.ExitFaceDirection == ELayoutFaceDirection::PosZ)
			{
				return true;
			}
		}

		return false;
	}

	bool PlacementParticipatesInVerticalContinuationAdjacency(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		return FSolveContext::IsOccupiedPlacement(Placement)
			&& PlacementExportsUpwardVerticalContinuationContract(
				Context,
				Placement);
	}

	TMap<FIntVector, FSolveContext::FSolvePlacement> BuildVerticalContinuationFixedNeighbors(
		const TSet<FIntVector>& FullPlannedCellSet,
		const TSet<FIntVector>& LevelPlannedCellSet,
		const TMap<FIntVector, FSolveContext::FSolvePlacement>& SolvedPlacements,
		const int32 CurrentLevel,
		const TArray<FLayoutModuleSolveSnapshot>* ModuleSnapshots)
	{
		TMap<FIntVector, FSolveContext::FSolvePlacement> Result;
		TOptional<FSolveContext> VariantLookupContext;
		auto EnsureVariantLookupContext =
			[&VariantLookupContext, ModuleSnapshots]() -> FSolveContext*
		{
			if (ModuleSnapshots == nullptr || ModuleSnapshots->IsEmpty())
			{
				return nullptr;
			}

			if (!VariantLookupContext.IsSet())
			{
				FSolveContext LookupContext;
				LookupContext.ModuleSnapshots = *ModuleSnapshots;
				BuildOrientedVariants(LookupContext);
				VariantLookupContext = MoveTemp(LookupContext);
			}

			return &VariantLookupContext.GetValue();
		};
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : SolvedPlacements)
		{
			if (FSolveContext::IsOccupiedPlacement(PlacementPair.Value)
				&& PlacementPair.Value.BundleRootCell.Z < CurrentLevel
				&& PlacementPair.Key.Z == CurrentLevel
				&& LevelPlannedCellSet.Contains(PlacementPair.Key))
			{
				Result.Add(PlacementPair.Key, PlacementPair.Value);
				continue;
			}

			if (PlacementPair.Key.Z != CurrentLevel - 1
				|| !FullPlannedCellSet.Contains(PlacementPair.Key))
			{
				continue;
			}

			const FIntVector ContinuationCell =
				PlacementPair.Key + FIntVector(0, 0, 1);
			if (!LevelPlannedCellSet.Contains(ContinuationCell))
			{
				continue;
			}

			FSolveContext* LookupContext = EnsureVariantLookupContext();
			// Project-specific continuation rebuild must preserve real upward
			// vertical-access contracts even when the lower planned intent was
			// widened away from VerticalAccess during upper-level reconstruction.
			if (LookupContext != nullptr
				&& PlacementExportsUpwardVerticalContinuationContract(
					*LookupContext,
					PlacementPair.Value))
			{
				Result.Add(PlacementPair.Key, PlacementPair.Value);
				continue;
			}

			FLayoutFaceRule TopFaceRule;
			bool bHasTopFaceRule = false;
			if (PlacementPair.Value.VariantIndex != INDEX_NONE && ModuleSnapshots != nullptr && !ModuleSnapshots->IsEmpty())
			{
				// Project-specific continuation rebuild must preserve the carried local
				// bundle cell's top-face contract instead of collapsing back to a
				// root-level variant face during upper-level fixed-neighbor selection.
				if (LookupContext != nullptr)
				{
					bHasTopFaceRule = TryGetPlacementFaceRule(
						*LookupContext,
						PlacementPair.Value,
						ELayoutFaceDirection::PosZ,
						TopFaceRule);
				}
			}

			if (bHasTopFaceRule && RequiresFilledNeighborOccupancy(TopFaceRule))
			{
				Result.Add(PlacementPair.Key, PlacementPair.Value);
			}
		}

		return Result;
	}

	/** Audits lateral neighbors, explicit VA landings and conditional deck occupancy across Z. */
	bool ShouldAuditAdjacencyBetweenCells(const FSolveContext& Context, const FIntVector& Cell, const FIntVector& NeighborCell)
	{
		if (Cell.Z == NeighborCell.Z)
		{
			return true;
		}

		if (Context.PlannedCellIntents.Contains(Cell)
			&& Context.PlannedCellIntents.Contains(NeighborCell))
		{
			const FIntVector& LowerCell = Cell.Z < NeighborCell.Z ? Cell : NeighborCell;
			const FIntVector& UpperCell = Cell.Z < NeighborCell.Z ? NeighborCell : Cell;
			const FLayoutPlannedCell* Upper = Context.OwningTopologyCellsByPhysicalCell.Find(UpperCell);
			// An offered deck and its lower cap must settle together: admitting open
			// sky statically must never permit a filled deck over RequiresEmptyNeighbor.
			return Context.PlannedCellIntents.FindChecked(LowerCell) == ELayoutCellIntent::VerticalAccess
				|| (Upper != nullptr && Upper->bIsBridgeCell && Upper->bIsTopBridgeOffer);
		}

		if (Context.FixedNeighborPlacements.Contains(Cell)
			|| Context.FixedNeighborPlacements.Contains(NeighborCell))
		{
			return true;
		}

		const FSolveContext::FSolvePlacement* Placement =
			Context.Placements.Find(Cell);
		if (Placement != nullptr
			&& PlacementParticipatesInVerticalContinuationAdjacency(
				Context,
				*Placement))
		{
			return true;
		}

		const FSolveContext::FSolvePlacement* NeighborPlacement =
			Context.Placements.Find(NeighborCell);
		return NeighborPlacement != nullptr
			&& PlacementParticipatesInVerticalContinuationAdjacency(
				Context,
				*NeighborPlacement);
	}

	TSet<FGameplayTag> BuildRootWalkableAreasForSolvedLevel(const FSolveContext& Context, const int32 Level)
	{
		TSet<FGameplayTag> Result;
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
		{
			if (PlacementPair.Key.Z != Level || !FSolveContext::IsOccupiedPlacement(PlacementPair.Value))
			{
				continue;
			}

			for (const FGameplayTag& WalkableArea : GetPlacementWalkableAreas(Context, PlacementPair.Value))
			{
				if (LayoutProfileSolverInternal::IsPlacedEntryRootForWalkableArea(Context, PlacementPair.Key, PlacementPair.Value, WalkableArea))
				{
					Result.Add(WalkableArea);
				}
			}
		}

		if (Level == 0 && Result.Contains(LayoutGameplayTags::TraversalPrimary))
		{
			return {LayoutGameplayTags::TraversalPrimary};
		}

		return Result;
	}

	TSet<FGameplayTag> BuildNextLevelActiveWalkableAreas(
		const FSolveContext& LevelContext,
		const TSet<FGameplayTag>& CurrentLevelActiveAreas,
		const int32 CurrentLevel)
	{
		TSet<FGameplayTag> Result;
		for (const FLayoutPlannedCell& PlannedCell : LevelContext.Result.PlannedCells)
		{
			if (PlannedCell.Cell.Z != CurrentLevel)
			{
				continue;
			}

			const FSolveCellFaceInterface* UpwardFaceInterface =
				FindCompiledCellFaceInterface(
					LevelContext,
					PlannedCell.Cell,
					ELayoutFaceDirection::PosZ);
			if (UpwardFaceInterface == nullptr
				|| (UpwardFaceInterface->NeighborKind
						!= ESolveCellFaceNeighborKind::InternalPlannedNeighbor
					&& UpwardFaceInterface->NeighborKind
						!= ESolveCellFaceNeighborKind::ExternalPlannedNeighbor))
			{
				continue;
			}

			const FSolveContext::FSolvePlacement* Placement = LevelContext.Placements.Find(PlannedCell.Cell);
			if (Placement == nullptr || !FSolveContext::IsOccupiedPlacement(*Placement))
			{
				continue;
			}

			for (const FLayoutDerivedVerticalAccessContract& Contract : GetPlacementVerticalAccessContracts(LevelContext, *Placement))
			{
				if (Contract.ExitFaceDirection != ELayoutFaceDirection::PosZ)
				{
					continue;
				}

				bool bCurrentLevelCanEnter = CurrentLevelActiveAreas.IsEmpty();
				if (!bCurrentLevelCanEnter)
				{
					for (const FGameplayTag& SourceTraversalChannel : Contract.SourceTraversalChannels)
					{
						if (SourceTraversalChannel.IsValid() && CurrentLevelActiveAreas.Contains(SourceTraversalChannel))
						{
							bCurrentLevelCanEnter = true;
							break;
						}
					}
				}

				if (!bCurrentLevelCanEnter)
				{
					continue;
				}

				for (const FGameplayTag& ExitTraversalChannel : Contract.ExitTraversalChannels)
				{
					if (ExitTraversalChannel.IsValid())
					{
						Result.Add(ExitTraversalChannel);
					}
				}
			}
		}

		return Result;
	}

	/** Installs exact pre-proof candidate domains into any solve-context construction path. */
	void InstallCandidateDomainRestrictionsFromRequest(
		FSolveContext& Context,
		const FLayoutRegionSolveRequest& Request,
		const bool bJunctionRestrictionsOnly = false)
	{
		Context.CandidateDomainCertificateId = Request.CandidateDomainCertificateId;
		Context.Result.CandidateDomainCertificateId = Request.CandidateDomainCertificateId;
		Context.Result.CandidateDomainRestrictionIds.Reset();
		Context.CandidateDomainRestrictionsByCell.Reset();
		for (const FLayoutCellCandidateDomainRestriction& Restriction :
			Request.CandidateDomainRestrictions)
		{
			// Multi-level host selection still owns provisional non-junction domains;
			// owner-junction cells are final and must survive that context rebuild.
			if (bJunctionRestrictionsOnly && !Restriction.bTreatAsJunctionPlacementZone)
			{
				continue;
			}
			if (Restriction.RestrictionId.IsNone()
				|| Restriction.AllowedCandidates.IsEmpty()
				|| Context.CandidateDomainRestrictionsByCell.Contains(Restriction.Cell))
			{
				Context.Result.FailureReason = FString::Printf(
					TEXT("Candidate-domain certificate '%s' contains an invalid, empty, or duplicate restriction at %s."),
					*Request.CandidateDomainCertificateId.ToString(),
					*Restriction.Cell.ToString());
				break;
			}
			Context.CandidateDomainRestrictionsByCell.Add(Restriction.Cell, Restriction);
			Context.Result.CandidateDomainRestrictionIds.AddUnique(Restriction.RestrictionId);
			if (Restriction.BeforeVerticalAccessCandidates.IsSet() && !Restriction.BeforeVerticalAccessRestrictionId.IsNone())
			{
				// The narrowed domain consumes its inherited child restriction as well
				// as the exact VA overlay; parent proof audits require both identities.
				Context.Result.CandidateDomainRestrictionIds.AddUnique(Restriction.BeforeVerticalAccessRestrictionId);
			}
		}
		Context.Result.CandidateDomainRestrictionIds.Sort(
			[](const FLayoutId& Left, const FLayoutId& Right)
			{
				return Left.LexicalLess(Right);
			});
	}

	/** Solves every planned level in one CSP so vertical failures backtrack through lower-level choices. */
	FLayoutSolveResult SolveUnifiedVerticalRegions(
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const FLayoutSolverExecutionSettings* ExecutionSettings,
		const int32 Seed,
		const FLayoutSolveResult& BaseResult,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& FullPlannedCells,
		const bool bUseIndexedCandidateFiltering = true,
		const FLayoutModuleCatalog* ModuleCatalog = nullptr,
		const TArray<FLayoutSolveBoundaryPoint>* IncomingBoundaryPoints = nullptr,
		const TArray<FLayoutCommittedEndpointAnchor>* CommittedEndpointAnchors = nullptr,
		const TArray<FLayoutCommittedTraversalAnchor>* CommittedTraversalAnchors = nullptr,
		const bool bUsesChildSolveContext = false,
		const bool bDeferFinalReachabilityAudit = false,
		const bool bValidateLiveCompositePlacementGuard = true,
		const FLayoutFrozenTerrainContract* FrozenTerrainContract = nullptr,
		const TMap<FIntVector, uint8>* ExternalPlannedNeighborFaceMasks = nullptr,
		bool* OutHasPreparedInitialDomainFailure = nullptr,
		FIntVector* OutPreparedInitialDomainFailureCell = nullptr,
		const ELayoutContractRegionScope RegionScope = ELayoutContractRegionScope::Root,
		const FLayoutRegionSolveRequest* FrozenRequestContext = nullptr)
	{
		if (OutHasPreparedInitialDomainFailure != nullptr)
		{
			*OutHasPreparedInitialDomainFailure = false;
		}
		if (OutPreparedInitialDomainFailureCell != nullptr)
		{
			*OutPreparedInitialDomainFailureCell = FIntVector::ZeroValue;
		}
		FLayoutSolveResult InitialResult = BaseResult;
		InitialResult.FootprintSize = FootprintSize;
		InitialResult.PlannedCells = FullPlannedCells;
		InitialResult.Placements.Reset();
		InitialResult.ExportedEntryCells.Reset();
		InitialResult.PropagationStats = FLayoutSolverPropagationStats();
		InitialResult.RouteDomainFilterDiagnostics = FLayoutRouteDomainFilterDiagnostics{};

		FLayoutProfileSolveSnapshot AdjustedProfileSnapshot = ProfileSnapshot;
		for (const FLayoutPlannedCell& PlannedCell : FullPlannedCells)
		{
			AdjustedProfileSnapshot.LevelCount = FMath::Max(
				AdjustedProfileSnapshot.LevelCount,
				PlannedCell.Cell.Z + 1);
		}

		FSolveContext Context;
		ApplySolveSettings(
			Context,
			nullptr,
			&AdjustedProfileSnapshot,
			ExecutionSettings,
			Seed,
			InitialResult,
			bUseIndexedCandidateFiltering,
			ModuleCatalog);
		Context.RegionScope = RegionScope;
		Context.bUsesChildSolveContext = bUsesChildSolveContext;
		Context.bDeferFinalReachabilityAudit = bDeferFinalReachabilityAudit;
		Context.FrozenTerrainContract = FrozenTerrainContract;
		Context.bUsesSteppedTerrainContract = FrozenRequestContext != nullptr
			&& FrozenRequestContext->bHasSelectedModePlan
			&& FrozenRequestContext->SelectedModePlan.bUsesSteppedTerrainTopology;
		if (FrozenRequestContext != nullptr)
		{
			Context.OwningTopModuleLevelByXY = FrozenRequestContext->OwningTopModuleLevelByXY;
			Context.EffectiveSnapshotId = FrozenRequestContext->EffectiveSnapshotId;
			Context.RegionDebugPath = FrozenRequestContext->RegionDebugPath;
			Context.PrecommittedZoneFeatureProviderCommitments =
				FrozenRequestContext->PrecommittedZoneFeatureProviderCommitments;
			// Exact pre-proof domains remain authoritative while selected
			// VerticalAccess hosts and child modules solve in one CSP.
			InstallCandidateDomainRestrictionsFromRequest(
				Context,
				*FrozenRequestContext);
			LayoutZoneFeatureDemand::CompileDirectChildExternalProviderCapacity(
				FrozenRequestContext->EffectiveSnapshotId,
				FrozenRequestContext->ProfileSnapshot.ZoneFeatureRequirements,
				FrozenRequestContext->ContentSetSnapshot.Entries,
				Context.HardZoneFeatureExternalProviderCapacityByRequirementId);
		}
		if (FrozenTerrainContract != nullptr)
		{
			InstallTerrainBackedNeighborFaces(Context, *FrozenTerrainContract);
		}
		if (ExternalPlannedNeighborFaceMasks != nullptr)
		{
			Context.ExternalPlannedNeighborFaceMasks = *ExternalPlannedNeighborFaceMasks;
		}
		if (IncomingBoundaryPoints != nullptr)
		{
			Context.IncomingBoundaryPoints = *IncomingBoundaryPoints;
		}
		if (CommittedEndpointAnchors != nullptr)
		{
			Context.CommittedEndpointAnchors = *CommittedEndpointAnchors;
		}
		if (CommittedTraversalAnchors != nullptr)
		{
			Context.CommittedTraversalAnchors = *CommittedTraversalAnchors;
		}
		BuildOverridePlan(Context, FootprintSize, FullPlannedCells);

		const bool bSolved = SolvePreparedContext(
			Context,
			true,
			bValidateLiveCompositePlacementGuard);
		if (OutHasPreparedInitialDomainFailure != nullptr)
		{
			*OutHasPreparedInitialDomainFailure = Context.bHasPreparedInitialDomainFailure;
		}
		if (OutPreparedInitialDomainFailureCell != nullptr
			&& Context.bHasPreparedInitialDomainFailure)
		{
			*OutPreparedInitialDomainFailureCell = Context.PreparedInitialDomainFailureCell;
		}
		if (!bSolved)
		{
			RestoreBestPartialPlacements(Context);
			SortResultPlacements(Context.Result);
			Context.Result.bSucceeded = false;
			Context.Result.PropagationStats.CandidateAttemptCount = Context.CandidateAttemptCount;
			return Context.Result;
		}

		Context.Result.ExportedEntryCells.Reset();
		AppendContextPlacementsToResult(Context, Context.Result);
		SortResultPlacements(Context.Result);
		if (!Context.Result.PlannedCells.IsEmpty()
			&& Context.Result.Placements.IsEmpty())
		{
			int32 OccupiedContextCellCount = 0;
			int32 BundleRootCellCount = 0;
			for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& Pair : Context.Placements)
			{
				if (FSolveContext::IsOccupiedPlacement(Pair.Value))
				{
					++OccupiedContextCellCount;
					BundleRootCellCount += Pair.Value.bBundleRoot ? 1 : 0;
				}
			}
			Context.Result.bSucceeded = false;
			Context.Result.FailureReason = FString::Printf(
				TEXT("Unified vertical solve produced no public placement roots from %d occupied context cells (%d bundle roots, %d planned cells)."),
				OccupiedContextCellCount,
				BundleRootCellCount,
				Context.Result.PlannedCells.Num());
			return Context.Result;
		}
		Context.Result.bSucceeded = true;
		Context.Result.FailureReason.Reset();
		Context.Result.PropagationStats.CandidateAttemptCount = Context.CandidateAttemptCount;
		return Context.Result;
	}

	TArray<FLayoutSolveBoundaryPoint> BuildExportedBoundaryPointsFromSolveResult(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutSolveResult& SolveResult);

	struct FTerrainStageFrontierRecord
	{
		FIntVector FromCell = FIntVector::ZeroValue;
		FIntVector ToCell = FIntVector::ZeroValue;
		int32 LowerLevel = 0;
		int32 HigherLevel = 0;
		int32 FrontierId = INDEX_NONE;
	};

	static int32 ResolveSteppedTerrainSnappedLevelForSample(
		const FLayoutSteppedTerrainSupportMap& SupportMap,
		const FLayoutSteppedTerrainSupportSample& SupportSample)
	{
		if (SupportMap.SharedCellHeightInBlocks <= 0)
		{
			return 0;
		}

		const int32 SnappedCeilingZ = SupportSample.SnappedSupportCeilingZ != 0
			? SupportSample.SnappedSupportCeilingZ
			: SupportSample.SupportSurfaceZ;
		return FMath::FloorToInt(
			static_cast<float>(SnappedCeilingZ)
			/ static_cast<float>(SupportMap.SharedCellHeightInBlocks));
	}

	static TMap<FIntVector, int32> BuildSteppedTerrainSnappedLevelByCell(
		const FLayoutSteppedTerrainSupportMap& SupportMap)
	{
		TMap<FIntVector, int32> SnappedLevelByCell;
		SnappedLevelByCell.Reserve(SupportMap.SupportSamples.Num());
		for (const FLayoutSteppedTerrainSupportSample& SupportSample : SupportMap.SupportSamples)
		{
			SnappedLevelByCell.Add(
				SupportSample.LocalCell,
				ResolveSteppedTerrainSnappedLevelForSample(SupportMap, SupportSample));
		}
		return SnappedLevelByCell;
	}

	static bool IsLexicographicallyEarlierTerrainStageCell(
		const FIntVector& Left,
		const FIntVector& Right)
	{
		if (Left.Z != Right.Z)
		{
			return Left.Z < Right.Z;
		}
		if (Left.Y != Right.Y)
		{
			return Left.Y < Right.Y;
		}
		return Left.X < Right.X;
	}

	static int32 BuildDeterministicTerrainStageFrontierId(
		const FTerrainStageFrontierRecord& Frontier,
		const int32 FrontierIndex)
	{
		return static_cast<int32>(HashCombineFast(
			static_cast<uint32>(FrontierIndex + 1),
			HashCombineFast(
				GetTypeHash(Frontier.FromCell),
				HashCombineFast(
					GetTypeHash(Frontier.ToCell),
					HashCombineFast(
						static_cast<uint32>(Frontier.LowerLevel),
						static_cast<uint32>(Frontier.HigherLevel))))));
	}

	static TArray<FTerrainStageFrontierRecord> BuildTerrainStageFrontierRecords(
		const FLayoutSteppedTerrainSupportMap& SupportMap)
	{
		TArray<FTerrainStageFrontierRecord> Frontiers;
		const TMap<FIntVector, int32> SnappedLevelByCell =
			BuildSteppedTerrainSnappedLevelByCell(SupportMap);
		for (const FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep : SupportMap.AdjacencySteps)
		{
			if (AdjacencyStep.SnappedLevelDelta <= 0)
			{
				continue;
			}
			const int32* FromLevel = SnappedLevelByCell.Find(AdjacencyStep.FromCell);
			const int32* ToLevel = SnappedLevelByCell.Find(AdjacencyStep.ToCell);
			if (FromLevel == nullptr || ToLevel == nullptr || *FromLevel == *ToLevel)
			{
				continue;
			}
			FTerrainStageFrontierRecord& Frontier = Frontiers.AddDefaulted_GetRef();
			Frontier.FromCell = AdjacencyStep.FromCell;
			Frontier.ToCell = AdjacencyStep.ToCell;
			Frontier.LowerLevel = FMath::Min(*FromLevel, *ToLevel);
			Frontier.HigherLevel = FMath::Max(*FromLevel, *ToLevel);
		}
		Frontiers.Sort([](const FTerrainStageFrontierRecord& Left, const FTerrainStageFrontierRecord& Right)
		{
			if (Left.LowerLevel != Right.LowerLevel)
			{
				return Left.LowerLevel < Right.LowerLevel;
			}
			if (Left.HigherLevel != Right.HigherLevel)
			{
				return Left.HigherLevel < Right.HigherLevel;
			}
			if (Left.FromCell != Right.FromCell)
			{
				return IsLexicographicallyEarlierTerrainStageCell(Left.FromCell, Right.FromCell);
			}
			return IsLexicographicallyEarlierTerrainStageCell(Left.ToCell, Right.ToCell);
		});
		for (int32 FrontierIndex = 0; FrontierIndex < Frontiers.Num(); ++FrontierIndex)
		{
			Frontiers[FrontierIndex].FrontierId =
				BuildDeterministicTerrainStageFrontierId(Frontiers[FrontierIndex], FrontierIndex);
		}
		return Frontiers;
	}

	static void BuildTerrainStageCellClassification(
		const FLayoutSteppedTerrainSupportMap& SupportMap,
		TMap<FIntVector, int32>& OutSnappedLevelByCell,
		int32& OutInitialReachableLevel,
		TMap<FIntVector, int32>& OutFutureFrontierIdByCell)
	{
		OutSnappedLevelByCell = BuildSteppedTerrainSnappedLevelByCell(SupportMap);
		OutFutureFrontierIdByCell.Reset();
		OutInitialReachableLevel = INDEX_NONE;
		for (const TPair<FIntVector, int32>& Pair : OutSnappedLevelByCell)
		{
			OutInitialReachableLevel = OutInitialReachableLevel == INDEX_NONE
				? Pair.Value
				: FMath::Min(OutInitialReachableLevel, Pair.Value);
		}
		for (const FTerrainStageFrontierRecord& Frontier : BuildTerrainStageFrontierRecords(SupportMap))
		{
			const int32* FromLevel = OutSnappedLevelByCell.Find(Frontier.FromCell);
			const int32* ToLevel = OutSnappedLevelByCell.Find(Frontier.ToCell);
			if (FromLevel != nullptr && *FromLevel == Frontier.HigherLevel)
			{
				int32& FrontierId = OutFutureFrontierIdByCell.FindOrAdd(Frontier.FromCell, Frontier.FrontierId);
				FrontierId = FMath::Min(FrontierId, Frontier.FrontierId);
			}
			if (ToLevel != nullptr && *ToLevel == Frontier.HigherLevel)
			{
				int32& FrontierId = OutFutureFrontierIdByCell.FindOrAdd(Frontier.ToCell, Frontier.FrontierId);
				FrontierId = FMath::Min(FrontierId, Frontier.FrontierId);
			}
		}
	}

	static void PartitionBoundaryPointsByTerrainStageOwnership(
		const FLayoutRegionSolveRequest& Request,
		const TArray<FLayoutSolveBoundaryPoint>& BoundaryPoints,
		TArray<FLayoutSolveBoundaryPoint>& OutReachableBoundaryPoints,
		TArray<FLayoutSolveBoundaryPoint>& OutFutureTerraceBoundaryPoints)
	{
		OutReachableBoundaryPoints.Reset();
		OutFutureTerraceBoundaryPoints.Reset();
		if (Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks <= 0
			|| Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty())
		{
			OutReachableBoundaryPoints = BoundaryPoints;
			return;
		}

		TMap<FIntVector, int32> SnappedLevelByCell;
		TMap<FIntVector, int32> FutureFrontierIdByCell;
		int32 InitialReachableLevel = INDEX_NONE;
		BuildTerrainStageCellClassification(
			Request.SteppedTerrainSupportMap,
			SnappedLevelByCell,
			InitialReachableLevel,
			FutureFrontierIdByCell);
		if (InitialReachableLevel == INDEX_NONE)
		{
			OutReachableBoundaryPoints = BoundaryPoints;
			return;
		}

		for (FLayoutSolveBoundaryPoint BoundaryPoint : BoundaryPoints)
		{
			const int32* SnappedLevel = SnappedLevelByCell.Find(BoundaryPoint.LocalCell);
			const bool bDeferredTerrace = SnappedLevel != nullptr && *SnappedLevel > InitialReachableLevel;
			BoundaryPoint.bTerrainStageReachablePlateauOwned = !bDeferredTerrace;
			BoundaryPoint.bTerrainStageFutureTerraceProofOnly = bDeferredTerrace;
			BoundaryPoint.TerrainAscentFrontierId = bDeferredTerrace
				? FutureFrontierIdByCell.FindRef(BoundaryPoint.LocalCell)
				: INDEX_NONE;
			if (bDeferredTerrace)
			{
				OutFutureTerraceBoundaryPoints.Add(MoveTemp(BoundaryPoint));
			}
			else
			{
				OutReachableBoundaryPoints.Add(MoveTemp(BoundaryPoint));
			}
		}
	}

	static TArray<FLayoutClosureCoverageSegmentRecord> BuildFutureTerraceProofClosureSegments(
		const FLayoutRegionSolveRequest& Request,
		const TArray<FLayoutClosureCoverageSegmentRecord>& ClosureSegments)
	{
		TArray<FLayoutClosureCoverageSegmentRecord> FutureSegments;
		if (Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks <= 0
			|| Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty())
		{
			return FutureSegments;
		}
		TMap<FIntVector, int32> SnappedLevelByCell;
		TMap<FIntVector, int32> FutureFrontierIdByCell;
		int32 InitialReachableLevel = INDEX_NONE;
		BuildTerrainStageCellClassification(
			Request.SteppedTerrainSupportMap,
			SnappedLevelByCell,
			InitialReachableLevel,
			FutureFrontierIdByCell);
		for (const FLayoutClosureCoverageSegmentRecord& Segment : ClosureSegments)
		{
			const int32* SnappedLevel = SnappedLevelByCell.Find(Segment.Cell);
			if (SnappedLevel != nullptr && *SnappedLevel > InitialReachableLevel)
			{
				FutureSegments.Add(Segment);
			}
		}
		return FutureSegments;
	}

	static TArray<FLayoutSteppedTerrainStageDiagnostic> BuildTerrainStageDiagnostics(
		const FLayoutRegionSolveRequest& Request)
	{
		TArray<FLayoutSteppedTerrainStageDiagnostic> Diagnostics;
		if (Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks <= 0
			|| Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty())
		{
			return Diagnostics;
		}
		TMap<FIntVector, int32> SnappedLevelByCell;
		TMap<FIntVector, int32> FutureFrontierIdByCell;
		int32 InitialReachableLevel = INDEX_NONE;
		BuildTerrainStageCellClassification(
			Request.SteppedTerrainSupportMap,
			SnappedLevelByCell,
			InitialReachableLevel,
			FutureFrontierIdByCell);
		if (InitialReachableLevel == INDEX_NONE)
		{
			return Diagnostics;
		}
		FLayoutSteppedTerrainStageDiagnostic& InitialStage = Diagnostics.AddDefaulted_GetRef();
		InitialStage.StageIndex = 0;
		InitialStage.TerrainAscentFrontierId = INDEX_NONE;
		InitialStage.RegionDebugPath = Request.RegionDebugPath;
		InitialStage.SnappedTerrainLevel = InitialReachableLevel;
		InitialStage.bReachablePlateau = true;
		InitialStage.bDeferredTerrace = false;
		TArray<FTerrainStageFrontierRecord> Frontiers = BuildTerrainStageFrontierRecords(Request.SteppedTerrainSupportMap);
		Frontiers.Sort([](const FTerrainStageFrontierRecord& Left, const FTerrainStageFrontierRecord& Right)
		{
			if (Left.FrontierId != Right.FrontierId)
			{
				return Left.FrontierId < Right.FrontierId;
			}
			if (Left.HigherLevel != Right.HigherLevel)
			{
				return Left.HigherLevel < Right.HigherLevel;
			}
			return Left.LowerLevel < Right.LowerLevel;
		});
		for (const FTerrainStageFrontierRecord& Frontier : Frontiers)
		{
			FLayoutSteppedTerrainStageDiagnostic& Stage = Diagnostics.AddDefaulted_GetRef();
			Stage.StageIndex = Diagnostics.Num() - 1;
			Stage.TerrainAscentFrontierId = Frontier.FrontierId;
			Stage.RegionDebugPath = Request.RegionDebugPath;
			Stage.SnappedTerrainLevel = Frontier.HigherLevel;
			Stage.bReachablePlateau = false;
			Stage.bDeferredTerrace = true;
		}
		return Diagnostics;
	}

	static TArray<FLayoutSteppedTerrainFrontierOwnershipDiagnostic> BuildTerrainFrontierOwnershipDiagnostics(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutSolveResult* SolveResult,
		const TArray<FLayoutNegotiatedChildResponsibilityContract>* NegotiatedContracts = nullptr)
	{
		TArray<FLayoutSteppedTerrainFrontierOwnershipDiagnostic> Diagnostics;
		if (Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks <= 0
			|| Request.SteppedTerrainSupportMap.SupportSamples.IsEmpty())
		{
			return Diagnostics;
		}
		for (const FTerrainStageFrontierRecord& Frontier : BuildTerrainStageFrontierRecords(Request.SteppedTerrainSupportMap))
		{
			FLayoutSteppedTerrainFrontierOwnershipDiagnostic& Diagnostic =
				Diagnostics.AddDefaulted_GetRef();
			Diagnostic.TerrainAscentFrontierId = Frontier.FrontierId;
			Diagnostic.RegionDebugPath = Request.RegionDebugPath;
			Diagnostic.LowerSnappedTerrainLevel = Frontier.LowerLevel;
			Diagnostic.HigherSnappedTerrainLevel = Frontier.HigherLevel;
			Diagnostic.ProtectedAscentPocketCells = { Frontier.FromCell, Frontier.ToCell };
			Diagnostic.bBlocked = true;
			Diagnostic.bHasSupportingVerticalAccess = false;

			if (SolveResult != nullptr)
			{
				for (const FLayoutPlacedModule& Placement : SolveResult->Placements)
				{
					if (Placement.Intent != ELayoutCellIntent::VerticalAccess)
					{
						continue;
					}
					if (Placement.Cell == Frontier.FromCell || Placement.Cell == Frontier.ToCell)
					{
						Diagnostic.ParentOwnedVerticalAccessCells.AddUnique(Placement.Cell);
					}
				}
			}

			if (NegotiatedContracts != nullptr)
			{
				for (const FLayoutNegotiatedChildResponsibilityContract& Contract : *NegotiatedContracts)
				{
					for (const FLayoutNegotiatedHostVerticalAccessFrontierResponsibility& FrontierResponsibility :
						Contract.HostVerticalAccessFrontierResponsibilities)
					{
						if (FrontierResponsibility.TerrainAscentFrontierId != Frontier.FrontierId)
						{
							continue;
						}
						Diagnostic.ParentOwnedVerticalAccessCells.Append(
							FrontierResponsibility.CountedParentVerticalAccessCells);
						Diagnostic.ChildOwnedRegionDebugPaths.Append(
							FrontierResponsibility.CountedChildProviderRegionDebugPaths);
					}
				}
			}

			Diagnostic.ParentOwnedVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return IsLexicographicallyEarlierTerrainStageCell(Left, Right);
			});
			for (int32 CellIndex = Diagnostic.ParentOwnedVerticalAccessCells.Num() - 1; CellIndex > 0; --CellIndex)
			{
				if (Diagnostic.ParentOwnedVerticalAccessCells[CellIndex] == Diagnostic.ParentOwnedVerticalAccessCells[CellIndex - 1])
				{
					Diagnostic.ParentOwnedVerticalAccessCells.RemoveAt(CellIndex);
				}
			}
			Diagnostic.ChildOwnedRegionDebugPaths.Sort();
			for (int32 RegionIndex = Diagnostic.ChildOwnedRegionDebugPaths.Num() - 1; RegionIndex > 0; --RegionIndex)
			{
				if (Diagnostic.ChildOwnedRegionDebugPaths[RegionIndex] == Diagnostic.ChildOwnedRegionDebugPaths[RegionIndex - 1])
				{
					Diagnostic.ChildOwnedRegionDebugPaths.RemoveAt(RegionIndex);
				}
			}
			Diagnostic.bHasSupportingVerticalAccess =
				!Diagnostic.ParentOwnedVerticalAccessCells.IsEmpty()
				|| !Diagnostic.ChildOwnedRegionDebugPaths.IsEmpty();
			Diagnostic.bBlocked = !Diagnostic.bHasSupportingVerticalAccess;
		}
		return Diagnostics;
	}

	static void ApplyStagedTerrainArtifactsToRegionResult(
		const FLayoutRegionSolveRequest& Request,
		FLayoutRegionSolveResult& InOutRegionResult,
		const TArray<FLayoutNegotiatedChildResponsibilityContract>* NegotiatedContracts = nullptr)
	{
		TArray<FLayoutSolveBoundaryPoint> FullBoundaryPoints =
			BuildExportedBoundaryPointsFromSolveResult(Request, InOutRegionResult.SolveResult);
		PartitionBoundaryPointsByTerrainStageOwnership(
			Request,
			FullBoundaryPoints,
			InOutRegionResult.ExportedBoundaryPoints,
			InOutRegionResult.FutureTerraceProofBoundaryPoints);
		InOutRegionResult.SolveResult.TerrainStageDiagnostics =
			BuildTerrainStageDiagnostics(Request);
		InOutRegionResult.TerrainStageDiagnostics =
			InOutRegionResult.SolveResult.TerrainStageDiagnostics;
		InOutRegionResult.SolveResult.TerrainFrontierOwnershipDiagnostics =
			BuildTerrainFrontierOwnershipDiagnostics(Request, &InOutRegionResult.SolveResult, NegotiatedContracts);
		InOutRegionResult.TerrainFrontierOwnershipDiagnostics =
			InOutRegionResult.SolveResult.TerrainFrontierOwnershipDiagnostics;
		InOutRegionResult.SolveResult.FutureTerraceProofClosureSegments =
			BuildFutureTerraceProofClosureSegments(
				Request,
				InOutRegionResult.SolveResult.ClosureSegments);
		InOutRegionResult.FutureTerraceProofClosureSegments =
			InOutRegionResult.SolveResult.FutureTerraceProofClosureSegments;
		InOutRegionResult.SolveResult.PropagationStats.TerrainStageCount =
			InOutRegionResult.SolveResult.TerrainStageDiagnostics.Num();
		InOutRegionResult.SolveResult.PropagationStats.TerrainBlockedFrontierCount =
			InOutRegionResult.SolveResult.TerrainFrontierOwnershipDiagnostics.FilterByPredicate(
				[](const FLayoutSteppedTerrainFrontierOwnershipDiagnostic& Diagnostic)
				{
					return Diagnostic.bBlocked;
				}).Num();
	}

	TArray<FLayoutSolveBoundaryPoint> BuildExportedBoundaryPointsFromSolveResult(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutSolveResult& SolveResult)
	{
		TArray<FLayoutSolveBoundaryPoint> BoundaryPoints;
		if (!SolveResult.bSucceeded)
		{
			return BoundaryPoints;
		}

		TSet<FIntVector> FilledCells;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			if (Placement.Module != nullptr || Placement.CompositeModule != nullptr || !Placement.OccupiedLocalCells.IsEmpty())
			{
				if (const FLayoutModuleSolveSnapshot* ModuleSnapshot = LayoutProfileSolverInternal::FindModuleSnapshotForPlacedModule(Request, Placement))
				{
					for (const FIntVector& WorldCell : BuildPlacementWorldOccupiedCells(Placement, ModuleSnapshot))
					{
						FilledCells.Add(WorldCell);
					}
				}
				else
				{
					FilledCells.Add(Placement.Cell);
				}
			}
		}

		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			const FLayoutModuleSolveSnapshot* ModuleSnapshot = LayoutProfileSolverInternal::FindModuleSnapshotForPlacedModule(Request, Placement);
			if (ModuleSnapshot == nullptr)
			{
				continue;
			}

			const FIntPoint RotationFootprint(ModuleSnapshot->BoundsCells.X, ModuleSnapshot->BoundsCells.Y);

			for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : ModuleSnapshot->GeneratedLocalCellFaceRules)
			{
				const bool bUsesExplicitBoundaryFacing = CellSnapshot.ExposedFaceRules.ContainsByPredicate(
					[](const FLayoutFaceRule& FaceRule)
					{
						return FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceExterior;
					});
				const FIntVector WorldCell = Placement.Cell + RotateCellInFootprintYaw(
					CellSnapshot.LocalCell,
					RotationFootprint,
					Placement.YawRotationSteps);

				for (const FLayoutFaceRule& LocalFaceRule : CellSnapshot.ExposedFaceRules)
				{
					FLayoutFaceRule WorldFaceRule = LocalFaceRule;
					WorldFaceRule.Direction = FLayoutDirectionUtils::RotateYaw(LocalFaceRule.Direction, Placement.YawRotationSteps);

					if (WorldFaceRule.Direction != ELayoutFaceDirection::PosZ
						&& WorldFaceRule.Direction != ELayoutFaceDirection::NegZ
						&& bUsesExplicitBoundaryFacing
						&& WorldFaceRule.BoundaryRequirement != ELayoutFaceBoundaryRequirement::MustFaceExterior)
					{
						continue;
					}

					const FIntVector NeighborCell = WorldCell + FLayoutDirectionUtils::ToCellDelta(WorldFaceRule.Direction);
					if (FilledCells.Contains(NeighborCell))
					{
						continue;
					}

					FLayoutSolveBoundaryPoint& BoundaryPoint = BoundaryPoints.AddDefaulted_GetRef();
					BoundaryPoint.LocalCell = WorldCell;
					BoundaryPoint.FaceDirection = WorldFaceRule.Direction;
					BoundaryPoint.ConnectionTag = WorldFaceRule.GetEffectiveConnectionTag();
					BoundaryPoint.AllowedConnectionTags = WorldFaceRule.GetEffectiveAllowedConnectionTags();
					BoundaryPoint.ConnectedTraversalChannels = WorldFaceRule.ConnectedTraversalChannels;
					BoundaryPoint.bRequireMatchingYawWithFilledNeighbor =
						(WorldFaceRule.Direction == ELayoutFaceDirection::PosZ || WorldFaceRule.Direction == ELayoutFaceDirection::NegZ)
						&& WorldFaceRule.bRequireMatchingYawWithFilledNeighbor;
					BoundaryPoint.SourceRegionDebugPath = Request.RegionDebugPath;
					BoundaryPoint.SourceCell = Placement.Cell;
					BoundaryPoint.SourceYawRotationSteps = Placement.YawRotationSteps;
				}
			}
		}

		return BoundaryPoints;
	}

	bool BoundaryPointTouchesSuppliedPlannedCells(
		const FLayoutSolveBoundaryPoint& BoundaryPoint,
		const FLayoutRegionSolveRequest& Request)
	{
		if (Request.PlannedCells.IsEmpty())
		{
			return true;
		}

		const FIntVector DependentCell = BoundaryPoint.LocalCell + FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection);
		for (const FLayoutPlannedCell& PlannedCell : Request.PlannedCells)
		{
			if (PlannedCell.Cell == DependentCell)
			{
				return true;
			}
		}

		return false;
	}

	bool AreEquivalentBoundaryPoints(
		const FLayoutSolveBoundaryPoint& Left,
		const FLayoutSolveBoundaryPoint& Right)
	{
		return Left.LocalCell == Right.LocalCell
			&& Left.FaceDirection == Right.FaceDirection
			&& Left.ConnectionTag == Right.ConnectionTag
			&& Left.AllowedConnectionTags == Right.AllowedConnectionTags
			&& Left.ConnectedTraversalChannels == Right.ConnectedTraversalChannels
			&& Left.bRepresentsFilledNeighbor == Right.bRepresentsFilledNeighbor
			&& Left.bRequiresBoundaryFacing == Right.bRequiresBoundaryFacing
			&& Left.bRequireMatchingYawWithFilledNeighbor
				== Right.bRequireMatchingYawWithFilledNeighbor
			&& Left.SourceCell == Right.SourceCell
			&& Left.SourceYawRotationSteps == Right.SourceYawRotationSteps
			&& Left.CommitmentId == Right.CommitmentId
			&& Left.bTerrainStageReachablePlateauOwned
				== Right.bTerrainStageReachablePlateauOwned
			&& Left.bTerrainStageFutureTerraceProofOnly
				== Right.bTerrainStageFutureTerraceProofOnly
			&& Left.TerrainAscentFrontierId == Right.TerrainAscentFrontierId;
	}

	TArray<FLayoutSolveBoundaryPoint> BuildExportedBoundaryPointsFromContext(
		const FSolveContext& Context,
		const FString& RegionDebugPath)
	{
		TArray<FLayoutSolveBoundaryPoint> BoundaryPoints;

		TSet<FIntVector> FilledCells;
		auto AccumulateFilledBundleCells =
			[&Context, &FilledCells](
				const TPair<FIntVector, FSolveContext::FSolvePlacement>&
					PlacementPair)
		{
			if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value)
				|| !IsBundleRootSolvePlacement(
					PlacementPair.Key,
					PlacementPair.Value))
			{
				return;
			}

			if (const FLayoutModuleSolveSnapshot* ModuleSnapshot =
					FindVariantModuleSnapshot(
						Context,
						PlacementPair.Value.VariantIndex))
			{
				for (const FIntVector& WorldCell :
					LayoutPlacementOccupancy::BuildSnapshotWorldOccupiedCells(
						ModuleSnapshot,
						PlacementPair.Key,
						PlacementPair.Value.YawRotationSteps))
				{
					FilledCells.Add(WorldCell);
				}
				return;
			}

			FilledCells.Add(PlacementPair.Key);
		};

		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>&
			PlacementPair : Context.Placements)
		{
			AccumulateFilledBundleCells(PlacementPair);
		}
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>&
			PlacementPair : Context.FixedNeighborPlacements)
		{
			AccumulateFilledBundleCells(PlacementPair);
		}

		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>&
			PlacementPair : Context.Placements)
		{
			if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value)
				|| !IsBundleRootSolvePlacement(
					PlacementPair.Key,
					PlacementPair.Value))
			{
				continue;
			}

			const FLayoutModuleSolveSnapshot* ModuleSnapshot =
				FindVariantModuleSnapshot(
					Context,
					PlacementPair.Value.VariantIndex);
			if (ModuleSnapshot == nullptr)
			{
				continue;
			}

			const FIntPoint RotationFootprint(
				ModuleSnapshot->BoundsCells.X,
				ModuleSnapshot->BoundsCells.Y);
			for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot :
				ModuleSnapshot->GeneratedLocalCellFaceRules)
			{
				const bool bUsesExplicitBoundaryFacing =
					CellSnapshot.ExposedFaceRules.ContainsByPredicate(
						[](const FLayoutFaceRule& FaceRule)
						{
							return FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceExterior;
						});
				const FIntVector WorldCell =
					PlacementPair.Key
					+ RotateCellInFootprintYaw(
						CellSnapshot.LocalCell,
						RotationFootprint,
						PlacementPair.Value.YawRotationSteps);

				for (const FLayoutFaceRule& LocalFaceRule :
					CellSnapshot.ExposedFaceRules)
				{
					FLayoutFaceRule WorldFaceRule = LocalFaceRule;
					WorldFaceRule.Direction = FLayoutDirectionUtils::RotateYaw(
						LocalFaceRule.Direction,
						PlacementPair.Value.YawRotationSteps);

					if (WorldFaceRule.Direction
							!= ELayoutFaceDirection::PosZ
						&& WorldFaceRule.Direction
							!= ELayoutFaceDirection::NegZ
						&& bUsesExplicitBoundaryFacing
						&& WorldFaceRule.BoundaryRequirement != ELayoutFaceBoundaryRequirement::MustFaceExterior)
					{
						continue;
					}

					const FIntVector NeighborCell =
						WorldCell
						+ FLayoutDirectionUtils::ToCellDelta(
							WorldFaceRule.Direction);
					if (FilledCells.Contains(NeighborCell))
					{
						continue;
					}

					FLayoutSolveBoundaryPoint& BoundaryPoint =
						BoundaryPoints.AddDefaulted_GetRef();
					BoundaryPoint.LocalCell = WorldCell;
					BoundaryPoint.FaceDirection = WorldFaceRule.Direction;
					BoundaryPoint.ConnectionTag =
						WorldFaceRule.GetEffectiveConnectionTag();
					BoundaryPoint.AllowedConnectionTags =
						WorldFaceRule.GetEffectiveAllowedConnectionTags();
					BoundaryPoint.ConnectedTraversalChannels =
						WorldFaceRule.ConnectedTraversalChannels;
					BoundaryPoint.bRequireMatchingYawWithFilledNeighbor =
						(WorldFaceRule.Direction
								== ELayoutFaceDirection::PosZ
							|| WorldFaceRule.Direction
								== ELayoutFaceDirection::NegZ)
						&& WorldFaceRule.bRequireMatchingYawWithFilledNeighbor;
					BoundaryPoint.SourceRegionDebugPath = RegionDebugPath;
					BoundaryPoint.SourceCell = PlacementPair.Key;
					BoundaryPoint.SourceYawRotationSteps =
						PlacementPair.Value.YawRotationSteps;
				}
			}
		}

		return BoundaryPoints;
	}


}

bool LayoutProfileSolverInternal::ApplySparsePlacementRulesToSolveResult(
	const FLayoutProfileSolveSnapshot& ProfileSnapshot,
	const FString& RegionDebugPath,
	const int32 Seed,
	FLayoutSolveResult& SolveResult,
	FString& OutFailureReason)
{
	return ApplySparsePlacementRulesToResultInternal(ProfileSnapshot, RegionDebugPath, Seed, SolveResult, OutFailureReason);
}

TArray<FLayoutSolveBoundaryPoint> LayoutProfileSolverInternal::BuildExportedBoundaryPointsFromSolveResultForTests(
	const FLayoutRegionSolveRequest& Request,
	const FLayoutSolveResult& SolveResult)
{
	return BuildExportedBoundaryPointsFromSolveResult(Request, SolveResult);
}

bool LayoutProfileSolverInternal::IsOccupiedPlacedModule(const FLayoutPlacedModule& Placement)
{
	return Placement.Module != nullptr || Placement.CompositeModule != nullptr || !Placement.OccupiedLocalCells.IsEmpty();
}

const FLayoutModuleSolveSnapshot* LayoutProfileSolverInternal::FindModuleSnapshotForPlacedModule(
	const FLayoutRegionSolveRequest& Request,
	const FLayoutPlacedModule& Placement)
{
	if (Request.ModuleCatalog.Modules.IsValidIndex(Placement.ModuleSnapshotIndex))
	{
		const FLayoutModuleSolveSnapshot& IndexedSnapshot =
			Request.ModuleCatalog.Modules[Placement.ModuleSnapshotIndex];
		if (Placement.ModuleSnapshotId.IsNone()
			|| IndexedSnapshot.SnapshotId == Placement.ModuleSnapshotId)
		{
			return &IndexedSnapshot;
		}
	}
	if (!Placement.ModuleSnapshotId.IsNone())
	{
		if (const FLayoutModuleSolveSnapshot* SnapshotById =
			Request.ModuleCatalog.Modules.FindByPredicate(
				[&Placement](const FLayoutModuleSolveSnapshot& ModuleSnapshot)
				{
					return ModuleSnapshot.SnapshotId == Placement.ModuleSnapshotId;
				}))
		{
			return SnapshotById;
		}
	}
	if (!Placement.SourceContentEntryId.IsNone())
	{
		if (const FLayoutRegionContentEntrySolveSnapshot* EntrySnapshot =
			FindContentSetEntrySnapshotById(Request.ContentSetSnapshot, Placement.SourceContentEntryId))
		{
			if (Request.ModuleCatalog.Modules.IsValidIndex(EntrySnapshot->ModuleSnapshotIndex))
			{
				return &Request.ModuleCatalog.Modules[EntrySnapshot->ModuleSnapshotIndex];
			}
		}
	}

	return Request.ModuleCatalog.Modules.FindByPredicate(
		[&Placement](const FLayoutModuleSolveSnapshot& ModuleSnapshot)
		{
			const bool bMatchesLiveModule = Placement.Module != nullptr && ModuleSnapshot.SourceModule == Placement.Module;
			const bool bMatchesComposite = Placement.CompositeModule != nullptr && ModuleSnapshot.SourceCompositeModule == Placement.CompositeModule;
			if (!bMatchesLiveModule && !bMatchesComposite)
			{
				return false;
			}

			return Placement.OccupiedLocalCells.IsEmpty() || ModuleSnapshot.OccupiedLocalCells == Placement.OccupiedLocalCells;
		});
}

TMap<FIntVector, FSolveContext::FSolvePlacement> LayoutProfileSolverInternal::BuildVerticalContinuationFixedNeighborsForTests(
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const TMap<FIntVector, FSolveContext::FSolvePlacement>& SolvedPlacements,
	const int32 CurrentLevel,
	const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots)
{
	const TArray<FLayoutPlannedCell> LevelPlannedCells =
		FilterPlannedCellsForLevel(
			PlannedCells,
			CurrentLevel);
	const TSet<FIntVector> FullPlannedCellSet =
		BuildPlannedCellSet(PlannedCells);
	const TSet<FIntVector> LevelPlannedCellSet =
		BuildPlannedCellSet(LevelPlannedCells);
	return BuildVerticalContinuationFixedNeighbors(
		FullPlannedCellSet,
		LevelPlannedCellSet,
		SolvedPlacements,
		CurrentLevel,
		ModuleSnapshots.IsEmpty() ? nullptr : &ModuleSnapshots);
}

	TMap<FIntVector, uint8>
LayoutProfileSolverInternal::BuildVerticalContinuationUpwardContinuationFaceMasksForTests(
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const int32 CurrentLevel)
{
	TMap<FIntVector, uint8> Result;
	const TSet<FIntVector> PlannedCellSet =
		BuildPlannedCellSet(PlannedCells);
	const TSet<FIntVector> LevelPlannedCellSet =
		BuildPlannedCellSet(
			FilterPlannedCellsForLevel(
				PlannedCells,
				CurrentLevel));
	AddVerticalContinuationExternalPlannedNeighborFaces(
		LevelPlannedCellSet,
		PlannedCellSet,
		Result);
	return Result;
}

TSet<FGameplayTag> LayoutProfileSolverInternal::BuildNextLevelActiveWalkableAreasForTests(
	const FSolveContext& LevelContext,
	const TSet<FGameplayTag>& CurrentLevelActiveAreas,
	const int32 CurrentLevel)
{
	return BuildNextLevelActiveWalkableAreas(
		LevelContext,
		CurrentLevelActiveAreas,
		CurrentLevel);
}

int32 LayoutProfileSolverInternal::CountViableVerticalAccessOrientationsAtCellForTests(
	const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots,
	const FLayoutProfileSolveSnapshot& Profile,
	const FIntPoint& FootprintSize,
	const TSet<FIntVector>& EntryCells,
	const FIntVector& Cell)
{
	return CountViableVerticalAccessOrientationsAtCell(ModuleSnapshots, Profile, FootprintSize, EntryCells, Cell);
}

bool LayoutProfileSolverInternal::DoesPreparedVerticalAccessHostAdmitCandidatePair(
	const FLayoutRegionSolveRequest& Request,
	const TArray<FLayoutPlannedCell>& SourceCells,
	const FIntVector& LowerCell,
	FString& OutFailureReason)
{
	return DoesVerticalAccessHostAdmitCandidatePair(
		Request,
		Request.FootprintSize,
		SourceCells,
		LowerCell,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
	const FLayoutRegionSolveRequest& Request,
	const FIntPoint& FootprintSize,
	const TArray<FLayoutPlannedCell>& SourceCells,
	const FIntVector& LowerCell,
	FString& OutFailureReason)
{
	FLayoutRegionSolveRequest SizedRequest = Request;
	SizedRequest.FootprintSize = FootprintSize;
	return DoesPreparedVerticalAccessHostAdmitCandidatePair(
		SizedRequest,
		SourceCells,
		LowerCell,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
	const FLayoutRegionSolveRequest& Request,
	TArray<FLayoutPlannedCell>& InOutPlannedCells,
	TArray<FLayoutVerticalAccessHostGroup>& OutHostGroups,
	FString& OutFailureReason)
{
	return ApplyVerticalAccessPlanning(
		Request,
		Request.FootprintSize,
		InOutPlannedCells,
		OutHostGroups,
		OutFailureReason,
		nullptr);
}

bool LayoutProfileSolverInternal::ApplyVerticalAccessPlanningForTests(
	const FLayoutRegionSolveRequest& Request,
	TArray<FLayoutPlannedCell>& InOutPlannedCells,
	TArray<FLayoutVerticalAccessHostGroup>& OutHostGroups,
	FString& OutFailureReason)
{
	return RebuildVerticalAccessPlanningForPreparedTopology(
		Request,
		InOutPlannedCells,
		OutHostGroups,
		OutFailureReason);
}

void LayoutProfileSolverInternal::BuildOrientedVariantsForTests(FSolveContext& Context)
{
	BuildOrientedVariants(Context);
}

bool LayoutProfileSolverInternal::PrepareSolveContextThroughRouteDomainStageForTests(
	FSolveContext& Context)
{
	return PrepareSolveContextThroughRouteDomainStage(Context);
}

void LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(
	FSolveContext& Context)
{
	RefreshCompiledFaceInterfaces(Context);
}

bool LayoutProfileSolverInternal::TryBuildSparseStructuralLocalSolveView(
	const FSolveContext& OwningContext,
	const TSet<FIntVector>& LocalWorkCells,
	FSolveContext& OutLocalContext,
	FString& OutFailureReason,
	const bool bReusePreparedVariants,
	const bool bPrepareIndexedSearch,
	const bool bPrepareDomains)
{
	check(bPrepareDomains || !bPrepareIndexedSearch);
	OutFailureReason.Reset();
	if (LocalWorkCells.IsEmpty())
	{
		OutFailureReason = TEXT("Sparse structural local solve requires at least one work cell.");
		return false;
	}

	OutLocalContext = OwningContext;
	if (OutLocalContext.OwningTerrainResidualRuleIdByCell.IsEmpty())
	{
		OutLocalContext.OwningTerrainResidualRuleIdByCell = OwningContext.TerrainResidualRuleIdByCell;
	}
	// A previous crop temporarily promoted its work cells. Re-cropping must restore
	// preserved-empty authority for cells omitted by the new assignment.
	OutLocalContext.TerrainResidualRuleIdByCell = OutLocalContext.OwningTerrainResidualRuleIdByCell;
	OutLocalContext.Result.bSucceeded = false;
	OutLocalContext.Result.Placements.Reset();
	OutLocalContext.Result.PlannedCells.Reset();
	OutLocalContext.PlannedCellIntents.Reset();
	OutLocalContext.SolveOrder.Reset();
	OutLocalContext.FinalizedCellsByPhysicalCell.Reset();
	// Keep committed occupancy, including composite shadows outside local work.
	// Cropping changes search scope, never ownership of already selected cells.
	OutLocalContext.BestPartialPlacements.Reset();
	OutLocalContext.InitialDomains.Reset();
	OutLocalContext.InitialDomainAdmissionFailuresByCell.Reset();
	OutLocalContext.IndexedOrderedDomainCandidatesByCell.Reset();
	OutLocalContext.IndexedDomainBitsByCell.Reset();

	for (const FIntVector& Cell : LocalWorkCells)
	{
		if (!ConsumeSparseStructuralWork(OutLocalContext))
		{
			OutFailureReason = OutLocalContext.Result.FailureReason;
			return false;
		}
		const FLayoutPlannedCell* OwningCell =
			OwningContext.OwningTopologyCellsByPhysicalCell.Find(Cell);
		if (OwningCell == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("Sparse structural local work cell %s is outside canonical owning-region topology."),
				*Cell.ToString());
			return false;
		}

		const int32 LocalIndex = OutLocalContext.Result.PlannedCells.Add(*OwningCell);
		OutLocalContext.PlannedCellIntents.Add(Cell, OwningCell->Intent);
		OutLocalContext.SolveOrder.Add(Cell);
		OutLocalContext.TerrainResidualRuleIdByCell.Remove(Cell);
		if (const FLayoutFinalizedCellViewRecord* OwningView =
				OwningContext.FinalizedCellsByPhysicalCell.Find(Cell))
		{
			FLayoutFinalizedCellViewRecord LocalView = *OwningView;
			LocalView.PlannedCellIndex = LocalIndex;
			OutLocalContext.FinalizedCellsByPhysicalCell.Add(Cell, MoveTemp(LocalView));
		}
	}

	OutLocalContext.SolveOrder.Sort([](const FIntVector& Left, const FIntVector& Right)
	{
		if (Left.Z != Right.Z) return Left.Z < Right.Z;
		if (Left.Y != Right.Y) return Left.Y < Right.Y;
		return Left.X < Right.X;
	});
	OutLocalContext.bTreatTerrainResidualsAsSettledEmpty = true;
	RefreshCompiledFaceInterfaces(OutLocalContext);
	if (!bReusePreparedVariants || OutLocalContext.Variants.IsEmpty()) BuildOrientedVariants(OutLocalContext);
	else if (auto* Ledger = LayoutSolveExecution::CurrentThreadLedger()) ++Ledger->VariantReuses;
	if (bPrepareDomains) BuildInitialDomains(OutLocalContext);
	if (bPrepareIndexedSearch)
	{
		InitializeIndexedCandidateUniverse(OutLocalContext);
		BuildIndexedCompatibilityRows(OutLocalContext);
	}
	if (OutLocalContext.bTimeBudgetExceeded || !OutLocalContext.Result.FailureReason.IsEmpty())
	{
		OutFailureReason = OutLocalContext.Result.FailureReason.IsEmpty()
			? TEXT("Sparse local domain preparation exhausted its shared budget.")
			: OutLocalContext.Result.FailureReason;
		return false;
	}
	return true;
}

bool LayoutProfileSolverInternal::TryImportSparseStructuralChildProof(
	FSolveContext& ParentContext,
	const FLayoutRegionSolveRequest& ChildRequest,
	const FLayoutRegionSolveResult& ChildProof,
	TArray<FSparseStructuralTraversalPort>& InOutPorts,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	auto Charge = [&]()
	{
		if (ConsumeSparseStructuralWork(ParentContext)) return true;
		OutFailureReason = ParentContext.Result.FailureReason;
		return false;
	};
	if (!Charge()) return false;
	if (!ChildProof.SolveResult.bSucceeded || ChildProof.bDroppedAsOptionalChild
		|| ChildProof.RegionDebugPath != ChildRequest.RegionDebugPath
		|| ChildProof.RegionCellOffset != ChildRequest.RegionCellOffset
		|| ChildProof.SolveResult.PlannedCells.IsEmpty() || ChildProof.CommittedEndpointAnchors.IsEmpty())
	{
		OutFailureReason = TEXT("Sparse child import requires an accepted request-matched placement proof with exact endpoints.");
		return false;
	}
	FSolveContext Trial = ParentContext;
	FSolveContext ChildContext;
	ChildContext.ProfileSnapshot = ChildRequest.ProfileSnapshot;
	ChildContext.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	ChildContext.ModuleSnapshots = ChildRequest.ModuleCatalog.Modules;
	for (const FLayoutModuleSolveSnapshot& Snapshot : ChildContext.ModuleSnapshots)
	{
		if (!Charge()) return false;
		if (!Trial.ModuleSnapshots.ContainsByPredicate([&](const FLayoutModuleSolveSnapshot& Existing)
			{ return Existing.SnapshotId == Snapshot.SnapshotId; }))
		{
			Trial.ModuleSnapshots.Add(Snapshot);
			Trial.FixedOnlyModuleSnapshotIds.Add(Snapshot.SnapshotId);
		}
	}
	BuildOrientedVariants(ChildContext);
	BuildOrientedVariants(Trial);
	SeedSparseFixedNeighborPlacements(ChildProof.SolveResult, ChildContext);
	ChildContext.Placements = MoveTemp(ChildContext.FixedNeighborPlacements);
	for (TPair<FIntVector, FSolveContext::FSolvePlacement>& Pair : ChildContext.Placements)
	{
		if (!Charge()) return false;
		Pair.Value.VariantIndex = ChildContext.Variants.IndexOfByPredicate([&](const FSolveContext::FOrientedModuleVariant& Variant)
			{ return Variant.ModuleSnapshotId == Pair.Value.ModuleSnapshotId && Variant.YawRotationSteps == Pair.Value.YawRotationSteps; });
		if (Pair.Value.VariantIndex == INDEX_NONE || !ChildProof.SolveResult.PlannedCells.ContainsByPredicate(
			[&](const FLayoutPlannedCell& Cell) { return Cell.Cell == Pair.Key; }))
		{
			OutFailureReason = TEXT("Accepted child placement is outside its proved topology or frozen module/yaw catalog.");
			return false;
		}
	}
	TSet<FIntVector> ReservedCells;
	for (const FLayoutPlannedCell& ChildCell : ChildProof.SolveResult.PlannedCells)
	{
		if (!Charge()) return false;
		const FIntVector Cell = ChildCell.Cell + ChildProof.RegionCellOffset;
		if (!Trial.OwningTopologyCellsByPhysicalCell.Contains(Cell) || Trial.ChildReservationCells.Contains(Cell)
			|| Trial.Placements.Contains(Cell) || Trial.FixedNeighborPlacements.Contains(Cell)
			|| InOutPorts.ContainsByPredicate([&](const FSparseStructuralTraversalPort& Port)
				{ return Port.Cell == Cell || Port.ClaimedCells.Contains(Cell) || Port.RequiredSupportCells.Contains(Cell)
					|| (Port.RequiredClearanceCells.Contains(Cell) && ChildContext.Placements.Contains(ChildCell.Cell)); }))
		{
			OutFailureReason = FString::Printf(TEXT("Sparse child replacement overlaps a selected claim or leaves owning topology at %s."), *Cell.ToString());
			return false;
		}
		ReservedCells.Add(Cell);
		Trial.ChildReservationCells.Add(Cell);
		Trial.PlannedCellIntents.Remove(Cell);
		Trial.TerrainResidualRuleIdByCell.Remove(Cell);
		FSolveContext::FSolvePlacement Fixed;
		if (const FSolveContext::FSolvePlacement* ChildPlacement = ChildContext.Placements.Find(ChildCell.Cell))
		{
			Fixed = *ChildPlacement;
			Fixed.BundleRootCell += ChildProof.RegionCellOffset;
			Fixed.ModuleSnapshotIndex = Trial.ModuleSnapshots.IndexOfByPredicate([&](const FLayoutModuleSolveSnapshot& Snapshot)
				{ return Snapshot.SnapshotId == Fixed.ModuleSnapshotId; });
			Fixed.VariantIndex = Trial.Variants.IndexOfByPredicate([&](const FSolveContext::FOrientedModuleVariant& Variant)
				{ return Variant.ModuleSnapshotId == Fixed.ModuleSnapshotId && Variant.YawRotationSteps == Fixed.YawRotationSteps; });
		}
		else Fixed.bEmpty = true;
		Trial.FixedNeighborPlacements.Add(Cell, Fixed);
	}
	Trial.Result.PlannedCells.RemoveAll([&](const FLayoutPlannedCell& Cell) { return ReservedCells.Contains(Cell.Cell); });
	Trial.SolveOrder.RemoveAll([&](const FIntVector& Cell) { return ReservedCells.Contains(Cell); });
	for (FLayoutSolveBoundaryPoint Boundary : ChildProof.ExportedBoundaryPoints)
	{
		if (!Charge()) return false;
		Boundary.LocalCell += ChildProof.RegionCellOffset;
		Boundary.SourceCell += ChildProof.RegionCellOffset;
		Boundary.bRequiresBoundaryFacing = false;
		Trial.IncomingBoundaryPoints.Add(MoveTemp(Boundary));
	}
	TArray<FIntVector> RequiredSupportCells;
	TArray<FIntVector> RequiredClearanceCells;
	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& Pair : ChildContext.Placements)
	{
		if (!FSolveContext::IsOccupiedPlacement(Pair.Value)) continue;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			if (!Charge()) return false;
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(Index);
			const FIntVector NeighborCell = Pair.Key + ChildProof.RegionCellOffset + FLayoutDirectionUtils::ToCellDelta(Direction);
			if (ReservedCells.Contains(NeighborCell)) continue;
			FLayoutFaceRule Face;
			if (!TryGetSolvePlacementFaceRule(ChildContext, Pair.Value, Direction, Face)) continue;
			if (RequiresFilledNeighborOccupancy(Face)) RequiredSupportCells.AddUnique(NeighborCell);
			else if (Face.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor) RequiredClearanceCells.AddUnique(NeighborCell);
		}
	}
	const TSharedPtr<const FLayoutRegionSolveResult> Retained = MakeShared<FLayoutRegionSolveResult>(ChildProof);
	TArray<FSparseStructuralTraversalPort> ChildPorts;
	for (const FLayoutCommittedEndpointAnchor& Endpoint : ChildProof.CommittedEndpointAnchors)
	{
		if (!Charge()) return false;
		const FIntVector Ingress = Endpoint.LocalCell + ChildProof.RegionCellOffset
			+ FLayoutDirectionUtils::ToCellDelta(Endpoint.FaceDirection);
		const FSolveContext::FSolvePlacement* Placement = ChildContext.Placements.Find(Endpoint.LocalCell);
		FLayoutFaceRule Face;
		if (Endpoint.CommitmentId.IsNone() || Endpoint.TraversalChannels.IsEmpty()
			|| !Trial.PlannedCellIntents.Contains(Ingress) || Placement == nullptr
			|| !TryGetSolvePlacementFaceRule(ChildContext, *Placement, Endpoint.FaceDirection, Face)
			|| !Face.ConnectedTraversalChannels.HasAllExact(Endpoint.TraversalChannels)
			|| !ChildProof.ExportedBoundaryPoints.ContainsByPredicate([&](const FLayoutSolveBoundaryPoint& Boundary)
				{ return Boundary.LocalCell == Endpoint.LocalCell && Boundary.FaceDirection == Endpoint.FaceDirection
					&& Boundary.ConnectedTraversalChannels.HasAllExact(Endpoint.TraversalChannels); }))
		{
			OutFailureReason = FString::Printf(TEXT("Accepted child endpoint %s has no exact placed face or legal parent ingress."), *Endpoint.CommitmentId.ToString());
			return false;
		}
		for (const FGameplayTag& Channel : Endpoint.TraversalChannels)
		{
			FSparseStructuralTraversalPort& Port = ChildPorts.AddDefaulted_GetRef();
			Port.ProofId = Endpoint.CommitmentId;
			Port.Cell = Ingress;
			Port.TraversalChannel = Channel;
			Port.EndpointCommitment = Endpoint;
			Port.ChildProof = Retained;
			Port.RequiredSupportCells = RequiredSupportCells;
			Port.RequiredClearanceCells = RequiredClearanceCells;
			for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
			{
				if (Trial.PlannedCellIntents.Contains(Ingress + FLayoutDirectionUtils::ToCellDelta(Direction)))
					Port.RouteFaceMask |= LayoutFaceDirectionMask(Direction);
			}
			// A selected gate/stair may already own ingress. No extra corridor exit is needed
			// for direct contact; the coordinator and placed graph still prove connectivity.
			if (!Charge()) return false;
			const TArray<FWalkableNodeKey> Roots{{Endpoint.LocalCell, Channel}};
			const TSet<FWalkableNodeKey> Reached = BuildReachableWalkableNodesFromPlacedRoots(ChildContext, &Roots, &ParentContext);
			if (!Charge()) return false;
			for (const FLayoutCommittedEndpointAnchor& Other : ChildProof.CommittedEndpointAnchors)
			{
				for (const FGameplayTag& OtherChannel : Other.TraversalChannels)
				{
					if (Reached.Contains({Other.LocalCell, OtherChannel}))
						Port.InternallyReachableNodes.Add({Other.LocalCell + ChildProof.RegionCellOffset
							+ FLayoutDirectionUtils::ToCellDelta(Other.FaceDirection), OtherChannel});
				}
			}
		}
	}
	Trial.SparseStructuralChildProofs.Add(Retained);
	Trial.CandidateAttemptCount = ParentContext.CandidateAttemptCount;
	TSet<FIntVector> WorkCells;
	for (const TPair<FIntVector, ELayoutCellIntent>& Pair : Trial.PlannedCellIntents) WorkCells.Add(Pair.Key);
	FSolveContext Refreshed;
	const bool bPrepared = TryBuildSparseStructuralLocalSolveView(Trial, WorkCells, Refreshed, OutFailureReason);
	ParentContext.CandidateAttemptCount = FMath::Max(ParentContext.CandidateAttemptCount, Refreshed.CandidateAttemptCount);
	ParentContext.bTimeBudgetExceeded |= Refreshed.bTimeBudgetExceeded;
	if (!bPrepared) return false;
	ParentContext = MoveTemp(Refreshed);
	InOutPorts.Append(MoveTemp(ChildPorts));
	return true;
}

bool LayoutProfileSolverInternal::ValidateSolvedPlacementAdjacencyForTests(
	FSolveContext& Context,
	FString& OutFailureReason)
{
	return ValidateSolvedPlacementAdjacency(Context, OutFailureReason);
}

FLayoutSolveResult LayoutProfileSolverInternal::BuildSolveResultFromContextPlacementsForTests(
	const FSolveContext& Context)
{
	FLayoutSolveResult Result;
	Result.FootprintSize = Context.Result.FootprintSize;
	AppendContextPlacementsToResult(Context, Result);
	SortResultPlacements(Result);
	return Result;
}

TMap<FIntVector, FSolveContext::FSolvePlacement> LayoutProfileSolverInternal::BuildSparseFixedNeighborPlacementsForTests(
	const FLayoutSolveResult& SolveResult)
{
	FSolveContext Context;
	SeedSparseFixedNeighborPlacements(SolveResult, Context);
	return Context.FixedNeighborPlacements;
}

TSet<FIntVector> LayoutProfileSolverInternal::BuildSolveResultOccupiedWorldCellsForTests(
	const FLayoutSolveResult& SolveResult)
{
	return BuildSolveResultOccupiedWorldCells(SolveResult);
}

bool LayoutProfileSolverInternal::TryGetPlacementFaceRuleForTests(
	const FSolveContext& Context,
	const FSolveContext::FSolvePlacement& Placement,
	const ELayoutFaceDirection Direction,
	FLayoutFaceRule& OutFaceRule)
{
	return TryGetPlacementFaceRule(Context, Placement, Direction, OutFaceRule);
}

int32 LayoutProfileSolverInternal::GetCandidateReachabilityPreferenceForTests(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const TSet<FWalkableNodeKey>& ReachableNodes)
{
	return GetCandidateReachabilityPreference(Context, Cell, Candidate, ReachableNodes);
}

bool LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
	FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	FString& OutFailureReason)
{
	return IsCandidateCompatible(Context, Cell, Candidate, &OutFailureReason);
}

TArray<FIntVector> LayoutProfileSolverInternal::CommitOccupiedCandidateBundleForTests(
	FSolveContext& Context,
	const FIntVector& RootCell,
	const FSolveCandidate& Candidate)
{
	return CommitOccupiedCandidateBundle(Context, RootCell, Candidate);
}

void LayoutProfileSolverInternal::RollbackOccupiedCandidateBundleForTests(
	FSolveContext& Context,
	const TArray<FIntVector>& PlacedCells)
{
	RollbackOccupiedCandidateBundle(Context, PlacedCells);
}

bool LayoutProfileSolverInternal::ForwardCheckPlacedCellsForTests(
	FSolveContext& Context,
	const TArray<FIntVector>& PlacedCells,
	FString& OutFailureReason)
{
	return ForwardCheckPlacedCells(Context, PlacedCells, OutFailureReason);
}

namespace
{
bool DoesEntryCellAdmitCandidate(
	const FLayoutRegionSolveRequest& Request,
	const FIntPoint& FootprintSize,
	const TArray<FLayoutPlannedCell>& SourceCells,
	const FIntVector& EntryCell,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	TArray<FLayoutPlannedCell> TrialCells = SourceCells;
	bool bFoundEntryCell = false;
	for (FLayoutPlannedCell& PlannedCell : TrialCells)
	{
		if (PlannedCell.Cell == EntryCell)
		{
			PlannedCell.Intent = ELayoutCellIntent::Entry;
			bFoundEntryCell = true;
		}
	}
	if (!bFoundEntryCell)
	{
		OutFailureReason = FString::Printf(
			TEXT("Entry cell %s is not part of the final planned topology."),
			*EntryCell.ToString());
		return false;
	}

	FSolveContext Context;
	FLayoutSolveResult BaseResult;
	BaseResult.Seed = Request.Seed;
	ApplySolveSettings(
		Context,
		nullptr,
		&Request.ProfileSnapshot,
		&Request.ExecutionSettings,
		Request.Seed,
		BaseResult,
		true,
		&Request.ModuleCatalog);
	Context.OwningTopModuleLevelByXY = Request.OwningTopModuleLevelByXY;
	BuildOverridePlan(Context, FootprintSize, TrialCells);
	BuildOrientedVariants(Context);

	for (const FSolveCandidate& Candidate : BuildOrderedCandidates(
		Context,
		EntryCell,
		ELayoutCellIntent::Entry))
	{
		if (IsOccupiedCandidate(Candidate)
			&& CandidateHasRole(Context, Candidate, ELayoutModuleRole::Entry)
			&& CandidateExposesEntryFace(Context, Candidate))
		{
			return true;
		}
	}

	OutFailureReason = FString::Printf(
		TEXT("Entry cell %s has no occupied Entry-role candidate admitted by its resolved content-set zone and face rules."),
		*EntryCell.ToString());
	return false;
}

/** Selects root and continuation terrain-seam Entries before VerticalAccess host planning. */
bool PromoteFinalizedTerrainSeamEntries(
	const FLayoutRegionSolveRequest& Request,
	const FIntPoint& FootprintSize,
	TArray<FLayoutPlannedCell>& InOutPlannedCells,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!Request.ProfileSnapshot.bSupportsSteppedTerrainSolve
		|| !Request.ProfileSnapshot.bEnableTerrainSeams)
	{
		return true;
	}

	FSolveContext Context;
	FLayoutSolveResult BaseResult;
	BaseResult.Seed = Request.Seed;
	ApplySolveSettings(
		Context,
		nullptr,
		&Request.ProfileSnapshot,
		&Request.ExecutionSettings,
		Request.Seed,
		BaseResult,
		true,
		&Request.ModuleCatalog);
	Context.OwningTopModuleLevelByXY = Request.OwningTopModuleLevelByXY;
	BuildOverridePlan(Context, FootprintSize, InOutPlannedCells);
	BuildOrientedVariants(Context);
	const bool bPromoted = PromoteRouteTerrainSeamCellsToEntries(Context);
	if (!bPromoted && !Context.Result.FailureReason.IsEmpty())
	{
		OutFailureReason = Context.Result.FailureReason;
		return false;
	}
	if (bPromoted)
	{
		InOutPlannedCells = MoveTemp(Context.Result.PlannedCells);
	}
	return true;
}

/** Verifies one candidate-local face has support in the adjacent planned domain. */
bool HasCompatiblePlannedNeighborInDirection(
	FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const FIntVector& CandidateLocalCell,
	const ELayoutFaceDirection Direction,
	FString& OutFailureReason)
{
	FLayoutFaceRule CandidateFaceRule;
	if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
		Context,
		Candidate,
		CandidateLocalCell,
		Direction,
		CandidateFaceRule))
	{
		OutFailureReason = FString::Printf(
			TEXT("Host candidate '%s' yaw=%d local cell %s has no %s face rule."),
			*Candidate.ModuleSnapshotId.ToString(),
			Candidate.YawRotationSteps,
			*CandidateLocalCell.ToString(),
			*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)));
		return false;
	}
	const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
	const ELayoutCellIntent* NeighborIntent = Context.PlannedCellIntents.Find(NeighborCell);
	if (NeighborIntent == nullptr)
	{
		return IsFaceCompatibleWithOccupancy(CandidateFaceRule, false, false);
	}
	for (const FSolveCandidate& NeighborCandidate : BuildOrderedCandidates(
		Context,
		NeighborCell,
		*NeighborIntent))
	{
		if (!IsOccupiedCandidate(NeighborCandidate))
		{
			// A planned empty candidate remains interior topology, but can satisfy
			// clearance or optional occupancy just like a lateral empty neighbor.
			if (IsFaceCompatibleWithOccupancy(CandidateFaceRule, true, false)) return true;
			continue;
		}
		FLayoutFaceRule NeighborFaceRule;
		if (TryGetCandidateFaceRule(
			Context,
			NeighborCandidate,
			FLayoutDirectionUtils::GetOpposite(Direction),
			NeighborFaceRule)
			&& DoesFilledNeighborFacePairSatisfyInterfaceContract(
				CandidateFaceRule,
				Candidate.YawRotationSteps,
				NeighborFaceRule,
				NeighborCandidate.YawRotationSteps))
		{
			return true;
		}
	}
	OutFailureReason = FString::Printf(
		TEXT("Host cell %s candidate '%s' yaw=%d local cell %s has no compatible %s neighbor domain at %s."),
		*Cell.ToString(),
		*Candidate.ModuleSnapshotId.ToString(),
		Candidate.YawRotationSteps,
		*CandidateLocalCell.ToString(),
		*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
		*NeighborCell.ToString());
	return false;
}

/** Request-local prospective domains; these support admission, never replace the final joint witness. */
struct FProspectiveHostSupport
{
	FIntVector LowerCell = FIntVector::ZeroValue;
	const TArray<FIntVector>* Roots = nullptr;
	TMap<FIntVector, TArray<FSolveCandidate>> CandidatesByRoot;
};

/** Finds exact non-overlapping bundle support at another eligible host, retaining authored zone/level checks. */
bool HasCompatibleProspectiveHostNeighbor(
	FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const FIntVector& CandidateLocalCell,
	const ELayoutFaceDirection Direction,
	FProspectiveHostSupport& Support,
	TArray<FLayoutVerticalAccessSupportCandidate>* OutOffers = nullptr)
{
	if (Support.Roots == nullptr) return false;
	bool bFound = false;
	FLayoutFaceRule Face;
	if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
		Context, Candidate, CandidateLocalCell, Direction, Face)) return false;
	const TArray<int32>* Variants = Context.VariantIndicesByIntent.Find(ELayoutCellIntent::VerticalAccess);
	if (Variants == nullptr) return false;
	const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
	const FLayoutModuleSolveSnapshot& OwnSnapshot = Context.ModuleSnapshots[Candidate.ModuleSnapshotIndex];
	const FIntVector OwnRoot = Cell - LayoutPlacementOccupancy::ProjectLocalCellToWorld(
		FIntVector::ZeroValue, CandidateLocalCell, OwnSnapshot.BoundsCells, Candidate.YawRotationSteps);
	TSet<FIntVector> OwnCells = {Support.LowerCell, Support.LowerCell + FIntVector(0, 0, 1)};
	for (const FIntVector& Local : LayoutPlacementOccupancy::ResolveSnapshotOccupiedLocalCells(&OwnSnapshot))
	{
		OwnCells.Add(LayoutPlacementOccupancy::ProjectLocalCellToWorld(
			OwnRoot, Local, OwnSnapshot.BoundsCells, Candidate.YawRotationSteps));
	}
	for (const int32 VariantIndex : *Variants)
	{
		const FSolveContext::FOrientedModuleVariant& Variant = Context.Variants[VariantIndex];
		const FLayoutModuleSolveSnapshot& Snapshot = Context.ModuleSnapshots[Variant.ModuleSnapshotIndex];
		if (Snapshot.OccupiedLocalCells.Num() < 2) continue;
		for (const FIntVector& Local : Snapshot.OccupiedLocalCells)
		{
			const FIntVector Root = NeighborCell - LayoutPlacementOccupancy::ProjectLocalCellToWorld(
				FIntVector::ZeroValue, Local, Snapshot.BoundsCells, Variant.YawRotationSteps);
			if (Root == Support.LowerCell || !Support.Roots->Contains(Root)) continue;
			const bool bOverlaps = Snapshot.OccupiedLocalCells.ContainsByPredicate([&](const FIntVector& Occupied)
			{
				return OwnCells.Contains(LayoutPlacementOccupancy::ProjectLocalCellToWorld(
					Root, Occupied, Snapshot.BoundsCells, Variant.YawRotationSteps));
			});
			if (bOverlaps) continue;

			TArray<FSolveCandidate>* RootCandidates = Support.CandidatesByRoot.Find(Root);
			if (RootCandidates == nullptr)
			{
				TArray<FLayoutPlannedCell> TrialCells = Context.Result.PlannedCells;
				FLayoutPlannedCell* RootPlan = TrialCells.FindByPredicate(
					[&Root](const FLayoutPlannedCell& Planned) { return Planned.Cell == Root; });
				if (RootPlan == nullptr) continue;
				RootPlan->Intent = ELayoutCellIntent::VerticalAccess;
				FSolveContext Probe = Context;
				BuildOverridePlan(Probe, Context.FootprintSize, TrialCells);
				RootCandidates = &Support.CandidatesByRoot.Add(Root,
					BuildOrderedCandidates(Probe, Root, ELayoutCellIntent::VerticalAccess));
			}
			const FSolveCandidate* Neighbor = RootCandidates->FindByPredicate(
				[VariantIndex](const FSolveCandidate& Value) { return !Value.bEmpty && Value.VariantIndex == VariantIndex; });
			if (Neighbor == nullptr) continue;
			FLayoutFaceRule NeighborFace;
			if (LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
					Context, *Neighbor, Local, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFace)
				&& DoesFilledNeighborFacePairSatisfyInterfaceContract(
					Face, Candidate.YawRotationSteps, NeighborFace, Neighbor->YawRotationSteps))
			{
				if (OutOffers == nullptr) return true;
				FLayoutVerticalAccessSupportCandidate Offer;
				Offer.Cell = NeighborCell;
				Offer.RootCell = Root;
				Offer.ModuleSnapshotId = Neighbor->ModuleSnapshotId;
				Offer.ModuleSnapshotIndex = Neighbor->ModuleSnapshotIndex;
				Offer.YawRotationSteps = Neighbor->YawRotationSteps;
				OutOffers->Add(Offer);
				bFound = true;
			}
		}
	}
	return bFound;
}

/** Verifies lateral support against ordinary and prospective bundle domains without selecting neighbors. */
bool HasCompatiblePlannedLateralNeighbors(
	FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const FIntVector& CandidateLocalCell,
	FString& OutFailureReason,
	FProspectiveHostSupport& Support)
{
	OutFailureReason.Reset();
	for (const ELayoutFaceDirection Direction : {
		ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
		ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
	{
		const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
		const ELayoutCellIntent* NeighborIntent = Context.PlannedCellIntents.Find(NeighborCell);
		if (NeighborIntent == nullptr)
		{
			continue;
		}

		bool bFoundCompatibleNeighbor = false;
		int32 OccupiedNeighborCandidateCount = 0;
		FString FirstNeighborFailure;
		const TArray<FSolveCandidate> NeighborCandidates = BuildOrderedCandidates(
			Context, NeighborCell, *NeighborIntent);
		for (const FSolveCandidate& NeighborCandidate : NeighborCandidates)
		{
			if (!IsOccupiedCandidate(NeighborCandidate))
			{
				FLayoutFaceRule CandidateFaceRule;
				if (LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
						Context,
						Candidate,
						CandidateLocalCell,
						Direction,
						CandidateFaceRule)
					&& IsFaceCompatibleWithOccupancy(CandidateFaceRule, true, false))
				{
					bFoundCompatibleNeighbor = true;
					break;
				}
				continue;
			}
			++OccupiedNeighborCandidateCount;
			FLayoutFaceRule CandidateFaceRule;
			FLayoutFaceRule NeighborFaceRule;
			const bool bHasCandidateFace = LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
				Context, Candidate, CandidateLocalCell, Direction, CandidateFaceRule);
			const bool bHasNeighborFace = TryGetCandidateFaceRule(
				Context, NeighborCandidate, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule);
			EFilledNeighborFaceCompatibilityFailure Failure = EFilledNeighborFaceCompatibilityFailure::None;
			const bool bCompatible = bHasCandidateFace
				&& bHasNeighborFace
				&& DoesFilledNeighborFacePairSatisfyInterfaceContract(
					CandidateFaceRule,
					Candidate.YawRotationSteps,
					NeighborFaceRule,
					NeighborCandidate.YawRotationSteps,
					&Failure);
			if (bCompatible)
			{
				bFoundCompatibleNeighbor = true;
				break;
			}

			if (FirstNeighborFailure.IsEmpty())
			{
				FirstNeighborFailure = FString::Printf(
					TEXT("Host cell %s local cell %s candidate '%s' yaw=%d cannot join lateral neighbor %s candidate '%s' yaw=%d on %s (%s)."),
					*Cell.ToString(),
					*CandidateLocalCell.ToString(),
					*Candidate.ModuleSnapshotId.ToString(),
					Candidate.YawRotationSteps,
					*NeighborCell.ToString(),
					*NeighborCandidate.ModuleSnapshotId.ToString(),
					NeighborCandidate.YawRotationSteps,
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
					(bHasCandidateFace && bHasNeighborFace)
						? ToDebugString(Failure)
						: TEXT("missing face rule"));
			}
		}
		if (!bFoundCompatibleNeighbor && HasCompatibleProspectiveHostNeighbor(
			Context, Cell, Candidate, CandidateLocalCell, Direction, Support))
		{
			bFoundCompatibleNeighbor = true;
		}
		if (!bFoundCompatibleNeighbor)
		{
			TArray<FString> TerrainBackedFaces;
			const uint8 TerrainBackedFaceMask = Context.TerrainBackedFilledFaceMasksByCell.FindRef(NeighborCell);
			for (const ELayoutFaceDirection FaceDirection : {
				ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
			{
				if (LayoutFaceMaskContainsDirection(TerrainBackedFaceMask, FaceDirection))
				{
					TerrainBackedFaces.Add(StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
						static_cast<int64>(FaceDirection)));
				}
			}
			const TArray<FString>* const AdmissionFailures =
				Context.InitialDomainAdmissionFailuresByCell.Find(NeighborCell);
			const FString FailureDetail = FirstNeighborFailure.IsEmpty()
				? FString::Printf(
					TEXT("Host cell %s candidate '%s' yaw=%d has no occupied lateral candidate at %s."),
					*Cell.ToString(),
					*Candidate.ModuleSnapshotId.ToString(),
					Candidate.YawRotationSteps,
					*NeighborCell.ToString())
				: MoveTemp(FirstNeighborFailure);
			OutFailureReason = FString::Printf(
				TEXT("%s NeighborDomain=[intent=%s candidates=%d occupied=%d terrainBackedFilledFaces=[%s] AdmissionFailures=[%s]]"),
				*FailureDetail,
				ToDebugString(*NeighborIntent),
				NeighborCandidates.Num(),
				OccupiedNeighborCandidateCount,
				*FString::Join(TerrainBackedFaces, TEXT(",")),
				AdmissionFailures == nullptr || AdmissionFailures->IsEmpty()
					? TEXT("<none>")
					: *FString::Join(*AdmissionFailures, TEXT(" | ")));
			return false;
		}
	}
	return true;
}

/** Requires a direct structural route endpoint; prospective companion support cannot invent a route. */
bool HasRouteEligibleHorizontalTraversalNeighbor(
	FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const FIntVector& CandidateLocalCell,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	// Optional route offers are not admission requirements. Explicit walkable
	// neighbor contacts remain mandatory, including on an upper composite landing.
	if (!Context.ProfileSnapshot.bRequireAllTraversalChannelsReachable
		&& Context.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate([](const auto& Rule)
		{
			return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
				|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
		}))
	{
		bool bRequiresWalkableContact = false;
		for (const auto Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
		{
			FLayoutFaceRule Face;
			bRequiresWalkableContact |= TryGetSolveCandidateLocalFaceRule(Context, Candidate, CandidateLocalCell, Direction, Face)
				&& Face.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
		}
		if (!bRequiresWalkableContact) return true;
	}
	bool bHasCompatibleNonEntryNeighbor = false;
	TSet<FIntVector> CompatibleEntryNeighbors;
	for (const ELayoutFaceDirection Direction : {
		ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
		ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
	{
		FLayoutFaceRule CandidateFaceRule;
		if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
				Context, Candidate, CandidateLocalCell, Direction, CandidateFaceRule)
			|| CandidateFaceRule.ConnectedTraversalChannels.IsEmpty())
		{
			continue;
		}

		const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
		const ELayoutCellIntent* const NeighborIntent = Context.PlannedCellIntents.Find(NeighborCell);
		if (NeighborIntent == nullptr)
		{
			continue;
		}
		// Host opening may terminate at exact traversal-capable structural shell.
		// Generic boundary intermediates remain restricted by route search itself.
		for (const FSolveCandidate& NeighborCandidate : BuildOrderedCandidates(
			Context, NeighborCell, *NeighborIntent))
		{
			if (!IsOccupiedCandidate(NeighborCandidate))
			{
				continue;
			}
			FLayoutFaceRule NeighborFaceRule;
			if (!TryGetCandidateFaceRule(
					Context,
					NeighborCandidate,
					FLayoutDirectionUtils::GetOpposite(Direction),
					NeighborFaceRule)
				|| !DoesFilledNeighborFacePairSatisfyInterfaceContract(
					CandidateFaceRule,
					Candidate.YawRotationSteps,
					NeighborFaceRule,
					NeighborCandidate.YawRotationSteps))
			{
				continue;
			}

			const bool bSharesTraversal = CandidateFaceRule.ConnectedTraversalChannels.HasAnyExact(
				NeighborFaceRule.ConnectedTraversalChannels);
			if (!bSharesTraversal)
			{
				continue;
			}
			if (*NeighborIntent != ELayoutCellIntent::Entry)
			{
				bHasCompatibleNonEntryNeighbor = true;
			}
			else
			{
				CompatibleEntryNeighbors.Add(NeighborCell);
			}
			break;
		}
	}

	if (bHasCompatibleNonEntryNeighbor || !CompatibleEntryNeighbors.IsEmpty())
	{
		return true;
	}
	OutFailureReason = FString::Printf(
		TEXT("Host cell %s local cell %s candidate '%s' yaw=%d has no exact reciprocal horizontal traversal endpoint in adjacent planned domains."),
		*Cell.ToString(),
		*CandidateLocalCell.ToString(),
		*Candidate.ModuleSnapshotId.ToString(),
		Candidate.YawRotationSteps);
	return false;
}

/** Prevents a host from consuming an Entry's sole exact interior edge unless that host can carry it. */
bool PreservesSoleAdjacentEntryRoute(
	FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const FIntVector& CandidateLocalCell,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	for (const ELayoutFaceDirection Direction : {
		ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
		ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
	{
		const FIntVector EntryCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
		const ELayoutCellIntent* const EntryIntent = Context.PlannedCellIntents.Find(EntryCell);
		if (EntryIntent == nullptr || *EntryIntent != ELayoutCellIntent::Entry)
		{
			continue;
		}

		const TArray<FSolveCandidate> EntryCandidates = BuildOrderedCandidates(
			Context, EntryCell, *EntryIntent);
		auto CandidatesShareTraversal = [&Context](
			const FSolveCandidate& Left,
			const FIntVector& LeftLocalCell,
			const ELayoutFaceDirection LeftDirection,
			const FSolveCandidate& Right)
		{
			FLayoutFaceRule LeftFace;
			FLayoutFaceRule RightFace;
			return IsOccupiedCandidate(Right)
				&& LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
					Context, Left, LeftLocalCell, LeftDirection, LeftFace)
				&& LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
					Context,
					Right,
					FIntVector::ZeroValue,
					FLayoutDirectionUtils::GetOpposite(LeftDirection),
					RightFace)
				&& DoesFilledNeighborFacePairSatisfyInterfaceContract(
					LeftFace,
					Left.YawRotationSteps,
					RightFace,
					Right.YawRotationSteps)
				&& LeftFace.ConnectedTraversalChannels.HasAnyExact(
					RightFace.ConnectedTraversalChannels);
		};

		bool bCandidateConnectsEntry = false;
		for (const FSolveCandidate& EntryCandidate : EntryCandidates)
		{
			if (CandidatesShareTraversal(
					Candidate,
					CandidateLocalCell,
					Direction,
					EntryCandidate))
			{
				bCandidateConnectsEntry = true;
				break;
			}
		}
		if (bCandidateConnectsEntry)
		{
			// Admission preserves this Entry edge, not global connectivity. Other anchors
			// may connect through the Entry or another level; the placed graph owns that proof.
			continue;
		}

		bool bEntryHasAlternateRouteEdge = false;
		for (const ELayoutFaceDirection EntryDirection : {
			ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			const FIntVector OtherCell = EntryCell + FLayoutDirectionUtils::ToCellDelta(EntryDirection);
			if (OtherCell == Cell)
			{
				continue;
			}
			const ELayoutCellIntent* const OtherIntent = Context.PlannedCellIntents.Find(OtherCell);
			if (OtherIntent == nullptr)
			{
				continue;
			}
			const bool bBoundaryCanCarryRoute = *OtherIntent != ELayoutCellIntent::Boundary
				|| (Context.ModuleLevelByCell.FindRef(OtherCell) > 0
					&& GetLevelFillModeForCell(Context.ProfileSnapshot, OtherCell)
						== ELayoutLevelFillMode::BoundaryOnly)
				|| Context.SteppedTraversalDeckCells.Contains(OtherCell);
			if (!bBoundaryCanCarryRoute)
			{
				continue;
			}
			const TArray<FSolveCandidate> OtherCandidates = BuildOrderedCandidates(
				Context, OtherCell, *OtherIntent);
			for (const FSolveCandidate& EntryCandidate : EntryCandidates)
			{
				for (const FSolveCandidate& OtherCandidate : OtherCandidates)
				{
					if (CandidatesShareTraversal(
							EntryCandidate,
							FIntVector::ZeroValue,
							EntryDirection,
							OtherCandidate))
					{
						bEntryHasAlternateRouteEdge = true;
						break;
					}
				}
				if (bEntryHasAlternateRouteEdge)
				{
					break;
				}
			}
			if (bEntryHasAlternateRouteEdge)
			{
				break;
			}
		}
		if (!bEntryHasAlternateRouteEdge)
		{
			OutFailureReason = FString::Printf(
				TEXT("VerticalAccess host %s candidate '%s' yaw=%d blocks adjacent Entry %s's sole exact route edge."),
				*Cell.ToString(),
				*Candidate.ModuleSnapshotId.ToString(),
				Candidate.YawRotationSteps,
				*EntryCell.ToString());
			return false;
		}
	}
	return true;
}

bool DoesVerticalAccessHostAdmitCandidatePair(
	const FLayoutRegionSolveRequest& Request,
	const FIntPoint& FootprintSize,
	const TArray<FLayoutPlannedCell>& SourceCells,
	const FIntVector& LowerCell,
	FString& OutFailureReason,
	const TArray<FIntVector>* PotentialHostRoots,
	FLayoutVerticalAccessHostOption* OutExactWitness,
	TArray<FLayoutVerticalAccessHostOption>* OutExactWitnesses,
	FSolveContext* InvocationVariants)
{
	OutFailureReason.Reset();
	if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
	if (OutExactWitness != nullptr)
	{
		*OutExactWitness = FLayoutVerticalAccessHostOption();
	}
	if (OutExactWitnesses != nullptr)
	{
		OutExactWitnesses->Reset();
	}
	FProspectiveHostSupport Support;
	Support.LowerCell = LowerCell;
	Support.Roots = PotentialHostRoots;
	const FIntVector UpperCell = LowerCell + FIntVector(0, 0, 1);
	TArray<FLayoutPlannedCell> TrialCells = SourceCells;
	TSet<FIntVector> EntryCells;
	for (const FLayoutPlannedCell& PlannedCell : TrialCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry)
		{
			EntryCells.Add(PlannedCell.Cell);
		}
	}

	bool bFoundLower = false;
	bool bFoundUpper = false;
	uint8 UpperLandingContactMask = 0;
	for (FLayoutPlannedCell& PlannedCell : TrialCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			PlannedCell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
				FootprintSize, EntryCells, PlannedCell.Cell);
		}
		if (PlannedCell.Cell == LowerCell)
		{
			PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
			bFoundLower = true;
		}
		if (PlannedCell.Cell == UpperCell)
		{
			bFoundUpper = true;
			UpperLandingContactMask = PlannedCell.VerticalAccessLandingContactMask;
		}
	}
	if (!bFoundLower || !bFoundUpper)
	{
		OutFailureReason = TEXT("VerticalAccess host pair is missing a planned lower cell or direct upper landing.");
		return false;
	}
	FSolveContext Context;
	FLayoutSolveResult BaseResult;
	BaseResult.Seed = Request.Seed;
	ApplySolveSettings(
		Context,
		nullptr,
		&Request.ProfileSnapshot,
		&Request.ExecutionSettings,
		Request.Seed,
		BaseResult,
		true,
		&Request.ModuleCatalog);
	// Keep replaced-child faces and authored levels consistent with normal CSP.
	Context.OwningTopModuleLevelByXY = Request.OwningTopModuleLevelByXY;
	Context.ExternalPlannedNeighborFaceMasks = Request.ExternalPlannedNeighborFaceMasks;
	// Host preflight compiles same frozen occupancy and route-anchor evidence as
	// normal CSP; otherwise local alternatives can terminate required routes.
	InstallTerrainBackedNeighborFaces(Context, Request.PrecomputedFrozenTerrainContract);
	Context.CommittedEndpointAnchors = Request.CommittedEndpointAnchors;
	Context.CommittedTraversalAnchors = Request.CommittedTraversalAnchors;
	BuildOverridePlan(Context, FootprintSize, TrialCells);
	if (!Context.TerrainResidualRuleIdByCell.IsEmpty())
	{
		bool bHasNormalNeighbor = false;
		for (const auto Direction : { ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			const FIntVector Neighbor = UpperCell + FLayoutDirectionUtils::ToCellDelta(Direction);
			bHasNormalNeighbor |= Context.PlannedCellIntents.Contains(Neighbor)
				&& IsSparseHostLandingDestination(Context, Neighbor);
		}
		if (!bHasNormalNeighbor)
		{
			OutFailureReason = TEXT("Sparse upper landing has no adjacent normal or preserved ground destination.");
			return false;
		}
	}
	check(Context.FixedOnlyModuleSnapshotIds.IsEmpty());
	if (InvocationVariants != nullptr && !InvocationVariants->Variants.IsEmpty())
	{
		Context.Variants = InvocationVariants->Variants;
		Context.VariantIndicesByIntent = InvocationVariants->VariantIndicesByIntent;
		if (auto* Work = LayoutSolveExecution::CurrentThreadLedger()) ++Work->VariantReuses;
	}
	else
	{
		BuildOrientedVariants(Context);
		if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
		if (InvocationVariants != nullptr)
		{
			InvocationVariants->Variants = Context.Variants;
			InvocationVariants->VariantIndicesByIntent = Context.VariantIndicesByIntent;
		}
	}

	const TArray<FSolveCandidate> LowerCandidates = BuildOrderedCandidates(
		Context, LowerCell, ELayoutCellIntent::VerticalAccess);
	auto DoesCandidateCoverLandingContacts = [&Context, UpperLandingContactMask, &UpperCell](
		const FSolveCandidate& Candidate,
		const FIntVector& CandidateLocalCell,
		FString& OutRejection)
	{
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			if (!LayoutFaceMaskContainsDirection(UpperLandingContactMask, Direction))
			{
				continue;
			}
			FLayoutFaceRule FaceRule;
			const bool bHasFaceRule = LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
				Context, Candidate, CandidateLocalCell, Direction, FaceRule);
			// Lower TerrainSeamFaceMask owns retaining support. An upper landing
			// only needs compatible bridge exposure, so Any is valid here.
			if (!bHasFaceRule
				|| (FaceRule.BoundaryRequirement != ELayoutFaceBoundaryRequirement::Any
					&& FaceRule.BoundaryRequirement != ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam
					&& FaceRule.BoundaryRequirement != ELayoutFaceBoundaryRequirement::MustFaceExteriorOrTerrainSeam))
			{
				const FString ActualBoundaryRequirement = bHasFaceRule
					? StaticEnum<ELayoutFaceBoundaryRequirement>()->GetNameStringByValue(
						static_cast<int64>(FaceRule.BoundaryRequirement))
					: TEXT("<missing face rule>");
				OutRejection = FString::Printf(
					TEXT("Landing %s candidate '%s' yaw=%d has boundary requirement %s at directed terrain contact %s; requires Any, MustFaceTerrainSeam, or MustFaceExteriorOrTerrainSeam."),
					*UpperCell.ToString(),
					*Candidate.ModuleSnapshotId.ToString(),
					Candidate.YawRotationSteps,
					*ActualBoundaryRequirement,
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)));
				return false;
			}
		}
		return true;
	};
	const TArray<FSolveCandidate> UpperCandidates = BuildOrderedCandidates(
		Context, UpperCell, Context.PlannedCellIntents.FindRef(UpperCell));
	int32 VerticalAccessLowerCandidateCount = 0;
	int32 OccupiedUpperCandidateCount = 0;
	int32 LandingContactCompatiblePairCount = 0;
	int32 VerticalCompatiblePairCount = 0;
	int32 LowerLateralCompatibleCandidateCount = 0;
	int32 UpperLateralCompatiblePairCount = 0;
	FString FirstLandingContactFailure;
	FString FirstVerticalPairFailure;
	FString FirstLowerLateralFailure;
	FString FirstUpperLateralFailure;
	TArray<FString> BoundedLowerLateralFailures;
	TArray<FString> BoundedUpperLateralFailures;
	const auto BuildTraversalPortMask = [&Context](
		const FSolveCandidate& Candidate,
		const FIntVector& CandidateLocalCell)
	{
		uint8 PortMask = 0;
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			FLayoutFaceRule FaceRule;
			if (LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
					Context, Candidate, CandidateLocalCell, Direction, FaceRule)
				&& !FaceRule.ConnectedTraversalChannels.IsEmpty())
			{
				PortMask |= LayoutFaceDirectionMask(Direction);
			}
		}
		return PortMask;
	};
	const auto DoesExactPairPreserveAdjacentDomains = [
		&Context,
		&LowerCell,
		&UpperCell,
		&Support](
		const FSolveCandidate& LowerCandidate,
		const FSolveCandidate& UpperCandidate,
		FString& OutRejection)
	{
		FSolveContext Probe = Context;
		TSet<FIntVector> OccupiedCells;
		for (const FIntVector& OccupiedCell : CommitOccupiedCandidateBundle(
			Probe, LowerCell, LowerCandidate))
		{
			OccupiedCells.Add(OccupiedCell);
		}
		if (!OccupiedCells.Contains(UpperCell))
		{
			for (const FIntVector& OccupiedCell : CommitOccupiedCandidateBundle(
				Probe, UpperCell, UpperCandidate))
			{
				OccupiedCells.Add(OccupiedCell);
			}
		}

		RefreshCompiledFaceInterfaces(Probe);
		TSet<FIntVector> CheckedNeighbors;
		for (const FIntVector& OccupiedCell : OccupiedCells)
		{
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const FIntVector NeighborCell = OccupiedCell
					+ FLayoutDirectionUtils::ToCellDelta(
						static_cast<ELayoutFaceDirection>(DirectionIndex));
				if (OccupiedCells.Contains(NeighborCell)
					|| CheckedNeighbors.Contains(NeighborCell))
				{
					continue;
				}
				CheckedNeighbors.Add(NeighborCell);
				const ELayoutCellIntent* NeighborIntent =
					Probe.PlannedCellIntents.Find(NeighborCell);
				if (NeighborIntent == nullptr || Probe.Placements.Contains(NeighborCell))
				{
					continue;
				}
				if (BuildOrderedCandidates(Probe, NeighborCell, *NeighborIntent).IsEmpty()
					&& !IsPlannedCellDischargedByForeignBundleCandidate(
						Probe, NeighborCell, nullptr))
				{
					const FSolveContext::FSolvePlacement& Placement = Probe.Placements.FindChecked(OccupiedCell);
					FSolveCandidate PlacedCandidate;
					PlacedCandidate.bEmpty = false;
					PlacedCandidate.VariantIndex = Placement.VariantIndex;
					PlacedCandidate.ModuleSnapshotIndex = Placement.ModuleSnapshotIndex;
					PlacedCandidate.ModuleSnapshotId = Placement.ModuleSnapshotId;
					PlacedCandidate.YawRotationSteps = Placement.YawRotationSteps;
					// Another already-required host may own this neighbor as a composite
					// shadow. Exact1 never supplies prospective roots and cannot gain a stair here.
					if (HasCompatibleProspectiveHostNeighbor(Probe, OccupiedCell, PlacedCandidate,
						Placement.LocalBundleCell, static_cast<ELayoutFaceDirection>(DirectionIndex), Support))
					{
						continue;
					}
					OutRejection = FString::Printf(
						TEXT("Exact VerticalAccess pair at %s removes every adjacent candidate at %s."),
						*LowerCell.ToString(),
						*NeighborCell.ToString());
					return false;
				}
			}
		}
		return true;
	};
	const auto AppendCandidateExternalObligations = [&Context, &Support](
		FSolveContext& SupportContext,
		const FIntVector& RootCell,
		const FSolveCandidate& Candidate,
		FLayoutVerticalAccessHostOption& InOutWitness)
	{
		const FLayoutModuleSolveSnapshot* Snapshot =
			FindVariantModuleSnapshot(Context, Candidate.VariantIndex);
		if (Snapshot == nullptr)
		{
			return;
		}
		const TArray<FIntVector> LocalCells =
			LayoutPlacementOccupancy::ResolveSnapshotOccupiedLocalCells(Snapshot);
		FProspectiveHostSupport ExactSupport;
		ExactSupport.LowerCell = Support.LowerCell;
		ExactSupport.Roots = Support.Roots;
		for (const FIntVector& LocalCell : LocalCells)
		{
			const FIntVector WorldCell = LayoutPlacementOccupancy::ProjectLocalCellToWorld(
				RootCell,
				LocalCell,
				Snapshot->BoundsCells,
				Candidate.YawRotationSteps);
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction =
					static_cast<ELayoutFaceDirection>(DirectionIndex);
				const FIntVector NeighborCell =
					WorldCell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (InOutWitness.OccupiedCells.Contains(NeighborCell))
				{
					continue;
				}
				FLayoutFaceRule FaceRule;
				if (!LayoutProfileSolverInternal::TryGetSolveCandidateLocalFaceRule(
						Context, Candidate, LocalCell, Direction, FaceRule))
				{
					continue;
				}
				if (FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor
					|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor)
				{
					// Only planned external cells need a module-domain offer. Unplanned
					// terrain/child interfaces were checked against their frozen authority.
					if (const ELayoutCellIntent* NeighborIntent =
							SupportContext.PlannedCellIntents.Find(NeighborCell))
					{
						InOutWitness.RequiredFilledSupportCells.AddUnique(NeighborCell);
						for (const FSolveCandidate& SupportCandidate :
							BuildOrderedCandidates(SupportContext, NeighborCell, *NeighborIntent))
						{
							if (!IsOccupiedCandidate(SupportCandidate))
							{
								continue;
							}
							FLayoutFaceRule SupportFaceRule;
							if (!TryGetCandidateFaceRule(
									Context,
									SupportCandidate,
									FLayoutDirectionUtils::GetOpposite(Direction),
									SupportFaceRule)
								|| !DoesFilledNeighborFacePairSatisfyInterfaceContract(
									FaceRule,
									Candidate.YawRotationSteps,
									SupportFaceRule,
									SupportCandidate.YawRotationSteps))
							{
								continue;
							}
							const bool bAlreadyRecorded =
								InOutWitness.FilledSupportCandidates.ContainsByPredicate(
									[&NeighborCell, &SupportCandidate](
										const FLayoutVerticalAccessSupportCandidate& Existing)
									{
										return Existing.Cell == NeighborCell
											&& Existing.ModuleSnapshotId == SupportCandidate.ModuleSnapshotId
											&& Existing.YawRotationSteps == SupportCandidate.YawRotationSteps;
									});
							if (!bAlreadyRecorded)
							{
								FLayoutVerticalAccessSupportCandidate& Recorded =
									InOutWitness.FilledSupportCandidates.AddDefaulted_GetRef();
								Recorded.Cell = NeighborCell;
								Recorded.RootCell = NeighborCell;
								Recorded.ModuleSnapshotId = SupportCandidate.ModuleSnapshotId;
								Recorded.ModuleSnapshotIndex = SupportCandidate.ModuleSnapshotIndex;
								Recorded.YawRotationSteps = SupportCandidate.YawRotationSteps;
							}
						}
						HasCompatibleProspectiveHostNeighbor(SupportContext, WorldCell, Candidate,
							LocalCell, Direction, ExactSupport, &InOutWitness.FilledSupportCandidates);
					}
				}
				else if (FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
				{
					InOutWitness.RequiredEmptyClearanceCells.AddUnique(NeighborCell);
				}
			}
		}
	};
	const auto PopulateExactWitness = [
		&Context,
		&LowerCell,
		&UpperCell,
		&BuildTraversalPortMask,
		&AppendCandidateExternalObligations,
		&FirstUpperLateralFailure,
		OutExactWitness,
		OutExactWitnesses](
		const FSolveCandidate& LowerCandidate,
		const FSolveCandidate& UpperCandidate,
		const FIntVector& UpperCandidateLocalCell)
	{
		FLayoutVerticalAccessHostOption Witness;
		Witness.bHasExactCandidateWitness = true;
		Witness.LowerCell = LowerCell;
		Witness.UpperCell = UpperCell;
		Witness.LowerModuleSnapshotId = LowerCandidate.ModuleSnapshotId;
		Witness.LowerModuleSnapshotIndex = LowerCandidate.ModuleSnapshotIndex;
		Witness.LowerYawRotationSteps = LowerCandidate.YawRotationSteps;
		Witness.UpperModuleSnapshotId = UpperCandidate.ModuleSnapshotId;
		Witness.UpperModuleSnapshotIndex = UpperCandidate.ModuleSnapshotIndex;
		Witness.UpperYawRotationSteps = UpperCandidate.YawRotationSteps;
		Witness.UpperCandidateLocalCell = UpperCandidateLocalCell;
		Witness.OccupiedCells = BuildCandidateWorldOccupiedCells(
			Context, LowerCell, LowerCandidate);
		if (UpperCandidateLocalCell == FIntVector::ZeroValue)
		{
			for (const FIntVector& OccupiedCell : BuildCandidateWorldOccupiedCells(
				Context, UpperCell, UpperCandidate))
			{
				Witness.OccupiedCells.AddUnique(OccupiedCell);
			}
		}
		// Support candidates must satisfy the whole selected bundle, not a union
		// of independent face offers that can disagree at a shared support cell.
		FSolveContext SupportContext = Context;
		CommitOccupiedCandidateBundle(SupportContext, LowerCell, LowerCandidate);
		if (UpperCandidateLocalCell == FIntVector::ZeroValue)
		{
			CommitOccupiedCandidateBundle(SupportContext, UpperCell, UpperCandidate);
		}
		RefreshCompiledFaceInterfaces(SupportContext);
		AppendCandidateExternalObligations(SupportContext, LowerCell, LowerCandidate, Witness);
		if (UpperCandidateLocalCell == FIntVector::ZeroValue)
		{
			AppendCandidateExternalObligations(SupportContext, UpperCell, UpperCandidate, Witness);
		}
		Witness.LowerTraversalPortFaceMask = BuildTraversalPortMask(
			LowerCandidate, FIntVector::ZeroValue);
		Witness.UpperTraversalPortFaceMask = BuildTraversalPortMask(
			UpperCandidate, UpperCandidateLocalCell);
		const auto PublishWitness = [OutExactWitness, OutExactWitnesses](FLayoutVerticalAccessHostOption&& Selected)
		{
			if (OutExactWitness != nullptr && !OutExactWitness->bHasExactCandidateWitness) *OutExactWitness = Selected;
			if (OutExactWitnesses != nullptr) OutExactWitnesses->Add(MoveTemp(Selected));
		};
		const bool bPreservesTerrain = Context.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate([](const auto& Rule)
		{
			return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
				|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
		});
		if (!bPreservesTerrain)
		{
			PublishWitness(MoveTemp(Witness));
			return true;
		}

		// Ground access commits an exact compatible landing module on the preserved
		// ground plane. Other residual air is not a destination. Keep different
		// support requirements separate; equivalent landing domains stay available
		// for joint normal-face propagation rather than fixing an isolated wall yaw.
		bool bHasDestination = false;
		for (const auto Direction : { ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			const FIntVector Destination = UpperCell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const ELayoutCellIntent* Intent = SupportContext.PlannedCellIntents.Find(Destination);
			if (Intent == nullptr || !IsSparseHostLandingDestination(Context, Destination)
				|| Witness.OccupiedCells.Contains(Destination)
				|| Witness.RequiredEmptyClearanceCells.Contains(Destination)) continue;
			FLayoutFaceRule LandingFace;
			if (!TryGetSolveCandidateLocalFaceRule(Context, UpperCandidate, UpperCandidateLocalCell, Direction, LandingFace)
				|| LandingFace.ConnectedTraversalChannels.IsEmpty()) continue;
			TArray<FLayoutVerticalAccessHostOption> LandingAlternatives;
			for (const FSolveCandidate& Target : BuildOrderedCandidates(SupportContext, Destination, *Intent))
			{
				FLayoutFaceRule TargetFace;
				if (!IsOccupiedCandidate(Target) || CandidateHasRole(SupportContext, Target, ELayoutModuleRole::VerticalAccess)
					|| !TryGetCandidateFaceRule(SupportContext, Target, FLayoutDirectionUtils::GetOpposite(Direction), TargetFace)
					|| !LandingFace.ConnectedTraversalChannels.HasAnyExact(TargetFace.ConnectedTraversalChannels)
					|| !DoesFilledNeighborFacePairSatisfyInterfaceContract(LandingFace, UpperCandidate.YawRotationSteps,
						TargetFace, Target.YawRotationSteps)) continue;
				FLayoutVerticalAccessHostOption Selected = Witness;
				Selected.UpperTraversalPortFaceMask = LayoutFaceDirectionMask(Direction);
				Selected.RequiredFilledSupportCells.AddUnique(Destination);
				Selected.FilledSupportCandidates.RemoveAll([&Destination](const auto& SupportCandidate)
				{
					return SupportCandidate.Cell == Destination;
				});
				FLayoutVerticalAccessSupportCandidate& Contact = Selected.FilledSupportCandidates.AddDefaulted_GetRef();
				Contact.Cell = Destination;
				Contact.RootCell = Destination;
				Contact.ModuleSnapshotId = Target.ModuleSnapshotId;
				Contact.ModuleSnapshotIndex = Target.ModuleSnapshotIndex;
				Contact.YawRotationSteps = Target.YawRotationSteps;
				// A traversable landing is itself structure. Its support and clearance
				// must travel with this exact yaw, not a union of unrelated landings.
				AppendCandidateExternalObligations(SupportContext, Destination, Target, Selected);
				const bool bMissingSupport = Selected.RequiredFilledSupportCells.ContainsByPredicate(
					[&Selected](const FIntVector& Cell)
					{
						return !Selected.OccupiedCells.Contains(Cell)
							&& !Selected.FilledSupportCandidates.ContainsByPredicate(
								[&Cell](const auto& Candidate) { return Candidate.Cell == Cell; });
					});
				if (bMissingSupport) continue;
				const auto SameSupport = [](const auto& A, const auto& B)
				{
					return A.Cell == B.Cell && A.RootCell == B.RootCell
						&& A.ModuleSnapshotId == B.ModuleSnapshotId && A.YawRotationSteps == B.YawRotationSteps;
				};
				const auto SameOtherSupports = [&](const auto& A, const auto& B)
				{
					return !A.FilledSupportCandidates.ContainsByPredicate([&](const auto& SupportCandidate)
					{
						return SupportCandidate.Cell != Destination && !B.FilledSupportCandidates.ContainsByPredicate(
							[&](const auto& Other) { return SameSupport(SupportCandidate, Other); });
					});
				};
				auto* Equivalent = LandingAlternatives.FindByPredicate([&](const auto& Existing)
				{
					return SupportContext.ModuleSnapshots[Target.ModuleSnapshotIndex].OccupiedLocalCells.Num() == 1
						&& Existing.FilledSupportCandidates.ContainsByPredicate([&](const auto& C)
							{ return C.Cell == Destination && C.ModuleSnapshotId == Target.ModuleSnapshotId; })
						&& Existing.RequiredFilledSupportCells.Num() == Selected.RequiredFilledSupportCells.Num()
						&& !Existing.RequiredFilledSupportCells.ContainsByPredicate(
							[&](const auto& Cell) { return !Selected.RequiredFilledSupportCells.Contains(Cell); })
						&& SameOtherSupports(Existing, Selected) && SameOtherSupports(Selected, Existing);
				});
				if (Equivalent != nullptr)
				{
					// Stair clearance is common to every alternative. Landing-specific
					// empty faces remain conditional on the normal candidate chosen by CSP;
					// unioning them would incorrectly require all landing yaws at once.
					Equivalent->RequiredEmptyClearanceCells.RemoveAll(
						[&](const auto& Cell) { return !Selected.RequiredEmptyClearanceCells.Contains(Cell); });
					for (const auto& SupportCandidate : Selected.FilledSupportCandidates)
						if (SupportCandidate.Cell == Destination && !Equivalent->FilledSupportCandidates.ContainsByPredicate(
							[&](const auto& Other) { return SameSupport(SupportCandidate, Other); }))
							Equivalent->FilledSupportCandidates.Add(SupportCandidate);
				}
				else LandingAlternatives.Add(MoveTemp(Selected));
				bHasDestination = true;
			}
			for (auto& Alternative : LandingAlternatives) PublishWitness(MoveTemp(Alternative));
		}
		if (!bHasDestination && FirstUpperLateralFailure.IsEmpty())
		{
			FirstUpperLateralFailure = FString::Printf(
				TEXT("Sparse landing %s has no exact reciprocal traversal contact with normal structure or authored ground."),
				*UpperCell.ToString());
		}
		return bHasDestination;
	};
	for (const FSolveCandidate& LowerCandidate : LowerCandidates)
	{
		if (!IsOccupiedCandidate(LowerCandidate)
			|| !CandidateHasRole(Context, LowerCandidate, ELayoutModuleRole::VerticalAccess))
		{
			continue;
		}
		++VerticalAccessLowerCandidateCount;
		FString LowerLateralFailure;
		if (!HasCompatiblePlannedLateralNeighbors(
				Context,
				LowerCell,
				LowerCandidate,
				FIntVector::ZeroValue,
				LowerLateralFailure, Support))
		{
			if (FirstLowerLateralFailure.IsEmpty())
			{
				FirstLowerLateralFailure = LowerLateralFailure;
			}
			if (BoundedLowerLateralFailures.Num() < 8)
			{
				BoundedLowerLateralFailures.Add(FString::Printf(
					TEXT("%s/yaw%d: %s"),
					*LowerCandidate.ModuleSnapshotId.ToString(),
					LowerCandidate.YawRotationSteps,
					*LowerLateralFailure));
			}
			continue;
		}
		FString LowerRouteFailure;
		if (!HasRouteEligibleHorizontalTraversalNeighbor(
				Context,
				LowerCell,
				LowerCandidate,
				FIntVector::ZeroValue,
				LowerRouteFailure)
			|| !PreservesSoleAdjacentEntryRoute(
				Context,
				LowerCell,
				LowerCandidate,
				FIntVector::ZeroValue,
				LowerRouteFailure))
		{
			if (FirstLowerLateralFailure.IsEmpty())
			{
				FirstLowerLateralFailure = LowerRouteFailure;
			}
			if (BoundedLowerLateralFailures.Num() < 8)
			{
				BoundedLowerLateralFailures.Add(FString::Printf(
					TEXT("%s/yaw%d: %s"),
					*LowerCandidate.ModuleSnapshotId.ToString(),
					LowerCandidate.YawRotationSteps,
					*LowerRouteFailure));
			}
			continue;
		}
		++LowerLateralCompatibleCandidateCount;

		const FSolveContext::FOrientedModuleVariant* const LowerVariant =
			FindCandidateVariant(Context, LowerCandidate);
		const FLayoutModuleSolveSnapshot* const LowerSnapshot = LowerVariant != nullptr
			&& Context.ModuleSnapshots.IsValidIndex(LowerVariant->ModuleSnapshotIndex)
				? &Context.ModuleSnapshots[LowerVariant->ModuleSnapshotIndex]
				: nullptr;
		TOptional<FIntVector> OwnedUpperLocalCell;
		if (LowerSnapshot != nullptr && LowerSnapshot->OccupiedLocalCells.Num() > 1)
		{
			for (const FIntVector& LocalCell : LowerSnapshot->OccupiedLocalCells)
			{
				if (LayoutPlacementOccupancy::ProjectLocalCellToWorld(
						LowerCell,
						LocalCell,
						LowerSnapshot->BoundsCells,
						LowerCandidate.YawRotationSteps) == UpperCell)
				{
					OwnedUpperLocalCell = LocalCell;
					break;
				}
			}
		}
		if (OwnedUpperLocalCell.IsSet())
		{
			++OccupiedUpperCandidateCount;
			FString ContactFailure;
			if (!DoesCandidateCoverLandingContacts(
					LowerCandidate,
					OwnedUpperLocalCell.GetValue(),
					ContactFailure))
			{
				if (FirstLandingContactFailure.IsEmpty())
				{
					FirstLandingContactFailure = MoveTemp(ContactFailure);
				}
				continue;
			}
			++LandingContactCompatiblePairCount;

			const bool bHasDirectInternalTraversal = LowerSnapshot->DerivedInternalTraversalLinks.ContainsByPredicate(
				[&OwnedUpperLocalCell](const FLayoutDerivedInternalTraversalLink& Link)
				{
					return (Link.FromLocalCell == FIntVector::ZeroValue
							&& Link.ToLocalCell == OwnedUpperLocalCell.GetValue())
						|| (Link.bBidirectional
							&& Link.ToLocalCell == FIntVector::ZeroValue
							&& Link.FromLocalCell == OwnedUpperLocalCell.GetValue());
				});
			if (!bHasDirectInternalTraversal)
			{
				if (FirstVerticalPairFailure.IsEmpty())
				{
					FirstVerticalPairFailure = FString::Printf(
						TEXT("Composite VerticalAccess candidate '%s' yaw=%d owns direct upper landing %s but has no derived internal traversal link from root local cell to local cell %s."),
						*LowerCandidate.ModuleSnapshotId.ToString(),
						LowerCandidate.YawRotationSteps,
						*UpperCell.ToString(),
						*OwnedUpperLocalCell.GetValue().ToString());
				}
				continue;
			}
			++VerticalCompatiblePairCount;

			FString UpperLateralFailure;
			if (!HasCompatiblePlannedLateralNeighbors(
					Context,
					UpperCell,
					LowerCandidate,
					OwnedUpperLocalCell.GetValue(),
					UpperLateralFailure, Support)
				|| !HasRouteEligibleHorizontalTraversalNeighbor(
					Context,
					UpperCell,
					LowerCandidate,
					OwnedUpperLocalCell.GetValue(),
					UpperLateralFailure)
				|| !HasCompatiblePlannedNeighborInDirection(
					Context,
					UpperCell,
					LowerCandidate,
					OwnedUpperLocalCell.GetValue(),
					ELayoutFaceDirection::PosZ,
					UpperLateralFailure))
			{
				if (FirstUpperLateralFailure.IsEmpty())
				{
					FirstUpperLateralFailure = UpperLateralFailure;
				}
				if (BoundedUpperLateralFailures.Num() < 8)
				{
					BoundedUpperLateralFailures.Add(FString::Printf(
						TEXT("%s/yaw%d: %s"),
						*LowerCandidate.ModuleSnapshotId.ToString(),
						LowerCandidate.YawRotationSteps,
						*UpperLateralFailure));
				}
				continue;
			}
			FString AdjacentDomainFailure;
			if (!DoesExactPairPreserveAdjacentDomains(
					LowerCandidate,
					LowerCandidate,
					AdjacentDomainFailure))
			{
				if (FirstUpperLateralFailure.IsEmpty())
				{
					FirstUpperLateralFailure = MoveTemp(AdjacentDomainFailure);
				}
				continue;
			}
			if (!PopulateExactWitness(LowerCandidate, LowerCandidate, OwnedUpperLocalCell.GetValue())) continue;
			++UpperLateralCompatiblePairCount;
			if (OutExactWitnesses == nullptr)
			{
				return true;
			}
			continue;
		}

		for (const FSolveCandidate& UpperCandidate : UpperCandidates)
		{
			if (!IsOccupiedCandidate(UpperCandidate))
			{
				continue;
			}
			++OccupiedUpperCandidateCount;
			FString ContactFailure;
			if (!DoesCandidateCoverLandingContacts(
					UpperCandidate,
					FIntVector::ZeroValue,
					ContactFailure))
			{
				if (FirstLandingContactFailure.IsEmpty())
				{
					FirstLandingContactFailure = MoveTemp(ContactFailure);
				}
				continue;
			}
			++LandingContactCompatiblePairCount;
			if (!AreAdjacentCandidatesCompatible(
					Context, LowerCandidate, UpperCandidate, ELayoutFaceDirection::PosZ))
			{
				if (FirstVerticalPairFailure.IsEmpty())
				{
					FLayoutFaceRule LowerFaceRule;
					FLayoutFaceRule UpperFaceRule;
					const bool bHasLowerFace = TryGetCandidateFaceRule(
						Context, LowerCandidate, ELayoutFaceDirection::PosZ, LowerFaceRule);
					const bool bHasUpperFace = TryGetCandidateFaceRule(
						Context, UpperCandidate, ELayoutFaceDirection::NegZ, UpperFaceRule);
					EFilledNeighborFaceCompatibilityFailure Failure = EFilledNeighborFaceCompatibilityFailure::None;
					if (bHasLowerFace && bHasUpperFace)
					{
						DoesFilledNeighborFacePairSatisfyInterfaceContract(
							LowerFaceRule,
							LowerCandidate.YawRotationSteps,
							UpperFaceRule,
							UpperCandidate.YawRotationSteps,
							&Failure);
					}
					FirstVerticalPairFailure = FString::Printf(
						TEXT("Vertical pair lower %s candidate '%s' yaw=%d and landing %s candidate '%s' yaw=%d rejected (%s)."),
						*LowerCell.ToString(),
						*LowerCandidate.ModuleSnapshotId.ToString(),
						LowerCandidate.YawRotationSteps,
						*UpperCell.ToString(),
						*UpperCandidate.ModuleSnapshotId.ToString(),
						UpperCandidate.YawRotationSteps,
						(bHasLowerFace && bHasUpperFace)
							? ToDebugString(Failure)
							: TEXT("missing face rule"));
				}
				continue;
			}
			++VerticalCompatiblePairCount;
			FString UpperLateralFailure;
			if (!HasCompatiblePlannedLateralNeighbors(
					Context,
					UpperCell,
					UpperCandidate,
					FIntVector::ZeroValue,
					UpperLateralFailure, Support)
				|| !HasRouteEligibleHorizontalTraversalNeighbor(
					Context,
					UpperCell,
					UpperCandidate,
					FIntVector::ZeroValue,
					UpperLateralFailure))
			{
				if (FirstUpperLateralFailure.IsEmpty())
				{
					FirstUpperLateralFailure = MoveTemp(UpperLateralFailure);
				}
				continue;
			}
			FString AdjacentDomainFailure;
			if (!DoesExactPairPreserveAdjacentDomains(
					LowerCandidate,
					UpperCandidate,
					AdjacentDomainFailure))
			{
				if (FirstUpperLateralFailure.IsEmpty())
				{
					FirstUpperLateralFailure = MoveTemp(AdjacentDomainFailure);
				}
				continue;
			}
			if (!PopulateExactWitness(LowerCandidate, UpperCandidate, FIntVector::ZeroValue)) continue;
			++UpperLateralCompatiblePairCount;
			if (OutExactWitnesses == nullptr)
			{
				return true;
			}
		}
	}
	if (OutExactWitnesses != nullptr && !OutExactWitnesses->IsEmpty())
	{
		return true;
	}
	FString FailureDetail = !FirstUpperLateralFailure.IsEmpty()
		? FirstUpperLateralFailure
		: (!FirstLowerLateralFailure.IsEmpty()
			? FirstLowerLateralFailure
			: (!FirstVerticalPairFailure.IsEmpty()
				? FirstVerticalPairFailure
				: FirstLandingContactFailure));
	if (FailureDetail.IsEmpty())
	{
		if (const TArray<FString>* AdmissionFailures =
			Context.InitialDomainAdmissionFailuresByCell.Find(LowerCell))
		{
			FailureDetail = FString::Printf(
				TEXT("LowerAdmissionFailures=[%s]"),
				AdmissionFailures->IsEmpty()
					? TEXT("<none>")
					: *FString::Join(*AdmissionFailures, TEXT(" | ")));
		}
	}
	if (!BoundedLowerLateralFailures.IsEmpty())
	{
		FailureDetail += FString::Printf(
			TEXT(" Bounded lower-route rejections=[%s]"),
			*FString::Join(BoundedLowerLateralFailures, TEXT(" | ")));
	}
	if (!BoundedUpperLateralFailures.IsEmpty())
	{
		FailureDetail += FString::Printf(
			TEXT(" Bounded upper-route rejections=[%s]"),
			*FString::Join(BoundedUpperLateralFailures, TEXT(" | ")));
	}
	// Report both incoming metadata and the rebuilt owning topology used by admission.
	// A sparse-rule zone is not a structural content-entry zone, nor filled occupancy.
	TArray<FString> HostTopology;
	for (const FIntVector& Cell : { LowerCell, UpperCell })
	{
		const FLayoutPlannedCell* SourceCell = SourceCells.FindByPredicate(
			[&Cell](const FLayoutPlannedCell& Planned) { return Planned.Cell == Cell; });
		const ELayoutPlacementZone* ResolvedZone = Context.PlacementZonesByCell.Find(Cell);
		HostTopology.Add(FString::Printf(
			TEXT("%s sourceZone=%s resolvedZone=%s moduleLevel=%d seamMask=%u preserved=%d footprintBoundary=%d"),
			*Cell.ToString(),
			SourceCell != nullptr ? *StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(SourceCell->PlacementZone)) : TEXT("<missing>"),
			ResolvedZone != nullptr ? *StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(*ResolvedZone)) : TEXT("<missing>"),
			LayoutProfileSolverInternal::GetFinalizedCellModuleLevel(Context, Cell),
			static_cast<uint32>(Context.TerrainSeamFaceMasksByCell.FindRef(Cell)),
			Context.TerrainResidualRuleIdByCell.Contains(Cell), IsBoundaryCell(Cell, FootprintSize)));
	}
	OutFailureReason = FString::Printf(
		TEXT("No compatible VerticalAccess module and direct upper landing pair remains at %s (candidates: lower=%d, VerticalAccess=%d, upper=%d, occupiedUpper=%d, landing=%d, vertical=%d, lowerLateral=%d, upperLateral=%d). HostTopology[%s]. %s"),
		*LowerCell.ToString(),
		LowerCandidates.Num(),
		VerticalAccessLowerCandidateCount,
		UpperCandidates.Num(),
		OccupiedUpperCandidateCount,
		LandingContactCompatiblePairCount,
		VerticalCompatiblePairCount,
		LowerLateralCompatibleCandidateCount,
		UpperLateralCompatiblePairCount,
		*FString::Join(HostTopology, TEXT(" | ")),
		*FailureDetail);
	return false;
}

bool BuildNormalDestinationComponents(const FLayoutRegionSolveRequest& Request,
	const TArray<FLayoutPlannedCell>& Cells, TMap<FIntVector, int32>& OutComponents,
	TMap<FIntVector, int32>& OutGroundComponents, FString& OutFailureReason)
{
	OutComponents.Reset();
	OutGroundComponents.Reset();
	FSolveContext Context;
	FLayoutSolveResult Result;
	ApplySolveSettings(Context, nullptr, &Request.ProfileSnapshot, &Request.ExecutionSettings,
		Request.Seed, Result, true, &Request.ModuleCatalog);
	Context.ExternalPlannedNeighborFaceMasks = Request.ExternalPlannedNeighborFaceMasks;
	InstallTerrainBackedNeighborFaces(Context, Request.PrecomputedFrozenTerrainContract);
	TArray<FLayoutPlannedCell> BaseCells = Cells;
	TSet<FIntVector> Entries;
	for (const auto& Cell : Cells) if (Cell.Intent == ELayoutCellIntent::Entry) Entries.Add(Cell.Cell);
	for (auto& Cell : BaseCells)
	{
		if (Cell.Intent == ELayoutCellIntent::VerticalAccess)
			Cell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(Request.FootprintSize, Entries, Cell.Cell);
	}
	Context.OwningTopModuleLevelByXY = Request.OwningTopModuleLevelByXY;
	BuildOverridePlan(Context, Request.FootprintSize, BaseCells);
	if (!Context.Result.FailureReason.IsEmpty())
	{
		OutFailureReason = Context.Result.FailureReason;
		return false;
	}
	// Ground connectivity comes from the preserved authored ground plane, not
	// ordinary-module compatibility or arbitrary physical Z. Cardinal same-stage
	// neighbors share a component; a height change creates an access obligation.
	TArray<FIntVector> GroundCells;
	for (const auto& Cell : BaseCells)
		if (IsPreservedGroundDestination(Context, Cell.Cell)) GroundCells.Add(Cell.Cell);
	GroundCells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		return A.Z != B.Z ? A.Z < B.Z : A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
	});
	int32 GroundComponentId = 0;
	for (const FIntVector& Seed : GroundCells)
	{
		if (OutGroundComponents.Contains(Seed)) continue;
		TArray<FIntVector> Pending{Seed};
		OutGroundComponents.Add(Seed, GroundComponentId);
		for (int32 Index = 0; Index < Pending.Num(); ++Index)
		{
			if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::SparseWork, OutFailureReason)) return false;
			for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
			{
				const FIntVector Neighbor = Pending[Index] + Delta;
				if (!OutGroundComponents.Contains(Neighbor) && IsPreservedGroundDestination(Context, Neighbor))
				{
					OutGroundComponents.Add(Neighbor, GroundComponentId);
					Pending.Add(Neighbor);
				}
			}
		}
		++GroundComponentId;
	}
	BuildOrientedVariants(Context);
	TMap<FIntVector, TArray<FSolveCandidate>> Domains;
	TArray<FIntVector> OrderedCells;
	for (const auto& Cell : BaseCells)
	{
		if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
		if (Context.TerrainResidualRuleIdByCell.Contains(Cell.Cell)
			|| !IsSparseHostLandingDestination(Context, Cell.Cell)) continue;
		TArray<FSolveCandidate> Domain = BuildOrderedCandidates(Context, Cell.Cell, Cell.Intent);
		Domain.RemoveAll([&Context](const FSolveCandidate& Candidate)
		{
			if (!IsOccupiedCandidate(Candidate) || CandidateHasRole(Context, Candidate, ELayoutModuleRole::VerticalAccess)) return true;
			for (const auto Direction : { ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
			{
				FLayoutFaceRule Face;
				if (TryGetCandidateFaceRule(Context, Candidate, Direction, Face) && !Face.ConnectedTraversalChannels.IsEmpty()) return false;
			}
			return true;
		});
		if (Domain.IsEmpty()) continue;
		Domains.Add(Cell.Cell, MoveTemp(Domain));
		OrderedCells.Add(Cell.Cell);
	}
	OrderedCells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		return A.Z != B.Z ? A.Z < B.Z : A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
	});
	int32 ComponentId = 0;
	for (const FIntVector& Seed : OrderedCells)
	{
		if (OutComponents.Contains(Seed)) continue;
		TArray<FIntVector> Pending{Seed};
		OutComponents.Add(Seed, ComponentId);
		for (int32 Index = 0; Index < Pending.Num(); ++Index)
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			const FIntVector Cell = Pending[Index];
			for (const auto Direction : { ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
			{
				const FIntVector Neighbor = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				const auto* NeighborDomain = Domains.Find(Neighbor);
				if (NeighborDomain == nullptr || OutComponents.Contains(Neighbor)) continue;
				bool bConnects = false;
				for (const auto& Candidate : Domains.FindChecked(Cell))
				{
					FLayoutFaceRule Face;
					if (!TryGetCandidateFaceRule(Context, Candidate, Direction, Face)) continue;
					for (const auto& Other : *NeighborDomain)
					{
						if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::SparseWork, OutFailureReason)) return false;
						FLayoutFaceRule Opposite;
						if (TryGetCandidateFaceRule(Context, Other, FLayoutDirectionUtils::GetOpposite(Direction), Opposite)
							&& Face.ConnectedTraversalChannels.HasAnyExact(Opposite.ConnectedTraversalChannels)
							&& DoesFilledNeighborFacePairSatisfyInterfaceContract(Face, Candidate.YawRotationSteps, Opposite, Other.YawRotationSteps))
						{
							bConnects = true;
							break;
						}
					}
					if (bConnects) break;
				}
				if (bConnects)
				{
					OutComponents.Add(Neighbor, ComponentId);
					Pending.Add(Neighbor);
				}
			}
		}
		++ComponentId;
	}
	return LayoutSolveExecution::Checkpoint(OutFailureReason);
}
}

bool LayoutProfileSolverInternal::EnrichPlannedCellsWithSteppedTerrainIntents(
	TArray<FLayoutPlannedCell>& InOutPlannedCells,
	FLayoutRegionSolveRequest& InOutRequest,
	FString& OutFailureReason,
	ELayoutSteppedTerrainFinalizationFailureKind* const OutFailureKind)
{
	OutFailureReason.Reset();
	if (OutFailureKind != nullptr)
	{
		*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::None;
	}
	if (InOutPlannedCells.IsEmpty())
	{
		return true;
	}

	const bool bTerrainSeamsEnabled = InOutRequest.ProfileSnapshot.bEnableTerrainSeams
		&& InOutRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
		&& (!InOutRequest.bHasSelectedModePlan || InOutRequest.SelectedModePlan.bUsesSteppedTerrainTopology);
	if (!bTerrainSeamsEnabled)
	{
		for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			PlannedCell.TerrainSeamFaceMask = 0;
			PlannedCell.VerticalAccessLandingContactMask = 0;
			if (PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam)
			{
				PlannedCell.EntryOrigin = ELayoutEntryOrigin::None;
				if (PlannedCell.Intent == ELayoutCellIntent::Entry)
				{
					PlannedCell.Intent = ELayoutCellIntent::Interior;
				}
			}
		}
	}

	// Continuation entries are endpoint contracts. Terrain entry selection may not move or add them.
	const bool bContinuationPlacement =
		IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(InOutRequest));
	if (bContinuationPlacement)
	{
		// Route expansion labels all non-endpoint cells Connector. Stepped topology
		// uses root-equivalent spatial roles so generated decks can admit an Interior
		// or Boundary VerticalAccess host instead of rejecting every route cell.
		TSet<FIntVector> EntryCells;
		for (const FLayoutPlannedCell& Cell : InOutPlannedCells)
		{
			if (Cell.Intent == ELayoutCellIntent::Entry)
			{
				EntryCells.Add(Cell.Cell);
			}
		}
		for (FLayoutPlannedCell& Cell : InOutPlannedCells)
		{
			if (Cell.Intent == ELayoutCellIntent::Connector && !Cell.bIsBridgeCell)
			{
				Cell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
					InOutRequest.FootprintSize,
					EntryCells,
					Cell.Cell);
			}
		}
	}
	else
	{
		TSet<FIntVector> EntryCells;
		if (InOutRequest.bHasQualifiedEntryCells)
		{

			TArray<FIntVector> QualifiedEntries;
			FString EntryFailureReason;
			if (TryBuildQualifiedSteppedRootEntryCells(
				InOutRequest.ProfileSnapshot,
				InOutRequest.FootprintSize,
				InOutRequest.Seed,
				InOutRequest.QualifiedEntryCells,
				QualifiedEntries,
				EntryFailureReason))
			{
				const TSet<FIntVector> QualifiedEntrySet(QualifiedEntries);
				for (FLayoutPlannedCell& Cell : InOutPlannedCells)
				{
					if (Cell.Intent == ELayoutCellIntent::Entry
						&& (Cell.EntryOrigin == ELayoutEntryOrigin::None
							|| Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary))
					{
						Cell.EntryOrigin = ELayoutEntryOrigin::None;
						Cell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
							InOutRequest.FootprintSize,
							EntryCells,
							Cell.Cell);
					}
					if (QualifiedEntrySet.Contains(Cell.Cell) && !Cell.bIsBridgeCell)
					{
						Cell.Intent = ELayoutCellIntent::Entry;
						Cell.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
					}
				}
			}
			else
			{
				OutFailureReason = EntryFailureReason;
				return false;
			}
		}
		else
		{
			TArray<FIntVector> BuiltEntries;
			if (!TryBuildEntryCells(
				InOutRequest.ProfileSnapshot,
				InOutRequest.ModuleCatalog.Modules,
				InOutRequest.FootprintSize,
				InOutRequest.Seed,
				BuiltEntries,
				OutFailureReason))
			{
				return false;
			}
			for (FLayoutPlannedCell& Cell : InOutPlannedCells)
			{
				if (Cell.Intent == ELayoutCellIntent::Entry
					&& (Cell.EntryOrigin == ELayoutEntryOrigin::None || Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary))
				{
					Cell.EntryOrigin = ELayoutEntryOrigin::None;
					Cell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(InOutRequest.FootprintSize, EntryCells, Cell.Cell);
				}
				if (BuiltEntries.Contains(Cell.Cell) && !Cell.bIsBridgeCell)
				{
					Cell.Intent = ELayoutCellIntent::Entry;
					Cell.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
				}
			}
		}

		FString EntryPlanningFailureReason;
		if (!ApplySteppedTerrainAwareEntryPlanning(
			InOutRequest,
			InOutRequest.FootprintSize,
			InOutPlannedCells,
			EntryPlanningFailureReason))
		{
			OutFailureReason = EntryPlanningFailureReason;
			return false;
		}
	}

	// Preserve contracted continuation endpoints, then select any separate
	// interior terrain-seam gate before VerticalAccess hosts reserve cells.
	if (bTerrainSeamsEnabled)
	{
		FString TerrainSeamEntryFailureReason;
		if (!PromoteFinalizedTerrainSeamEntries(
			InOutRequest,
			InOutRequest.FootprintSize,
			InOutPlannedCells,
			TerrainSeamEntryFailureReason))
		{
			// Local project fix: reject module-inadmissible seam gates before
			// VerticalAccess host permutations so binding fallback can run once.
			if (OutFailureKind != nullptr)
			{
				*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::EntryTerrainQualificationInfeasible;
			}
			OutFailureReason = TerrainSeamEntryFailureReason;
			return false;
		}
	}

	// Reject lost authored Entry authority before enumerating expensive host alternatives.
	if (!bContinuationPlacement)
	{
		FLayoutSolveResult EntryResult;
		FSolveContext EntryContext;
		ApplySolveSettings(EntryContext, nullptr, &InOutRequest.ProfileSnapshot,
			&InOutRequest.ExecutionSettings, InOutRequest.Seed, EntryResult, true, &InOutRequest.ModuleCatalog);
		EntryContext.RegionScope = InOutRequest.bHasSelectedModePlan
			? InOutRequest.SelectedModePlan.Scope : ELayoutContractRegionScope::Root;
		EntryContext.bUsesChildSolveContext = !InOutRequest.SourceParentRegionDebugPath.IsEmpty()
			|| !InOutRequest.CommittedEndpointAnchors.IsEmpty();
		EntryContext.QualifiedEntryCells = InOutRequest.QualifiedEntryCells;
		EntryContext.OwningTopModuleLevelByXY = InOutRequest.OwningTopModuleLevelByXY;
		BuildOverridePlan(EntryContext, InOutRequest.FootprintSize, InOutPlannedCells);
		const bool bFlatRoot = !EntryContext.bUsesChildSolveContext
			&& (!InOutRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
				|| (InOutRequest.bHasSelectedModePlan && !InOutRequest.SelectedModePlan.bUsesSteppedTerrainTopology));
		if (!EntryContext.Result.FailureReason.IsEmpty()
			|| (bFlatRoot && !FinalizeAuthoredFlatEntries(EntryContext))
			|| !ValidateAuthoredExteriorEntryCount(EntryContext))
		{
			OutFailureReason = EntryContext.Result.FailureReason;
			return false;
		}
		InOutPlannedCells = MoveTemp(EntryContext.Result.PlannedCells);
	}

	// VA assignment — authored alternatives in every mode, plus generated stepped-deck supplements.
	InOutRequest.VerticalAccessHostGroups.Reset();
	FString VerticalAccessFailureReason;
	if (!ApplyVerticalAccessPlanning(
		InOutRequest,
		InOutRequest.FootprintSize,
		InOutPlannedCells,
		InOutRequest.VerticalAccessHostGroups,
		VerticalAccessFailureReason,
		OutFailureKind))
	{
		OutFailureReason = VerticalAccessFailureReason;
		return false;
	}

	return true;
}

namespace
{
	/** Returns true when a frozen request owns a complete stepped terrain contract. */
	static bool DoesRequestUseSteppedTerrainContract(const FLayoutRegionSolveRequest& Request)
	{
		if (!Request.bHasSelectedModePlan)
		{
			return false;
		}
		return Request.SelectedModePlan.bUsesSteppedTerrainTopology;
	}

	/**
	 * Centralizes request-owned carrier initialization for the live per-region
	 * solve context so new frozen request contracts do not drift between root,
	 * vertical, and test-only solve entry points.
	 */
	static void InitializeSolveContextFromRequest(
		LayoutProfileSolverInternal::FSolveContext& Context,
		const FLayoutRegionSolveRequest& Request)
	{
		Context.RegionScope = Request.bHasSelectedModePlan
			? Request.SelectedModePlan.Scope
			: ELayoutContractRegionScope::Root;
		Context.bUsesChildSolveContext = !Request.SourceParentRegionDebugPath.IsEmpty()
			|| !Request.CommittedEndpointAnchors.IsEmpty();
		Context.bDeferFinalReachabilityAudit = Request.bDeferTraversalValidationToSchedule;
		const int32 PlannedCellCount = !Request.PrecomputedPlannedCells.IsEmpty()
			? Request.PrecomputedPlannedCells.Num()
			: Request.PlannedCells.Num();
		Context.bForceExpensiveForwardChecks = PlannedCellCount <= FullPropagationCellThreshold
			&& (Request.bDeferClosureValidationToSchedule
				|| Request.bDeferTraversalValidationToSchedule);
		Context.EffectiveSnapshotId = Request.EffectiveSnapshotId;
		Context.RegionDebugPath = Request.RegionDebugPath;
		Context.PrecommittedZoneFeatureProviderCommitments =
			Request.PrecommittedZoneFeatureProviderCommitments;
		LayoutZoneFeatureDemand::CompileDirectChildExternalProviderCapacity(
			Request.EffectiveSnapshotId,
			Request.ProfileSnapshot.ZoneFeatureRequirements,
			Request.ContentSetSnapshot.Entries,
			Context.HardZoneFeatureExternalProviderCapacityByRequirementId);
		Context.FrozenTerrainContract = &Request.PrecomputedFrozenTerrainContract;
		Context.OwningTopModuleLevelByXY = Request.OwningTopModuleLevelByXY;
		Context.bUsesSteppedTerrainContract = DoesRequestUseSteppedTerrainContract(Request);
		Context.FootprintSize = Request.FootprintSize;
		Context.ExternalPlannedNeighborFaceMasks =
			Request.ExternalPlannedNeighborFaceMasks;
		Context.IncomingBoundaryPoints = Request.IncomingBoundaryPoints;
		Context.CommittedEndpointAnchors = Request.CommittedEndpointAnchors;
		Context.CommittedTraversalAnchors = Request.CommittedTraversalAnchors;
		Context.ForcedPlacementBundleInsertions = Request.ForcedPlacementBundleInsertions;
		Context.RequiredRouteConstraints = Request.RequiredRouteConstraints;
		InstallCandidateDomainRestrictionsFromRequest(Context, Request);
		Context.TerrainCellContracts = Request.PrecomputedFrozenTerrainContract.CellContracts;
		InstallTerrainBackedNeighborFaces(Context, Request.PrecomputedFrozenTerrainContract);
		Context.VerticalAccessReservedCells.Reset();
		for (const FLayoutVerticalAccessHostGroup& HostGroup : Request.VerticalAccessHostGroups)
		{
			for (const FLayoutVerticalAccessHostOption& HostOption : HostGroup.Options)
			{
				Context.VerticalAccessReservedCells.Add(HostOption.LowerCell);
				Context.VerticalAccessReservedCells.Add(HostOption.UpperCell);
			}
		}

		// Exact selected child contracts, catalogs, and mapped topology must reach
		// parent domain admission together. Missing diagnostic maps keep the
		// reservation but cannot manufacture a boundary-domain witness.
		for (const FLayoutNegotiatedChildResponsibilityContract& Contract :
			Request.NegotiatedChildResponsibilityContracts)
		{
			if (Contract.ChildRegionDebugPath == Request.RegionDebugPath)
			{
				continue;
			}

			const FLayoutRegionContentSetSolveSnapshot* ChildContentSet =
				Request.ChildContentSetSnapshots.Find(Contract.ChildRegionDebugPath);
			const FLayoutModuleCatalog* ChildCatalog =
				Request.ChildModuleCatalogs.Find(Contract.ChildRegionDebugPath);
			const TArray<FLayoutPlannedCell>* ChildPlannedCells =
				Request.ChildPlannedCellsByRegion.Find(Contract.ChildRegionDebugPath);
			int32 ChildTopModuleLevel = INDEX_NONE;
			if (ChildPlannedCells != nullptr)
			{
				for (const FLayoutPlannedCell& ChildPlannedCell : *ChildPlannedCells)
				{
					ChildTopModuleLevel = FMath::Max(
						ChildTopModuleLevel,
						ChildPlannedCell.ModuleLevelIndex);
				}
			}
			for (const FLayoutNegotiatedLevelCellSet& LevelSet :
				Contract.ReplacementVolumeByLevel)
			{
				for (const FIntVector& Cell : LevelSet.Cells)
				{
					Context.ChildReservationCells.Add(Cell);
					if (ChildContentSet == nullptr
						|| ChildCatalog == nullptr
						|| ChildPlannedCells == nullptr)
					{
						continue;
					}

					const FLayoutPlannedCell* ChildPlannedCell =
						ChildPlannedCells->FindByPredicate(
							[Cell](const FLayoutPlannedCell& Candidate)
							{
								return Candidate.Cell == Cell;
							});
					if (ChildPlannedCell != nullptr)
					{
						Context.ChildContentSetsByCell.Add(Cell, ChildContentSet);
						Context.ChildModuleCatalogsByCell.Add(Cell, ChildCatalog);
						Context.ChildPlannedCellsByCell.Add(Cell, *ChildPlannedCell);
						Context.ChildTopModuleLevelByCell.Add(Cell, ChildTopModuleLevel);
					}
				}
			}
		}
	}

	/**
	 * Request-owned stepped support must be able to reshape the deterministic
	 * planned-cell surface before request-backed solve/inspection entry points
	 * freeze that plan into later prepared-contract compilation.
	 */
	static void BuildRequestAwarePlan(
		LayoutProfileSolverInternal::FSolveContext& Context,
		const FLayoutRegionSolveRequest& Request)
	{
		auto RestorePrecomputedReservedOpenReservations = [&Context](const FLayoutFrozenTerrainContract& FrozenTerrainContract)
		{
			if (!FrozenTerrainContract.ReservedOpenTerrainReservations.IsEmpty())
			{
				// Supplied-plan rebuilding resets transient records; retain immutable removed-cell authority.
				Context.Result.CompiledReservations = FrozenTerrainContract.ReservedOpenTerrainReservations;
			}
		};
		if (!Request.PlannedCells.IsEmpty() || !Request.PrecomputedPlannedCells.IsEmpty())
		{
			// Prefer the precomputed adapter output when available — it includes
			// all Z-levels for multi-level profiles.
			TArray<FLayoutPlannedCell> PlannedCells = !Request.PrecomputedPlannedCells.IsEmpty()
				? Request.PrecomputedPlannedCells
				: Request.PlannedCells;
			// Reserved child volume remains planned across the regional cut, so it must not turn parent interior into exterior shell.
			TSet<FIntVector> ExternalChildCells = Context.ChildReservationCells;
			for (const TPair<FString, TArray<FLayoutPlannedCell>>& ChildPlanPair :
				Request.ChildPlannedCellsByRegion)
			{
				for (const FLayoutPlannedCell& ChildPlannedCell : ChildPlanPair.Value)
				{
					ExternalChildCells.Add(ChildPlannedCell.Cell);
				}
			}
			for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
			{
				for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
				{
					const ELayoutFaceDirection Direction =
						static_cast<ELayoutFaceDirection>(DirectionIndex);
					if (ExternalChildCells.Contains(
						PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction)))
					{
						Context.ExternalPlannedNeighborFaceMasks.FindOrAdd(PlannedCell.Cell)
							|= BuildDirectionMask(Direction);
					}
				}
			}
			if (Context.bUsesChildSolveContext
				|| IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request)))
			{
				// Only exact incoming anchors are internal contracts; root-authored Entries keep exterior ownership.
				TSet<FIntVector> ContractedEntryCells;
				for (const FLayoutCommittedEndpointAnchor& Anchor : Request.CommittedEndpointAnchors)
				{
					ContractedEntryCells.Add(Anchor.LocalCell);
				}
				const bool bDependentChildRegion = !Request.SourceParentRegionDebugPath.IsEmpty();
				const bool bContinuationRegion =
					IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request));
				const ELayoutEntryOrigin ContractOrigin = bContinuationRegion
					? ELayoutEntryOrigin::Continuation
					: ELayoutEntryOrigin::ChildContract;
				for (FLayoutPlannedCell& PlannedCell : PlannedCells)
				{
					const bool bCanReplaceAuthoredOrigin = bDependentChildRegion || bContinuationRegion;
					if (PlannedCell.Intent == ELayoutCellIntent::Entry
						&& ContractedEntryCells.Contains(PlannedCell.Cell)
						&& (PlannedCell.EntryOrigin == ELayoutEntryOrigin::None
							|| (bCanReplaceAuthoredOrigin
								&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary)))
					{
						PlannedCell.EntryOrigin = ContractOrigin;
					}
				}
			}
			if (Request.ProfileSnapshot.bSupportsSteppedTerrainSolve
				&& Request.SourceParentRegionDebugPath.IsEmpty()
				&& !IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request)))
			{
				CompileFinalSurfaceTopology(PlannedCells, nullptr);
				for (FLayoutPlannedCell& PlannedCell : PlannedCells)
				{
					if (PlannedCell.Intent == ELayoutCellIntent::Entry
						&& (PlannedCell.EntryOrigin == ELayoutEntryOrigin::None
							|| (PlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary
								&& PlannedCell.PlacementZone == ELayoutPlacementZone::Interior)))
					{
						PlannedCell.Intent = ELayoutCellIntent::Interior;
					}
				}
			}
			UE_LOG(LogTemp, Display,
				TEXT("LayoutProfileSolver: using %d supplied planned cells (Z range %d..%d) for region '%s'"),
				PlannedCells.Num(),
				PlannedCells.Num() > 0 ? PlannedCells[0].Cell.Z : 0,
				PlannedCells.Num() > 0 ? PlannedCells.Last().Cell.Z : 0,
				*Request.RegionDebugPath);

			const bool bPlanFinalizedDuringPrewarm = Request.bHasFinalizedSteppedTerrainIntents;
			if (bPlanFinalizedDuringPrewarm)
			{
				BuildOverridePlan(Context, Request.FootprintSize, PlannedCells);
				if (!Context.Result.FailureReason.IsEmpty())
				{
					Context.Result.bSucceeded = false;
					return;
				}
				RestorePrecomputedReservedOpenReservations(Request.PrecomputedFrozenTerrainContract);
				Context.Result.bSucceeded = true;
				return;
			}

			const bool bSelectedSteppedEnvironment = DoesRequestUseSteppedTerrainContract(Request);
			if (bSelectedSteppedEnvironment
				&& !Context.bUsesChildSolveContext
				&& !IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request)))
			{
				FLayoutRegionSolveRequest FinalizationRequest = Request;
				FString FinalizationFailureReason;
				if (!FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
						PlannedCells,
						FinalizationRequest,
						FinalizationFailureReason))
				{
					Context.Result.bSucceeded = false;
					Context.Result.FailureReason = FinalizationFailureReason;
					return;
				}
				BuildOverridePlan(Context, Request.FootprintSize, PlannedCells);
				if (!Context.Result.FailureReason.IsEmpty())
				{
					Context.Result.bSucceeded = false;
					return;
				}
				RestorePrecomputedReservedOpenReservations(FinalizationRequest.PrecomputedFrozenTerrainContract);
				Context.Result.bSucceeded = true;
				return;
			}

			// Continuation entries are supplied endpoint contracts. Ordinary roots use
			// terrain-qualified candidates when adapter evidence provides them.
			if (!Context.bUsesChildSolveContext
				&& !IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request)))
			{
				TArray<FIntVector> EntryCells;
				FString EntryFailureReason;
				const bool bBuiltEntries = Request.bHasQualifiedEntryCells
					? TryBuildQualifiedSteppedRootEntryCells(
						Request.ProfileSnapshot,
						Request.FootprintSize,
						Request.Seed,
						Request.QualifiedEntryCells,
						EntryCells,
						EntryFailureReason)
					: TryBuildEntryCells(
						Request.ProfileSnapshot,
						Request.ModuleCatalog.Modules,
						Request.FootprintSize,
						Request.Seed,
						EntryCells,
						EntryFailureReason);
				if (!bBuiltEntries)
				{
					Context.Result.bSucceeded = false;
					Context.Result.FailureReason = EntryFailureReason;
					return;
				}

				if (Request.bHasQualifiedEntryCells)
				{
					const TSet<FIntVector> EmptyEntryCells;
					for (FLayoutPlannedCell& Cell : PlannedCells)
					{
						if (Cell.Intent == ELayoutCellIntent::Entry
							&& (Cell.EntryOrigin == ELayoutEntryOrigin::None
								|| Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary))
						{
							Cell.EntryOrigin = ELayoutEntryOrigin::None;
							Cell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
								Request.FootprintSize,
								EmptyEntryCells,
								Cell.Cell);
						}
					}
				}
				const TSet<FIntVector> EntryCellSet(EntryCells);
				for (FLayoutPlannedCell& Cell : PlannedCells)
				{
					if (EntryCellSet.Contains(Cell.Cell) && !Cell.bIsBridgeCell)
					{
						Cell.Intent = ELayoutCellIntent::Entry;
						Cell.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
					}
				}
			}

			if (bSelectedSteppedEnvironment)
			{
				// Stepped plans are finalized once by contract precompute so the
				// frozen host groups and planned cells reach every solve unchanged.
				Context.Result.bSucceeded = false;
				Context.Result.FailureReason = TEXT("Supplied stepped-terrain plans must be finalized by contract precompute before solving.");
				return;
			}

			BuildOverridePlan(Context, Request.FootprintSize, PlannedCells);
			if (!Context.Result.FailureReason.IsEmpty())
			{
				Context.Result.bSucceeded = false;
				return;
			}
			RestorePrecomputedReservedOpenReservations(Request.PrecomputedFrozenTerrainContract);
			if (Request.SourceParentRegionDebugPath.IsEmpty()
				&& !bSelectedSteppedEnvironment
				&& !IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request))
				&& (!FinalizeAuthoredFlatEntries(Context)
					|| !ValidateAuthoredFlatVerticalAccessCount(Context)))
			{
				Context.Result.bSucceeded = false;
				return;
			}
			Context.Result.bSucceeded = true;
			return;
		}

		BuildPlan(Context);
		if (!Context.Result.FailureReason.IsEmpty())
		{
			Context.Result.bSucceeded = false;
			return;
		}
		TArray<FLayoutPlannedCell> PlannedCells = Context.Result.PlannedCells;
		TArray<FLayoutCellReservationRecord> CompiledReservations = MoveTemp(Context.Result.CompiledReservations);

		BuildOverridePlan(Context, Context.FootprintSize, PlannedCells);
		Context.Result.CompiledReservations = MoveTemp(CompiledReservations);
		Context.Result.bSucceeded = true;
	}

	/**
	 * Centralizes the frozen request-to-context handoff so runtime entry points
	 * and test/diagnostic helpers all see the same live prepared solve surface.
	 */
	static LayoutProfileSolverInternal::FSolveContext BuildLiveSolveContextFromRequest(
		const FLayoutRegionSolveRequest& Request,
		FLayoutSolveResult& InOutResult,
		const bool bDeferUnresolvedExternalFaceValidation = false)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_ContextBuild, STAT_PorismLayout_ContextBuild);
		LayoutProfileSolverInternal::FSolveContext Context;
		ApplySolveSettings(
			Context,
			nullptr,
			&Request.ProfileSnapshot,
			&Request.ExecutionSettings,
			Request.Seed,
			InOutResult,
			true,
			&Request.ModuleCatalog);
		InitializeSolveContextFromRequest(Context, Request);
		Context.bDeferUnresolvedExternalFaceValidation =
			bDeferUnresolvedExternalFaceValidation;
		Context.QualifiedEntryCells = Request.QualifiedEntryCells;
		if (!Context.Result.FailureReason.IsEmpty())
		{
			return Context;
		}
		for (const FLayoutAdapterDiagnostic& Diagnostic : Request.PrecomputedAdapterDiagnostics)
		{
			if (!Diagnostic.Detail.IsEmpty())
			{
				AddSolveWarning(Context, Diagnostic.Detail);
			}
		}
		BuildRequestAwarePlan(Context, Request);
		if (Context.Result.FailureReason.IsEmpty() && !ValidateAuthoredExteriorEntryCount(Context))
		{
			Context.Result.bSucceeded = false;
		}
		return Context;
	}

	/** Applies authored void rules only after stepped projection and host planning have established protected cells. */
	/** Applies deferred reservations after adapter topology is final, including flat fallback adapter output. */
	bool CompileFinalizedSteppedReservedOpenSpace(
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		FLayoutRegionSolveRequest& Request,
		FString& OutFailureReason,
		ELayoutSteppedTerrainFinalizationFailureKind* const OutFailureKind,
		TArray<FLayoutCellReservationRecord>* const OutSelectedReservations)
	{
		if (OutFailureKind != nullptr)
		{
			*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::None;
		}
		if (OutSelectedReservations != nullptr)
		{
			OutSelectedReservations->Reset();
		}
		if (Request.ProfileSnapshot.ReservedOpenSpaceRules.IsEmpty())
		{
			return true;
		}

		const TArray<FLayoutCellReservationRecord>& FrozenReservations =
			Request.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations;
		if (!FrozenReservations.IsEmpty())
		{
			TSet<FIntVector> FrozenReservedCells;
			for (const FLayoutCellReservationRecord& Reservation : FrozenReservations)
			{
				if (Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty)
				{
					FrozenReservedCells.Add(Reservation.Cell);
				}
			}
			InOutPlannedCells.RemoveAll(
				[&FrozenReservedCells](const FLayoutPlannedCell& PlannedCell)
				{
					return FrozenReservedCells.Contains(PlannedCell.Cell);
				});
			if (OutSelectedReservations != nullptr)
			{
				*OutSelectedReservations = FrozenReservations;
			}
			return true;
		}

		FLayoutSolveResult ReservationResult;
		FSolveContext ReservationContext;
		ApplySolveSettings(
			ReservationContext,
			nullptr,
			&Request.ProfileSnapshot,
			&Request.ExecutionSettings,
			Request.Seed,
			ReservationResult,
			true,
			&Request.ModuleCatalog);
		InitializeSolveContextFromRequest(ReservationContext, Request);
		BuildOverridePlan(ReservationContext, Request.FootprintSize, InOutPlannedCells);
		if (!CompileReservedOpenSpaceRules(ReservationContext))
		{
			OutFailureReason = ReservationContext.Result.FailureReason;
			if (OutFailureKind != nullptr && ReservationContext.bReservedOpenPrewarmInfeasible)
			{
				*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::ReservedOpenSteppedTopologyInfeasible;
			}
			return false;
		}

		TArray<FLayoutCellReservationRecord> SelectedReservations;
		for (const FLayoutCellReservationRecord& Reservation : ReservationContext.Result.CompiledReservations)
		{
			if (Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty)
			{
				SelectedReservations.Add(Reservation);
			}
		}
		Request.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations = SelectedReservations;
		if (OutSelectedReservations != nullptr)
		{
			*OutSelectedReservations = MoveTemp(SelectedReservations);
		}
		InOutPlannedCells = MoveTemp(ReservationContext.Result.PlannedCells);
		return true;
	}
}

bool LayoutProfileSolverInternal::BuildAuthoredPlan(
	const FLayoutProfileSolveSnapshot& ProfileSnapshot,
	const FLayoutModuleCatalog& ModuleCatalog,
	const int32 Seed,
	const FIntPoint& FootprintSize,
	TArray<FLayoutPlannedCell>& OutPlannedCells,
	FString& OutFailureReason,
	TArray<FLayoutCellReservationRecord>* const OutCompiledReservations)
{
	return BuildAuthoredPlanImpl(
		ProfileSnapshot,
		ModuleCatalog,
		Seed,
		FootprintSize,
		OutPlannedCells,
		OutFailureReason,
		OutCompiledReservations);
}

bool LayoutProfileSolverInternal::PrepareSolveContextThroughRouteDomainStage(
	FSolveContext& Context, const bool bReusePreparedVariants)
{
	return PrepareSolveContextThroughRouteDomainStageImpl(Context, bReusePreparedVariants);
}

void LayoutProfileSolverInternal::FinalizePreparedSolveContextForIndexedSearch(
	FSolveContext& Context)
{
	FinalizePreparedSolveContextForIndexedSearchImpl(Context);
}

bool LayoutProfileSolverInternal::ContinuePreparedSolveContextAfterRouteDomainStage(
	FSolveContext& Context,
	const bool bValidateClosureContracts)
{
	const bool bSolved = ContinuePreparedSolveContextAfterRouteDomainStageImpl(
		Context,
		bValidateClosureContracts);
	PublishTraceIfNeeded(Context);
	return bSolved;
}

LayoutProfileSolverInternal::FSolveContext LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(
	const FLayoutRegionSolveRequest& Request)
{
	FLayoutSolveResult ScratchResult;
	return BuildLiveSolveContextFromRequest(Request, ScratchResult);
}

static bool TryPrepareRequestSolveContextThroughRouteDomainStageInternal(
	const FLayoutRegionSolveRequest& Request,
	FSolveContext& OutContext,
	const bool bDeferFinalReachabilityAudit,
	const bool bForceReachabilityBranchFaceScore)
{
	FLayoutSolveResult RootScratchResult;
	FSolveContext RootContext =
		BuildLiveSolveContextFromRequest(Request, RootScratchResult);
	for (const FLayoutPlannedCell& PlannedCell : RootContext.Result.PlannedCells)
	{
		RootContext.ProfileSnapshot.LevelCount = FMath::Max(
			RootContext.ProfileSnapshot.LevelCount,
			PlannedCell.Cell.Z + 1);
	}

	OutContext = MoveTemp(RootContext);
	OutContext.bDeferFinalReachabilityAudit = bDeferFinalReachabilityAudit;
	OutContext.bForceReachabilityBranchFaceScore =
		bForceReachabilityBranchFaceScore;

	if (!ValidateLiveLeafSolverModuleSnapshots(OutContext))
	{
		return false;
	}

	return LayoutProfileSolverInternal::PrepareSolveContextThroughRouteDomainStage(
		OutContext);
}

	/**
	 * Splits the deterministic post-route-domain prefix away from the recursive
	 * brancher so diagnostics and future architecture cuts can inspect the exact
	 * frontier after forced placements have settled.
	 */
	static void PopulatePreparedSearchPrefixStageSummary(
		FSolveContext& Context,
		const FPreparedSearchFrontier& SearchFrontier,
		LayoutProfileSolverInternal::FPreparedSearchPrefixStageSummary& OutSummary)
	{
		OutSummary = LayoutProfileSolverInternal::FPreparedSearchPrefixStageSummary{};
		OutSummary.PlacedCellCount = Context.Placements.Num();
		OutSummary.ForcedPlacementCellCount = SearchFrontier.ForcedCells.Num();
		OutSummary.bSolvedAfterForcedPlacements =
			SearchFrontier.bSolvedAfterForcedPlacements || AllCellsSolved(Context);
		if (OutSummary.bSolvedAfterForcedPlacements)
		{
			return;
		}

		const bool bRequiresReachability =
			LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(
				Context);
		const TSet<FWalkableNodeKey> ReachableNodes =
			bRequiresReachability
				? LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(
					Context)
				: TSet<FWalkableNodeKey>();
		const TSet<FWalkableNodeKey>* ReachableNodesPtr =
			bRequiresReachability ? &ReachableNodes : nullptr;

		int32 TightestRemainingSize = MAX_int32;
		for (const FIntVector& CandidateCell : Context.SolveOrder)
		{
			if (Context.Placements.Contains(CandidateCell))
			{
				continue;
			}

			const TArray<FSolveCandidate> LegalCandidates =
				GetLegalCandidatesForCellIndexed(
					Context,
					CandidateCell,
					nullptr,
					ReachableNodesPtr,
					true);
			if (LegalCandidates.IsEmpty()
				&& IsPlannedCellDischargedByForeignBundleCandidate(
					Context,
					CandidateCell,
					ReachableNodesPtr))
			{
				continue;
			}

			++OutSummary.ConstrainedCellCount;
			OutSummary.EligibleCandidateCount += LegalCandidates.Num();
			TightestRemainingSize = FMath::Min(
				TightestRemainingSize,
				LegalCandidates.Num());
			if (LegalCandidates.Num() == 1)
			{
				++OutSummary.SingleRemainingCandidateCellCount;
			}
			if (LegalCandidates.Num() <= 4)
			{
				++OutSummary.AtMostFourRemainingCandidateCellCount;
			}
		}

		OutSummary.TightestRemainingSize =
			TightestRemainingSize == MAX_int32 ? 0 : TightestRemainingSize;
		OutSummary.SelectedCell = SearchFrontier.SelectedCell;
		OutSummary.SelectedIntent =
			Context.PlannedCellIntents.FindRef(SearchFrontier.SelectedCell);
		OutSummary.SelectedDomainSize =
			SearchFrontier.SelectedCandidates.Num();

		if (!SearchFrontier.SelectedCandidates.IsEmpty())
		{
			return;
		}

		auto ClassifySearchPrefixFailureKind =
			[](const FString& FailureReason) -> FLayoutId
		{
			if (FailureReason.Contains(
					TEXT("Post-prefix constrained-cell feasibility rejected planned cell"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("ConstrainedCellImmediatePropagationFailure");
			}
			if (FailureReason.Contains(
					TEXT("connection tag compatibility failed with incoming boundary"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("BoundaryConnectionTagMismatch");
			}
			if (FailureReason.Contains(
					TEXT("share no ConnectedTraversalChannels"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("BoundaryTraversalChannelMismatch");
			}
			if (FailureReason.Contains(
					TEXT("not marked Must Face Region Boundary"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("BoundaryFaceNotAllowed");
			}
			if (FailureReason.Contains(
					TEXT("cannot consume incoming boundary"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("BoundaryOccupancyMismatch");
			}
			if (FailureReason.Contains(
					TEXT("requires matching yaw with incoming boundary"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("BoundaryYawMismatch");
			}
			if (FailureReason.Contains(
					TEXT("Required traversal anchor could not be connected"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("TraversalAnchorDisconnected");
			}
			if (FailureReason.Contains(
					TEXT("Route-constrained planned cell started with eligible candidates"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("RouteFaceConstraintFailure");
			}
			if (FailureReason.Contains(
					TEXT("Propagation found no legal candidates"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("PropagationNoCandidates");
			}
			if (FailureReason.Contains(
					TEXT("Forward checking rejected neighboring planned cell"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("ForwardCheckNeighborExhausted");
			}
			if (FailureReason.Contains(
					TEXT("Forced placement bundle insertion"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("ForcedBundleInsertion");
			}
			if (FailureReason.Contains(
					TEXT("Forced placement rejected cell"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("ForcedPlacementPruned");
			}
			if (FailureReason.Contains(
					TEXT("could not select an unsolved planned cell"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("NoSelectableCell");
			}
			if (FailureReason.Contains(
					TEXT("MaxSolverDurationSeconds"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("TimeBudgetExceeded");
			}
			if (FailureReason.Contains(
					TEXT("MaxSolverCandidateAttempts"),
					ESearchCase::IgnoreCase))
			{
				return TEXT("AttemptBudgetExceeded");
			}
			return FailureReason.IsEmpty() ? NAME_None : FLayoutId(TEXT("Unknown"));
		};

		auto CaptureDominantSearchPrefixFailureKind =
			[&ClassifySearchPrefixFailureKind](
				const TArray<FString>& FailureReasons,
				FLayoutId& OutFailureKind,
				int32& OutFailureCount)
		{
			OutFailureKind = NAME_None;
			OutFailureCount = 0;

			TMap<FLayoutId, int32> FailureCountsByKind;
			for (const FString& FailureReason : FailureReasons)
			{
				const FLayoutId FailureKind =
					ClassifySearchPrefixFailureKind(FailureReason);
				if (FailureKind.IsNone())
				{
					continue;
				}

				int32& Count = FailureCountsByKind.FindOrAdd(FailureKind);
				++Count;
				if (Count > OutFailureCount)
				{
					OutFailureKind = FailureKind;
					OutFailureCount = Count;
				}
			}
		};

		if (!Context.Result.FailureReason.IsEmpty())
		{
			OutSummary.DominantFailureKind =
				ClassifySearchPrefixFailureKind(Context.Result.FailureReason);
			OutSummary.DominantFailureCount =
				OutSummary.DominantFailureKind.IsNone() ? 0 : 1;
			OutSummary.bNoCellSelectable =
				OutSummary.DominantFailureKind == TEXT("NoSelectableCell");
			return;
		}

		if (DoesContextCarryCompiledPlannedCell(
				Context,
				SearchFrontier.SelectedCell))
		{
			TArray<FString> FailureReasons;
			GetLegalCandidatesForCellIndexed(
				Context,
				SearchFrontier.SelectedCell,
				&FailureReasons,
				ReachableNodesPtr,
				true);
			CaptureDominantSearchPrefixFailureKind(
				FailureReasons,
				OutSummary.DominantFailureKind,
				OutSummary.DominantFailureCount);
			return;
		}

		OutSummary.bNoCellSelectable = true;
		OutSummary.DominantFailureKind = TEXT("NoSelectableCell");
		OutSummary.DominantFailureCount = 1;
	}

	static void PopulatePreparedSearchPrefixStageCarrier(
		FSolveContext& Context,
		const FPreparedSearchFrontier& SearchFrontier,
		LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier& OutCarrier)
	{
		OutCarrier = LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier{};
		OutCarrier.ForcedCells = SearchFrontier.ForcedCells;
		OutCarrier.SelectedCell = SearchFrontier.SelectedCell;
		OutCarrier.SelectedCandidates = SearchFrontier.SelectedCandidates;
		OutCarrier.bSolvedAfterForcedPlacements =
			SearchFrontier.bSolvedAfterForcedPlacements || AllCellsSolved(Context);
		PopulatePreparedSearchPrefixStageSummary(
			Context,
			SearchFrontier,
			OutCarrier.Summary);
	}

	static bool TryPrepareSolveContextThroughSearchPrefixStageImpl(
		FSolveContext& InOutContext,
		LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier& OutStageCarrier)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_SearchPrefix, STAT_PorismLayout_SearchPrefix);
		OutStageCarrier = LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier{};
		FPreparedSearchFrontier SearchFrontier;
		const bool bPrepared =
			TryPrepareSolveContextThroughSearchPrefixStageInternal(
				InOutContext,
				0,
				SearchFrontier);
		PopulatePreparedSearchPrefixStageCarrier(
			InOutContext,
			SearchFrontier,
			OutStageCarrier);
		return bPrepared;
	}

	static bool ContinuePreparedSolveContextAfterSearchBranchStageImpl(
		FSolveContext& Context,
		const LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier& SearchBranchStage,
		const bool bValidateClosureContracts)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_BranchSearch, STAT_PorismLayout_BranchSearch);
		const bool bSolved =
			ContinueSolveFromPreparedSearchBranchStage(
				Context,
				0,
				SearchBranchStage)
			&& (!bValidateClosureContracts
				|| LayoutProfileSolverInternal::ValidateClosureCoverageForContext(
					Context));
		Context.Result.bSucceeded = bSolved;
		if (bSolved)
		{
			Context.Result.FailureReason.Reset();
		}

		return bSolved;
	}

	static bool ContinuePreparedSolveContextAfterSearchBranchApplyStageImpl(
		FSolveContext& Context,
		const LayoutProfileSolverInternal::FPreparedSearchBranchApplyStageCarrier&
			SearchBranchApplyStage,
		const bool bValidateClosureContracts)
	{
		const bool bSolved =
			(!bValidateClosureContracts
				 ? ContinuePreparedSolveContextAfterDeterministicSingleCandidateStagesImpl(
						Context,
						SearchBranchApplyStage.NextSolveDepth)
				 : SolveRecursive(Context, SearchBranchApplyStage.NextSolveDepth))
			&& (!bValidateClosureContracts
				|| LayoutProfileSolverInternal::ValidateClosureCoverageForContext(
					Context));
		Context.Result.bSucceeded = bSolved;
		if (bSolved)
		{
			Context.Result.FailureReason.Reset();
			return true;
		}

		++Context.Result.PropagationStats.BacktrackCount;
		++Context.Result.PropagationStats.TerrainStageBacktrackCount;
		RollbackPreparedSearchBranchApplyStageCarrier(
			Context,
			SearchBranchApplyStage);
		return false;
	}

	static bool ContinuePreparedSolveContextAfterSearchPrefixStageImpl(
		FSolveContext& Context,
		const LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier& SearchPrefixStage,
		const bool bValidateClosureContracts)
	{
		LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier
			SearchBranchStage;
		PopulatePreparedSearchBranchStageCarrier(
			SearchPrefixStage,
			SearchBranchStage);
		return ContinuePreparedSolveContextAfterSearchBranchStageImpl(
			Context,
			SearchBranchStage,
			bValidateClosureContracts);
	}

bool LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughRouteDomainStage(
	const FLayoutRegionSolveRequest& Request,
	FSolveContext& OutContext,
	const bool bDeferFinalReachabilityAudit)
{
	return TryPrepareRequestSolveContextThroughRouteDomainStageInternal(
		Request,
		OutContext,
		bDeferFinalReachabilityAudit,
		false);
}

bool LayoutProfileSolverInternal::TryPrepareSolveContextThroughSearchPrefixStage(
	FSolveContext& Context,
	FPreparedSearchPrefixStageCarrier& OutStageCarrier)
{
	return TryPrepareSolveContextThroughSearchPrefixStageImpl(
		Context,
		OutStageCarrier);
}

bool LayoutProfileSolverInternal::TryPrepareSolveContextThroughSearchBranchApplyStage(
	FSolveContext& Context,
	const FPreparedSearchBranchStageCarrier& SearchBranchStage,
	const int32 WorkItemIndex,
	FPreparedSearchBranchApplyStageCarrier& OutStageCarrier)
{
	return TryPrepareSolveContextThroughSearchBranchApplyStageImpl(
		Context,
		0,
		SearchBranchStage,
		WorkItemIndex,
		OutStageCarrier);
}

void LayoutProfileSolverInternal::BuildPreparedSearchBranchStageFromSearchPrefixStage(
	const FPreparedSearchPrefixStageCarrier& SearchPrefixStage,
	FPreparedSearchBranchStageCarrier& OutStageCarrier)
{
	PopulatePreparedSearchBranchStageCarrier(
		SearchPrefixStage,
		OutStageCarrier);
}

bool LayoutProfileSolverInternal::ContinuePreparedSolveContextAfterSearchBranchStage(
	FSolveContext& Context,
	const FPreparedSearchBranchStageCarrier& SearchBranchStage,
	const bool bValidateClosureContracts)
{
	const bool bSolved =
		ContinuePreparedSolveContextAfterSearchBranchStageImpl(
			Context,
			SearchBranchStage,
			bValidateClosureContracts);
	PublishTraceIfNeeded(Context);
	return bSolved;
}

bool LayoutProfileSolverInternal::ContinuePreparedSolveContextAfterSearchBranchApplyStage(
	FSolveContext& Context,
	const FPreparedSearchBranchApplyStageCarrier& SearchBranchApplyStage,
	const bool bValidateClosureContracts)
{
	const bool bSolved =
		ContinuePreparedSolveContextAfterSearchBranchApplyStageImpl(
			Context,
			SearchBranchApplyStage,
			bValidateClosureContracts);
	PublishTraceIfNeeded(Context);
	return bSolved;
}

bool LayoutProfileSolverInternal::ContinuePreparedSolveContextAfterSearchPrefixStage(
	FSolveContext& Context,
	const FPreparedSearchPrefixStageCarrier& SearchPrefixStage,
	const bool bValidateClosureContracts)
{
	const bool bSolved =
		ContinuePreparedSolveContextAfterSearchPrefixStageImpl(
			Context,
			SearchPrefixStage,
			bValidateClosureContracts);
	PublishTraceIfNeeded(Context);
	return bSolved;
}

bool LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughSearchPrefixStage(
	const FLayoutRegionSolveRequest& Request,
	FSolveContext& OutContext,
	FPreparedSearchPrefixStageCarrier& OutStageCarrier,
	const bool bDeferFinalReachabilityAudit)
{
	if (!TryPrepareRequestSolveContextThroughRouteDomainStage(
		Request,
		OutContext,
		bDeferFinalReachabilityAudit))
	{
		return false;
	}

	return LayoutProfileSolverInternal::TryPrepareSolveContextThroughSearchPrefixStage(
		OutContext,
		OutStageCarrier);
}

static FLayoutSolveResult BuildPreparedFailureRequestBackedLeafSolveResultFromContext(
	const FLayoutRegionSolveRequest& Request,
	LayoutProfileSolverInternal::FSolveContext&& Context);

bool LayoutProfileSolverInternal::TryPrepareRequestBackedLeafRegionThroughSearchPrefixStage(
	const FLayoutRegionSolveRequest& Request,
	FPreparedRequestBackedLeafSearchPrefixContinuation& OutContinuation,
	const bool bDeferFinalReachabilityAudit)
{
	OutContinuation = FPreparedRequestBackedLeafSearchPrefixContinuation{};
	OutContinuation.bReadyForFullSolveContinuation =
		LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughSearchPrefixStage(
			Request,
			OutContinuation.Context,
			OutContinuation.SearchPrefixStage,
			bDeferFinalReachabilityAudit);
	if (OutContinuation.bReadyForFullSolveContinuation)
	{
		return true;
	}

	OutContinuation.FailureSolveResult =
		BuildPreparedFailureRequestBackedLeafSolveResultFromContext(
			Request,
			MoveTemp(OutContinuation.Context));
	return false;
}

bool LayoutProfileSolverInternal::TryPrepareRequestBackedLeafRegionThroughSearchBranchStage(
	const FLayoutRegionSolveRequest& Request,
	FPreparedRequestBackedLeafSearchBranchContinuation& OutContinuation,
	const bool bDeferFinalReachabilityAudit)
{
	OutContinuation = FPreparedRequestBackedLeafSearchBranchContinuation{};
	FPreparedRequestBackedLeafSearchPrefixContinuation PrefixContinuation;
	if (!LayoutProfileSolverInternal::
			TryPrepareRequestBackedLeafRegionThroughSearchPrefixStage(
				Request,
				PrefixContinuation,
				bDeferFinalReachabilityAudit))
	{
		OutContinuation.FailureSolveResult =
			MoveTemp(PrefixContinuation.FailureSolveResult);
		return false;
	}

	OutContinuation.Context = MoveTemp(PrefixContinuation.Context);
	LayoutProfileSolverInternal::BuildPreparedSearchBranchStageFromSearchPrefixStage(
		PrefixContinuation.SearchPrefixStage,
		OutContinuation.SearchBranchStage);
	OutContinuation.bReadyForFullSolveContinuation = true;
	return true;
}

bool LayoutProfileSolverInternal::TryPrepareRequestBackedLeafRegionThroughSearchBranchApplyStage(
	const FLayoutRegionSolveRequest& Request,
	FPreparedRequestBackedLeafSearchBranchContinuation&& BranchContinuation,
	const int32 WorkItemIndex,
	FPreparedRequestBackedLeafSearchBranchApplyContinuation& OutContinuation)
{
	OutContinuation = FPreparedRequestBackedLeafSearchBranchApplyContinuation{};
	if (!BranchContinuation.bReadyForFullSolveContinuation)
	{
		OutContinuation.FailureSolveResult =
			MoveTemp(BranchContinuation.FailureSolveResult);
		return false;
	}

	OutContinuation.Context = MoveTemp(BranchContinuation.Context);
	if (!LayoutProfileSolverInternal::TryPrepareSolveContextThroughSearchBranchApplyStage(
			OutContinuation.Context,
			BranchContinuation.SearchBranchStage,
			WorkItemIndex,
			OutContinuation.SearchBranchApplyStage))
	{
		OutContinuation.FailureSolveResult =
			BuildPreparedFailureRequestBackedLeafSolveResultFromContext(
				Request,
				MoveTemp(OutContinuation.Context));
		return false;
	}

	OutContinuation.bReadyForFullSolveContinuation = true;
	return true;
}

bool LayoutProfileSolverInternal::TryPrepareRequestBackedLeafRegionThroughSearchPrefixStage(
	const FLayoutRegionSolveRequest& Request,
	FPreparedRequestBackedLeafSearchBranchApplyContinuation&& BranchApplyContinuation,
	FPreparedRequestBackedLeafSearchPrefixContinuation& OutContinuation)
{
	OutContinuation = FPreparedRequestBackedLeafSearchPrefixContinuation{};
	if (!BranchApplyContinuation.bReadyForFullSolveContinuation)
	{
		OutContinuation.FailureSolveResult =
			MoveTemp(BranchApplyContinuation.FailureSolveResult);
		return false;
	}

	OutContinuation.Context = MoveTemp(BranchApplyContinuation.Context);
	OutContinuation.bReadyForFullSolveContinuation =
		LayoutProfileSolverInternal::TryPrepareSolveContextThroughSearchPrefixStage(
			OutContinuation.Context,
			OutContinuation.SearchPrefixStage);
	if (OutContinuation.bReadyForFullSolveContinuation)
	{
		return true;
	}

	OutContinuation.FailureSolveResult =
		BuildPreparedFailureRequestBackedLeafSolveResultFromContext(
			Request,
			MoveTemp(OutContinuation.Context));
	return false;
}

LayoutProfileSolverInternal::FPreparedPlanRouteDemandSummary
LayoutProfileSolverInternal::BuildPreparedPlanRouteDemandSummaryForRequest(
	const FLayoutRegionSolveRequest& Request)
{
	FPreparedPlanRouteDemandSummary Summary;

	auto PopulateSummaryFromContext =
		[&Summary](const FSolveContext& Context)
	{
		for (const FLayoutRouteConstraintRecord& RouteConstraint :
			Context.Result.RouteConstraints)
		{
			++Summary.RouteConstraintCount;
			Summary.MainRouteConstraintCount +=
				RouteConstraint.bScoreAsMainRoute ? 1 : 0;
			if (RouteConstraint.Intent == ELayoutCellIntent::Boundary)
			{
				++Summary.BoundaryRouteConstraintCount;
				if (RouteConstraint.FaceRequirements.Num() > 1)
				{
					++Summary.BoundaryMultiFaceConstraintCount;
				}
			}
			else if (RouteConstraint.Intent == ELayoutCellIntent::Interior
				|| RouteConstraint.Intent == ELayoutCellIntent::Core)
			{
				++Summary.InteriorRouteConstraintCount;
			}
			Summary.RouteFaceRequirementCount +=
				RouteConstraint.FaceRequirements.Num();
		}

		for (const FLayoutCellReservationRecord& Reservation :
			Context.Result.CompiledReservations)
		{
			if (Reservation.ReservationKind ==
				ELayoutCellReservationKind::RequiredRoute)
			{
				++Summary.RequiredRouteReservationCount;
			}
			else if (Reservation.ReservationKind ==
				ELayoutCellReservationKind::ReachabilityBranch)
			{
				++Summary.ReachabilityBranchReservationCount;
			}
		}

		Summary.LiveDomainConstrainedCellCount =
			Context.Result.RouteDomainFilterDiagnostics.ConstrainedCellCount;
		Summary.LiveDomainBoundaryConstrainedCellCount =
			Context.Result.RouteDomainFilterDiagnostics.BoundaryConstrainedCellCount;
		Summary.LiveDomainMultiFaceBoundaryConstrainedCellCount =
			Context.Result.RouteDomainFilterDiagnostics
				.MultiFaceBoundaryConstrainedCellCount;
		Summary.LiveDomainEligibleCandidateCount =
			Context.Result.RouteDomainFilterDiagnostics.TotalEligibleCandidateCount;
		Summary.LiveDomainEliminatedCandidateCount =
			Context.Result.RouteDomainFilterDiagnostics.EliminatedCandidateCount;
		Summary.LiveDomainTightestRemainingSize =
			Context.Result.RouteDomainFilterDiagnostics.TightestRemainingDomainSize;
		Summary.LiveDomainSingleRemainingCandidateCellCount =
			Context.Result.RouteDomainFilterDiagnostics
				.SingleRemainingCandidateCellCount;
		Summary.LiveDomainAtMostFourRemainingCandidateCellCount =
			Context.Result.RouteDomainFilterDiagnostics
				.AtMostFourRemainingCandidateCellCount;
		Summary.bLiveDomainFailedConstraint =
			Context.Result.RouteDomainFilterDiagnostics.bFailedConstraint;
		Summary.LiveDomainFailedConstraintCell =
			Context.Result.RouteDomainFilterDiagnostics.FailedConstraintCell;
		Summary.LiveDomainFailedConstraintEligibleCandidateCount =
			Context.Result.RouteDomainFilterDiagnostics
				.FailedConstraintEligibleCandidateCount;
		Summary.LiveDomainFailedConstraintRequiredFaceCount =
			Context.Result.RouteDomainFilterDiagnostics
				.FailedConstraintRequiredFaceCount;
		Summary.LiveDomainFailedConstraintDominantFailureKind =
			Context.Result.RouteDomainFilterDiagnostics
				.FailedConstraintDominantFailureKind;
		Summary.LiveDomainFailedConstraintDominantFailureCount =
			Context.Result.RouteDomainFilterDiagnostics
				.FailedConstraintDominantFailureCount;
	};

	auto CaptureFailureReason =
		[&Summary](const FString& FailureReason)
	{
		FailureReason.Split(
			TEXT("\n"),
			&Summary.FailureFirstLine,
			nullptr);
		if (Summary.FailureFirstLine.IsEmpty())
		{
			Summary.FailureFirstLine = FailureReason;
		}
	};

	auto ClassifySearchPrefixFailureKind =
		[](const FString& FailureReason) -> FLayoutId
	{
		if (FailureReason.Contains(
				TEXT("Post-prefix constrained-cell feasibility rejected planned cell"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("ConstrainedCellImmediatePropagationFailure");
		}
		if (FailureReason.Contains(
				TEXT("connection tag compatibility failed with incoming boundary"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("BoundaryConnectionTagMismatch");
		}
		if (FailureReason.Contains(
				TEXT("share no ConnectedTraversalChannels"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("BoundaryTraversalChannelMismatch");
		}
		if (FailureReason.Contains(
				TEXT("not marked Must Face Region Boundary"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("BoundaryFaceNotAllowed");
		}
		if (FailureReason.Contains(
				TEXT("cannot consume incoming boundary"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("BoundaryOccupancyMismatch");
		}
		if (FailureReason.Contains(
				TEXT("requires matching yaw with incoming boundary"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("BoundaryYawMismatch");
		}
		if (FailureReason.Contains(
				TEXT("Required traversal anchor could not be connected"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("TraversalAnchorDisconnected");
		}
		if (FailureReason.Contains(
				TEXT("Route-constrained planned cell started with eligible candidates"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("RouteFaceConstraintFailure");
		}
		if (FailureReason.Contains(
				TEXT("Propagation found no legal candidates"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("PropagationNoCandidates");
		}
		if (FailureReason.Contains(
				TEXT("Forward checking rejected neighboring planned cell"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("ForwardCheckNeighborExhausted");
		}
		if (FailureReason.Contains(
				TEXT("Forced placement bundle insertion"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("ForcedBundleInsertion");
		}
		if (FailureReason.Contains(
				TEXT("Forced placement rejected cell"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("ForcedPlacementPruned");
		}
		if (FailureReason.Contains(
				TEXT("could not select an unsolved planned cell"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("NoSelectableCell");
		}
		if (FailureReason.Contains(
				TEXT("MaxSolverDurationSeconds"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("TimeBudgetExceeded");
		}
		if (FailureReason.Contains(
				TEXT("MaxSolverCandidateAttempts"),
				ESearchCase::IgnoreCase))
		{
			return TEXT("AttemptBudgetExceeded");
		}
		return FailureReason.IsEmpty() ? NAME_None : FLayoutId(TEXT("Unknown"));
	};

	auto CaptureSearchPrefixFailureReason =
		[&ClassifySearchPrefixFailureKind](
			const FString& FailureReason,
			FLayoutId& OutFailureKind,
			int32& OutFailureCount)
	{
		OutFailureKind = ClassifySearchPrefixFailureKind(FailureReason);
		OutFailureCount = OutFailureKind.IsNone() ? 0 : 1;
	};

	auto ApplySearchPrefixStageToSummary =
		[&Summary](
			const LayoutProfileSolverInternal::FPreparedSearchPrefixStageSummary& Stage,
			const bool bRepairMode)
	{
		if (bRepairMode)
		{
			Summary.RepairModeSearchPrefixPlacedCellCount = Stage.PlacedCellCount;
			Summary.RepairModeSearchPrefixForcedPlacementCellCount =
				Stage.ForcedPlacementCellCount;
			Summary.bRepairModeSearchPrefixSolvedAfterForcedPlacements =
				Stage.bSolvedAfterForcedPlacements;
			Summary.bRepairModeSearchPrefixNoCellSelectable =
				Stage.bNoCellSelectable;
			Summary.RepairModeSearchPrefixConstrainedCellCount =
				Stage.ConstrainedCellCount;
			Summary.RepairModeSearchPrefixEligibleCandidateCount =
				Stage.EligibleCandidateCount;
			Summary.RepairModeSearchPrefixTightestRemainingSize =
				Stage.TightestRemainingSize;
			Summary.RepairModeSearchPrefixSingleRemainingCandidateCellCount =
				Stage.SingleRemainingCandidateCellCount;
			Summary.RepairModeSearchPrefixAtMostFourRemainingCandidateCellCount =
				Stage.AtMostFourRemainingCandidateCellCount;
			Summary.RepairModeSearchPrefixSelectedCell = Stage.SelectedCell;
			Summary.RepairModeSearchPrefixSelectedIntent = Stage.SelectedIntent;
			Summary.RepairModeSearchPrefixSelectedDomainSize =
				Stage.SelectedDomainSize;
			Summary.RepairModeSearchPrefixDominantFailureKind =
				Stage.DominantFailureKind;
			Summary.RepairModeSearchPrefixDominantFailureCount =
				Stage.DominantFailureCount;
			return;
		}

		Summary.SearchPrefixPlacedCellCount = Stage.PlacedCellCount;
		Summary.SearchPrefixForcedPlacementCellCount =
			Stage.ForcedPlacementCellCount;
		Summary.bSearchPrefixSolvedAfterForcedPlacements =
			Stage.bSolvedAfterForcedPlacements;
		Summary.bSearchPrefixNoCellSelectable = Stage.bNoCellSelectable;
		Summary.SearchPrefixConstrainedCellCount = Stage.ConstrainedCellCount;
		Summary.SearchPrefixEligibleCandidateCount = Stage.EligibleCandidateCount;
		Summary.SearchPrefixTightestRemainingSize =
			Stage.TightestRemainingSize;
		Summary.SearchPrefixSingleRemainingCandidateCellCount =
			Stage.SingleRemainingCandidateCellCount;
		Summary.SearchPrefixAtMostFourRemainingCandidateCellCount =
			Stage.AtMostFourRemainingCandidateCellCount;
		Summary.SearchPrefixSelectedCell = Stage.SelectedCell;
		Summary.SearchPrefixSelectedIntent = Stage.SelectedIntent;
		Summary.SearchPrefixSelectedDomainSize = Stage.SelectedDomainSize;
		Summary.SearchPrefixDominantFailureKind = Stage.DominantFailureKind;
		Summary.SearchPrefixDominantFailureCount = Stage.DominantFailureCount;
	};

	FSolveContext PreparedContext;
	const bool bPrepared =
		LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughRouteDomainStage(
			Request,
			PreparedContext,
			false);
	if (PreparedContext.Result.PlannedCells.IsEmpty())
	{
		return Summary;
	}
	PopulateSummaryFromContext(PreparedContext);
	if (!bPrepared && !PreparedContext.Result.FailureReason.IsEmpty())
	{
		CaptureFailureReason(PreparedContext.Result.FailureReason);

		Summary.bRepairModeRouteDomainAttempted = true;
		FSolveContext RepairPreparedContext;
		const bool bRepairPrepared =
			TryPrepareRequestSolveContextThroughRouteDomainStageInternal(
				Request,
				RepairPreparedContext,
				false,
				true);
		Summary.bRepairModeRouteDomainSucceeded = bRepairPrepared;
		const FLayoutRouteDomainFilterDiagnostics& RepairDiagnostics =
			RepairPreparedContext.Result.RouteDomainFilterDiagnostics;
		Summary.bRepairModeLiveDomainFailedConstraint =
			RepairDiagnostics.bFailedConstraint;
		Summary.RepairModeLiveDomainFailedConstraintCell =
			RepairDiagnostics.FailedConstraintCell;
		Summary.RepairModeLiveDomainFailedConstraintEligibleCandidateCount =
			RepairDiagnostics.FailedConstraintEligibleCandidateCount;
		Summary.RepairModeLiveDomainFailedConstraintRequiredFaceCount =
			RepairDiagnostics.FailedConstraintRequiredFaceCount;
		Summary.RepairModeLiveDomainFailedConstraintDominantFailureKind =
			RepairDiagnostics.FailedConstraintDominantFailureKind;
		Summary.RepairModeLiveDomainFailedConstraintDominantFailureCount =
			RepairDiagnostics.FailedConstraintDominantFailureCount;
	}
	else if (bPrepared)
	{
		LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier
			SearchPrefixStage;
		const bool bPreparedSearchPrefix =
			TryPrepareSolveContextThroughSearchPrefixStageImpl(
				PreparedContext,
				SearchPrefixStage);
		ApplySearchPrefixStageToSummary(
			SearchPrefixStage.Summary,
			false);
		if (!bPreparedSearchPrefix
			&& !PreparedContext.Result.FailureReason.IsEmpty())
		{
			CaptureFailureReason(PreparedContext.Result.FailureReason);
			CaptureSearchPrefixFailureReason(
				PreparedContext.Result.FailureReason,
				Summary.SearchPrefixDominantFailureKind,
				Summary.SearchPrefixDominantFailureCount);

			Summary.bRepairModeSearchPrefixAttempted = true;
			FSolveContext RepairPrefixContext;
			LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier
				RepairSearchPrefixStage;
			const bool bRepairPreparedRouteDomain =
				TryPrepareRequestSolveContextThroughRouteDomainStageInternal(
					Request,
					RepairPrefixContext,
					false,
					true);
			const bool bRepairPreparedSearchPrefix =
				bRepairPreparedRouteDomain
				&& TryPrepareSolveContextThroughSearchPrefixStageImpl(
					RepairPrefixContext,
					RepairSearchPrefixStage);
			Summary.bRepairModeSearchPrefixSucceeded =
				bRepairPreparedSearchPrefix;
			if (bRepairPreparedRouteDomain)
			{
				ApplySearchPrefixStageToSummary(
					RepairSearchPrefixStage.Summary,
					true);
			}
			if (!bRepairPreparedSearchPrefix
				&& !RepairPrefixContext.Result.FailureReason.IsEmpty())
			{
				CaptureSearchPrefixFailureReason(
					RepairPrefixContext.Result.FailureReason,
					Summary.RepairModeSearchPrefixDominantFailureKind,
					Summary.RepairModeSearchPrefixDominantFailureCount);
			}
		}
	}

	return Summary;
}

FLayoutRegionSolveResult LayoutProfileSolverInternal::SolveRegionIgnoringLiveCompositePlacementGuardForTests(
	const FLayoutRegionSolveRequest& Request)
{
	return SolveRequestBackedRegion(Request, false);
}

bool FLayoutModuleSolveSnapshot::SupportsIntent(const ELayoutCellIntent Intent) const
{
	return SupportedCellIntents.Contains(Intent);
}

bool FLayoutModuleSolveSnapshot::SupportsRootIntent(const ELayoutCellIntent Intent) const
{
	return RootSupportedCellIntents.Contains(Intent);
}

bool FLayoutModuleSolveSnapshot::ExposesTraversalChannel(const FGameplayTag& WalkableArea) const
{
	return TraversalChannels.HasTagExact(WalkableArea);
}

int32 FLayoutProfileSolver::DeriveRegionSeed(
	const int32 WorldSeed,
	const FIntVector& LayoutLocationCell,
	const FName PlanningContextName,
	const FString& RegionDebugPath,
	const FName LayoutProfileName)
{
	uint32 SeedHash = static_cast<uint32>(WorldSeed);
	SeedHash = HashCombineFast(SeedHash, GetTypeHash(LayoutLocationCell));
	SeedHash = HashCombineFast(SeedHash, GetTypeHash(PlanningContextName));
	SeedHash = HashCombineFast(SeedHash, GetTypeHash(RegionDebugPath));
	SeedHash = HashCombineFast(SeedHash, GetTypeHash(LayoutProfileName));
	return static_cast<int32>(SeedHash);
}

static void InitializeRequestBackedRegionResultFromRequest(
	const FLayoutRegionSolveRequest& Request,
	FLayoutRegionSolveResult& OutRegionResult)
{
	OutRegionResult.RegionDebugPath = Request.RegionDebugPath;
	OutRegionResult.RegionCellOffset = Request.RegionCellOffset;
	OutRegionResult.SourceContentEntryId = Request.SourceContentEntryId;
	OutRegionResult.CommittedEndpointAnchors = Request.CommittedEndpointAnchors;
	OutRegionResult.NegotiatedChildResponsibilityContracts =
		Request.NegotiatedChildResponsibilityContracts;
	OutRegionResult.ProofRecords = Request.ProofRecords;
	OutRegionResult.ValidationAssertions = Request.ValidationAssertions;
	OutRegionResult.SteppedTerrainSupportMap = Request.SteppedTerrainSupportMap;
	OutRegionResult.ForcedPlacementBundleInsertions =
		Request.ForcedPlacementBundleInsertions;
	OutRegionResult.RequiredRouteConstraints = Request.RequiredRouteConstraints;
	OutRegionResult.FutureTerraceProofBoundaryPoints =
		Request.FutureTerraceProofBoundaryPoints;

	for (const FLayoutValidationAssertionRecord& Assertion :
		Request.ValidationAssertions)
	{
		if (!Assertion.bPassed)
		{
			FLayoutValidationMessage& Message =
				OutRegionResult.SolveResult.Messages.AddDefaulted_GetRef();
			Message.Severity = ELayoutValidationSeverity::Error;
			Message.Message = Assertion.FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Snapshot assertion '%s' failed."),
					*Assertion.AssertionId.ToString())
				: Assertion.FailureReason;
		}
	}
}

static bool TryFinalizeEarlyRequestBackedRegionFailure(
	const FLayoutRegionSolveRequest& Request,
	FLayoutRegionSolveResult& InOutRegionResult)
{
	if (const FLayoutValidationAssertionRecord* FailedRequestAssertion =
		FindFailedBlockingStandaloneRequestContractAssertion(Request))
	{
		InOutRegionResult.SolveResult.FailureReason =
			FailedRequestAssertion->FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Region solve request '%s' failed blocking request-contract assertion '%s' before leaf solve execution."),
					*Request.RegionDebugPath,
					*FailedRequestAssertion->AssertionId.ToString())
				: FailedRequestAssertion->FailureReason;
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
			InOutRegionResult.SolveResult);
		return true;
	}

	InOutRegionResult.SolveResult.Seed = Request.Seed;
	InOutRegionResult.SolveResult.SharedCellSizeInBlocks =
		ResolveRequestSharedCellSizeInBlocks(Request);
	InOutRegionResult.SolveResult.TemplatePlacementZOffsetBlocks =
		Request.TemplatePlacementZOffsetBlocks;
	InOutRegionResult.SolveResult.RootPlacementKind =
		Request.RootPlacementKind;
	InOutRegionResult.SolveResult.WorldBindingPlacementPolicy =
		Request.WorldBindingPlacementPolicy;
	PopulateResolvedTerrainAlignmentLevel(
		Request,
		InOutRegionResult.SolveResult);
	if (!Request.PlannedCells.IsEmpty()
		&& (Request.FootprintSize.X <= 0 || Request.FootprintSize.Y <= 0))
	{
		InOutRegionResult.SolveResult.FailureReason =
			TEXT("Region solve with supplied planned cells requires a positive footprint.");
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
			InOutRegionResult.SolveResult);
		return true;
	}

	InOutRegionResult.SolveResult.Messages.Append(
		Request.ModuleCatalog.Validation.Messages);
	InOutRegionResult.SolveResult.Messages.Append(
		Request.ContentSetSnapshot.Validation.Messages);
	InOutRegionResult.SolveResult.Messages.Append(
		Request.ProfileSnapshot.Validation.Messages);
	if (!Request.ModuleCatalog.Validation.IsValid()
		|| !Request.ContentSetSnapshot.Validation.IsValid()
		|| !Request.ProfileSnapshot.Validation.IsValid())
	{
		InOutRegionResult.SolveResult.FailureReason =
			TEXT("Region solve aborted because the content set, module solve snapshot, or profile failed validation.");
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
			InOutRegionResult.SolveResult);
		return true;
	}

	FString StaticProviderFailureReason;
	if (!LayoutZoneFeatureDemand::ValidateStaticProviderPotential(
			Request.EffectiveSnapshotId,
			Request.ContentSetSnapshot.SnapshotId,
			Request.ProfileSnapshot.ZoneFeatureRequirements,
			Request.ContentSetSnapshot.Entries,
			Request.PrecommittedZoneFeatureProviderCommitments,
			StaticProviderFailureReason))
	{
		InOutRegionResult.SolveResult.PreparationFailureKind =
			ELayoutSolvePreparationFailureKind::ZoneFeatureProviderCapacityInfeasible;
		InOutRegionResult.SolveResult.FailureReason =
			MoveTemp(StaticProviderFailureReason);
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
			InOutRegionResult.SolveResult);
		return true;
	}

	return false;
}

bool LayoutProfileSolverInternal::TryPrepareSparseStructuralLocalSolveView(
	const FLayoutRegionSolveRequest& Request,
	const TSet<FIntVector>& LocalWorkCells,
	FSolveContext& OutLocalContext,
	FString& OutFailureReason)
{
	FLayoutRegionSolveResult RegionResult;
	InitializeRequestBackedRegionResultFromRequest(Request, RegionResult);
	if (TryFinalizeEarlyRequestBackedRegionFailure(Request, RegionResult))
	{
		OutFailureReason = RegionResult.SolveResult.FailureReason;
		return false;
	}
	// Stop at canonical topology. Sparse local domains must not depend on a
	// successful full-parent route/search-prefix proof before they can exist.
	const FSolveContext OwningContext = BuildLiveSolveContextFromRequest(Request, RegionResult.SolveResult);
	if (!OwningContext.Result.bSucceeded || !OwningContext.Result.FailureReason.IsEmpty())
	{
		OutFailureReason = OwningContext.Result.FailureReason;
		return false;
	}
	return TryBuildSparseStructuralLocalSolveView(OwningContext, LocalWorkCells, OutLocalContext, OutFailureReason);
}

FLayoutRegionSolveResult LayoutProfileSolverInternal::BuildRequestBackedRegionPreparedTopology(
	const FLayoutRegionSolveRequest& Request)
{
	FLayoutRegionSolveResult RegionResult;
	InitializeRequestBackedRegionResultFromRequest(Request, RegionResult);
	if (TryFinalizeEarlyRequestBackedRegionFailure(Request, RegionResult))
	{
		return RegionResult;
	}

	FSolveContext Context = BuildLiveSolveContextFromRequest(
		Request,
		RegionResult.SolveResult);
	RegionResult.SolveResult = MoveTemp(Context.Result);
	return RegionResult;
}

FLayoutRegionSolveResult LayoutProfileSolverInternal::BuildRequestBackedChildIntrinsicPreparedTopology(
	const FLayoutRegionSolveRequest& Request)
{
	FLayoutRegionSolveResult RegionResult;
	InitializeRequestBackedRegionResultFromRequest(Request, RegionResult);
	if (TryFinalizeEarlyRequestBackedRegionFailure(Request, RegionResult))
	{
		return RegionResult;
	}

	FSolveContext Context = BuildLiveSolveContextFromRequest(
		Request,
		RegionResult.SolveResult,
		true);
	RegionResult.SolveResult = MoveTemp(Context.Result);
	return RegionResult;
}

bool LayoutProfileSolverInternal::TryFindFirstUnoccupiedGeneratedBridgeCell(
	const FLayoutSolveResult& SolveResult,
	FLayoutPlannedCell& OutBridgeCell)
{
	for (const FLayoutResidualCellRecord& ResidualCell : SolveResult.ResidualUnoccupiedCells)
	{
		const FLayoutPlannedCell* const PlannedCell = SolveResult.PlannedCells.FindByPredicate(
			[&ResidualCell](const FLayoutPlannedCell& Candidate)
			{
				return Candidate.Cell == ResidualCell.Cell;
			});
		if (PlannedCell != nullptr && PlannedCell->bIsBridgeCell
			&& (!PlannedCell->bIsTopBridgeOffer || PlannedCell->VerticalAccessLandingContactMask != 0
				|| PlannedCell->Intent == ELayoutCellIntent::VerticalAccess
				|| PlannedCell->Intent == ELayoutCellIntent::Connector
				|| PlannedCell->Intent == ELayoutCellIntent::Entry))
		{
			OutBridgeCell = *PlannedCell;
			return true;
		}
	}
	return false;
}

static void FinalizeRequestBackedLeafRegionResultFromContext(
	const FLayoutRegionSolveRequest& Request,
	LayoutProfileSolverInternal::FSolveContext&& Context,
	FLayoutRegionSolveResult& InOutRegionResult)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_LeafResultFinalize, STAT_PorismLayout_LeafResultFinalize);
	if (Context.Result.bSucceeded)
	{
		Context.Result.FailureReason.Reset();
		Context.Result.ExportedEntryCells.Reset();
		AppendContextPlacementsToResult(Context, Context.Result);
		SortResultPlacements(Context.Result);
	}

	InOutRegionResult.SolveResult = MoveTemp(Context.Result);
	InOutRegionResult.SolveResult.SharedCellSizeInBlocks =
		ResolveRequestSharedCellSizeInBlocks(Request);
	InOutRegionResult.SolveResult.TemplatePlacementZOffsetBlocks =
		Request.TemplatePlacementZOffsetBlocks;
	InOutRegionResult.SolveResult.RootPlacementKind =
		Request.RootPlacementKind;
	InOutRegionResult.SolveResult.WorldBindingPlacementPolicy =
		Request.WorldBindingPlacementPolicy;
	PopulateResolvedTerrainAlignmentLevel(
		Request,
		InOutRegionResult.SolveResult);
	ApplyStagedTerrainArtifactsToRegionResult(
		Request,
		InOutRegionResult,
		&Request.NegotiatedChildResponsibilityContracts);
	LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
		InOutRegionResult.SolveResult);
	if (InOutRegionResult.SolveResult.bSucceeded)
	{
		FLayoutPlannedCell UnoccupiedBridgeCell;
		if (LayoutProfileSolverInternal::TryFindFirstUnoccupiedGeneratedBridgeCell(
				InOutRegionResult.SolveResult,
				UnoccupiedBridgeCell))
		{
			InOutRegionResult.SolveResult.bSucceeded = false;
			InOutRegionResult.SolveResult.FailureReason = FString::Printf(
				TEXT("Generated bridge cell %s (authored level %d) is unoccupied. Check module face rules for bridge cell positions."),
				*UnoccupiedBridgeCell.Cell.ToString(),
				UnoccupiedBridgeCell.ModuleLevelIndex);
		}
	}
	if (InOutRegionResult.SolveResult.bSucceeded)
	{
		FString SparseFailureReason;
		if (!LayoutProfileSolverInternal::ApplySparsePlacementRulesToSolveResult(
				Request.ProfileSnapshot,
				Request.RegionDebugPath,
				Request.Seed,
				InOutRegionResult.SolveResult,
				SparseFailureReason))
		{
			InOutRegionResult.SolveResult.bSucceeded = false;
			InOutRegionResult.SolveResult.FailureReason = SparseFailureReason;
		}
		else
		{
			FString ZoneFeatureFailureReason;
			if (!EvaluateLeafZoneFeatureRequirementsForResult(
					Request,
					InOutRegionResult.SolveResult,
					ZoneFeatureFailureReason))
			{
				InOutRegionResult.SolveResult.bSucceeded = false;
				InOutRegionResult.SolveResult.FailureReason =
					ZoneFeatureFailureReason;
			}
		}
	}
}

/**
 * Finalizes one prepared request-backed leaf failure solve state without
 * rebuilding the heavier success-only result artifacts.
 *
 * This narrower path serves staged parent-proof continuations because deferred
 * validation only needs the exact failure solve result and diagnostics;
 * exported boundary points and residual-cell materialization stay deferred
 * until a prepared solve actually succeeds.
 */
static FLayoutSolveResult BuildPreparedFailureRequestBackedLeafSolveResultFromContext(
	const FLayoutRegionSolveRequest& Request,
	LayoutProfileSolverInternal::FSolveContext&& Context)
{
	FLayoutSolveResult SolveResult = MoveTemp(Context.Result);
	SolveResult.SharedCellSizeInBlocks =
		ResolveRequestSharedCellSizeInBlocks(Request);
	SolveResult.TemplatePlacementZOffsetBlocks =
		Request.TemplatePlacementZOffsetBlocks;
	SolveResult.RootPlacementKind =
		Request.RootPlacementKind;
	SolveResult.WorldBindingPlacementPolicy =
		Request.WorldBindingPlacementPolicy;
	PopulateResolvedTerrainAlignmentLevel(
		Request,
		SolveResult);
	return SolveResult;
}

/**
 * Rewraps one already-prepared leaf failure solve state into the normal
 * request-backed region result shape for compatibility-only callers.
 */
static void FinalizePreparedFailureRequestBackedLeafRegionResultFromSolveResult(
	const FLayoutRegionSolveRequest& Request,
	FLayoutSolveResult&& FailureSolveResult,
	FLayoutRegionSolveResult& InOutRegionResult)
{
	InOutRegionResult.SolveResult = MoveTemp(FailureSolveResult);
}

FLayoutRegionSolveResult
LayoutProfileSolverInternal::BuildRequestBackedLeafRegionResultFromPreparedContext(
	const FLayoutRegionSolveRequest& Request,
	FSolveContext&& Context,
	const bool bPublishTrace)
{
	FLayoutRegionSolveResult RegionResult;
	InitializeRequestBackedRegionResultFromRequest(Request, RegionResult);
	if (TryFinalizeEarlyRequestBackedRegionFailure(Request, RegionResult))
	{
		return RegionResult;
	}

	if (bPublishTrace)
	{
		PublishTraceIfNeeded(Context);
	}

	if (!Context.Result.bSucceeded)
	{
		FinalizePreparedFailureRequestBackedLeafRegionResultFromSolveResult(
			Request,
			BuildPreparedFailureRequestBackedLeafSolveResultFromContext(
				Request,
				MoveTemp(Context)),
			RegionResult);
		return RegionResult;
	}

	FinalizeRequestBackedLeafRegionResultFromContext(
		Request,
		MoveTemp(Context),
		RegionResult);
	return RegionResult;
}

static EPreparedSearchBranchOutcome
TrySolvePreparedRequestBackedSearchBranchWorkItemWithPostBranchPrefix(
	FSolveContext& Context,
	const int32 SolveDepth,
	const FPreparedSearchBranchWorkItem& WorkItem,
	const bool bValidateClosureContracts,
	TArray<FString>& InOutCandidateFailureReasons)
{
	FPreparedSearchBranchApplyStageResult ApplyStage;
	const EPreparedSearchBranchApplyOutcome ApplyOutcome =
		TryApplyPreparedSearchBranchWorkItemStage(
			Context,
			SolveDepth,
			WorkItem,
			ApplyStage);
	if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::AbortSolve)
	{
		return EPreparedSearchBranchOutcome::AbortSolve;
	}

	if (ApplyOutcome == EPreparedSearchBranchApplyOutcome::ReadyForRecursiveContinuation)
	{
		LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier
			PostBranchPrefixStage;
		const bool bPreparedPostBranchPrefix =
			TryPrepareSolveContextThroughSearchPrefixStageImpl(
				Context,
				PostBranchPrefixStage);
		if (bPreparedPostBranchPrefix
			&& ContinuePreparedSolveContextAfterSearchPrefixStageImpl(
				Context,
				PostBranchPrefixStage,
				bValidateClosureContracts))
		{
			return EPreparedSearchBranchOutcome::Solved;
		}

		if (!bPreparedPostBranchPrefix)
		{
			RollbackForcedPlacements(
				Context,
				PostBranchPrefixStage.ForcedCells);
		}

		if (Context.bTimeBudgetExceeded
			|| Context.CandidateAttemptCount >= Context.MaxCandidateAttempts)
		{
			RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
			return EPreparedSearchBranchOutcome::AbortSolve;
		}
	}

	if (!ApplyStage.ForwardCheckFailure.IsEmpty()
		&& InOutCandidateFailureReasons.Num() < Context.MaxFailureDetails)
	{
		if (ApplyStage.bOccupiedCandidate)
		{
			InOutCandidateFailureReasons.Add(FString::Printf(
				TEXT("%s: %s"),
				*BuildCandidateDebugName(Context, ApplyStage.WorkItem.Candidate),
				*ApplyStage.ForwardCheckFailure));
			AddTraceEvent(Context, FString::Printf(
				TEXT("pruned-candidate cell=%s candidate=%s reason=%s"),
				*ApplyStage.WorkItem.Cell.ToString(),
				*BuildCandidateDebugName(
					Context,
					ApplyStage.WorkItem.Candidate),
				*ApplyStage.ForwardCheckFailure));
		}
		else
		{
			InOutCandidateFailureReasons.Add(ApplyStage.ForwardCheckFailure);
			AddTraceEvent(Context, FString::Printf(
				TEXT("pruned-empty cell=%s reason=%s"),
				*ApplyStage.WorkItem.Cell.ToString(),
				*ApplyStage.ForwardCheckFailure));
		}
	}
	else if (!Context.Result.FailureReason.IsEmpty())
	{
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty()
			&& Context.PlannedCellIntents.FindRef(ApplyStage.WorkItem.Cell)
				== ELayoutCellIntent::VerticalAccess)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[VerticalAccessPreparedCandidateBacktrack] region=%s cell=%s candidate=%s reason=%s"),
				*Context.RegionDebugPath,
				*ApplyStage.WorkItem.Cell.ToString(),
				*BuildCandidateDebugName(Context, ApplyStage.WorkItem.Candidate),
				*Context.Result.FailureReason);
		}
		if (ApplyStage.bOccupiedCandidate)
		{
			AddTraceEvent(Context, FString::Printf(
				TEXT("backtrack-candidate cell=%s candidate=%s reason=%s"),
				*ApplyStage.WorkItem.Cell.ToString(),
				*BuildCandidateDebugName(
					Context,
					ApplyStage.WorkItem.Candidate),
				*Context.Result.FailureReason));
		}
		else
		{
			AddTraceEvent(Context, FString::Printf(
				TEXT("backtrack-empty cell=%s reason=%s"),
				*ApplyStage.WorkItem.Cell.ToString(),
				*Context.Result.FailureReason));
		}
		AddCandidateFailureDetail(
			Context,
			InOutCandidateFailureReasons,
			ApplyStage.WorkItem.Candidate,
			Context.Result.FailureReason);
		ClearBacktrackableFailure(Context);
	}

	++Context.Result.PropagationStats.BacktrackCount;
	++Context.Result.PropagationStats.TerrainStageBacktrackCount;
	RollbackPreparedSearchBranchApplyStage(Context, ApplyStage);
	return EPreparedSearchBranchOutcome::Rejected;
}

static bool ContinuePreparedRequestBackedLeafSolveAfterSearchBranchStageImpl(
	FSolveContext& Context,
	const LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier& SearchBranchStage,
	const bool bValidateClosureContracts)
{
	const LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier&
		SearchPrefixStage = SearchBranchStage.SearchPrefixStage;
	if (SearchPrefixStage.bSolvedAfterForcedPlacements)
	{
		return ContinuePreparedSolveContextAfterSearchBranchStageImpl(
			Context,
			SearchBranchStage,
			bValidateClosureContracts);
	}

	const FIntVector& Cell = SearchPrefixStage.SelectedCell;
	const ELayoutCellIntent Intent = SearchPrefixStage.Summary.SelectedIntent;
	TArray<FString> CandidateFailureReasons;
	AddTraceEvent(Context, FString::Printf(
		TEXT("select-cell depth=%d cell=%s intent=%s candidates=%d placements=%d attempts=%d"),
		0,
		*Cell.ToString(),
		ToDebugString(Intent),
		SearchBranchStage.WorkItems.Num(),
		Context.Placements.Num(),
		Context.CandidateAttemptCount));
	if (Context.bIncludeTraceCandidateDetails && IsTracingEnabled(Context))
	{
		TArray<FString> CandidateNames;
		const int32 MaxCandidateNames =
			FMath::Min(SearchBranchStage.WorkItems.Num(), 12);
		CandidateNames.Reserve(MaxCandidateNames);
		for (int32 CandidateIndex = 0; CandidateIndex < MaxCandidateNames;
			++CandidateIndex)
		{
			const FSolveCandidate& Candidate =
				SearchBranchStage.WorkItems[CandidateIndex].Candidate;
			CandidateNames.Add(
				LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate)
					? BuildCandidateDebugName(Context, Candidate)
					: TEXT("<empty>"));
		}

		AddTraceEvent(Context, FString::Printf(
			TEXT("candidate-order cell=%s intent=%s firstCandidates=%s%s"),
			*Cell.ToString(),
			ToDebugString(Intent),
			CandidateNames.IsEmpty()
				? TEXT("<none>")
				: *FString::Join(CandidateNames, TEXT(" | ")),
			SearchBranchStage.WorkItems.Num() > MaxCandidateNames
				? TEXT(" | ...")
				: TEXT("")));
	}
	if (SearchBranchStage.WorkItems.IsEmpty())
	{
		GetLegalCandidatesForCellIndexed(
			Context,
			Cell,
			&CandidateFailureReasons);
		Context.Result.FailureReason = FString::Printf(
			TEXT("No legal module candidates remain for intent %s(%d) at cell %s after CSP propagation. Cell context: %s. %s. Candidate failures: %s"),
			ToDebugString(Intent),
			static_cast<int32>(Intent),
			*Cell.ToString(),
			*BuildCellDebugString(Context, Cell),
			*BuildDomainDebugString(Context, Cell),
			CandidateFailureReasons.IsEmpty()
				? TEXT("<none>")
				: *FString::Join(CandidateFailureReasons, TEXT(" | ")));
		AddTraceEvent(Context, FString::Printf(
			TEXT("domain-empty cell=%s reason=%s"),
			*Cell.ToString(),
			*Context.Result.FailureReason));
		MemoizeFailedState(Context);
		RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
		return false;
	}

	for (const FPreparedSearchBranchWorkItem& BranchWorkItem :
		SearchBranchStage.WorkItems)
	{
		const EPreparedSearchBranchOutcome BranchOutcome =
			TrySolvePreparedRequestBackedSearchBranchWorkItemWithPostBranchPrefix(
				Context,
				0,
				BranchWorkItem,
				bValidateClosureContracts,
				CandidateFailureReasons);
		if (BranchOutcome == EPreparedSearchBranchOutcome::Solved)
		{
			return true;
		}
		if (BranchOutcome == EPreparedSearchBranchOutcome::AbortSolve)
		{
			RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
			return false;
		}
	}

	if (Context.Result.FailureReason.IsEmpty())
	{
		const FString IntentText = FString::Printf(
			TEXT("%s(%d)"),
			ToDebugString(Intent),
			static_cast<int32>(Intent));
		if (!CandidateFailureReasons.IsEmpty())
		{
			Context.Result.FailureReason = FString::Printf(
				TEXT("No compatible module candidate could satisfy intent %s at cell %s. Cell context: %s. Candidate failures: %s"),
				*IntentText,
				*Cell.ToString(),
				*BuildCellDebugString(Context, Cell),
				*FString::Join(CandidateFailureReasons, TEXT(" | ")));
		}
		else
		{
			Context.Result.FailureReason = FString::Printf(
				TEXT("No compatible module candidate could satisfy intent %s at cell %s. Cell context: %s."),
				*IntentText,
				*Cell.ToString(),
				*BuildCellDebugString(Context, Cell));
		}
	}

	MemoizeFailedState(Context);
	AddTraceEvent(Context, FString::Printf(
		TEXT("memoize-failed-state depth=%d cell=%s placements=%d attempts=%d"),
		0,
		*Cell.ToString(),
		Context.Placements.Num(),
		Context.CandidateAttemptCount));
	RollbackForcedPlacements(Context, SearchPrefixStage.ForcedCells);
	return false;
}

/** Retains largest solved lower-level prefix after a prepared multilevel failure. */
static void TryBuildRetainedLowerLevelPartial(
	const FLayoutRegionSolveRequest& Request,
	const LayoutProfileSolverInternal::FSolveContext& PreparedContext,
	FLayoutSolveResult& InOutFailedResult,
	bool bValidateLiveCompositePlacementGuard);

FLayoutRegionSolveResult
LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchPrefixStage(
	const FLayoutRegionSolveRequest& Request,
	FSolveContext&& Context,
	const FPreparedSearchPrefixStageCarrier& SearchPrefixStage)
{
	FPreparedSearchBranchStageCarrier SearchBranchStage;
	LayoutProfileSolverInternal::BuildPreparedSearchBranchStageFromSearchPrefixStage(
		SearchPrefixStage,
		SearchBranchStage);
	return LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchBranchStage(
		Request,
		MoveTemp(Context),
		SearchBranchStage);
}

FLayoutRegionSolveResult
LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchBranchStage(
	const FLayoutRegionSolveRequest& Request,
	FSolveContext&& Context,
	const FPreparedSearchBranchStageCarrier& SearchBranchStage)
{
	ContinuePreparedRequestBackedLeafSolveAfterSearchBranchStageImpl(
		Context,
		SearchBranchStage,
		!Request.bDeferClosureValidationToSchedule);
	if (!Context.Result.bSucceeded)
	{
		TryBuildRetainedLowerLevelPartial(Request, Context, Context.Result, true);
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(Context.Result);
	}
	return LayoutProfileSolverInternal::BuildRequestBackedLeafRegionResultFromPreparedContext(
		Request,
		MoveTemp(Context),
		false);
}

FLayoutRegionSolveResult
LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchBranchApplyStage(
	const FLayoutRegionSolveRequest& Request,
	FSolveContext&& Context,
	const FPreparedSearchBranchApplyStageCarrier& SearchBranchApplyStage)
{
	LayoutProfileSolverInternal::ContinuePreparedSolveContextAfterSearchBranchApplyStage(
		Context,
		SearchBranchApplyStage,
		!Request.bDeferClosureValidationToSchedule);
	if (!Context.Result.bSucceeded)
	{
		TryBuildRetainedLowerLevelPartial(Request, Context, Context.Result, true);
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(Context.Result);
	}
	return LayoutProfileSolverInternal::BuildRequestBackedLeafRegionResultFromPreparedContext(
		Request,
		MoveTemp(Context),
		false);
}

FLayoutRegionSolveResult
LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchPrefixStage(
	const FLayoutRegionSolveRequest& Request,
	FPreparedRequestBackedLeafSearchPrefixContinuation&& Continuation)
{
	if (!Continuation.bReadyForFullSolveContinuation)
	{
		FLayoutRegionSolveResult FailureRegionResult;
		InitializeRequestBackedRegionResultFromRequest(
			Request,
			FailureRegionResult);
		FinalizePreparedFailureRequestBackedLeafRegionResultFromSolveResult(
			Request,
			MoveTemp(Continuation.FailureSolveResult),
			FailureRegionResult);
		TryBuildRetainedLowerLevelPartial(
			Request,
			Continuation.Context,
			FailureRegionResult.SolveResult,
			true);
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
			FailureRegionResult.SolveResult);
		return FailureRegionResult;
	}

	return LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchPrefixStage(
		Request,
		MoveTemp(Continuation.Context),
		Continuation.SearchPrefixStage);
}

FLayoutRegionSolveResult
LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchBranchStage(
	const FLayoutRegionSolveRequest& Request,
	FPreparedRequestBackedLeafSearchBranchContinuation&& Continuation)
{
	if (!Continuation.bReadyForFullSolveContinuation)
	{
		FLayoutRegionSolveResult FailureRegionResult;
		InitializeRequestBackedRegionResultFromRequest(
			Request,
			FailureRegionResult);
		FinalizePreparedFailureRequestBackedLeafRegionResultFromSolveResult(
			Request,
			MoveTemp(Continuation.FailureSolveResult),
			FailureRegionResult);
		TryBuildRetainedLowerLevelPartial(
			Request,
			Continuation.Context,
			FailureRegionResult.SolveResult,
			true);
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
			FailureRegionResult.SolveResult);
		return FailureRegionResult;
	}

	return LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchBranchStage(
		Request,
		MoveTemp(Continuation.Context),
		Continuation.SearchBranchStage);
}

FLayoutRegionSolveResult
LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchBranchApplyStage(
	const FLayoutRegionSolveRequest& Request,
	FPreparedRequestBackedLeafSearchBranchApplyContinuation&& Continuation)
{
	if (!Continuation.bReadyForFullSolveContinuation)
	{
		FLayoutRegionSolveResult FailureRegionResult;
		InitializeRequestBackedRegionResultFromRequest(
			Request,
			FailureRegionResult);
		FinalizePreparedFailureRequestBackedLeafRegionResultFromSolveResult(
			Request,
			MoveTemp(Continuation.FailureSolveResult),
			FailureRegionResult);
		TryBuildRetainedLowerLevelPartial(
			Request,
			Continuation.Context,
			FailureRegionResult.SolveResult,
			true);
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
			FailureRegionResult.SolveResult);
		return FailureRegionResult;
	}

	return LayoutProfileSolverInternal::ContinuePreparedRequestBackedLeafRegionAfterSearchBranchApplyStage(
		Request,
		MoveTemp(Continuation.Context),
		Continuation.SearchBranchApplyStage);
}

/** Retains largest solved lower-level prefix while preserving complete failed plan for preview. */
static void TryBuildRetainedLowerLevelPartial(
	const FLayoutRegionSolveRequest& Request,
	const LayoutProfileSolverInternal::FSolveContext& PreparedContext,
	FLayoutSolveResult& InOutFailedResult,
	const bool bValidateLiveCompositePlacementGuard)
{
	const TArray<FLayoutPlannedCell>& FullPlannedCells = PreparedContext.Result.PlannedCells;
	TSet<FIntVector> FullPlannedCellSet;
	int32 HighestPlannedLevel = MIN_int32;
	int32 LowestPlannedLevel = MAX_int32;
	FullPlannedCellSet.Reserve(FullPlannedCells.Num());
	for (const FLayoutPlannedCell& PlannedCell : FullPlannedCells)
	{
		FullPlannedCellSet.Add(PlannedCell.Cell);
		HighestPlannedLevel = FMath::Max(HighestPlannedLevel, PlannedCell.Cell.Z);
		LowestPlannedLevel = FMath::Min(LowestPlannedLevel, PlannedCell.Cell.Z);
	}
	if (HighestPlannedLevel == MIN_int32 || LowestPlannedLevel == MAX_int32)
	{
		return;
	}

	// Keep each candidate lower prefix unified so VerticalAccess and its landing
	// remain a strict pair. Try shorter prefixes when a failed middle level
	// prevents the largest lower prefix from solving.
	for (int32 Level = HighestPlannedLevel; Level > LowestPlannedLevel; --Level)
	{
		TSet<FIntVector> RequiredCutLandings;
		for (const FLayoutPlannedCell& PlannedCell : FullPlannedCells)
		{
			if (PlannedCell.Intent != ELayoutCellIntent::VerticalAccess)
			{
				continue;
			}

			const FIntVector LandingCell = PlannedCell.Cell + FIntVector(0, 0, 1);
			if (LandingCell.Z == Level && FullPlannedCellSet.Contains(LandingCell))
			{
				RequiredCutLandings.Add(LandingCell);
			}
		}

		TArray<FLayoutPlannedCell> LevelCells;
		for (const FLayoutPlannedCell& PlannedCell : FullPlannedCells)
		{
			if (PlannedCell.Cell.Z < Level
				|| RequiredCutLandings.Contains(PlannedCell.Cell))
			{
				LevelCells.Add(PlannedCell);
			}
		}
		if (LevelCells.IsEmpty())
		{
			continue;
		}

		TSet<FIntVector> LevelCellSet;
		LevelCellSet.Reserve(LevelCells.Num());
		for (const FLayoutPlannedCell& LevelCell : LevelCells)
		{
			LevelCellSet.Add(LevelCell.Cell);
		}
		TArray<FLayoutSolveBoundaryPoint> LevelBoundaryPoints;
		for (const FLayoutSolveBoundaryPoint& BoundaryPoint : Request.IncomingBoundaryPoints)
		{
			if (LevelCellSet.Contains(BoundaryPoint.LocalCell))
			{
				LevelBoundaryPoints.Add(BoundaryPoint);
			}
		}
		TArray<FLayoutCommittedEndpointAnchor> LevelEndpointAnchors;
		for (const FLayoutCommittedEndpointAnchor& EndpointAnchor : Request.CommittedEndpointAnchors)
		{
			if (LevelCellSet.Contains(EndpointAnchor.LocalCell))
			{
				LevelEndpointAnchors.Add(EndpointAnchor);
			}
		}
		TArray<FLayoutCommittedTraversalAnchor> LevelTraversalAnchors;
		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : Request.CommittedTraversalAnchors)
		{
			if (LevelCellSet.Contains(TraversalAnchor.Cell))
			{
				LevelTraversalAnchors.Add(TraversalAnchor);
			}
		}

		FLayoutSolveResult LevelBase = PreparedContext.Result;
		LevelBase.bSucceeded = false;
		LevelBase.PlannedCells = LevelCells;
		LevelBase.Placements.Reset();
		LevelBase.ExportedEntryCells.Reset();
		LevelBase.FailureReason.Reset();
		const TMap<FIntVector, uint8> ExternalNeighborFaces =
			BuildExternalPlannedNeighborFaceMasks(LevelCells, FullPlannedCellSet);
		const FLayoutSolveResult LevelResult = SolveUnifiedVerticalRegions(
			Request.ProfileSnapshot,
			&Request.ExecutionSettings,
			Request.Seed,
			LevelBase,
			PreparedContext.FootprintSize,
			LevelCells,
			true,
			&Request.ModuleCatalog,
			&LevelBoundaryPoints,
			&LevelEndpointAnchors,
			&LevelTraversalAnchors,
			PreparedContext.bUsesChildSolveContext,
			Request.bDeferTraversalValidationToSchedule,
			bValidateLiveCompositePlacementGuard,
			&Request.PrecomputedFrozenTerrainContract,
			&ExternalNeighborFaces,
			nullptr,
			nullptr,
			Request.bHasSelectedModePlan
				? Request.SelectedModePlan.Scope
				: ELayoutContractRegionScope::Root,
			&Request);
		if (!LevelResult.bSucceeded || LevelResult.Placements.IsEmpty())
		{
			continue;
		}

		bool bAllPlacementsFitSolvedPrefix = true;
		for (const FLayoutPlacedModule& Placement : LevelResult.Placements)
		{
			const TArray<FIntVector>& OccupiedLocalCells = Placement.OccupiedLocalCells;
			if (OccupiedLocalCells.IsEmpty())
			{
				bAllPlacementsFitSolvedPrefix &= LevelCellSet.Contains(Placement.Cell);
				continue;
			}
			for (const FIntVector& OccupiedLocalCell : OccupiedLocalCells)
			{
				if (!LevelCellSet.Contains(Placement.Cell + OccupiedLocalCell))
				{
					bAllPlacementsFitSolvedPrefix = false;
					break;
				}
			}
			if (!bAllPlacementsFitSolvedPrefix)
			{
				break;
			}
		}
		if (!bAllPlacementsFitSolvedPrefix
			|| LevelResult.Placements.Num() <= InOutFailedResult.Placements.Num())
		{
			continue;
		}

		InOutFailedResult.Placements = LevelResult.Placements;
		InOutFailedResult.ExportedEntryCells.Reset();
		for (const FLayoutPlacedModule& Placement : InOutFailedResult.Placements)
		{
			if (Placement.Intent == ELayoutCellIntent::Entry
				|| Placement.Intent == ELayoutCellIntent::Connector)
			{
				InOutFailedResult.ExportedEntryCells.AddUnique(Placement.Cell);
			}
		}
		SortResultPlacements(InOutFailedResult);
		return;
	}

}

namespace
{
	/** Omission is a count choice only; it must never reach candidate/witness consumers. */
	bool IsOmittedVerticalAccessHost(const FLayoutVerticalAccessHostGroup& Group, const int32 OptionIndex)
	{
		return Group.bAllowOmission && OptionIndex == Group.Options.Num();
	}

	/** Try the preferred target first, then omission before spending the budget on surplus yaws. */
	int32 VerticalAccessHostOptionAtRank(const FLayoutVerticalAccessHostGroup& Group, const int32 Rank)
	{
		if (!Group.bAllowOmission || Group.Options.IsEmpty()) return Rank;
		if (Group.bPreferOmission) return Rank == 0 ? Group.Options.Num() : Rank - 1;
		if (Rank == 0) return 0;
		return Rank == 1 ? Group.Options.Num() : Rank - 1;
	}

	/** Rejects only conflicts proved by the selected prefix and fixed occupancy, before request/domain copies. */
	bool ValidateVerticalAccessHostPrefix(
		const FLayoutModuleCatalog& Catalog,
		const TArray<FLayoutVerticalAccessHostGroup>& Groups,
		const TArray<int32>& Indices,
		const int32 PrefixCount,
		const TSet<FIntVector>& ForbiddenOccupiedCells,
		int32& OutConflictGroup,
		FString& OutFailureReason)
	{
		TSet<FIntVector> Occupied, FilledSupport, Clearance;
		TArray<FIntVector> Roots;
		for (int32 Index = 0; Index < PrefixCount; ++Index)
		{
			OutConflictGroup = Index;
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			if (Groups.IsValidIndex(Index) && Indices.IsValidIndex(Index)
				&& IsOmittedVerticalAccessHost(Groups[Index], Indices[Index])) continue;
			if (!Groups.IsValidIndex(Index) || !Indices.IsValidIndex(Index)
				|| !Groups[Index].Options.IsValidIndex(Indices[Index]))
			{
				OutFailureReason = TEXT("VerticalAccess prefix contains an invalid host option.");
				return false;
			}
			const auto& Option = Groups[Index].Options[Indices[Index]];
			const TArray<FIntVector> Cells = Option.bHasExactCandidateWitness
				? Option.OccupiedCells : TArray<FIntVector>{Option.LowerCell, Option.UpperCell};
			for (const auto& Cell : Cells)
			{
				if (Occupied.Contains(Cell) || Clearance.Contains(Cell) || ForbiddenOccupiedCells.Contains(Cell))
				{
					OutFailureReason = FString::Printf(TEXT("VerticalAccess prefix conflicts with fixed or selected occupancy at %s."), *Cell.ToString());
					return false;
				}
			}
			for (const auto& Cell : Option.RequiredEmptyClearanceCells)
			{
				if (Occupied.Contains(Cell) || FilledSupport.Contains(Cell))
				{
					OutFailureReason = TEXT("VerticalAccess prefix clearance conflicts with selected filled authority.");
					return false;
				}
			}
			for (const auto& Cell : Option.RequiredFilledSupportCells)
			{
				if (Clearance.Contains(Cell))
				{
					OutFailureReason = TEXT("VerticalAccess prefix support conflicts with selected empty clearance.");
					return false;
				}
			}
			for (const auto& Root : Roots)
			{
				if (Root.X == Option.LowerCell.X && Root.Y == Option.LowerCell.Y
					&& FMath::Abs(Root.Z - Option.LowerCell.Z) <= 1)
				{
					OutFailureReason = TEXT("VerticalAccess prefix contains overlapping or stacked roots.");
					return false;
				}
			}
			Roots.Add(Option.LowerCell);
			Occupied.Append(Cells);
			FilledSupport.Append(Option.RequiredFilledSupportCells);
			Clearance.Append(Option.RequiredEmptyClearanceCells);
		}
		// Prospective composite support is conditional on another counted host.
		// Reject only when neither the selected prefix nor any remaining option can
		// supply it. This avoids building full requests for impossible support tuples.
		for (int32 Index = 0; Index < PrefixCount; ++Index)
		{
			if (IsOmittedVerticalAccessHost(Groups[Index], Indices[Index])) continue;
			const auto& Selected = Groups[Index].Options[Indices[Index]];
			if (!Selected.bHasExactCandidateWitness) continue;
			for (const auto& SupportCell : Selected.RequiredFilledSupportCells)
			{
				if (Selected.OccupiedCells.Contains(SupportCell)) continue;
				bool bPossible = false;
				for (const auto& Support : Selected.FilledSupportCandidates)
				{
					if (Support.Cell != SupportCell) continue;
					if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::RankedNodes, OutFailureReason)) return false;
					const auto* Module = Catalog.Modules.FindByPredicate([&](const auto& M) { return M.SnapshotId == Support.ModuleSnapshotId; });
					if (Module == nullptr || (Support.RootCell == SupportCell && !Module->Roles.Contains(ELayoutModuleRole::VerticalAccess)))
					{
						bPossible = true; // Ordinary support or unresolved catalog authority stays with final proof.
						break;
					}
					for (int32 ProviderIndex = 0; ProviderIndex < Groups.Num() && !bPossible; ++ProviderIndex)
					{
						if (ProviderIndex == Index) continue;
						const int32 First = ProviderIndex < PrefixCount ? Indices[ProviderIndex] : 0;
						const int32 End = ProviderIndex < PrefixCount ? First + 1 : Groups[ProviderIndex].Options.Num();
						for (int32 OptionIndex = First; OptionIndex < End; ++OptionIndex)
						{
							if (!Groups[ProviderIndex].Options.IsValidIndex(OptionIndex)) continue;
							if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::RankedNodes, OutFailureReason)) return false;
							const auto& Provider = Groups[ProviderIndex].Options[OptionIndex];
							if (!Provider.bHasExactCandidateWitness) { bPossible = true; break; }
							if (Provider.LowerCell != Support.RootCell || Provider.LowerModuleSnapshotId != Support.ModuleSnapshotId
								|| Provider.LowerYawRotationSteps != Support.YawRotationSteps || !Provider.OccupiedCells.Contains(SupportCell)) continue;
							if (ProviderIndex >= PrefixCount && Provider.OccupiedCells.ContainsByPredicate([&](const auto& Cell)
								{ return Occupied.Contains(Cell) || Clearance.Contains(Cell) || ForbiddenOccupiedCells.Contains(Cell); })) continue;
							bPossible = true;
							break;
						}
					}
					if (bPossible) break;
				}
				if (!bPossible)
				{
					OutConflictGroup = PrefixCount - 1; // Later choices can invalidate an earlier host's remaining provider.
					OutFailureReason = FString::Printf(TEXT("VerticalAccess prefix has no remaining selected-host support for %s."), *SupportCell.ToString());
					return false;
				}
			}
		}
		OutConflictGroup = INDEX_NONE;
		return true;
	}

	/** Applies prefix-validated hosts; neighboring face compatibility still belongs to CSP. */
	bool BuildVerticalAccessHostAlternativeCells(
		const FLayoutRegionSolveRequest& Request,
		const TArray<int32>& OptionIndices,
		TArray<FLayoutPlannedCell>& OutPlannedCells,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		OutPlannedCells = !Request.PrecomputedPlannedCells.IsEmpty()
			? Request.PrecomputedPlannedCells
			: Request.PlannedCells;
		if (OptionIndices.Num() != Request.VerticalAccessHostGroups.Num())
		{
			OutFailureReason = TEXT("VerticalAccess host carrier index count does not match frozen host groups.");
			return false;
		}

		TSet<FIntVector> EntryCells;
		for (const FLayoutVerticalAccessHostGroup& Group : Request.VerticalAccessHostGroups)
		{
			if (Group.Options.IsEmpty() && !Group.bAllowOmission)
			{
				OutFailureReason = FString::Printf(TEXT("VerticalAccess host group '%s' has no admitted alternatives."), *Group.GroupId.ToString());
				return false;
			}
		}
		for (const FLayoutPlannedCell& PlannedCell : OutPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				EntryCells.Add(PlannedCell.Cell);
			}
		}
		for (FLayoutPlannedCell& PlannedCell : OutPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				PlannedCell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
					Request.FootprintSize,
					EntryCells,
					PlannedCell.Cell);
			}
		}

		TSet<FIntVector> SelectedHosts;
		for (int32 GroupIndex = 0; GroupIndex < Request.VerticalAccessHostGroups.Num(); ++GroupIndex)
		{
			// Both owning search paths validated this complete prefix before copying the request.
			if (IsOmittedVerticalAccessHost(Request.VerticalAccessHostGroups[GroupIndex], OptionIndices[GroupIndex])) continue;
			const FLayoutVerticalAccessHostOption& Option =
				Request.VerticalAccessHostGroups[GroupIndex].Options[OptionIndices[GroupIndex]];
			SelectedHosts.Add(Option.LowerCell);
			FLayoutPlannedCell* SelectedCell = OutPlannedCells.FindByPredicate([&Option](const FLayoutPlannedCell& PlannedCell)
			{
				return PlannedCell.Cell == Option.LowerCell;
			});
			if (SelectedCell == nullptr)
			{
				OutFailureReason = FString::Printf(TEXT("VerticalAccess host %s is absent from frozen planned cells."), *Option.LowerCell.ToString());
				return false;
			}
			SelectedCell->Intent = ELayoutCellIntent::VerticalAccess;
		}

		const auto EveryVerticalAccessRootCandidateRequiresFilled = [&Request](
			const ELayoutFaceDirection Direction)
		{
			bool bFoundCandidate = false;
			for (const FLayoutModuleSolveSnapshot& Module : Request.ModuleCatalog.Modules)
			{
				if (!Module.Roles.Contains(ELayoutModuleRole::VerticalAccess)) continue;
				for (const int32 Yaw : Module.AllowedYawRotationSteps)
				{
					const FLayoutModuleFaceRules WorldRules =
						BuildWorldFaceRulesForYaw(Module.EffectiveFaceRules, Yaw);
					const FLayoutFaceRule* FaceRule = WorldRules.FindRule(Direction);
					if (FaceRule == nullptr) continue;
					bFoundCandidate = true;
					if (IsFaceCompatibleWithOccupancy(*FaceRule, true, false)) return false;
				}
			}
			return bFoundCandidate;
		};
		for (const FIntVector& SelectedHost : SelectedHosts)
		{
			for (const ELayoutFaceDirection Direction : {
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY})
			{
				if (!EveryVerticalAccessRootCandidateRequiresFilled(Direction)) continue;
				const FIntVector SupportCell = SelectedHost + FLayoutDirectionUtils::ToCellDelta(Direction);
				FLayoutPlannedCell* PlannedSupport = OutPlannedCells.FindByPredicate(
					[&SupportCell](const FLayoutPlannedCell& PlannedCell)
					{
						return PlannedCell.Cell == SupportCell;
					});
				if (PlannedSupport == nullptr
					|| PlannedSupport->Intent == ELayoutCellIntent::Entry
					|| PlannedSupport->Intent == ELayoutCellIntent::VerticalAccess)
				{
					continue;
				}
				const bool bSparseSupport = Request.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate(
					[&Request, PlannedSupport](const FLayoutSparsePlacementRuleSolveSnapshot& Rule)
					{
						return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
							&& Rule.PlacementZone == PlannedSupport->PlacementZone
							&& DoesCellMatchAuthoredLevelScope(
								PlannedSupport->Cell,
								Request.ProfileSnapshot.LevelCount,
								Rule.LevelPlacementPolicy,
								Rule.SpecificLevel);
					});
				if (bSparseSupport)
				{
					PlannedSupport->Intent = ELayoutCellIntent::Connector;
				}
			}
		}
		return true;
	}

	bool ApplySelectedVerticalAccessCandidateWitnessesInPlace(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		const TArray<int32>& OptionIndices,
		FLayoutRegionSolveRequest& InOutRequest,
		FString& OutFailureReason)
	{
		if (HostGroups.Num() != OptionIndices.Num())
		{
			OutFailureReason = TEXT("VerticalAccess exact-witness selection does not match host groups.");
			return false;
		}
		if (InOutRequest.CandidateDomainCertificateId.IsNone())
		{
			InOutRequest.CandidateDomainCertificateId = FLayoutId(*FString::Printf(
				TEXT("VerticalAccessExact.%s.%d"),
				*InOutRequest.EffectiveSnapshotId.ToString(),
				InOutRequest.Seed));
		}
		for (FLayoutCellCandidateDomainRestriction& Restriction : InOutRequest.CandidateDomainRestrictions)
		{
			if (Restriction.BeforeVerticalAccessCandidates.IsSet())
			{
				Restriction.AllowedCandidates = MoveTemp(Restriction.BeforeVerticalAccessCandidates.GetValue());
				Restriction.RestrictionId = Restriction.BeforeVerticalAccessRestrictionId;
				Restriction.BeforeVerticalAccessRestrictionId = NAME_None;
				Restriction.BeforeVerticalAccessCandidates.Reset();
			}
		}
		for (const FLayoutVerticalAccessHostGroup& Group : HostGroups)
		{
			const FString ExactPrefix = FString::Printf(
				TEXT("%s.Exact."), *Group.GroupId.ToString());
			const FString SupportPrefix = FString::Printf(
				TEXT("%s.Support."), *Group.GroupId.ToString());
			InOutRequest.CandidateDomainRestrictions.RemoveAll(
				[&ExactPrefix, &SupportPrefix](const FLayoutCellCandidateDomainRestriction& Restriction)
				{
					const FString RestrictionId = Restriction.RestrictionId.ToString();
					return RestrictionId.StartsWith(ExactPrefix)
						|| RestrictionId.StartsWith(SupportPrefix);
				});
		}

		const auto ApplyRestriction = [&InOutRequest, &OutFailureReason](
			const FLayoutId GroupId,
			const FIntVector& Cell,
			const FLayoutId ModuleSnapshotId,
			const int32 YawRotationSteps)
		{
			FLayoutCellCandidateDomainRestriction* Restriction =
				InOutRequest.CandidateDomainRestrictions.FindByPredicate(
					[&Cell](const FLayoutCellCandidateDomainRestriction& Candidate)
					{
						return Candidate.Cell == Cell;
					});
			if (Restriction == nullptr)
			{
				Restriction = &InOutRequest.CandidateDomainRestrictions.AddDefaulted_GetRef();
				Restriction->Cell = Cell;
				Restriction->RestrictionId = FLayoutId(*FString::Printf(
					TEXT("%s.Exact.%d.%d.%d.%s.Yaw%d"),
					*GroupId.ToString(),
					Cell.X,
					Cell.Y,
					Cell.Z,
					*ModuleSnapshotId.ToString(),
					YawRotationSteps));
			}
			FLayoutCandidateVariantIdentity ExactCandidate;
			ExactCandidate.ModuleSnapshotId = ModuleSnapshotId;
			ExactCandidate.YawRotationSteps = YawRotationSteps;
			if (Restriction->RestrictionId.ToString().StartsWith(
				FString::Printf(TEXT("%s.Exact."), *GroupId.ToString())))
			{
				Restriction->AllowedCandidates = {ExactCandidate};
				return true;
			}
			if (!Restriction->BeforeVerticalAccessCandidates.IsSet())
			{
				Restriction->BeforeVerticalAccessCandidates = Restriction->AllowedCandidates;
				Restriction->BeforeVerticalAccessRestrictionId = Restriction->RestrictionId;
			}
			Restriction->AllowedCandidates.RemoveAll(
				[&ExactCandidate](const FLayoutCandidateVariantIdentity& Candidate)
				{
					return Candidate.ModuleSnapshotId != ExactCandidate.ModuleSnapshotId
						|| Candidate.YawRotationSteps != ExactCandidate.YawRotationSteps;
				});
			if (!Restriction->AllowedCandidates.IsEmpty())
			{
				return true;
			}
			OutFailureReason = FString::Printf(
				TEXT("VerticalAccess exact candidate conflicts with existing restriction at %s."),
				*Cell.ToString());
			return false;
		};

		for (int32 GroupIndex = 0; GroupIndex < HostGroups.Num(); ++GroupIndex)
		{
			const FLayoutVerticalAccessHostGroup& Group = HostGroups[GroupIndex];
			if (IsOmittedVerticalAccessHost(Group, OptionIndices[GroupIndex])) continue;
			if (!Group.Options.IsValidIndex(OptionIndices[GroupIndex]))
			{
				OutFailureReason = FString::Printf(
					TEXT("VerticalAccess group '%s' lost selected exact witness."),
					*Group.GroupId.ToString());
				return false;
			}
			const FLayoutVerticalAccessHostOption& Option = Group.Options[OptionIndices[GroupIndex]];
			if (!Option.bHasExactCandidateWitness)
			{
				continue;
			}
			if (!ApplyRestriction(
					Group.GroupId,
					Option.LowerCell,
					Option.LowerModuleSnapshotId,
					Option.LowerYawRotationSteps))
			{
				return false;
			}
			if (Option.UpperCandidateLocalCell == FIntVector::ZeroValue
				&& !ApplyRestriction(
					Group.GroupId,
					Option.UpperCell,
					Option.UpperModuleSnapshotId,
					Option.UpperYawRotationSteps))
			{
				return false;
			}
			for (const FIntVector& SupportCell : Option.RequiredFilledSupportCells)
			{
				TArray<FLayoutCandidateVariantIdentity> SupportDomain;
				bool bSelectedHostSuppliesSupport = false;
				for (const FLayoutVerticalAccessSupportCandidate& Candidate :
					Option.FilledSupportCandidates)
				{
					if (Candidate.Cell != SupportCell)
					{
						continue;
					}
					const bool bCountedStair = InOutRequest.ModuleCatalog.Modules.IsValidIndex(Candidate.ModuleSnapshotIndex)
						&& InOutRequest.ModuleCatalog.Modules[Candidate.ModuleSnapshotIndex].Roles.Contains(ELayoutModuleRole::VerticalAccess);
					if (Candidate.RootCell != SupportCell || bCountedStair)
					{
						// Support may consume another selected provider, never create an
						// uncounted stair or apply a composite-root domain to its shadow cell.
						for (int32 OtherIndex = 0; OtherIndex < HostGroups.Num(); ++OtherIndex)
						{
							if (!HostGroups[OtherIndex].Options.IsValidIndex(OptionIndices[OtherIndex])) continue;
							const FLayoutVerticalAccessHostOption& Other = HostGroups[OtherIndex].Options[OptionIndices[OtherIndex]];
							if (Other.bHasExactCandidateWitness && Other.LowerCell == Candidate.RootCell
								&& Other.LowerModuleSnapshotId == Candidate.ModuleSnapshotId
								&& Other.LowerYawRotationSteps == Candidate.YawRotationSteps
								&& Other.OccupiedCells.Contains(SupportCell))
							{
								bSelectedHostSuppliesSupport = true;
								break;
							}
						}
						continue;
					}
					FLayoutCandidateVariantIdentity& Identity =
						SupportDomain.AddDefaulted_GetRef();
					Identity.ModuleSnapshotId = Candidate.ModuleSnapshotId;
					Identity.YawRotationSteps = Candidate.YawRotationSteps;
				}
				if (bSelectedHostSuppliesSupport) continue;
				if (SupportDomain.IsEmpty())
				{
					OutFailureReason = FString::Printf(
						TEXT("VerticalAccess required support has no exact module domain at %s."), *SupportCell.ToString());
					return false;
				}
				FLayoutCellCandidateDomainRestriction* Restriction =
					InOutRequest.CandidateDomainRestrictions.FindByPredicate(
						[&SupportCell](const FLayoutCellCandidateDomainRestriction& Existing)
						{
							return Existing.Cell == SupportCell;
						});
				if (Restriction == nullptr)
				{
					const FString SupportDomainIdentity = FString::JoinBy(
						SupportDomain,
						TEXT("|"),
						[](const FLayoutCandidateVariantIdentity& Candidate)
						{
							return FString::Printf(
								TEXT("%s@%d"),
								*Candidate.ModuleSnapshotId.ToString(),
								Candidate.YawRotationSteps);
						});
					Restriction = &InOutRequest.CandidateDomainRestrictions.AddDefaulted_GetRef();
					Restriction->Cell = SupportCell;
					Restriction->RestrictionId = FLayoutId(*FString::Printf(
						TEXT("%s.Support.%d.%d.%d.%08X"),
						*Group.GroupId.ToString(),
						SupportCell.X,
						SupportCell.Y,
						SupportCell.Z,
						FCrc::StrCrc32(*SupportDomainIdentity)));
					Restriction->AllowedCandidates = MoveTemp(SupportDomain);
					continue;
				}
				if (!Restriction->BeforeVerticalAccessCandidates.IsSet())
				{
					Restriction->BeforeVerticalAccessCandidates = Restriction->AllowedCandidates;
					Restriction->BeforeVerticalAccessRestrictionId = Restriction->RestrictionId;
				}
				Restriction->AllowedCandidates.RemoveAll(
					[&SupportDomain](const FLayoutCandidateVariantIdentity& Existing)
					{
						return !SupportDomain.ContainsByPredicate(
							[&Existing](const FLayoutCandidateVariantIdentity& Candidate)
							{
								return Candidate.ModuleSnapshotId == Existing.ModuleSnapshotId
									&& Candidate.YawRotationSteps == Existing.YawRotationSteps;
							});
					});
				if (Restriction->AllowedCandidates.IsEmpty())
				{
					OutFailureReason = FString::Printf(
						TEXT("VerticalAccess support domain conflicts with existing restriction at %s."),
						*SupportCell.ToString());
					return false;
				}
			}
		}
		return true;
	}

	/** Failed witness selection leaves original child domains and prior assignment untouched. */
	bool ApplySelectedVerticalAccessCandidateWitnesses(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		const TArray<int32>& OptionIndices,
		FLayoutRegionSolveRequest& Request,
		FString& FailureReason)
	{
		FLayoutRegionSolveRequest Trial = Request;
		if (!ApplySelectedVerticalAccessCandidateWitnessesInPlace(HostGroups, OptionIndices, Trial, FailureReason))
		{
			return false;
		}
		for (FLayoutCellCandidateDomainRestriction& Restriction : Trial.CandidateDomainRestrictions)
		{
			if (!Restriction.BeforeVerticalAccessCandidates.IsSet()) continue;
			TArray<FString> CandidateIds;
			for (const FLayoutCandidateVariantIdentity& Candidate : Restriction.AllowedCandidates)
			{
				CandidateIds.Add(FString::Printf(TEXT("%s@%d"), *Candidate.ModuleSnapshotId.ToString(), Candidate.YawRotationSteps));
			}
			CandidateIds.Sort();
			Restriction.RestrictionId = FLayoutId(*FString::Printf(TEXT("%s.VA.%08X"),
				*Restriction.BeforeVerticalAccessRestrictionId.ToString(), FCrc::StrCrc32(*FString::Join(CandidateIds, TEXT("|")))));
		}
		Request = MoveTemp(Trial);
		return true;
	}

	bool AdvanceVerticalAccessHostAlternativeIndices(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		TArray<int32>& InOutOptionIndices)
	{
		for (int32 GroupIndex = InOutOptionIndices.Num() - 1; GroupIndex >= 0; --GroupIndex)
		{
			const FLayoutVerticalAccessHostGroup& Group = HostGroups[GroupIndex];
			const int32 OptionIndex = InOutOptionIndices[GroupIndex];
			const int32 Rank = !Group.bAllowOmission || Group.Options.IsEmpty()
				? OptionIndex
				: (Group.bPreferOmission
					? (IsOmittedVerticalAccessHost(Group, OptionIndex) ? 0 : OptionIndex + 1)
					: (OptionIndex == 0 ? 0 : (IsOmittedVerticalAccessHost(Group, OptionIndex) ? 1 : OptionIndex + 1)));
			if (Rank + 1 < Group.Options.Num() + static_cast<int32>(Group.bAllowOmission))
			{
				InOutOptionIndices[GroupIndex] = VerticalAccessHostOptionAtRank(Group, Rank + 1);
				return true;
			}
			InOutOptionIndices[GroupIndex] = VerticalAccessHostOptionAtRank(Group, 0);
		}
		return false;
	}

	FString DescribeVerticalAccessHostSelection(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		const TArray<int32>& OptionIndices)
	{
		TArray<FString> Details;
		for (int32 GroupIndex = 0; GroupIndex < HostGroups.Num(); ++GroupIndex)
		{
			const FLayoutVerticalAccessHostGroup& Group = HostGroups[GroupIndex];
			if (IsOmittedVerticalAccessHost(Group, OptionIndices[GroupIndex]))
			{
				Details.Add(FString::Printf(TEXT("group=%s omitted=RangeSurplus"), *Group.GroupId.ToString()));
				continue;
			}
			if (!Group.Options.IsValidIndex(OptionIndices[GroupIndex]))
			{
				continue;
			}
			const FLayoutVerticalAccessHostOption& Option = Group.Options[OptionIndices[GroupIndex]];
			Details.Add(FString::Printf(
				TEXT("group=%s deck=%s selectedHost=%s tier=%s firstRejectedAlternative=%s"),
				*Group.GroupId.ToString(),
				*Group.DeckCell.ToString(),
				*Option.LowerCell.ToString(),
				*StaticEnum<ELayoutVerticalAccessHostTier>()->GetNameStringByValue(static_cast<int64>(Option.Tier)),
				*Group.FirstRejectedAdmission));
		}
		return FString::Join(Details, TEXT(" | "));
	}

	FLayoutSolveResult SolveUnifiedVerticalRegionsWithHostAlternatives(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutSolveResult& BaseResult,
		FLayoutRegionSolveRequest& OutLastAttemptRequest)
	{
		LayoutSolveExecution::FScope ExecutionScope(Request.ExecutionSettings.MaxSolveDurationSeconds, Request.ExecutionSettings.MaxCandidateAttempts);
		const double SharedStartTimeSeconds = FPlatformTime::Seconds();
		int32 ConsumedCandidateAttempts = 0;
		int32 MaximumRankSum = 0;
		for (const FLayoutVerticalAccessHostGroup& Group : Request.VerticalAccessHostGroups)
		{
			MaximumRankSum += FMath::Max(0, Group.Options.Num() - 1 + static_cast<int32>(Group.bAllowOmission));
		}
		FLayoutSolveResult LastResult = BaseResult;
		LastResult.bSucceeded = false;
		// Host tuples only change spatial authority; their catalog is invocation-immutable.
		TArray<FSolveContext::FOrientedModuleVariant> SparseVariants;
		TMap<ELayoutCellIntent, TArray<int32>> SparseVariantIndices;
		TSet<FIntVector> FixedEntryCells;
		const auto& AuthorityCells = Request.PrecomputedPlannedCells.IsEmpty() ? Request.PlannedCells : Request.PrecomputedPlannedCells;
		for (const auto& Cell : AuthorityCells)
		{
			if (Cell.Intent == ELayoutCellIntent::Entry) FixedEntryCells.Add(Cell.Cell);
		}
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[VerticalAccessHostSolveGroups] region=%s groups=%s"),
				*Request.RegionDebugPath,
				*FString::JoinBy(Request.VerticalAccessHostGroups, TEXT("|"), [](const FLayoutVerticalAccessHostGroup& Group)
				{
					return FString::Printf(
						TEXT("%s:%s"),
						*Group.GroupId.ToString(),
						Group.Options.IsEmpty() ? TEXT("<none>") : *Group.Options[0].LowerCell.ToString());
				}));
		}
		const int32 PerAssignmentCandidateAttemptLimit =
			Request.ExecutionSettings.MaxCandidateAttempts;
		FString FirstSelection;
		FString FirstFailureReason;
		FString LastSelection;
		FString LastFailureReason;
		bool bCanceled = false;
		bool bMinimumCountOnly = false;
		const bool bHasPreferredSurplus = Request.VerticalAccessHostGroups.ContainsByPredicate([](const auto& Group)
		{
			return Group.bAllowOmission && !Group.bIsSupplemental && !Group.bPreferOmission;
		});
		// Reserve once for the entire preferred-count search. Reborrowing half of
		// the remainder per tuple can spend almost everything without trying the minimum.
		TOptional<LayoutSolveExecution::FOptionalImprovementScope> PreferredCountAllowance;
		if (bHasPreferredSurplus) PreferredCountAllowance.Emplace();

		const auto TryAssignment = [&](const TArray<int32>& OptionIndices)
		{
			if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::HostAssignments, LastResult.FailureReason)) return true;
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty()
				&& FirstSelection.IsEmpty())
			{
				UE_LOG(LogTemp, Display,
					TEXT("[VerticalAccessHostFirstIteration] region=%s selection=%s"),
					*Request.RegionDebugPath,
					*DescribeVerticalAccessHostSelection(Request.VerticalAccessHostGroups, OptionIndices));
			}
			if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested())
			{
				LastResult.bSucceeded = false;
				LastResult.FailureReason = TEXT("VerticalAccess host alternative solving was canceled.");
				bCanceled = true;
				return true;
			}
			const double ElapsedSeconds = FPlatformTime::Seconds() - SharedStartTimeSeconds;
			if (Request.ExecutionSettings.MaxSolveDurationSeconds > 0.0f
				&& ElapsedSeconds >= Request.ExecutionSettings.MaxSolveDurationSeconds)
			{
				return true;
			}
			if (ConsumedCandidateAttempts >= Request.ExecutionSettings.MaxCandidateAttempts)
			{
				return true;
			}

			// Requests already preferring the minimum retain their existing optional
			// improvement guard; a preferred-surplus pass has one enclosing allowance.
			TOptional<LayoutSolveExecution::FOptionalImprovementScope> SurplusAllowance;
			for (int32 Index = 0; !bHasPreferredSurplus && Index < Request.VerticalAccessHostGroups.Num(); ++Index)
			{
				const FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups[Index];
				if (Group.bAllowOmission && !Group.bIsSupplemental && !IsOmittedVerticalAccessHost(Group, OptionIndices[Index]))
				{
					SurplusAllowance.Emplace();
					break;
				}
			}
			FLayoutRegionSolveRequest AttemptRequest = Request;
			FString SelectionFailureReason;
			if (!BuildVerticalAccessHostAlternativeCells(
					AttemptRequest,
					OptionIndices,
					AttemptRequest.PrecomputedPlannedCells,
					SelectionFailureReason))
			{
				LastResult.bSucceeded = false;
				LastResult.FailureReason = SelectionFailureReason;
				LastSelection = DescribeVerticalAccessHostSelection(
					Request.VerticalAccessHostGroups,
					OptionIndices);
				LastFailureReason = SelectionFailureReason;
				if (FirstFailureReason.IsEmpty())
				{
					FirstSelection = LastSelection;
					FirstFailureReason = SelectionFailureReason;
				}
				OutLastAttemptRequest = MoveTemp(AttemptRequest);
				return false;
			}
			AttemptRequest.PlannedCells = AttemptRequest.PrecomputedPlannedCells;
			// Route-domain terrain-seam gates may only exclude this attempt's chosen
			// VerticalAccess hosts. Reserving every alternative makes a valid seam
			// cell appear permanently blocked and forces pointless full-plan retries.
			TArray<FLayoutVerticalAccessHostGroup> SelectedHostGroups;
			SelectedHostGroups.Reserve(Request.VerticalAccessHostGroups.Num());
			for (int32 GroupIndex = 0; GroupIndex < Request.VerticalAccessHostGroups.Num(); ++GroupIndex)
			{
				const FLayoutVerticalAccessHostGroup& SourceGroup = Request.VerticalAccessHostGroups[GroupIndex];
				if (IsOmittedVerticalAccessHost(SourceGroup, OptionIndices[GroupIndex])) continue;
				SelectedHostGroups.Add(SourceGroup);
				SelectedHostGroups.Last().Options = { SourceGroup.Options[OptionIndices[GroupIndex]] };
				SelectedHostGroups.Last().bAllowOmission = false;
				SelectedHostGroups.Last().bPreferOmission = false;
			}
			AttemptRequest.VerticalAccessHostGroups = MoveTemp(SelectedHostGroups);
			if (!ApplySelectedVerticalAccessCandidateWitnesses(
					Request.VerticalAccessHostGroups,
					OptionIndices,
					AttemptRequest,
					SelectionFailureReason))
			{
				LastResult.bSucceeded = false;
				LastResult.FailureReason = SelectionFailureReason;
				LastSelection = DescribeVerticalAccessHostSelection(
					Request.VerticalAccessHostGroups, OptionIndices);
				LastFailureReason = SelectionFailureReason;
				if (FirstFailureReason.IsEmpty())
				{
					FirstSelection = LastSelection;
					FirstFailureReason = SelectionFailureReason;
				}
				OutLastAttemptRequest = MoveTemp(AttemptRequest);
				return false;
			}
			// Keep one fixed global budget, but let fail-first host ordering spend
			// remaining attempts deeply enough to finish a viable assignment.
			AttemptRequest.ExecutionSettings.MaxCandidateAttempts = FMath::Min(
				PerAssignmentCandidateAttemptLimit,
				FMath::Max(
					0,
					Request.ExecutionSettings.MaxCandidateAttempts - ConsumedCandidateAttempts));
			if (Request.ExecutionSettings.MaxSolveDurationSeconds > 0.0f)
			{
				AttemptRequest.ExecutionSettings.MaxSolveDurationSeconds = FMath::Max(
					0.0,
					static_cast<double>(Request.ExecutionSettings.MaxSolveDurationSeconds) - ElapsedSeconds);
			}

			const bool bSparseProjection = !AttemptRequest.bDeferTraversalValidationToSchedule
				&& AttemptRequest.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate([](const auto& Rule)
				{
					return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
						|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
				});
			if (bSparseProjection)
			{
				FLayoutRegionSolveResult Initial;
				InitializeRequestBackedRegionResultFromRequest(AttemptRequest, Initial);
				FSolveContext Owner = BuildLiveSolveContextFromRequest(AttemptRequest, Initial.SolveResult);
				if (Owner.Result.FailureReason.IsEmpty())
				{
					if (SparseVariants.IsEmpty())
					{
						BuildOrientedVariants(Owner);
						if (Owner.Result.FailureReason.IsEmpty())
						{
							SparseVariants = Owner.Variants;
							SparseVariantIndices = Owner.VariantIndicesByIntent;
						}
					}
					else
					{
						Owner.Variants = SparseVariants;
						Owner.VariantIndicesByIntent = SparseVariantIndices;
					}
				}
				FSolveContext Solved;
				FLayoutRegionSolveRequest Authority;
				const bool bSolved = TrySolveSparseStructuralProjection(AttemptRequest, Owner, Solved, Authority, true);
				if (bSolved)
				{
					AppendContextPlacementsToResult(Solved, Solved.Result);
					SortResultPlacements(Solved.Result);
					AttemptRequest = MoveTemp(Authority);
				}
				Solved.Result.bSucceeded = bSolved;
				Solved.Result.PropagationStats.CandidateAttemptCount = Solved.CandidateAttemptCount;
				LastResult = MoveTemp(Solved.Result);
			}
			else
			{
			FSolveContext RoutePreflightContext;
			const bool bRoutePreflightSucceeded =
				LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughRouteDomainStage(
					AttemptRequest,
					RoutePreflightContext,
					AttemptRequest.bDeferTraversalValidationToSchedule);
			LayoutProfileSolverInternal::FPreparedSearchPrefixStageCarrier PrefixStage;
			const bool bPrefixPreflightSucceeded = bRoutePreflightSucceeded
				&& LayoutProfileSolverInternal::TryPrepareSolveContextThroughSearchPrefixStage(
					RoutePreflightContext,
					PrefixStage);
			bool bBranchArcPreflightSucceeded = bPrefixPreflightSucceeded;
			FString BranchArcPreflightFailure;
			if (bPrefixPreflightSucceeded
				&& !PrefixStage.bSolvedAfterForcedPlacements)
			{
				LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier BranchStage;
				LayoutProfileSolverInternal::BuildPreparedSearchBranchStageFromSearchPrefixStage(
					PrefixStage,
					BranchStage);
				bBranchArcPreflightSucceeded = false;
				for (int32 WorkItemIndex = 0;
					WorkItemIndex < BranchStage.WorkItems.Num();
					++WorkItemIndex)
				{
					LayoutProfileSolverInternal::FSolveContext BranchContext =
						RoutePreflightContext;
					LayoutProfileSolverInternal::FPreparedSearchBranchApplyStageCarrier
						BranchApplyStage;
					if (LayoutProfileSolverInternal::
						TryPrepareSolveContextThroughSearchBranchApplyStage(
							BranchContext,
							BranchStage,
							WorkItemIndex,
							BranchApplyStage))
					{
						bBranchArcPreflightSucceeded = true;
						break;
					}
					if (BranchArcPreflightFailure.IsEmpty())
					{
						BranchArcPreflightFailure = BranchContext.Result.FailureReason;
					}
				}
			}
			if (!bPrefixPreflightSucceeded || !bBranchArcPreflightSucceeded)
			{
				LastResult = MoveTemp(RoutePreflightContext.Result);
				LastResult.bSucceeded = false;
				if (!bBranchArcPreflightSucceeded
					&& !BranchArcPreflightFailure.IsEmpty())
				{
					LastResult.FailureReason = BranchArcPreflightFailure;
				}
				else if (LastResult.FailureReason.IsEmpty())
				{
					LastResult.FailureReason = FString::Printf(
						TEXT("VerticalAccess host deterministic-prefix preflight rejected selection at %s: failure=%s count=%d domain=%d."),
						*PrefixStage.Summary.SelectedCell.ToString(),
						*PrefixStage.Summary.DominantFailureKind.ToString(),
						PrefixStage.Summary.DominantFailureCount,
						PrefixStage.Summary.SelectedDomainSize);
				}
			}
			else
			{
				const bool bSolvedFromPreparedPrefix =
					LayoutProfileSolverInternal::ContinuePreparedSolveContextAfterSearchPrefixStage(
						RoutePreflightContext,
						PrefixStage,
						!AttemptRequest.bDeferClosureValidationToSchedule);
				if (bSolvedFromPreparedPrefix)
				{
					RoutePreflightContext.Result.ExportedEntryCells.Reset();
					AppendContextPlacementsToResult(
						RoutePreflightContext,
						RoutePreflightContext.Result);
				}
				else
				{
					RestoreBestPartialPlacements(RoutePreflightContext);
				}
				SortResultPlacements(RoutePreflightContext.Result);
				RoutePreflightContext.Result.PropagationStats.CandidateAttemptCount =
					RoutePreflightContext.CandidateAttemptCount;
				LastResult = MoveTemp(RoutePreflightContext.Result);
			}
			}
			if (LastResult.bSucceeded && !LayoutSolveExecution::Checkpoint(LastResult.FailureReason))
			{
				LastResult.bSucceeded = false;
			}
			ConsumedCandidateAttempts += LastResult.PropagationStats.CandidateAttemptCount;
			LastSelection = DescribeVerticalAccessHostSelection(
				Request.VerticalAccessHostGroups,
				OptionIndices);
			if (!LastResult.bSucceeded)
			{
				LastFailureReason = LastResult.FailureReason;
				if (FirstFailureReason.IsEmpty())
				{
					FirstSelection = LastSelection;
					FirstFailureReason = LastResult.FailureReason;
				}
			}
			OutLastAttemptRequest = MoveTemp(AttemptRequest);
			if (LastResult.bSucceeded && LastResult.Placements.IsEmpty()
				&& !LastResult.PlannedCells.IsEmpty())
			{
				LastResult.bSucceeded = false;
				LastResult.FailureReason = FString::Printf(
					TEXT("VerticalAccess host solve returned success without finalized placement roots. region=%s profile=%s planned=%d residuals=%d sparseRules=%d."),
					*Request.RegionDebugPath,
					*Request.ProfileSnapshot.SnapshotId.ToString(),
					LastResult.PlannedCells.Num(),
					LastResult.ResidualUnoccupiedCells.Num(),
					Request.ProfileSnapshot.SparsePlacementRules.Num());
			}
			if (LastResult.bSucceeded)
			{
				LastResult.PropagationStats.CandidateAttemptCount = ConsumedCandidateAttempts;
				return true;
			}
			// A failure cell is not a dependency proof: support/yaw overlays affect remote domains.
			return false;
		};

		TArray<int32> PendingAssignment;
		PendingAssignment.SetNumZeroed(Request.VerticalAccessHostGroups.Num());
		TFunction<bool(int32, int32)> VisitRank = [&](const int32 GroupIndex, const int32 RemainingRank)
		{
			if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::RankedNodes, LastResult.FailureReason)) return true;
			if (GroupIndex == Request.VerticalAccessHostGroups.Num())
			{
				return RemainingRank == 0 && TryAssignment(PendingAssignment);
			}
			const FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups[GroupIndex];
			const bool bForceOmission = bMinimumCountOnly && Group.bAllowOmission && !Group.bIsSupplemental;
			const int32 LastOptionRank = bForceOmission ? 0
				: FMath::Min(RemainingRank, Group.Options.Num() - 1 + static_cast<int32>(Group.bAllowOmission));
			for (int32 OptionRank = 0; OptionRank <= LastOptionRank; ++OptionRank)
			{
				if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::RankedNodes, LastResult.FailureReason)) return true;
				PendingAssignment[GroupIndex] = bForceOmission ? Group.Options.Num()
					: VerticalAccessHostOptionAtRank(Group, OptionRank);
				int32 ConflictGroup = INDEX_NONE;
				if (!ValidateVerticalAccessHostPrefix(Request.ModuleCatalog, Request.VerticalAccessHostGroups, PendingAssignment,
					GroupIndex + 1, FixedEntryCells, ConflictGroup, LastFailureReason))
				{
					if (LayoutSolveExecution::ShouldStop()) return true;
					continue;
				}
				if (VisitRank(GroupIndex + 1, RemainingRank - OptionRank)) return true;
			}
			return false;
		};
		for (int32 Rank = 0; Rank <= MaximumRankSum; ++Rank)
		{
			if (VisitRank(0, Rank)) break;
		}
		PreferredCountAllowance.Reset();
		if (!LastResult.bSucceeded && bHasPreferredSurplus && !bCanceled
			&& LayoutSolveExecution::Checkpoint(LastResult.FailureReason))
		{
			bMinimumCountOnly = true;
			int32 MinimumRankSum = 0;
			for (const auto& Group : Request.VerticalAccessHostGroups)
				if (!Group.bAllowOmission || Group.bIsSupplemental)
					MinimumRankSum += FMath::Max(0, Group.Options.Num() - 1 + static_cast<int32>(Group.bAllowOmission));
			for (int32 Rank = 0; Rank <= MinimumRankSum; ++Rank)
			{
				if (VisitRank(0, Rank)) break;
			}
		}
		if (LastResult.bSucceeded && LayoutSolveExecution::Checkpoint(LastResult.FailureReason)) return LastResult;

		LastResult.bSucceeded = false;
		LastResult.PropagationStats.CandidateAttemptCount = ConsumedCandidateAttempts;
		if (!LayoutSolveExecution::Checkpoint(LastResult.FailureReason))
		{
			if (!FirstFailureReason.IsEmpty()) LastResult.FailureReason += FString::Printf(
				TEXT(" First host proof rejection: %s"), *FirstFailureReason.Left(2048));
			return LastResult;
		}
		if (bCanceled)
		{
			return LastResult;
		}
		const bool bHasDistinctLastFailure =
			!LastFailureReason.IsEmpty()
			&& (LastSelection != FirstSelection || LastFailureReason != FirstFailureReason);
		LastResult.FailureReason = bHasDistinctLastFailure
			? FString::Printf(
				TEXT("VerticalAccess host alternatives exhausted under the shared solve budget after %d candidate attempts. First selection: %s. First failure: %s Last selection: %s. Last failure: %s"),
				ConsumedCandidateAttempts,
				*FirstSelection,
				*FirstFailureReason,
				*LastSelection,
				*LastFailureReason)
			: FString::Printf(
				TEXT("VerticalAccess host alternatives exhausted under the shared solve budget after %d candidate attempts. Failure: %s. Last selection: %s"),
				ConsumedCandidateAttempts,
				*FirstFailureReason,
				*LastSelection);
		return LastResult;
	}
}

FLayoutSolveResult LayoutProfileSolverInternal::SolveVerticalAccessHostAlternativesForTests(
	const FLayoutRegionSolveRequest& Request,
	FLayoutRegionSolveRequest& OutLastAttemptRequest)
{
	return SolveUnifiedVerticalRegionsWithHostAlternatives(Request, FLayoutSolveResult(), OutLastAttemptRequest);
}

bool LayoutProfileSolverInternal::ApplySelectedVerticalAccessCandidateWitnessesForTests(
	const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
	const TArray<int32>& OptionIndices,
	FLayoutRegionSolveRequest& Request,
	FString& FailureReason)
{
	return ApplySelectedVerticalAccessCandidateWitnesses(HostGroups, OptionIndices, Request, FailureReason);
}

bool LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
	const FLayoutRegionSolveRequest& Request,
	const bool bAllowExteriorEntryRelocation,
	const TSet<FIntVector>& ForbiddenEntryCells,
	const TSet<FIntVector>& ForbiddenVerticalAccessCells,
	FLayoutRegionSolveRequest& OutRequest,
	FString& OutFailureReason,
	FLayoutRegionSolveResult* OutFinalProof)
{
	if (OutFinalProof != nullptr) *OutFinalProof = FLayoutRegionSolveResult{};
	LayoutSolveExecution::FScope ExecutionScope(Request.ExecutionSettings.MaxSolveDurationSeconds, Request.ExecutionSettings.MaxCandidateAttempts);
	OutRequest = Request;
	OutFailureReason.Reset();
	if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
	TArray<int32> HostOptionIndices;
	for (const FLayoutVerticalAccessHostGroup& Group : Request.VerticalAccessHostGroups)
	{
		HostOptionIndices.Add(VerticalAccessHostOptionAtRank(Group, 0));
	}
	SCOPED_NAMED_EVENT(Layout_Authority_Freeze, FColor::Orange);
	const double TraceStartSeconds = FPlatformTime::Seconds();
	uint64 TraceHostTuples = 0;
	const int32 AttemptLimit = FMath::Clamp(
		Request.ExecutionSettings.MaxCandidateAttempts,
		1,
		256);
	int32 AttemptCount = 0;
	FString FirstFailure;
	FString LastFailure;
	// A stair's root yaw and upper port do not identify the normal landing yaw.
	// Preserve that choice across refresh/reordering; otherwise distinct retries
	// collapse to the first landing, and replacing it can erase a legal alternative.
	const bool bHasExactSparseLanding = Request.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate([](const auto& Rule)
	{
		return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
			|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
	});
	const auto MatchesHostSelection = [bHasExactSparseLanding](const FLayoutVerticalAccessHostOption& A, const FLayoutVerticalAccessHostOption& B)
	{
		if (A.LowerCell != B.LowerCell || A.UpperCell != B.UpperCell
			|| A.LowerModuleSnapshotId != B.LowerModuleSnapshotId || A.LowerYawRotationSteps != B.LowerYawRotationSteps
			|| A.UpperModuleSnapshotId != B.UpperModuleSnapshotId || A.UpperYawRotationSteps != B.UpperYawRotationSteps
			|| A.UpperCandidateLocalCell != B.UpperCandidateLocalCell
			|| A.UpperTraversalPortFaceMask != B.UpperTraversalPortFaceMask) return false;
		// Ordinary host support remains a refreshable domain, not a selected landing yaw.
		if (!bHasExactSparseLanding) return true;
		FIntVector ADestination, BDestination;
		const bool bAHasDestination = TryGetSparseHostDestination(A, ADestination);
		const bool bBHasDestination = TryGetSparseHostDestination(B, BDestination);
		if (bAHasDestination != bBHasDestination) return false;
		if (!bAHasDestination) return true;
		if (ADestination != BDestination) return false;
		const auto ContainsContact = [&ADestination](const auto& Candidates, const auto& Contact)
		{
			return Candidates.ContainsByPredicate([&](const auto& Candidate)
			{
				return Candidate.Cell == ADestination && Candidate.RootCell == Contact.RootCell
					&& Candidate.ModuleSnapshotId == Contact.ModuleSnapshotId
					&& Candidate.YawRotationSteps == Contact.YawRotationSteps;
			});
		};
		for (const auto& Contact : A.FilledSupportCandidates)
			if (Contact.Cell == ADestination && !ContainsContact(B.FilledSupportCandidates, Contact)) return false;
		for (const auto& Contact : B.FilledSupportCandidates)
			if (Contact.Cell == BDestination && !ContainsContact(A.FilledSupportCandidates, Contact)) return false;
		return true;
	};
	// Rectangle corners cannot admit an Edge-only gate. Reject that impossible
	// relocation before enumerating stair/Entry tuples; other admission stays with proof.
	const bool bHasCornerEntryModule = Request.ModuleCatalog.Modules.ContainsByPredicate([](const auto& Module)
	{
		return Module.SupportsIntent(ELayoutCellIntent::Entry)
			&& (Module.PlacementZone == ELayoutPlacementZone::Any
				|| Module.PlacementZone == ELayoutPlacementZone::Corner
				|| Module.PlacementZone == ELayoutPlacementZone::Perimeter);
	});
	// One immutable catalog per freeze invocation; no prepared occupancy or proof crosses attempts.
	FSolveContext InvocationVariants;
	TSet<FIntVector> FixedOccupiedCells = ForbiddenVerticalAccessCells;
	const auto& AuthorityCells = Request.PrecomputedPlannedCells.IsEmpty() ? Request.PlannedCells : Request.PrecomputedPlannedCells;
	for (const auto& Cell : AuthorityCells)
	{
		const bool bExterior = Cell.Cell.X == 0 || Cell.Cell.Y == 0
			|| Cell.Cell.X == Request.FootprintSize.X - 1 || Cell.Cell.Y == Request.FootprintSize.Y - 1;
		const bool bRelocatable = bAllowExteriorEntryRelocation
			&& (Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary
				|| (Cell.EntryOrigin == ELayoutEntryOrigin::None && bExterior));
		if (Cell.Intent == ELayoutCellIntent::Entry && !bRelocatable) FixedOccupiedCells.Add(Cell.Cell);
	}
	ON_SCOPE_EXIT
	{
		// A terminal budget check must not erase the first rejected authority assignment.
		if (LayoutSolveExecution::ShouldStop() && !FirstFailure.IsEmpty()
			&& !OutFailureReason.Contains(FirstFailure.Left(2048)))
		{
			OutFailureReason += FString::Printf(TEXT(" First authority rejection: %s"), *FirstFailure.Left(2048));
		}
		TRACE_BOOKMARK(TEXT("Layout_AuthorityEnd region=%s hostTuples=%llu entryAttempts=%d limit=%d stopped=%d failure=%s"),
			*Request.RegionDebugPath.Left(128), static_cast<unsigned long long>(TraceHostTuples), AttemptCount, AttemptLimit,
			LayoutSolveExecution::ShouldStop(), *OutFailureReason.Left(256));
	};

	do
	{
		// Charge pre-entry rejection separately from Entry/CSP attempts; sample progress without
		// formatting the complete host product or a potentially oversized rejection payload.
		if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
		if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::HostAssignments, OutFailureReason)) return false;
		++TraceHostTuples;
		if (SHOULD_TRACE_BOOKMARK() && (TraceHostTuples <= 4 || (TraceHostTuples & (TraceHostTuples - 1)) == 0))
		{
			TRACE_BOOKMARK(TEXT("Layout_AuthorityHostTuple region=%s tuple=%llu entryAttempts=%d limit=%d elapsedMs=%.3f groups=%d previousFailure=%s"),
				*Request.RegionDebugPath.Left(128), static_cast<unsigned long long>(TraceHostTuples), AttemptCount, AttemptLimit,
				(FPlatformTime::Seconds() - TraceStartSeconds) * 1000.0,
				HostOptionIndices.Num(), *LastFailure.Left(256));
		}
		int32 ConflictGroup = INDEX_NONE;
		if (!ValidateVerticalAccessHostPrefix(Request.ModuleCatalog, Request.VerticalAccessHostGroups, HostOptionIndices,
			HostOptionIndices.Num(), FixedOccupiedCells, ConflictGroup, LastFailure))
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			if (FirstFailure.IsEmpty()) FirstFailure = LastFailure;
			// This prefix is impossible regardless of later groups; skip only its suffix.
			for (int32 Index = ConflictGroup + 1; Index < HostOptionIndices.Num(); ++Index)
			{
				const FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups[Index];
				HostOptionIndices[Index] = VerticalAccessHostOptionAtRank(Group,
					Group.Options.Num() - 1 + static_cast<int32>(Group.bAllowOmission));
			}
			continue;
		}
		TOptional<LayoutSolveExecution::FOptionalImprovementScope> SurplusAllowance;
		for (int32 Index = 0; Index < Request.VerticalAccessHostGroups.Num(); ++Index)
		{
			const FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups[Index];
			if (Group.bAllowOmission && !Group.bIsSupplemental && !IsOmittedVerticalAccessHost(Group, HostOptionIndices[Index]))
			{
				SurplusAllowance.Emplace();
				break;
			}
		}
		FLayoutRegionSolveRequest HostRequest = Request;
		if (!Request.VerticalAccessHostGroups.IsEmpty())
		{
			FString HostFailure;
			if (!BuildVerticalAccessHostAlternativeCells(
					HostRequest,
					HostOptionIndices,
					HostRequest.PrecomputedPlannedCells,
					HostFailure))
			{
				if (FirstFailure.IsEmpty()) FirstFailure = HostFailure;
				LastFailure = MoveTemp(HostFailure);
				continue;
			}
			bool bHostOverlapsForbiddenAuthority = false;
			for (int32 GroupIndex = 0;
				GroupIndex < Request.VerticalAccessHostGroups.Num();
				++GroupIndex)
			{
				if (IsOmittedVerticalAccessHost(Request.VerticalAccessHostGroups[GroupIndex], HostOptionIndices[GroupIndex])) continue;
				const FLayoutVerticalAccessHostOption& SelectedOption =
					Request.VerticalAccessHostGroups[GroupIndex]
						.Options[HostOptionIndices[GroupIndex]];
				bHostOverlapsForbiddenAuthority |=
					ForbiddenVerticalAccessCells.Contains(SelectedOption.LowerCell)
					|| ForbiddenVerticalAccessCells.Contains(SelectedOption.UpperCell);
			}
			if (bHostOverlapsForbiddenAuthority)
			{
				LastFailure = FString::Printf(
					TEXT("VerticalAccess host assignment intersects child volume or committed child egress. Selection=%s"),
					*DescribeVerticalAccessHostSelection(
						Request.VerticalAccessHostGroups,
						HostOptionIndices));
				continue;
			}
			HostRequest.PlannedCells = HostRequest.PrecomputedPlannedCells;
			for (int32 GroupIndex = HostRequest.VerticalAccessHostGroups.Num() - 1; GroupIndex >= 0; --GroupIndex)
			{
				FLayoutVerticalAccessHostGroup& Group = HostRequest.VerticalAccessHostGroups[GroupIndex];
				if (IsOmittedVerticalAccessHost(Group, HostOptionIndices[GroupIndex]))
				{
					HostRequest.VerticalAccessHostGroups.RemoveAt(GroupIndex);
					continue;
				}
				Group.Options = {Group.Options[HostOptionIndices[GroupIndex]]};
				Group.DeckCell = Group.Options[0].UpperCell;
				Group.bAllowOmission = false;
				Group.bPreferOmission = false;
			}
			if (!ApplySelectedVerticalAccessCandidateWitnesses(
					Request.VerticalAccessHostGroups,
					HostOptionIndices,
					HostRequest,
					LastFailure))
			{
				if (FirstFailure.IsEmpty()) FirstFailure = LastFailure;
				continue;
			}
		}

		const auto IsPhysicalExteriorCell = [&HostRequest](const FIntVector& Cell)
		{
			return Cell.X == 0
				|| Cell.Y == 0
				|| Cell.X == HostRequest.FootprintSize.X - 1
				|| Cell.Y == HostRequest.FootprintSize.Y - 1;
		};
		const auto IsRelocatableExteriorEntry = [&IsPhysicalExteriorCell](
			const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Intent == ELayoutCellIntent::Entry
				&& (PlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary
					|| (PlannedCell.EntryOrigin == ELayoutEntryOrigin::None
						&& IsPhysicalExteriorCell(PlannedCell.Cell)));
		};

		TArray<FLayoutPlannedCell> OriginalEntries;
		TSet<FIntVector> FixedEntryCells;
		for (const FLayoutPlannedCell& PlannedCell : HostRequest.PlannedCells)
		{
			if (IsRelocatableExteriorEntry(PlannedCell))
			{
				OriginalEntries.Add(PlannedCell);
			}
			else if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				// Local project fix: child prewarm may relocate authored perimeter Entries,
				// but terrain-seam and contracted gates retain their proved topology.
				FixedEntryCells.Add(PlannedCell.Cell);
				if (ForbiddenEntryCells.Contains(PlannedCell.Cell))
				{
					OutFailureReason = FString::Printf(
						TEXT("Fixed internal or contracted Entry at %s intersects child authority."),
						*PlannedCell.Cell.ToString());
					return false;
				}
			}
		}

		TArray<TArray<FIntVector>> EntryCandidatesBySlot;
		EntryCandidatesBySlot.SetNum(OriginalEntries.Num());
		for (int32 EntryIndex = 0; EntryIndex < OriginalEntries.Num(); ++EntryIndex)
		{
			const FLayoutPlannedCell& OriginalEntry = OriginalEntries[EntryIndex];
			TArray<FIntVector>& EntryCandidates = EntryCandidatesBySlot[EntryIndex];
			for (const FLayoutPlannedCell& Candidate : HostRequest.PlannedCells)
			{
				// QualifiedEntryCells is frozen terrain authority. Child-aware relocation
				// may narrow that domain, but must never manufacture terrain-safe Entries.
				if ((!bAllowExteriorEntryRelocation
						&& Candidate.Cell != OriginalEntry.Cell)
					|| (Request.bHasQualifiedEntryCells
						&& !Request.QualifiedEntryCells.Contains(Candidate.Cell))
					|| ForbiddenEntryCells.Contains(Candidate.Cell)
					|| FixedEntryCells.Contains(Candidate.Cell)
					|| !IsPhysicalExteriorCell(Candidate.Cell)
					|| (!bHasCornerEntryModule
						&& HostRequest.ExternalPlannedNeighborFaceMasks.FindRef(Candidate.Cell) == 0
						&& (Candidate.Cell.X == 0 || Candidate.Cell.X == HostRequest.FootprintSize.X - 1)
						&& (Candidate.Cell.Y == 0 || Candidate.Cell.Y == HostRequest.FootprintSize.Y - 1))
					|| (!Request.bHasQualifiedEntryCells && Candidate.Cell.Z != OriginalEntry.Cell.Z)
					|| Candidate.ModuleLevelIndex != OriginalEntry.ModuleLevelIndex
					|| Candidate.Intent == ELayoutCellIntent::VerticalAccess)
				{
					continue;
				}
				EntryCandidates.Add(Candidate.Cell);
			}
			EntryCandidates.Sort([&Request, EntryIndex](
				const FIntVector& Left,
				const FIntVector& Right)
			{
				const uint32 LeftRank = HashCombineFast(
					HashCombineFast(static_cast<uint32>(Request.Seed), EntryIndex + 1),
					GetTypeHash(Left));
				const uint32 RightRank = HashCombineFast(
					HashCombineFast(static_cast<uint32>(Request.Seed), EntryIndex + 1),
					GetTypeHash(Right));
				if (LeftRank != RightRank) return LeftRank < RightRank;
				if (Left.Y != Right.Y) return Left.Y < Right.Y;
				return Left.X < Right.X;
			});
			if (EntryCandidates.IsEmpty())
			{
				LastFailure = TEXT("No exterior Entry candidate remains for prepared parent authority.");
				break;
			}
		}
		if (EntryCandidatesBySlot.ContainsByPredicate(
			[](const TArray<FIntVector>& Candidates) { return Candidates.IsEmpty(); }))
		{
			continue;
		}

		TArray<FIntVector> SelectedEntryCells;
		TSet<FIntVector> UsedEntryCells;
		TFunction<bool(int32)> SearchEntries = [&](const int32 EntryIndex)
		{
			if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::EntryNodes, OutFailureReason)
				|| AttemptCount >= AttemptLimit)
			{
				return false;
			}
			if (EntryIndex < EntryCandidatesBySlot.Num())
			{
				// Retry ranking uses this branch's actual Entries, not independently hashed slots.
				TArray<FIntVector> RankedCandidates = EntryCandidatesBySlot[EntryIndex];
				RankedCandidates.StableSort([&](const FIntVector& Left, const FIntVector& Right)
				{
					const auto Score = [&](const FIntVector& Cell)
					{
						int32 Distance = MAX_int32;
						bool bUnusedHeight = true;
						const auto Include = [&](const FIntVector& Selected)
						{
							Distance = FMath::Min(Distance, FMath::Abs(Cell.X - Selected.X) + FMath::Abs(Cell.Y - Selected.Y));
							bUnusedHeight &= Cell.Z != Selected.Z;
						};
						for (const FIntVector& Selected : SelectedEntryCells) Include(Selected);
						for (const FIntVector& Selected : FixedEntryCells) Include(Selected);
						const int32 Band = Distance <= 1 ? -1 : Distance / FMath::Max(1,
							FMath::Max(Request.FootprintSize.X, Request.FootprintSize.Y) / 4);
						return TPair<int32, bool>(Band, bUnusedHeight);
					};
					const auto LeftScore = Score(Left);
					const auto RightScore = Score(Right);
					if (LeftScore.Key != RightScore.Key) return LeftScore.Key > RightScore.Key;
					if (LeftScore.Value != RightScore.Value) return LeftScore.Value;
					return false; // Retain the existing seeded order within each band.
				});
				for (const FIntVector& CandidateCell : RankedCandidates)
				{
					if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
					if (UsedEntryCells.Contains(CandidateCell)) continue;
					UsedEntryCells.Add(CandidateCell);
					SelectedEntryCells.Add(CandidateCell);
					if (SearchEntries(EntryIndex + 1)) return true;
					SelectedEntryCells.Pop(EAllowShrinking::No);
					UsedEntryCells.Remove(CandidateCell);
				}
				return false;
			}

			++AttemptCount;
			if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::EntryAssignments, OutFailureReason)) return false;
			SCOPED_NAMED_EVENT(Layout_Authority_EntryAttempt, FColor::Orange);
			TRACE_BOOKMARK(TEXT("Layout_AuthorityEntryAttempt region=%s attempt=%d limit=%d hostTuple=%llu entries=%d"),
				*Request.RegionDebugPath.Left(128), AttemptCount, AttemptLimit, static_cast<unsigned long long>(TraceHostTuples),
				SelectedEntryCells.Num());
			FLayoutRegionSolveRequest AttemptRequest = HostRequest;
			TSet<FIntVector> NoEntryCells;
			for (FLayoutPlannedCell& PlannedCell : AttemptRequest.PlannedCells)
			{
				if (IsRelocatableExteriorEntry(PlannedCell))
				{
					PlannedCell.Intent = DetermineProvisionalIntentWithoutVerticalAccess(
						AttemptRequest.FootprintSize,
						NoEntryCells,
						PlannedCell.Cell);
					PlannedCell.EntryOrigin = ELayoutEntryOrigin::None;
				}
			}
			for (int32 SelectedIndex = 0;
				SelectedIndex < SelectedEntryCells.Num();
				++SelectedIndex)
			{
				FLayoutPlannedCell* SelectedCell =
					AttemptRequest.PlannedCells.FindByPredicate(
						[&](const FLayoutPlannedCell& PlannedCell)
						{
							return PlannedCell.Cell == SelectedEntryCells[SelectedIndex];
						});
				if (SelectedCell == nullptr) return false;
				SelectedCell->Intent = ELayoutCellIntent::Entry;
				SelectedCell->EntryOrigin = OriginalEntries[SelectedIndex].EntryOrigin;
			}
			AttemptRequest.PrecomputedPlannedCells = AttemptRequest.PlannedCells;

			// Entry relocation changes exact lateral interfaces. Rebuild each selected
			// local VerticalAccess alternative before accepting joint parent authority.
			bool bRefreshedHostWitnesses = true;
			FString RefreshedHostFailure;
			TArray<FIntVector> SelectedSupportRoots;
			for (const FLayoutVerticalAccessHostGroup& SelectedGroup : AttemptRequest.VerticalAccessHostGroups)
			{
				if (SelectedGroup.Options.Num() == 1) SelectedSupportRoots.Add(SelectedGroup.Options[0].LowerCell);
			}
			for (FLayoutVerticalAccessHostGroup& Group :
				AttemptRequest.VerticalAccessHostGroups)
			{
				if (Group.Options.Num() != 1)
				{
					bRefreshedHostWitnesses = false;
					RefreshedHostFailure = FString::Printf(
						TEXT("Prepared parent host group '%s' is not narrowed to one local alternative."),
						*Group.GroupId.ToString());
					break;
				}
				if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
				if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::WitnessRefreshes, OutFailureReason)) return false;
				SCOPED_NAMED_EVENT(Layout_Authority_RefreshHostWitness, FColor::Orange);
				const FLayoutVerticalAccessHostOption PreviousOption = Group.Options[0];
				if (!PreviousOption.bHasExactCandidateWitness)
				{
					continue;
				}
				TArray<FLayoutVerticalAccessHostOption> RefreshedOptions;
				if (!DoesVerticalAccessHostAdmitCandidatePair(
						AttemptRequest,
						AttemptRequest.FootprintSize,
						AttemptRequest.PlannedCells,
						PreviousOption.LowerCell,
						RefreshedHostFailure,
						SelectedSupportRoots.Num() > 1 ? &SelectedSupportRoots : nullptr,
						nullptr,
						&RefreshedOptions,
						&InvocationVariants))
				{
					bRefreshedHostWitnesses = false;
					break;
				}
				FLayoutVerticalAccessHostOption* RefreshedOption =
					RefreshedOptions.FindByPredicate(
						[&PreviousOption, &MatchesHostSelection](const FLayoutVerticalAccessHostOption& Candidate)
						{
							return MatchesHostSelection(Candidate, PreviousOption);
						});
				if (RefreshedOption == nullptr)
				{
					bRefreshedHostWitnesses = false;
					RefreshedHostFailure = FString::Printf(
						TEXT("Selected VerticalAccess module/yaw witness at %s is no longer legal after Entry settlement."),
						*PreviousOption.LowerCell.ToString());
					break;
				}
				RefreshedOption->Tier = PreviousOption.Tier;
				RefreshedOption->SeedRank = PreviousOption.SeedRank;
				Group.Options[0] = MoveTemp(*RefreshedOption);
				Group.DeckCell = Group.Options[0].UpperCell;
			}
			if (bRefreshedHostWitnesses
				&& !AttemptRequest.VerticalAccessHostGroups.IsEmpty())
			{
				TArray<int32> RefreshedOptionIndices;
				RefreshedOptionIndices.Init(
					0, AttemptRequest.VerticalAccessHostGroups.Num());
				bRefreshedHostWitnesses = ApplySelectedVerticalAccessCandidateWitnesses(
					AttemptRequest.VerticalAccessHostGroups,
					RefreshedOptionIndices,
					AttemptRequest,
					RefreshedHostFailure);
			}
			if (!bRefreshedHostWitnesses)
			{
				if (FirstFailure.IsEmpty()) FirstFailure = RefreshedHostFailure;
				LastFailure = MoveTemp(RefreshedHostFailure);
				return false;
			}

			if (OutFinalProof != nullptr)
			{
				FLayoutRegionSolveRequest FinalAuthority;
				FLayoutRegionSolveResult Proof = SolveRequestBackedRegion(AttemptRequest, false, &FinalAuthority);
				if (!Proof.SolveResult.bSucceeded)
				{
					LastFailure = Proof.SolveResult.FailureReason;
					if (FirstFailure.IsEmpty()) FirstFailure = LastFailure;
				}
				if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
				if (Proof.SolveResult.bSucceeded)
				{
					OutRequest = MoveTemp(FinalAuthority);
					OutFailureReason.Reset();
					*OutFinalProof = MoveTemp(Proof);
					return true;
				}
				return false;
			}

			FSolveContext PreparedContext;
			bool bPreparedAuthority = false;
			if (AttemptRequest.ProfileSnapshot.LevelCount == 1)
			{
				FPreparedSearchPrefixStageCarrier PrefixCarrier;
				bPreparedAuthority =
					TryPrepareRequestSolveContextThroughSearchPrefixStage(
						AttemptRequest,
						PreparedContext,
						PrefixCarrier,
						false);
			}
			else
			{
				// Multi-level regional proof has its own staged prefix. The shared
				// route-domain seam is the strongest non-CSP prewarm witness.
				bPreparedAuthority = TryPrepareRequestSolveContextThroughRouteDomainStage(
					AttemptRequest,
					PreparedContext,
					false);
			}
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			if (bPreparedAuthority)
			{
				if (!Request.VerticalAccessHostGroups.IsEmpty())
				{
					// Prepared proof selects one host per group, but later child settlement
					// may invalidate that witness. Preserve every admitted post-exclusion
					// alternative, with the proven selection first, instead of freezing a singleton.
					TArray<FLayoutVerticalAccessHostGroup> RetainedHostGroups =
						Request.VerticalAccessHostGroups;
					for (int32 GroupIndex = 0; GroupIndex < RetainedHostGroups.Num(); ++GroupIndex)
					{
						FLayoutVerticalAccessHostGroup& RetainedGroup = RetainedHostGroups[GroupIndex];
						const FLayoutVerticalAccessHostGroup* SelectedGroup = AttemptRequest.VerticalAccessHostGroups.FindByPredicate(
							[&RetainedGroup](const FLayoutVerticalAccessHostGroup& Group) { return Group.GroupId == RetainedGroup.GroupId; });
						RetainedGroup.Options.RemoveAll(
							[&](const FLayoutVerticalAccessHostOption& Option)
							{
								return ForbiddenVerticalAccessCells.Contains(Option.LowerCell)
									|| ForbiddenVerticalAccessCells.Contains(Option.UpperCell)
									|| SelectedEntryCells.Contains(Option.LowerCell)
									|| SelectedEntryCells.Contains(Option.UpperCell);
							});
						// Keep the proved count as preference without discarding legal surplus alternatives.
						RetainedGroup.bPreferOmission = RetainedGroup.bAllowOmission && SelectedGroup == nullptr;
						if (SelectedGroup == nullptr) continue;
						const FLayoutVerticalAccessHostOption SelectedOption = SelectedGroup->Options[0];
						const int32 SelectedIndex = RetainedGroup.Options.IndexOfByPredicate(
							[&SelectedOption, &MatchesHostSelection](const FLayoutVerticalAccessHostOption& Option)
							{
								return MatchesHostSelection(Option, SelectedOption);
							});
						if (SelectedIndex == INDEX_NONE)
						{
							return false;
						}
						if (SelectedIndex > 0)
						{
							RetainedGroup.Options.Swap(0, SelectedIndex);
						}
						// Retain refreshed support/clearance as well as exact module/yaw identity.
						RetainedGroup.Options[0] = SelectedOption;
						RetainedGroup.DeckCell = SelectedOption.UpperCell;
					}
					AttemptRequest.VerticalAccessHostGroups = MoveTemp(RetainedHostGroups);
				}
				OutRequest = MoveTemp(AttemptRequest);
				OutFailureReason.Reset();
				return true;
			}
			const FString Failure = PreparedContext.Result.FailureReason;
			if (FirstFailure.IsEmpty()) FirstFailure = Failure;
			LastFailure = Failure;
			// Relocated Entry and host-support overlays can change domains elsewhere.
			// Keep this rejection assignment-local unless an actual dependency proof exists.
			return false;
		};

		if (SearchEntries(0))
		{
			return true;
		}
	}
	while (AttemptCount < AttemptLimit
		&& !Request.VerticalAccessHostGroups.IsEmpty()
		&& AdvanceVerticalAccessHostAlternativeIndices(
			Request.VerticalAccessHostGroups,
			HostOptionIndices));

	if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
	OutFailureReason = FString::Printf(
		TEXT("Prepared parent authority found no bounded Entry/VerticalAccess witness after %d attempt(s). First=%s Last=%s"),
		AttemptCount,
		FirstFailure.IsEmpty() ? TEXT("<none>") : *FirstFailure,
		LastFailure.IsEmpty() ? TEXT("<none>") : *LastFailure);
	return false;
}

bool LayoutProfileSolverInternal::ValidateSelectedHostProofAuthority(
	const FLayoutRegionSolveRequest& InheritedRequest,
	const FLayoutRegionSolveRequest& SelectedRequest,
	const FLayoutSolveResult& Result,
	FString& OutFailureReason)
{
	if (!Result.bSucceeded || Result.CandidateDomainCertificateId != SelectedRequest.CandidateDomainCertificateId)
	{
		OutFailureReason = TEXT("Selected host proof has no successful matching candidate-domain certificate.");
		return false;
	}
	const auto IsHostOverlay = [&InheritedRequest](const FLayoutId Id)
	{
		const FString Name = Id.ToString();
		for (const auto& Group : InheritedRequest.VerticalAccessHostGroups)
		{
			const FString Prefix = Group.GroupId.ToString();
			if (Name.StartsWith(Prefix + TEXT(".Exact.")) || Name.StartsWith(Prefix + TEXT(".Support."))) return true;
		}
		return false;
	};
	for (const auto& Inherited : InheritedRequest.CandidateDomainRestrictions)
	{
		const FLayoutId PermanentId = Inherited.BeforeVerticalAccessCandidates.IsSet()
			? Inherited.BeforeVerticalAccessRestrictionId : Inherited.RestrictionId;
		if (IsHostOverlay(PermanentId)) continue;
		const auto& PermanentDomain = Inherited.BeforeVerticalAccessCandidates.IsSet()
			? Inherited.BeforeVerticalAccessCandidates.GetValue() : Inherited.AllowedCandidates;
		const auto* Selected = SelectedRequest.CandidateDomainRestrictions.FindByPredicate(
			[&](const FLayoutCellCandidateDomainRestriction& Candidate)
			{
				return Candidate.Cell == Inherited.Cell
					&& (Candidate.RestrictionId == PermanentId
						|| Candidate.BeforeVerticalAccessRestrictionId == PermanentId);
			});
		if (Selected == nullptr || Selected->AllowedCandidates.IsEmpty()
			|| Selected->bTreatAsJunctionPlacementZone != Inherited.bTreatAsJunctionPlacementZone
			|| !Result.CandidateDomainRestrictionIds.Contains(PermanentId)
			|| Selected->AllowedCandidates.ContainsByPredicate([&](const FLayoutCandidateVariantIdentity& Candidate)
			{
				return !PermanentDomain.ContainsByPredicate([&](const FLayoutCandidateVariantIdentity& Allowed)
				{
					return Allowed.ModuleSnapshotId == Candidate.ModuleSnapshotId
						&& Allowed.YawRotationSteps == Candidate.YawRotationSteps;
				});
			}))
		{
			OutFailureReason = FString::Printf(TEXT("Selected host proof lost or widened inherited domain '%s' at %s."),
				*PermanentId.ToString(), *Inherited.Cell.ToString());
			return false;
		}
	}
	for (const auto& Selected : SelectedRequest.CandidateDomainRestrictions)
	{
		if (Selected.AllowedCandidates.IsEmpty()
			|| !Result.CandidateDomainRestrictionIds.Contains(Selected.RestrictionId))
		{
			OutFailureReason = FString::Printf(TEXT("Selected host proof did not consume final restriction '%s'."),
				*Selected.RestrictionId.ToString());
			return false;
		}
	}
	return true;
}

/** Necessary face-consistency check on mandatory single-cell structure before independent POI choices.
 * Selected composite footprints are excluded; catalogs permitting other composite ownership bypass this
 * check. It cannot certify a solve, alter counts, or reject a host merely from a remote failure coordinate. */
static bool PruneSparseStructuralFaceDomains(
	const FLayoutRegionSolveRequest& Request,
	const TSet<FIntVector>& MandatoryCells,
	LayoutProfileSolverInternal::FSolveContext& Context,
	FString& OutFailure)
{
	using namespace LayoutProfileSolverInternal;
	for (const auto& Module : Request.ModuleCatalog.Modules)
	{
		if (Module.BoundsCells != FIntVector(1, 1, 1)
			&& (!Module.SupportsIntent(ELayoutCellIntent::VerticalAccess)
				|| !Request.ProfileSnapshot.bRestrictVerticalAccessModulesToVerticalAccessCells)) return true;
	}
	TSet<FIntVector> CompositeCells;
	for (const auto& Pair : Context.InitialDomains)
		for (const auto& Candidate : Pair.Value)
		{
			const auto* Module = FindVariantModuleSnapshot(Context, Candidate.VariantIndex);
			if (!IsOccupiedCandidate(Candidate) || Module == nullptr || Module->BoundsCells == FIntVector(1, 1, 1)) continue;
			for (const auto& LocalCell : Module->OccupiedLocalCells)
				CompositeCells.Add(LayoutPlacementOccupancy::ProjectLocalCellToWorld(
					Pair.Key, LocalCell, Module->BoundsCells, Candidate.YawRotationSteps));
		}
	TSet<FIntVector> Nodes;
	for (const auto& Cell : MandatoryCells)
	{
		const auto* Domain = Context.InitialDomains.Find(Cell);
		if (CompositeCells.Contains(Cell) || Domain == nullptr || Domain->IsEmpty()) continue;
		if (!Domain->ContainsByPredicate([&](const auto& Candidate)
		{
			const auto* Module = FindVariantModuleSnapshot(Context, Candidate.VariantIndex);
			return !IsOccupiedCandidate(Candidate) || Module == nullptr || Module->BoundsCells != FIntVector(1, 1, 1);
		})) Nodes.Add(Cell);
	}
	TArray<FIntVector> Pending = Nodes.Array();
	Pending.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.Z != B.Z) return A.Z < B.Z;
		if (A.Y != B.Y) return A.Y < B.Y;
		return A.X < B.X;
	});
	for (int32 Next = 0; Next < Pending.Num(); ++Next)
	{
		const FIntVector Cell = Pending[Next];
		auto& Domain = Context.InitialDomains.FindChecked(Cell);
		for (const auto Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
		{
			const FIntVector Neighbor = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			if (!Nodes.Contains(Neighbor)) continue;
			const auto& NeighborDomain = Context.InitialDomains.FindChecked(Neighbor);
			FString LastRejectedCandidate;
			bool bChanged = false;
			for (int32 Index = Domain.Num() - 1; Index >= 0; --Index)
			{
				bool bSupported = false;
				for (const auto& Other : NeighborDomain)
				{
					if (!ConsumeSparseStructuralWork(Context)) { OutFailure = Context.Result.FailureReason; return false; }
					if (AreAdjacentCandidatesCompatible(Context, Domain[Index], Other, Direction)) { bSupported = true; break; }
				}
				if (!bSupported)
				{
					if (Domain.Num() == 1) LastRejectedCandidate = BuildCandidateDebugName(Context, Domain[Index]).Left(256);
					Domain.RemoveAt(Index, EAllowShrinking::No); bChanged = true;
				}
			}
			if (Domain.IsEmpty())
			{
				FString NeighborCandidates;
				for (int32 Index = 0; Index < FMath::Min(NeighborDomain.Num(), 4); ++Index)
					NeighborCandidates += BuildCandidateDebugName(Context, NeighborDomain[Index]).Left(256) + TEXT(";");

				if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
				{
					FString Restrictions;
					for (const auto& Restriction : Request.CandidateDomainRestrictions)
					{
						if (Restriction.Cell.Z != Cell.Z || (Restriction.Cell.X != Cell.X && Restriction.Cell.Y != Cell.Y)) continue;
						Restrictions += FString::Printf(TEXT(" [%s %s:"), *Restriction.Cell.ToString(), *Restriction.RestrictionId.ToString());
						for (const auto& Allowed : Restriction.AllowedCandidates)
							Restrictions += FString::Printf(TEXT(" %s/%d"), *Allowed.ModuleSnapshotId.ToString(), Allowed.YawRotationSteps);
						Restrictions += TEXT("]");
						if (Restrictions.Len() > 6000) break;
					}
					UE_LOG(LogTemp, Display, TEXT("DEBUG-wall-owner %s against %s restrictions=%s"), *Cell.ToString(), *Neighbor.ToString(), *Restrictions);
				}
				OutFailure = FString::Printf(TEXT("Mandatory structural face domains conflict at %s against %s before provider selection. LastCandidate=%s NeighborCandidates=[%s]"),
					*Cell.ToString(), *Neighbor.ToString(), *LastRejectedCandidate, *NeighborCandidates);
				return false;
			}
			if (bChanged)
				for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
					if (Nodes.Contains(Cell + Delta)) Pending.Add(Cell + Delta);
		}
	}
	return true;
}

/** Selects exact Entry/committed interfaces over one routing view, then proves normal cells and sparse claims together. */
static bool TrySolveSparseStructuralProjection(
	const FLayoutRegionSolveRequest& Request,
	LayoutProfileSolverInternal::FSolveContext& OwningContext,
	LayoutProfileSolverInternal::FSolveContext& OutSolvedContext,
	FLayoutRegionSolveRequest& OutAuthority,
	const bool bValidateLiveCompositePlacementGuard)
{
	using namespace LayoutProfileSolverInternal;
	OutAuthority = Request;
	OutSolvedContext = OwningContext;
	OutSolvedContext.Result.bSucceeded = false;
	if (!OwningContext.Result.FailureReason.IsEmpty()) return false;
	if (bValidateLiveCompositePlacementGuard && !ValidateLiveLeafSolverModuleSnapshots(OutSolvedContext)) return false;
	TSet<FIntVector> Work;
	for (const auto& Cell : OwningContext.Result.PlannedCells)
	{
		if (!OwningContext.ChildReservationCells.Contains(Cell.Cell)
			&& !OwningContext.TerrainResidualRuleIdByCell.Contains(Cell.Cell)) Work.Add(Cell.Cell);
	}
	for (const auto& Restriction : Request.CandidateDomainRestrictions)
	{
		if (!OwningContext.ChildReservationCells.Contains(Restriction.Cell)) Work.Add(Restriction.Cell);
	}
	for (const auto& Anchor : Request.CommittedTraversalAnchors) Work.Add(Anchor.Cell);
	for (const auto& Group : Request.VerticalAccessHostGroups)
	{
		if (Group.Options.Num() != 1)
		{
			OutSolvedContext.Result.FailureReason = TEXT("Sparse structural preparation requires selected exact host authority.");
			return false;
		}
		Work.Append(Group.Options[0].OccupiedCells);
		for (const FIntVector& Support : Group.Options[0].RequiredFilledSupportCells)
		{
			if (OwningContext.OwningTopologyCellsByPhysicalCell.Contains(Support)
				&& !OwningContext.ChildReservationCells.Contains(Support)) Work.Add(Support);
		}
	}
	const TSet<FIntVector> MandatoryWork = Work;
	// Discover feature-root offers once. Only selected roots enter the joint
	// structural claim; unselected courtyard offers are cropped out again.
	TArray<LayoutZoneFeatureDemand::FHardDemand> ProviderDemands;
	LayoutZoneFeatureDemand::CompileHardDemands(Request.ProfileSnapshot.ZoneFeatureRequirements, ProviderDemands);
	TArray<TArray<FIntVector>> ProviderRoots;
	ProviderRoots.SetNum(ProviderDemands.Num());
	if (!ProviderDemands.IsEmpty() && OwningContext.Variants.IsEmpty()) BuildOrientedVariants(OwningContext);
	for (int32 DemandIndex = 0; DemandIndex < ProviderDemands.Num(); ++DemandIndex)
	{
		const auto& Demand = ProviderDemands[DemandIndex];
		const int32 Precommitted = OwningContext.PrecommittedZoneFeatureProviderCommitments.FilterByPredicate(
			[&Demand](const auto& Credit) { return Credit.RequirementId == Demand.RequirementId; }).Num();
		if (Demand.MinCount <= Precommitted) continue;
		TArray<int32> MatchingVariants;
		for (int32 VariantIndex = 0; VariantIndex < OwningContext.Variants.Num(); ++VariantIndex)
			if (LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchDemand(OwningContext.Variants[VariantIndex].ProvidedZoneFeatures, Demand))
				MatchingVariants.Add(VariantIndex);
		for (const auto& Cell : OwningContext.Result.PlannedCells)
		{
			if (OwningContext.ChildReservationCells.Contains(Cell.Cell)
				|| !LayoutZoneFeatureDemand::DoesFinalizedCellMatchRequirementZone(Cell, Request.FootprintSize, Demand.Zone)) continue;
			for (const int32 VariantIndex : MatchingVariants)
			{
				if (!ConsumeSparseStructuralWork(OwningContext))
				{
					OutSolvedContext.Result.FailureReason = OwningContext.Result.FailureReason;
					return false;
				}
				const auto& Variant = OwningContext.Variants[VariantIndex];
				if (!DoesModuleMatchResolvedPlacementZone(OwningContext, Cell.Cell, Variant.PlacementZone)
					|| !DoesCellMatchLevelPlacementPolicy(Cell.Cell, GetFinalizedCellModuleLevel(OwningContext, Cell.Cell),
						OwningContext.TopPlannedLevelByXY, Variant.LevelPlacementPolicy, Variant.SpecificLevel)) continue;
				ProviderRoots[DemandIndex].Add(Cell.Cell);
				Work.Add(Cell.Cell);
				break;
			}
		}
		ProviderRoots[DemandIndex].Sort([&Request, &OwningContext](const FIntVector& A, const FIntVector& B)
		{
			// Prefer free-standing providers away from already planned structure.
			// This is ordering only: touching a wall/host can still be the legal choice.
			const auto AdjacentStructureCount = [&OwningContext](const FIntVector& Cell)
			{
				int32 Count = 0;
				for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
					Count += OwningContext.PlannedCellIntents.Contains(Cell + Delta)
						&& !OwningContext.TerrainResidualRuleIdByCell.Contains(Cell + Delta);
				return Count;
			};
			const int32 LeftContacts = AdjacentStructureCount(A), RightContacts = AdjacentStructureCount(B);
			if (LeftContacts != RightContacts) return LeftContacts < RightContacts;
			const uint32 Left = HashCombineFast(GetTypeHash(A), Request.Seed);
			const uint32 Right = HashCombineFast(GetTypeHash(B), Request.Seed);
			if (Left != Right) return Left < Right;
			if (A.Z != B.Z) return A.Z < B.Z;
			if (A.Y != B.Y) return A.Y < B.Y;
			return A.X < B.X;
		});
	}
	// Normal structure needs support offers even when a child has already satisfied
	// the feature demand (and no free-standing provider roots are being discovered).
	for (const FIntVector& Cell : MandatoryWork)
		for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0), FIntVector(0,0,1), FIntVector(0,0,-1)})
		{
			if (!ConsumeSparseStructuralWork(OwningContext))
			{
				OutSolvedContext.Result.FailureReason = OwningContext.Result.FailureReason;
				return false;
			}
			const FIntVector Neighbor = Cell + Delta;
			if (OwningContext.TerrainResidualRuleIdByCell.Contains(Neighbor)
				&& !OwningContext.ChildReservationCells.Contains(Neighbor)) Work.Add(Neighbor);
		}
	if (Work.IsEmpty())
	{
		OutSolvedContext.bHasCompleteSparseStructuralRouteAssignment = true;
		return SolvePreparedContext(OutSolvedContext, true, bValidateLiveCompositePlacementGuard);
	}
	FSolveContext Prepared;
	FString Failure;
	// Admission must use the restored offer topology below, not build disposable
	// settled-empty domains that are immediately replaced before any consumer.
	if (!TryBuildSparseStructuralLocalSolveView(OwningContext, Work, Prepared, Failure,
		/*bReusePreparedVariants=*/true, /*bPrepareIndexedSearch=*/false, /*bPrepareDomains=*/false))
	{
		OutSolvedContext.Result.FailureReason = Failure;
		return false;
	}
	// Offers are not simultaneous occupancy. Keep both empty and filled possibilities
	// during discovery; the joint crop later claims only selected providers/support.
	for (const FIntVector& Cell : Work)
		if (!MandatoryWork.Contains(Cell))
			if (const FLayoutId* Rule = OwningContext.TerrainResidualRuleIdByCell.Find(Cell))
				Prepared.TerrainResidualRuleIdByCell.Add(Cell, *Rule);
	Prepared.bTreatTerrainResidualsAsSettledEmpty = false;
	// Keep omitted owning terrain visible as virtual topology, without full-area domains.
	for (const auto& Cell : OwningContext.Result.PlannedCells)
	{
		if (!Prepared.PlannedCellIntents.Contains(Cell.Cell)
			&& !Prepared.ChildReservationCells.Contains(Cell.Cell))
		{
			const int32 Index = Prepared.Result.PlannedCells.Add(Cell);
			Prepared.PlannedCellIntents.Add(Cell.Cell, Cell.Intent);
			if (const auto* View = OwningContext.FinalizedCellsByPhysicalCell.Find(Cell.Cell))
			{
				auto& RestoredView = Prepared.FinalizedCellsByPhysicalCell.Add(Cell.Cell, *View);
				RestoredView.PlannedCellIndex = Index;
			}
		}
	}
	RefreshCompiledFaceInterfaces(Prepared);
	if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::DomainBuilds, OutSolvedContext.Result.FailureReason)) return false;
	for (const FIntVector& Cell : Work)
	{
		if (!LayoutSolveExecution::Checkpoint(OutSolvedContext.Result.FailureReason)) return false;
		Prepared.InitialDomains.Add(Cell, BuildOrderedCandidates(Prepared, Cell, Prepared.PlannedCellIntents.FindChecked(Cell)));
	}
	if (!PruneSparseStructuralFaceDomains(Request, MandatoryWork, Prepared, Failure))
	{
		OutSolvedContext.Result.FailureReason = Failure;
		OutSolvedContext.CandidateAttemptCount = Prepared.CandidateAttemptCount;
		return false;
	}
	TArray<FIntVector> InterfaceCells;
	for (const auto& Pair : Prepared.PlannedCellIntents)
	{
		if (Pair.Value == ELayoutCellIntent::Entry) InterfaceCells.Add(Pair.Key);
	}
	for (const auto& Anchor : Prepared.CommittedTraversalAnchors)
	{
		if (!Anchor.TraversalChannel.IsValid() || !Prepared.PlannedCellIntents.Contains(Anchor.Cell))
		{
			OutSolvedContext.Result.FailureReason = TEXT("Sparse traversal commitment lacks a planned cell or valid channel.");
			return false;
		}
		InterfaceCells.AddUnique(Anchor.Cell);
		if (Prepared.OwningTerrainResidualRuleIdByCell.Contains(Anchor.Cell)
			&& Prepared.PlannedCellIntents[Anchor.Cell] == ELayoutCellIntent::Interior)
		{
			Prepared.PlannedCellIntents[Anchor.Cell] = ELayoutCellIntent::Connector;
			for (auto& Cell : Prepared.Result.PlannedCells)
			{
				if (Cell.Cell == Anchor.Cell) Cell.Intent = ELayoutCellIntent::Connector;
			}
		}
	}
	if (!Prepared.CommittedTraversalAnchors.IsEmpty())
	{
		RefreshCompiledFaceInterfaces(Prepared);
		for (const auto& Anchor : Prepared.CommittedTraversalAnchors)
		{
			Prepared.InitialDomains.Add(Anchor.Cell, BuildOrderedCandidates(Prepared, Anchor.Cell, Prepared.PlannedCellIntents[Anchor.Cell]));
		}
	}
	InterfaceCells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.Z != B.Z) return A.Z < B.Z;
		if (A.Y != B.Y) return A.Y < B.Y;
		return A.X < B.X;
	});
	TArray<TArray<FIntVector>> InterfaceChoices;
	for (const auto& Cell : InterfaceCells) InterfaceChoices.Add({Cell});
	TSet<FIntVector> BoundDestinations;
	for (const auto& Group : Request.VerticalAccessHostGroups)
	{
		FIntVector Destination;
		if (Group.Options.Num() != 1 || !TryGetSparseHostDestination(Group.Options[0], Destination)
			|| !Prepared.PlannedCellIntents.Contains(Destination)
			|| (Prepared.OwningTerrainResidualRuleIdByCell.Contains(Destination)
				&& !IsPreservedGroundDestination(OwningContext, Destination))
			|| Prepared.ChildReservationCells.Contains(Destination))
		{
			OutSolvedContext.Result.FailureReason = TEXT("Sparse host lost its bound direct normal-zone landing contact.");
			return false;
		}
		if (!InterfaceCells.Contains(Destination) && !BoundDestinations.Contains(Destination))
		{
			BoundDestinations.Add(Destination);
			InterfaceChoices.Add({Destination});
		}
	}
	TArray<int32> InterfaceDemandIndices;
	InterfaceDemandIndices.Init(INDEX_NONE, InterfaceChoices.Num());
	for (int32 DemandIndex = 0; DemandIndex < ProviderDemands.Num(); ++DemandIndex)
	{
		const auto& Demand = ProviderDemands[DemandIndex];
		int32 Committed = OwningContext.PrecommittedZoneFeatureProviderCommitments.FilterByPredicate(
			[&Demand](const auto& Credit) { return Credit.RequirementId == Demand.RequirementId; }).Num();
		// A selected landing may already be this provider. Count forced roots once
		// instead of demanding a second POI and relying on MaxCount rejection.
		for (const auto& Restriction : Prepared.CandidateDomainRestrictionsByCell)
		{
			const auto* Cell = Prepared.OwningTopologyCellsByPhysicalCell.Find(Restriction.Key);
			const auto* Domain = Prepared.InitialDomains.Find(Restriction.Key);
			if (Cell != nullptr && Domain != nullptr && !Domain->IsEmpty()
				&& LayoutZoneFeatureDemand::DoesFinalizedCellMatchRequirementZone(*Cell, Request.FootprintSize, Demand.Zone)
				&& !Domain->ContainsByPredicate([&](const auto& Candidate)
					{ return !IsOccupiedCandidate(Candidate) || !LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchDemand(
						Prepared.Variants[Candidate.VariantIndex].ProvidedZoneFeatures, Demand); })) ++Committed;
		}
		for (int32 Slot = Committed; Slot < Demand.MinCount; ++Slot)
		{
			InterfaceChoices.Add(ProviderRoots[DemandIndex]);
			InterfaceDemandIndices.Add(DemandIndex);
		}
	}
	TArray<FIntVector> NormalInterfaces;
	TArray<FIntVector> AcceptedInterfaces;
	FString FirstProjectionFailure;
	TFunction<bool(int32)> SelectEntries = [&](const int32 Index)
	{
		if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::EntryNodes, Failure)) return false;
		if (Index == InterfaceChoices.Num())
		{
			TArray<FSparseStructuralTraversalPort> Ports;
			TArray<int32> SelectedOptions;
			SelectedOptions.Init(0, Request.VerticalAccessHostGroups.Num());
			if (!BuildSparseStructuralTraversalPorts(Prepared, Request.VerticalAccessHostGroups, SelectedOptions, Ports, Failure, NormalInterfaces)) return false;
			if (Ports.IsEmpty())
			{
				// No POIs to stitch: validate the canonical settled-empty/normal projection.
				OutSolvedContext = OwningContext;
				OutSolvedContext.bHasCompleteSparseStructuralRouteAssignment = true;
				return SolvePreparedContext(OutSolvedContext, true, bValidateLiveCompositePlacementGuard);
			}
			FSparseStructuralRouteAssignment Assignment;
			if (!TrySolveSparseStructuralPortAssignment(Prepared, Ports, OutSolvedContext, Assignment, Failure))
			{
				if (FirstProjectionFailure.IsEmpty()) FirstProjectionFailure = Failure;
				return false;
			}
			AcceptedInterfaces = InterfaceCells;
			for (const auto& Cell : NormalInterfaces) AcceptedInterfaces.AddUnique(Cell);
			return true;
		}
		for (const FIntVector& Cell : InterfaceChoices[Index])
		{
		const TArray<FSolveCandidate> Candidates = Prepared.InitialDomains.FindChecked(Cell);
		const auto* Existing = Prepared.CandidateDomainRestrictionsByCell.Find(Cell);
		const bool bHadRestriction = Existing != nullptr;
		FLayoutCellCandidateDomainRestriction Selected = Existing != nullptr ? *Existing : FLayoutCellCandidateDomainRestriction{};
		const FLayoutCellCandidateDomainRestriction Original = Selected;
		Selected.Cell = Cell;
		if (Selected.RestrictionId.IsNone())
		{
			Selected.RestrictionId = FLayoutId(*FString::Printf(TEXT("Sparse.Interface.%s"), *Cell.ToString()));
		}
		const bool bNormalOffer = Index >= InterfaceCells.Num();
		const FLayoutId OriginalResidual = Prepared.TerrainResidualRuleIdByCell.FindRef(Cell);
		Prepared.TerrainResidualRuleIdByCell.Remove(Cell);
		if (bNormalOffer) NormalInterfaces.Add(Cell);
		ON_SCOPE_EXIT
		{
			if (!OriginalResidual.IsNone()) Prepared.TerrainResidualRuleIdByCell.Add(Cell, OriginalResidual);
			if (bNormalOffer) NormalInterfaces.Pop(EAllowShrinking::No);
			Prepared.InitialDomains.Add(Cell, Candidates);
			if (bHadRestriction) Prepared.CandidateDomainRestrictionsByCell.Add(Cell, Original);
			else Prepared.CandidateDomainRestrictionsByCell.Remove(Cell);
		};
		for (const auto& Candidate : Candidates)
		{
			if (!LayoutSolveExecution::Checkpoint(Failure)) return false;
			if (!IsOccupiedCandidate(Candidate)) continue;
			const int32 DemandIndex = InterfaceDemandIndices[Index];
			if (DemandIndex != INDEX_NONE)
			{
				if (!LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchDemand(
					Prepared.Variants[Candidate.VariantIndex].ProvidedZoneFeatures, ProviderDemands[DemandIndex])) continue;
			}
			Selected.AllowedCandidates = {{Candidate.ModuleSnapshotId, Candidate.YawRotationSteps}};
			Prepared.CandidateDomainRestrictionsByCell.Add(Cell, Selected);
			Prepared.InitialDomains.Add(Cell, {Candidate});
			if (SelectEntries(Index + 1)) return true;
		}
		if (Failure.IsEmpty()) Failure = FString::Printf(TEXT("Sparse cell %s has no legal occupied interface. %s"), *Cell.ToString(),
			*FString::Join(Prepared.InitialDomainAdmissionFailuresByCell.FindRef(Cell), TEXT(" | ")));
		}
		return false;
	};
	if (!SelectEntries(0))
	{
		OutSolvedContext.Result.PlannedCells = OwningContext.Result.PlannedCells;
		OutSolvedContext.CandidateAttemptCount = Prepared.CandidateAttemptCount;
		OutSolvedContext.bTimeBudgetExceeded |= Prepared.bTimeBudgetExceeded;
		OutSolvedContext.Result.bSucceeded = false;
		if (!Failure.IsEmpty()) OutSolvedContext.Result.FailureReason = Failure;
		if (!FirstProjectionFailure.IsEmpty() && FirstProjectionFailure != Failure)
			OutSolvedContext.Result.FailureReason += FString::Printf(TEXT(" First structural proof rejection: %s"), *FirstProjectionFailure.Left(2048));
		return false;
	}
	for (const FIntVector& Cell : AcceptedInterfaces)
	{
		const auto& Selected = OutSolvedContext.CandidateDomainRestrictionsByCell.FindChecked(Cell);
		// These singleton domains were installed before the successful joint proof.
		OutSolvedContext.Result.CandidateDomainRestrictionIds.AddUnique(Selected.RestrictionId);
		auto* Existing = OutAuthority.CandidateDomainRestrictions.FindByPredicate([&](const auto& Value) { return Value.Cell == Cell; });
		if (Existing != nullptr) *Existing = Selected;
		else OutAuthority.CandidateDomainRestrictions.Add(Selected);
	}
	if (!AcceptedInterfaces.IsEmpty() && OutAuthority.CandidateDomainCertificateId.IsNone())
	{
		OutAuthority.CandidateDomainCertificateId = FLayoutId(*FString::Printf(TEXT("SparseStructural.%s.%d"),
			*Request.EffectiveSnapshotId.ToString(), Request.Seed));
	}
	OutSolvedContext.CandidateDomainCertificateId = OutAuthority.CandidateDomainCertificateId;
	OutSolvedContext.Result.CandidateDomainCertificateId = OutAuthority.CandidateDomainCertificateId;
	OutSolvedContext.Result.CandidateDomainRestrictionIds.Sort([](const FLayoutId& A, const FLayoutId& B) { return A.LexicalLess(B); });
	// Restore omitted terrain without erasing route/support annotations or retaining local array indices.
	TMap<FIntVector, FLayoutPlannedCell> ActiveCells;
	for (const auto& Cell : OutSolvedContext.Result.PlannedCells) ActiveCells.Add(Cell.Cell, Cell);
	TArray<FLayoutPlannedCell> PublishedCells = OwningContext.Result.PlannedCells;
	auto PublishedViews = OwningContext.FinalizedCellsByPhysicalCell;
	for (int32 Index = 0; Index < PublishedCells.Num(); ++Index)
	{
		const FIntVector Cell = PublishedCells[Index].Cell;
		if (const auto* Active = ActiveCells.Find(Cell)) PublishedCells[Index] = *Active;
		if (const auto* ActiveView = OutSolvedContext.FinalizedCellsByPhysicalCell.Find(Cell))
		{
			auto& View = PublishedViews.FindOrAdd(Cell);
			View = *ActiveView;
			View.PlannedCellIndex = Index;
		}
	}
	OutSolvedContext.Result.PlannedCells = MoveTemp(PublishedCells);
	OutSolvedContext.FinalizedCellsByPhysicalCell = MoveTemp(PublishedViews);
	return LayoutSolveExecution::Checkpoint(OutSolvedContext.Result.FailureReason);
}

FLayoutRegionSolveResult LayoutProfileSolverInternal::SolveRequestBackedRegion(
	const FLayoutRegionSolveRequest& Request,
	const bool bValidateLiveCompositePlacementGuard,
	FLayoutRegionSolveRequest* OutFinalAuthority)
{
	check(OutFinalAuthority != &Request);
	LayoutSolveExecution::FScope ExecutionScope(Request.ExecutionSettings.MaxSolveDurationSeconds, Request.ExecutionSettings.MaxCandidateAttempts);
	if (OutFinalAuthority != nullptr) *OutFinalAuthority = Request;
	FString ExecutionFailure;
	if (!LayoutSolveExecution::Checkpoint(ExecutionFailure))
	{
		FLayoutRegionSolveResult Failed;
		InitializeRequestBackedRegionResultFromRequest(Request, Failed);
		Failed.SolveResult.bSucceeded = false;
		Failed.SolveResult.FailureReason = MoveTemp(ExecutionFailure);
		return Failed;
	}
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_LeafRegion, STAT_PorismLayout_LeafSolve);
	INC_DWORD_STAT(STAT_PorismLayout_RegionSolveCalls);
	const bool bPreservesTerrain = Request.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate(
		[](const FLayoutSparsePlacementRuleSolveSnapshot& Rule)
		{
			return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
				|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
		});
	const bool bDirectRoot = Request.SourceParentRegionDebugPath.IsEmpty()
		&& !IsContinuationRootPlacementKind(ResolveEffectiveRootPlacementKind(Request));
	const bool bUsesSteppedEnvironment = !Request.bHasSelectedModePlan
		|| DoesRequestUseSteppedTerrainContract(Request);
	if (bDirectRoot
		&& ((Request.ProfileSnapshot.bSupportsSteppedTerrainSolve && bUsesSteppedEnvironment)
			// Sparse flat requests need exact host authority before empty settlement.
			// Ordinary supplied plans retain their explicit intents, not a new flat plan.
			|| (bPreservesTerrain && Request.VerticalAccessHostGroups.IsEmpty()))
		&& !Request.bHasFinalizedSteppedTerrainIntents
		&& (bPreservesTerrain || !Request.PrecomputedPlannedCells.IsEmpty() || !Request.PlannedCells.IsEmpty()))
	{
		FLayoutRegionSolveRequest FinalizedRequest = Request;
		TArray<FLayoutPlannedCell>& CellsToFinalize = !FinalizedRequest.PrecomputedPlannedCells.IsEmpty()
			? FinalizedRequest.PrecomputedPlannedCells
			: FinalizedRequest.PlannedCells;
		FString FinalizationFailureReason;
		// Direct imported requests may carry no plan. They need the same exact
		// host preparation as adapter-backed requests before sparse empty settlement.
		const bool bHasAuthoredPlan = !CellsToFinalize.IsEmpty()
			|| BuildAuthoredPlan(FinalizedRequest.ProfileSnapshot, FinalizedRequest.ModuleCatalog,
				FinalizedRequest.Seed, FinalizedRequest.FootprintSize, CellsToFinalize, FinalizationFailureReason);
		if (!bHasAuthoredPlan || !FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
				CellsToFinalize,
				FinalizedRequest,
				FinalizationFailureReason))
		{
			FLayoutRegionSolveResult FailedResult;
			InitializeRequestBackedRegionResultFromRequest(Request, FailedResult);
			FailedResult.SolveResult.bSucceeded = false;
			FailedResult.SolveResult.FailureReason = FinalizationFailureReason;
			return FailedResult;
		}
		FinalizedRequest.PlannedCells = CellsToFinalize;
		FinalizedRequest.PrecomputedPlannedCells = CellsToFinalize;
		return SolveRequestBackedRegion(FinalizedRequest, bValidateLiveCompositePlacementGuard, OutFinalAuthority);
	}

	FLayoutRegionSolveResult RegionResult;
	InitializeRequestBackedRegionResultFromRequest(Request, RegionResult);
	if (TryFinalizeEarlyRequestBackedRegionFailure(Request, RegionResult))
	{
		return RegionResult;
	}

	FSolveContext Context =
		BuildLiveSolveContextFromRequest(Request, RegionResult.SolveResult);

	if (bPreservesTerrain && Request.ProfileSnapshot.LevelCount == 1
		&& Request.ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::None
		&& Request.VerticalAccessHostGroups.IsEmpty()
		&& !Request.bDeferTraversalValidationToSchedule
		&& Context.Result.bSucceeded && Context.Result.FailureReason.IsEmpty())
	{
		FSolveContext Solved;
		FLayoutRegionSolveRequest SelectedAuthority;
		if (!TrySolveSparseStructuralProjection(Request, Context, Solved, SelectedAuthority, bValidateLiveCompositePlacementGuard))
		{
			Solved.Result.bSucceeded = false;
		}
		if (Solved.Result.bSucceeded && OutFinalAuthority != nullptr) *OutFinalAuthority = SelectedAuthority;
		FinalizeRequestBackedLeafRegionResultFromContext(SelectedAuthority, MoveTemp(Solved), RegionResult);
		if (!LayoutSolveExecution::Checkpoint(RegionResult.SolveResult.FailureReason)) RegionResult.SolveResult.bSucceeded = false;
		return RegionResult;
	}

	if (Request.ProfileSnapshot.LevelCount > 1 || !Request.VerticalAccessHostGroups.IsEmpty())
	{
		const TArray<FLayoutCellReservationRecord> PreparedReservations = Context.Result.CompiledReservations;
		// Rebuilt contexts borrow catalog/terrain pointers. Keep selected authority alive
		// through partial-result recovery and artifact projection, not just host selection.
		TUniquePtr<FLayoutRegionSolveRequest> LastAttemptAuthority;
		if (Request.VerticalAccessHostGroups.IsEmpty())
		{
			RegionResult.SolveResult = SolveUnifiedVerticalRegions(
				Request.ProfileSnapshot,
				&Request.ExecutionSettings,
				Request.Seed,
				Context.Result,
				Context.FootprintSize,
				Context.Result.PlannedCells,
				true,
				&Request.ModuleCatalog,
				&Request.IncomingBoundaryPoints,
				&Request.CommittedEndpointAnchors,
				&Request.CommittedTraversalAnchors,
				Context.bUsesChildSolveContext,
				Request.bDeferTraversalValidationToSchedule,
				bValidateLiveCompositePlacementGuard,
				&Request.PrecomputedFrozenTerrainContract,
				&Request.ExternalPlannedNeighborFaceMasks,
				nullptr,
				nullptr,
				Request.bHasSelectedModePlan
					? Request.SelectedModePlan.Scope
					: ELayoutContractRegionScope::Root,
				&Request);
		}
		else
		{
			LastAttemptAuthority = MakeUnique<FLayoutRegionSolveRequest>(Request);
			FLayoutRegionSolveRequest& LastAttemptRequest = *LastAttemptAuthority;
			RegionResult.SolveResult = SolveUnifiedVerticalRegionsWithHostAlternatives(
				Request,
				Context.Result,
				LastAttemptRequest);
			if (RegionResult.SolveResult.bSucceeded
				&& !ValidateSelectedHostProofAuthority(Request, LastAttemptRequest, RegionResult.SolveResult,
					RegionResult.SolveResult.FailureReason))
			{
				RegionResult.SolveResult.bSucceeded = false;
			}
			FLayoutSolveResult LastAttemptBaseResult = RegionResult.SolveResult;
			Context = BuildLiveSolveContextFromRequest(LastAttemptRequest, LastAttemptBaseResult);
			if (OutFinalAuthority != nullptr) *OutFinalAuthority = LastAttemptRequest;
		}
		if (!PreparedReservations.IsEmpty())
		{
			// Vertical-region solving rebuilds leaf contexts; restore frozen selected reservations after it returns.
			RegionResult.SolveResult.CompiledReservations = PreparedReservations;
		}
		RegionResult.SolveResult.TemplatePlacementZOffsetBlocks = Request.TemplatePlacementZOffsetBlocks;
		RegionResult.SolveResult.SharedCellSizeInBlocks = ResolveRequestSharedCellSizeInBlocks(Request);
		RegionResult.SolveResult.RootPlacementKind = Request.RootPlacementKind;
		RegionResult.SolveResult.WorldBindingPlacementPolicy = Request.WorldBindingPlacementPolicy;
		PopulateResolvedTerrainAlignmentLevel(Request, RegionResult.SolveResult);
		LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(RegionResult.SolveResult);
		if (RegionResult.SolveResult.bSucceeded)
		{
			FLayoutPlannedCell UnoccupiedBridgeCell;
			if (LayoutProfileSolverInternal::TryFindFirstUnoccupiedGeneratedBridgeCell(
					RegionResult.SolveResult,
					UnoccupiedBridgeCell))
			{
				RegionResult.SolveResult.bSucceeded = false;
				RegionResult.SolveResult.FailureReason = FString::Printf(
					TEXT("Generated bridge cell %s (authored level %d) is unoccupied. Check module face rules for bridge cell positions."),
					*UnoccupiedBridgeCell.Cell.ToString(),
					UnoccupiedBridgeCell.ModuleLevelIndex);
			}
		}
		if (RegionResult.SolveResult.bSucceeded)
		{
			FString SparseFailureReason;
			if (!LayoutProfileSolverInternal::ApplySparsePlacementRulesToSolveResult(
				Request.ProfileSnapshot,
				Request.RegionDebugPath,
				Request.Seed,
				RegionResult.SolveResult,
				SparseFailureReason))
			{
				RegionResult.SolveResult.bSucceeded = false;
				RegionResult.SolveResult.FailureReason = SparseFailureReason;
			}
			else
			{
				FString ZoneFeatureFailureReason;
				if (!EvaluateLeafZoneFeatureRequirementsForResult(Request, RegionResult.SolveResult, ZoneFeatureFailureReason))
				{
					RegionResult.SolveResult.bSucceeded = false;
					RegionResult.SolveResult.FailureReason = ZoneFeatureFailureReason;
				}
			}
		}
		if (!RegionResult.SolveResult.bSucceeded)
		{
			// Preserve the complete planned grid and original full-solve failure, then
			// retain only lower physical levels that solve against that full topology.
			TryBuildRetainedLowerLevelPartial(
				Request,
				Context,
				RegionResult.SolveResult,
				bValidateLiveCompositePlacementGuard);
			LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(
				RegionResult.SolveResult);
		}
		ApplyStagedTerrainArtifactsToRegionResult(
			Request,
			RegionResult,
			&Request.NegotiatedChildResponsibilityContracts);
		return RegionResult;
	}

	const bool bSolved =
		SolvePreparedContext(
			Context,
			!Request.bDeferClosureValidationToSchedule,
			bValidateLiveCompositePlacementGuard);
	if (!bSolved)
	{
		Context.Result.bSucceeded = false;
		if (Context.Result.FailureReason.IsEmpty())
		{
			Context.Result.FailureReason = FString::Printf(
				TEXT("Leaf region solve failed: propagation found %d cell(s) with no legal candidates."),
				Context.Result.PropagationStats.FailedCellCount);
		}
	}
	FinalizeRequestBackedLeafRegionResultFromContext(
		Request,
		MoveTemp(Context),
		RegionResult);
	return RegionResult;
}

/** Rejects undersized shifted topology after route and adapter mutations settle. */
static bool ValidateMinimumFinalizedSteppedShiftClusterSize(
	const FLayoutRegionSolveRequest& Request,
	const TArray<FLayoutPlannedCell>& FinalizedCells,
	FString& OutFailureReason)
{
	const int32 ConfiguredMinimum =
		Request.WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells;
	const int32 MinimumClusterCells = ConfiguredMinimum > 0 ? ConfiguredMinimum : 3;
	if (MinimumClusterCells <= 1)
	{
		return true;
	}

	TSet<FIntPoint> ShiftedColumns;
	for (const FLayoutPlannedCell& PlannedCell : FinalizedCells)
	{
		const bool bShiftedAuthoredCell = !PlannedCell.bIsBridgeCell
			&& PlannedCell.ModuleLevelIndex != INDEX_NONE
			&& PlannedCell.Cell.Z != PlannedCell.ModuleLevelIndex;
		const bool bElevatedGeneratedBridge = PlannedCell.bIsBridgeCell && PlannedCell.Cell.Z > 0;
		if (bShiftedAuthoredCell || bElevatedGeneratedBridge)
		{
			ShiftedColumns.Add(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
		}
	}

	TSet<FIntPoint> Unvisited = ShiftedColumns;
	while (!Unvisited.IsEmpty())
	{
		FIntPoint FirstCell = *Unvisited.CreateConstIterator();
		for (const FIntPoint& Cell : Unvisited)
		{
			if (Cell.Y < FirstCell.Y || (Cell.Y == FirstCell.Y && Cell.X < FirstCell.X))
			{
				FirstCell = Cell;
			}
		}
		TArray<FIntPoint> Pending = {FirstCell};
		TArray<FIntPoint> Cluster;
		Unvisited.Remove(FirstCell);
		while (!Pending.IsEmpty())
		{
			const FIntPoint Cell = Pending.Pop(EAllowShrinking::No);
			Cluster.Add(Cell);
			for (const FIntPoint& Delta : {
				FIntPoint(1, 0), FIntPoint(-1, 0),
				FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const FIntPoint Neighbor = Cell + Delta;
				if (Unvisited.Remove(Neighbor) > 0)
				{
					Pending.Add(Neighbor);
				}
			}
		}
		if (Cluster.Num() < MinimumClusterCells)
		{
			Cluster.Sort([](const FIntPoint& Left, const FIntPoint& Right)
			{
				return Left.Y != Right.Y ? Left.Y < Right.Y : Left.X < Right.X;
			});
			OutFailureReason = FString::Printf(
				TEXT("Finalized stepped topology has a connected shifted cluster of %d cells below configured minimum %d at X=%d Y=%d."),
				Cluster.Num(),
				MinimumClusterCells,
				Cluster[0].X,
				Cluster[0].Y);
			return false;
		}
	}
	return true;
}

/** Verifies frozen bridge and shifted-cell topology through the same static admission used by worker initial domains. */
static bool ValidateFinalizedSteppedTopologyStaticDomains(
	FLayoutRegionSolveRequest& Request,
	TArray<FLayoutPlannedCell>& FinalizedCells,
	FString& OutFailureReason)
{
	const bool bUsesSteppedTerrainContract = DoesRequestUseSteppedTerrainContract(Request);
	if (!bUsesSteppedTerrainContract)
	{
		return true;
	}

	TSet<FIntVector> AffectedCells;
	for (const FLayoutPlannedCell& PlannedCell : FinalizedCells)
	{
		const bool bShiftedAuthoredCell = !PlannedCell.bIsBridgeCell
			&& PlannedCell.ModuleLevelIndex != INDEX_NONE
			&& PlannedCell.ModuleLevelIndex != PlannedCell.Cell.Z;
		if (!PlannedCell.bIsBridgeCell && !bShiftedAuthoredCell)
		{
			continue;
		}
		AffectedCells.Add(PlannedCell.Cell);
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY,
			ELayoutFaceDirection::PosZ, ELayoutFaceDirection::NegZ })
		{
			AffectedCells.Add(PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction));
		}
	}
	if (AffectedCells.IsEmpty())
	{
		return true;
	}

	FLayoutSolveResult ProbeResult;
	LayoutProfileSolverInternal::FSolveContext ProbeContext;
	ApplySolveSettings(
		ProbeContext,
		nullptr,
		&Request.ProfileSnapshot,
		&Request.ExecutionSettings,
		Request.Seed,
		ProbeResult,
		true,
		&Request.ModuleCatalog);
	InitializeSolveContextFromRequest(ProbeContext, Request);
	BuildOrientedVariants(ProbeContext);
	if (!ProbeContext.Result.FailureReason.IsEmpty())
	{
		OutFailureReason = ProbeContext.Result.FailureReason;
		return false;
	}
	TSet<FIntVector> AdmittedHostCells;
	for (const auto& Group : Request.VerticalAccessHostGroups)
		for (const auto& Option : Group.Options)
			if (Option.bHasExactCandidateWitness) AdmittedHostCells.Append(Option.OccupiedCells);
	TSet<FIntVector> HostRequiredCells;
	FIntVector UnsupportedCell = FIntVector::ZeroValue;
	FString ProbeFailureReason;
	FString StructuralAdmissionFailure;
	FSolveContext StructuralVariants;
	TArray<FIntVector> WallTransitionCells;
	TMap<FIntVector, const FLayoutPlannedCell*> CellsByPosition;
	TMap<FIntPoint, int32> AuthoredTops = Request.OwningTopModuleLevelByXY;
	for (const auto& Cell : FinalizedCells)
	{
		CellsByPosition.Add(Cell.Cell, &Cell);
		const FIntPoint XY(Cell.Cell.X, Cell.Cell.Y);
		if (!Cell.bIsBridgeCell && !Request.OwningTopModuleLevelByXY.Contains(XY))
		{
			int32& Top = AuthoredTops.FindOrAdd(XY);
			Top = FMath::Max(Top, ResolvePlannedCellModuleLevel(Cell));
		}
	}
	TSet<FIntVector> WallTopCells;
	for (const auto& Cell : FinalizedCells)
	{
		if (!Cell.bIsBridgeCell && IsBoundaryCell(Cell.Cell, Request.FootprintSize)
			&& ResolvePlannedCellModuleLevel(Cell) == AuthoredTops.FindRef(FIntPoint(Cell.Cell.X, Cell.Cell.Y)))
			WallTopCells.Add(Cell.Cell);
	}
	TMap<FIntVector, TArray<FIntVector>> WallDestinations;
	for (const auto& Cell : FinalizedCells)
	{
		if (!Cell.bIsBridgeCell || !WallTopCells.Contains(Cell.Cell - FIntVector(0, 0, 1))) continue;
		for (const auto Delta : {FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0)})
		{
			if (WallTopCells.Contains(Cell.Cell + Delta))
				WallDestinations.FindOrAdd(Cell.Cell).Add(Cell.Cell + Delta);
		}
	}
	// Normal-fill profiles prove connectivity through their whole interior and
	// authored stairs. A geometric wall step is not an additional local stair
	// obligation there. Sparse preservation keeps its explicit wall access demands;
	// static bridge/cap legality below remains mandatory for both paths.
	const bool bPreservesTerrain = Request.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate([](const auto& Rule)
	{
		return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
			|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
	});
	if (bPreservesTerrain) WallDestinations.GetKeys(WallTransitionCells);
	const auto ProvesWallCrossing = [&WallDestinations](const FLayoutVerticalAccessHostOption& Witness, const FIntVector& Crossing)
	{
		const auto* Destinations = WallDestinations.Find(Crossing);
		FIntVector Destination;
		return Destinations != nullptr && Witness.bHasExactCandidateWitness
			&& Witness.LowerCell == Crossing - FIntVector(0, 0, 1)
			&& Witness.UpperCell == Crossing && Witness.LowerTraversalPortFaceMask != 0
			&& TryGetSparseHostDestination(Witness, Destination) && Destinations->Contains(Destination);
	};
	WallTransitionCells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.Z != B.Z) return A.Z > B.Z;
		if (A.Y != B.Y) return A.Y > B.Y;
		return A.X > B.X;
	});
	// Wall traversal is independent of static cap legality. Offer exact bundles
	// at height transitions even when both disconnected wall runs admit modules.
	while (true)
	{
		const bool bTraversalOffer = DoesMutatedPlanKeepStaticDomains(ProbeContext, FinalizedCells,
			AffectedCells, UnsupportedCell, ProbeFailureReason, false, &AdmittedHostCells, &HostRequiredCells);
		if (bTraversalOffer)
		{
			if (WallTransitionCells.IsEmpty()) break;
			UnsupportedCell = WallTransitionCells.Pop(EAllowShrinking::No);
			// An alternative footprint is not coverage. Only an unavoidable exact
			// crossing in an existing mandatory group can discharge this obligation.
			if (Request.VerticalAccessHostGroups.ContainsByPredicate([&](const auto& Group)
			{
				return !Group.bAllowOmission && !Group.Options.IsEmpty()
					&& !Group.Options.ContainsByPredicate([&](const auto& Option) { return !ProvesWallCrossing(Option, UnsupportedCell); });
			})) continue;
		}
		if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
		TSet<FIntVector> CandidateRoots;
		for (const auto& Module : Request.ModuleCatalog.Modules)
		{
			if (!Module.SupportsIntent(ELayoutCellIntent::VerticalAccess)) continue;
			for (const int32 Yaw : Module.AllowedYawRotationSteps)
			{
				for (const FIntVector& LocalCell : Module.OccupiedLocalCells)
				{
					if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::RankedNodes, OutFailureReason)) return false;
					CandidateRoots.Add(UnsupportedCell - LayoutPlacementOccupancy::ProjectLocalCellToWorld(
						FIntVector::ZeroValue, LocalCell, Module.BoundsCells, Yaw));
				}
			}
		}
		CandidateRoots.Add(UnsupportedCell);
		CandidateRoots.Add(UnsupportedCell - FIntVector(0, 0, 1));
		TArray<FIntVector> OrderedRoots = CandidateRoots.Array();
		OrderedRoots.Sort([](const FIntVector& A, const FIntVector& B)
		{
			if (A.Z != B.Z) return A.Z < B.Z;
			if (A.Y != B.Y) return A.Y < B.Y;
			return A.X < B.X;
		});
		FLayoutVerticalAccessHostGroup StructuralGroup;
		StructuralGroup.GroupId = FLayoutId(*FString::Printf(TEXT("Generated%s.%s"),
			bTraversalOffer ? TEXT("WallTransition") : TEXT("StructuralBridgeRepair"), *UnsupportedCell.ToString()));
		// A discovered wall-height crossing owes an actual ascent, independently of
		// optional long routes. Static-only repair offers may still yield to another
		// compatible normal/bundle choice in the final structural proof.
		StructuralGroup.bAllowOmission = !bTraversalOffer;
		StructuralGroup.bIsSupplemental = true;
		for (const FIntVector& Root : OrderedRoots)
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			const auto* Lower = CellsByPosition.FindRef(Root);
			const auto* Upper = CellsByPosition.FindRef(Root + FIntVector(0, 0, 1));
			// Only generated bridge transitions belong to this supplemental pass;
			// ordinary authored pairs keep their existing count slots. A bridge can
			// lie below shifted normal structure, with different authored levels.
			// Exact admission owns level/zone/face legality, not this geometric filter.
			if (Lower == nullptr || Upper == nullptr
				|| (!Lower->bIsBridgeCell && !Upper->bIsBridgeCell)
				|| Lower->Intent == ELayoutCellIntent::Entry
				|| Upper->Intent == ELayoutCellIntent::Entry) continue;
			TArray<FLayoutVerticalAccessHostOption> Witnesses;
			FString Failure;
			if (!DoesVerticalAccessHostAdmitCandidatePair(Request, Request.FootprintSize, FinalizedCells,
				Root, Failure, nullptr, nullptr, &Witnesses, &StructuralVariants))
			{
				if (StructuralAdmissionFailure.IsEmpty()) StructuralAdmissionFailure = Failure;
				continue;
			}
			for (auto& Witness : Witnesses)
			{
				if (!Witness.bHasExactCandidateWitness || !Witness.OccupiedCells.Contains(UnsupportedCell)) continue;
				if (bTraversalOffer && !ProvesWallCrossing(Witness, UnsupportedCell)) continue;
				// Perimeter-only stairs describe wall-top crossings, not generic support repair.
				const auto* Module = Request.ModuleCatalog.Modules.FindByPredicate([&](const auto& Candidate)
					{ return Candidate.SnapshotId == Witness.LowerModuleSnapshotId; });
				if (!bTraversalOffer && Module != nullptr && Module->PlacementZone == ELayoutPlacementZone::Perimeter
					&& !ProvesWallCrossing(Witness, Witness.UpperCell)) continue;
				Witness.LowerCell = Root;
				Witness.UpperCell = Root + FIntVector(0, 0, 1);
				Witness.Tier = ELayoutVerticalAccessHostTier::Constrained;
				Witness.SeedRank = GetVerticalAccessHostSeedRank(Request, StructuralGroup.GroupId, Root);
				StructuralGroup.Options.Add(MoveTemp(Witness));
			}
		}
		if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
		if (StructuralGroup.Options.IsEmpty())
		{
			if (bTraversalOffer)
			{
				OutFailureReason = FString::Printf(TEXT("Wall-top crossing at %s has no exact stair connecting its lower and upper wall runs. %s"),
					*UnsupportedCell.ToString(), *StructuralAdmissionFailure.Left(2048));
				return false;
			}
			break;
		}
		if (bTraversalOffer) HostRequiredCells.Add(UnsupportedCell);
		StructuralGroup.DeckCell = StructuralGroup.Options[0].UpperCell;
		for (const auto& Option : StructuralGroup.Options) AdmittedHostCells.Append(Option.OccupiedCells);
		Request.VerticalAccessHostGroups.Add(MoveTemp(StructuralGroup));
	}
	if (ProbeFailureReason.IsEmpty())
	{
		if (!HostRequiredCells.IsEmpty())
		{
			// Prefer the demonstrated structural repair before unrelated coverage offers.
			// Do not freeze these choices or remove alternatives before joint host proof.
			for (auto& Group : Request.VerticalAccessHostGroups)
			{
				Group.Options.StableSort([&HostRequiredCells](const auto& A, const auto& B)
				{
					int32 Left = 0, Right = 0;
					for (const auto& Cell : HostRequiredCells)
					{
						Left += A.OccupiedCells.Contains(Cell);
						Right += B.OccupiedCells.Contains(Cell);
					}
					return Left > Right;
				});
				if (!Group.Options.IsEmpty()) Group.DeckCell = Group.Options[0].UpperCell;
			}
			FLayoutRegionSolveRequest PreviewRequest = Request;
			PreviewRequest.PrecomputedPlannedCells = FinalizedCells;
			TArray<int32> Options;
			for (const auto& Group : Request.VerticalAccessHostGroups) Options.Add(VerticalAccessHostOptionAtRank(Group, 0));
			if (!BuildVerticalAccessHostAlternativeCells(PreviewRequest, Options, FinalizedCells, OutFailureReason)) return false;
		}
		return true;
	}

	if (LayoutSolveExecution::ShouldStop())
	{
		OutFailureReason = ProbeFailureReason;
		return false;
	}
	const FLayoutFrozenTerrainStageCellRecord* const SourceStage =
		Request.PrecomputedFrozenTerrainContract.StageMap.FindByPredicate([&UnsupportedCell](const FLayoutFrozenTerrainStageCellRecord& Stage)
		{
			return Stage.FootprintCellXY == FIntPoint(UnsupportedCell.X, UnsupportedCell.Y);
		});
	OutFailureReason = FString::Printf(
		TEXT("Frozen stepped topology rejected before worker dispatch: first unsupported domain=%s; source evidence=%s; admitted stair coverage=none. %s Structural wall-transition admission: %s"),
		*UnsupportedCell.ToString(),
		SourceStage != nullptr ? *SourceStage->SourceEvidenceId.ToString() : TEXT("<none>"),
		*ProbeFailureReason,
		StructuralAdmissionFailure.IsEmpty() ? TEXT("No exact wall/bridge bundle covers the unsupported cell.") : *StructuralAdmissionFailure);
	return false;
}

bool FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
	TArray<FLayoutPlannedCell>& InOutPlannedCells,
	FLayoutRegionSolveRequest& InOutRequest,
	FString& OutFailureReason,
	ELayoutSteppedTerrainFinalizationFailureKind* OutFailureKind,
	TArray<FLayoutCellReservationRecord>* OutSelectedReservations)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Terrain_SteppedPlanFinalize, STAT_PorismLayout_SteppedPlanFinalize);
	if (OutFailureKind != nullptr)
	{
		*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::None;
	}
	if (OutSelectedReservations != nullptr)
	{
		OutSelectedReservations->Reset();
	}
	// Flat explicit requests and flat recovery must establish the same Entry/host authority.
	if ((InOutRequest.bHasSelectedModePlan && !InOutRequest.SelectedModePlan.bUsesSteppedTerrainTopology)
		|| !InOutRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve)
	{
		const bool bFinalizedFlat = FinalizeFlatTerrainPlan(InOutPlannedCells, InOutRequest, OutFailureReason);
		if (bFinalizedFlat && OutSelectedReservations != nullptr)
		{
			*OutSelectedReservations = InOutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations;
		}
		return bFinalizedFlat;
	}
	const bool bFinalized = LayoutProfileSolverInternal::EnrichPlannedCellsWithSteppedTerrainIntents(
		InOutPlannedCells,
		InOutRequest,
		OutFailureReason,
		OutFailureKind);
	if (!bFinalized)
	{
		InOutRequest.bHasFinalizedSteppedTerrainIntents = false;
		return false;
	}
	if (!ValidateMinimumFinalizedSteppedShiftClusterSize(
			InOutRequest,
			InOutPlannedCells,
			OutFailureReason)
		|| !ValidateFinalizedSteppedTopologyStaticDomains(
			InOutRequest,
			InOutPlannedCells,
			OutFailureReason))
	{
		// Local project fix: static admission failure is stage-topology infeasibility,
		// allowing the existing authored flat-fallback policy to handle it.
		if (OutFailureKind != nullptr)
		{
			*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible;
		}
		InOutRequest.bHasFinalizedSteppedTerrainIntents = false;
		return false;
	}

	TArray<FLayoutCellReservationRecord> SelectedReservations;
	if (!CompileFinalizedSteppedReservedOpenSpace(
			InOutPlannedCells,
			InOutRequest,
			OutFailureReason,
			OutFailureKind,
			&SelectedReservations))
	{
		InOutRequest.bHasFinalizedSteppedTerrainIntents = false;
		return false;
	}
	if (!InOutRequest.ProfileSnapshot.ReservedOpenSpaceRules.IsEmpty())
	{
		// Keep selected removed-cell authority after stepped intent enrichment returns.
		InOutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations = SelectedReservations;
		if (OutSelectedReservations != nullptr)
		{
			*OutSelectedReservations = MoveTemp(SelectedReservations);
		}
	}
	InOutRequest.bHasFinalizedSteppedTerrainIntents = true;
	return true;
}

bool FLayoutProfileSolver::FinalizeFlatTerrainPlan(
	TArray<FLayoutPlannedCell>& InOutPlannedCells,
	FLayoutRegionSolveRequest& InOutRequest,
	FString& OutFailureReason)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Terrain_FlatPlanFinalize, STAT_PorismLayout_SteppedPlanFinalize);
	InOutRequest.bHasFinalizedSteppedTerrainIntents = false;
	TArray<FLayoutCellReservationRecord> SelectedReservations;
	if (!CompileFinalizedSteppedReservedOpenSpace(
			InOutPlannedCells,
			InOutRequest,
			OutFailureReason,
			nullptr,
			&SelectedReservations))
	{
		return false;
	}
	if (!InOutRequest.ProfileSnapshot.ReservedOpenSpaceRules.IsEmpty())
	{
		InOutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations = SelectedReservations;
	}

	// Flat removals settle first. Shared enrichment then binds Entry origins and
	// exact host alternatives; worker reuse may not fall back to coordinate-only VA.
	InOutRequest.bHasFinalizedSteppedTerrainIntents =
		LayoutProfileSolverInternal::EnrichPlannedCellsWithSteppedTerrainIntents(
			InOutPlannedCells, InOutRequest, OutFailureReason, nullptr);
	return InOutRequest.bHasFinalizedSteppedTerrainIntents;
}

FLayoutRegionSolveResult FLayoutProfileSolver::SolveRegion(const FLayoutRegionSolveRequest& Request)
{
	return LayoutProfileSolverInternal::SolveRequestBackedRegion(
		Request,
		true);
}


FLayoutIndexedDomainSnapshot FLayoutProfileSolver::BuildIndexedDomainSnapshot(const FLayoutRegionSolveRequest& Request)
{
	FLayoutIndexedDomainSnapshot Snapshot;
	Snapshot.Messages.Append(Request.ContentSetSnapshot.Validation.Messages);
	Snapshot.Messages.Append(Request.ModuleCatalog.Validation.Messages);
	Snapshot.Messages.Append(Request.ProfileSnapshot.Validation.Messages);

	if (!Request.ContentSetSnapshot.Validation.IsValid()
		|| !Request.ModuleCatalog.Validation.IsValid()
		|| !Request.ProfileSnapshot.Validation.IsValid())
	{
		Snapshot.FailureReason = TEXT("Indexed domain snapshot aborted because the content set, module solve snapshot, or profile failed validation.");
		return Snapshot;
	}

	if (!Request.PlannedCells.IsEmpty() && (Request.FootprintSize.X <= 0 || Request.FootprintSize.Y <= 0))
	{
		Snapshot.FailureReason = TEXT("Indexed domain snapshot with supplied planned cells requires a positive footprint.");
		return Snapshot;
	}

	FLayoutSolveResult BaseResult;
	BaseResult.Seed = Request.Seed;
	BaseResult.Messages = Snapshot.Messages;

	FSolveContext Context =
		BuildLiveSolveContextFromRequest(Request, BaseResult);

	Snapshot = BuildIndexedDomainSnapshotFromPreparedContext(Context);
	Snapshot.Messages.Append(BaseResult.Messages);
	return Snapshot;
}

FLayoutSolveResult FLayoutProfileSolver::Solve(
	const ULayoutRegionContentSetAsset* ContentSet,
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FLayoutSolverExecutionSettings& ExecutionSettings)
{
	if (IsInGameThread() && !GIsAutomationTesting)
	{
		return BuildFailedStandaloneConvenienceSolveResult(TEXT("Synchronous standalone layout proof is disabled on the game thread. Submit background layout solve work and publish the result on the game thread instead."));
	}

	FLayoutRegionSolveRequest Request;
	FString RequestFailureReason;
	if (!TryBuildStandaloneConvenienceRequest(
		Profile,
		ContentSet,
		Seed,
		TEXT("Standalone"),
		ExecutionSettings,
		Request,
		RequestFailureReason))
	{
		return BuildFailedStandaloneConvenienceSolveResult(RequestFailureReason);
	}
	return SolveRegionTree(Request).MergedSolveResult;
}

FLayoutSolveResult FLayoutProfileSolver::Solve(
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FLayoutSolverExecutionSettings& ExecutionSettings)
{
	if (IsInGameThread() && !GIsAutomationTesting)
	{
		return BuildFailedStandaloneConvenienceSolveResult(TEXT("Synchronous standalone layout proof is disabled on the game thread. Submit background layout solve work and publish the result on the game thread instead."));
	}

	FLayoutRegionSolveRequest Request;
	FString RequestFailureReason;
	if (!TryBuildStandaloneConvenienceRequest(
		Profile,
		nullptr,
		Seed,
		TEXT("Standalone"),
		ExecutionSettings,
		Request,
		RequestFailureReason))
	{
		return BuildFailedStandaloneConvenienceSolveResult(RequestFailureReason);
	}
	return SolveRegionTree(Request).MergedSolveResult;
}

FLayoutSolveResult FLayoutProfileSolver::SolveWithPlannedCells(
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FIntPoint& FootprintSize,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const FLayoutSolverExecutionSettings& ExecutionSettings)
{
	if (IsInGameThread() && !GIsAutomationTesting)
	{
		return BuildFailedStandaloneConvenienceSolveResult(TEXT("Synchronous standalone layout proof is disabled on the game thread. Submit background layout solve work and publish the result on the game thread instead."));
	}

	FLayoutRegionSolveRequest Request;
	FString RequestFailureReason;
	if (!TryBuildStandaloneConvenienceRequest(
		Profile,
		nullptr,
		Seed,
		TEXT("StandalonePlannedCells"),
		ExecutionSettings,
		Request,
		RequestFailureReason))
	{
		return BuildFailedStandaloneConvenienceSolveResult(RequestFailureReason);
	}
	Request.FootprintSize = FootprintSize;
	Request.PlannedCells = PlannedCells;
	return SolveRegion(Request).SolveResult;
}

// -----------------------------------------------------------------------
// Child boundary compatibility helpers used by parent CSP during solve
// and by the freeze function during deferred validation.
// -----------------------------------------------------------------------

bool LayoutProfileSolverInternal::HasCompatibleChildBoundaryModule(
	const FLayoutRegionContentSetSolveSnapshot& ChildContentSet,
	const FLayoutModuleCatalog& ModuleCatalog,
	const ELayoutCellIntent Intent,
	const ELayoutFaceDirection ChildFaceDirection,
	const FLayoutFaceRule& ParentFaceRule,
	FGameplayTag* OutChildConnectionTag)
{
	for (const FLayoutRegionContentEntrySolveSnapshot& Entry : ChildContentSet.Entries)
	{
		if (Entry.ContentKind != ELayoutRegionContentKind::Module) continue;
		if (Entry.ModuleSnapshotIndex == INDEX_NONE || !ModuleCatalog.Modules.IsValidIndex(Entry.ModuleSnapshotIndex)) continue;
		const FLayoutModuleSolveSnapshot& Module = ModuleCatalog.Modules[Entry.ModuleSnapshotIndex];
		const FLayoutFaceRule* ChildFaceRule = nullptr;
		switch (ChildFaceDirection)
		{
		case ELayoutFaceDirection::PosX: ChildFaceRule = &Module.EffectiveFaceRules.PosX; break;
		case ELayoutFaceDirection::NegX: ChildFaceRule = &Module.EffectiveFaceRules.NegX; break;
		case ELayoutFaceDirection::PosY: ChildFaceRule = &Module.EffectiveFaceRules.PosY; break;
		case ELayoutFaceDirection::NegY: ChildFaceRule = &Module.EffectiveFaceRules.NegY; break;
		case ELayoutFaceDirection::PosZ: ChildFaceRule = &Module.EffectiveFaceRules.PosZ; break;
		case ELayoutFaceDirection::NegZ: ChildFaceRule = &Module.EffectiveFaceRules.NegZ; break;
		default: continue;
		}
		if (AreConnectionTagsCompatible(*ChildFaceRule, ParentFaceRule)
			&& IsFaceCompatibleWithOccupancy(*ChildFaceRule, true, true))
		{
			if (OutChildConnectionTag) *OutChildConnectionTag = ChildFaceRule->GetEffectiveConnectionTag();
			return true;
		}
	}
	return false;
}

/** Multi-face collective check: at least one child module must satisfy ALL
 *  parent faces touching this child cell simultaneously.  Used by the parent
 *  CSP to prevent layouts where individual face-pair checks pass but no single
 *  child module can satisfy the full set of boundary faces together.
 *  If OutChildConnectionTagsByDirection is non-null and a compatible module
 *  is found, each parent direction's entry receives the child module's
 *  connection tag for the opposite (child-facing) direction for freeze use. */
bool LayoutProfileSolverInternal::HasCompatibleChildBoundaryModule(
	const FLayoutRegionContentSetSolveSnapshot& ChildContentSet,
	const FLayoutModuleCatalog& ModuleCatalog,
	const FLayoutPlannedCell& ChildPlannedCell,
	const int32 ChildTopModuleLevel,
	const TMap<ELayoutFaceDirection, FLayoutFaceRule>& ParentFaceRulesByDirection,
	FGameplayTag* OutChildConnectionTag,
	const bool bParentFilled)
{
	for (const FLayoutRegionContentEntrySolveSnapshot& Entry : ChildContentSet.Entries)
	{
		if (Entry.ContentKind != ELayoutRegionContentKind::Module) continue;
		if (Entry.ModuleSnapshotIndex == INDEX_NONE || !ModuleCatalog.Modules.IsValidIndex(Entry.ModuleSnapshotIndex)) continue;
		const FLayoutModuleSolveSnapshot& Module = ModuleCatalog.Modules[Entry.ModuleSnapshotIndex];
		if (!DoesChildModuleEntryMatchPlannedCell(
			Entry,
			Module,
			ChildPlannedCell,
			ChildTopModuleLevel))
		{
			continue;
		}

		// Check every allowed yaw rotation: the child CSP evaluates
		// rotated face rules, so the parent must validate against
		// those same world-space face rules.
		for (const int32 YawStep : Module.AllowedYawRotationSteps)
		{
			const FLayoutModuleFaceRules WorldRules =
				BuildWorldFaceRulesForYaw(Module.EffectiveFaceRules, YawStep);

			bool bAllFacesCompatible = true;
			for (const auto& FacePair : ParentFaceRulesByDirection)
			{
				const ELayoutFaceDirection ParentDir = FacePair.Key;
				const FLayoutFaceRule& ParentRule = FacePair.Value;
				const ELayoutFaceDirection ChildDir = FLayoutDirectionUtils::GetOpposite(ParentDir);

				const FLayoutFaceRule* ChildFaceRule = WorldRules.FindRule(ChildDir);
				if (ChildFaceRule == nullptr)
				{
					bAllFacesCompatible = false;
					break;
				}

				if ((bParentFilled && !AreConnectionTagsCompatible(*ChildFaceRule, ParentRule))
					|| !IsFaceCompatibleWithOccupancy(*ChildFaceRule, bParentFilled, bParentFilled))
				{
					bAllFacesCompatible = false;
					break;
				}
			}

			if (bAllFacesCompatible)
			{
				if (OutChildConnectionTag)
				{
					for (const auto& FacePair : ParentFaceRulesByDirection)
					{
						const ELayoutFaceDirection ChildDir = FLayoutDirectionUtils::GetOpposite(FacePair.Key);
						if (const FLayoutFaceRule* Rule = WorldRules.FindRule(ChildDir))
						{
							*OutChildConnectionTag = Rule->GetEffectiveConnectionTag();
							break;
						}
					}
				}
				return true;
			}
		}
	}
	return false;
}

/** Returns the child module's effective face rule for the given direction.
 *  Used by the freeze function after the multi-face collective check has
 *  identified a compatible module. */
const FLayoutFaceRule* LayoutProfileSolverInternal::GetChildBoundaryModuleFaceRuleForDirection(
	const FLayoutRegionContentSetSolveSnapshot& ChildContentSet,
	const FLayoutModuleCatalog& ModuleCatalog,
	const TMap<ELayoutFaceDirection, FLayoutFaceRule>& ParentFaceRulesByDirection,
	const ELayoutFaceDirection ChildFaceDirection)
{
	for (const FLayoutRegionContentEntrySolveSnapshot& Entry : ChildContentSet.Entries)
	{
		if (Entry.ContentKind != ELayoutRegionContentKind::Module) continue;
		if (Entry.ModuleSnapshotIndex == INDEX_NONE || !ModuleCatalog.Modules.IsValidIndex(Entry.ModuleSnapshotIndex)) continue;
		const FLayoutModuleSolveSnapshot& Module = ModuleCatalog.Modules[Entry.ModuleSnapshotIndex];

		bool bAllFacesCompatible = true;
		for (const auto& FacePair : ParentFaceRulesByDirection)
		{
			const ELayoutFaceDirection ParentDir = FacePair.Key;
			const FLayoutFaceRule& ParentRule = FacePair.Value;
			const ELayoutFaceDirection ChildDir = FLayoutDirectionUtils::GetOpposite(ParentDir);

			const FLayoutFaceRule* ChildFaceRule = nullptr;
			switch (ChildDir)
			{
			case ELayoutFaceDirection::PosX: ChildFaceRule = &Module.EffectiveFaceRules.PosX; break;
			case ELayoutFaceDirection::NegX: ChildFaceRule = &Module.EffectiveFaceRules.NegX; break;
			case ELayoutFaceDirection::PosY: ChildFaceRule = &Module.EffectiveFaceRules.PosY; break;
			case ELayoutFaceDirection::NegY: ChildFaceRule = &Module.EffectiveFaceRules.NegY; break;
			case ELayoutFaceDirection::PosZ: ChildFaceRule = &Module.EffectiveFaceRules.PosZ; break;
			case ELayoutFaceDirection::NegZ: ChildFaceRule = &Module.EffectiveFaceRules.NegZ; break;
			default: bAllFacesCompatible = false; break;
			}
			if (!bAllFacesCompatible) break;

			if (!AreConnectionTagsCompatible(*ChildFaceRule, ParentRule)
				|| !IsFaceCompatibleWithOccupancy(*ChildFaceRule, true, true))
			{
				bAllFacesCompatible = false;
				break;
			}
		}

		if (bAllFacesCompatible)
		{
			// Found the collectively-compatible module. Return its face rule.
			switch (ChildFaceDirection)
			{
			case ELayoutFaceDirection::PosX: return &Module.EffectiveFaceRules.PosX;
			case ELayoutFaceDirection::NegX: return &Module.EffectiveFaceRules.NegX;
			case ELayoutFaceDirection::PosY: return &Module.EffectiveFaceRules.PosY;
			case ELayoutFaceDirection::NegY: return &Module.EffectiveFaceRules.NegY;
			case ELayoutFaceDirection::PosZ: return &Module.EffectiveFaceRules.PosZ;
			case ELayoutFaceDirection::NegZ: return &Module.EffectiveFaceRules.NegZ;
			default: return nullptr;
			}
		}
	}
	return nullptr;
}

#if WITH_AUTOMATION_TESTS
void FLayoutProfileSolver::SetCancellationCheckpointGateForTesting(
	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> Gate)
{
	GCancellationCheckpointGateForTesting = MoveTemp(Gate);
}

void FLayoutProfileSolver::SetCancellationCheckpointCounterForTesting(
	TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> Counter)
{
	GCancellationCheckpointCounterForTesting = MoveTemp(Counter);
}

bool FLayoutProfileSolver::RunCancellationCheckpointProbeForTesting(const int32 IterationCount)
{
	for (int32 Iteration = 0; Iteration < IterationCount; ++Iteration)
	{
		WaitOnCancellationCheckpointGateForTesting();
		if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested())
		{
			return false;
		}
	}
	return true;
}

FLayoutSolveResult FLayoutProfileSolver::SolveWithoutIndexedFiltering(
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FLayoutSolverExecutionSettings& ExecutionSettings)
{
	FLayoutSolveResult Result;
	Result.Seed = Seed;

	if (Profile == nullptr)
	{
		Result.FailureReason = TEXT("Layout solve requires a profile.");
		return Result;
	}

	const FLayoutValidationResult ModuleCatalogValidation = FLayoutValidationResult();
	const FLayoutValidationResult ProfileValidation = Profile->ValidateProfile();
	Result.Messages.Append(ModuleCatalogValidation.Messages);
	Result.Messages.Append(ProfileValidation.Messages);

	if (!ModuleCatalogValidation.IsValid() || !ProfileValidation.IsValid())
	{
		Result.FailureReason = TEXT("Layout solve aborted because the profile failed validation.");
		return Result;
	}

	FSolveContext Context;
	ApplySolveSettings(Context, Profile, nullptr, &ExecutionSettings, Seed, Result, false);

	BuildPlan(Context);
	if (!Context.Result.FailureReason.IsEmpty())
	{
		return Context.Result;
	}
	if (Context.ProfileSnapshot.LevelCount > 1)
	{
		return SolveUnifiedVerticalRegions(
			Context.ProfileSnapshot,
			&ExecutionSettings,
			Seed,
			Context.Result,
			Context.FootprintSize,
			Context.Result.PlannedCells,
			false,
			nullptr,
			&Context.IncomingBoundaryPoints,
			&Context.CommittedEndpointAnchors,
			&Context.CommittedTraversalAnchors,
			Context.bUsesChildSolveContext);
	}

	const bool bSolved = SolvePreparedContext(Context);

	if (bSolved)
	{
		Context.Result.FailureReason.Reset();
		Context.Result.ExportedEntryCells.Reset();
		AppendContextPlacementsToResult(Context, Context.Result);
		SortResultPlacements(Context.Result);
	}

	return Context.Result;
}

FLayoutSolveResult FLayoutProfileSolver::SolveWithPlannedCellsWithoutIndexedFiltering(
	const ULayoutProfileAsset* Profile,
	const int32 Seed,
	const FIntPoint& FootprintSize,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const FLayoutSolverExecutionSettings& ExecutionSettings)
{
	FLayoutSolveResult Result;
	Result.Seed = Seed;

	if (Profile == nullptr)
	{
		Result.FailureReason = TEXT("Layout solve requires a profile.");
		return Result;
	}

	if (FootprintSize.X <= 0 || FootprintSize.Y <= 0 || PlannedCells.IsEmpty())
	{
		Result.FailureReason = TEXT("SolveWithPlannedCells requires a positive footprint and at least one planned cell.");
		return Result;
	}

	const FLayoutValidationResult ModuleCatalogValidation = FLayoutValidationResult();
	const FLayoutValidationResult ProfileValidation = Profile->ValidateProfile();
	Result.Messages.Append(ModuleCatalogValidation.Messages);
	Result.Messages.Append(ProfileValidation.Messages);

	if (!ModuleCatalogValidation.IsValid() || !ProfileValidation.IsValid())
	{
		Result.FailureReason = TEXT("Layout solve aborted because the profile failed validation.");
		return Result;
	}

	FSolveContext Context;
	ApplySolveSettings(Context, Profile, nullptr, &ExecutionSettings, Seed, Result, false);

	BuildOverridePlan(Context, FootprintSize, PlannedCells);
	if (Context.ProfileSnapshot.LevelCount > 1)
	{
		return SolveUnifiedVerticalRegions(
			Context.ProfileSnapshot,
			&ExecutionSettings,
			Seed,
			Context.Result,
			FootprintSize,
			PlannedCells,
			false,
			nullptr,
			&Context.IncomingBoundaryPoints,
			&Context.CommittedEndpointAnchors,
			&Context.CommittedTraversalAnchors,
			Context.bUsesChildSolveContext);
	}

	const bool bSolved = SolvePreparedContext(Context);

	if (bSolved)
	{
		Context.Result.FailureReason.Reset();
		Context.Result.ExportedEntryCells.Reset();
		AppendContextPlacementsToResult(Context, Context.Result);
		SortResultPlacements(Context.Result);
	}

	return Context.Result;
}
#endif
