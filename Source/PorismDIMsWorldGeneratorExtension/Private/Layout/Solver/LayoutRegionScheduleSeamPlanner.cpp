// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Types/LayoutGameplayTags.h"

/**
 * Private rewrite home for `FLayoutSeamAndOptionalPlanner`.
 *
 * This file will own seam ownership, narrow junction requirements, and
 * optional-child structural decisions after responsibility negotiation settles.
 */

namespace LayoutRegionScheduleSolverFacade
{
	namespace
	{
#if WITH_AUTOMATION_TESTS
		static FLayoutRegionSolveRequest BuildRootSeamPlanningRequest(
			const FRecursiveScheduleSolveContext& SolveContext,
			const FCompiledStructuralInputs& StructuralInputs)
		{
			FLayoutRegionSolveRequest RootRequest = SolveContext.RootRequest;
			/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
			RootRequest.PlannedCells = StructuralInputs.RootPlannedCells;
			RootRequest.FootprintSize = StructuralInputs.RootFootprintSize;
			return RootRequest;
		}

		static FLayoutChildCapabilityEnvelope BuildRootSeamPlanningEnvelope(
			const FLayoutRegionSolveRequest& RootSeamPlanningRequest)
		{
			FLayoutChildCapabilityEnvelope Envelope;
			if (RootSeamPlanningRequest.bUseSuppliedChildCapabilityEnvelope)
			{
				Envelope = RootSeamPlanningRequest.SuppliedChildCapabilityEnvelope;
			}
			else
			{
				Envelope = BuildChildCapabilityEnvelopeFromRequest(RootSeamPlanningRequest);
			}

			Envelope.RegionDebugPath = RootSeamPlanningRequest.RegionDebugPath;
			return Envelope;
		}
#endif

		static bool HasEquivalentSeamRecord(
			const TArray<FLayoutPartitionSeamRecord>& ExistingSeams,
			const FLayoutPartitionSeamRecord& CandidateSeam)
		{
			return ExistingSeams.ContainsByPredicate(
				[&CandidateSeam](const FLayoutPartitionSeamRecord& ExistingSeam)
				{
					return ExistingSeam.ParentRegionDebugPath == CandidateSeam.ParentRegionDebugPath
						&& ExistingSeam.OwnerRegionDebugPath == CandidateSeam.OwnerRegionDebugPath
						&& ExistingSeam.PassiveRegionDebugPath == CandidateSeam.PassiveRegionDebugPath
						&& ExistingSeam.InterfaceFamily == CandidateSeam.InterfaceFamily
						&& ExistingSeam.OwnerStartCell == CandidateSeam.OwnerStartCell
						&& ExistingSeam.OwnerEndCell == CandidateSeam.OwnerEndCell
						&& ExistingSeam.PassiveStartCell == CandidateSeam.PassiveStartCell
						&& ExistingSeam.PassiveEndCell == CandidateSeam.PassiveEndCell
						&& ExistingSeam.SegmentCount == CandidateSeam.SegmentCount;
				});
		}

		static bool DoesExistingSeamCoverCellPair(
			const TArray<FLayoutPartitionSeamRecord>& ExistingSeams,
			const FString& ParentPath,
			const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment)
		{
			return ExistingSeams.ContainsByPredicate(
				[&](const FLayoutPartitionSeamRecord& ExistingSeam)
				{
					if (ExistingSeam.ParentRegionDebugPath != ParentPath)
					{
						return false;
					}

					const bool bMatchesForwardRegions =
						ExistingSeam.OwnerRegionDebugPath == Segment.RegionAPath
						&& ExistingSeam.PassiveRegionDebugPath == Segment.RegionBPath;
					const bool bMatchesReverseRegions =
						ExistingSeam.OwnerRegionDebugPath == Segment.RegionBPath
						&& ExistingSeam.PassiveRegionDebugPath == Segment.RegionAPath;
					if (!bMatchesForwardRegions && !bMatchesReverseRegions)
					{
						return false;
					}

					const FIntVector OwnerStep(
						FMath::Clamp(ExistingSeam.OwnerEndCell.X - ExistingSeam.OwnerStartCell.X, -1, 1),
						FMath::Clamp(ExistingSeam.OwnerEndCell.Y - ExistingSeam.OwnerStartCell.Y, -1, 1),
						FMath::Clamp(ExistingSeam.OwnerEndCell.Z - ExistingSeam.OwnerStartCell.Z, -1, 1));
					const FIntVector PassiveStep(
						FMath::Clamp(ExistingSeam.PassiveEndCell.X - ExistingSeam.PassiveStartCell.X, -1, 1),
						FMath::Clamp(ExistingSeam.PassiveEndCell.Y - ExistingSeam.PassiveStartCell.Y, -1, 1),
						FMath::Clamp(ExistingSeam.PassiveEndCell.Z - ExistingSeam.PassiveStartCell.Z, -1, 1));
					for (int32 SegmentIndex = 0; SegmentIndex < ExistingSeam.SegmentCount; ++SegmentIndex)
					{
						const FIntVector OwnerCell = ExistingSeam.OwnerStartCell + OwnerStep * SegmentIndex;
						const FIntVector PassiveCell = ExistingSeam.PassiveStartCell + PassiveStep * SegmentIndex;
						if (bMatchesForwardRegions
							&& OwnerCell == Segment.RegionACell
							&& PassiveCell == Segment.RegionBCell)
						{
							return true;
						}
						if (bMatchesReverseRegions
							&& OwnerCell == Segment.RegionBCell
							&& PassiveCell == Segment.RegionACell)
						{
							return true;
						}
					}

					return false;
				});
		}

		static bool IsExactOverlapSeamSegment(
			const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment)
		{
			return Segment.RegionACell == Segment.RegionBCell;
		}

		/**
		 * Mixed-height sibling overlap can currently produce both the exact
		 * overlapping seam cells and one-cell inward-adjacent duplicates from the
		 * same pair. Keep the exact overlap segments and drop the duplicate
		 * adjacent segments so one pair contributes one structural run per shared
		 * face instead of parallel copies.
		 */
		static void NormalizeMixedHeightSiblingOverlapSegments(
			const bool bIsParentChildPair,
			TArray<LayoutProfileSolverInternal::FCompiledSeamSegment>& InOutSegments)
		{
			if (bIsParentChildPair || InOutSegments.IsEmpty())
			{
				return;
			}

			const bool bHasExactOverlapSegments =
				InOutSegments.ContainsByPredicate(
					[](const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment)
					{
						return IsExactOverlapSeamSegment(Segment);
					});
			if (!bHasExactOverlapSegments)
			{
				return;
			}

			InOutSegments.RemoveAll(
				[](const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment)
				{
					return !IsExactOverlapSeamSegment(Segment);
				});
		}

