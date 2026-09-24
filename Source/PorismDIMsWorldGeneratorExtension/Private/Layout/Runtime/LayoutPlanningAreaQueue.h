// Copyright 2026 Spotted Loaf Studio
#pragma once

#include "CoreMinimal.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"

/** Game-thread bookkeeping over the component-owned loaded directory.
 * Each LOD/origin is admitted to discovery once per generation, even across unload/recreation.
 * Native lifetimes own resumable scan cursors, not layout boundaries or quotas. The
 * bounded canonical frontier shares failure allowance across overlapping LODs, not completion.
 * Workers capture values only; this queue and its borrowed directory never cross threads.
 */
class FLayoutPlanningAreaQueue
{
public:
	using FChunkKey = TTuple<int32, FIntVector>;

	/** Required-layer priority is a bounded union of conservative layout-reach boxes, not a new queue. */
	void SetStartupBounds(const TArray<FBox>& Bounds, const int32 FirstRequiredDetailLevel)
	{
		if (StartupBounds == Bounds && StartupFirstDetailLevel == FirstRequiredDetailLevel) return;
		StartupBounds = Bounds;
		StartupFirstDetailLevel = FirstRequiredDetailLevel;
		bNeedsWork = true;
	}
	bool IsStartupPosition(const FIntVector& Position) const
	{
		for (const FBox& Bounds : StartupBounds) if (Bounds.IsInsideOrOn(FVector(Position))) return true;
		return false;
	}
	/** Queued root/route priority uses nearest-center Chebyshev distance, saturating at far-world coordinates.
	 * No centers means lowest priority; chunk admission separately measures distance to native coverage. */
	static int32 ComputePriority(const FIntVector& SiteCenter, const TConstArrayView<FIntVector> Centers)
	{
		if (Centers.IsEmpty()) return MIN_int32;
		int64 NearestDistance = MAX_int32;
		for (const FIntVector& Center : Centers)
		{
			const int64 Distance = FMath::Max3(
				FMath::Abs(static_cast<int64>(SiteCenter.X) - Center.X),
				FMath::Abs(static_cast<int64>(SiteCenter.Y) - Center.Y),
				FMath::Abs(static_cast<int64>(SiteCenter.Z) - Center.Z));
			NearestDistance = FMath::Min(NearestDistance, Distance);
		}
		return -static_cast<int32>(NearestDistance);
	}

	explicit FLayoutPlanningAreaQueue(TArray<FLayoutLoadedChunkLayer>& InDirectory) : Directory(InDirectory) {}

	/** Starts an input revision without erasing Created authority or native loaded coverage. */
	void Reset(const TCHAR* Reason = TEXT("input invalidation"), const bool bRetireStarted = false)
	{
		LastResetReason = Reason;
		Areas.Reset();
		WorkingChunks.Reset();
		LastCenters.Reset();
		++InputRevision;
		for (auto It = PendingCreatedChunks.CreateIterator(); It; ++It)
		{
			FLayoutLoadedChunkState* Chunk = FindChunk(*It);
			if (!Chunk) { It.RemoveCurrent(); continue; }
			Chunk->InputRevision = InputRevision;
			if (bRetireStarted && Chunk->bScanStarted)
			{
				Settle(*It, *Chunk, true);
				It.RemoveCurrent();
			}
			// Revision changes fence captures, not the completed prefix or terminal state.
		}
		bNeedsWork = true;
	}

	/** Starts a new generation's coverage and discovery history; lifetime/scan ids still fence old callbacks. */
	void ResetLoadedDirectory()
	{
		Reset(TEXT("generation lifetime"));
		++Generation;
		DirectoryChanges.SetNum(FMath::Max(DirectoryChanges.Num(), Directory.Num()));
		for (int32 Level = 0; Level < Directory.Num(); ++Level)
			DirectoryChanges[Level].Removed += Directory[Level].Chunks.Num();
		Directory.Reset();
		PendingCreatedChunks.Reset();
		DiscoveryAdmittedChunks.Reset();
		StartupBounds.Reset();
		RecreatedDiscoverySkipped = 0;
		EvictedChunks = 0;
	}

	/** No-center/disabled cleanup drops automatic work, not still-loaded terrain or settled summaries. */
	void RetireWorkingSet()
	{
		for (auto It = PendingCreatedChunks.CreateIterator(); It; ++It)
		{
			FLayoutLoadedChunkState* Chunk = FindChunk(*It);
			if (!Chunk) { It.RemoveCurrent(); continue; }
			if (Chunk->bScanStarted)
			{
				Settle(*It, *Chunk, true);
				It.RemoveCurrent();
			}
		}
		Areas.Reset();
		WorkingChunks.Reset();
		LastCenters.Reset();
		bNeedsWork = false;
	}

