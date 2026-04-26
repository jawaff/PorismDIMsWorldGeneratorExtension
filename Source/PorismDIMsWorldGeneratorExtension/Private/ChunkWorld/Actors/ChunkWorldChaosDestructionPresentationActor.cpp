// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldChaosDestructionPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Field/FieldSystemComponent.h"
#include "Field/FieldSystemObjects.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Misc/StringBuilder.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldChaosDestructionPresentation, Log, All);

AChunkWorldChaosDestructionPresentationActor::AChunkWorldChaosDestructionPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	DestructionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DestructionRoot"));
	SetRootComponent(DestructionRoot);

	GeometryCollectionComponent = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("GeometryCollectionComponent"));
	GeometryCollectionComponent->SetupAttachment(DestructionRoot);
	GeometryCollectionComponent->SetIsReplicated(false);
	GeometryCollectionComponent->SetMobility(EComponentMobility::Movable);
	GeometryCollectionComponent->SetSimulatePhysics(true);
	GeometryCollectionComponent->SetEnableGravity(true);
	GeometryCollectionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GeometryCollectionComponent->SetGenerateOverlapEvents(false);
	GeometryCollectionComponent->SetLinearDamping(3.0f);
	GeometryCollectionComponent->SetAngularDamping(4.5f);

	FieldSystemComponent = CreateDefaultSubobject<UFieldSystemComponent>(TEXT("FieldSystemComponent"));
	FieldSystemComponent->SetupAttachment(DestructionRoot);
	FieldSystemComponent->SetIsReplicated(false);

	InitialLifeSpan = 0.0f;
}

void AChunkWorldChaosDestructionPresentationActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RefreshGeometryCollectionPresentation();
}

void AChunkWorldChaosDestructionPresentationActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AChunkWorldChaosDestructionPresentationActor, ReplicatedTriggerState);
}

void AChunkWorldChaosDestructionPresentationActor::TriggerBlockDestruction_Implementation(const FChunkWorldBlockDestructionRequest& Request)
{
	ExecuteFrameworkDestructionTrigger(Request);
}

void AChunkWorldChaosDestructionPresentationActor::ExecuteFrameworkDestructionTrigger(const FChunkWorldBlockDestructionRequest& Request)
{
	AcceptDestructionTrigger(Request, HasAuthority());
}

void AChunkWorldChaosDestructionPresentationActor::AcceptDestructionTrigger(const FChunkWorldBlockDestructionRequest& Request, bool bRecordReplicatedTriggerState)
{
	if (bHasTriggeredDestruction)
	{
		return;
	}

	bHasTriggeredDestruction = true;
	LastDestructionRequest = Request;

	if (bRecordReplicatedTriggerState && HasAuthority())
	{
		ReplicatedTriggerState.Request = Request;
		++ReplicatedTriggerState.TriggerSerial;
		ForceNetUpdate();
	}

	SetActorTransform(Request.SpawnTransform);
	RefreshGeometryCollectionPresentation();
	ApplyPreFractureCollisionStaging();
	ApplyTemporaryBottomAnchor();
	ApplyPendingPresentationState();
	HandleDestructionTriggered(Request);

	if (DestructionTuning.LifeSpanAfterTriggerSeconds > 0.0f)
	{
		SetLifeSpan(DestructionTuning.TriggerDelaySeconds + DestructionTuning.LifeSpanAfterTriggerSeconds);
	}

	if (UWorld* World = GetWorld())
	{
		if (DestructionTuning.TriggerDelaySeconds > 0.0f)
		{
			World->GetTimerManager().SetTimer(
				DestructionExecutionTimerHandle,
				this,
				&AChunkWorldChaosDestructionPresentationActor::HandleDelayedDestructionExecution,
				DestructionTuning.TriggerDelaySeconds,
				false);
			return;
		}

		World->GetTimerManager().SetTimerForNextTick(this, &AChunkWorldChaosDestructionPresentationActor::HandleDelayedDestructionExecution);
		return;
	}

	ExecuteDestructionPresentation();
}

void AChunkWorldChaosDestructionPresentationActor::OnRep_ReplicatedTriggerState()
{
	if (ReplicatedTriggerState.TriggerSerial <= 0)
	{
		return;
	}

	AcceptDestructionTrigger(ReplicatedTriggerState.Request, false);
}

