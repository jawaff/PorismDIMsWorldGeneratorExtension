// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Contracts/LayoutManifestSnapshots.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutManifestSnapshotsBuildAndValidateProfile,
	"PorismExtension.Layout.Contracts.ManifestSnapshots.BuildAndValidateProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutManifestSnapshotsBuildAndValidateProfile::RunTest(const FString& Parameters)
{
	const FSoftObjectPath ProfilePath(TEXT("/Game/Profiles/MyProfile"));
	const FLayoutProfileManifestSnapshot Snapshot = FLayoutProfileManifestSnapshot::Build(ProfilePath);

	TestEqual(TEXT("Semantic id uses profile path"), Snapshot.SemanticId, FLayoutId(TEXT("Profile./Game/Profiles/MyProfile")));
	TestEqual(TEXT("Profile path preserved"), Snapshot.ProfilePath, ProfilePath);
	TestNotEqual(TEXT("Content hash non-zero"), Snapshot.ContentHash, 0u);

	FString FailureReason;
	TestTrue(TEXT("Validation passes"), Snapshot.Validate(FailureReason));

	// Tamper with hash.
	FLayoutProfileManifestSnapshot Tampered = Snapshot;
	Tampered.ContentHash = 0;
	TestFalse(TEXT("Zero hash fails validation"), Tampered.Validate(FailureReason));

	// Missing path.
	FLayoutProfileManifestSnapshot NoPath = Snapshot;
	NoPath.ProfilePath.Reset();
	TestFalse(TEXT("Null path fails"), NoPath.Validate(FailureReason));

	// Missing id.
	FLayoutProfileManifestSnapshot NoId = Snapshot;
	NoId.SemanticId = NAME_None;
	TestFalse(TEXT("None id fails"), NoId.Validate(FailureReason));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutManifestSnapshotsDeterministicId,
	"PorismExtension.Layout.Contracts.ManifestSnapshots.DeterministicId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutManifestSnapshotsDeterministicId::RunTest(const FString& Parameters)
{
	const FSoftObjectPath PathA(TEXT("/Game/Profiles/ProfileA"));
	const FSoftObjectPath PathB(TEXT("/Game/Profiles/ProfileB"));

	const FLayoutProfileManifestSnapshot SnapA1 = FLayoutProfileManifestSnapshot::Build(PathA);
	const FLayoutProfileManifestSnapshot SnapA2 = FLayoutProfileManifestSnapshot::Build(PathA);
	const FLayoutProfileManifestSnapshot SnapB = FLayoutProfileManifestSnapshot::Build(PathB);

	TestEqual(TEXT("Same path produces same id"), SnapA1.SemanticId, SnapA2.SemanticId);
	TestEqual(TEXT("Same path produces same hash"), SnapA1.ContentHash, SnapA2.ContentHash);
	TestNotEqual(TEXT("Different paths produce different ids"), SnapA1.SemanticId, SnapB.SemanticId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutManifestSnapshotsTamperContentSet,
	"PorismExtension.Layout.Contracts.ManifestSnapshots.TamperContentSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutManifestSnapshotsTamperContentSet::RunTest(const FString& Parameters)
{
	const FSoftObjectPath Path(TEXT("/Game/ContentSets/MyContentSet"));
	const FLayoutContentSetManifestSnapshot Snapshot = FLayoutContentSetManifestSnapshot::Build(Path);

	TestEqual(TEXT("ContentSet semantic id"), Snapshot.SemanticId, FLayoutId(TEXT("ContentSet./Game/ContentSets/MyContentSet")));
	TestNotEqual(TEXT("Content hash non-zero"), Snapshot.ContentHash, 0u);

	FString FailureReason;
	TestTrue(TEXT("Validation passes"), Snapshot.Validate(FailureReason));

	FLayoutContentSetManifestSnapshot Tampered = Snapshot;
	Tampered.ContentHash = 0;
	TestFalse(TEXT("Zero hash fails"), Tampered.Validate(FailureReason));

	FLayoutContentSetManifestSnapshot NoPath = Snapshot;
	NoPath.ContentSetPath.Reset();
	TestFalse(TEXT("Null path fails"), NoPath.Validate(FailureReason));

	FLayoutContentSetManifestSnapshot NoId = Snapshot;
	NoId.SemanticId = NAME_None;
	TestFalse(TEXT("None id fails"), NoId.Validate(FailureReason));

	// Hash mismatch from tampered path.
	FLayoutContentSetManifestSnapshot BadHash = Snapshot;
	BadHash.ContentSetPath = FSoftObjectPath(TEXT("/Game/Other/OtherSet"));
	TestFalse(TEXT("Hash mismatch fails"), BadHash.Validate(FailureReason));

	return true;
}

