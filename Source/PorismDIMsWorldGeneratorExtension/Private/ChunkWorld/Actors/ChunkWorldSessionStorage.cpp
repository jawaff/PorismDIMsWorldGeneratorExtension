// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <filesystem>

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldSessionStorage, Log, All);

namespace
{
	/** Reject reparse points in the complete path, including parents of the managed root. */
	bool IsUnlinkedStoragePath(IPlatformFile& Files, FString Path)
	{
		while (!Path.IsEmpty())
		{
			if (Files.IsSymlink(*Path) != ESymlinkResult::NonSymlink) return false;
			const FString Parent = FPaths::GetPath(Path);
			if (Parent == Path) break;
			Path = Parent;
		}
		return true;
	}

	/** Do not follow links encountered in an owned tree, even during a partial cleanup. */
	bool DeleteUnlinkedStorageTree(IPlatformFile& Files, const FString& Directory, bool bSessionRoot = false)
	{
		if (!IsUnlinkedStoragePath(Files, Directory)) return false;
		const FString Marker = Directory / TEXT(".owner");
		const bool bDeletedChildren = Files.IterateDirectory(*Directory, [&Files, &Marker, bSessionRoot](const TCHAR* Path, bool bDirectory)
		{
			if (Files.IsSymlink(Path) != ESymlinkResult::NonSymlink) return false;
			if (bSessionRoot && Marker == Path) return true;
			return bDirectory ? DeleteUnlinkedStorageTree(Files, Path) : Files.DeleteFile(Path);
		});
		// Keep ownership evidence if a child cannot be deleted, allowing a later safe retry.
		return bDeletedChildren && (!bSessionRoot || Files.DeleteFile(*Marker)) && Files.DeleteDirectory(*Directory);
	}
}

bool AChunkWorldExtended::CreateSessionStorage()
{
	check(SessionStorageDirectory.IsEmpty() && !bSessionWritersMayExist);
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformPhysical();
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ChunkWorldSessions"));
	if (!IsUnlinkedStoragePath(Files, Root) || !Files.CreateDirectoryTree(*Root))
	{
		UE_LOG(LogChunkWorldSessionStorage, Error, TEXT("Cannot create unlinked session storage root: %s"), *Root);
		return false;
	}

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Directory = Root / Token;
	// Unlike CreateDirectoryTree, create_directory returns false for an existing directory.
	// Never adopt somebody else's payload, including an unlikely GUID collision.
	const FTCHARToUTF8 Utf8Path(*Directory);
	const std::filesystem::path NativeDirectory(std::u8string_view(
		reinterpret_cast<const char8_t*>(Utf8Path.Get()), Utf8Path.Length()));
	std::error_code Error;
	if (!std::filesystem::create_directory(NativeDirectory, Error))
	{
		UE_LOG(LogChunkWorldSessionStorage, Error, TEXT("Cannot exclusively create session directory: %s (%s)"),
			*Directory, UTF8_TO_TCHAR(Error.message().c_str()));
		return false;
	}

	SessionStorageDirectory = Directory;
	if (!IsUnlinkedStoragePath(Files, Directory)
		|| !FFileHelper::SaveStringToFile(Token, *(Directory / TEXT(".owner")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		|| !Files.CreateDirectory(*(Directory / TEXT("Save")))
		|| !Files.CreateDirectory(*(Directory / TEXT("Cache"))))
	{
		// No native writers started. Retain failed/uncertain ownership for diagnosis, never adopt it later.
		UE_LOG(LogChunkWorldSessionStorage, Error, TEXT("Cannot prepare owned session storage: %s"), *Directory);
		return false;
	}

	SaveTarget = Directory / TEXT("Save/");
	CacheTarget = Directory / TEXT("Cache/");
	AutoSave = true;
	UseCache = true;
	UE_LOG(LogChunkWorldSessionStorage, Log, TEXT("Started disposable storage Owner=%s Directory=%s"), *GetName(), *Directory);
	return true;
}

void AChunkWorldExtended::ReleaseSessionStorage()
{
	if (SessionStorageDirectory.IsEmpty()) return;
	if (bSessionWritersMayExist)
	{
		UE_LOG(LogChunkWorldSessionStorage, Error, TEXT("Retaining session storage because native writer shutdown is unconfirmed: %s"), *SessionStorageDirectory);
		return;
	}

	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformPhysical();
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ChunkWorldSessions"));
	FString Token;
	const bool bOwned = FPaths::GetPath(SessionStorageDirectory) == Root
		&& IsUnlinkedStoragePath(Files, SessionStorageDirectory)
		&& Files.IsSymlink(*(SessionStorageDirectory / TEXT(".owner"))) == ESymlinkResult::NonSymlink
		&& FFileHelper::LoadFileToString(Token, *(SessionStorageDirectory / TEXT(".owner")))
		&& Token == FPaths::GetCleanFilename(SessionStorageDirectory);
	if (!bOwned || !DeleteUnlinkedStorageTree(Files, SessionStorageDirectory, true))
	{
		UE_LOG(LogChunkWorldSessionStorage, Error, TEXT("Session cleanup failed; payload will not be reused: %s"), *SessionStorageDirectory);
		return;
	}
	UE_LOG(LogChunkWorldSessionStorage, Log, TEXT("Removed disposable storage Owner=%s Directory=%s"), *GetName(), *SessionStorageDirectory);
	SessionStorageDirectory.Reset();
}
