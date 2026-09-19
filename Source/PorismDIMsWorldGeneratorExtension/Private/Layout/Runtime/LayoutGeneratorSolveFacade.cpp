// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Runtime/LayoutGeneratorSolveFacade.h"

#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"

#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"

const FLayoutRegionContentEntry* FLayoutGeneratorSolveFacade::FindFrozenPlacementSourceEntry(
	const ULayoutRegionContentSetAsset* ContentSet, const FLayoutPlacedModule& Placement)
{
	return LayoutWorldBindingRuntimeHelpers::FindFrozenPlacementSourceEntry(ContentSet, Placement);
}

FLayoutBackgroundSolveHandle FLayoutGeneratorSolveFacade::SubmitExplicitRoot(
	const FLayoutGeneratorSolveRequest& Request,
	TFunction<void(const FLayoutGeneratorSolveResult& Result)> OnCompleted,
	FLayoutGeneratorSolveAsyncState* const AsyncState)
{
	check(IsInGameThread());

		const uint64 SubmitGeneration = AsyncState != nullptr ? ++AsyncState->ActiveGeneration : 0;
	if (AsyncState != nullptr)
	{
		CancelSubmittedSolve(Request.RuntimeComponent, *AsyncState);
	}

	FLayoutGeneratorSolveResult ImmediateFailure;
	const double StartSeconds = FPlatformTime::Seconds();
	if (Request.RuntimeComponent == nullptr)
	{
		ImmediateFailure.FailureReason = TEXT("Layout generator solve requires a runtime component.");
		ImmediateFailure.TotalSolveSeconds = FPlatformTime::Seconds() - StartSeconds;
		if (OnCompleted)
		{
			OnCompleted(ImmediateFailure);
		}
		return FLayoutBackgroundSolveHandle();
	}

	if (Request.LayoutProfile == nullptr)
	{
		ImmediateFailure.FailureReason = TEXT("Layout generator solve requires a layout profile.");
		ImmediateFailure.TotalSolveSeconds = FPlatformTime::Seconds() - StartSeconds;
		if (OnCompleted)
		{
			OnCompleted(ImmediateFailure);
		}
		return FLayoutBackgroundSolveHandle();
	}

	FLayoutWorldBindingRuntimeView RuntimeView;
	TOptional<FLayoutWorldBindingSiteFrontendSelection> FrontendSelection;
	FString FailureReason;
	FIntVector SiteCenterBlockWorldPos = Request.RequestedSiteCenterBlockWorldPos;
	if (Request.WorldBinding != nullptr)
	{
		FLayoutWorldBindingSiteFrontendSelection LocalFrontendSelection;
		if (!LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRootRuntimeViewFromWorldBindingProfile(
			Request.WorldBinding,
			Request.LayoutProfile,
			RuntimeView,
			LocalFrontendSelection,
			FailureReason))
		{
			ImmediateFailure.FailureReason = FailureReason;
			ImmediateFailure.TotalSolveSeconds = FPlatformTime::Seconds() - StartSeconds;
			if (OnCompleted)
			{
				OnCompleted(ImmediateFailure);
			}
			return FLayoutBackgroundSolveHandle();
		}

		LocalFrontendSelection.CompatibleBiomeRowNames.Reset();
		// Explicit tool placement uses hovered active terrain; binding data only supplies
		// runtime policy, including continuation-family policy.
		LocalFrontendSelection.bUseAnyActiveBiomeSurface = true;
		FrontendSelection = LocalFrontendSelection;
	}
	else
	{
		ULayoutRegionContentSetAsset* const EffectiveContentSet =
			LayoutWorldBindingRuntimeHelpers::ResolveRuntimePreferredContentSet(Request.LayoutProfile);
		RuntimeView = LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Request.LayoutProfile, EffectiveContentSet, FLayoutRootSolveBudgetSettings(),
			FLayoutWorldBindingPlacementPolicy());
	}

	TSharedRef<FLayoutBackgroundSolveHandle, ESPMode::ThreadSafe> SubmittedHandleRef =
		MakeShared<FLayoutBackgroundSolveHandle, ESPMode::ThreadSafe>();
	*SubmittedHandleRef = Request.RuntimeComponent->SubmitExplicitRootLayoutSiteSolve(
		SiteCenterBlockWorldPos,
		RuntimeView,
		Request.SolveSeed,
		[
			StartSeconds,
			RuntimeComponent = Request.RuntimeComponent,
			AsyncState,
			SubmitGeneration,
			SubmittedHandleRef,
			OnCompleted = MoveTemp(OnCompleted)](
				const bool bSucceeded,
				const FResolvedLayoutSiteRecord& SiteRecord,
				const FLayoutRegionSolveScheduleResult& ScheduleResult,
				const FString& CompletionFailureReason) mutable
		{
			if (AsyncState != nullptr
				&& (AsyncState->ActiveGeneration != SubmitGeneration
					|| AsyncState->ActiveHandle != *SubmittedHandleRef))
			{
				return;
			}

			FLayoutGeneratorSolveResult Result;
			Result.bSucceeded = bSucceeded;
			Result.SiteRecord = SiteRecord;
			Result.ScheduleResult = ScheduleResult;
			Result.FailureReason = CompletionFailureReason;
			if (Result.bSucceeded)
			{
				if (Result.ScheduleResult.MergedSolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::None)
				{
					if (RuntimeComponent == nullptr
						|| !RuntimeComponent->TryGetAcceptedFrozenTerrainContract(
							Result.SiteRecord,
							Result.FrozenTerrainContract,
							&Result.FailureReason)
						|| Result.FrozenTerrainContract.ContractId.IsNone())
					{
						Result.bSucceeded = false;
						if (Result.FailureReason.IsEmpty())
						{
							Result.FailureReason = TEXT("Layout generator explicit-root preview could not resolve an accepted frozen terrain contract.");
						}
					}
				}
				else
				{
					Result.bSucceeded = false;
					Result.FailureReason = TEXT("Layout generator explicit-root preview requires solved-artifact frozen terrain-contract provenance; compatibility-only terrain contracts are disabled.");
				}
			}
			else if (Result.FailureReason.IsEmpty())
			{
				Result.FailureReason = TEXT("Layout generator explicit-root background solve failed.");
			}
			Result.TotalSolveSeconds = FPlatformTime::Seconds() - StartSeconds;
			if (OnCompleted)
			{
				OnCompleted(Result);
			}
			if (AsyncState != nullptr)
			{
				AsyncState->ActiveHandle.Reset();
			}
		},
		FrontendSelection.IsSet() ? &FrontendSelection.GetValue() : nullptr);
	if (AsyncState != nullptr)
	{
		AsyncState->ActiveHandle = *SubmittedHandleRef;
	}
	return *SubmittedHandleRef;
}

