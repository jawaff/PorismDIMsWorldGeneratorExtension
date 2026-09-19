// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"

#include "PorismDIMsWorldGeneratorExtension.h"

namespace
{
	const TCHAR* LexToStringNoiseSlot(const EBiomeNoiseSlot NoiseSlot)
	{
		switch (NoiseSlot)
		{
		case EBiomeNoiseSlot::DomainNoise:
			return TEXT("DomainNoise");
		case EBiomeNoiseSlot::GenA:
			return TEXT("GenA");
		default:
			return TEXT("Unknown");
		}
	}

}

FNodeLink UBiomeFastNoiseEditor::GetNoiseRef_Implementation(UObject* Creator)
{
	const int32 NodeCountBefore = Nodes != nullptr ? static_cast<int32>(Nodes->size()) : -1;

	FNodeLink Result = Strategy != nullptr
		? Strategy->BuildBiomeNoise(this, Creator, NoiseSlot, BiomeTag)
		: Constant(0.0f);
	const int32 NodeCountAfter = Nodes != nullptr ? static_cast<int32>(Nodes->size()) : -1;
	if (Result.Node == nullptr || !Result.Node->BaseNode)
	{
		UE_LOG(
			LogPorismDIMsWorldGeneratorExtension,
			Warning,
			TEXT("Biome FNE returned invalid noise: Wrapper='%s' Slot=%s BiomeTag='%s' Strategy='%s' ResultNode=%s ResultBaseNode=%s NodesPtr=%p NodeCountBefore=%d NodeCountAfter=%d."),
			*GetPathName(),
			LexToStringNoiseSlot(NoiseSlot),
			*BiomeTag.ToString(),
			Strategy != nullptr ? *Strategy->GetPathName() : TEXT("None"),
			Result.Node != nullptr ? TEXT("Valid") : TEXT("Invalid"),
			Result.Node != nullptr && Result.Node->BaseNode ? TEXT("Valid") : TEXT("Invalid"),
			Nodes,
			NodeCountBefore,
			NodeCountAfter);
	}
	return Result;
}