	/** Sampling keeps its existing four-by-four canonical lattice; capacity is world-shared. */
	void Configure(const int32 Spacing, const int32 Capacity)
	{
		const int32 NewSpacing = FMath::Clamp(Spacing, 1, MAX_int32 / 4);
		if (SampleSpacing != NewSpacing)
		{
			SampleSpacing = NewSpacing;
			Reset(TEXT("sample spacing"), true);
		}
		MaxCachedChunks = FMath::Max(1, Capacity);
		while (WorkingChunks.Num() > MaxCachedChunks && EvictWorkingChunk(MIN_int64)) {}
	}

	/** Mirrors native layer count and accounts for coverage discarded by a configuration shrink. */
	void ResizeLoadedLayers(const int32 Count)
	{
		DirectoryChanges.SetNum(FMath::Max(DirectoryChanges.Num(), Directory.Num()));
		for (int32 Level = Count; Level < Directory.Num(); ++Level)
			DirectoryChanges[Level].Removed += Directory[Level].Chunks.Num();
		if (Count < Directory.Num())
		{
			LastResetReason = TEXT("native layer count shrank");
			for (auto It = PendingCreatedChunks.CreateIterator(); It; ++It)
				if (It->Get<0>() >= Count) It.RemoveCurrent();
		}
		Directory.SetNum(Count);
	}

	/** Updates native lifetime authority. Only the first Created for a LOD/origin in this generation
	 * admits discovery; recreation restores coverage but never retries abandoned or completed discovery. */
	bool Observe(const FChunkKey& Key, const FIntVector Size, const bool bCreated)
	{
		if (Key.Get<0>() < 0 || Size.GetMin() <= 0) return false;
		if (!Directory.IsValidIndex(Key.Get<0>())) ResizeLoadedLayers(Key.Get<0>() + 1);
		FLayoutLoadedChunkLayer& Layer = Directory[Key.Get<0>()];
		Layer.ChunkSizeInBlocks = Size;
		if (FLayoutLoadedChunkState* Chunk = Layer.Chunks.Find(Key.Get<1>()))
		{
			const bool bChanged = bCreated && !Chunk->bCreated;
			if (bChanged)
			{
				++CreatedGrants;
				AdmitCreatedDiscovery(Key, *Chunk);
				Chunk->InputRevision = InputRevision;
			}
			else if (bCreated) ++DuplicateCreated;
			else if (!Chunk->bCreated) ++UpdatedOnly;
			Chunk->bCreated |= bCreated;
			bNeedsWork |= bChanged;
			WakeCoverageWaits(Key);
			return bChanged;
		}
		FLayoutLoadedChunkState Chunk;
		Chunk.bCreated = bCreated;
		Chunk.Lifetime = ++NextLifetime;
		Chunk.InputRevision = InputRevision;
		if (bCreated)
		{
			++CreatedGrants;
			AdmitCreatedDiscovery(Key, Chunk);
		}
		else ++UpdatedOnly;
		Layer.Chunks.Add(Key.Get<1>(), Chunk);
		DirectoryChanges.SetNum(FMath::Max(DirectoryChanges.Num(), Key.Get<0>() + 1));
		++DirectoryChanges[Key.Get<0>()].Added;
		bNeedsWork |= bCreated;
		WakeCoverageWaits(Key);
		return true;
	}

	/** Ends this native lifetime and its pending work, retaining generation-wide discovery history.
	 * Pending scan results cannot attach to a later recreation, nor can recreation enqueue another scan. */
	bool Forget(const FChunkKey& Key)
	{
		if (!Directory.IsValidIndex(Key.Get<0>()) || Directory[Key.Get<0>()].Chunks.Remove(Key.Get<1>()) == 0) return false;
		DirectoryChanges.SetNum(FMath::Max(DirectoryChanges.Num(), Key.Get<0>() + 1));
		++DirectoryChanges[Key.Get<0>()].Removed;
		WorkingChunks.Remove(Key);
		PendingCreatedChunks.Remove(Key);
		for (auto It = Areas.CreateIterator(); It; ++It)
		{
			if (It.Value().Owner != Key) continue;
			It.Value().bScanning = false;
			if (It.Value().InFlight.IsEmpty()) It.RemoveCurrent();
		}
		bNeedsWork = true;
		return true;
	}

