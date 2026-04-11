// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldChaosGentleReleaseActor.h"

AChunkWorldChaosGentleReleaseActor::AChunkWorldChaosGentleReleaseActor()
{
	DestructionTuning.ExternalStrainMagnitude = 550000.0f;
	DestructionTuning.FractureFieldMode = EChunkWorldChaosFractureFieldMode::Uniform;
	DestructionTuning.bScaleStrainRadiusFromCollectionBounds = true;
	DestructionTuning.StrainRadiusBoundsMultiplier = 0.55f;
	DestructionTuning.FixedStrainRadius = 45.0f;
	DestructionTuning.StrainFalloff = EFieldFalloffType::Field_Falloff_Linear;
	DestructionTuning.bApplyGentleSeparationImpulse = false;
	DestructionTuning.bDisableSeparationImpulseDuringDiagnostics = true;
	DestructionTuning.SeparationImpulseDelaySeconds = 0.12f;
	DestructionTuning.SeparationImpulseStrength = 0.0f;
	DestructionTuning.SeparationImpulseRadius = 0.0f;
	DestructionTuning.bDelayCollisionUntilAfterFracture = true;
	DestructionTuning.CollisionEnableDelaySeconds = 0.15f;
	DestructionTuning.bUseTemporaryBottomAnchor = true;
	DestructionTuning.BottomAnchorHeightFraction = 0.12f;
	DestructionTuning.BottomAnchorReleaseDelaySeconds = 0.12f;
	DestructionTuning.BottomAnchorMaxLevel = -1;
	DestructionTuning.LifeSpanAfterTriggerSeconds = 6.0f;
}