void AChunkWorldChaosDestructionPresentationActor::ExecuteDestructionPresentation()
{
	RevealPresentationForExecution();

	const FVector FieldOrigin = ResolveFieldOrigin(LastDestructionRequest);
	const float ExternalStrainRadius = ResolveExternalStrainRadius();
	const float SeparationImpulseRadius = ResolveSeparationImpulseRadius();
	StartRuntimeDiagnostics();
	LastDiagnosticsMetrics.ResolvedFieldOrigin = FieldOrigin;
	LastDiagnosticsMetrics.ResolvedExternalStrainRadius = ExternalStrainRadius;
	LastDiagnosticsMetrics.ResolvedSeparationImpulseRadius = SeparationImpulseRadius;
	if (GeometryCollectionComponent != nullptr)
	{
		LastDiagnosticsMetrics.TriggerBoundsOrigin = GeometryCollectionComponent->Bounds.Origin;
		LastDiagnosticsMetrics.FieldOriginOffsetFromBoundsCenter = FVector::Distance(FieldOrigin, LastDiagnosticsMetrics.TriggerBoundsOrigin);
		LastDiagnosticsMetrics.NormalizedFieldOriginOffset =
			ExternalStrainRadius > KINDA_SMALL_NUMBER
				? LastDiagnosticsMetrics.FieldOriginOffsetFromBoundsCenter / ExternalStrainRadius
				: 0.0f;
	}

	ApplyExternalStrainField(FieldOrigin);
	if (GeometryCollectionComponent != nullptr)
	{
		GeometryCollectionComponent->WakeAllRigidBodies();
	}

	if (DestructionTuning.bUseTemporaryBottomAnchor && DestructionTuning.bReleaseTemporaryBottomAnchor)
	{
		if (UWorld* World = GetWorld())
		{
			if (DestructionTuning.BottomAnchorReleaseDelaySeconds > 0.0f)
			{
				World->GetTimerManager().SetTimer(
					BottomAnchorReleaseTimerHandle,
					this,
					&AChunkWorldChaosDestructionPresentationActor::HandleDelayedBottomAnchorRelease,
					DestructionTuning.BottomAnchorReleaseDelaySeconds,
					false);
			}
			else
			{
				ReleaseTemporaryBottomAnchor();
			}
		}
	}

	if (DestructionTuning.bDelayCollisionUntilAfterFracture)
	{
		if (UWorld* World = GetWorld())
		{
			if (DestructionTuning.CollisionEnableDelaySeconds > 0.0f)
			{
				World->GetTimerManager().SetTimer(
					CollisionEnableTimerHandle,
					this,
					&AChunkWorldChaosDestructionPresentationActor::HandleDelayedCollisionEnable,
					DestructionTuning.CollisionEnableDelaySeconds,
					false);
			}
			else
			{
				World->GetTimerManager().SetTimerForNextTick(this, &AChunkWorldChaosDestructionPresentationActor::HandleDelayedCollisionEnable);
			}
		}
	}
	else
	{
		RestorePostFractureCollisionResponses();
	}

	bSkippedSeparationImpulseForDiagnostics = ShouldSkipSeparationImpulseForCurrentTrigger();

	if (!DestructionTuning.bApplyGentleSeparationImpulse
		|| DestructionTuning.SeparationImpulseStrength <= 0.0f
		|| SeparationImpulseRadius <= 0.0f
		|| bSkippedSeparationImpulseForDiagnostics)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (DestructionTuning.SeparationImpulseDelaySeconds > 0.0f)
		{
			World->GetTimerManager().SetTimer(
				SeparationImpulseTimerHandle,
				this,
				&AChunkWorldChaosDestructionPresentationActor::HandleDelayedSeparationImpulse,
				DestructionTuning.SeparationImpulseDelaySeconds,
				false);
			return;
		}
	}

	ApplyGentleSeparationImpulse(FieldOrigin);
}

