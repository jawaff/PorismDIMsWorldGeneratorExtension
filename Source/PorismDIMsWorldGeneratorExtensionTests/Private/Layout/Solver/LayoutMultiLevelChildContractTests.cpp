// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutMultiLevelChildContractUsesAuthoredInterfaceLevelsTest,
	"PorismExtension.Layout.Solver.MultiLevelChildContract.UsesAuthoredInterfaceLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutMultiLevelChildContractUsesAuthoredInterfaceLevelsTest::RunTest(const FString& Parameters)
{
	FLayoutNegotiatedChildResponsibilityContract Contract;
	Contract.ParentRegionDebugPath = TEXT("Parent");
	Contract.ChildRegionDebugPath = TEXT("Child");
	Contract.HostVerticalAccessResponsibility = ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	Contract.RequiredHostProviderCount = 1;
	Contract.bRequiresExactHostProviderCount = true;
	Contract.CountedChildProviderRegionDebugPaths = {TEXT("Child")};
	Contract.bHasRequiredHostIngressAnchor = true;
	Contract.RequiredHostIngressAnchor.CommitmentId = TEXT("Ingress");
	Contract.RequiredHostIngressAnchor.LocalCell = FIntVector(0, 0, 2);
	Contract.RequiredHostIngressAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	Contract.bHasRequiredHostEgressAnchor = true;
	Contract.RequiredHostEgressAnchor.CommitmentId = TEXT("Egress");
	Contract.RequiredHostEgressAnchor.LocalCell = FIntVector(0, 0, 3);
	Contract.RequiredHostEgressAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	Contract.RequiredChildGenerallyConnectableAnchorPairId = TEXT("Child.VerticalPair");
	Contract.RequiredChildInternalVerticalSpanLevels = {0, 1};

	FLayoutNegotiatedLevelCellSet& LowerReplacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	LowerReplacement.Level = 0;
	LowerReplacement.Cells = {FIntVector(0, 0, 2)};
	FLayoutNegotiatedLevelCellSet& UpperReplacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	UpperReplacement.Level = 1;
	UpperReplacement.Cells = {FIntVector(0, 0, 3)};
	FLayoutNegotiatedLevelInterfaceContract& LowerInterface = Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
	LowerInterface.Level = 0;
	LowerInterface.EndpointAnchors = {Contract.RequiredHostIngressAnchor};
	FLayoutNegotiatedLevelInterfaceContract& UpperInterface = Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
	UpperInterface.Level = 1;
	UpperInterface.EndpointAnchors = {Contract.RequiredHostEgressAnchor};

	FString FailureReason;
	TestTrue(FString::Printf(TEXT("Physical stage shifts retain authored interface levels: %s"), *FailureReason),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(Contract, FailureReason));

	Contract.CommittedParentChildInterfacesByLevel[1].EndpointAnchors.Reset();
	TestFalse(TEXT("Missing authored upper interface rejects host contribution"),
		LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(Contract, FailureReason));
	TestTrue(TEXT("Failure identifies authored-level interface carrier"),
		FailureReason.Contains(TEXT("authored-level interface sets")));
	return true;
}
