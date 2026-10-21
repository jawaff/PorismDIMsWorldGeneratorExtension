// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Foundation/LandformFoundationOps.h"
#include "Biome/Noise/Foundation/LandformFoundationPayloads.h"
#include "Biome/Noise/Foundation/FoundationTerrainProfileOps.h"
#include "UObject/UnrealType.h"

namespace
{
	bool InRange(const double Value, const double Min, const double Max = 1000000.0)
	{
		return FMath::IsFinite(Value) && Value >= Min && Value <= Max;
	}

	bool ValidPoint(const FVector& Point)
	{
		return InRange(Point.X, -1000000) && InRange(Point.Y, -1000000) && InRange(Point.Z, -1000000);
	}

	bool ValidRadii(const FVector2D& Radii)
	{
		return InRange(Radii.X, 0.001) && InRange(Radii.Y, 0.001);
	}

	float MinRadius(const FVector2D& Radii)
	{
		return static_cast<float>(FMath::Min(Radii.X, Radii.Y));
	}

	FVector2D ExpandedRadii(const FVector2D& Radii, const float NoiseAmplitude)
	{
		return Radii * (1.0 + NoiseAmplitude / MinRadius(Radii));
	}

	// Author geometry in block coordinates, then convert field strength back to noise units.
	// This preserves shapes under anisotropic scale and nonzero authored/world origins.
	struct FGraph
	{
		UFastNoiseEditor* E;
		FNodeLink X, Y, Z;

		FGraph(UFastNoiseEditor* Editor, const FResolvedWorldGenScaleContext& Scale) : E(Editor)
		{
			const FVector Origin = Scale.AuthoredBlockPositionToNoise(FVector::ZeroVector);
			X = E->SubtractFloat(E->DivideFloat(E->PositionOutput(FVector(1, 0, 0), FVector::ZeroVector), Scale.BlocksToNoiseScale.X), Origin.X / Scale.BlocksToNoiseScale.X);
			Y = E->SubtractFloat(E->DivideFloat(E->PositionOutput(FVector(0, 1, 0), FVector::ZeroVector), Scale.BlocksToNoiseScale.Y), Origin.Y / Scale.BlocksToNoiseScale.Y);
			Z = E->SubtractFloat(E->DivideFloat(E->PositionOutput(FVector(0, 0, 1), FVector::ZeroVector), Scale.BlocksToNoiseScale.Z), Origin.Z / Scale.BlocksToNoiseScale.Z);
		}

		FNodeLink Clamp(FNodeLink V, const float Low, const float High) const
		{
			return E->MinFloat(E->MaxFloat(V, Low), High);
		}

		FNodeLink Length(FNodeLink A, FNodeLink B) const
		{
			// FastNoise's approximate float power maps zero to one; keep the logarithm input positive.
			return E->PowFloatFloat(E->MaxFloat(E->Add(E->Multiply(A, A), E->Multiply(B, B)), 1.e-12f), 0.5f);
		}

		FNodeLink Radius(const FVector& Center, const FVector2D& Radii) const
		{
			return Length(E->DivideFloat(E->SubtractFloat(X, Center.X), Radii.X), E->DivideFloat(E->SubtractFloat(Y, Center.Y), Radii.Y));
		}

		FNodeLink Noise(const float Amplitude, const float Frequency, const float VerticalScale, const int32 Seed) const
		{
			if (Amplitude == 0) return E->Constant(0);
			FNodeLink Source = E->SeedOffset(E->OpenSimplex2(), Seed);
			Source = E->DomainAxisScale(Source, FVector(Frequency, Frequency, Frequency * VerticalScale));
			return E->MultiplyFloat(Clamp(Source, -1, 1), Amplitude);
		}

		FNodeLink Volume(FNodeLink Side, FNodeLink Bottom, FNodeLink Top) const
		{
			return E->Min(Side, E->Min(E->Subtract(Top, Z), E->Subtract(Z, Bottom)));
		}
	};

	FVector2D ProfileOrigin(const FFoundationProviderDefinition& Provider)
	{
		if (const auto* P = Provider.ProviderPayload.GetPtr<FArchBridgeFoundationPayload>())
		{
			return FVector2D(P->DeckPoints[0]);
		}
		if (const auto* P = Provider.ProviderPayload.GetPtr<FMountainFoundationPayload>()) return FVector2D(P->BaseCenter);
		if (const auto* P = Provider.ProviderPayload.GetPtr<FColumnFoundationPayload>()) return FVector2D(P->BaseCenter);
		if (const auto* P = Provider.ProviderPayload.GetPtr<FPitFoundationPayload>()) return FVector2D(P->Center);
		return FVector2D::ZeroVector;
	}
}

