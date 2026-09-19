// Copyright 2026 Spotted Loaf Studio

#include "Layout/Testing/LayoutProfileJsonExportLibrary.h"

#include "Layout/Testing/LayoutProfileJsonFixture.h"
#include "Misc/Paths.h"

bool ULayoutProfileJsonExportLibrary::ExportLayoutProfileJsonFixtureFromContentSet(
	ULayoutProfileAsset* Profile,
	ULayoutRegionContentSetAsset* ContentSet,
	const FString& FilePath,
	FString& OutError)
{
	OutError.Reset();
	if (FilePath.IsEmpty())
	{
		OutError = TEXT("Layout JSON fixture export requires a file path.");
		return false;
	}

	const FString AbsoluteFilePath = FPaths::IsRelative(FilePath)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FilePath)
		: FPaths::ConvertRelativePathToFull(FilePath);
	return FLayoutProfileJsonFixture::ExportToFile(Profile, ContentSet, AbsoluteFilePath, OutError);
}
