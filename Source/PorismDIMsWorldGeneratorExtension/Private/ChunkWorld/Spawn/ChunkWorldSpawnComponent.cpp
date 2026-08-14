// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Spawn/ChunkWorldSpawnComponent.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"

namespace
{
	/** Returns the shared base definition only when an opaque project definition inherits the plugin contract. */
	const FChunkWorldSpawnSubjectDefinitionBase* GetSpawnDefinitionBase(const FInstancedStruct& Definition)
	{
		const UScriptStruct* DefinitionStruct = Definition.GetScriptStruct();
		if (DefinitionStruct == nullptr || !DefinitionStruct->IsChildOf(FChunkWorldSpawnSubjectDefinitionBase::StaticStruct()))
		{
			return nullptr;
		}

		return reinterpret_cast<const FChunkWorldSpawnSubjectDefinitionBase*>(Definition.GetMemory());
	}

	/** Returns whether a plugin-base definition can safely participate in weighted selection and biome-transform resolution. */
	bool IsSpawnDefinitionBaseValid(const FChunkWorldSpawnSubjectDefinitionBase* DefinitionBase)
	{
		return DefinitionBase != nullptr
			&& FMath::IsFinite(DefinitionBase->SelectionWeight)
			&& DefinitionBase->SelectionWeight > 0.0f;
	}

	/** Returns whether an actor currently sits below its world's active KillZ boundary. */
	bool IsActorBelowKillZ(const AActor& Actor)
	{
		const UWorld* World = Actor.GetWorld();
		const AWorldSettings* WorldSettings = World != nullptr ? World->GetWorldSettings() : nullptr;
		return WorldSettings != nullptr && Actor.GetActorLocation().Z < WorldSettings->KillZ;
	}

	/** Formats a finite world-space value at double round-trip precision so distinct large-world requests cannot share a cache entry. */
	FString FormatCacheReal(const double Value)
	{
		return FString::Printf(TEXT("%.17g"), Value);
	}

	/** Returns whether provider-supplied world bounds are safe for bounded provisional-location sampling. */
	bool IsFiniteWorldBounds(const FBox& Bounds)
	{
		const FVector Center = Bounds.GetCenter();
		const FVector Extent = Bounds.GetExtent();
		return Bounds.IsValid != 0
			&& FMath::IsFinite(Bounds.Min.X)
			&& FMath::IsFinite(Bounds.Min.Y)
			&& FMath::IsFinite(Bounds.Min.Z)
			&& FMath::IsFinite(Bounds.Max.X)
			&& FMath::IsFinite(Bounds.Max.Y)
			&& FMath::IsFinite(Bounds.Max.Z)
			&& FMath::IsFinite(Center.X)
			&& FMath::IsFinite(Center.Y)
			&& FMath::IsFinite(Center.Z)
			&& FMath::IsFinite(Extent.X)
			&& FMath::IsFinite(Extent.Y)
			&& FMath::IsFinite(Extent.Z);
	}

	/** Sorts provider output independently of provider registration order before deterministic ticket selection. */
	bool IsCandidateHigherPriority(const FChunkWorldSpawnCandidate& Left, const FChunkWorldSpawnCandidate& Right)
	{
		if (!FMath::IsNearlyEqual(Left.BaseScore, Right.BaseScore))
		{
			return Left.BaseScore > Right.BaseScore;
		}
		if (Left.SourceId != Right.SourceId)
		{
			return Left.SourceId.LexicalLess(Right.SourceId);
		}
		return Left.SourceKind.LexicalLess(Right.SourceKind);
	}
}

UChunkWorldSpawnComponent::UChunkWorldSpawnComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

AChunkWorldExtended* UChunkWorldSpawnComponent::GetChunkWorld() const
{
	return Cast<AChunkWorldExtended>(GetOwner());
}

FChunkWorldSpawnTicketHandle UChunkWorldSpawnComponent::SubmitSpawnRequest(const FChunkWorldSpawnRequest& Request)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogChunkWorldSpawn, Error, TEXT("Rejected spawn request outside the game thread."));
		return FChunkWorldSpawnTicketHandle();
	}

	PruneTerminalTickets(FGuid(), true);
	if (TicketRecords.Num() >= FMath::Max(1, MaximumSpawnTicketRecords))
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected spawn request because pending tickets occupy the record limit. Limit=%d"), MaximumSpawnTicketRecords);
		return FChunkWorldSpawnTicketHandle();
	}

	if (!IsAuthorityGameThread())
	{
		return CreateFailedTicket(Request, EChunkWorldSpawnFailureCategory::WorldInvalid, TEXT("Spawn requests require an authority-owned AChunkWorldExtended."));
	}

	if (!Request.SubjectBinding.StableSubjectId.IsValid())
	{
		return CreateFailedTicket(Request, EChunkWorldSpawnFailureCategory::SubjectInvalid, TEXT("Spawn requests require a valid opaque stable subject id."));
	}

	EChunkWorldSpawnFailureCategory BindingFailure = EChunkWorldSpawnFailureCategory::None;
	FString BindingFailureReason;
	if (!IsPendingRequestValid(Request, BindingFailure, BindingFailureReason))
	{
		return CreateFailedTicket(Request, BindingFailure, BindingFailureReason);
	}

	PruneInvalidRegistrations();
	if (Request.AllowedSourceFamiliesInPriorityOrder.IsEmpty())
	{
		return CreateFailedTicket(Request, EChunkWorldSpawnFailureCategory::NoEligibleSource, TEXT("A request requires at least one source family."));
	}
	if (!Request.SubjectDefinitions.IsEmpty() && !Request.SubjectDefinitions.ContainsByPredicate([](const FInstancedStruct& Definition)
	{
		return IsSpawnDefinitionBaseValid(GetSpawnDefinitionBase(Definition));
	}))
	{
		return CreateFailedTicket(Request, EChunkWorldSpawnFailureCategory::NoEligibleSubjectDefinition, TEXT("Subject definitions must include at least one finite, positively weighted definition when supplied."));
	}

	FChunkWorldSpawnTicketHandle Handle;
	Handle.TicketId = FGuid::NewGuid();
	FSpawnTicketRecord& Record = TicketRecords.Add(Handle.TicketId);
	Record.Request = Request;
	Record.Result.RequestId = Request.RequestId.IsValid() ? Request.RequestId : FGuid::NewGuid();
	Record.Request.RequestId = Record.Result.RequestId;
	Record.Result.DebugReason = TEXT("Awaiting bounded source selection and provisional theoretical placement.");
	RefreshRuntimeTick();
	UE_LOG(LogChunkWorldSpawn, Log, TEXT("Submitted pending spawn ticket. Ticket=%s Request=%s"), *Handle.TicketId.ToString(), *Record.Result.RequestId.ToString());
	return Handle;
}