UScriptStruct* LandformFoundation::PayloadType(const EFoundationProviderType Type)
{
	switch (Type)
	{
	case EFoundationProviderType::ArchBridge: return FArchBridgeFoundationPayload::StaticStruct();
	case EFoundationProviderType::Mountain: return FMountainFoundationPayload::StaticStruct();
	case EFoundationProviderType::Column: return FColumnFoundationPayload::StaticStruct();
	case EFoundationProviderType::PitVoid: return FPitFoundationPayload::StaticStruct();
	default: return nullptr;
	}
}

FString LandformFoundation::Validate(const FFoundationProviderDefinition& Provider)
{
	if (Provider.ProviderPayload.GetScriptStruct() != PayloadType(Provider.ProviderType))
		return TEXT("Landform provider and payload types do not match.");
	if (!Provider.Reservations.IsEmpty())
		return TEXT("Landform providers do not support direct surface-anchor reservations. Use an island or plane child as the reservation host.");
	if (Provider.ProviderType == EFoundationProviderType::PitVoid && Provider.ContributionType != EFoundationContributionType::SubtractiveVoid)
		return TEXT("Pit Void must use Subtractive Void contribution.");

	if (const auto* P = Provider.ProviderPayload.GetPtr<FArchBridgeFoundationPayload>())
	{
		if (P->DeckPoints.Num() < 2 || P->DeckPoints.Num() > 32 || !InRange(P->HalfWidthBlocks, 0.001)
			|| !InRange(P->CrownThicknessBlocks, 0.001) || !InRange(P->ArchRiseBlocks, 0) || !InRange(P->DomainPaddingBlocks, 0.001))
			return TEXT("Bridge requires 2-32 deck points, positive width/thickness/padding and nonnegative arch rise.");
		for (int32 I = 0; I < P->DeckPoints.Num(); ++I)
		{
			if (!ValidPoint(P->DeckPoints[I]) || (I > 0 && FVector2D::Distance(FVector2D(P->DeckPoints[I]), FVector2D(P->DeckPoints[I - 1])) < 0.001))
				return TEXT("Bridge points must be finite and consecutive points at least 0.001 blocks apart in XY.");
		}
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FMountainFoundationPayload>())
	{
		if (!ValidPoint(P->BaseCenter) || !ValidRadii(P->BaseRadiiBlocks) || !InRange(P->HeightBlocks, 0.001)
			|| !InRange(P->SummitRadiusFraction, 0, 0.9) || !InRange(P->SlopePower, 0.25, 4) || !InRange(P->DomainPaddingBlocks, 0.001))
			return TEXT("Mountain requires finite position, positive radii/height/padding, summit fraction 0-0.9 and slope power 0.25-4.");
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FColumnFoundationPayload>())
	{
		if (!ValidPoint(P->BaseCenter) || !ValidRadii(P->BaseRadiiBlocks) || !ValidRadii(P->TopRadiiBlocks)
			|| !InRange(P->HeightBlocks, 0.001) || !InRange(P->DomainPaddingBlocks, 0.001) || !InRange(P->WallNoiseAmplitudeBlocks, 0)
			|| P->WallNoiseAmplitudeBlocks >= FMath::Min(MinRadius(P->BaseRadiiBlocks), MinRadius(P->TopRadiiBlocks)) * 0.5f
			|| !InRange(P->WallNoiseScale, 0.001) || !InRange(P->WallVerticalScale, 0))
			return TEXT("Column requires finite position, positive radii/height/padding/frequency and wall amplitude below half the smallest radius.");
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FPitFoundationPayload>())
	{
		if (!ValidPoint(P->Center) || !ValidRadii(P->OpeningRadiiBlocks) || !InRange(P->DepthBlocks, 0.001)
			|| !InRange(P->BottomRadiusFraction, 0, 0.9) || !InRange(P->OpeningTopPaddingBlocks, 0.001)
			|| !InRange(P->RimNoiseAmplitudeBlocks, 0) || P->RimNoiseAmplitudeBlocks >= MinRadius(P->OpeningRadiiBlocks) * 0.5f
			|| !InRange(P->RimNoiseScale, 0.001))
			return TEXT("Pit requires finite position, positive radii/depth/top clearance, bottom fraction 0-0.9 and rim amplitude below half the smallest radius.");
	}
	if (const UScriptStruct* Profile = Provider.TerrainProfile.GetScriptStruct())
	{
		for (TFieldIterator<FNumericProperty> It(Profile); It; ++It)
		{
			if (It->IsFloatingPoint() && !InRange(It->GetFloatingPointPropertyValue(It->ContainerPtrToValuePtr<void>(Provider.TerrainProfile.GetMemory())), -1000000))
				return TEXT("Landform terrain-profile values must be finite and within one million authored units.");
		}
	}
	return FString();
}

