// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutManifestSnapshots.h"

namespace
{
	/** Deterministic hash of string + int32 for snapshot content integrity. */
	uint32 HashSnapshotContent(const FString& IdentityString, int32 SchemaVersion)
	{
		uint32 Hash = HashCombineFast(GetTypeHash(IdentityString), static_cast<uint32>(SchemaVersion));
		return Hash;
	}
}

FLayoutProfileManifestSnapshot FLayoutProfileManifestSnapshot::Build(
	const FSoftObjectPath& InProfilePath,
	const int32 InSchemaVersion)
{
	FLayoutProfileManifestSnapshot Snapshot;
	Snapshot.ProfilePath = InProfilePath;
	Snapshot.SnapshotSchemaVersion = InSchemaVersion;

	const FString IdentityString = FString::Printf(
		TEXT("Profile.%s"),
		*InProfilePath.ToString());
	Snapshot.SemanticId = FLayoutId(*IdentityString);
	Snapshot.ContentHash = HashSnapshotContent(IdentityString, InSchemaVersion);
	return Snapshot;
}

bool FLayoutProfileManifestSnapshot::Validate(FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (SemanticId.IsNone())
	{
		OutFailureReason = TEXT("Profile manifest snapshot requires a semantic id.");
		return false;
	}
	if (ContentHash == 0)
	{
		OutFailureReason = TEXT("Profile manifest snapshot requires a non-zero content hash.");
		return false;
	}
	if (ProfilePath.IsNull())
	{
		OutFailureReason = TEXT("Profile manifest snapshot requires a non-null profile path.");
		return false;
	}
	const uint32 RebuiltHash = HashSnapshotContent(
		FString::Printf(TEXT("Profile.%s"), *ProfilePath.ToString()),
		SnapshotSchemaVersion);
	if (RebuiltHash != ContentHash)
	{
		OutFailureReason = TEXT("Profile manifest snapshot hash mismatch.");
		return false;
	}
	return true;
}

FLayoutContentSetManifestSnapshot FLayoutContentSetManifestSnapshot::Build(
	const FSoftObjectPath& InContentSetPath,
	const int32 InSchemaVersion)
{
	FLayoutContentSetManifestSnapshot Snapshot;
	Snapshot.ContentSetPath = InContentSetPath;
	Snapshot.SnapshotSchemaVersion = InSchemaVersion;

	const FString IdentityString = FString::Printf(
		TEXT("ContentSet.%s"),
		*InContentSetPath.ToString());
	Snapshot.SemanticId = FLayoutId(*IdentityString);
	Snapshot.ContentHash = HashSnapshotContent(IdentityString, InSchemaVersion);
	return Snapshot;
}

bool FLayoutContentSetManifestSnapshot::Validate(FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (SemanticId.IsNone())
	{
		OutFailureReason = TEXT("Content-set manifest snapshot requires a semantic id.");
		return false;
	}
	if (ContentHash == 0)
	{
		OutFailureReason = TEXT("Content-set manifest snapshot requires a non-zero content hash.");
		return false;
	}
	if (ContentSetPath.IsNull())
	{
		OutFailureReason = TEXT("Content-set manifest snapshot requires a non-null content-set path.");
		return false;
	}
	const uint32 RebuiltHash = HashSnapshotContent(
		FString::Printf(TEXT("ContentSet.%s"), *ContentSetPath.ToString()),
		SnapshotSchemaVersion);
	if (RebuiltHash != ContentHash)
	{
		OutFailureReason = TEXT("Content-set manifest snapshot hash mismatch.");
		return false;
	}
	return true;
}