bool UChunkWorldSpawnComponent::CancelSpawnTicket(const FChunkWorldSpawnTicketHandle Ticket)
{
	if (!IsAuthorityGameThread())
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected spawn ticket cancellation outside the authority game thread. Ticket=%s"), *Ticket.TicketId.ToString());
		return false;
	}

	FSpawnTicketRecord* Record = FindTicket(Ticket);
	if (Record == nullptr || Record->State == EChunkWorldSpawnTicketState::Failed || Record->State == EChunkWorldSpawnTicketState::Canceled || Record->State == EChunkWorldSpawnTicketState::Ready)
	{
		return false;
	}

	CancelTicket(Ticket.TicketId, EChunkWorldSpawnFailureCategory::RequestCanceled, TEXT("The caller canceled this spawn ticket."));
	RefreshRuntimeTick();
	return true;
}

EChunkWorldSpawnTicketState UChunkWorldSpawnComponent::GetSpawnTicketState(const FChunkWorldSpawnTicketHandle Ticket) const
{
	if (const FSpawnTicketRecord* Record = FindTicket(Ticket))
	{
		return Record->State;
	}

	return EChunkWorldSpawnTicketState::Failed;
}

bool UChunkWorldSpawnComponent::GetSpawnTicketResult(const FChunkWorldSpawnTicketHandle Ticket, FChunkWorldSpawnResult& OutResult) const
{
	const FSpawnTicketRecord* Record = FindTicket(Ticket);
	if (Record == nullptr)
	{
		return false;
	}

	OutResult = Record->Result;
	return true;
}

bool UChunkWorldSpawnComponent::RegisterSpawnSourceProvider(const TScriptInterface<IChunkWorldSpawnSourceProvider> InProvider)
{
	if (!IsAuthorityGameThread())
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected source provider registration outside the authority game thread."));
		return false;
	}

	UObject* ProviderObject = InProvider.GetObject();
	if (!IsValid(ProviderObject) || InProvider.GetInterface() == nullptr)
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected invalid spawn source provider registration."));
		return false;
	}

	PruneInvalidRegistrations();
	if (RegisteredSourceProviders.ContainsByPredicate([ProviderObject](const TWeakObjectPtr<UObject>& ExistingProvider)
	{
		return ExistingProvider.Get() == ProviderObject;
	}))
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected duplicate spawn source provider. Provider=%s"), *GetNameSafe(ProviderObject));
		return false;
	}
	if (RegisteredSourceProviders.Num() >= FMath::Max(1, MaximumRegisteredSourceProviders))
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected spawn source provider because the configured registration limit is full. Provider=%s Limit=%d"), *GetNameSafe(ProviderObject), MaximumRegisteredSourceProviders);
		return false;
	}

	RegisteredSourceProviders.Add(ProviderObject);
	InvalidateCandidateQueryCache();
	UE_LOG(LogChunkWorldSpawn, Log, TEXT("Registered spawn source provider. Provider=%s Family=%s"), *GetNameSafe(ProviderObject), *UEnum::GetValueAsString(InProvider->GetSpawnSourceFamily()));
	return true;
}

bool UChunkWorldSpawnComponent::UnregisterSpawnSourceProvider(const TScriptInterface<IChunkWorldSpawnSourceProvider> InProvider)
{
	if (!IsAuthorityGameThread())
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected source provider unregister outside the authority game thread."));
		return false;
	}

	UObject* ProviderObject = InProvider.GetObject();
	if (!IsValid(ProviderObject) || InProvider.GetInterface() == nullptr)
	{
		return false;
	}

	const int32 RemovedCount = RegisteredSourceProviders.RemoveAll([ProviderObject](const TWeakObjectPtr<UObject>& ExistingProvider)
	{
		return ExistingProvider.Get() == ProviderObject;
	});
	if (RemovedCount == 0)
	{
		return false;
	}

	InvalidateCandidateQueryCache();
	UE_LOG(LogChunkWorldSpawn, Log, TEXT("Unregistered spawn source provider. Provider=%s"), *GetNameSafe(ProviderObject));
	return true;
}

bool UChunkWorldSpawnComponent::RegisterSpawnOriginProvider(const TScriptInterface<IChunkWorldSpawnOriginProvider> InProvider)
{
	if (!IsAuthorityGameThread())
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected origin provider registration outside the authority game thread."));
		return false;
	}

	UObject* ProviderObject = InProvider.GetObject();
	if (!IsValid(ProviderObject) || InProvider.GetInterface() == nullptr)
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected invalid spawn origin provider registration."));
		return false;
	}

	PruneInvalidRegistrations();
	if (RegisteredOriginProvider.Get() == ProviderObject)
	{
		return true;
	}

	if (RegisteredOriginProvider.IsValid())
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected origin provider replacement. Unregister the current provider before registering another. Existing=%s Requested=%s"), *GetNameSafe(RegisteredOriginProvider.Get()), *GetNameSafe(ProviderObject));
		return false;
	}

	RegisteredOriginProvider = ProviderObject;
	UE_LOG(LogChunkWorldSpawn, Log, TEXT("Registered spawn origin provider. Provider=%s"), *GetNameSafe(ProviderObject));
	return true;
}