FVector AChunkWorldChaosDestructionPresentationActor::ResolveFieldOrigin(const FChunkWorldBlockDestructionRequest& Request) const
{
	const FTransform ActorTransform = GetActorTransform();
	FVector BaseOrigin = ActorTransform.GetLocation();

	if (DestructionTuning.bCenterFieldOriginOnCollectionBounds && GeometryCollectionComponent != nullptr)
	{
		BaseOrigin = GeometryCollectionComponent->Bounds.Origin;
	}
	else if (!Request.RepresentativeWorldPos.IsNearlyZero())
	{
		BaseOrigin = Request.RepresentativeWorldPos;
	}

	return BaseOrigin + ActorTransform.TransformVectorNoScale(DestructionTuning.StrainFieldLocalOffset);
}

float AChunkWorldChaosDestructionPresentationActor::ResolveExternalStrainRadius() const
{
	if (!DestructionTuning.bScaleStrainRadiusFromCollectionBounds || GeometryCollectionComponent == nullptr)
	{
		return DestructionTuning.FixedStrainRadius;
	}

	return GeometryCollectionComponent->Bounds.SphereRadius * DestructionTuning.StrainRadiusBoundsMultiplier;
}

float AChunkWorldChaosDestructionPresentationActor::ResolveSeparationImpulseRadius() const
{
	return DestructionTuning.SeparationImpulseRadius;
}

void AChunkWorldChaosDestructionPresentationActor::ApplyExternalStrainField(const FVector& FieldOrigin)
{
	if (GeometryCollectionComponent == nullptr || DestructionTuning.ExternalStrainMagnitude <= 0.0f)
	{
		return;
	}

	if (DestructionTuning.FractureFieldMode == EChunkWorldChaosFractureFieldMode::Uniform)
	{
		// Project-specific change: the gentle break-apart path uses uniform strain so heavy props release evenly and gravity drives the visible motion instead of a blast-like focal point.
		UUniformScalar* UniformStrainField = NewObject<UUniformScalar>(this);
		if (UniformStrainField == nullptr)
		{
			return;
		}

		UniformStrainField->SetUniformScalar(DestructionTuning.ExternalStrainMagnitude);
		GeometryCollectionComponent->ApplyPhysicsField(
			true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
			nullptr,
			UniformStrainField);
		return;
	}

	// Project-specific change: use one transient radial strain field for localized fracture patterns when subclasses explicitly want a directional break.
	URadialFalloff* StrainField = NewObject<URadialFalloff>(this);
	if (StrainField == nullptr)
	{
		return;
	}

	StrainField->SetRadialFalloff(
		DestructionTuning.ExternalStrainMagnitude,
		0.0f,
		1.0f,
		0.0f,
		LastDiagnosticsMetrics.ResolvedExternalStrainRadius > 0.0f ? LastDiagnosticsMetrics.ResolvedExternalStrainRadius : ResolveExternalStrainRadius(),
		FieldOrigin,
		DestructionTuning.StrainFalloff);

	GeometryCollectionComponent->ApplyPhysicsField(
		true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr,
		StrainField);
}

void AChunkWorldChaosDestructionPresentationActor::ApplyGentleSeparationImpulse(const FVector& FieldOrigin)
{
	const float SeparationImpulseRadius = LastDiagnosticsMetrics.ResolvedSeparationImpulseRadius > 0.0f
		? LastDiagnosticsMetrics.ResolvedSeparationImpulseRadius
		: ResolveSeparationImpulseRadius();
	if (GeometryCollectionComponent == nullptr || DestructionTuning.SeparationImpulseStrength <= 0.0f || SeparationImpulseRadius <= 0.0f)
	{
		return;
	}

	// Project-specific change: prefer a small delayed impulse here so heavy props separate enough to read as broken while gravity remains the dominant motion.
	GeometryCollectionComponent->AddRadialImpulse(
		FieldOrigin,
		SeparationImpulseRadius,
		DestructionTuning.SeparationImpulseStrength,
		DestructionTuning.SeparationImpulseFalloff,
		true);
}

void AChunkWorldChaosDestructionPresentationActor::ApplyPreFractureCollisionStaging()
{
	if (GeometryCollectionComponent == nullptr)
	{
		return;
	}

	if (!DestructionTuning.bDelayCollisionUntilAfterFracture)
	{
		RestorePostFractureCollisionResponses();
		return;
	}

	// Keep Chaos physics active while temporarily ignoring world contacts so overlap kickout
	// from the source presentation does not dominate the initial fracture step.
	GeometryCollectionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GeometryCollectionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
	GeometryCollectionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
}