	/** Cumulative mutation counts distinguish membership replacement from stable totals.
	 * Input revisions fence unfinished captures; generation increments identify coverage resets.
	 * Call only at a bounded diagnostic cadence: screening counts inspect pending Created work. */
	FString DescribeBookkeeping() const
	{
		int32 Loaded = 0, Screening = 0;
		FString Layers;
		for (int32 Level = 0; Level < FMath::Max(Directory.Num(), DirectoryChanges.Num()); ++Level)
		{
			const int32 Count = Directory.IsValidIndex(Level) ? Directory[Level].Chunks.Num() : 0;
			Loaded += Count;
			Layers += FString::Printf(TEXT(" L%d=%d(+%llu/-%llu)"), Level, Count,
				DirectoryChanges.IsValidIndex(Level) ? DirectoryChanges[Level].Added : 0,
				DirectoryChanges.IsValidIndex(Level) ? DirectoryChanges[Level].Removed : 0);
		}
		for (const FChunkKey& Key : PendingCreatedChunks)
			if (const FLayoutLoadedChunkState* Chunk = FindChunk(Key))
				Screening += Chunk->Screening == ELayoutChunkScreening::Pending
					|| Chunk->Screening == ELayoutChunkScreening::Uncertain;
		int32 Backlog = 0, Exhausted = 0;
		GetCounts(Backlog, Exhausted);
		int32 Parked = 0;
		for (const auto& Pair : Areas)
			Parked += !Pair.Value.bCompleted && !Pair.Value.bQueued && !Pair.Value.bScanning;
		return FString::Printf(TEXT("generation=%llu inputRevision=%llu reset=%s loaded=%d backlog=%d screening=%d frontier=%d working=%d parked=%d created=%llu duplicateCreated=%llu updatedOnly=%llu completed=%llu retired=%llu discoveryAdmitted=%d recreatedDiscoverySkipped=%llu%s"),
			Generation, InputRevision, *LastResetReason, Loaded, Backlog, Screening, Areas.Num(), WorkingChunks.Num(),
			Parked, CreatedGrants, DuplicateCreated, UpdatedOnly, CompletedTraversals, RetiredTraversals,
			DiscoveryAdmittedChunks.Num(), RecreatedDiscoverySkipped, *Layers);
	}

	/** Selects one nearest useful native chunk per rotating center turn. No per-player queues or captures. */
	bool TakeNext(const TArray<FIntVector>& Centers, FIntPoint& OutArea)
	{
		if (Centers != LastCenters)
		{
			LastCenters = Centers;
			bNeedsWork = true;
		}
		if (!bNeedsWork || Centers.IsEmpty()) return false;
		bool bAdvanced = false;
		for (int32 Turn = 0; Turn < Centers.Num(); ++Turn)
		{
			const int32 CenterIndex = (NextCenter + Turn) % Centers.Num();
			const FIntVector Center = Centers[CenterIndex];
			TOptional<FChunkKey> Best;
			FIntPoint BestArea = FIntPoint::ZeroValue, BestStart = FIntPoint::ZeroValue;
			int64 BestOffset = 0, BestDistance = MAX_int64;
			bool bBestStartup = false;
			// Created callbacks alone enqueue work; loaded coverage is never searched for candidates.
			for (auto It = PendingCreatedChunks.CreateIterator(); It; ++It)
			{
				const FChunkKey Key = *It;
				FLayoutLoadedChunkState* Pending = FindChunk(Key);
				if (!Pending) { It.RemoveCurrent(); continue; }
				FLayoutLoadedChunkState& Chunk = *Pending;
				FIntPoint First, Last;
				GetChunkAreas(Key, First, Last);
				FIntPoint Start = Chunk.ScanStartArea;
				int64 Offset = Chunk.ScanOffset;
				if (!Chunk.bScanStarted)
				{
					const FIntPoint Desired = AreaAt(FIntPoint(Center.X, Center.Y));
					Start = FIntPoint(FMath::Clamp(Desired.X, First.X, Last.X), FMath::Clamp(Desired.Y, First.Y, Last.Y));
					if (!Chunk.bScanStarted || Start != Chunk.ScanStartArea) Offset = 0;
				}
				const int64 Columns = int64(Last.X) - First.X + 1;
				const int64 Count = Columns * (int64(Last.Y) - First.Y + 1);
				if (Offset >= Count)
				{
					Settle(Key, Chunk);
					WorkingChunks.Remove(Key);
					It.RemoveCurrent();
					continue;
				}
				const int64 Index = ((int64(Start.Y) - First.Y) * Columns + Start.X - First.X + Offset) % Count;
				const FIntPoint Area(int32(First.X + Index % Columns), int32(First.Y + Index / Columns));
				if (const FState* State = Areas.Find(Area))
				{
					const bool bSameOwner = State->Owner == Key && State->OwnerLifetime == Chunk.Lifetime;
					if ((bSameOwner && State->bCompleted) || State->Failures.Num() >= 3)
					{
						Chunk.ScanStartArea = Start;
						Chunk.ScanOffset = Offset + 1;
						Chunk.bScanStarted = true;
						bAdvanced = true;
						continue;
					}
					if (State->bScanning || State->Failures.Num() + State->InFlight.Num() >= 3) continue;
					if (bSameOwner && !State->bQueued) continue;
					if (!bSameOwner && (!State->InFlight.IsEmpty() || (!State->bCompleted && IsOwnerCurrent(*State)))) continue;
				}
				const int64 Distance = DistanceToChunk(Key, Center);
				const bool bStartup = IsStartupKey(Key);
				if (!Best.IsSet() || (bStartup != bBestStartup ? bStartup
					: Distance < BestDistance || (Distance == BestDistance && KeyBefore(Key, Best.GetValue()))))
				{
					bBestStartup = bStartup;
					Best = Key;
					BestArea = Area;
					BestStart = Start;
					BestOffset = Offset;
					BestDistance = Distance;
				}
			}
			if (!Best.IsSet()) continue;
			FLayoutLoadedChunkState* Chunk = FindChunk(Best.GetValue());
			if (Chunk->Screening == ELayoutChunkScreening::Eligible && !Promote(Best.GetValue())) continue;
			if (!Areas.Contains(BestArea) && !MakeFrontierRoom()) return false;
			Chunk->ScanStartArea = BestStart;
			Chunk->ScanOffset = BestOffset;
			Chunk->bScanStarted = true;
			FState& State = Areas.FindOrAdd(BestArea);
			State.Owner = Best.GetValue();
			State.OwnerLifetime = Chunk->Lifetime;
			State.OwnerStart = BestStart;
			State.OwnerOffset = BestOffset;
			State.ScanId = ++NextScanId;
			State.bScanning = true;
			State.bQueued = false;
			State.bCompleted = State.bDeferred = State.bWaitingCoverage = State.bWakeDuringScan = State.bCapacityReleasedDuringScan = false;
			State.BlockingReservations.Reset();
			OutArea = BestArea;
			SelectedCenter = Center;
			NextCenter = (CenterIndex + 1) % Centers.Num();
			return true;
		}
		bNeedsWork = bAdvanced;
		return false;
	}