		/**
		 * Shared pair policy flow. The in-place seam helper still owns the
		 * deterministic leaf stages: segment discovery, run grouping, seam
		 * contract arbitration, and seam-record reconstruction.
		 */
		static bool AppendSchedulePartitionSeamsForPairFromLeafStagesInternal(
			const FString& ParentPath,
			const FString& LeftPath,
			const FString& RightPath,
			const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
			const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
			TArray<FLayoutPartitionSeamRecord>& InOutSeams,
			TSet<FString>& OutPassiveRegionPaths,
			const bool bPlannedSeamsAreAuthoritative,
			FString& OutFailureReason)
		{
			const FLayoutRegionSolveRequest* const* LeftRequestPtr = RequestsByPath.Find(LeftPath);
			const FLayoutRegionSolveRequest* const* RightRequestPtr = RequestsByPath.Find(RightPath);
			if (LeftRequestPtr == nullptr || RightRequestPtr == nullptr || *LeftRequestPtr == nullptr || *RightRequestPtr == nullptr)
			{
				return true;
			}

			TArray<LayoutProfileSolverInternal::FScheduleSeamPlanningCell> LeftPlannedCells;
			TArray<LayoutProfileSolverInternal::FScheduleSeamPlanningCell> RightPlannedCells;
			if (!LayoutProfileSolverInternal::TryGetSchedulePlannedCellsForSeamPlanning(**LeftRequestPtr, LeftPlannedCells)
				|| !LayoutProfileSolverInternal::TryGetSchedulePlannedCellsForSeamPlanning(**RightRequestPtr, RightPlannedCells))
			{
				return true;
			}

			TArray<LayoutProfileSolverInternal::FCompiledSeamSegment> Segments;
			const bool bIsParentChildPair = LeftPath == ParentPath || RightPath == ParentPath;
			LayoutProfileSolverInternal::CollectSharedSeamSegmentsForPair(
				LeftPath,
				LeftPlannedCells,
				RightPath,
				RightPlannedCells,
				!bIsParentChildPair,
				true,
				bIsParentChildPair,
				Segments);
			NormalizeMixedHeightSiblingOverlapSegments(
				bIsParentChildPair,
				Segments);
			if (Segments.IsEmpty())
			{
				return true;
			}

			if (bPlannedSeamsAreAuthoritative)
			{
				for (int32 SegmentIndex = Segments.Num() - 1; SegmentIndex >= 0; --SegmentIndex)
				{
					const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment = Segments[SegmentIndex];
					if (!DoesExistingSeamCoverCellPair(InOutSeams, ParentPath, Segment))
					{
						OutFailureReason = FString::Printf(
							TEXT("Authoritative recursive seam planning requires explicit committed seam coverage for segment between world cells %s and %s across '%s' and '%s' under parent '%s'."),
							*Segment.RegionACell.ToString(),
							*Segment.RegionBCell.ToString(),
							*Segment.RegionAPath,
							*Segment.RegionBPath,
							*ParentPath);
						return false;
					}
				}

				return true;
			}

			TArray<TArray<LayoutProfileSolverInternal::FCompiledSeamSegment>> Runs;
			LayoutProfileSolverInternal::BuildContiguousSeamRuns(Segments, Runs);
			for (const TArray<LayoutProfileSolverInternal::FCompiledSeamSegment>& RunSegments : Runs)
			{
				LayoutProfileSolverInternal::FChosenSeamContract ChosenContract;
				if (!LayoutProfileSolverInternal::TryChooseSeamContractForRun(
						RunSegments,
						ParentPath,
						CapabilityEnvelopeByRegion,
						ChosenContract))
				{
					const LayoutProfileSolverInternal::FCompiledSeamSegment& FirstSegment = RunSegments[0];
					const FLayoutChildCapabilityEnvelope* LeftEnvelope = CapabilityEnvelopeByRegion.Find(FirstSegment.RegionAPath);
					const FLayoutChildCapabilityEnvelope* RightEnvelope = CapabilityEnvelopeByRegion.Find(FirstSegment.RegionBPath);
					const bool bLeftAuthoredSeamIntent =
						LeftEnvelope != nullptr
						&& LayoutProfileSolverInternal::HasAnyChildSeamCapabilityForFace(
							*LeftEnvelope,
							FirstSegment.RegionAFaceDirection);
					const bool bRightAuthoredSeamIntent =
						RightEnvelope != nullptr
						&& LayoutProfileSolverInternal::HasAnyChildSeamCapabilityForFace(
							*RightEnvelope,
							FirstSegment.RegionBFaceDirection);
					if (!bLeftAuthoredSeamIntent && !bRightAuthoredSeamIntent)
					{
						continue;
					}

					bool bRecoveredAsPerSegmentContracts = false;
					for (const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment : RunSegments)
					{
						LayoutProfileSolverInternal::FChosenSeamContract SegmentContract;
						const TArray<LayoutProfileSolverInternal::FCompiledSeamSegment> SingleSegmentRun{Segment};
						if (!LayoutProfileSolverInternal::TryChooseSeamContractForRun(
								SingleSegmentRun,
								ParentPath,
								CapabilityEnvelopeByRegion,
								SegmentContract))
						{
							continue;
						}

						const FLayoutPartitionSeamRecord SegmentSeamRecord =
							LayoutProfileSolverInternal::BuildPartitionSeamRecordFromRun(
								ParentPath,
								SingleSegmentRun,
								SegmentContract);
						if (HasEquivalentSeamRecord(InOutSeams, SegmentSeamRecord))
						{
							continue;
						}

						InOutSeams.Add(SegmentSeamRecord);
						OutPassiveRegionPaths.Add(SegmentSeamRecord.PassiveRegionDebugPath);
						bRecoveredAsPerSegmentContracts = true;
					}
					if (bRecoveredAsPerSegmentContracts)
					{
						continue;
					}

					// One-sided face intent can describe a seam a region is willing
					// to own or accept without forcing every incidental multi-region
					// corner contact on that face to become a hard schedule failure.
					if (bLeftAuthoredSeamIntent != bRightAuthoredSeamIntent)
					{
						continue;
					}

					OutFailureReason = FString::Printf(
						TEXT("Could not find a compatible seam-owning/accepting capability pair for shared span between '%s' and '%s' under parent '%s'."),
						*LeftPath,
						*RightPath,
						*ParentPath);
					return false;
				}

				const FLayoutPartitionSeamRecord SeamRecord =
					LayoutProfileSolverInternal::BuildPartitionSeamRecordFromRun(
						ParentPath,
						RunSegments,
						ChosenContract);
				if (HasEquivalentSeamRecord(InOutSeams, SeamRecord))
				{
					continue;
				}

				InOutSeams.Add(SeamRecord);
				OutPassiveRegionPaths.Add(SeamRecord.PassiveRegionDebugPath);
			}

			for (const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment : Segments)
			{
				if (!LayoutProfileSolverInternal::DoesSeamSegmentRequireDoorInterface(Segment))
				{
					continue;
				}

				const TArray<LayoutProfileSolverInternal::FCompiledSeamSegment> SingleSegmentRun{Segment};
				LayoutProfileSolverInternal::FChosenSeamContract SegmentContract;
				if (!LayoutProfileSolverInternal::TryChooseSeamContractForRun(
						SingleSegmentRun,
						ParentPath,
						CapabilityEnvelopeByRegion,
						SegmentContract))
				{
					continue;
				}

				const FLayoutPartitionSeamRecord CandidateDoorSeam =
					LayoutProfileSolverInternal::BuildPartitionSeamRecordFromRun(
						ParentPath,
						SingleSegmentRun,
						SegmentContract);
				if (HasEquivalentSeamRecord(InOutSeams, CandidateDoorSeam))
				{
					continue;
				}

				InOutSeams.Add(CandidateDoorSeam);
				OutPassiveRegionPaths.Add(CandidateDoorSeam.PassiveRegionDebugPath);
			}

			return true;
		}

		static int32 ResolveFaceAxis(const ELayoutFaceDirection FaceDirection)
		{
			switch (FaceDirection)
			{
			case ELayoutFaceDirection::PosX:
			case ELayoutFaceDirection::NegX:
				return 0;
			case ELayoutFaceDirection::PosY:
			case ELayoutFaceDirection::NegY:
				return 1;
			case ELayoutFaceDirection::PosZ:
			case ELayoutFaceDirection::NegZ:
				return 2;
			default:
				return INDEX_NONE;
			}
		}

		static bool IsSupportedOwnerJunctionSeam(
			const FLayoutPartitionSeamRecord& SeamRecord)
		{
			const bool bHorizontalOwnerFace =
				SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosX
				|| SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::NegX
				|| SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
				|| SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::NegY;
			const bool bHorizontalPassiveFace =
				SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::PosX
				|| SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegX
				|| SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::PosY
				|| SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY;
			return bHorizontalOwnerFace && bHorizontalPassiveFace;
		}

		static void EnumerateOwnerCells(
			const FLayoutPartitionSeamRecord& SeamRecord,
			TArray<FIntVector>& OutOwnerCells)
		{
			OutOwnerCells.Reset();
			if (SeamRecord.SegmentCount <= 0)
			{
				return;
			}

			const FIntVector OwnerStep(
				FMath::Clamp(SeamRecord.OwnerEndCell.X - SeamRecord.OwnerStartCell.X, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Y - SeamRecord.OwnerStartCell.Y, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Z - SeamRecord.OwnerStartCell.Z, -1, 1));
			OutOwnerCells.Reserve(SeamRecord.SegmentCount);
			for (int32 SegmentIndex = 0; SegmentIndex < SeamRecord.SegmentCount; ++SegmentIndex)
			{
				OutOwnerCells.Add(SeamRecord.OwnerStartCell + OwnerStep * SegmentIndex);
			}
		}

		static FLayoutId BuildJunctionAdjacencyClassId(
			const bool bOwnerIsParentRegion,
			const bool bSharedCornerTopology,
			const ELayoutFaceDirection ContinuingOwnerFaceDirection,
			const ELayoutFaceDirection BranchOwnerFaceDirection)
		{
			const TCHAR* TopologyName = TEXT("SiblingSharedCorner");
			if (!bSharedCornerTopology)
			{
				TopologyName = bOwnerIsParentRegion
					? TEXT("ParentChildEdgeAttach")
					: TEXT("SiblingEdgeAttach");
			}
			else if (bOwnerIsParentRegion)
			{
				TopologyName = TEXT("ParentChildSharedCorner");
			}

			return FLayoutId(*FString::Printf(
				TEXT("%s.%d.%d"),
				TopologyName,
				static_cast<int32>(ContinuingOwnerFaceDirection),
				static_cast<int32>(BranchOwnerFaceDirection)));
		}

		static FLayoutId BuildJunctionRequirementId(
			const FLayoutPartitionSeamRecord& ContinuingSeam,
			const FLayoutPartitionSeamRecord& BranchSeam,
			const FIntVector& JunctionCell)
		{
				return FLayoutId(*FString::Printf(
					TEXT("Junction.%s.%s.%s.%d.%d.%d"),
					*ContinuingSeam.ParentRegionDebugPath,
					*ContinuingSeam.OwnerRegionDebugPath,
					*BranchSeam.PassiveRegionDebugPath,
					JunctionCell.X,
					JunctionCell.Y,
					JunctionCell.Z));
		}

		struct FNormalizedOwnerJunctionSeam
		{
			FLayoutPartitionSeamRecord RepresentativeSeam;
			TArray<FIntVector> OwnerCells;
			int32 FaceAxis = INDEX_NONE;
			int32 RunAxis = INDEX_NONE;
		};

		struct FOwnerJunctionSeamFragment
		{
			FLayoutPartitionSeamRecord SeamRecord;
			TArray<FIntVector> OwnerCells;
			int32 RunAxis = INDEX_NONE;
		};

		struct FGroupedOwnerJunctionSeamInput
		{
			int32 FaceAxis = INDEX_NONE;
			TArray<FOwnerJunctionSeamFragment> Fragments;
		};

		static FString BuildNormalizedOwnerJunctionLineKey(
			const FIntVector& OwnerCell,
			const int32 RunAxis)
		{
			switch (RunAxis)
			{
			case 0:
				return FString::Printf(TEXT("%d|%d"), OwnerCell.Y, OwnerCell.Z);
			case 1:
				return FString::Printf(TEXT("%d|%d"), OwnerCell.X, OwnerCell.Z);
			case 2:
				return FString::Printf(TEXT("%d|%d"), OwnerCell.X, OwnerCell.Y);
			default:
				return FString::Printf(TEXT("%d|%d|%d"), OwnerCell.X, OwnerCell.Y, OwnerCell.Z);
			}
		}

		static int32 CoordinateForRunAxis(
			const FIntVector& OwnerCell,
			const int32 RunAxis)
		{
			switch (RunAxis)
			{
			case 0:
				return OwnerCell.X;
			case 1:
				return OwnerCell.Y;
			case 2:
				return OwnerCell.Z;
			default:
				return 0;
			}
		}

		static int32 ResolveNormalizedSeamRunAxis(
			const TArray<FIntVector>& OwnerCells);

		static int32 ResolveGroupedOwnerJunctionRunAxis(
			const FGroupedOwnerJunctionSeamInput& GroupedInput)
		{
			for (const FOwnerJunctionSeamFragment& Fragment : GroupedInput.Fragments)
			{
				if (Fragment.RunAxis != INDEX_NONE)
				{
					return Fragment.RunAxis;
				}
			}

			TArray<FIntVector> AllOwnerCells;
			for (const FOwnerJunctionSeamFragment& Fragment : GroupedInput.Fragments)
			{
				for (const FIntVector& OwnerCell : Fragment.OwnerCells)
				{
					AllOwnerCells.AddUnique(OwnerCell);
				}
			}

			return ResolveNormalizedSeamRunAxis(AllOwnerCells);
		}