float LandformFoundation::ProfileRadius(const FFoundationProviderDefinition& Provider)
{
	if (const auto* P = Provider.ProviderPayload.GetPtr<FArchBridgeFoundationPayload>()) return P->HalfWidthBlocks;
	if (const auto* P = Provider.ProviderPayload.GetPtr<FMountainFoundationPayload>()) return MinRadius(P->BaseRadiiBlocks);
	if (const auto* P = Provider.ProviderPayload.GetPtr<FColumnFoundationPayload>()) return MinRadius(P->TopRadiiBlocks);
	if (const auto* P = Provider.ProviderPayload.GetPtr<FPitFoundationPayload>()) return MinRadius(P->OpeningRadiiBlocks);
	return 0;
}

FBox LandformFoundation::Bounds(const FFoundationProviderDefinition& Provider)
{
	FBox Result(ForceInit);
	if (!Validate(Provider).IsEmpty()) return Result;
	const float Extra = GetFoundationTerrainProfileMaxPositiveHeightBlocks(Provider.TerrainProfile);
	if (const auto* P = Provider.ProviderPayload.GetPtr<FArchBridgeFoundationPayload>())
	{
		for (const FVector& Point : P->DeckPoints)
		{
			const float Width = P->HalfWidthBlocks + P->DomainPaddingBlocks;
			Result += Point - FVector(Width, Width, P->CrownThicknessBlocks + P->ArchRiseBlocks + P->DomainPaddingBlocks);
			Result += Point + FVector(Width, Width, Extra + P->DomainPaddingBlocks);
		}
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FMountainFoundationPayload>())
	{
		const FVector2D Radii = ExpandedRadii(P->BaseRadiiBlocks, P->DomainPaddingBlocks);
		const FVector Extent(Radii.X, Radii.Y, P->DomainPaddingBlocks);
		Result = FBox(P->BaseCenter - Extent, P->BaseCenter + Extent + FVector(0, 0, P->HeightBlocks + Extra));
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FColumnFoundationPayload>())
	{
		const FVector2D Base = ExpandedRadii(P->BaseRadiiBlocks, P->WallNoiseAmplitudeBlocks);
		const FVector2D Top = ExpandedRadii(P->TopRadiiBlocks, P->WallNoiseAmplitudeBlocks);
		const FVector Extent(FMath::Max(Base.X, Top.X) + P->DomainPaddingBlocks, FMath::Max(Base.Y, Top.Y) + P->DomainPaddingBlocks, P->DomainPaddingBlocks);
		Result = FBox(P->BaseCenter - Extent, P->BaseCenter + Extent + FVector(0, 0, P->HeightBlocks + Extra));
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FPitFoundationPayload>())
	{
		const FVector2D Radii = ExpandedRadii(P->OpeningRadiiBlocks, P->RimNoiseAmplitudeBlocks);
		Result = FBox(P->Center - FVector(Radii.X, Radii.Y, P->DepthBlocks), P->Center + FVector(Radii.X, Radii.Y, P->OpeningTopPaddingBlocks));
	}
	return Result;
}

void LandformFoundation::Transform(FFoundationProviderDefinition& Provider, const FVector& OffsetBlocks, const float Scale)
{
	if (auto* P = Provider.ProviderPayload.GetMutablePtr<FArchBridgeFoundationPayload>())
	{
		for (FVector& Point : P->DeckPoints) Point = Point * Scale + OffsetBlocks;
		P->HalfWidthBlocks *= Scale; P->CrownThicknessBlocks *= Scale; P->ArchRiseBlocks *= Scale; P->DomainPaddingBlocks *= Scale;
	}
	if (auto* P = Provider.ProviderPayload.GetMutablePtr<FMountainFoundationPayload>())
	{
		P->BaseCenter = P->BaseCenter * Scale + OffsetBlocks;
		P->BaseRadiiBlocks *= Scale; P->HeightBlocks *= Scale; P->DomainPaddingBlocks *= Scale;
	}
	if (auto* P = Provider.ProviderPayload.GetMutablePtr<FColumnFoundationPayload>())
	{
		P->BaseCenter = P->BaseCenter * Scale + OffsetBlocks;
		P->BaseRadiiBlocks *= Scale; P->TopRadiiBlocks *= Scale; P->HeightBlocks *= Scale;
		P->WallNoiseAmplitudeBlocks *= Scale; P->DomainPaddingBlocks *= Scale;
	}
	if (auto* P = Provider.ProviderPayload.GetMutablePtr<FPitFoundationPayload>())
	{
		P->Center = P->Center * Scale + OffsetBlocks;
		P->OpeningRadiiBlocks *= Scale; P->DepthBlocks *= Scale; P->RimNoiseAmplitudeBlocks *= Scale; P->OpeningTopPaddingBlocks *= Scale;
	}
}