	/** Positive worker evidence admits a working slot before expensive solve/template capture. */
	bool MarkEligible(const FIntPoint Area, const uint64 ScanId)
	{
		if (!IsCurrentScan(Area, ScanId)) return false;
		FState& State = Areas.FindChecked(Area);
		FindChunk(State.Owner)->Screening = ELayoutChunkScreening::Eligible;
		return Promote(State.Owner);
	}

	/** Only a proven missing eligible binding/row is a global negative; negative point samples are not. */
	void MarkIrrelevant(const FIntPoint Area, const uint64 ScanId)
	{
		if (!IsCurrentScan(Area, ScanId)) return;
		const FChunkKey Key = Areas.FindChecked(Area).Owner;
		FLayoutLoadedChunkState& Chunk = *FindChunk(Key);
		if (Chunk.Screening != ELayoutChunkScreening::Irrelevant) ++CompletedTraversals;
		Chunk.Screening = ELayoutChunkScreening::Irrelevant;
		WorkingChunks.Remove(Key);
		PendingCreatedChunks.Remove(Key);
	}

	uint64 GetScanId(const FIntPoint Area) const
	{
		const FState* State = Areas.Find(Area);
		return State ? State->ScanId : 0;
	}

	/** Both scan and native lifetime must still match; input reset removes the frontier itself. */
	bool IsCurrentScan(const FIntPoint Area, const uint64 ScanId) const
	{
		const FState* State = Areas.Find(Area);
		const FLayoutLoadedChunkState* Chunk = State ? FindChunk(State->Owner) : nullptr;
		return State && State->bScanning && State->ScanId == ScanId && Chunk
			&& Chunk->bCreated && Chunk->Lifetime == State->OwnerLifetime && Chunk->InputRevision == InputRevision;
	}

	/** Completes one bounded area, not an entire native chunk. Deferred work keeps its cursor. */
	void FinishScan(const FIntPoint Area, const bool bDeferred, const uint64 ScanId, const bool bCanceled = false)
	{
		if (!IsCurrentScan(Area, ScanId)) return;
		FState& State = Areas.FindChecked(Area);
		State.bScanning = false;
		State.bDeferred = bDeferred;
		// Preserve capacity released before publication without polling genuinely blocked work
		// or reopening a completed empty scan.
		State.bQueued = bCanceled || State.bWakeDuringScan || (bDeferred && State.bCapacityReleasedDuringScan);
		State.bWakeDuringScan = State.bCapacityReleasedDuringScan = false;
		State.bCompleted = (!State.bQueued && !bDeferred && !State.bWaitingCoverage && State.BlockingReservations.IsEmpty())
			|| State.Failures.Num() >= 3;
		FLayoutLoadedChunkState& Chunk = *FindChunk(State.Owner);
		if (Chunk.Screening == ELayoutChunkScreening::Pending) Chunk.Screening = ELayoutChunkScreening::Uncertain;
		if (State.bCompleted && Chunk.ScanStartArea == State.OwnerStart && Chunk.ScanOffset == State.OwnerOffset)
		{
			++Chunk.ScanOffset;
			FIntPoint First, Last;
			GetChunkAreas(State.Owner, First, Last);
			if (Chunk.ScanOffset >= (int64(Last.X) - First.X + 1) * (int64(Last.Y) - First.Y + 1))
			{
				Settle(State.Owner, Chunk);
				WorkingChunks.Remove(State.Owner);
				PendingCreatedChunks.Remove(State.Owner);
			}
		}
		bNeedsWork = true;
	}