		static bool IsOwnerCellAtNormalizedSeamEndpoint(
			const FNormalizedOwnerJunctionSeam& Seam,
			const FIntVector& OwnerCell)
		{
			if (Seam.OwnerCells.IsEmpty())
			{
				return false;
			}

				const auto CoordinateForAxis =
					[Axis = Seam.RunAxis](const FIntVector& Cell) -> int32
					{
						switch (Axis)
						{
						case 0:
							return Cell.X;
						case 1:
							return Cell.Y;
					case 2:
						return Cell.Z;
					default:
						return 0;
					}
				};

			const int32 CandidateCoordinate = CoordinateForAxis(OwnerCell);
			int32 MinimumCoordinate = CoordinateForAxis(Seam.OwnerCells[0]);
			int32 MaximumCoordinate = MinimumCoordinate;
			for (int32 CellIndex = 1; CellIndex < Seam.OwnerCells.Num(); ++CellIndex)
			{
				const int32 Coordinate = CoordinateForAxis(Seam.OwnerCells[CellIndex]);
				MinimumCoordinate = FMath::Min(MinimumCoordinate, Coordinate);
				MaximumCoordinate = FMath::Max(MaximumCoordinate, Coordinate);
			}

			return CandidateCoordinate == MinimumCoordinate
				|| CandidateCoordinate == MaximumCoordinate;
		}

		static int32 GetNormalizedSeamSegmentCount(
			const FNormalizedOwnerJunctionSeam& Seam)
		{
			return Seam.OwnerCells.Num();
		}

		static int32 ResolveNormalizedSeamRunAxis(
			const TArray<FIntVector>& OwnerCells)
		{
			if (OwnerCells.Num() < 2)
			{
				return INDEX_NONE;
			}

			const FIntVector& FirstCell = OwnerCells[0];
			for (int32 CellIndex = 1; CellIndex < OwnerCells.Num(); ++CellIndex)
			{
				const FIntVector& OtherCell = OwnerCells[CellIndex];
				if (OtherCell.X != FirstCell.X)
				{
					return 0;
				}
				if (OtherCell.Y != FirstCell.Y)
				{
					return 1;
				}
				if (OtherCell.Z != FirstCell.Z)
				{
					return 2;
				}
			}

			return INDEX_NONE;
		}

		/**
		 * Collapses fragmented same-owner seam records into one deterministic
		 * candidate per passive/interface/face combination so replayed merged seam
		 * output can feed the same narrow junction planner as the synthetic tests.
		 */
		static TArray<FNormalizedOwnerJunctionSeam> BuildNormalizedOwnerJunctionSeams(
			const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams)
		{
			TMap<FString, FGroupedOwnerJunctionSeamInput> GroupedSeamsByKey;
			for (const FLayoutPartitionSeamRecord& SeamRecord : PlannedPartitionSeams)
			{
				if (!IsSupportedOwnerJunctionSeam(SeamRecord))
				{
					continue;
				}

				const int32 Axis = ResolveFaceAxis(SeamRecord.OwnerFaceDirection);
				if (Axis == INDEX_NONE)
				{
					continue;
				}

				const FString SeamKey = FString::Printf(
					TEXT("%s|%s|%s|%s|%d|%d"),
					*SeamRecord.ParentRegionDebugPath,
					*SeamRecord.OwnerRegionDebugPath,
					*SeamRecord.PassiveRegionDebugPath,
					*SeamRecord.InterfaceFamily.ToString(),
					static_cast<int32>(SeamRecord.OwnerFaceDirection),
					static_cast<int32>(SeamRecord.PassiveFaceDirection));
				TArray<FIntVector> OwnerCells;
				EnumerateOwnerCells(SeamRecord, OwnerCells);
				if (OwnerCells.IsEmpty())
				{
					continue;
				}

				FGroupedOwnerJunctionSeamInput& GroupedInput =
					GroupedSeamsByKey.FindOrAdd(SeamKey);
				GroupedInput.FaceAxis = Axis;

				FOwnerJunctionSeamFragment& Fragment =
					GroupedInput.Fragments.AddDefaulted_GetRef();
				Fragment.SeamRecord = SeamRecord;
				Fragment.OwnerCells = MoveTemp(OwnerCells);
				Fragment.RunAxis = ResolveNormalizedSeamRunAxis(Fragment.OwnerCells);
			}

			TArray<FNormalizedOwnerJunctionSeam> NormalizedSeams;
			for (TPair<FString, FGroupedOwnerJunctionSeamInput>& Pair : GroupedSeamsByKey)
			{
				FGroupedOwnerJunctionSeamInput& GroupedInput = Pair.Value;
				const int32 RunAxis = ResolveGroupedOwnerJunctionRunAxis(GroupedInput);
				TMap<FString, TArray<FIntVector>> CellsByLineKey;
				TMap<FString, TArray<const FOwnerJunctionSeamFragment*>> FragmentsByLineKey;

				for (const FOwnerJunctionSeamFragment& Fragment : GroupedInput.Fragments)
				{
					for (const FIntVector& OwnerCell : Fragment.OwnerCells)
					{
						const FString LineKey =
							BuildNormalizedOwnerJunctionLineKey(OwnerCell, RunAxis);
						CellsByLineKey.FindOrAdd(LineKey).AddUnique(OwnerCell);
						FragmentsByLineKey.FindOrAdd(LineKey).AddUnique(&Fragment);
					}
				}

				for (TPair<FString, TArray<FIntVector>>& LinePair : CellsByLineKey)
				{
					TArray<FIntVector>& LineCells = LinePair.Value;
					LineCells.Sort(
						[RunAxis](const FIntVector& Left, const FIntVector& Right)
						{
							const int32 LeftCoordinate = CoordinateForRunAxis(Left, RunAxis);
							const int32 RightCoordinate = CoordinateForRunAxis(Right, RunAxis);
							if (LeftCoordinate != RightCoordinate)
							{
								return LeftCoordinate < RightCoordinate;
							}
							if (Left.X != Right.X)
							{
								return Left.X < Right.X;
							}
							if (Left.Y != Right.Y)
							{
								return Left.Y < Right.Y;
							}
							return Left.Z < Right.Z;
						});

					TArray<FIntVector> CurrentCluster;
					const auto FlushCluster =
						[&]()
						{
							if (CurrentCluster.IsEmpty())
							{
								return;
							}

							FNormalizedOwnerJunctionSeam& NormalizedSeam =
								NormalizedSeams.AddDefaulted_GetRef();
							NormalizedSeam.FaceAxis = GroupedInput.FaceAxis;
							NormalizedSeam.RunAxis = RunAxis;
							NormalizedSeam.OwnerCells = CurrentCluster;

							const TArray<const FOwnerJunctionSeamFragment*>* const FragmentsForLine =
								FragmentsByLineKey.Find(LinePair.Key);
							if (FragmentsForLine != nullptr)
							{
								for (const FOwnerJunctionSeamFragment* Fragment : *FragmentsForLine)
								{
									const bool bTouchesCluster =
										Fragment != nullptr
										&& Fragment->OwnerCells.ContainsByPredicate(
											[&CurrentCluster](const FIntVector& OwnerCell)
											{
												return CurrentCluster.Contains(OwnerCell);
											});
									if (!bTouchesCluster)
									{
										continue;
									}

									if (NormalizedSeam.RepresentativeSeam.SeamId.IsNone()
										|| Fragment->SeamRecord.SegmentCount > NormalizedSeam.RepresentativeSeam.SegmentCount
										|| (Fragment->SeamRecord.SegmentCount == NormalizedSeam.RepresentativeSeam.SegmentCount
											&& Fragment->SeamRecord.SeamId.LexicalLess(NormalizedSeam.RepresentativeSeam.SeamId)))
									{
										NormalizedSeam.RepresentativeSeam = Fragment->SeamRecord;
									}
								}
							}

							CurrentCluster.Reset();
						};

					for (const FIntVector& OwnerCell : LineCells)
					{
						if (!CurrentCluster.IsEmpty())
						{
							const int32 PreviousCoordinate =
								CoordinateForRunAxis(CurrentCluster.Last(), RunAxis);
							const int32 CandidateCoordinate =
								CoordinateForRunAxis(OwnerCell, RunAxis);
							if (RunAxis == INDEX_NONE
								|| CandidateCoordinate != PreviousCoordinate + 1)
							{
								FlushCluster();
							}
						}

						CurrentCluster.Add(OwnerCell);
					}

					FlushCluster();
				}
			}

			return NormalizedSeams;
		}