bool UChunkWorldSpawnComponent::UnregisterSpawnOriginProvider(const TScriptInterface<IChunkWorldSpawnOriginProvider> InProvider)
{
	if (!IsAuthorityGameThread())
	{
		UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Rejected origin provider unregister outside the authority game thread."));
		return false;
	}

	UObject* ProviderObject = InProvider.GetObject();
	if (!IsValid(ProviderObject) || InProvider.GetInterface() == nullptr || RegisteredOriginProvider.Get() != ProviderObject)
	{
		return false;
	}

	RegisteredOriginProvider.Reset();
	UE_LOG(LogChunkWorldSpawn, Log, TEXT("Unregistered spawn origin provider. Provider=%s"), *GetNameSafe(ProviderObject));
	return true;
}

void UChunkWorldSpawnComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsAuthorityGameThread())
	{
		SetComponentTickEnabled(false);
		return;
	}

	PruneInvalidRegistrations();
	UpdateSpawnTickets();
	UpdateOutOfWorldTracking();
	RefreshRuntimeTick();
}

void UChunkWorldSpawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TArray<FGuid> NonTerminalTicketIds;
	for (const TPair<FGuid, FSpawnTicketRecord>& TicketPair : TicketRecords)
	{
		const EChunkWorldSpawnTicketState State = TicketPair.Value.State;
		if (State == EChunkWorldSpawnTicketState::Pending)
		{
			NonTerminalTicketIds.Add(TicketPair.Key);
		}
	}
	for (const FGuid TicketId : NonTerminalTicketIds)
	{
		CancelTicket(TicketId, EChunkWorldSpawnFailureCategory::RequestCanceled, TEXT("The owning chunk world ended play before this ticket completed."));
	}

	RegisteredSourceProviders.Reset();
	RegisteredOriginProvider.Reset();
	TrackedExecutionActors.Reset();
	InvalidateCandidateQueryCache();
	SetComponentTickEnabled(false);

	Super::EndPlay(EndPlayReason);
}

bool UChunkWorldSpawnComponent::IsAuthorityGameThread() const
{
	const AChunkWorldExtended* ChunkWorld = GetChunkWorld();
	return IsInGameThread() && IsValid(ChunkWorld) && ChunkWorld->GetWorld() != nullptr && ChunkWorld->HasAuthority();
}

bool UChunkWorldSpawnComponent::HasNonTerminalTickets() const
{
	for (const TPair<FGuid, FSpawnTicketRecord>& TicketPair : TicketRecords)
	{
		const EChunkWorldSpawnTicketState State = TicketPair.Value.State;
		if (State == EChunkWorldSpawnTicketState::Pending)
		{
			return true;
		}
	}

	return false;
}

void UChunkWorldSpawnComponent::RefreshRuntimeTick()
{
	SetComponentTickEnabled(HasNonTerminalTickets() || !TrackedExecutionActors.IsEmpty());
}

void UChunkWorldSpawnComponent::UpdateSpawnTickets()
{
	TArray<FGuid> PendingTicketIds;
	for (const TPair<FGuid, FSpawnTicketRecord>& TicketPair : TicketRecords)
	{
		if (TicketPair.Value.State == EChunkWorldSpawnTicketState::Pending)
		{
			PendingTicketIds.Add(TicketPair.Key);
		}
	}

	for (const FGuid TicketId : PendingTicketIds)
	{
		UpdateSpawnTicket(TicketId);
	}
}

void UChunkWorldSpawnComponent::UpdateSpawnTicket(const FGuid& TicketId)
{
	for (;;)
	{
		FSpawnTicketRecord* TicketRecord = TicketRecords.Find(TicketId);
		if (TicketRecord == nullptr || TicketRecord->State != EChunkWorldSpawnTicketState::Pending)
		{
			return;
		}

		EChunkWorldSpawnFailureCategory InvalidationFailure = EChunkWorldSpawnFailureCategory::None;
		FString InvalidationReason;
		if (!IsPendingRequestValid(TicketRecord->Request, InvalidationFailure, InvalidationReason))
		{
			if (InvalidationFailure == EChunkWorldSpawnFailureCategory::OwnerInvalid)
			{
				CancelTicket(TicketId, InvalidationFailure, InvalidationReason);
			}
			else
			{
				FailTicket(TicketId, InvalidationFailure, InvalidationReason);
			}
			return;
		}
		if (!TicketRecord->bHasSelectedCandidate)
		{
			FString SelectionFailure;
			if (SelectCandidateForCurrentFamily(TicketId, SelectionFailure))
			{
				continue;
			}

			TicketRecord = TicketRecords.Find(TicketId);
			if (TicketRecord == nullptr || TicketRecord->State != EChunkWorldSpawnTicketState::Pending)
			{
				return;
			}
			if (TicketRecord->Request.bAllowLowerPrioritySourceFallback && ++TicketRecord->SourceFamilyIndex < TicketRecord->Request.AllowedSourceFamiliesInPriorityOrder.Num())
			{
				continue;
			}
			FailTicket(TicketId, EChunkWorldSpawnFailureCategory::NoEligibleSource, SelectionFailure);
			return;
		}

		FTransform ApprovedTransform;
		FString PlacementFailure;
		if (!ResolveBiomePlacement(*TicketRecord, ApprovedTransform, PlacementFailure))
		{
			if (TicketRecord->Request.bAllowLowerPrioritySourceFallback && ++TicketRecord->SourceFamilyIndex < TicketRecord->Request.AllowedSourceFamiliesInPriorityOrder.Num())
			{
				TicketRecord->bHasSelectedCandidate = false;
				TicketRecord->SelectedCandidate = FChunkWorldSpawnCandidate();
				TicketRecord->SelectedSubjectDefinition.Reset();
				continue;
			}
			FailTicket(TicketId, EChunkWorldSpawnFailureCategory::UnsafePlacement, PlacementFailure);
			return;
		}

		TicketRecord->Result.bSuccess = true;
		TicketRecord->Result.FailureCategory = EChunkWorldSpawnFailureCategory::None;
		TicketRecord->Result.ApprovedTransform = ApprovedTransform;
		TicketRecord->Result.SelectedSubjectDefinition = TicketRecord->SelectedSubjectDefinition;
		TicketRecord->Result.SelectedSourceFamily = TicketRecord->SelectedCandidate.SourceFamily;
		TicketRecord->Result.SelectedSourceKind = TicketRecord->SelectedCandidate.SourceKind;
		TicketRecord->Result.SelectedSourceId = TicketRecord->SelectedCandidate.SourceId;
		TicketRecord->Result.ApprovedCandidateRegion.Minimum = TicketRecord->SelectedCandidate.SearchBounds.Min;
		TicketRecord->Result.ApprovedCandidateRegion.Maximum = TicketRecord->SelectedCandidate.SearchBounds.Max;
		TicketRecord->Result.DebugReason = FString::Printf(
			TEXT("Approved deterministic provisional biome-backed location. Source=%s TheoreticalSurfaceZ=%.3f Location=%s."),
			*TicketRecord->SelectedCandidate.DebugLabel,
			TicketRecord->SelectedCandidate.TheoreticalSurfaceZ,
			*ApprovedTransform.GetLocation().ToString());
		UE_LOG(LogChunkWorldSpawn, Log, TEXT("Approved provisional spawn location. Ticket=%s Source=%s TheoreticalSurfaceZ=%.3f Location=%s"),
			*TicketId.ToString(),
			*TicketRecord->SelectedCandidate.DebugLabel,
			TicketRecord->SelectedCandidate.TheoreticalSurfaceZ,
			*ApprovedTransform.GetLocation().ToString());
		TicketRecord->State = EChunkWorldSpawnTicketState::Ready;
		TicketRecord->TerminalSequence = ++NextTerminalTicketSequence;
		const FChunkWorldSpawnResult ResolvedResult = TicketRecord->Result;
		OnSpawnResolvedNative.Broadcast(ResolvedResult);
		OnSpawnResolved.Broadcast(ResolvedResult);
		UE_LOG(LogChunkWorldSpawn, Log, TEXT("Published approved spawn location. Ticket=%s Request=%s"), *TicketId.ToString(), *ResolvedResult.RequestId.ToString());
		PruneTerminalTickets(TicketId);
		return;
	}
}