void AChunkWorldChaosDestructionPresentationActor::RestorePostFractureCollisionResponses()
{
	if (GeometryCollectionComponent == nullptr)
	{
		return;
	}

	GeometryCollectionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GeometryCollectionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	GeometryCollectionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
}

void AChunkWorldChaosDestructionPresentationActor::ApplyTemporaryBottomAnchor()
{
	if (GeometryCollectionComponent == nullptr || !DestructionTuning.bUseTemporaryBottomAnchor)
	{
		return;
	}

	const FBoxSphereBounds Bounds = GeometryCollectionComponent->Bounds;
	const FVector Extents = Bounds.BoxExtent;
	if (Extents.Z <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float HeightFraction = FMath::Clamp(DestructionTuning.BottomAnchorHeightFraction, 0.0f, 1.0f);
	if (HeightFraction <= 0.0f)
	{
		return;
	}

	const FVector LocalMin(-Extents.X, -Extents.Y, -Extents.Z);
	const FVector LocalMax(Extents.X, Extents.Y, -Extents.Z + (Extents.Z * 2.0f * HeightFraction));
	const FBox LocalAnchorBox(LocalMin, LocalMax);

	GeometryCollectionComponent->SetAnchoredByTransformedBox(
		LocalAnchorBox,
		GeometryCollectionComponent->GetComponentTransform(),
		true,
		DestructionTuning.BottomAnchorMaxLevel);
}

void AChunkWorldChaosDestructionPresentationActor::ReleaseTemporaryBottomAnchor()
{
	if (GeometryCollectionComponent == nullptr || !DestructionTuning.bUseTemporaryBottomAnchor)
	{
		return;
	}

	const FBoxSphereBounds Bounds = GeometryCollectionComponent->Bounds;
	const FVector Extents = Bounds.BoxExtent;
	if (Extents.IsNearlyZero())
	{
		return;
	}

	const FBox LocalReleaseBox(-Extents, Extents);
	GeometryCollectionComponent->SetAnchoredByTransformedBox(
		LocalReleaseBox,
		GeometryCollectionComponent->GetComponentTransform(),
		false,
		DestructionTuning.BottomAnchorMaxLevel);
}

void AChunkWorldChaosDestructionPresentationActor::HandleDestructionTriggered(const FChunkWorldBlockDestructionRequest& Request)
{
	ReceiveDestructionTriggered(Request);
}

void AChunkWorldChaosDestructionPresentationActor::StartRuntimeDiagnostics()
{
	if (!DiagnosticsConfig.bEnableRuntimeDiagnostics || GeometryCollectionComponent == nullptr)
	{
		LastDiagnosticsSuggestionSummary.Reset();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RuntimeDiagnosticsSampleTimerHandle);
		World->GetTimerManager().ClearTimer(RuntimeDiagnosticsFinalizeTimerHandle);
		RuntimeDiagnosticsStartTimeSeconds = World->GetTimeSeconds();
	}

	bRuntimeDiagnosticsActive = true;
	LastDiagnosticsSuggestionSummary.Reset();
	LastDiagnosticsMetrics = FChunkWorldChaosDestructionDiagnosticsMetrics();
	LastDiagnosticsMetrics.InitialBoundsOrigin = GeometryCollectionComponent->Bounds.Origin;
	LastDiagnosticsMetrics.InitialBoundsSphereRadius = GeometryCollectionComponent->Bounds.SphereRadius;
	LastDiagnosticsMetrics.PeakBoundsSphereRadius = GeometryCollectionComponent->Bounds.SphereRadius;
	LastDiagnosticsMetrics.TriggerBoundsOrigin = GeometryCollectionComponent->Bounds.Origin;
	bSkippedSeparationImpulseForDiagnostics = false;

	CaptureRuntimeDiagnosticsSample();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RuntimeDiagnosticsSampleTimerHandle,
			this,
			&AChunkWorldChaosDestructionPresentationActor::HandleRuntimeDiagnosticsSample,
			DiagnosticsConfig.SampleIntervalSeconds,
			true);
		World->GetTimerManager().SetTimer(
			RuntimeDiagnosticsFinalizeTimerHandle,
			this,
			&AChunkWorldChaosDestructionPresentationActor::HandleFinalizeRuntimeDiagnostics,
			DiagnosticsConfig.EvaluationWindowSeconds,
			false);
	}
}