		static TArray<FOwnedSeamJunctionRequirement> BuildOwnerSideJunctionRequirementsInternal(
			const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
			const FLayoutRegionSolveRequest* ParentRequest = nullptr)
		{
			struct FOwnerJunctionCandidateGroup
			{
				FString ParentRegionDebugPath;
				FString OwnerRegionDebugPath;
				FIntVector JunctionCell = FIntVector::ZeroValue;
				TArray<int32> NormalizedSeamIndices;
			};

			const TArray<FNormalizedOwnerJunctionSeam> NormalizedSeams =
				BuildNormalizedOwnerJunctionSeams(PlannedPartitionSeams);
			TMap<FString, FOwnerJunctionCandidateGroup> CandidateGroups;
			for (int32 SeamIndex = 0; SeamIndex < NormalizedSeams.Num(); ++SeamIndex)
			{
				const FNormalizedOwnerJunctionSeam& SeamRecord =
					NormalizedSeams[SeamIndex];
				for (const FIntVector& OwnerCell : SeamRecord.OwnerCells)
				{
					const FString GroupKey = FString::Printf(
						TEXT("%s|%s|%d|%d|%d"),
						*SeamRecord.RepresentativeSeam.ParentRegionDebugPath,
						*SeamRecord.RepresentativeSeam.OwnerRegionDebugPath,
						OwnerCell.X,
						OwnerCell.Y,
						OwnerCell.Z);
					FOwnerJunctionCandidateGroup& Group =
						CandidateGroups.FindOrAdd(GroupKey);
					Group.ParentRegionDebugPath =
						SeamRecord.RepresentativeSeam.ParentRegionDebugPath;
					Group.OwnerRegionDebugPath =
						SeamRecord.RepresentativeSeam.OwnerRegionDebugPath;
					Group.JunctionCell = OwnerCell;
					Group.NormalizedSeamIndices.AddUnique(SeamIndex);
				}
			}

			TArray<FOwnedSeamJunctionRequirement> JunctionRequirements;
			TSet<FLayoutId> EmittedRequirementIds;
			for (const TPair<FString, FOwnerJunctionCandidateGroup>& Pair : CandidateGroups)
			{
				const FOwnerJunctionCandidateGroup& Group = Pair.Value;
				if (Group.NormalizedSeamIndices.Num() < 2)
				{
					continue;
				}

				for (int32 LeftIndex = 0; LeftIndex < Group.NormalizedSeamIndices.Num(); ++LeftIndex)
				{
					const FNormalizedOwnerJunctionSeam& LeftSeam =
						NormalizedSeams[Group.NormalizedSeamIndices[LeftIndex]];
					for (int32 RightIndex = LeftIndex + 1; RightIndex < Group.NormalizedSeamIndices.Num(); ++RightIndex)
						{
							const FNormalizedOwnerJunctionSeam& RightSeam =
								NormalizedSeams[Group.NormalizedSeamIndices[RightIndex]];
							// Two runs against one child are an ordinary shared corner unless
							// the parent perimeter continues through this cell on both sides.
							const FIntVector& Cell = Group.JunctionCell;
							const bool bParentPerimeterContinues = ParentRequest != nullptr
								&& Group.ParentRegionDebugPath == ParentRequest->RegionDebugPath
								&& HasParentPerimeterContinuation(*ParentRequest, Cell);
							if (LeftSeam.RepresentativeSeam.ParentRegionDebugPath != RightSeam.RepresentativeSeam.ParentRegionDebugPath
								|| LeftSeam.RepresentativeSeam.OwnerRegionDebugPath != RightSeam.RepresentativeSeam.OwnerRegionDebugPath
								|| (LeftSeam.RepresentativeSeam.PassiveRegionDebugPath == RightSeam.RepresentativeSeam.PassiveRegionDebugPath
									&& !bParentPerimeterContinues
									&& GetNormalizedSeamSegmentCount(LeftSeam) >= 2
									&& GetNormalizedSeamSegmentCount(RightSeam) >= 2)
								|| !LeftSeam.RepresentativeSeam.InterfaceFamily.MatchesTagExact(RightSeam.RepresentativeSeam.InterfaceFamily)
								|| LeftSeam.FaceAxis == RightSeam.FaceAxis)
							{
								continue;
							}

						const bool bLeftCellIsEndpoint =
							IsOwnerCellAtNormalizedSeamEndpoint(LeftSeam, Group.JunctionCell);
						const bool bRightCellIsEndpoint =
							IsOwnerCellAtNormalizedSeamEndpoint(RightSeam, Group.JunctionCell);
						if (!bLeftCellIsEndpoint && !bRightCellIsEndpoint)
						{
							continue;
						}

						const FNormalizedOwnerJunctionSeam* ContinuingSeam = &LeftSeam;
						const FNormalizedOwnerJunctionSeam* BranchSeam = &RightSeam;
						const bool bLeftCanContinue = !bLeftCellIsEndpoint;
						const bool bRightCanContinue = !bRightCellIsEndpoint;
						const bool bSharedCornerTopology =
							bLeftCellIsEndpoint && bRightCellIsEndpoint;
						if (bLeftCanContinue != bRightCanContinue)
						{
							ContinuingSeam = bLeftCanContinue ? &LeftSeam : &RightSeam;
							BranchSeam = bLeftCanContinue ? &RightSeam : &LeftSeam;
						}
						else if (GetNormalizedSeamSegmentCount(RightSeam) > GetNormalizedSeamSegmentCount(LeftSeam)
							|| (GetNormalizedSeamSegmentCount(RightSeam) == GetNormalizedSeamSegmentCount(LeftSeam)
								&& RightSeam.RepresentativeSeam.SeamId.LexicalLess(LeftSeam.RepresentativeSeam.SeamId)))
						{
							ContinuingSeam = &RightSeam;
							BranchSeam = &LeftSeam;
						}

						const bool bOwnerIsParentRegion =
							ContinuingSeam->RepresentativeSeam.OwnerRegionDebugPath
								== ContinuingSeam->RepresentativeSeam.ParentRegionDebugPath;

						if (GetNormalizedSeamSegmentCount(*ContinuingSeam) < 2
							|| GetNormalizedSeamSegmentCount(*BranchSeam) < 1)
						{
							continue;
						}

						const FLayoutId RequirementId =
							BuildJunctionRequirementId(
								ContinuingSeam->RepresentativeSeam,
								BranchSeam->RepresentativeSeam,
								Group.JunctionCell);
						if (EmittedRequirementIds.Contains(RequirementId))
						{
							continue;
						}

						FOwnedSeamJunctionRequirement& Requirement =
							JunctionRequirements.AddDefaulted_GetRef();
						Requirement.JunctionRequirementId = RequirementId;
						Requirement.OwnerRegionDebugPath =
							ContinuingSeam->RepresentativeSeam.OwnerRegionDebugPath;
						Requirement.PassiveRegionDebugPath =
							BranchSeam->RepresentativeSeam.PassiveRegionDebugPath;
						Requirement.AdjacencyClassId =
							BuildJunctionAdjacencyClassId(
								bOwnerIsParentRegion,
								bSharedCornerTopology,
								ContinuingSeam->RepresentativeSeam.OwnerFaceDirection,
								BranchSeam->RepresentativeSeam.OwnerFaceDirection);
						Requirement.JunctionCell = Group.JunctionCell;
						Requirement.ContinuingSeamId =
							ContinuingSeam->RepresentativeSeam.SeamId;
						Requirement.BranchSeamId =
							BranchSeam->RepresentativeSeam.SeamId;
						Requirement.ContinuingPassiveRegionDebugPath =
							ContinuingSeam->RepresentativeSeam.PassiveRegionDebugPath;
						Requirement.InterfaceFamily =
							ContinuingSeam->RepresentativeSeam.InterfaceFamily;
						Requirement.ContinuingOwnerFaceDirection =
							ContinuingSeam->RepresentativeSeam.OwnerFaceDirection;
						Requirement.BranchOwnerFaceDirection =
							BranchSeam->RepresentativeSeam.OwnerFaceDirection;
						EmittedRequirementIds.Add(RequirementId);
					}
				}
			}

			// Junction exists where passive perimeter leaves endpoint of reciprocal
			// shared seam while owner keeps seam authority against another region.
			struct FPassivePerimeterSplitRun
			{
				FString ParentRegionDebugPath;
				FString OwnerRegionDebugPath;
				FString PassiveRegionDebugPath;
				ELayoutFaceDirection OwnerFaceDirection = ELayoutFaceDirection::PosX;
				int32 RunAxis = INDEX_NONE;
				TArray<FIntVector> Cells;
				TMap<FIntVector, int32> RepresentativeSeamIndexByCell;
			};
			TMap<FString, FPassivePerimeterSplitRun> SplitRunsByKey;
			for (int32 SeamIndex = 0; SeamIndex < PlannedPartitionSeams.Num(); ++SeamIndex)
			{
				const FLayoutPartitionSeamRecord& Seam = PlannedPartitionSeams[SeamIndex];
				if (!IsSupportedOwnerJunctionSeam(Seam)
					|| FLayoutDirectionUtils::GetOpposite(Seam.OwnerFaceDirection)
						!= Seam.PassiveFaceDirection)
				{
					continue;
				}
				const int32 FaceAxis = ResolveFaceAxis(Seam.OwnerFaceDirection);
				const int32 RunAxis = FaceAxis == 0 ? 1 : (FaceAxis == 1 ? 0 : INDEX_NONE);
				if (RunAxis == INDEX_NONE)
				{
					continue;
				}

				TArray<FIntVector> OwnerCells;
				EnumerateOwnerCells(Seam, OwnerCells);
				for (const FIntVector& Cell : OwnerCells)
				{
					const int32 LineCoordinate = FaceAxis == 0 ? Cell.X : Cell.Y;
					const FString Key = FString::Printf(
						TEXT("%s|%s|%s|%d|%d|%d"),
						*Seam.ParentRegionDebugPath,
						*Seam.OwnerRegionDebugPath,
						*Seam.PassiveRegionDebugPath,
						static_cast<int32>(Seam.OwnerFaceDirection),
						LineCoordinate,
						Cell.Z);
					FPassivePerimeterSplitRun& Run = SplitRunsByKey.FindOrAdd(Key);
					Run.ParentRegionDebugPath = Seam.ParentRegionDebugPath;
					Run.OwnerRegionDebugPath = Seam.OwnerRegionDebugPath;
					Run.PassiveRegionDebugPath = Seam.PassiveRegionDebugPath;
					Run.OwnerFaceDirection = Seam.OwnerFaceDirection;
					Run.RunAxis = RunAxis;
					Run.Cells.AddUnique(Cell);
					int32* ExistingIndex = Run.RepresentativeSeamIndexByCell.Find(Cell);
					if (ExistingIndex == nullptr
						|| Seam.SeamId.LexicalLess(PlannedPartitionSeams[*ExistingIndex].SeamId))
					{
						Run.RepresentativeSeamIndexByCell.Add(Cell, SeamIndex);
					}
				}
			}

			for (TPair<FString, FPassivePerimeterSplitRun>& Pair : SplitRunsByKey)
			{
				FPassivePerimeterSplitRun& Run = Pair.Value;
				Run.Cells.Sort(
					[RunAxis = Run.RunAxis](const FIntVector& Left, const FIntVector& Right)
					{
						return CoordinateForRunAxis(Left, RunAxis)
							< CoordinateForRunAxis(Right, RunAxis);
					});
				TArray<FIntVector> Cluster;
				const auto EmitPassiveSplit = [&](const FIntVector& Cell, const bool bMinimumEndpoint)
				{
					const int32* RepresentativeIndex = Run.RepresentativeSeamIndexByCell.Find(Cell);
					if (RepresentativeIndex == nullptr)
					{
						return;
					}
					const bool bOwnerContinuesAgainstAnotherRegion =
						PlannedPartitionSeams.ContainsByPredicate(
							[&](const FLayoutPartitionSeamRecord& Seam)
							{
								if (!IsSupportedOwnerJunctionSeam(Seam)
									|| Seam.ParentRegionDebugPath != Run.ParentRegionDebugPath
									|| Seam.OwnerRegionDebugPath != Run.OwnerRegionDebugPath
									|| Seam.PassiveRegionDebugPath == Run.PassiveRegionDebugPath
									|| Seam.OwnerFaceDirection != Run.OwnerFaceDirection)
								{
									return false;
								}
								TArray<FIntVector> OtherOwnerCells;
								EnumerateOwnerCells(Seam, OtherOwnerCells);
								return OtherOwnerCells.Contains(Cell);
							});
					if (!bOwnerContinuesAgainstAnotherRegion)
					{
						return;
					}

					const ELayoutFaceDirection BranchFaceDirection = Run.RunAxis == 0
						? (bMinimumEndpoint ? ELayoutFaceDirection::NegX : ELayoutFaceDirection::PosX)
						: (bMinimumEndpoint ? ELayoutFaceDirection::NegY : ELayoutFaceDirection::PosY);
					const FLayoutPartitionSeamRecord& ContinuingSeam =
						PlannedPartitionSeams[*RepresentativeIndex];
					const FLayoutId RequirementId(*FString::Printf(
						TEXT("Junction.PassiveSplit.%s.%s.%s.%d.%d.%d.%d.%d"),
						*Run.ParentRegionDebugPath,
						*Run.OwnerRegionDebugPath,
						*Run.PassiveRegionDebugPath,
						Cell.X,
						Cell.Y,
						Cell.Z,
						static_cast<int32>(Run.OwnerFaceDirection),
						static_cast<int32>(BranchFaceDirection)));
					if (EmittedRequirementIds.Contains(RequirementId))
					{
						return;
					}

					FOwnedSeamJunctionRequirement& Requirement =
						JunctionRequirements.AddDefaulted_GetRef();
					Requirement.JunctionRequirementId = RequirementId;
					Requirement.OwnerRegionDebugPath = Run.OwnerRegionDebugPath;
					Requirement.PassiveRegionDebugPath = Run.PassiveRegionDebugPath;
					Requirement.AdjacencyClassId = FLayoutId(*FString::Printf(
						TEXT("PassivePerimeterSplit.%d.%d"),
						static_cast<int32>(Run.OwnerFaceDirection),
						static_cast<int32>(BranchFaceDirection)));
					Requirement.JunctionCell = Cell;
					Requirement.ContinuingSeamId = ContinuingSeam.SeamId;
					Requirement.BranchSeamId = NAME_None;
					Requirement.ContinuingPassiveRegionDebugPath = Run.PassiveRegionDebugPath;
					Requirement.InterfaceFamily = ContinuingSeam.InterfaceFamily;
					Requirement.ContinuingOwnerFaceDirection = Run.OwnerFaceDirection;
					Requirement.BranchOwnerFaceDirection = BranchFaceDirection;
					EmittedRequirementIds.Add(RequirementId);
				};
				const auto FlushCluster = [&]()
				{
					if (Cluster.Num() >= 2)
					{
						EmitPassiveSplit(Cluster[0], true);
						EmitPassiveSplit(Cluster.Last(), false);
					}
					Cluster.Reset();
				};
				for (const FIntVector& Cell : Run.Cells)
				{
					if (!Cluster.IsEmpty()
						&& CoordinateForRunAxis(Cell, Run.RunAxis)
							!= CoordinateForRunAxis(Cluster.Last(), Run.RunAxis) + 1)
					{
						FlushCluster();
					}
					Cluster.Add(Cell);
				}
				FlushCluster();
			}

			JunctionRequirements.Sort(
				[](const FOwnedSeamJunctionRequirement& Left,
					const FOwnedSeamJunctionRequirement& Right)
				{
					if (Left.OwnerRegionDebugPath != Right.OwnerRegionDebugPath)
					{
						return Left.OwnerRegionDebugPath < Right.OwnerRegionDebugPath;
					}
					if (Left.JunctionCell != Right.JunctionCell)
					{
						if (Left.JunctionCell.X != Right.JunctionCell.X)
						{
							return Left.JunctionCell.X < Right.JunctionCell.X;
						}
						if (Left.JunctionCell.Y != Right.JunctionCell.Y)
						{
							return Left.JunctionCell.Y < Right.JunctionCell.Y;
						}
						return Left.JunctionCell.Z < Right.JunctionCell.Z;
					}
					return Left.JunctionRequirementId.LexicalLess(Right.JunctionRequirementId);
				});
			return JunctionRequirements;
		}