	/** Captures the selected native lifetime, independently of its discovery completion. */
	FLayoutCreatedChunkIdentity GetScanOrigin(const FIntPoint Area) const
	{
		const FState* State = Areas.Find(Area);
		return State ? FLayoutCreatedChunkIdentity{State->Owner.Get<0>(), State->Owner.Get<1>(), State->OwnerLifetime}
			: FLayoutCreatedChunkIdentity{};
	}

	/** Inclusive selected-owner bounds clip candidate centers only, never solved footprints. */
	bool GetScanBounds(const FIntPoint Area, FIntVector& Min, FIntVector& Max) const
	{
		const FLayoutCreatedChunkIdentity Origin = GetScanOrigin(Area);
		if (!Origin.IsCurrent(Directory)) return false;
		FIntPoint AreaMin, AreaMax;
		GetBounds(Area, AreaMin, AreaMax);
		Min = Origin.Origin;
		const FIntVector Size = Directory[Origin.DetailLevel].ChunkSizeInBlocks;
		for (int32 Axis = 0; Axis < 3; ++Axis) Max[Axis] = Clamp(int64(Min[Axis]) + Size[Axis] - 1);
		Min.X = FMath::Max(Min.X, AreaMin.X);
		Min.Y = FMath::Max(Min.Y, AreaMin.Y);
		Max.X = FMath::Min(Max.X, AreaMax.X);
		Max.Y = FMath::Min(Max.Y, AreaMax.Y);
		return true;
	}

	/** Absent coverage parks work; observed restored coverage is instead a terminal exclusion. */
	void MarkCoverageWaiting(const FIntPoint Area)
	{
		if (FState* State = Areas.Find(Area)) State->bWaitingCoverage = true;
	}

	/** A dependency already changed while a worker captured it; retry the unfinished area once. */
	void RequestRetry(const FIntPoint Area)
	{
		if (FState* State = Areas.Find(Area)) Wake(*State);
	}

	/** Called when retained automatic owner capacity is released, including after application. */
	void NotifyCapacityAvailable()
	{
		for (auto& Pair : Areas) if (Pair.Value.bDeferred) Wake(Pair.Value);
	}

	/** Track only unresolved reservations, never committed/permanent exclusions. */
	void MarkBlockedByReservation(const FIntPoint Area, const FString& RecordKey)
	{
		if (FState* State = Areas.Find(Area)) State->BlockingReservations.Add(RecordKey);
	}

	/** Releasing a root wakes its affected frontier, not every blocked region. */
	void NotifyReservationReleased(const FString& RecordKey)
	{
		for (auto& Pair : Areas)
		{
			FState& State = Pair.Value;
			if (State.BlockingReservations.Remove(RecordKey) > 0) Wake(State);
		}
	}

	/** Reserves the unchanged three-failure allowance before any root preparation. */
	bool BeginAttempt(const FIntPoint Area, const FString& RecordKey)
	{
		FState* State = Areas.Find(Area);
		if (!State || State->Failures.Contains(RecordKey) || State->InFlight.Contains(RecordKey)
			|| State->Failures.Num() + State->InFlight.Num() >= 3) return false;
		State->InFlight.Add(RecordKey);
		return true;
	}

	/** Success/cancellation return allowance. Only actual failures enter the retained no-repeat set. */
	void FinishAttempt(const FIntPoint Area, const FString& RecordKey, const bool bFailed)
	{
		// Success makes an exclusion permanent; failure/cancellation removes it. Both settle a wait.
		NotifyReservationReleased(RecordKey);
		FState* State = Areas.Find(Area);
		if (!State || State->InFlight.Remove(RecordKey) == 0) return;
		if (bFailed) State->Failures.Add(RecordKey);
		for (auto& Pair : Areas)
		{
			FState& Candidate = Pair.Value;
			// Publication consumes this edge only if it reports deferred work.
			Candidate.bCapacityReleasedDuringScan |= Candidate.bScanning;
			if (Candidate.bDeferred) Wake(Candidate);
		}
		bNeedsWork = true;
	}