void AChunkWorldChaosDestructionPresentationActor::CaptureRuntimeDiagnosticsSample()
{
	if (!bRuntimeDiagnosticsActive || GeometryCollectionComponent == nullptr)
	{
		return;
	}

	const FVector CurrentOrigin = GeometryCollectionComponent->Bounds.Origin;
	const FVector CurrentVelocity = GeometryCollectionComponent->GetPhysicsLinearVelocity();
	const FVector HorizontalOffset = FVector(
		CurrentOrigin.X - LastDiagnosticsMetrics.InitialBoundsOrigin.X,
		CurrentOrigin.Y - LastDiagnosticsMetrics.InitialBoundsOrigin.Y,
		0.0f);
	const float HorizontalDrift = HorizontalOffset.Size();
	const float UpwardDisplacement = FMath::Max(0.0f, CurrentOrigin.Z - LastDiagnosticsMetrics.InitialBoundsOrigin.Z);
	const float CurrentSpeed = CurrentVelocity.Size();
	const float CurrentUpwardVelocity = FMath::Max(0.0f, CurrentVelocity.Z);
	const float CurrentBoundsSphereRadius = GeometryCollectionComponent->Bounds.SphereRadius;

	LastDiagnosticsMetrics.MaxHorizontalDrift = FMath::Max(LastDiagnosticsMetrics.MaxHorizontalDrift, HorizontalDrift);
	LastDiagnosticsMetrics.MaxUpwardDisplacement = FMath::Max(LastDiagnosticsMetrics.MaxUpwardDisplacement, UpwardDisplacement);
	LastDiagnosticsMetrics.MaxComponentSpeed = FMath::Max(LastDiagnosticsMetrics.MaxComponentSpeed, CurrentSpeed);
	LastDiagnosticsMetrics.MaxUpwardVelocity = FMath::Max(LastDiagnosticsMetrics.MaxUpwardVelocity, CurrentUpwardVelocity);
	LastDiagnosticsMetrics.PeakBoundsSphereRadius = FMath::Max(LastDiagnosticsMetrics.PeakBoundsSphereRadius, CurrentBoundsSphereRadius);
	LastDiagnosticsMetrics.bObservedVisibleBreakup |=
		(CurrentBoundsSphereRadius - LastDiagnosticsMetrics.InitialBoundsSphereRadius) >= DiagnosticsConfig.MinimumExpectedSpreadIncrease;

	if (UWorld* World = GetWorld())
	{
		LastDiagnosticsMetrics.SampledDurationSeconds = World->GetTimeSeconds() - RuntimeDiagnosticsStartTimeSeconds;
	}
}

void AChunkWorldChaosDestructionPresentationActor::FinalizeRuntimeDiagnostics()
{
	if (!bRuntimeDiagnosticsActive)
	{
		return;
	}

	bRuntimeDiagnosticsActive = false;
	CaptureRuntimeDiagnosticsSample();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RuntimeDiagnosticsSampleTimerHandle);
		World->GetTimerManager().ClearTimer(RuntimeDiagnosticsFinalizeTimerHandle);
	}

	TArray<FString> Suggestions;
	BuildDiagnosticsSuggestions(Suggestions);

	if (Suggestions.IsEmpty())
	{
		Suggestions.Add(TEXT("Observed motion fits the current gentle break-and-slump target."));
	}

	LastDiagnosticsSuggestionSummary = FString::Join(Suggestions, TEXT(" "));

	if (DiagnosticsConfig.bLogSuggestions)
	{
		UE_LOG(
			LogChunkWorldChaosDestructionPresentation,
			Log,
			TEXT("%s diagnostics for block %s: %s Metrics: SpreadDelta=%.2f HorizontalDrift=%.2f UpwardDisplacement=%.2f MaxSpeed=%.2f MaxUpwardVelocity=%.2f FieldOrigin=%s BoundsOrigin=%s FieldOffset=%.2f NormalizedFieldOffset=%.2f StrainRadius=%.2f SeparationRadius=%.2f SeparationSkipped=%d Duration=%.2f"),
			*GetNameSafe(this),
			*LastDestructionRequest.BlockWorldPos.ToString(),
			*LastDiagnosticsSuggestionSummary,
			LastDiagnosticsMetrics.PeakBoundsSphereRadius - LastDiagnosticsMetrics.InitialBoundsSphereRadius,
			LastDiagnosticsMetrics.MaxHorizontalDrift,
			LastDiagnosticsMetrics.MaxUpwardDisplacement,
			LastDiagnosticsMetrics.MaxComponentSpeed,
			LastDiagnosticsMetrics.MaxUpwardVelocity,
			*LastDiagnosticsMetrics.ResolvedFieldOrigin.ToCompactString(),
			*LastDiagnosticsMetrics.TriggerBoundsOrigin.ToCompactString(),
			LastDiagnosticsMetrics.FieldOriginOffsetFromBoundsCenter,
			LastDiagnosticsMetrics.NormalizedFieldOriginOffset,
			LastDiagnosticsMetrics.ResolvedExternalStrainRadius,
			LastDiagnosticsMetrics.ResolvedSeparationImpulseRadius,
			bSkippedSeparationImpulseForDiagnostics ? 1 : 0,
			LastDiagnosticsMetrics.SampledDurationSeconds);
	}
}

