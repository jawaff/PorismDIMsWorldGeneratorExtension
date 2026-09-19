// Copyright 2026 Spotted Loaf Studio
#pragma once

#include "CoreMinimal.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"

/** Game-thread bookkeeping over the component-owned loaded directory.
 * Native chunks own resumable scan cursors, not layout boundaries or quotas. The
 * bounded canonical frontier shares failure/no-repeat state across overlapping LODs.
 * Workers capture values only; this queue and its borrowed directory never cross threads.
 */
class FLayoutPlanningAreaQueue
{
public:
	using FChunkKey = TTuple<int32, FIntVector>;

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
	void Reset(const TCHAR* Reason = TEXT("input invalidation"))
	{
		LastResetReason = Reason;
		RetireWorkingSet();
		++InputRevision;
		for (FLayoutLoadedChunkLayer& Layer : Directory)
		for (auto& Pair : Layer.Chunks)
		{
			Pair.Value.Screening = ELayoutChunkScreening::Pending;
			Pair.Value.InputRevision = InputRevision;
			Pair.Value.ScanOffset = 0;
			Pair.Value.bScanStarted = false;
		}
		bNeedsWork = true;
	}

	/** Ends generation coverage; monotonic lifetime/scan identities still fence old callbacks. */
	void ResetLoadedDirectory()
	{
		Reset(TEXT("generation lifetime"));
		++Generation;
		DirectoryChanges.SetNum(FMath::Max(DirectoryChanges.Num(), Directory.Num()));
		for (int32 Level = 0; Level < Directory.Num(); ++Level)
			DirectoryChanges[Level].Removed += Directory[Level].Chunks.Num();
		Directory.Reset();
		EvictedChunks = 0;
	}

	/** No-center/disabled cleanup drops automatic work, not still-loaded terrain or settled summaries. */
	void RetireWorkingSet()
	{
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
			Reset(TEXT("sample spacing"));
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
		if (Count < Directory.Num()) LastResetReason = TEXT("native layer count shrank");
		Directory.SetNum(Count);
	}

	/** Updates one native observation. Repeated Created/Updated notifications do not restart a settled scan. */
	bool Observe(const FChunkKey& Key, const FIntVector Size, const bool bCreated)
	{
		if (Key.Get<0>() < 0 || Size.GetMin() <= 0) return false;
		if (!Directory.IsValidIndex(Key.Get<0>())) ResizeLoadedLayers(Key.Get<0>() + 1);
		FLayoutLoadedChunkLayer& Layer = Directory[Key.Get<0>()];
		Layer.ChunkSizeInBlocks = Size;
		if (FLayoutLoadedChunkState* Chunk = Layer.Chunks.Find(Key.Get<1>()))
		{
			const bool bChanged = bCreated && !Chunk->bCreated;
			Chunk->bCreated |= bCreated;
			bNeedsWork |= bChanged;
			if (bChanged) ReopenAreasForCoverage(Key);
			return bChanged;
		}
		FLayoutLoadedChunkState Chunk;
		Chunk.bCreated = bCreated;
		Chunk.Lifetime = ++NextLifetime;
		Chunk.InputRevision = InputRevision;
		Layer.Chunks.Add(Key.Get<1>(), Chunk);
		DirectoryChanges.SetNum(FMath::Max(DirectoryChanges.Num(), Key.Get<0>() + 1));
		++DirectoryChanges[Key.Get<0>()].Added;
		bNeedsWork |= bCreated;
		if (bCreated) ReopenAreasForCoverage(Key);
		return true;
	}

