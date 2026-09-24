// Copyright 2026 Spotted Loaf Studio

#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldExtended/ChunkWorldWalker.h"
#include "LayoutPlanningAreaQueue.h"
#include "LayoutStartupCoverage.h"
#include "Misc/ScopeLock.h"

void UChunkWorldLayoutRuntimeComponent::SetStartupCoverageRequired(UObject* Consumer, const bool bRequired,
	const TConstArrayView<TWeakObjectPtr<UObject>> Walkers)
{
	check(IsInGameThread());
	if (!Consumer) return;
	if (!StartupCoverage)
	{
		if (!bRequired) return;
		StartupCoverage = MakeShared<FLayoutStartupCoverage>();
	}
	if (bRequired) StartupCoverage->Consumers.Add(Consumer, TArray<TWeakObjectPtr<UObject>>(Walkers));
	else StartupCoverage->Consumers.Remove(Consumer);
	RefreshStartupCoverage();
}

int32 UChunkWorldLayoutRuntimeComponent::GetFirstReadinessDetailLevel() const
{
	AChunkWorldExtended* World = GetOwningChunkWorld();
	const int32 Layers = World && World->IsRunning() ? World->GetChunkLayerCount() : 0;
	return Layers > 0 ? Layers - FMath::Clamp(ReadinessLODCount, 1, Layers) : 0;
}

void UChunkWorldLayoutRuntimeComponent::InvalidateStartupCoverage()
{
	if (!StartupCoverage) return;
	StartupCoverage->Targets.Reset();
	StartupCoverage->Walkers.Reset();
	StartupCoverage->BoundsInBlocks.Reset();
	StartupCoverage->LayerShapes.Reset();
	StartupCoverage->FirstRequiredDetailLevel = INDEX_NONE;
	PlanningAreaQueue->SetStartupBounds({}, 0);
	++StartupCoverage->Revision;
}