void AChunkWorldChaosDestructionPresentationActor::BuildDiagnosticsSuggestions(TArray<FString>& OutSuggestions) const
{
	const float ObservedSpreadDelta = LastDiagnosticsMetrics.PeakBoundsSphereRadius - LastDiagnosticsMetrics.InitialBoundsSphereRadius;

	if (!LastDiagnosticsMetrics.bObservedVisibleBreakup)
	{
		OutSuggestions.Add(FString::Printf(
			TEXT("Breakup looked weak. Increase ExternalStrainMagnitude above %.0f or slightly widen the strain radius."),
			DestructionTuning.ExternalStrainMagnitude));
	}

	if (LastDiagnosticsMetrics.MaxHorizontalDrift > DiagnosticsConfig.TargetMaxHorizontalDrift)
	{
		OutSuggestions.Add(FString::Printf(
			TEXT("Horizontal drift reached %.1f, above the %.1f target. Lower SeparationImpulseStrength below %.0f and consider moving StrainFieldLocalOffset closer to the collection center."),
			LastDiagnosticsMetrics.MaxHorizontalDrift,
			DiagnosticsConfig.TargetMaxHorizontalDrift,
			DestructionTuning.SeparationImpulseStrength));
	}

	if (LastDiagnosticsMetrics.MaxUpwardDisplacement > DiagnosticsConfig.TargetMaxUpwardDisplacement)
	{
		OutSuggestions.Add(FString::Printf(
			TEXT("Upward lift reached %.1f, above the %.1f target. Reduce SeparationImpulseStrength or shorten SeparationImpulseRadius so gravity takes over earlier."),
			LastDiagnosticsMetrics.MaxUpwardDisplacement,
			DiagnosticsConfig.TargetMaxUpwardDisplacement));
	}

	if (LastDiagnosticsMetrics.MaxComponentSpeed > DiagnosticsConfig.TargetMaxComponentSpeed)
	{
		OutSuggestions.Add(FString::Printf(
			TEXT("Peak speed reached %.1f, above the %.1f gentle-motion target. Lower SeparationImpulseStrength and, if spread already looks broad, reduce StrainRadiusBoundsMultiplier or FixedStrainRadius."),
			LastDiagnosticsMetrics.MaxComponentSpeed,
			DiagnosticsConfig.TargetMaxComponentSpeed));
	}

	if (ObservedSpreadDelta > DiagnosticsConfig.MinimumExpectedSpreadIncrease * 3.0f)
	{
		OutSuggestions.Add(FString::Printf(
			TEXT("Spread delta reached %.1f, which suggests a wide breakup. If the result feels explosive, reduce ExternalStrainMagnitude from %.0f or tighten the strain radius slightly."),
			ObservedSpreadDelta,
			DestructionTuning.ExternalStrainMagnitude));
	}

	if (LastDiagnosticsMetrics.NormalizedFieldOriginOffset > DiagnosticsConfig.TargetMaxNormalizedFieldOriginOffset)
	{
		OutSuggestions.Add(FString::Printf(
			TEXT("Field origin offset normalized to %.2f, above the %.2f centering target. Keep bCenterFieldOriginOnCollectionBounds enabled and reduce StrainFieldLocalOffset so the field stays closer to the collection center."),
			LastDiagnosticsMetrics.NormalizedFieldOriginOffset,
			DiagnosticsConfig.TargetMaxNormalizedFieldOriginOffset));
	}

	if (bSkippedSeparationImpulseForDiagnostics)
	{
		if (LastDiagnosticsMetrics.bObservedVisibleBreakup
			&& LastDiagnosticsMetrics.MaxHorizontalDrift <= DiagnosticsConfig.TargetMaxHorizontalDrift
			&& LastDiagnosticsMetrics.MaxUpwardDisplacement <= DiagnosticsConfig.TargetMaxUpwardDisplacement)
		{
			OutSuggestions.Add(TEXT("Diagnostics skipped the separation impulse and the strain-only result stayed controlled. If full behavior still launches chunks, the separation impulse is the main source of variance."));
		}
		else if (!LastDiagnosticsMetrics.bObservedVisibleBreakup)
		{
			OutSuggestions.Add(TEXT("Diagnostics skipped the separation impulse and breakup still looked weak. Raise strain first before reintroducing any separation energy."));
		}
	}

	if (OutSuggestions.IsEmpty() && DiagnosticsConfig.bEnableRuntimeDiagnostics)
	{
		OutSuggestions.Add(TEXT("Current tuning stayed within the configured gentle-slump thresholds."));
	}
}