bool UChunkWorldSpawnComponent::SelectCandidateForCurrentFamily(const FGuid& TicketId, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	const FSpawnTicketRecord* InitialTicketRecord = TicketRecords.Find(TicketId);
	if (InitialTicketRecord == nullptr || InitialTicketRecord->State != EChunkWorldSpawnTicketState::Pending
		|| !InitialTicketRecord->Request.AllowedSourceFamiliesInPriorityOrder.IsValidIndex(InitialTicketRecord->SourceFamilyIndex))
	{
		OutFailureReason = TEXT("The request has no remaining allowed source family.");
		return false;
	}

	AChunkWorldExtended* ChunkWorld = GetChunkWorld();
	if (ChunkWorld == nullptr)
	{
		OutFailureReason = TEXT("The owning chunk world is unavailable.");
		return false;
	}

	const FChunkWorldSpawnRequest Request = InitialTicketRecord->Request;
	const EChunkWorldSpawnSourceFamily RequestedFamily = Request.AllowedSourceFamiliesInPriorityOrder[InitialTicketRecord->SourceFamilyIndex];
	FChunkWorldSpawnQueryContext QueryContext;
	QueryContext.ChunkWorld = ChunkWorld;
	QueryContext.QueryOrigin = Request.PreferredSearchOrigin;
	QueryContext.MaximumCandidates = FMath::Max(1, MaximumCandidatesPerProvider);

	TArray<FChunkWorldSpawnCandidate> Candidates;
	FString CacheKey;
	const bool bCanUseCache = CandidateQueryCacheSeconds > 0.0f && TryBuildCandidateCacheKey(Request, RequestedFamily, CacheKey);
	if (const FSpawnTicketRecord* CurrentTicketRecord = TicketRecords.Find(TicketId); CurrentTicketRecord == nullptr || CurrentTicketRecord->State != EChunkWorldSpawnTicketState::Pending)
	{
		OutFailureReason = TEXT("The ticket settled while resolving provider query caching.");
		return false;
	}

	const double NowSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	if (const FCandidateQueryCacheEntry* CachedEntry = bCanUseCache ? CandidateQueryCache.Find(CacheKey) : nullptr; CachedEntry != nullptr && CachedEntry->ExpiryTimeSeconds >= NowSeconds)
	{
		Candidates = CachedEntry->Candidates;
		UE_LOG(LogChunkWorldSpawn, VeryVerbose, TEXT("Spawn candidate cache hit. Ticket=%s Family=%s CandidateCount=%d"), *TicketId.ToString(), *UEnum::GetValueAsString(RequestedFamily), Candidates.Num());
	}
	else
	{
		const TArray<TWeakObjectPtr<UObject>> ProviderSnapshot = RegisteredSourceProviders;
		for (const TWeakObjectPtr<UObject>& ProviderObject : ProviderSnapshot)
		{
			IChunkWorldSpawnSourceProvider* Provider = Cast<IChunkWorldSpawnSourceProvider>(ProviderObject.Get());
			if (Provider == nullptr || !Provider->IsSpawnSourceProviderEnabled() || Provider->GetSpawnSourceFamily() != RequestedFamily || !Provider->CanServeSpawnRequest(Request))
			{
				continue;
			}

			const int32 CandidateCountBeforeProvider = Candidates.Num();
			Provider->GatherSpawnCandidates(Request, QueryContext, Candidates);
			if (const FSpawnTicketRecord* CurrentTicketRecord = TicketRecords.Find(TicketId); CurrentTicketRecord == nullptr || CurrentTicketRecord->State != EChunkWorldSpawnTicketState::Pending)
			{
				OutFailureReason = TEXT("The ticket settled during provider candidate gathering.");
				return false;
			}
			if (Candidates.Num() - CandidateCountBeforeProvider > QueryContext.MaximumCandidates)
			{
				Candidates.SetNum(CandidateCountBeforeProvider + QueryContext.MaximumCandidates);
				UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Trimmed unbounded spawn provider output. Ticket=%s Provider=%s Limit=%d"), *TicketId.ToString(), *GetNameSafe(ProviderObject.Get()), QueryContext.MaximumCandidates);
			}
		}

		if (bCanUseCache)
		{
			if (CandidateQueryCache.Num() >= FMath::Max(1, MaximumCandidateQueryCacheEntries))
			{
				FString OldestKey;
				double OldestExpiry = TNumericLimits<double>::Max();
				for (const TPair<FString, FCandidateQueryCacheEntry>& CachePair : CandidateQueryCache)
				{
					if (CachePair.Value.ExpiryTimeSeconds < OldestExpiry)
					{
						OldestKey = CachePair.Key;
							OldestExpiry = CachePair.Value.ExpiryTimeSeconds;
					}
				}
				CandidateQueryCache.Remove(OldestKey);
			}
			FCandidateQueryCacheEntry& CacheEntry = CandidateQueryCache.FindOrAdd(CacheKey);
			CacheEntry.Candidates = Candidates;
			CacheEntry.ExpiryTimeSeconds = NowSeconds + CandidateQueryCacheSeconds;
		}
		UE_LOG(LogChunkWorldSpawn, VeryVerbose, TEXT("Spawn candidate cache miss. Ticket=%s Family=%s CandidateCount=%d Cacheable=%s"), *TicketId.ToString(), *UEnum::GetValueAsString(RequestedFamily), Candidates.Num(), bCanUseCache ? TEXT("true") : TEXT("false"));
	}

	Candidates.RemoveAll([RequestedFamily, &Request](const FChunkWorldSpawnCandidate& Candidate)
	{
		return Candidate.SourceFamily != RequestedFamily
			|| !IsFiniteWorldBounds(Candidate.SearchBounds)
			|| !FMath::IsFinite(Candidate.TheoreticalLocation.X)
			|| !FMath::IsFinite(Candidate.TheoreticalLocation.Y)
			|| !FMath::IsFinite(Candidate.TheoreticalLocation.Z)
			|| !FMath::IsFinite(Candidate.TheoreticalSurfaceZ)
			|| !FMath::IsFinite(Candidate.BaseScore)
			|| !Candidate.SpawnTags.HasAll(Request.RequiredSpawnTags)
			|| !Candidate.BiomeTags.HasAll(Request.RequiredBiomeTags);
	});
	Candidates.Sort(IsCandidateHigherPriority);

	struct FCompatiblePair
	{
		const FChunkWorldSpawnCandidate* Candidate = nullptr;
		const FInstancedStruct* Definition = nullptr;
		double Weight = 0.0;
	};

	TArray<FCompatiblePair> CompatiblePairs;
	for (const FChunkWorldSpawnCandidate& Candidate : Candidates)
	{
		if (Request.SubjectDefinitions.IsEmpty())
		{
			FCompatiblePair& Pair = CompatiblePairs.AddDefaulted_GetRef();
			Pair.Candidate = &Candidate;
			Pair.Weight = FMath::Max(0.01, 1.0 + static_cast<double>(Candidate.BaseScore));
			continue;
		}

		for (const FInstancedStruct& SubjectDefinition : Request.SubjectDefinitions)
		{
			const FChunkWorldSpawnSubjectDefinitionBase* DefinitionBase = GetSpawnDefinitionBase(SubjectDefinition);
			if (!IsSpawnDefinitionBaseValid(DefinitionBase)
				|| !Candidate.SpawnTags.HasAll(DefinitionBase->RequiredSpawnTags))
			{
				continue;
			}

			FCompatiblePair& Pair = CompatiblePairs.AddDefaulted_GetRef();
			Pair.Candidate = &Candidate;
			Pair.Definition = &SubjectDefinition;
			Pair.Weight = static_cast<double>(DefinitionBase->SelectionWeight) * FMath::Max(0.01, 1.0 + static_cast<double>(Candidate.BaseScore));
		}
	}

	if (CompatiblePairs.IsEmpty())
	{
		OutFailureReason = FString::Printf(TEXT("No compatible candidates were supplied for requested family %s."), *UEnum::GetValueAsString(RequestedFamily));
		return false;
	}

	double TotalWeight = 0.0;
	for (const FCompatiblePair& Pair : CompatiblePairs)
	{
		TotalWeight += Pair.Weight;
	}

	FRandomStream RandomStream(static_cast<int32>(TicketId.A ^ TicketId.B ^ TicketId.C ^ TicketId.D));
	double RemainingWeight = static_cast<double>(RandomStream.FRand()) * TotalWeight;
	const FCompatiblePair* SelectedPair = &CompatiblePairs.Last();
	for (const FCompatiblePair& Pair : CompatiblePairs)
	{
		RemainingWeight -= Pair.Weight;
		if (RemainingWeight <= 0.0)
		{
			SelectedPair = &Pair;
			break;
		}
	}

	FSpawnTicketRecord* CurrentTicketRecord = TicketRecords.Find(TicketId);
	if (CurrentTicketRecord == nullptr || CurrentTicketRecord->State != EChunkWorldSpawnTicketState::Pending)
	{
		return false;
	}

	CurrentTicketRecord->SelectedCandidate = *SelectedPair->Candidate;
	if (SelectedPair->Definition != nullptr)
	{
		CurrentTicketRecord->SelectedSubjectDefinition = *SelectedPair->Definition;
	}
	else
	{
		CurrentTicketRecord->SelectedSubjectDefinition.Reset();
	}
	CurrentTicketRecord->bHasSelectedCandidate = true;
	UE_LOG(LogChunkWorldSpawn, Log, TEXT("Selected spawn candidate. Ticket=%s Family=%s CandidateCount=%d Source=%s"), *TicketId.ToString(), *UEnum::GetValueAsString(RequestedFamily), Candidates.Num(), *CurrentTicketRecord->SelectedCandidate.DebugLabel);
	return true;
}