	bool HasFailed(const FIntPoint Area, const FString& RecordKey) const
	{
		const FState* State = Areas.Find(Area);
		return State && State->Failures.Contains(RecordKey);
	}

	/** Inclusive canonical center ownership. Callers sample an authored-footprint halo without clipping layout geometry. */
	void GetBounds(const FIntPoint Area, FIntPoint& Min, FIntPoint& Max) const
	{
		const int64 Width = int64(SampleSpacing) * 4;
		Min = FIntPoint(Clamp(int64(Area.X) * Width), Clamp(int64(Area.Y) * Width));
		Max = FIntPoint(Clamp((int64(Area.X) + 1) * Width - 1), Clamp((int64(Area.Y) + 1) * Width - 1));
	}

	void GetCounts(int32& Queued, int32& Exhausted) const
	{
		Queued = Exhausted = 0;
		TSet<FChunkKey> Parked;
		for (const auto& Pair : Areas)
			if (!Pair.Value.bCompleted && !Pair.Value.bQueued && !Pair.Value.bScanning) Parked.Add(Pair.Value.Owner);
		for (const FChunkKey& Key : PendingCreatedChunks)
			if (!Parked.Contains(Key)) ++Queued;
		for (const auto& Pair : Areas) if (Pair.Value.Failures.Num() >= 3) ++Exhausted;
	}

	/** Readiness inspects only the unprocessed cursor suffix, including active/parked areas.
	 * Completed nearby areas never wait for a coarse owner's distant remainder. Reach still
	 * includes writes extending beyond candidate-center ownership; submitted roots are tracked separately. */
	bool HasPendingCreatedWork(const FBox& BoundsInBlocks, const FVector& ReachInBlocks, const int32 FirstRequiredDetailLevel) const
	{
		for (const FChunkKey& Key : PendingCreatedChunks)
		{
			if (Key.Get<0>() < FirstRequiredDetailLevel) continue;
			const FLayoutLoadedChunkState* Chunk = FindChunk(Key);
			if (!Chunk) continue;
			const FVector Min(Key.Get<1>());
			const FVector Max = Min + FVector(Directory[Key.Get<0>()].ChunkSizeInBlocks) - FVector(1.0);
			const FBox Query = BoundsInBlocks.ExpandBy(ReachInBlocks).Overlap(FBox(Min, Max));
			if (!Query.IsValid) continue;
			if (!Chunk->bScanStarted) return true;

			FIntPoint First, Last;
			GetChunkAreas(Key, First, Last);
			const int64 Columns = int64(Last.X) - First.X + 1;
			const int64 Count = Columns * (int64(Last.Y) - First.Y + 1);
			if (Chunk->ScanOffset >= Count) continue;
			const int64 Width = int64(SampleSpacing) * 4;
			const int64 Left = FMath::FloorToInt64(Query.Min.X / Width) - First.X;
			const int64 Right = FMath::FloorToInt64(Query.Max.X / Width) - First.X;
			const int64 Top = FMath::FloorToInt64(Query.Min.Y / Width) - First.Y;
			const int64 Bottom = FMath::FloorToInt64(Query.Max.Y / Width) - First.Y;
			const int64 Start = (int64(Chunk->ScanStartArea.Y) - First.Y) * Columns + Chunk->ScanStartArea.X - First.X;
			const int64 Begin = (Start + Chunk->ScanOffset) % Count;
			const int64 End = Begin + Count - Chunk->ScanOffset - 1;
			// Intersect a row-major interval with the query rectangle in constant time.
			const auto Intersects = [&](const int64 Lower, const int64 Upper)
			{
				int64 Row = FMath::Max(Top, Lower / Columns);
				int64 Column = FMath::Max(Left, Lower - Row * Columns);
				if (Column > Right) { ++Row; Column = Left; }
				return Row <= Bottom && Row * Columns + Column <= Upper;
			};
			if (Intersects(Begin, FMath::Min(End, Count - 1)) || (End >= Count && Intersects(0, End - Count))) return true;
		}
		return false;
	}

	/** Rejected diagnostics live only as long as the bounded canonical failure allowance. */
	void AppendRetainedFailureKeys(TSet<FString>& Keys) const
	{
		for (const auto& Pair : Areas) Keys.Append(Pair.Value.Failures);
	}

	int32 GetWorkingCount() const { return WorkingChunks.Num(); }
	int32 GetFrontierCount() const { return Areas.Num(); }
	uint64 GetEvictedCount() const { return EvictedChunks; }
	FIntVector GetSelectedCenter() const { return SelectedCenter; }

private:
	struct FState
	{
		FChunkKey Owner{INDEX_NONE, FIntVector::ZeroValue};
		uint64 OwnerLifetime = 0, ScanId = 0;
		FIntPoint OwnerStart = FIntPoint::ZeroValue;
		int64 OwnerOffset = 0;
		bool bQueued = true, bScanning = false, bDeferred = false, bCompleted = false;
		TSet<FString> BlockingReservations;
		bool bWaitingCoverage = false, bWakeDuringScan = false, bCapacityReleasedDuringScan = false;
		TSet<FString> Failures, InFlight;
	};