void UChunkWorldLayoutRuntimeComponent::RefreshStartupCoverage()
{
	if (!PlanningAreaQueue) return;
	if (!StartupCoverage) StartupCoverage = MakeShared<FLayoutStartupCoverage>();
	auto& Coverage = *StartupCoverage;
	for (auto It = Coverage.Consumers.CreateIterator(); It; ++It)
		if (!It.Key().IsValid()) It.RemoveCurrent();
	AChunkWorldExtended* World = GetOwningChunkWorld();
	if (!World || !World->IsRunning() || !World->RuntimeConfig || World->WorldChunks.empty())
	{
		InvalidateStartupCoverage();
		return;
	}
	// Configuration is generation-owned. Read no live chunk, task, render or collision pointers.
	const int32 Layers = World->GetChunkLayerCount();
	const int32 FirstRequiredDetailLevel = GetFirstReadinessDetailLevel();
	const auto* Root = World->WorldChunks[0];
	const auto* Config = World->RuntimeConfig;
	const double BlockSize = Config->BaseBlockSize;
	const double EffectiveRange = World->FrameViewRangeFactorNow;
	if (!FMath::IsFinite(BlockSize) || BlockSize <= 0) { InvalidateStartupCoverage(); return; }
	TArray<FIntVector> Shapes;
	Shapes.Add(FIntVector(static_cast<int32>(Config->AxisBehaviorX), static_cast<int32>(Config->AxisBehaviorY), static_cast<int32>(Config->AxisBehaviorZ)));
	Shapes.Add(FIntVector(Config->Grid.Is2D(), Config->Grid.FlatAxis, Layers));
	for (const auto* Layer : World->WorldChunks)
	{
		Shapes.Add(Layer->ChunkBlockFactor);
		Shapes.Add(Layer->ChunkFactor);
		Shapes.Add(Layer->ChildGridSize);
		Shapes.Add(Layer->BlockCount);
	}
	TArray<UObject*> Registered;
	{
		std::lock_guard<std::mutex> Lock(World->WorldLoadersKey);
		Registered = World->WorldLoaders;
	}
	TArray<TWeakObjectPtr<UObject>> Walkers;
	TArray<FLayoutStartupCoverage::FTarget> Targets;
	for (UObject* Walker : Registered)
	{
		if (!IsValid(Walker) || !Walker->GetClass()->ImplementsInterface(UChunkWorldWalker::StaticClass())) continue;
		// The vendor interface's Execute_ wrappers are not DLL-exported. Dispatch its reflected
		// contract directly, with the same native-interface fallback used by generated wrappers.
		FVector Location = FVector::ZeroVector;
		if (UFunction* Function = Walker->FindFunction(TEXT("GetTracingLocation")))
		{
			struct { FVector ReturnValue = FVector::ZeroVector; } Params;
			Walker->ProcessEvent(Function, &Params);
			Location = Params.ReturnValue;
		}
		else if (const auto* Native = Cast<IChunkWorldWalker>(Walker)) Location = Native->GetTracingLocation_Implementation();
		const FVector Local = World->GetTransform().InverseTransformPosition(Location);
		if (Local.ContainsNaN()) { InvalidateStartupCoverage(); return; }
		TArray<double> Multipliers;
		if (UFunction* Function = Walker->FindFunction(TEXT("GetViewDistanceMultiplier")))
		{
			struct { TArray<double> ReturnValue; } Params;
			Walker->ProcessEvent(Function, &Params);
			Multipliers = MoveTemp(Params.ReturnValue);
		}
		else if (const auto* Native = Cast<IChunkWorldWalker>(Walker)) Multipliers = Native->GetViewDistanceMultiplier_Implementation();
		if (Multipliers.Num() != Layers) Multipliers.Init(1.0, Layers);
		Walkers.Add(Walker);
		auto& Target = Targets.AddDefaulted_GetRef();
		Target.Position = FIntVector(Local / BlockSize);
		const FVector FinestBlockCount(World->WorldChunks[Layers - 1]->BlockCount);
		Target.Position = FIntVector(FVector(Target.Position) / FinestBlockCount * FinestBlockCount);
		Target.RootCenter = World->BlockWorldPosToChunkGridPos(Target.Position, Root);
		const double Factor = Multipliers[0] * EffectiveRange;
		const bool bInfinite = Config->AxisBehaviorX == EAxisBehavior::Infinity
			&& Config->AxisBehaviorY == EAxisBehavior::Infinity && Config->AxisBehaviorZ == EAxisBehavior::Infinity;
		if (bInfinite)
			Target.RootLimits = FIntVector(FVector(Root->SectorCount) * FMath::Max(Factor, 1.0));
		else
		{
			const float RawFactor = FMath::Max(Factor, 0.01f);
			const FIntVector Raw(FVector(Root->SectorCount) * double(RawFactor));
			const EAxisBehavior Axes[] = {Config->AxisBehaviorX, Config->AxisBehaviorY, Config->AxisBehaviorZ};
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Target.RootLimits[Axis] = Axes[Axis] == EAxisBehavior::SingleChunk ? 1 : FMath::Max(Raw[Axis], 1);
				if (Axes[Axis] != EAxisBehavior::Infinity) Target.RootCenter[Axis] = 0;
			}
		}
		for (int32 Layer = 0; Layer < Layers; ++Layer)
			Target.RefinementRanges.Add(World->WorldChunks[Layer]->ViewDistance * Multipliers[Layer] * EffectiveRange);
	}
	const FVector Reach = GetAutomaticLayoutReachInBlocks();
	if (Targets == Coverage.Targets && Walkers == Coverage.Walkers && Shapes == Coverage.LayerShapes
		&& Coverage.FirstRequiredDetailLevel == FirstRequiredDetailLevel
		&& Coverage.LayoutReach == Reach && Coverage.BlockSize == BlockSize && Coverage.Transform.Equals(World->GetTransform(), 0.0)) return;
	Coverage.Targets = MoveTemp(Targets);
	Coverage.Walkers = MoveTemp(Walkers);
	Coverage.LayerShapes = MoveTemp(Shapes);
	Coverage.FirstRequiredDetailLevel = FirstRequiredDetailLevel;
	Coverage.BlockSize = BlockSize;
	Coverage.LayoutReach = Reach;
	Coverage.Transform = World->GetTransform();
	Coverage.BoundsInBlocks.Reset();
	++Coverage.Revision;
	for (const auto& Target : Coverage.Targets)
	{
		FBox WalkerBounds(ForceInit);
		TFunction<void(int32, FIntVector)> Visit = [&](const int32 LayerIndex, const FIntVector Key)
		{
			const auto* Layer = World->WorldChunks[LayerIndex];
			const FIntVector Origin = Key * Layer->ChunkBlockFactor;
			// The coarsest required layer encloses its finer descendants. Stop here so
			// waiting for more LODs includes their larger native coverage, not just finest bounds.
			if (LayerIndex == FirstRequiredDetailLevel)
			{
				WalkerBounds += FVector(Origin);
				WalkerBounds += FVector(Origin) + FVector(Layer->ChunkBlockFactor) - FVector(1.0);
			}
			else
			{
				FVector Delta(Origin - Target.Position);
				if (Config->Grid.Is2D()) Delta[Config->Grid.FlatAxis] = 0;
				// Match native whole-child-group admission from the parent's origin.
				if (Delta.Length() > Target.RefinementRanges[LayerIndex + 1]) return;
				const auto* Child = World->WorldChunks[LayerIndex + 1];
				const FIntVector Base(Key.X * Layer->ChunkFactor.X / Child->ChunkFactor.X,
					Key.Y * Layer->ChunkFactor.Y / Child->ChunkFactor.Y, Key.Z * Layer->ChunkFactor.Z / Child->ChunkFactor.Z);
				for (int32 X = 0; X < Layer->ChildGridSize.X; ++X)
				for (int32 Y = 0; Y < Layer->ChildGridSize.Y; ++Y)
				for (int32 Z = 0; Z < Layer->ChildGridSize.Z; ++Z)
					Visit(LayerIndex + 1, Base + FIntVector(X, Y, Z));
			}
		};
		for (int32 X = 1 - Target.RootLimits.X; X < Target.RootLimits.X; ++X)
		for (int32 Y = 1 - Target.RootLimits.Y; Y < Target.RootLimits.Y; ++Y)
		for (int32 Z = 1 - Target.RootLimits.Z; Z < Target.RootLimits.Z; ++Z)
			Visit(0, Target.RootCenter + FIntVector(X, Y, Z));
		Coverage.BoundsInBlocks.Add(WalkerBounds);
	}
	TArray<FBox> PriorityBounds;
	for (const FBox& Bounds : Coverage.BoundsInBlocks)
		if (Bounds.IsValid) PriorityBounds.Add(Bounds.ExpandBy(Reach));
	PlanningAreaQueue->SetStartupBounds(PriorityBounds, FirstRequiredDetailLevel);
	LastPlanningWindowUpdateTimeSeconds = TNumericLimits<double>::Lowest();
}