bool UChunkWorldSpawnComponent::ResolveBiomePlacement(const FSpawnTicketRecord& TicketRecord, FTransform& OutTransform, FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	const FBox& Bounds = TicketRecord.SelectedCandidate.SearchBounds;
	if (Bounds.IsValid == 0)
	{
		OutFailureReason = TEXT("The selected biome-backed source bounds became unavailable before placement resolution.");
		return false;
	}

	const FVector CandidateLocation = TicketRecord.SelectedCandidate.TheoreticalLocation;
	if (!FMath::IsFinite(CandidateLocation.X) || !FMath::IsFinite(CandidateLocation.Y) || !FMath::IsFinite(CandidateLocation.Z))
	{
		OutFailureReason = TEXT("The selected biome-backed source produced a non-finite theoretical placement location.");
		return false;
	}
	if (CandidateLocation.X < Bounds.Min.X || CandidateLocation.X > Bounds.Max.X
		|| CandidateLocation.Y < Bounds.Min.Y || CandidateLocation.Y > Bounds.Max.Y)
	{
		OutFailureReason = TEXT("The provider-selected theoretical placement falls outside its candidate XY bounds.");
		return false;
	}

	const FVector DistanceFromOrigin = CandidateLocation - TicketRecord.Request.PreferredSearchOrigin;
	const double DistanceSquared = static_cast<double>(DistanceFromOrigin.X) * static_cast<double>(DistanceFromOrigin.X)
		+ static_cast<double>(DistanceFromOrigin.Y) * static_cast<double>(DistanceFromOrigin.Y)
		+ static_cast<double>(DistanceFromOrigin.Z) * static_cast<double>(DistanceFromOrigin.Z);
	const double MinimumDistanceSquared = static_cast<double>(TicketRecord.Request.MinimumSearchDistance) * static_cast<double>(TicketRecord.Request.MinimumSearchDistance);
	const double MaximumDistanceSquared = static_cast<double>(TicketRecord.Request.PreferredSearchRadius) * static_cast<double>(TicketRecord.Request.PreferredSearchRadius);
	if (!FMath::IsFinite(DistanceSquared)
		|| (TicketRecord.Request.MinimumSearchDistance > 0.0f && DistanceSquared < MinimumDistanceSquared)
		|| (TicketRecord.Request.PreferredSearchRadius > 0.0f && DistanceSquared > MaximumDistanceSquared))
	{
		OutFailureReason = TEXT("The selected biome-backed placement falls outside the request distance constraints.");
		return false;
	}

	OutTransform = FTransform(FRotator::ZeroRotator, CandidateLocation);
	return true;
}