		struct FLayoutSeamAndOptionalPlanner
		{
#if WITH_AUTOMATION_TESTS
			/** Builds synthetic seam plans for focused legacy planner coverage. Production consumes committed candidate seams. */
			static FNegotiatedSeamPlan BuildSeamPlan(
				const FRecursiveScheduleSolveContext& SolveContext,
				const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
			{
				FNegotiatedSeamPlan SeamPlan;
				SeamPlan.bPlannedPartitionSeamsAreAuthoritative = false;

				const FCompiledStructuralInputs StructuralInputs =
					BuildCompiledStructuralInputs(SolveContext);
				const FLayoutRegionSolveRequest RootSeamPlanningRequest =
					BuildRootSeamPlanningRequest(SolveContext, StructuralInputs);

				TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath;
				TMap<FString, FLayoutChildCapabilityEnvelope> CapabilityEnvelopeByRegion;
				TMap<FString, TArray<FString>> ChildrenByParent;
				if (!RootSeamPlanningRequest.RegionDebugPath.IsEmpty())
				{
					RequestsByPath.Add(
						RootSeamPlanningRequest.RegionDebugPath,
						&RootSeamPlanningRequest);
					CapabilityEnvelopeByRegion.Add(
						RootSeamPlanningRequest.RegionDebugPath,
						BuildRootSeamPlanningEnvelope(RootSeamPlanningRequest));
				}

				for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
				{
					if (!Result.bSucceeded
						|| Result.ChildRequest.RegionDebugPath.IsEmpty()
						|| Result.ChildRequest.SourceParentRegionDebugPath.IsEmpty()
						|| Result.ChildRequest.PlannedCells.IsEmpty()
						|| !Result.ChildRequest.bUseSuppliedChildCapabilityEnvelope)
					{
						continue;
					}

					RequestsByPath.Add(
						Result.ChildRequest.RegionDebugPath,
						&Result.ChildRequest);
					CapabilityEnvelopeByRegion.Add(
						Result.ChildRequest.RegionDebugPath,
						Result.ChildRequest.SuppliedChildCapabilityEnvelope);

					TArray<FString>& Children =
						ChildrenByParent.FindOrAdd(
							Result.ChildRequest.SourceParentRegionDebugPath);
					Children.AddUnique(Result.ChildRequest.RegionDebugPath);
				}

				if (RequestsByPath.IsEmpty()
					|| CapabilityEnvelopeByRegion.IsEmpty()
					|| ChildrenByParent.IsEmpty())
				{
					return SeamPlan;
				}

				TSet<FString> PassiveRegionPaths;
				FString FailureReason;
				TArray<FString> ParentPaths;
				ChildrenByParent.GetKeys(ParentPaths);
				ParentPaths.Sort();

				bool bBuiltSeams = true;
				for (const FString& ParentPath : ParentPaths)
				{
					TArray<FString> RelatedRegions = {ParentPath};
					if (const TArray<FString>* Children = ChildrenByParent.Find(ParentPath))
					{
						TArray<FString> SortedChildren = *Children;
						SortedChildren.Sort();
						RelatedRegions.Append(SortedChildren);
					}

					for (int32 LeftIndex = 0; LeftIndex < RelatedRegions.Num() && bBuiltSeams; ++LeftIndex)
					{
						for (int32 RightIndex = LeftIndex + 1; RightIndex < RelatedRegions.Num(); ++RightIndex)
						{
							if (!LayoutProfileSolverInternal::AppendSchedulePartitionSeamsForPairFromLeafStages(
									ParentPath,
									RelatedRegions[LeftIndex],
									RelatedRegions[RightIndex],
									RequestsByPath,
									CapabilityEnvelopeByRegion,
									SeamPlan.PlannedPartitionSeams,
									PassiveRegionPaths,
									false,
									FailureReason))
							{
								bBuiltSeams = false;
								break;
							}
						}
					}
				}
				if (!bBuiltSeams)
				{
					SeamPlan.PlannedPartitionSeams.Reset();
				}
				else
				{
					LayoutProfileSolverInternal::SortSchedulePartitionSeams(
						SeamPlan.PlannedPartitionSeams);
					SeamPlan.JunctionRequirements =
						BuildOwnerSideJunctionRequirementsInternal(
							SeamPlan.PlannedPartitionSeams);
				}

				static_cast<void>(SolveContext);
				return SeamPlan;
			}
#endif

			/** Builds deterministic optional-child keep/drop records after structural negotiation settles. */
			static FOptionalChildDecisionPlan BuildOptionalPlan(
				const FRecursiveScheduleSolveContext& SolveContext,
				const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
			{
				static_cast<void>(SolveContext);

				FOptionalChildDecisionPlan OptionalPlan;
				const auto AppendRelatedIdIfNamed =
					[](TArray<FLayoutId>& RelatedIds, const FLayoutId Id)
					{
						if (!Id.IsNone())
						{
							RelatedIds.Add(Id);
						}
					};

				const auto BuildOptionalDropDecisionId =
					[](const FNegotiatedDemandResult& Result) -> FLayoutId
					{
						const FString ParentPath =
							Result.ChildRequest.SourceParentRegionDebugPath.IsEmpty()
								? TEXT("UnknownParent")
								: Result.ChildRequest.SourceParentRegionDebugPath;
						const FString ChildPath =
							Result.ChildRegionDebugPath.IsEmpty()
								? Result.ChildRequest.RegionDebugPath
								: Result.ChildRegionDebugPath;
						const FString SourceEntryToken =
							Result.ChildRequest.SourceContentEntryId.IsNone()
								? TEXT("NoEntry")
								: Result.ChildRequest.SourceContentEntryId.ToString();
						return FLayoutId(*FString::Printf(
							TEXT("OptionalDrop.%s.%s.%s"),
							*ParentPath,
							*ChildPath,
							*SourceEntryToken));
					};

				for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
				{
					if (!Result.ChildRequest.bSourceContentEntryOptional)
					{
						continue;
					}

					if (Result.bSucceeded)
					{
						OptionalPlan.KeptChildRegionDebugPaths.Add(
							Result.ChildRegionDebugPath);
						continue;
					}

					FLayoutDroppedOptionalChildRecord& DropRecord =
						OptionalPlan.DroppedOptionalChildren.AddDefaulted_GetRef();
					DropRecord.DropDecisionId = BuildOptionalDropDecisionId(Result);
					DropRecord.ParentRegionDebugPath =
						Result.ChildRequest.SourceParentRegionDebugPath;
					DropRecord.ChildRegionDebugPath =
						Result.ChildRegionDebugPath.IsEmpty()
							? Result.ChildRequest.RegionDebugPath
							: Result.ChildRegionDebugPath;
					DropRecord.SourceContentEntryId =
						Result.ChildRequest.SourceContentEntryId;
					DropRecord.ChildProfileSnapshotId =
						Result.ChildRequest.EffectiveSnapshotId;
					AppendRelatedIdIfNamed(
						DropRecord.RelatedIds,
						Result.ChildRequest.SourceContentEntryId);
					AppendRelatedIdIfNamed(
						DropRecord.RelatedIds,
						Result.ChildRequest.EffectiveSnapshotId);
					DropRecord.FailureReason = Result.FailureReason;
				}

				OptionalPlan.KeptChildRegionDebugPaths.Sort();
				OptionalPlan.DroppedOptionalChildren.Sort(
					[](const FLayoutDroppedOptionalChildRecord& Left,
						const FLayoutDroppedOptionalChildRecord& Right)
					{
						if (Left.ParentRegionDebugPath != Right.ParentRegionDebugPath)
						{
							return Left.ParentRegionDebugPath < Right.ParentRegionDebugPath;
						}
						if (Left.ChildRegionDebugPath != Right.ChildRegionDebugPath)
						{
							return Left.ChildRegionDebugPath < Right.ChildRegionDebugPath;
						}
						return Left.DropDecisionId.LexicalLess(Right.DropDecisionId);
					});
				return OptionalPlan;
			}
		};
	}