bool UChunkWorldLayoutRuntimeComponent::IsStartupCoverageReady(UObject* Consumer)
{
	check(IsInGameThread());
	RefreshStartupCoverage();
	if (!StartupCoverage || bProcessingQueuedLayoutWork) return false;
	const auto& Coverage = *StartupCoverage;
	const auto* RequestedWalkers = Coverage.Consumers.Find(Consumer);
	if (!RequestedWalkers || RequestedWalkers->IsEmpty()) return false;
	int32 Regions = 0, Queued = 0;
	bool bLayoutsReady = true;
	const bool bLogCoverage = GetDetailedDiagnostics() && FPlatformTime::Seconds() >= Coverage.NextDiagnosticTime;
	FString RegionSummary;
	for (int32 Index = 0; Index < Coverage.Walkers.Num(); ++Index)
	{
		if (!RequestedWalkers->Contains(Coverage.Walkers[Index])) continue;
		const FBox& Bounds = Coverage.BoundsInBlocks[Index];
		if (!Bounds.IsValid) continue;
		++Regions;
		const FBox WorldBounds = FBox(Bounds.Min * Coverage.BlockSize,
			(Bounds.Max + FVector(1.0)) * Coverage.BlockSize).TransformBy(Coverage.Transform);
		bLayoutsReady &= !HasPendingAutomaticLayoutWork(WorldBounds);
		if (bLogCoverage)
			RegionSummary += FString::Printf(TEXT(" walker=%s worldMin=(%s) worldMax=(%s);"),
				*GetPathNameSafe(Coverage.Walkers[Index].Get()), *WorldBounds.Min.ToString(), *WorldBounds.Max.ToString());
		// Local queued terrain events must drain before the native receipt can be consumed.
		// Absence of an invented expected key is not an event or a layout obligation.
		FScopeLock Lock(&PendingChunkLoadsMutex);
		for (const auto& Pair : PendingChunkLoads)
		{
			auto* World = GetOwningChunkWorld();
			const int32 Level = Pair.Key.Get<0>();
			if (!World || Level < Coverage.FirstRequiredDetailLevel || Level >= World->GetChunkLayerCount()) continue;
			const FVector Min(Pair.Key.Get<1>());
			const FVector Max = Min + FVector(World->WorldChunks[Level]->ChunkBlockFactor) - FVector(1.0);
			if (Bounds.Intersect(FBox(Min, Max))) ++Queued;
		}
	}
	const bool bReady = Regions > 0 && Queued == 0 && bLayoutsReady;
	if (bLogCoverage)
	{
		UE_LOG(LogTemp, Log, TEXT("[StartupCoverage] world=%s consumer=%s revision=%llu firstRequiredLOD=%d regions=%d queued=%d layouts=%s result=%s bounds={%s} work={%s}"),
			*GetPathNameSafe(GetOwningChunkWorld()), *GetPathNameSafe(Consumer), Coverage.Revision, Coverage.FirstRequiredDetailLevel, Regions, Queued,
			bLayoutsReady ? TEXT("settled") : TEXT("unfinished"), bReady ? TEXT("ready") : TEXT("hold"),
			*RegionSummary, *PlanningAreaQueue->DescribeBookkeeping());
		StartupCoverage->NextDiagnosticTime = FPlatformTime::Seconds() + 5.0;
	}
	return bReady;
}