	/** Consumes discovery admission before any worker starts. Delete/eviction may abandon that attempt;
	 * a later Created lifetime still retains write authority, but must not refill discovery or screening. */
	void AdmitCreatedDiscovery(const FChunkKey& Key, FLayoutLoadedChunkState& Chunk)
	{
		if (DiscoveryAdmittedChunks.Contains(Key))
		{
			Chunk.Screening = ELayoutChunkScreening::Settled;
			++RecreatedDiscoverySkipped;
			return;
		}
		DiscoveryAdmittedChunks.Add(Key);
		PendingCreatedChunks.Add(Key);
	}

	void Settle(const FChunkKey& Key, FLayoutLoadedChunkState& Chunk, const bool bRetired = false)
	{
		if (Chunk.Screening == ELayoutChunkScreening::Settled || Chunk.Screening == ELayoutChunkScreening::Irrelevant) return;
		Chunk.Screening = ELayoutChunkScreening::Settled;
		if (bRetired) ++RetiredTraversals;
		else ++CompletedTraversals;
	}
	bool IsOwnerCurrent(const FState& State) const
	{
		const FLayoutLoadedChunkState* Chunk = FindChunk(State.Owner);
		return Chunk && Chunk->Lifetime == State.OwnerLifetime
			&& Chunk->Screening != ELayoutChunkScreening::Settled && Chunk->Screening != ELayoutChunkScreening::Irrelevant;
	}
	void Wake(FState& State)
	{
		if (State.bCompleted || State.Failures.Num() >= 3 || !IsOwnerCurrent(State)) return;
		State.bWakeDuringScan |= State.bScanning;
		State.bQueued = true;
		bNeedsWork = true;
	}

	static int32 Clamp(const int64 Value) { return int32(FMath::Clamp<int64>(Value, MIN_int32, MAX_int32)); }
	FIntPoint AreaAt(const FIntPoint Position) const
	{
		const double Width = double(SampleSpacing) * 4;
		return FIntPoint(FMath::FloorToInt(double(Position.X) / Width), FMath::FloorToInt(double(Position.Y) / Width));
	}
	FLayoutLoadedChunkState* FindChunk(const FChunkKey& Key) const
	{
		return Directory.IsValidIndex(Key.Get<0>()) ? Directory[Key.Get<0>()].Chunks.Find(Key.Get<1>()) : nullptr;
	}
	void GetChunkAreas(const FChunkKey& Key, FIntPoint& First, FIntPoint& Last) const
	{
		const FIntVector Size = Directory[Key.Get<0>()].ChunkSizeInBlocks;
		const FIntVector Origin = Key.Get<1>();
		First = AreaAt(FIntPoint(Origin.X, Origin.Y));
		Last = AreaAt(FIntPoint(Clamp(int64(Origin.X) + Size.X - 1), Clamp(int64(Origin.Y) + Size.Y - 1)));
	}
	/** Coverage arrival wakes only unfinished missing-coverage waits; completion never reopens. */
	void WakeCoverageWaits(const FChunkKey& Key)
	{
		FIntPoint First, Last;
		GetChunkAreas(Key, First, Last);
		for (auto& Pair : Areas)
		{
			if (Pair.Key.X < First.X || Pair.Key.X > Last.X || Pair.Key.Y < First.Y || Pair.Key.Y > Last.Y) continue;
			FState& State = Pair.Value;
			if (State.bWaitingCoverage) Wake(State);
		}
	}