bool UChunkWorldSpawnComponent::TryBuildCandidateCacheKey(const FChunkWorldSpawnRequest& Request, const EChunkWorldSpawnSourceFamily SourceFamily, FString& OutCacheKey) const
{
	TArray<FString> ProviderKeys;
	const TArray<TWeakObjectPtr<UObject>> ProviderSnapshot = RegisteredSourceProviders;
	for (const TWeakObjectPtr<UObject>& ProviderObject : ProviderSnapshot)
	{
		IChunkWorldSpawnSourceProvider* Provider = Cast<IChunkWorldSpawnSourceProvider>(ProviderObject.Get());
		if (Provider == nullptr || !Provider->IsSpawnSourceProviderEnabled() || Provider->GetSpawnSourceFamily() != SourceFamily || !Provider->CanServeSpawnRequest(Request))
		{
			continue;
		}

		const FString ProviderPath = ProviderObject->GetPathName();
		FString ProviderCacheKey;
		if (!Provider->TryBuildSpawnCandidateCacheKey(Request, ProviderCacheKey))
		{
			OutCacheKey.Reset();
			return false;
		}
		if (!ProviderObject.IsValid())
		{
			OutCacheKey.Reset();
			return false;
		}
		ProviderKeys.Add(FString::Printf(TEXT("%s=%s"), *ProviderPath, *ProviderCacheKey));
	}

	if (ProviderKeys.IsEmpty())
	{
		OutCacheKey.Reset();
		return false;
	}

	ProviderKeys.Sort();
	OutCacheKey = FString::Printf(
		TEXT("%s|%s|%s|%s,%s,%s,%s|%s|%s|%d|%s"),
		*UEnum::GetValueAsString(SourceFamily),
		*Request.RequiredSpawnTags.ToStringSimple(),
		*Request.RequiredBiomeTags.ToStringSimple(),
		*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*FormatCacheReal(Request.PreferredSearchOrigin.X),
		*FormatCacheReal(Request.PreferredSearchOrigin.Y),
		*FormatCacheReal(Request.PreferredSearchOrigin.Z),
		*FormatCacheReal(Request.MinimumSearchDistance),
		*FormatCacheReal(Request.PreferredSearchRadius),
		MaximumCandidatesPerProvider,
		*FString::Join(ProviderKeys, TEXT("|")));
	return true;
}

void UChunkWorldSpawnComponent::InvalidateCandidateQueryCache()
{
	CandidateQueryCache.Reset();
}

void UChunkWorldSpawnComponent::PruneTerminalTickets(const FGuid& ProtectedTicketId, const bool bReserveCapacityForNewTicket)
{
	const int32 RetentionLimit = FMath::Max(1, MaximumRetainedTerminalTickets);
	const int32 TicketRecordLimit = FMath::Max(1, MaximumSpawnTicketRecords);
	for (;;)
	{
		int32 TerminalCount = 0;
		FGuid OldestTicketId;
		uint64 OldestTerminalSequence = MAX_uint64;
		for (const TPair<FGuid, FSpawnTicketRecord>& TicketPair : TicketRecords)
		{
			const EChunkWorldSpawnTicketState State = TicketPair.Value.State;
			const bool bIsTerminal = State == EChunkWorldSpawnTicketState::Ready
				|| State == EChunkWorldSpawnTicketState::Failed
				|| State == EChunkWorldSpawnTicketState::Canceled;
			if (!bIsTerminal)
			{
				continue;
			}

			++TerminalCount;
			if (TicketPair.Key != ProtectedTicketId && TicketPair.Value.TerminalSequence < OldestTerminalSequence)
			{
				OldestTicketId = TicketPair.Key;
				OldestTerminalSequence = TicketPair.Value.TerminalSequence;
			}
		}

		const bool bExceedsTerminalRetention = TerminalCount > RetentionLimit;
		const bool bRequiresCapacity = bReserveCapacityForNewTicket && TicketRecords.Num() >= TicketRecordLimit;
		if ((!bExceedsTerminalRetention && !bRequiresCapacity) || !OldestTicketId.IsValid())
		{
			return;
		}

		TicketRecords.Remove(OldestTicketId);
	}
}