	bool AppendSchedulePartitionSeamsForPairFromLeafStagesBridge(
		const FString& ParentPath,
		const FString& LeftPath,
		const FString& RightPath,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		TArray<FLayoutPartitionSeamRecord>& InOutSeams,
		TSet<FString>& OutPassiveRegionPaths,
		const bool bPlannedSeamsAreAuthoritative,
		FString& OutFailureReason)
	{
		return AppendSchedulePartitionSeamsForPairFromLeafStagesInternal(
			ParentPath,
			LeftPath,
			RightPath,
			RequestsByPath,
			CapabilityEnvelopeByRegion,
			InOutSeams,
			OutPassiveRegionPaths,
			bPlannedSeamsAreAuthoritative,
			OutFailureReason);
	}

#if WITH_AUTOMATION_TESTS
	FNegotiatedSeamPlan BuildNegotiatedSeamPlan(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		return FLayoutSeamAndOptionalPlanner::BuildSeamPlan(
			SolveContext,
			NegotiatedDemandResults);
	}
#endif

	bool HasParentPerimeterContinuation(const FLayoutRegionSolveRequest& Request, const FIntVector& Cell)
	{
		const bool bAlongX = Cell.X > 0 && Cell.X < Request.FootprintSize.X - 1
			&& (Cell.Y == 0 || Cell.Y == Request.FootprintSize.Y - 1);
		const bool bAlongY = Cell.Y > 0 && Cell.Y < Request.FootprintSize.Y - 1
			&& (Cell.X == 0 || Cell.X == Request.FootprintSize.X - 1);
		if (!bAlongX && !bAlongY) return false;
		const auto& Cells = Request.PrecomputedPlannedCells.IsEmpty() ? Request.PlannedCells : Request.PrecomputedPlannedCells;
		const FIntVector Delta = bAlongX ? FIntVector(1, 0, 0) : FIntVector(0, 1, 0);
		for (const FIntVector Neighbor : {Cell - Delta, Cell + Delta})
		{
			if (!Cells.ContainsByPredicate([&](const FLayoutPlannedCell& Planned)
			{
				return Planned.Cell == Neighbor && !Planned.bIsTopBridgeOffer
					&& (Planned.Intent == ELayoutCellIntent::Boundary || Planned.Intent == ELayoutCellIntent::Entry
						|| Planned.Intent == ELayoutCellIntent::VerticalAccess);
			})) return false;
		}
		return true;
	}

	TArray<FOwnedSeamJunctionRequirement> BuildOwnerSideJunctionRequirements(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FLayoutRegionSolveRequest* ParentRequest)
	{
		return BuildOwnerSideJunctionRequirementsInternal(PlannedPartitionSeams, ParentRequest);
	}

	FOptionalChildDecisionPlan BuildOptionalChildDecisionPlan(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		return FLayoutSeamAndOptionalPlanner::BuildOptionalPlan(
			SolveContext,
			NegotiatedDemandResults);
	}
}

namespace
{
	static bool DoesChildCapabilitySupportOwnedSeam(
		const FLayoutChildCapabilitySeam& Capability,
		const FGameplayTag& InterfaceFamily,
		const ELayoutFaceDirection FaceDirection)
	{
		return Capability.bCanOwnSeam
			&& Capability.InterfaceFamily == InterfaceFamily
			&& Capability.FaceDirection == FaceDirection;
	}

	static bool DoesChildCapabilitySupportAcceptedSeam(
		const FLayoutChildCapabilitySeam& Capability,
		const FGameplayTag& InterfaceFamily,
		const ELayoutFaceDirection FaceDirection)
	{
		return Capability.bCanAcceptSeam
			&& Capability.InterfaceFamily == InterfaceFamily
			&& Capability.FaceDirection == FaceDirection;
	}

	static bool DoesSeamEnvelopeExposeAnyCapabilityForFace(
		const FLayoutChildCapabilityEnvelope& Envelope,
		const ELayoutFaceDirection FaceDirection)
	{
		return Envelope.SeamCapabilities.ContainsByPredicate(
			[FaceDirection](const FLayoutChildCapabilitySeam& Capability)
			{
				return Capability.FaceDirection == FaceDirection
					&& Capability.InterfaceFamily.IsValid()
					&& (Capability.bCanOwnSeam || Capability.bCanAcceptSeam);
			});
	}

	static bool IsDoorLikeEndpointCapability(
		const FLayoutChildCapabilityEndpoint& Capability)
	{
		return Capability.ConnectionTag == LayoutGameplayTags::FaceEntry
			|| Capability.ConnectionTag == LayoutGameplayTags::FaceOpen
			|| Capability.AllowedConnectionTags.HasTagExact(LayoutGameplayTags::FaceEntry)
			|| Capability.AllowedConnectionTags.HasTagExact(LayoutGameplayTags::FaceOpen);
	}

	static bool DoesEnvelopeExposeDoorEndpointCapability(
		const FLayoutChildCapabilityEnvelope& Envelope,
		const ELayoutFaceDirection FaceDirection)
	{
		return Envelope.EndpointCapabilities.ContainsByPredicate(
			[FaceDirection](const FLayoutChildCapabilityEndpoint& Capability)
			{
				return Capability.FaceDirection == FaceDirection
					&& IsDoorLikeEndpointCapability(Capability)
					&& !Capability.TraversalChannels.IsEmpty();
			});
	}

	static bool DoRegionsExposeImplicitSharedDoorInterface(
		const FLayoutChildCapabilityEnvelope& OwnerEnvelope,
		const ELayoutFaceDirection OwnerFaceDirection,
		const FLayoutChildCapabilityEnvelope& PassiveEnvelope,
		const ELayoutFaceDirection PassiveFaceDirection)
	{
		return OwnerFaceDirection == PassiveFaceDirection
			&& DoesEnvelopeExposeDoorEndpointCapability(OwnerEnvelope, OwnerFaceDirection)
			&& DoesEnvelopeExposeDoorEndpointCapability(PassiveEnvelope, PassiveFaceDirection);
	}

	static FIntPoint GetScheduleLeafFootprintSize(
		const TArray<LayoutProfileSolverInternal::FScheduleSeamPlanningCell>& Cells)
	{
		int32 MaxX = -1;
		int32 MaxY = -1;
		for (const LayoutProfileSolverInternal::FScheduleSeamPlanningCell& Cell : Cells)
		{
			MaxX = FMath::Max(MaxX, Cell.LocalCell.X);
			MaxY = FMath::Max(MaxY, Cell.LocalCell.Y);
		}

		return FIntPoint(MaxX + 1, MaxY + 1);
	}

	static bool TryGetScheduleLeafSharedOverlapFaceDirections(
		const FIntVector& RegionALocalCell,
		const FIntPoint& RegionAFootprintSize,
		const FIntVector& RegionBLocalCell,
		const FIntPoint& RegionBFootprintSize,
		const bool bAllowSameFaceOverlap,
		ELayoutFaceDirection& OutRegionAFaceDirection,
		ELayoutFaceDirection& OutRegionBFaceDirection)
	{
		if (RegionALocalCell.Y == RegionBLocalCell.Y)
		{
			if (RegionALocalCell.X == RegionAFootprintSize.X - 1 && RegionBLocalCell.X == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosX;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegX;
				return true;
			}

			if (RegionALocalCell.X == 0 && RegionBLocalCell.X == RegionBFootprintSize.X - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegX;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosX;
				return true;
			}
		}

		if (RegionALocalCell.X == RegionBLocalCell.X)
		{
			if (RegionALocalCell.Y == RegionAFootprintSize.Y - 1 && RegionBLocalCell.Y == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosY;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegY;
				return true;
			}

			if (RegionALocalCell.Y == 0 && RegionBLocalCell.Y == RegionBFootprintSize.Y - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegY;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosY;
				return true;
			}
		}

		if (bAllowSameFaceOverlap)
		{
			if (RegionALocalCell.X == RegionAFootprintSize.X - 1 && RegionBLocalCell.X == RegionBFootprintSize.X - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosX;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosX;
				return true;
			}

			if (RegionALocalCell.X == 0 && RegionBLocalCell.X == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegX;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegX;
				return true;
			}

			if (RegionALocalCell.Y == RegionAFootprintSize.Y - 1 && RegionBLocalCell.Y == RegionBFootprintSize.Y - 1)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::PosY;
				OutRegionBFaceDirection = ELayoutFaceDirection::PosY;
				return true;
			}

			if (RegionALocalCell.Y == 0 && RegionBLocalCell.Y == 0)
			{
				OutRegionAFaceDirection = ELayoutFaceDirection::NegY;
				OutRegionBFaceDirection = ELayoutFaceDirection::NegY;
				return true;
			}
		}

		return false;
	}

	static FString BuildScheduleLeafRunGroupingKey(
		const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment,
		const bool bPreferVerticalOverlapGrouping)
	{
		if (bPreferVerticalOverlapGrouping)
		{
			return FString::Printf(
				TEXT("%d|%d|%d|%d|%d|%d"),
				static_cast<int32>(Segment.RegionAFaceDirection),
				Segment.RegionACell.X,
				Segment.RegionACell.Y,
				Segment.RegionBCell.X,
				Segment.RegionBCell.Y,
				LayoutProfileSolverInternal::DoesSeamSegmentRequireDoorInterface(Segment) ? 1 : 0);
		}

		const int32 DoorMarker = LayoutProfileSolverInternal::DoesSeamSegmentRequireDoorInterface(Segment) ? 1 : 0;
		const ELayoutFaceDirection FaceDirection = Segment.RegionAFaceDirection;
		if (FaceDirection == ELayoutFaceDirection::PosX || FaceDirection == ELayoutFaceDirection::NegX)
		{
			return FString::Printf(
				TEXT("%d|%d|%d|%d|%d|%d"),
				static_cast<int32>(FaceDirection),
				Segment.RegionACell.Z,
				Segment.RegionACell.X,
				Segment.RegionBCell.X,
				0,
				DoorMarker);
		}

		return FString::Printf(
			TEXT("%d|%d|%d|%d|%d|%d"),
			static_cast<int32>(FaceDirection),
			Segment.RegionACell.Z,
			Segment.RegionACell.Y,
			Segment.RegionBCell.Y,
			1,
			DoorMarker);
	}

