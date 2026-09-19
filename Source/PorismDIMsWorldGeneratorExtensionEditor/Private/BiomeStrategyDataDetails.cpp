// Copyright 2026 Spotted Loaf Studio

#include "BiomeStrategyDataDetails.h"

#include "Biome/Noise/Strategy/BiomeStrategyData.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BiomeStrategyDataDetails"

namespace
{
	FString MakeChildProviderPath(const FString& ParentPath, const int32 ChildIndex, const FName DebugName)
	{
		return FString::Printf(
			TEXT("%s.ChildFoundations[%d:%s]"),
			*ParentPath,
			ChildIndex,
			*DebugName.ToString());
	}

	FString MakePrototypeProviderPath(const FString& ParentPath)
	{
		return FString::Printf(TEXT("%s.PrototypeProvider"), *ParentPath);
	}

	const TCHAR* LexToDisplayString(const EFoundationContributionType ContributionType)
	{
		switch (ContributionType)
		{
		case EFoundationContributionType::AdditiveBiome:
			return TEXT("Additive");
		case EFoundationContributionType::SubtractiveVoid:
			return TEXT("Subtractive Void");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* LexToDisplayString(const EFoundationProviderType ProviderType)
	{
		switch (ProviderType)
		{
		case EFoundationProviderType::Island:
			return TEXT("Island");
		case EFoundationProviderType::InfinitePlane:
			return TEXT("Infinite Plane");
		case EFoundationProviderType::MultiInstance:
			return TEXT("Multi Instance");
		case EFoundationProviderType::VCutVoid:
			return TEXT("V Cut Void");
		default:
			return TEXT("Unknown");
		}
	}

	FText BuildProviderLabel(const FFoundationProviderDefinition& Provider)
	{
		const FString BiomeTagText = Provider.BiomeTag.IsValid()
			? Provider.BiomeTag.ToString()
			: FString(TEXT("No Biome"));
		return FText::FromString(FString::Printf(
			TEXT("Provider: %s  [%s, %s, %s]"),
			*Provider.DebugName.ToString(),
			LexToDisplayString(Provider.ProviderType),
			LexToDisplayString(Provider.ContributionType),
			*BiomeTagText));
	}

	FText BuildReservationLabel(const FReservationDefinition& Reservation, const int32 Index)
	{
		const FString BiomeTagText = Reservation.BiomeTag.IsValid()
			? Reservation.BiomeTag.ToString()
			: FString(TEXT("No Biome"));
		return FText::FromString(FString::Printf(
			TEXT("Reservation %d: %s  [%s]"),
			Index,
			*Reservation.DebugName.ToString(),
			*BiomeTagText));
	}

	TSharedRef<SButton> MakeSmallButton(const FText& Text, const FText& ToolTip, TFunction<FReply()> Action)
	{
		return SNew(SButton)
			.Text(Text)
			.ToolTipText(ToolTip)
			.ContentPadding(FMargin(5.0f, 1.0f))
			.OnClicked_Lambda(MoveTemp(Action));
	}
}

TSharedRef<IDetailCustomization> FBiomeStrategyDataDetails::MakeInstance()
{
	return MakeShared<FBiomeStrategyDataDetails>();
}

void FBiomeStrategyDataDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	CachedDetailBuilder = &DetailBuilder;

	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

	UBiomeStrategyData* Strategy = nullptr;
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		if (UBiomeStrategyData* Candidate = Cast<UBiomeStrategyData>(Object.Get()))
		{
			Strategy = Candidate;
			break;
		}
	}

	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UBiomeStrategyData, AuthoringTargetProviderPath));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UBiomeStrategyData, AuthoringTargetReservationIndex));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UBiomeStrategyData, LastAuthoringToolResult));

	if (Strategy == nullptr)
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Biome Strategy|Hierarchy Tools"),
		LOCTEXT("HierarchyToolsCategory", "Hierarchy Tools"));
	BuildHierarchyPanel(Category, *Strategy);
}

void FBiomeStrategyDataDetails::BuildHierarchyPanel(IDetailCategoryBuilder& Category, UBiomeStrategyData& Strategy)
{
	const TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	AddProviderRows(Container, Strategy, Strategy.RootFoundationProvider, TEXT("RootFoundationProvider"), 0, true);

	if (!Strategy.LastAuthoringToolResult.IsEmpty())
	{
		Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Strategy.LastAuthoringToolResult))
				.AutoWrapText(true)
			];
	}

	Category.AddCustomRow(LOCTEXT("ProviderHierarchySearch", "Provider Reservation Hierarchy"))
		.WholeRowContent()
		[
			Container
		];
}