void UChunkWorldSpawnComponent::CancelTicket(const FGuid& TicketId, const EChunkWorldSpawnFailureCategory FailureCategory, const FString& DebugReason)
{
	FSpawnTicketRecord* TicketRecord = TicketRecords.Find(TicketId);
	if (TicketRecord == nullptr || TicketRecord->State == EChunkWorldSpawnTicketState::Failed || TicketRecord->State == EChunkWorldSpawnTicketState::Canceled || TicketRecord->State == EChunkWorldSpawnTicketState::Ready)
	{
		return;
	}

	TicketRecord->State = EChunkWorldSpawnTicketState::Canceled;
	TicketRecord->Result.bSuccess = false;
	TicketRecord->Result.FailureCategory = FailureCategory;
	TicketRecord->Result.DebugReason = DebugReason;
	TicketRecord->TerminalSequence = ++NextTerminalTicketSequence;
	UE_LOG(LogChunkWorldSpawn, Log, TEXT("Canceled spawn ticket. Ticket=%s Request=%s Failure=%s Reason=%s"), *TicketId.ToString(), *TicketRecord->Result.RequestId.ToString(), ChunkWorldSpawn::ToString(FailureCategory), *DebugReason);
	PruneTerminalTickets(TicketId);
}

void UChunkWorldSpawnComponent::FailTicket(const FGuid& TicketId, const EChunkWorldSpawnFailureCategory FailureCategory, const FString& DebugReason)
{
	FSpawnTicketRecord* TicketRecord = TicketRecords.Find(TicketId);
	if (TicketRecord == nullptr || TicketRecord->State == EChunkWorldSpawnTicketState::Failed || TicketRecord->State == EChunkWorldSpawnTicketState::Canceled || TicketRecord->State == EChunkWorldSpawnTicketState::Ready)
	{
		return;
	}

	TicketRecord->State = EChunkWorldSpawnTicketState::Failed;
	TicketRecord->Result.bSuccess = false;
	TicketRecord->Result.FailureCategory = FailureCategory;
	TicketRecord->Result.DebugReason = DebugReason;
	TicketRecord->TerminalSequence = ++NextTerminalTicketSequence;
	UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Spawn ticket failed. Ticket=%s Request=%s Failure=%s Reason=%s"), *TicketId.ToString(), *TicketRecord->Result.RequestId.ToString(), ChunkWorldSpawn::ToString(FailureCategory), *DebugReason);
	PruneTerminalTickets(TicketId);
}

bool UChunkWorldSpawnComponent::IsPendingRequestValid(const FChunkWorldSpawnRequest& Request, EChunkWorldSpawnFailureCategory& OutFailureCategory, FString& OutFailureReason) const
{
	OutFailureCategory = EChunkWorldSpawnFailureCategory::None;
	OutFailureReason.Reset();
	if (!FMath::IsFinite(Request.PreferredSearchOrigin.X)
		|| !FMath::IsFinite(Request.PreferredSearchOrigin.Y)
		|| !FMath::IsFinite(Request.PreferredSearchOrigin.Z)
		|| !FMath::IsFinite(Request.MinimumSearchDistance)
		|| !FMath::IsFinite(Request.PreferredSearchRadius)
		|| Request.MinimumSearchDistance < 0.0f
		|| Request.PreferredSearchRadius < 0.0f)
	{
		OutFailureCategory = EChunkWorldSpawnFailureCategory::RequestInvalid;
		OutFailureReason = TEXT("Spawn requests require finite, non-negative search distances and a finite search origin.");
		return false;
	}
	if (Request.SubjectBinding.RequestOwner.IsStale())
	{
		OutFailureCategory = EChunkWorldSpawnFailureCategory::OwnerInvalid;
		OutFailureReason = TEXT("The request owner became invalid while the ticket was pending.");
		return false;
	}
	switch (Request.SubjectBinding.SubjectKind)
	{
	case EChunkWorldSpawnSubjectKind::ExistingActor:
		if (Request.SubjectBinding.ExistingActor.IsStale() || !Request.SubjectBinding.ExistingActor.IsValid())
		{
			OutFailureCategory = EChunkWorldSpawnFailureCategory::SubjectInvalid;
			OutFailureReason = TEXT("Existing-actor spawn requests require a valid existing actor binding.");
			return false;
		}
		break;
	case EChunkWorldSpawnSubjectKind::ControllerOwned:
		if (Request.SubjectBinding.OwningController.IsStale() || !Request.SubjectBinding.OwningController.IsValid())
		{
			OutFailureCategory = EChunkWorldSpawnFailureCategory::SubjectInvalid;
			OutFailureReason = TEXT("Controller-owned spawn requests require a valid owning controller binding.");
			return false;
		}
		break;
	case EChunkWorldSpawnSubjectKind::ExternalStableId:
		break;
	default:
		OutFailureCategory = EChunkWorldSpawnFailureCategory::SubjectInvalid;
		OutFailureReason = TEXT("Spawn requests require a recognized subject binding kind.");
		return false;
	}

	return true;
}

