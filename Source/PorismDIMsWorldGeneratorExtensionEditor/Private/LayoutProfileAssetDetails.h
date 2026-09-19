// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UObject;
class ULayoutProfileAsset;

/** Details customization that exposes migration helpers and JSON fixture export for layout profiles. */
class FLayoutProfileAssetDetails : public IDetailCustomization
{
public:
	/** Creates the customization instance for PropertyEditor. */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** Adds migration helpers and solver-fixture export controls to the profile details view. */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Generates or refreshes the default perimeter-closure draft for the customized profile. */
	FReply GeneratePerimeterClosureDraft();

	/** Runs the fixture export for the currently customized profile. */
	FReply ExportFixture();

	/** Updates the path text edited in the details panel. */
	void OnExportPathCommitted(const FText& NewText, ETextCommit::Type CommitType);

	/** Returns the latest shared status text for migration/export actions. */
	FText GetStatusText() const;

	/** Returns a short authoring summary for the current closure migration state. */
	FText GetClosureMigrationSummaryText() const;

	/** Builds a deterministic default relative fixture path. */
	FString BuildDefaultExportPath() const;

	TWeakObjectPtr<ULayoutProfileAsset> CustomizedProfile;
	FString ExportPath;
	FString LastStatus;
};
