// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutOwnedIdentityTest,
	"PorismExtension.Layout.Runtime.Identity.OwnedText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOwnedIdentityTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Texts = {TEXT("None"), TEXT(""), TEXT("Root"), TEXT("root"), TEXT("Root_2"), TEXT("Root_10"), TEXT("Root_01"), TEXT("Root_0"), TEXT("Root_2147483646"), TEXT("Root_2147483647"), TEXT("Other")};
	for (const FString& A : Texts)
	{
		for (const FString& B : Texts)
		{
			const FLayoutId Left(A), Right(B);
			const FName OldLeft(*A), OldRight(*B);
			TestEqual(TEXT("Identity equality is unchanged"), Left == Right, OldLeft == OldRight);
			TestEqual(TEXT("Identity sorting is unchanged"), Left.LexicalLess(Right), OldLeft.LexicalLess(OldRight));
			if (Left == Right) TestEqual(TEXT("Equivalent keys hash equally"), GetTypeHash(Left), GetTypeHash(Right));
		}
	}
	TMap<FLayoutId, int32> Owners;
	Owners.Add(FLayoutId(TEXT("Root_2")), 7);
	TestEqual(TEXT("Case-insensitive lookup retains ownership"), Owners.FindRef(FLayoutId(TEXT("root_2"))), 7);
	TestTrue(TEXT("Default identity is None"), FLayoutId().IsNone());
	TestEqual(TEXT("Empty identity keeps serialized/hash text"), FLayoutId().ToString(), FString(TEXT("None")));

	const int32 NamesBefore = FName::GetNumAnsiNames();
	bool bRoundTrips = true;
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		const FString Text = FString::Printf(TEXT("OwnedIdentity.%s.%d"), *FGuid::NewGuid().ToString(), Index);
		const FLayoutId Id(Text);
		FLayoutModePlan Plan;
		Plan.SiteCenterBlockWorldPos = FIntVector(Index, 2 * Index, 3);
		const auto ModeId = FLayoutContractPipeline::BuildModePlanId(Plan);
		FLayoutFrozenTerrainContract Contract;
		Contract.SiteCenterBlockWorldPos = Plan.SiteCenterBlockWorldPos;
		const auto ArtifactId = FLayoutContractPipeline::BuildTerrainWriteArtifactId(Contract);
		bRoundTrips &= !ModeId.IsNone() && !ArtifactId.IsNone();
		bRoundTrips &= Id.ToString() == Text && Id == FLayoutId(Text);
	}
	const int32 NamesAfter = FName::GetNumAnsiNames();
	TestTrue(TEXT("Generated text survives ownership copies"), bRoundTrips);
	TestEqual(TEXT("Unique generated IDs do not allocate name-pool entries"), NamesAfter, NamesBefore);
	return true;
}