void UChunkWorldSpawnComponent::UpdateOutOfWorldTracking()
{
	UWorld* World = GetWorld();
	const AWorldSettings* WorldSettings = World != nullptr ? World->GetWorldSettings() : nullptr;
	if (WorldSettings == nullptr)
	{
		return;
	}

	const float KillZ = WorldSettings->KillZ;
	const double EventTimeSeconds = World->GetTimeSeconds();
	TArray<FChunkWorldOutOfWorldEvent> Events;
	TrackedExecutionActors.RemoveAll([World, KillZ, EventTimeSeconds, &Events](FTrackedSpawnActor& TrackedActor)
	{
		AActor* Actor = TrackedActor.Actor.Get();
		if (!IsValid(Actor) || Actor->GetWorld() != World)
		{
			return true;
		}

		const FVector ActorLocation = Actor->GetActorLocation();
		const bool bIsBelowKillZ = ActorLocation.Z < KillZ;
		if (bIsBelowKillZ && !TrackedActor.bWasBelowKillZ)
		{
			FChunkWorldOutOfWorldEvent& Event = Events.AddDefaulted_GetRef();
			Event.SubjectActor = Actor;
			Event.WorldLocation = ActorLocation;
			Event.KillZ = KillZ;
			Event.EventTimeSeconds = EventTimeSeconds;
		}
		TrackedActor.bWasBelowKillZ = bIsBelowKillZ;
		return false;
	});

	for (const FChunkWorldOutOfWorldEvent& Event : Events)
	{
		OnSubjectOutOfWorld.Broadcast(Event);
		UE_LOG(LogChunkWorldSpawnOutOfWorld, Log, TEXT("Observed tracked actor below KillZ. Actor=%s KillZ=%.2f Location=%s"), *GetNameSafe(Event.SubjectActor.Get()), Event.KillZ, *Event.WorldLocation.ToString());
	}
}

bool UChunkWorldSpawnComponent::RegisterTrackedSpawnActor(AActor* Actor)
{
	if (!IsAuthorityGameThread() || !IsValid(Actor) || Actor->GetWorld() != GetWorld())
	{
		UE_LOG(LogChunkWorldSpawnOutOfWorld, Warning, TEXT("Rejected invalid game actor registration for out-of-world tracking. Actor=%s"), *GetNameSafe(Actor));
		return false;
	}

	if (FTrackedSpawnActor* Existing = TrackedExecutionActors.FindByPredicate([Actor](const FTrackedSpawnActor& TrackedActor)
	{
		return TrackedActor.Actor.Get() == Actor;
	}))
	{
		Existing->bWasBelowKillZ = IsActorBelowKillZ(*Actor);
		return true;
	}

	const int32 TrackingLimit = FMath::Max(1, MaximumTrackedExecutionActors);
	if (TrackedExecutionActors.Num() >= TrackingLimit)
	{
		TrackedExecutionActors.RemoveAt(0, 1, EAllowShrinking::No);
		UE_LOG(LogChunkWorldSpawnOutOfWorld, Warning, TEXT("Discarded oldest tracked game actor to preserve the configured tracking bound. Limit=%d"), TrackingLimit);
	}

	FTrackedSpawnActor& TrackedActor = TrackedExecutionActors.AddDefaulted_GetRef();
	TrackedActor.Actor = Actor;
	TrackedActor.bWasBelowKillZ = IsActorBelowKillZ(*Actor);
	RefreshRuntimeTick();
	return true;
}

bool UChunkWorldSpawnComponent::UnregisterTrackedSpawnActor(AActor* Actor)
{
	if (!IsAuthorityGameThread() || !IsValid(Actor))
	{
		return false;
	}

	const int32 RemovedCount = TrackedExecutionActors.RemoveAll([Actor](const FTrackedSpawnActor& TrackedActor)
	{
		return TrackedActor.Actor.Get() == Actor;
	});
	RefreshRuntimeTick();
	return RemovedCount > 0;
}

FChunkWorldSpawnTicketHandle UChunkWorldSpawnComponent::CreateFailedTicket(
	const FChunkWorldSpawnRequest& Request,
	const EChunkWorldSpawnFailureCategory FailureCategory,
	const FString& DebugReason)
{
	FChunkWorldSpawnTicketHandle Handle;
	Handle.TicketId = FGuid::NewGuid();

	FSpawnTicketRecord& Record = TicketRecords.Add(Handle.TicketId);
	Record.Request = Request;
	Record.State = EChunkWorldSpawnTicketState::Failed;
	Record.Result.RequestId = Request.RequestId.IsValid() ? Request.RequestId : FGuid::NewGuid();
	Record.Result.bSuccess = false;
	Record.Result.FailureCategory = FailureCategory;
	Record.Result.DebugReason = DebugReason;
	Record.TerminalSequence = ++NextTerminalTicketSequence;

	UE_LOG(LogChunkWorldSpawn, Warning, TEXT("Spawn ticket failed. Ticket=%s Request=%s Failure=%s Reason=%s"), *Handle.TicketId.ToString(), *Record.Result.RequestId.ToString(), ChunkWorldSpawn::ToString(FailureCategory), *DebugReason);
	PruneTerminalTickets(Handle.TicketId);
	return Handle;
}

const UChunkWorldSpawnComponent::FSpawnTicketRecord* UChunkWorldSpawnComponent::FindTicket(const FChunkWorldSpawnTicketHandle Ticket) const
{
	return Ticket.IsValid() ? TicketRecords.Find(Ticket.TicketId) : nullptr;
}

UChunkWorldSpawnComponent::FSpawnTicketRecord* UChunkWorldSpawnComponent::FindTicket(const FChunkWorldSpawnTicketHandle Ticket)
{
	return Ticket.IsValid() ? TicketRecords.Find(Ticket.TicketId) : nullptr;
}

void UChunkWorldSpawnComponent::PruneInvalidRegistrations()
{
	RegisteredSourceProviders.RemoveAll([](const TWeakObjectPtr<UObject>& Provider)
	{
		return !Provider.IsValid();
	});

	if (!RegisteredOriginProvider.IsValid())
	{
		RegisteredOriginProvider.Reset();
	}
}