void FBiomeStrategyDataDetails::AddProviderRows(
	const TSharedRef<SVerticalBox>& Container,
	UBiomeStrategyData& Strategy,
	FFoundationProviderDefinition& Provider,
	const FString& Path,
	const int32 Depth,
	const bool bIsRootOrPrototype)
{
	constexpr float IndentWidth = 18.0f;

	Container->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(Depth * IndentWidth)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(BuildProviderLabel(Provider))
				.ToolTipText(FText::FromString(Path))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				.Visibility(bIsRootOrPrototype ? EVisibility::Collapsed : EVisibility::Visible)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.0f, 0.0f)
				[
					MakeSmallButton(
						LOCTEXT("MoveProviderEarlier", "^"),
						LOCTEXT("MoveProviderEarlierTooltip", "Move this provider earlier in its sibling list."),
						[&Strategy, Path, this]()
						{
							Strategy.AuthoringTargetProviderPath = Path;
							Strategy.MoveTargetProviderEarlier();
							RefreshDetails();
							return FReply::Handled();
						})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.0f, 0.0f)
				[
					MakeSmallButton(
						LOCTEXT("MoveProviderLater", "v"),
						LOCTEXT("MoveProviderLaterTooltip", "Move this provider later in its sibling list."),
						[&Strategy, Path, this]()
						{
							Strategy.AuthoringTargetProviderPath = Path;
							Strategy.MoveTargetProviderLater();
							RefreshDetails();
							return FReply::Handled();
						})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.0f, 0.0f)
				[
					MakeSmallButton(
						LOCTEXT("PromoteProvider", "<"),
						LOCTEXT("PromoteProviderTooltip", "Promote this provider one level up in the hierarchy."),
						[&Strategy, Path, this]()
						{
							Strategy.AuthoringTargetProviderPath = Path;
							Strategy.PromoteTargetProviderOneLevel();
							RefreshDetails();
							return FReply::Handled();
						})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.0f, 0.0f)
				[
					MakeSmallButton(
						LOCTEXT("DemoteProviderPrevious", "Prev"),
						LOCTEXT("DemoteProviderPreviousTooltip", "Demote this provider into the previous sibling's child list."),
						[&Strategy, Path, this]()
						{
							Strategy.AuthoringTargetProviderPath = Path;
							Strategy.DemoteTargetProviderIntoPreviousSibling();
							RefreshDetails();
							return FReply::Handled();
						})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.0f, 0.0f)
				[
					MakeSmallButton(
						LOCTEXT("DemoteProviderNext", "Next"),
						LOCTEXT("DemoteProviderNextTooltip", "Demote this provider into the next sibling's child list."),
						[&Strategy, Path, this]()
						{
							Strategy.AuthoringTargetProviderPath = Path;
							Strategy.DemoteTargetProviderIntoNextSibling();
							RefreshDetails();
							return FReply::Handled();
						})
				]
			]
		];

	AddReservationRows(Container, Strategy, Provider, Path, Depth + 1);

	if (FMultiInstanceFoundationPayload* MultiPayload = Provider.ProviderPayload.GetMutablePtr<FMultiInstanceFoundationPayload>())
	{
		if (FFoundationProviderPrototypeDefinition* PrototypeProvider = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>())
		{
			const FString PrototypePath = MakePrototypeProviderPath(Path);
			for (int32 ChildIndex = 0; ChildIndex < PrototypeProvider->ChildFoundations.Num(); ++ChildIndex)
			{
				if (FFoundationProviderDefinition* ChildProvider = PrototypeProvider->ChildFoundations[ChildIndex].GetMutablePtr<FFoundationProviderDefinition>())
				{
					AddProviderRows(
						Container,
						Strategy,
						*ChildProvider,
						MakeChildProviderPath(PrototypePath, ChildIndex, ChildProvider->DebugName),
						Depth + 1,
						true);
				}
			}
		}
	}

	for (int32 ChildIndex = 0; ChildIndex < Provider.ChildFoundations.Num(); ++ChildIndex)
	{
		if (FFoundationProviderDefinition* ChildProvider = Provider.ChildFoundations[ChildIndex].GetMutablePtr<FFoundationProviderDefinition>())
		{
			AddProviderRows(
				Container,
				Strategy,
				*ChildProvider,
				MakeChildProviderPath(Path, ChildIndex, ChildProvider->DebugName),
				Depth + 1,
				false);
		}
	}
}

void FBiomeStrategyDataDetails::AddReservationRows(
	const TSharedRef<SVerticalBox>& Container,
	UBiomeStrategyData& Strategy,
	FFoundationProviderDefinition& Provider,
	const FString& ProviderPath,
	const int32 Depth)
{
	constexpr float IndentWidth = 18.0f;

	for (int32 ReservationIndex = 0; ReservationIndex < Provider.Reservations.Num(); ++ReservationIndex)
	{
		FReservationDefinition& Reservation = Provider.Reservations[ReservationIndex];

		Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(Depth * IndentWidth)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(BuildReservationLabel(Reservation, ReservationIndex))
					.ToolTipText(FText::FromString(ProviderPath))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.0f, 0.0f)
					[
						MakeSmallButton(
							LOCTEXT("MoveReservationEarlier", "^"),
							LOCTEXT("MoveReservationEarlierTooltip", "Move this reservation earlier in its provider's reservation list."),
							[&Strategy, ProviderPath, ReservationIndex, this]()
							{
								Strategy.AuthoringTargetProviderPath = ProviderPath;
								Strategy.AuthoringTargetReservationIndex = ReservationIndex;
								Strategy.MoveTargetReservationEarlier();
								RefreshDetails();
								return FReply::Handled();
							})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.0f, 0.0f)
					[
						MakeSmallButton(
							LOCTEXT("MoveReservationLater", "v"),
							LOCTEXT("MoveReservationLaterTooltip", "Move this reservation later in its provider's reservation list."),
							[&Strategy, ProviderPath, ReservationIndex, this]()
							{
								Strategy.AuthoringTargetProviderPath = ProviderPath;
								Strategy.AuthoringTargetReservationIndex = ReservationIndex;
								Strategy.MoveTargetReservationLater();
								RefreshDetails();
								return FReply::Handled();
							})
					]
				]
			];
	}
}

void FBiomeStrategyDataDetails::RefreshDetails() const
{
	if (CachedDetailBuilder != nullptr)
	{
		CachedDetailBuilder->ForceRefreshDetails();
	}
}

#undef LOCTEXT_NAMESPACE