bool AChunkWorldChaosDestructionPresentationActor::ShouldSkipSeparationImpulseForCurrentTrigger() const
{
	return DiagnosticsConfig.bEnableRuntimeDiagnostics && DestructionTuning.bDisableSeparationImpulseDuringDiagnostics;
}

void AChunkWorldChaosDestructionPresentationActor::RefreshGeometryCollectionPresentation()
{
	if (GeometryCollectionComponent == nullptr)
	{
		return;
	}

	GeometryCollectionComponent->SetRelativeLocation(DestructionTuning.GeometryCollectionRelativeOffset);
	GeometryCollectionComponent->SetRestCollection(DestructionTuning.GeometryCollectionAsset);
}

void AChunkWorldChaosDestructionPresentationActor::ApplyPendingPresentationState()
{
	SetActorHiddenInGame(true);

	if (GeometryCollectionComponent == nullptr)
	{
		return;
	}

	GeometryCollectionComponent->SetVisibility(false, true);
	GeometryCollectionComponent->SetHiddenInGame(true, true);
}

void AChunkWorldChaosDestructionPresentationActor::RevealPresentationForExecution()
{
	SetActorHiddenInGame(false);

	if (GeometryCollectionComponent == nullptr)
	{
		return;
	}

	GeometryCollectionComponent->SetHiddenInGame(false, true);
	GeometryCollectionComponent->SetVisibility(true, true);
	GeometryCollectionComponent->SetSimulatePhysics(true);
	GeometryCollectionComponent->WakeAllRigidBodies();

	if (DestructionTuning.bDelayCollisionUntilAfterFracture)
	{
		GeometryCollectionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GeometryCollectionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
		GeometryCollectionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	}
}

void AChunkWorldChaosDestructionPresentationActor::HandleDelayedDestructionExecution()
{
	ExecuteDestructionPresentation();
}

void AChunkWorldChaosDestructionPresentationActor::HandleDelayedSeparationImpulse()
{
	ApplyGentleSeparationImpulse(ResolveFieldOrigin(LastDestructionRequest));
}

void AChunkWorldChaosDestructionPresentationActor::HandleDelayedCollisionEnable()
{
	RestorePostFractureCollisionResponses();
}

void AChunkWorldChaosDestructionPresentationActor::HandleDelayedBottomAnchorRelease()
{
	ReleaseTemporaryBottomAnchor();
}

void AChunkWorldChaosDestructionPresentationActor::HandleRuntimeDiagnosticsSample()
{
	CaptureRuntimeDiagnosticsSample();
}

void AChunkWorldChaosDestructionPresentationActor::HandleFinalizeRuntimeDiagnostics()
{
	FinalizeRuntimeDiagnostics();
}