	/** Removes just this LOD's lifetime. Pending scan results cannot attach to a later recreation. */
	bool Forget(const FChunkKey& Key)
	{
		if (!Directory.IsValidIndex(Key.Get<0>()) || Directory[Key.Get<0>()].Chunks.Remove(Key.Get<1>()) == 0) return false;
		DirectoryChanges.SetNum(FMath::Max(DirectoryChanges.Num(), Key.Get<0>() + 1));
		++DirectoryChanges[Key.Get<0>()].Removed;
		WorkingChunks.Remove(Key);
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
	 * Input revisions reopen screening; generation increments specifically identify coverage resets.
	 * Call only at a bounded diagnostic cadence: screening counts inspect loaded metadata. */
	FString DescribeBookkeeping() const
	{
		int32 Loaded = 0, Screening = 0;
		FString Layers;
		for (int32 Level = 0; Level < FMath::Max(Directory.Num(), DirectoryChanges.Num()); ++Level)
		{
			const int32 Count = Directory.IsValidIndex(Level) ? Directory[Level].Chunks.Num() : 0;
			Loaded += Count;
			if (Directory.IsValidIndex(Level))
				for (const auto& Pair : Directory[Level].Chunks)
					Screening += Pair.Value.bCreated && (Pair.Value.Screening == ELayoutChunkScreening::Pending
						|| Pair.Value.Screening == ELayoutChunkScreening::Uncertain);
			Layers += FString::Printf(TEXT(" L%d=%d(+%llu/-%llu)"), Level, Count,
				DirectoryChanges.IsValidIndex(Level) ? DirectoryChanges[Level].Added : 0,
				DirectoryChanges.IsValidIndex(Level) ? DirectoryChanges[Level].Removed : 0);
		}
		int32 Backlog = 0, Exhausted = 0;
		GetCounts(Backlog, Exhausted);
		return FString::Printf(TEXT("generation=%llu inputRevision=%llu reset=%s loaded=%d backlog=%d screening=%d frontier=%d working=%d%s"),
			Generation, InputRevision, *LastResetReason, Loaded, Backlog, Screening, Areas.Num(), WorkingChunks.Num(), *Layers);
	}

	/** Selects one nearest useful native chunk per rotating center turn. No per-player queues or captures. */
	bool TakeNext(const TArray<FIntVector>& Centers, FIntPoint& OutArea)
	{
		if (Centers != LastCenters)
		{
			LastCenters = Centers;
			PriorityRefreshTurns = Centers.Num();
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
			// ponytail: linear current-loaded metadata scan; index only if center polling measurably costs frames.
			for (int32 Level = Directory.Num() - 1; Level >= 0; --Level)
			for (auto& Pair : Directory[Level].Chunks)
			{
				FLayoutLoadedChunkState& Chunk = Pair.Value;
				if (!Chunk.bCreated || Chunk.Screening == ELayoutChunkScreening::Irrelevant
					|| Chunk.Screening == ELayoutChunkScreening::Settled) continue;
				const FChunkKey Key(Level, Pair.Key);
				FIntPoint First, Last;
				GetChunkAreas(Key, First, Last);
				FIntPoint Start = Chunk.ScanStartArea;
				int64 Offset = Chunk.ScanOffset;
				if (!Chunk.bScanStarted || (PriorityRefreshTurns > 0 && WorkingChunks.Contains(Key)))
				{
					const FIntPoint Desired = AreaAt(FIntPoint(Center.X, Center.Y));
					Start = FIntPoint(FMath::Clamp(Desired.X, First.X, Last.X), FMath::Clamp(Desired.Y, First.Y, Last.Y));
					if (!Chunk.bScanStarted || Start != Chunk.ScanStartArea) Offset = 0;
				}
				const int64 Columns = int64(Last.X) - First.X + 1;
				const int64 Count = Columns * (int64(Last.Y) - First.Y + 1);
				if (Offset >= Count)
				{
					Chunk.Screening = ELayoutChunkScreening::Settled;
					WorkingChunks.Remove(Key);
					continue;
				}
				const int64 Index = ((int64(Start.Y) - First.Y) * Columns + Start.X - First.X + Offset) % Count;
				const FIntPoint Area(int32(First.X + Index % Columns), int32(First.Y + Index / Columns));
				if (const FState* State = Areas.Find(Area))
				{
					if (State->bCompleted || State->Failures.Num() >= 3)
					{
						Chunk.ScanStartArea = Start;
						Chunk.ScanOffset = Offset + 1;
						Chunk.bScanStarted = true;
						bAdvanced = true;
						continue;
					}
					if (State->bScanning || !State->bQueued || State->Failures.Num() + State->InFlight.Num() >= 3) continue;
				}
				const int64 Distance = DistanceToChunk(Key, Center);
				if (!Best.IsSet() || Distance < BestDistance || (Distance == BestDistance && KeyBefore(Key, Best.GetValue())))
				{
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
			State.BlockingReservations.Reset();
			OutArea = BestArea;
			SelectedCenter = Center;
			NextCenter = (CenterIndex + 1) % Centers.Num();
			PriorityRefreshTurns = FMath::Max(0, PriorityRefreshTurns - 1);
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
		FindChunk(Key)->Screening = ELayoutChunkScreening::Irrelevant;
		WorkingChunks.Remove(Key);
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
		State.bQueued = bDeferred || bCanceled || State.bCoverageChangedDuringScan;
		State.bCoverageChangedDuringScan = false;
		State.bCompleted = !State.bQueued || State.Failures.Num() >= 3;
		FLayoutLoadedChunkState& Chunk = *FindChunk(State.Owner);
		if (Chunk.Screening == ELayoutChunkScreening::Pending) Chunk.Screening = ELayoutChunkScreening::Uncertain;
		if (State.bCompleted && Chunk.ScanStartArea == State.OwnerStart && Chunk.ScanOffset == State.OwnerOffset) ++Chunk.ScanOffset;
		bNeedsWork = true;
	}

	/** Track only roots which actually rejected a proposal/proved this region excluded. */
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
			if (State.BlockingReservations.Remove(RecordKey) == 0 || State.Failures.Num() >= 3) continue;
			State.bCoverageChangedDuringScan |= State.bScanning;
			State.bQueued = true;
			State.bCompleted = false;
			if (FLayoutLoadedChunkState* Chunk = FindChunk(State.Owner); Chunk && Chunk->Lifetime == State.OwnerLifetime
				&& Chunk->ScanStartArea == State.OwnerStart && Chunk->ScanOffset > State.OwnerOffset)
			{
				Chunk->ScanOffset = State.OwnerOffset;
				if (Chunk->Screening == ELayoutChunkScreening::Settled) Chunk->Screening = ELayoutChunkScreening::Uncertain;
			}
			bNeedsWork = true;
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
	void FinishAttempt(const FIntPoint Area, const FString& RecordKey, const bool bFailed, const bool bReservationReleased = false)
	{
		if (bFailed || bReservationReleased) NotifyReservationReleased(RecordKey);
		FState* State = Areas.Find(Area);
		if (!State || State->InFlight.Remove(RecordKey) == 0) return;
		if (bFailed) State->Failures.Add(RecordKey);
		for (auto& Pair : Areas)
		{
			FState& Candidate = Pair.Value;
			if (!Candidate.bDeferred) continue;
			if (Candidate.Failures.Num() >= 3) continue;
			Candidate.bQueued = true;
			Candidate.bCompleted = false;
			if (FLayoutLoadedChunkState* Chunk = FindChunk(Candidate.Owner); Chunk && Chunk->Lifetime == Candidate.OwnerLifetime
				&& Chunk->ScanStartArea == Candidate.OwnerStart && Chunk->ScanOffset > Candidate.OwnerOffset)
			{
				Chunk->ScanOffset = Candidate.OwnerOffset;
				Chunk->Screening = ELayoutChunkScreening::Eligible;
			}
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
		for (const FLayoutLoadedChunkLayer& Layer : Directory)
		for (const auto& Pair : Layer.Chunks)
			if (Pair.Value.bCreated && Pair.Value.Screening != ELayoutChunkScreening::Irrelevant
				&& Pair.Value.Screening != ELayoutChunkScreening::Settled) ++Queued;
		for (const auto& Pair : Areas) if (Pair.Value.Failures.Num() >= 3) ++Exhausted;
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
		bool bCoverageChangedDuringScan = false;
		TSet<FString> Failures, InFlight;
	};

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
	/** New eligible coverage invalidates completion only for intersecting retained areas.
	 * Keep failed keys and in-flight ownership; a running scan may publish, then revisit new coverage. */
	void ReopenAreasForCoverage(const FChunkKey& Key)
	{
		FIntPoint First, Last;
		GetChunkAreas(Key, First, Last);
		for (auto& Pair : Areas)
		{
			if (Pair.Key.X < First.X || Pair.Key.X > Last.X || Pair.Key.Y < First.Y || Pair.Key.Y > Last.Y) continue;
			FState& State = Pair.Value;
			if (State.Failures.Num() >= 3) continue;
			State.bCoverageChangedDuringScan |= State.bScanning;
			State.bCompleted = false;
			State.bQueued = true;
			if (FLayoutLoadedChunkState* Chunk = FindChunk(State.Owner);
				Chunk && Chunk->Lifetime == State.OwnerLifetime && Chunk->ScanStartArea == State.OwnerStart)
			{
				Chunk->ScanOffset = FMath::Min(Chunk->ScanOffset, State.OwnerOffset);
				if (Chunk->Screening == ELayoutChunkScreening::Settled) Chunk->Screening = ELayoutChunkScreening::Uncertain;
			}
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
		return Distance;
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
			if (!Pair.Value.bScanning && Pair.Value.InFlight.IsEmpty() && Pair.Value.ScanId < Oldest)
			{
				Victim = Pair.Key;
				Oldest = Pair.Value.ScanId;
			}
		}
		if (!Victim.IsSet()) return false;
		Areas.Remove(Victim.GetValue());
		return true;
	}

	struct FDirectoryChanges { uint64 Added = 0, Removed = 0; };
	TArray<FDirectoryChanges> DirectoryChanges;
	uint64 Generation = 0;
	FString LastResetReason = TEXT("none");
	TArray<FLayoutLoadedChunkLayer>& Directory;
	TSet<FChunkKey> WorkingChunks;
	TMap<FIntPoint, FState> Areas;
	TArray<FIntVector> LastCenters;
	FIntVector SelectedCenter = FIntVector::ZeroValue;
	uint64 InputRevision = 1, NextLifetime = 0, NextScanId = 0, EvictedChunks = 0;
	int32 SampleSpacing = 1, MaxCachedChunks = 256, NextCenter = 0, PriorityRefreshTurns = 0;
	bool bNeedsWork = false;
};
