// Copyright 2026 Spotted Loaf Studio

#include "Layout/Testing/LayoutTestWorldSupport.h"

#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Materials/Material.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 SolidTestMaterialIndex = SinfullMaterial;
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");
}

void FLayoutTestWorldSupport::InitializeTransientChunkWorld(
	AChunkWorldExtended* World,
	const FIntVector& ChunkBlockSize,
	const FName BiomeRowName)
{
	if (World == nullptr)
	{
		return;
	}

	ShutdownTransientChunkWorld(World);

	World->WorldGenDef = NewObject<UWorldGenDef>(World);
	World->WorldGenDefPredefined = true;
	World->WorldGenDef->BaseBlockSize = 1;
	World->WorldGenDef->ChunkBlockSize = ChunkBlockSize;
	World->WorldGenDef->ChunkCacheFileSize = 4;

	FMaterialDef AirMaterialDef;
	AirMaterialDef.MaterialIndex = DefaultMaterial;
	AirMaterialDef.Material = nullptr;
	World->WorldGenDef->WorldMaterialDefs.Add(AirMaterialDef);

	FMaterialDef SolidMaterialDef;
	SolidMaterialDef.MaterialIndex = SolidTestMaterialIndex;
	SolidMaterialDef.Material = UMaterial::GetDefaultMaterial(MD_Surface);
	World->WorldGenDef->WorldMaterialDefs.Add(SolidMaterialDef);

	FBiomeDualData BiomeRow;
	BiomeRow.BiomeName = BiomeRowName.ToString();
	BiomeRow.GenA = ConstantPositiveFastNoise;
	World->WorldGenDef->WorldBiomes.Add(BiomeRow);

	UBlockTypeSchemaRegistry* SchemaRegistry = NewObject<UBlockTypeSchemaRegistry>(World);
	if (FObjectProperty* RegistryProperty = FindFProperty<FObjectProperty>(AChunkWorldExtended::StaticClass(), TEXT("BlockTypeSchemaRegistry")))
	{
		RegistryProperty->SetObjectPropertyValue_InContainer(World, SchemaRegistry);
	}

	if (UBlockTypeSchemaComponent* SchemaComponent = World->GetBlockTypeSchemaComponent())
	{
		SchemaComponent->SetBlockTypeSchemaRegistry(SchemaRegistry);
	}

	FChunkDataParams ChunkParams;
	ChunkParams.ChunkType = EChunkTypes::ChunkCubic;
	ChunkParams.BlockSizeMulti = 1.0;
	ChunkParams.ChunkSizeMulti = FVector(1.0, 1.0, 1.0);
	ChunkParams.SectorCount = FIntVector(1, 1, 1);
	ChunkParams.RenderChunkBorder = false;
	ChunkParams.Collision = false;
	ChunkParams.Shadows = false;
	ChunkParams.InstancedMeshCollision = false;
	ChunkParams.RenderInstancedMeshs = false;
	ChunkParams.bEnableDecoMeshes = false;
	ChunkParams.AffectLumen = false;
	ChunkParams.AffectLumenISM = false;
	World->WorldGenDef->WorldChunks.Add(ChunkParams);
	World->SaveTarget = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation/Layout") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
	World->StartGen();
}

void FLayoutTestWorldSupport::ShutdownTransientChunkWorld(AChunkWorldExtended* World)
{
	if (World == nullptr)
	{
		return;
	}

	if (World->IsRunning())
	{
		World->StopGen();
	}
}

void FLayoutTestWorldSupport::WriteSurfaceBlock(
	AChunkWorldExtended* World,
	const FIntVector& BlockWorldPos,
	const int32 MaterialIndex)
{
	if (World == nullptr || !World->IsRunning())
	{
		return;
	}

	World->SetBlockValueByBlockWorldPos(BlockWorldPos, MaterialIndex, false);
}

void FLayoutTestWorldSupport::ConfigureSolidTemplate(
	UChunkStructureTemplate* Template,
	AChunkWorldExtended* World,
	const FIntVector& SizeInBlocks,
	const FIntVector& RelativeBlockPos,
	const int32 MaterialIndex)
{
	if (Template == nullptr || World == nullptr)
	{
		return;
	}

	Template->SizeInBlocks = SizeInBlocks;
	Template->Blocks.Reset();

	FBlockTemplateEntry BlockEntry;
	BlockEntry.RelativeBlockPos = RelativeBlockPos;
	BlockEntry.MaterialIndex = MaterialIndex;
	Template->Blocks.Add(BlockEntry);
	Template->CaptureMappingFromWorld(World);
}