FLayoutBackgroundSolveHandle FLayoutGeneratorSolveFacade::SubmitExplicitContinuation(
	const FLayoutGeneratorContinuationSolveRequest& Request,
	TFunction<void(const FLayoutGeneratorContinuationSolveResult& Result)> OnCompleted,
	FLayoutGeneratorSolveAsyncState* const AsyncState)
{
	check(IsInGameThread());
	const uint64 SubmitGeneration = AsyncState != nullptr ? ++AsyncState->ActiveGeneration : 0;
	if (AsyncState != nullptr)
	{
		CancelSubmittedSolve(Request.RuntimeComponent, *AsyncState);
	}
	const double StartSeconds = FPlatformTime::Seconds();
	FLayoutGeneratorContinuationSolveResult ImmediateFailure;
	auto CompleteFailure = [&OnCompleted, StartSeconds](FLayoutGeneratorContinuationSolveResult& Result, const FString& FailureReason)
	{
		Result.FailureReason = FailureReason;
		Result.TotalSolveSeconds = FPlatformTime::Seconds() - StartSeconds;
		if (OnCompleted)
		{
			OnCompleted(Result);
		}
	};
	if (Request.RuntimeComponent == nullptr || Request.WorldBinding == nullptr || Request.ContinuationProfile == nullptr)
	{
		CompleteFailure(ImmediateFailure, TEXT("Layout Generator continuation solve requires a runtime component, world binding, and continuation profile."));
		return FLayoutBackgroundSolveHandle();
	}

	FResolvedLayoutConnectorRecord ConnectorRecord;
	FString EdgeKey;
	FString FailureReason;
	if (!Request.RuntimeComponent->TryReserveExplicitContinuationEndpointPair(
			Request.StartEndpoint,
			Request.EndEndpoint,
			Request.WorldBinding,
			Request.ContinuationProfile,
			Request.SolveSeed,
			ConnectorRecord,
			EdgeKey,
			FailureReason))
	{
		CompleteFailure(ImmediateFailure, FailureReason);
		return FLayoutBackgroundSolveHandle();
	}

	TSharedRef<FLayoutBackgroundSolveHandle, ESPMode::ThreadSafe> SubmittedHandleRef =
		MakeShared<FLayoutBackgroundSolveHandle, ESPMode::ThreadSafe>();
	*SubmittedHandleRef = Request.RuntimeComponent->SubmitExplicitContinuationLayoutSolve(
		ConnectorRecord,
		Request.WorldBinding,
		EdgeKey,
		[AsyncState, SubmitGeneration, SubmittedHandleRef, StartSeconds, OnCompleted = MoveTemp(OnCompleted)](
			const FLayoutExplicitContinuationSolveResult& RuntimeResult) mutable
		{
			if (AsyncState != nullptr
				&& (AsyncState->ActiveGeneration != SubmitGeneration
					|| AsyncState->ActiveHandle != *SubmittedHandleRef))
			{
				return;
			}
			FLayoutGeneratorContinuationSolveResult Result;
			Result.Route = RuntimeResult.Route;
			for (const FLayoutExplicitContinuationSegmentSolveResult& RuntimeSegment : RuntimeResult.Segments)
			{
				FLayoutGeneratorContinuationSegmentSolveResult& Segment = Result.Segments.AddDefaulted_GetRef();
				Segment.Descriptor = RuntimeSegment.Descriptor;
				Segment.bSucceeded = RuntimeSegment.bSucceeded;
				Segment.bIsPartial = RuntimeSegment.bIsPartial;
				Segment.ConnectorRecord = RuntimeSegment.ConnectorRecord;
				Segment.ScheduleResult = RuntimeSegment.ScheduleResult;
				Segment.FrozenTerrainContract = RuntimeSegment.FrozenTerrainContract;
				Segment.PreviewGeometry = RuntimeSegment.PreviewGeometry;
				Segment.FailureReason = RuntimeSegment.FailureReason;
			}
			Result.bSucceeded = RuntimeResult.bSucceeded;
			Result.bIsPartial = RuntimeResult.bIsPartial;
			Result.FailureReason = RuntimeResult.FailureReason;
			if (!Result.bSucceeded && !Result.bIsPartial && Result.FailureReason.IsEmpty())
			{
				Result.FailureReason = TEXT("Layout Generator continuation preview did not publish an accepted segment artifact.");
			}
			Result.TotalSolveSeconds = FPlatformTime::Seconds() - StartSeconds;
			if (OnCompleted)
			{
				OnCompleted(Result);
			}
			if (AsyncState != nullptr)
			{
				AsyncState->ActiveHandle.Reset();
			}
		});
	if (AsyncState != nullptr)
	{
		AsyncState->ActiveHandle = *SubmittedHandleRef;
	}
	return *SubmittedHandleRef;
}

void FLayoutGeneratorSolveFacade::CancelSubmittedSolve(
	UChunkWorldLayoutRuntimeComponent* const RuntimeComponent,
	FLayoutGeneratorSolveAsyncState& AsyncState)
{
	check(IsInGameThread());
	if (RuntimeComponent != nullptr && AsyncState.ActiveHandle.IsValid())
	{
		RuntimeComponent->CancelBackgroundLayoutSolve(AsyncState.ActiveHandle);
	}
	AsyncState.ActiveHandle.Reset();
}