	static int32 GetScheduleLeafRunAxisValue(
		const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment,
		const bool bPreferVerticalOverlapGrouping)
	{
		if (bPreferVerticalOverlapGrouping)
		{
			return Segment.RegionACell.Z;
		}

		return (Segment.RegionAFaceDirection == ELayoutFaceDirection::PosX
				|| Segment.RegionAFaceDirection == ELayoutFaceDirection::NegX)
			? Segment.RegionACell.Y
			: Segment.RegionACell.X;
	}

	static bool DoesOverlapSegmentContinueVertically(
		const LayoutProfileSolverInternal::FCompiledSeamSegment& Segment,
		const TArray<LayoutProfileSolverInternal::FCompiledSeamSegment>& Segments)
	{
		if (Segment.RegionACell != Segment.RegionBCell)
		{
			return false;
		}

		return Segments.ContainsByPredicate(
			[&Segment](const LayoutProfileSolverInternal::FCompiledSeamSegment& OtherSegment)
			{
				return OtherSegment.RegionAPath == Segment.RegionAPath
					&& OtherSegment.RegionBPath == Segment.RegionBPath
					&& OtherSegment.RegionAFaceDirection == Segment.RegionAFaceDirection
					&& OtherSegment.RegionBFaceDirection == Segment.RegionBFaceDirection
					&& OtherSegment.RegionACell.X == Segment.RegionACell.X
					&& OtherSegment.RegionACell.Y == Segment.RegionACell.Y
					&& OtherSegment.RegionBCell.X == Segment.RegionBCell.X
					&& OtherSegment.RegionBCell.Y == Segment.RegionBCell.Y
					&& FMath::Abs(OtherSegment.RegionACell.Z - Segment.RegionACell.Z) == 1;
			});
	}