FNodeLink LandformFoundation::Build(UFastNoiseEditor* E, const FFoundationProviderDefinition& Provider,
	const FResolvedWorldGenScaleContext& Scale, const bool bDomain)
{
	if (!E || !Validate(Provider).IsEmpty() || Scale.BlocksToNoiseScale.ContainsNaN()
		|| FMath::Abs(Scale.BlocksToNoiseScale.X) < SMALL_NUMBER || FMath::Abs(Scale.BlocksToNoiseScale.Y) < SMALL_NUMBER
		|| FMath::Abs(Scale.BlocksToNoiseScale.Z) < SMALL_NUMBER) return FNodeLink();
	const FGraph G(E, Scale);
	const float ZScale = FMath::Abs(Scale.BlocksToNoiseScale.Z);
	const float Extra = GetFoundationTerrainProfileMaxPositiveHeightBlocks(Provider.TerrainProfile);
	FNodeLink Offset = E->Constant(0);
	if (!bDomain || Provider.ProviderType == EFoundationProviderType::PitVoid)
	{
		FFoundationTerrainProfileBuildContext Context{ E, &Provider, &Scale, ProfileOrigin(Provider) };
		FNodeLink Profile = BuildFoundationTerrainProfileSurfaceOffsetNode(Provider.TerrainProfile, Context);
		if (Profile.Node) Offset = E->DivideFloat(Profile, ZScale);
	}
	FNodeLink Field;
	if (const auto* P = Provider.ProviderPayload.GetPtr<FArchBridgeFoundationPayload>())
	{
		for (int32 I = 1; I < P->DeckPoints.Num(); ++I)
		{
			const FVector A = P->DeckPoints[I - 1], B = P->DeckPoints[I];
			const FVector2D Delta(B.X - A.X, B.Y - A.Y);
			FNodeLink DX = E->SubtractFloat(G.X, A.X), DY = E->SubtractFloat(G.Y, A.Y);
			FNodeLink T = G.Clamp(E->DivideFloat(E->Add(E->MultiplyFloat(DX, Delta.X), E->MultiplyFloat(DY, Delta.Y)), Delta.SizeSquared()), 0, 1);
			FNodeLink Distance = G.Length(E->Subtract(DX, E->MultiplyFloat(T, Delta.X)), E->Subtract(DY, E->MultiplyFloat(T, Delta.Y)));
			FNodeLink Deck = E->AddFloat(E->MultiplyFloat(T, B.Z - A.Z), A.Z);
			FNodeLink Bottom, Top;
			const float Width = P->HalfWidthBlocks + (bDomain ? P->DomainPaddingBlocks : 0);
			if (bDomain)
			{
				Top = E->AddFloat(Deck, Extra + P->DomainPaddingBlocks);
				Bottom = E->SubtractFloat(Deck, P->CrownThicknessBlocks + P->ArchRiseBlocks + P->DomainPaddingBlocks);
			}
			else
			{
				FNodeLink Along = E->SubtractFloat(E->MultiplyFloat(T, 2), 1);
				Bottom = E->Subtract(E->SubtractFloat(Deck, P->CrownThicknessBlocks), E->MultiplyFloat(E->Multiply(Along, Along), P->ArchRiseBlocks));
				Top = E->Max(E->Add(Deck, Offset), E->AddFloat(Bottom, FMath::Min(2.0f, P->CrownThicknessBlocks)));
			}
			FNodeLink Span = G.Volume(E->Subtract(E->Constant(Width), Distance), Bottom, Top);
			Field = Field.Node ? E->Max(Field, Span) : Span;
		}
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FMountainFoundationPayload>())
	{
		FNodeLink Radius = G.Radius(P->BaseCenter, P->BaseRadiiBlocks);
		FNodeLink Side = E->MultiplyFloat(E->Subtract(E->Constant(1), Radius), MinRadius(P->BaseRadiiBlocks));
		FNodeLink Top;
		if (bDomain)
		{
			// A box-height envelope avoids evaluating surface noise for ownership.
			Side = E->AddFloat(Side, P->DomainPaddingBlocks);
			Top = E->Constant(P->BaseCenter.Z + P->HeightBlocks + Extra + P->DomainPaddingBlocks);
		}
		else
		{
			FNodeLink Falloff = G.Clamp(E->DivideFloat(E->Subtract(E->Constant(1), Radius), 1 - P->SummitRadiusFraction), 0, 1);
			FNodeLink Height = E->MultiplyFloat(E->PowFloatFloat(E->MaxFloat(Falloff, 1.e-12f), P->SlopePower), P->HeightBlocks);
			Top = E->AddFloat(E->MaxFloat(E->Add(Height, E->Multiply(Offset, Falloff)), 0), P->BaseCenter.Z);
		}
		Field = G.Volume(Side, E->Constant(P->BaseCenter.Z - (bDomain ? P->DomainPaddingBlocks : 0)), Top);
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FColumnFoundationPayload>())
	{
		FNodeLink Side, Top;
		if (bDomain)
		{
			const FBox B = Bounds(Provider);
			FNodeLink DX = E->SubtractFloat(G.X, P->BaseCenter.X), DY = E->SubtractFloat(G.Y, P->BaseCenter.Y);
			Side = E->Min(E->Subtract(E->Constant(B.GetExtent().X), E->Max(DX, E->MultiplyFloat(DX, -1))), E->Subtract(E->Constant(B.GetExtent().Y), E->Max(DY, E->MultiplyFloat(DY, -1))));
			Top = E->Constant(B.Max.Z);
		}
		else
		{
			FNodeLink T = G.Clamp(E->DivideFloat(E->SubtractFloat(G.Z, P->BaseCenter.Z), P->HeightBlocks), 0, 1);
			FNodeLink RX = E->AddFloat(E->MultiplyFloat(T, P->TopRadiiBlocks.X - P->BaseRadiiBlocks.X), P->BaseRadiiBlocks.X);
			FNodeLink RY = E->AddFloat(E->MultiplyFloat(T, P->TopRadiiBlocks.Y - P->BaseRadiiBlocks.Y), P->BaseRadiiBlocks.Y);
			FNodeLink Radius = G.Length(E->Divide(E->SubtractFloat(G.X, P->BaseCenter.X), RX), E->Divide(E->SubtractFloat(G.Y, P->BaseCenter.Y), RY));
			Side = E->Multiply(E->Subtract(E->Constant(1), Radius), E->Min(RX, RY));
			Side = E->Add(Side, G.Noise(P->WallNoiseAmplitudeBlocks, P->WallNoiseScale, P->WallVerticalScale, P->WallSeedOffset));
			Top = E->AddFloat(E->MaxFloat(Offset, FMath::Min(2.0f, P->HeightBlocks) - P->HeightBlocks), P->BaseCenter.Z + P->HeightBlocks);
		}
		Field = G.Volume(Side, E->Constant(P->BaseCenter.Z - (bDomain ? P->DomainPaddingBlocks : 0)), Top);
	}
	if (const auto* P = Provider.ProviderPayload.GetPtr<FPitFoundationPayload>())
	{
		const float R = MinRadius(P->OpeningRadiiBlocks);
		FNodeLink Radius = E->Subtract(G.Radius(P->Center, P->OpeningRadiiBlocks), E->DivideFloat(G.Noise(P->RimNoiseAmplitudeBlocks, P->RimNoiseScale, 0, P->RimSeedOffset), R));
		FNodeLink Slope = G.Clamp(E->DivideFloat(E->SubtractFloat(Radius, P->BottomRadiusFraction), 1 - P->BottomRadiusFraction), 0, 1);
		FNodeLink Floor = E->Add(E->MultiplyFloat(E->Multiply(Slope, Slope), P->DepthBlocks), Offset);
		Floor = E->AddFloat(G.Clamp(Floor, 0, P->DepthBlocks), P->Center.Z - P->DepthBlocks);
		Field = G.Volume(E->MultiplyFloat(E->Subtract(E->Constant(1), Radius), R), Floor, E->Constant(P->Center.Z + P->OpeningTopPaddingBlocks));
	}
	return Field.Node ? E->MultiplyFloat(Field, bDomain ? ZScale : -ZScale) : FNodeLink();
}