bool UChunkWorldLayoutRuntimeComponent::DoesWriteAffectStartupCoverage(UObject* Consumer, const FBox& WorldBounds) const
{
	if (!StartupCoverage) return false;
	const auto& Coverage = *StartupCoverage;
	const auto* RequestedWalkers = Coverage.Consumers.Find(Consumer);
	if (!RequestedWalkers) return false;
	for (int32 Index = 0; Index < Coverage.Walkers.Num(); ++Index)
	{
		if (!RequestedWalkers->Contains(Coverage.Walkers[Index])) continue;
		const FBox& Bounds = Coverage.BoundsInBlocks[Index];
		if (Bounds.IsValid && WorldBounds.Intersect(FBox(Bounds.Min * Coverage.BlockSize,
			(Bounds.Max + FVector(1.0)) * Coverage.BlockSize).TransformBy(Coverage.Transform))) return true;
	}
	return false;
}

int32 UChunkWorldLayoutRuntimeComponent::ComputeAutomaticPlanningPriority(const FIntVector& Center, const TConstArrayView<FIntVector> Centers) const
{
	const int32 DistancePriority = FLayoutPlanningAreaQueue::ComputePriority(Center, Centers);
	// Required work sorts above ordinary non-positive distance priorities without increasing budgets.
	return PlanningAreaQueue && PlanningAreaQueue->IsStartupPosition(Center)
		? FMath::Max(1, MAX_int32 + DistancePriority) : DistancePriority;
}
