// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "ChunkWorldTools/ChunkWorldToolkitBase.h"

class FLayoutDirectRootGenerationController;
class IDetailsView;
class SMultiLineEditableTextBox;

/**
 * Docked toolkit panel for the Layout Generator mode.
 * Mirrors the Porism template-tool presentation while keeping the generation
 * attempt summary copyable and the settings section large enough for editing.
 */
class PORISMDIMSWORLDGENERATOREXTENSIONEDITOR_API FLayoutDirectRootGenerationToolkit : public FChunkWorldToolkitBase
{
public:
	/** Unbinds controller refresh notifications before toolkit destruction. */
	virtual ~FLayoutDirectRootGenerationToolkit() override;

	virtual FName GetToolkitFName() const override { return FName("LayoutDirectRootGenerationToolkit"); }
	virtual FText GetBaseToolkitName() const override { return FText::FromString(TEXT("Layout Generator")); }
	virtual FEdMode* GetEditorMode() const override;

	/** Returns the viewport-overlay legend for automated consistency checks. */
	static FText GetOverlayLegendTextForTesting();

protected:
	virtual TSharedRef<SWidget> BuildToolHeader() override;
	virtual TSharedRef<SWidget> BuildToolSettings() override;
	virtual TSharedRef<SWidget> BuildToolControls() override;

private:
	/** Returns the shared controller owned by the extension editor module. */
	FLayoutDirectRootGenerationController& GetController() const;

	/** Returns the copyable generation-attempt summary for the read-only text box. */
	FText GetGenerationSummaryText() const;

	/** Refreshes toolkit text and viewport overlays after async controller state changes. */
	void HandleControllerStateChanged();

	/** Returns the persistent control hint text shown at the bottom of the toolkit. */
	FText GetInstructionText() const;

	/** Builds the copyable phase and color legend for the viewport overlay. */
	static FText BuildOverlayLegendText();

	/** Returns the copyable phase and color legend for the viewport overlay. */
	FText GetOverlayLegendText() const;

	/** Shared details panel bound to the transient generation settings object. */
	TSharedPtr<IDetailsView> DetailsView;

	/** Generation summary widget requiring explicit invalidation after async completion. */
	TSharedPtr<SMultiLineEditableTextBox> GenerationSummaryTextBox;

	/** Subscription keeping async preview state visible without incidental Slate repaints. */
	FDelegateHandle ControllerStateChangedHandle;
};
