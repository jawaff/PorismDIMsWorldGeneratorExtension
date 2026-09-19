// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/FastNoiseEditor.h"

struct FFoundationProviderDefinition;
struct FInstancedStruct;
struct FResolvedWorldGenScaleContext;
enum class EFoundationProviderType : uint8;

/** Inputs shared by foundation terrain-profile handlers when they build FastNoise surface offsets. */
struct FFoundationTerrainProfileBuildContext
{
	UFastNoiseEditor* Editor = nullptr;
	const FFoundationProviderDefinition* Provider = nullptr;
	const FResolvedWorldGenScaleContext* ScaleContext = nullptr;
	FVector2D ProviderOriginXYBlocks = FVector2D::ZeroVector;
};

/** Structured validation issue emitted by private terrain-profile handlers. */
struct FFoundationTerrainProfileValidationIssue
{
	FString Path;
	FString Message;
	FString Fix;
};

/** Builds the authored top-surface offset node for the profile payload, or an invalid link for a flat/no-op profile. */
FNodeLink BuildFoundationTerrainProfileSurfaceOffsetNode(
	const FInstancedStruct& TerrainProfile,
	const FFoundationTerrainProfileBuildContext& Context);

/** Returns the maximum positive top-surface height contribution in authored blocks for domain top padding. */
float GetFoundationTerrainProfileMaxPositiveHeightBlocks(const FInstancedStruct& TerrainProfile);

/** Returns the total signed surface-search amplitude in authored blocks for runtime surface probing. */
float GetFoundationTerrainProfileSurfaceSearchAmplitudeBlocks(const FInstancedStruct& TerrainProfile);

/** Applies repeated-provider uniform scale to authored-block profile fields. */
bool ApplyFoundationTerrainProfileUniformScale(FInstancedStruct& TerrainProfile, float UniformScale);

/** Appends blocking configuration issues for the concrete profile payload type. */
void AppendFoundationTerrainProfileValidationIssues(
	const FFoundationProviderDefinition& Provider,
	const FString& Path,
	TArray<FFoundationTerrainProfileValidationIssue>& OutIssues);

/** Reports whether the profile payload type has a registered runtime handler. */
bool IsFoundationTerrainProfilePayloadSupported(const FInstancedStruct& TerrainProfile);

/** Reports whether the profile payload type is compatible with the selected provider family. */
bool IsFoundationTerrainProfilePayloadSupportedByProvider(const FInstancedStruct& TerrainProfile, EFoundationProviderType ProviderType);

/** Returns the supported profile display names for validation messages. */
FString GetSupportedFoundationTerrainProfileNames();

/** Returns the supported profile display names for the selected provider family. */
FString GetSupportedFoundationTerrainProfileNamesForProvider(EFoundationProviderType ProviderType);