	struct FSeamArbitrationOption
	{
		FString OwnerRegionPath;
		FString PassiveRegionPath;
		FGameplayTag InterfaceFamily;
		ELayoutFaceDirection OwnerFaceDirection = ELayoutFaceDirection::PosX;
		ELayoutFaceDirection PassiveFaceDirection = ELayoutFaceDirection::NegX;
		int32 OwnerPreference = 1;
	};
}

bool LayoutProfileSolverInternal::TryGetSchedulePlannedCellsForSeamPlanning(
	const FLayoutRegionSolveRequest& Request,
	TArray<FScheduleSeamPlanningCell>& OutPlannedCells)
{
	if (Request.PlannedCells.IsEmpty())
	{
		return false;
	}

	OutPlannedCells.Reset();
	OutPlannedCells.Reserve(Request.PlannedCells.Num());
	for (const FLayoutPlannedCell& PlannedCell : Request.PlannedCells)
	{
		FScheduleSeamPlanningCell& ScheduleCell = OutPlannedCells.AddDefaulted_GetRef();
		ScheduleCell.WorldCell = PlannedCell.Cell + Request.RegionCellOffset;
		ScheduleCell.LocalCell = PlannedCell.Cell;
		ScheduleCell.Intent = PlannedCell.Intent;
	}

	OutPlannedCells.Sort([](const FScheduleSeamPlanningCell& Left, const FScheduleSeamPlanningCell& Right)
	{
		if (Left.WorldCell.Z != Right.WorldCell.Z)
		{
			return Left.WorldCell.Z < Right.WorldCell.Z;
		}
		if (Left.WorldCell.Y != Right.WorldCell.Y)
		{
			return Left.WorldCell.Y < Right.WorldCell.Y;
		}
		return Left.WorldCell.X < Right.WorldCell.X;
	});
	return !OutPlannedCells.IsEmpty();
}

void LayoutProfileSolverInternal::CollectSharedSeamSegmentsForPair(
	const FString& RegionAPath,
	const TArray<FScheduleSeamPlanningCell>& RegionAPlannedCells,
	const FString& RegionBPath,
	const TArray<FScheduleSeamPlanningCell>& RegionBPlannedCells,
	const bool bIncludeAdjacentSegments,
	const bool bIncludeOverlapSegments,
	const bool bAllowSameFaceOverlap,
	TArray<FCompiledSeamSegment>& OutSegments)
{
	OutSegments.Reset();

	TMap<FIntVector, const FScheduleSeamPlanningCell*> RegionBCells;
	for (const FScheduleSeamPlanningCell& PlannedCell : RegionBPlannedCells)
	{
		RegionBCells.Add(PlannedCell.WorldCell, &PlannedCell);
	}

	const FIntPoint RegionAFootprintSize = GetScheduleLeafFootprintSize(RegionAPlannedCells);
	const FIntPoint RegionBFootprintSize = GetScheduleLeafFootprintSize(RegionBPlannedCells);

	static constexpr ELayoutFaceDirection HorizontalDirections[] =
	{
		ELayoutFaceDirection::NegX,
		ELayoutFaceDirection::PosX,
		ELayoutFaceDirection::NegY,
		ELayoutFaceDirection::PosY
	};

	for (const FScheduleSeamPlanningCell& PlannedCell : RegionAPlannedCells)
	{
		if (bIncludeAdjacentSegments)
		{
			for (const ELayoutFaceDirection FaceDirection : HorizontalDirections)
			{
				const FIntVector NeighborCell = PlannedCell.WorldCell + FLayoutDirectionUtils::ToCellDelta(FaceDirection);
				if (!RegionBCells.Contains(NeighborCell))
				{
					continue;
				}

				FCompiledSeamSegment& Segment = OutSegments.AddDefaulted_GetRef();
				Segment.RegionAPath = RegionAPath;
				Segment.RegionBPath = RegionBPath;
				Segment.RegionACell = PlannedCell.WorldCell;
				Segment.RegionBCell = NeighborCell;
				Segment.RegionAIntent = PlannedCell.Intent;
				Segment.RegionBIntent = RegionBCells.FindChecked(NeighborCell)->Intent;
				Segment.RegionAFaceDirection = FaceDirection;
				Segment.RegionBFaceDirection = FLayoutDirectionUtils::GetOpposite(FaceDirection);
			}
		}

		if (!bIncludeOverlapSegments)
		{
			continue;
		}

		const FScheduleSeamPlanningCell* const* OverlapCellPtr = RegionBCells.Find(PlannedCell.WorldCell);
		if (OverlapCellPtr == nullptr || *OverlapCellPtr == nullptr)
		{
			continue;
		}

		const FScheduleSeamPlanningCell& OverlapCell = **OverlapCellPtr;
		const bool bRegionACanParticipateInOverlapSeam =
			PlannedCell.Intent == ELayoutCellIntent::Boundary
			|| PlannedCell.Intent == ELayoutCellIntent::Entry;
		const bool bRegionBCanParticipateInOverlapSeam =
			OverlapCell.Intent == ELayoutCellIntent::Boundary
			|| OverlapCell.Intent == ELayoutCellIntent::Entry;
		if (!bRegionACanParticipateInOverlapSeam || !bRegionBCanParticipateInOverlapSeam)
		{
			continue;
		}

		ELayoutFaceDirection RegionAFaceDirection = ELayoutFaceDirection::PosX;
		ELayoutFaceDirection RegionBFaceDirection = ELayoutFaceDirection::NegX;
		if (!TryGetScheduleLeafSharedOverlapFaceDirections(
				PlannedCell.LocalCell,
				RegionAFootprintSize,
				OverlapCell.LocalCell,
				RegionBFootprintSize,
				bAllowSameFaceOverlap,
				RegionAFaceDirection,
				RegionBFaceDirection))
		{
			continue;
		}

		FCompiledSeamSegment& Segment = OutSegments.AddDefaulted_GetRef();
		Segment.RegionAPath = RegionAPath;
		Segment.RegionBPath = RegionBPath;
		Segment.RegionACell = PlannedCell.WorldCell;
		Segment.RegionBCell = OverlapCell.WorldCell;
		Segment.RegionAIntent = PlannedCell.Intent;
		Segment.RegionBIntent = OverlapCell.Intent;
		Segment.RegionAFaceDirection = RegionAFaceDirection;
		Segment.RegionBFaceDirection = RegionBFaceDirection;
	}
}

bool LayoutProfileSolverInternal::DoesSeamSegmentRequireDoorInterface(const FCompiledSeamSegment& Segment)
{
	return Segment.RegionAIntent == ELayoutCellIntent::Entry
		|| Segment.RegionBIntent == ELayoutCellIntent::Entry;
}

void LayoutProfileSolverInternal::BuildContiguousSeamRuns(
	const TArray<FCompiledSeamSegment>& Segments,
	TArray<TArray<FCompiledSeamSegment>>& OutRuns)
{
	OutRuns.Reset();

	TMap<FString, TArray<FCompiledSeamSegment>> SegmentsByGroup;
	for (const FCompiledSeamSegment& Segment : Segments)
	{
		const bool bPreferVerticalOverlapGrouping =
			DoesOverlapSegmentContinueVertically(Segment, Segments);
		SegmentsByGroup.FindOrAdd(
			BuildScheduleLeafRunGroupingKey(
				Segment,
				bPreferVerticalOverlapGrouping)).Add(Segment);
	}

	TArray<FString> GroupKeys;
	SegmentsByGroup.GetKeys(GroupKeys);
	GroupKeys.Sort();
	for (const FString& GroupKey : GroupKeys)
	{
		TArray<FCompiledSeamSegment>& GroupSegments = SegmentsByGroup.FindChecked(GroupKey);
		const bool bPreferVerticalOverlapGrouping =
			!GroupSegments.IsEmpty()
			&& DoesOverlapSegmentContinueVertically(GroupSegments[0], GroupSegments);
		GroupSegments.Sort(
			[bPreferVerticalOverlapGrouping](const FCompiledSeamSegment& Left, const FCompiledSeamSegment& Right)
			{
				return GetScheduleLeafRunAxisValue(Left, bPreferVerticalOverlapGrouping)
					< GetScheduleLeafRunAxisValue(Right, bPreferVerticalOverlapGrouping);
			});

		TArray<FCompiledSeamSegment> CurrentRun;
		int32 PreviousAxis = TNumericLimits<int32>::Min();
		for (const FCompiledSeamSegment& Segment : GroupSegments)
		{
			const int32 AxisValue =
				GetScheduleLeafRunAxisValue(Segment, bPreferVerticalOverlapGrouping);
			if (CurrentRun.IsEmpty() || AxisValue == PreviousAxis + 1)
			{
				CurrentRun.Add(Segment);
			}
			else
			{
				OutRuns.Add(CurrentRun);
				CurrentRun = {Segment};
			}

			PreviousAxis = AxisValue;
		}

		if (!CurrentRun.IsEmpty())
		{
			OutRuns.Add(CurrentRun);
		}
	}
}

bool LayoutProfileSolverInternal::HasAnyChildSeamCapabilityForFace(
	const FLayoutChildCapabilityEnvelope& Envelope,
	const ELayoutFaceDirection FaceDirection)
{
	return DoesSeamEnvelopeExposeAnyCapabilityForFace(Envelope, FaceDirection);
}

bool LayoutProfileSolverInternal::TryChooseSeamContractForRun(
	const TArray<FCompiledSeamSegment>& RunSegments,
	const FString& PreferredOwnerPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	FChosenSeamContract& OutChosenContract)
{
	OutChosenContract = FChosenSeamContract();
	if (RunSegments.IsEmpty())
	{
		return false;
	}

	const FCompiledSeamSegment& FirstSegment = RunSegments[0];
	const FLayoutChildCapabilityEnvelope* EnvelopeA = CapabilityEnvelopeByRegion.Find(FirstSegment.RegionAPath);
	const FLayoutChildCapabilityEnvelope* EnvelopeB = CapabilityEnvelopeByRegion.Find(FirstSegment.RegionBPath);
	if (EnvelopeA == nullptr || EnvelopeB == nullptr)
	{
		return false;
	}

	TArray<FSeamArbitrationOption> Options;
	auto AddOptionsForDirection =
		[&Options, &PreferredOwnerPath, &RunSegments](
			const FString& OwnerPath,
			const FString& PassivePath,
			const FLayoutChildCapabilityEnvelope& OwnerEnvelope,
			const FLayoutChildCapabilityEnvelope& PassiveEnvelope,
			const ELayoutFaceDirection OwnerFaceDirection,
			const ELayoutFaceDirection PassiveFaceDirection)
		{
			const bool bRunRequiresDoorInterface =
				RunSegments.ContainsByPredicate([](const FCompiledSeamSegment& Segment)
				{
					return LayoutProfileSolverInternal::DoesSeamSegmentRequireDoorInterface(Segment);
				});

			for (const FLayoutChildCapabilitySeam& OwnerCapability : OwnerEnvelope.SeamCapabilities)
			{
				if (bRunRequiresDoorInterface
					&& OwnerCapability.InterfaceFamily != LayoutGameplayTags::InterfacePartitionDoor)
				{
					continue;
				}

				if (!DoesChildCapabilitySupportOwnedSeam(
						OwnerCapability,
						OwnerCapability.InterfaceFamily,
						OwnerFaceDirection))
				{
					continue;
				}

				const bool bPassiveSupported = PassiveEnvelope.SeamCapabilities.ContainsByPredicate(
					[&OwnerCapability, PassiveFaceDirection](const FLayoutChildCapabilitySeam& PassiveCapability)
					{
						return DoesChildCapabilitySupportAcceptedSeam(
							PassiveCapability,
							OwnerCapability.InterfaceFamily,
							PassiveFaceDirection);
					});
				if (!bPassiveSupported)
				{
					continue;
				}

				FSeamArbitrationOption& Option = Options.AddDefaulted_GetRef();
				Option.OwnerRegionPath = OwnerPath;
				Option.PassiveRegionPath = PassivePath;
				Option.InterfaceFamily = OwnerCapability.InterfaceFamily;
				Option.OwnerFaceDirection = OwnerFaceDirection;
				Option.PassiveFaceDirection = PassiveFaceDirection;
				Option.OwnerPreference =
					(!PreferredOwnerPath.IsEmpty() && OwnerPath == PreferredOwnerPath) ? 0 : 1;
			}

			if (bRunRequiresDoorInterface
				&& DoRegionsExposeImplicitSharedDoorInterface(
					OwnerEnvelope,
					OwnerFaceDirection,
					PassiveEnvelope,
					PassiveFaceDirection))
			{
				FSeamArbitrationOption& Option = Options.AddDefaulted_GetRef();
				Option.OwnerRegionPath = OwnerPath;
				Option.PassiveRegionPath = PassivePath;
				Option.InterfaceFamily = LayoutGameplayTags::InterfacePartitionDoor;
				Option.OwnerFaceDirection = OwnerFaceDirection;
				Option.PassiveFaceDirection = PassiveFaceDirection;
				Option.OwnerPreference =
					(!PreferredOwnerPath.IsEmpty() && OwnerPath == PreferredOwnerPath) ? 0 : 1;
			}
		};

	AddOptionsForDirection(
		FirstSegment.RegionAPath,
		FirstSegment.RegionBPath,
		*EnvelopeA,
		*EnvelopeB,
		FirstSegment.RegionAFaceDirection,
		FirstSegment.RegionBFaceDirection);
	AddOptionsForDirection(
		FirstSegment.RegionBPath,
		FirstSegment.RegionAPath,
		*EnvelopeB,
		*EnvelopeA,
		FirstSegment.RegionBFaceDirection,
		FirstSegment.RegionAFaceDirection);

	if (Options.IsEmpty())
	{
		return false;
	}

	Options.Sort([](const FSeamArbitrationOption& Left, const FSeamArbitrationOption& Right)
	{
		if (Left.OwnerPreference != Right.OwnerPreference)
		{
			return Left.OwnerPreference < Right.OwnerPreference;
		}
		if (Left.OwnerPreference == Right.OwnerPreference
			&& Left.OwnerRegionPath != Right.OwnerRegionPath)
		{
			return Left.OwnerRegionPath > Right.OwnerRegionPath;
		}
		if (Left.OwnerRegionPath != Right.OwnerRegionPath)
		{
			return Left.OwnerRegionPath < Right.OwnerRegionPath;
		}
		if (Left.PassiveRegionPath != Right.PassiveRegionPath)
		{
			return Left.PassiveRegionPath < Right.PassiveRegionPath;
		}
		if (Left.InterfaceFamily != Right.InterfaceFamily)
		{
			return Left.InterfaceFamily.ToString() < Right.InterfaceFamily.ToString();
		}
		return static_cast<uint8>(Left.OwnerFaceDirection) < static_cast<uint8>(Right.OwnerFaceDirection);
	});

	const FSeamArbitrationOption& Option = Options[0];
	OutChosenContract.bValid = true;
	OutChosenContract.OwnerRegionPath = Option.OwnerRegionPath;
	OutChosenContract.PassiveRegionPath = Option.PassiveRegionPath;
	OutChosenContract.InterfaceFamily = Option.InterfaceFamily;
	OutChosenContract.OwnerFaceDirection = Option.OwnerFaceDirection;
	OutChosenContract.PassiveFaceDirection = Option.PassiveFaceDirection;
	return true;
}

#if WITH_AUTOMATION_TESTS
bool LayoutProfileSolverInternal::TryChooseLeafStageSeamContractForTests(
	const TArray<FCompiledSeamSegment>& RunSegments,
	const FString& PreferredOwnerPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	FChosenSeamContract& OutChosenContract)
{
	return TryChooseSeamContractForRun(
		RunSegments,
		PreferredOwnerPath,
		CapabilityEnvelopeByRegion,
		OutChosenContract);
}
#endif

FLayoutPartitionSeamRecord LayoutProfileSolverInternal::BuildPartitionSeamRecordFromRun(
	const FString& ParentRegionPath,
	const TArray<FCompiledSeamSegment>& RunSegments,
	const FChosenSeamContract& ChosenContract)
{
	check(!RunSegments.IsEmpty());
	const FCompiledSeamSegment& FirstSegment = RunSegments[0];
	const FCompiledSeamSegment& LastSegment = RunSegments.Last();

	const bool bOwnerIsRegionA = ChosenContract.OwnerRegionPath == FirstSegment.RegionAPath;
	FLayoutPartitionSeamRecord SeamRecord;
	SeamRecord.ParentRegionDebugPath = ParentRegionPath;
	SeamRecord.OwnerRegionDebugPath = ChosenContract.OwnerRegionPath;
	SeamRecord.PassiveRegionDebugPath = ChosenContract.PassiveRegionPath;
	SeamRecord.InterfaceFamily = ChosenContract.InterfaceFamily;
	SeamRecord.OwnerFaceDirection = ChosenContract.OwnerFaceDirection;
	SeamRecord.PassiveFaceDirection = ChosenContract.PassiveFaceDirection;
	SeamRecord.OwnerStartCell = bOwnerIsRegionA ? FirstSegment.RegionACell : FirstSegment.RegionBCell;
	SeamRecord.OwnerEndCell = bOwnerIsRegionA ? LastSegment.RegionACell : LastSegment.RegionBCell;
	SeamRecord.PassiveStartCell = bOwnerIsRegionA ? FirstSegment.RegionBCell : FirstSegment.RegionACell;
	SeamRecord.PassiveEndCell = bOwnerIsRegionA ? LastSegment.RegionBCell : LastSegment.RegionACell;
	SeamRecord.SegmentCount = RunSegments.Num();
	SeamRecord.SeamId = FLayoutId(*FString::Printf(
		TEXT("%s.%s.%s.%s"),
		*ParentRegionPath,
		*ChosenContract.OwnerRegionPath,
		*ChosenContract.PassiveRegionPath,
		*ChosenContract.InterfaceFamily.ToString()));
	return SeamRecord;
}

bool LayoutProfileSolverInternal::AppendSchedulePartitionSeamsForPairFromLeafStages(
	const FString& ParentPath,
	const FString& LeftPath,
	const FString& RightPath,
	const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
	const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
	TArray<FLayoutPartitionSeamRecord>& InOutSeams,
	TSet<FString>& OutPassiveRegionPaths,
	const bool bPlannedSeamsAreAuthoritative,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverFacade::AppendSchedulePartitionSeamsForPairFromLeafStagesBridge(
		ParentPath,
		LeftPath,
		RightPath,
		RequestsByPath,
		CapabilityEnvelopeByRegion,
		InOutSeams,
		OutPassiveRegionPaths,
		bPlannedSeamsAreAuthoritative,
		OutFailureReason);
}
