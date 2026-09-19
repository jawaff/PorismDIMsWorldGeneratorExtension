// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"

/**
 * Immutable pointer-free profile snapshot for manifest cache identity.
 *
 * Semantic id (stable string) + content hash for integrity. v1 minimum:
 * identity fields already in FLayoutContractManifestCacheKey.
 */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProfileManifestSnapshot
{
	/** Stable semantic id derived from profile identity fields. */
	FLayoutId SemanticId;

	/** Hash of snapshot content for integrity/determinism. */
	uint32 ContentHash = 0;

	/** Soft path to the source profile asset. */
	FSoftObjectPath ProfilePath;

	/** Snapshot schema version for replay compatibility. */
	int32 SnapshotSchemaVersion = 1;

	/** Builds a v1 snapshot from minimal profile identity. */
	static FLayoutProfileManifestSnapshot Build(
		const FSoftObjectPath& InProfilePath,
		int32 InSchemaVersion = 1);

	/** Validates that this snapshot carries the required identity fields and consistent hash. */
	bool Validate(FString& OutFailureReason) const;
};

/** Immutable pointer-free content-set snapshot for manifest cache identity. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContentSetManifestSnapshot
{
	FLayoutId SemanticId;
	uint32 ContentHash = 0;
	FSoftObjectPath ContentSetPath;
	int32 SnapshotSchemaVersion = 1;

	static FLayoutContentSetManifestSnapshot Build(
		const FSoftObjectPath& InContentSetPath,
		int32 InSchemaVersion = 1);

	bool Validate(FString& OutFailureReason) const;
};