	static bool KeyBefore(const FChunkKey& A, const FChunkKey& B)
	{
		if (A.Get<0>() != B.Get<0>()) return A.Get<0>() > B.Get<0>();
		const FIntVector X = A.Get<1>(), Y = B.Get<1>();
		return X.X != Y.X ? X.X < Y.X : (X.Y != Y.Y ? X.Y < Y.Y : X.Z < Y.Z);
	}
	int64 DistanceToChunk(const FChunkKey& Key, const FIntVector Center) const
	{
		const FIntVector Origin = Key.Get<1>(), Size = Directory[Key.Get<0>()].ChunkSizeInBlocks;
		int64 Distance = 0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
			Distance = FMath::Max(Distance, FMath::Max(int64(Origin[Axis]) - Center[Axis],
				int64(Center[Axis]) - (int64(Origin[Axis]) + Size[Axis] - 1)));
		return Distance;
	}
	int64 PriorityDistance(const FChunkKey& Key) const
	{
		int64 Distance = MAX_int64;
		for (const FIntVector Center : LastCenters) Distance = FMath::Min(Distance, DistanceToChunk(Key, Center));
		return IsStartupKey(Key) ? MIN_int64 / 2 + FMath::Min<int64>(Distance, MAX_int32) : Distance;
	}
	bool Promote(const FChunkKey& Key)
	{
		if (WorkingChunks.Contains(Key)) return true;
		if (WorkingChunks.Num() >= MaxCachedChunks && !EvictWorkingChunk(PriorityDistance(Key))) return false;
		WorkingChunks.Add(Key);
		return true;
	}
	bool EvictWorkingChunk(const int64 IncomingDistance)
	{
		TOptional<FChunkKey> Victim;
		int64 Distance = IncomingDistance;
		for (const FChunkKey& Key : WorkingChunks)
		{
			if (IsStartupKey(Key)) continue;
			bool bBusy = false;
			for (const auto& Pair : Areas)
				bBusy |= Pair.Value.Owner == Key && (Pair.Value.bScanning || !Pair.Value.InFlight.IsEmpty());
			if (bBusy) continue;
			const int64 CandidateDistance = PriorityDistance(Key);
			if (CandidateDistance > Distance || (CandidateDistance == Distance && (!Victim.IsSet() || KeyBefore(Key, Victim.GetValue()))))
			{
				Victim = Key;
				Distance = CandidateDistance;
			}
		}
		if (!Victim.IsSet()) return false;
		WorkingChunks.Remove(Victim.GetValue());
		if (FLayoutLoadedChunkState* Chunk = FindChunk(Victim.GetValue())) Settle(Victim.GetValue(), *Chunk, true);
		PendingCreatedChunks.Remove(Victim.GetValue());
		++EvictedChunks;
		return true;
	}
	bool MakeFrontierRoom()
	{
		if (Areas.Num() < MaxCachedChunks + 1) return true;
		TOptional<FIntPoint> Victim;
		uint64 Oldest = MAX_uint64;
		for (const auto& Pair : Areas)
		{
			if (!Pair.Value.bScanning && Pair.Value.InFlight.IsEmpty() && Pair.Value.ScanId < Oldest
				&& (Pair.Value.bCompleted || !IsStartupKey(Pair.Value.Owner)))
			{
				Victim = Pair.Key;
				Oldest = Pair.Value.ScanId;
			}
		}
		if (!Victim.IsSet()) return false;
		const FState& State = Areas.FindChecked(Victim.GetValue());
		if (!State.bCompleted && IsOwnerCurrent(State))
		{
			Settle(State.Owner, *FindChunk(State.Owner), true);
			WorkingChunks.Remove(State.Owner);
			PendingCreatedChunks.Remove(State.Owner);
		}
		Areas.Remove(Victim.GetValue());
		return true;
	}

	struct FDirectoryChanges { uint64 Added = 0, Removed = 0; };
	TArray<FDirectoryChanges> DirectoryChanges;
	uint64 Generation = 0;
	FString LastResetReason = TEXT("none");
	TArray<FLayoutLoadedChunkLayer>& Directory;
	/** Generation-wide admission ledger, populated only by Created. Never evict entries on unload or cache pressure:
	 * exact once-only discovery requires memory proportional to unique LOD/origins visited until generation reset. */
	TSet<FChunkKey> DiscoveryAdmittedChunks;
	TArray<FBox> StartupBounds;
	int32 StartupFirstDetailLevel = 0;
	bool IsStartupKey(const FChunkKey& Key) const
	{
		if (Key.Get<0>() < StartupFirstDetailLevel || !Directory.IsValidIndex(Key.Get<0>())) return false;
		const FVector Min(Key.Get<1>());
		const FBox ChunkBounds(Min, Min + FVector(Directory[Key.Get<0>()].ChunkSizeInBlocks) - FVector(1.0));
		for (const FBox& Bounds : StartupBounds) if (Bounds.Intersect(ChunkBounds)) return true;
		return false;
	}
	uint64 RecreatedDiscoverySkipped = 0;
	/** Event-owned discovery backlog, removed on completion/retirement; never rebuilt from coverage. */
	TSet<FChunkKey> PendingCreatedChunks;
	TSet<FChunkKey> WorkingChunks;
	TMap<FIntPoint, FState> Areas;
	TArray<FIntVector> LastCenters;
	FIntVector SelectedCenter = FIntVector::ZeroValue;
	uint64 InputRevision = 1, NextLifetime = 0, NextScanId = 0, EvictedChunks = 0;
	uint64 CreatedGrants = 0, DuplicateCreated = 0, UpdatedOnly = 0, CompletedTraversals = 0, RetiredTraversals = 0;
	int32 SampleSpacing = 1, MaxCachedChunks = 256, NextCenter = 0;
	bool bNeedsWork = false;
};
