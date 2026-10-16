#include "Visualization/HeatmapComponent.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Results/RoomMetrics.h"
#include "Results/SimulationResult.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AcousticsHeatmap"

namespace
{
	const TCHAR* HeatmapMaterialPath = TEXT("/Game/Acoustics/Materials/M_HeatmapTile.M_HeatmapTile");
	const TCHAR* FallbackMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	float SrgbToLinear(float Value)
	{
		Value = FMath::Clamp(Value, 0.0f, 1.0f);
		return Value <= 0.04045f ? Value / 12.92f : FMath::Pow((Value + 0.055f) / 1.055f, 2.4f);
	}
}

UHeatmapComponent::UHeatmapComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	SetGenerateOverlapEvents(false);
	NumCustomDataFloats = CustomFloatsPerTile;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		SetStaticMesh(PlaneMesh.Object);
	}
}

FLinearColor UHeatmapComponent::Viridis(float T)
{
	// Degree-6 polynomial fit of matplotlib's viridis in sRGB (max error below 1/255), by M. Zucker.
	const float X = FMath::Clamp(T, 0.0f, 1.0f);
	const FVector3f C0(0.2777273272234177f, 0.005407344544966578f, 0.3340998053353061f);
	const FVector3f C1(0.1050930431085774f, 1.404613529898575f, 1.384590162594685f);
	const FVector3f C2(-0.3308618287255563f, 0.214847559468213f, 0.09509516302823659f);
	const FVector3f C3(-4.634230498983486f, -5.799100973351585f, -19.33244095627987f);
	const FVector3f C4(6.228269936347081f, 14.17993336680509f, 56.69055260068105f);
	const FVector3f C5(4.776384997670288f, -13.74514537774601f, -65.35303263337234f);
	const FVector3f C6(-5.435455855934631f, 4.645852612178535f, 26.3124352495832f);
	const FVector3f Srgb = C0 + (C1 + (C2 + (C3 + (C4 + (C5 + C6 * X) * X) * X) * X) * X) * X;
	return FLinearColor(SrgbToLinear(Srgb.X), SrgbToLinear(Srgb.Y), SrgbToLinear(Srgb.Z), 1.0f);
}

TArray<EHeatmapMetric> UHeatmapComponent::AllMetrics()
{
	return { EHeatmapMetric::DirectSPL, EHeatmapMetric::C80, EHeatmapMetric::T30, EHeatmapMetric::EDT };
}

FText UHeatmapComponent::MetricLabel(EHeatmapMetric InMetric)
{
	switch (InMetric)
	{
	case EHeatmapMetric::DirectSPL: return LOCTEXT("DirectSPL", "Direct SPL");
	case EHeatmapMetric::C80: return LOCTEXT("C80", "C80");
	case EHeatmapMetric::T30: return LOCTEXT("T30", "T30");
	case EHeatmapMetric::EDT: return LOCTEXT("EDT", "EDT");
	case EHeatmapMetric::TotalLevel: return LOCTEXT("TotalLevel", "Total level");
	default: return FText::GetEmpty();
	}
}

FString UHeatmapComponent::MetricUnits(EHeatmapMetric InMetric)
{
	switch (InMetric)
	{
	case EHeatmapMetric::DirectSPL: return TEXT("dB");
	case EHeatmapMetric::C80: return TEXT("dB");
	case EHeatmapMetric::T30: return TEXT("s");
	case EHeatmapMetric::EDT: return TEXT("s");
	case EHeatmapMetric::TotalLevel: return TEXT("dB");
	default: return FString();
	}
}

FHeatmapSample UHeatmapComponent::MakeSample(const FReceiverResult& Receiver, double InSourcePowerLevelDb)
{
	FHeatmapSample Sample;
	const FBandMetrics& Band = Receiver.Metrics.Band(0);
	const double Direct = RoomAcoustics::DirectSoundLevelDb(Receiver.DirectEnergy, Receiver.Radius, InSourcePowerLevelDb);
	Sample.bDirectValid = Direct > RoomAcoustics::SilenceDb;
	Sample.DirectSPL = Sample.bDirectValid ? static_cast<float>(Direct) : 0.0f;
	Sample.bC80Valid = Receiver.Metrics.bValid && FMath::Abs(Band.C80) < RoomAcoustics::ClarityLimitDb;
	Sample.C80 = static_cast<float>(Band.C80);
	Sample.bT30Valid = Band.bT30Valid;
	Sample.T30 = static_cast<float>(Band.T30);
	Sample.bEDTValid = Band.bEDTValid;
	Sample.EDT = static_cast<float>(Band.EDT);
	const double Total = RoomAcoustics::DirectSoundLevelDb(Receiver.Histogram.TotalEnergy(0), Receiver.Radius, InSourcePowerLevelDb);
	Sample.bTotalValid = Total > RoomAcoustics::SilenceDb;
	Sample.TotalLevel = Sample.bTotalValid ? static_cast<float>(Total) : 0.0f;
	return Sample;
}

bool UHeatmapComponent::SampleValue(const FHeatmapSample& Sample, EHeatmapMetric InMetric, float& OutValue)
{
	switch (InMetric)
	{
	case EHeatmapMetric::DirectSPL: OutValue = Sample.DirectSPL; return Sample.bDirectValid;
	case EHeatmapMetric::C80: OutValue = Sample.C80; return Sample.bC80Valid;
	case EHeatmapMetric::T30: OutValue = Sample.T30; return Sample.bT30Valid;
	case EHeatmapMetric::EDT: OutValue = Sample.EDT; return Sample.bEDTValid;
	case EHeatmapMetric::TotalLevel: OutValue = Sample.TotalLevel; return Sample.bTotalValid;
	default: OutValue = 0.0f; return false;
	}
}

FVector UHeatmapComponent::TileLocalCenter(int32 TileIndex, int32 NumX, int32 NumY, float StepCm, float ZCm)
{
	if (NumX <= 0 || NumY <= 0)
	{
		return FVector(0.0f, 0.0f, ZCm);
	}
	const int32 X = TileIndex % NumX;
	const int32 Y = TileIndex / NumX;
	return FVector((X + 0.5f - 0.5f * NumX) * StepCm, (Y + 0.5f - 0.5f * NumY) * StepCm, ZCm);
}

FVector UHeatmapComponent::GetTileLocalCenter(int32 TileIndex) const
{
	if (CustomCentersCm.Num() > 0)
	{
		return CustomCentersCm.IsValidIndex(TileIndex) ? CustomCentersCm[TileIndex] + FVector(0.0, 0.0, TileZOffsetCm) : FVector::ZeroVector;
	}
	return TileLocalCenter(TileIndex, NumTilesX, NumTilesY, TileStepCm, TileZOffsetCm);
}

void UHeatmapComponent::BuildTilesAt(const TArray<FVector>& CentersCm, float TileSizeMeters, float ZOffsetMeters)
{
	const bool bSameCount = CentersCm.Num() == NumTiles();
	CustomCentersCm = CentersCm;
	const TArray<FHeatmapSample> Kept = Samples;
	BuildTiles(CentersCm.Num(), 1, TileSizeMeters, ZOffsetMeters);
	if (bSameCount && Kept.Num() > 0)
	{
		SetSamples(Kept);
	}
}

void UHeatmapComponent::BuildTiles(int32 NumX, int32 NumY, float TileSizeMeters, float ZOffsetMeters)
{
	NumX = FMath::Clamp(NumX, 0, CustomCentersCm.Num() > 0 ? 100000 : 1000);
	NumY = FMath::Clamp(NumY, 0, 1000);
	const bool bSameCount = NumX * NumY == NumTilesX * NumTilesY;
	NumTilesX = NumX;
	NumTilesY = NumY;
	TileStepCm = FMath::Max(1.0f, TileSizeMeters * static_cast<float>(Acoustics::CentimetersPerMeter));
	TileZOffsetCm = ZOffsetMeters * static_cast<float>(Acoustics::CentimetersPerMeter);

	if (!TileMaterial)
	{
		TileMaterial = LoadObject<UMaterialInterface>(nullptr, HeatmapMaterialPath);
		if (!TileMaterial)
		{
			UE_LOG(LogAcoustics, Warning, TEXT("Heatmap material %s not found: tiles use the engine basic-shape material (no colors)"), HeatmapMaterialPath);
			TileMaterial = LoadObject<UMaterialInterface>(nullptr, FallbackMaterialPath);
		}
	}
	if (TileMaterial)
	{
		SetMaterial(0, TileMaterial);
	}

	SetNumCustomDataFloats(CustomFloatsPerTile);
	ClearInstances();
	TArray<FTransform> Transforms;
	Transforms.Reserve(NumTiles());
	const float Scale = TileStepCm / 100.0f * FMath::Clamp(TileFillFraction, 0.1f, 1.0f); // the engine plane is 100 x 100 cm
	for (int32 Index = 0; Index < NumTiles(); ++Index)
	{
		Transforms.Emplace(FQuat::Identity, GetTileLocalCenter(Index), FVector(Scale, Scale, 1.0f));
	}
	if (Transforms.Num() > 0)
	{
		AddInstances(Transforms, false, false, false);
	}
	if (!bSameCount)
	{
		Samples.Reset();
	}
	Refresh();
}

void UHeatmapComponent::SetSamples(const TArray<FHeatmapSample>& InSamples)
{
	Samples = InSamples;
	Samples.SetNum(FMath::Min(Samples.Num(), NumTiles()));
	Refresh();
}

void UHeatmapComponent::ClearSamples()
{
	Samples.Reset();
	Refresh();
}

void UHeatmapComponent::SetMetric(EHeatmapMetric InMetric)
{
	if (Metric != InMetric)
	{
		Metric = InMetric;
		Refresh();
	}
}

void UHeatmapComponent::ApplyTileColor(int32 TileIndex, const FLinearColor& Color, bool bValid)
{
	const float Data[CustomFloatsPerTile] = { Color.R, Color.G, Color.B, bValid ? 1.0f : 0.0f };
	SetCustomData(TileIndex, MakeArrayView(Data, CustomFloatsPerTile), false);
}

void UHeatmapComponent::Refresh()
{
	const int32 Count = FMath::Min(GetInstanceCount(), NumTiles());
	NumValidTiles = 0;
	ShownMin = ShownMax = 0.0f;
	if (Count == 0)
	{
		return;
	}

	TArray<float> Values;
	TArray<bool> Valid;
	Values.Init(0.0f, Count);
	Valid.Init(false, Count);
	float MinValue = TNumericLimits<float>::Max();
	float MaxValue = TNumericLimits<float>::Lowest();
	for (int32 Index = 0; Index < Count && Index < Samples.Num(); ++Index)
	{
		if (SampleValue(Samples[Index], Metric, Values[Index]))
		{
			Valid[Index] = true;
			++NumValidTiles;
			MinValue = FMath::Min(MinValue, Values[Index]);
			MaxValue = FMath::Max(MaxValue, Values[Index]);
		}
	}

	if (bAutoRange && NumValidTiles > 0)
	{
		ShownMin = MinValue;
		ShownMax = MaxValue;
	}
	else
	{
		ShownMin = FMath::Min(RangeMin, RangeMax);
		ShownMax = FMath::Max(RangeMin, RangeMax);
	}
	const float Span = ShownMax - ShownMin;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Samples.Num() == 0)
		{
			ApplyTileColor(Index, EmptyColor, false);
		}
		else if (!Valid[Index])
		{
			ApplyTileColor(Index, InvalidColor, false);
		}
		else
		{
			const float T = Span > KINDA_SMALL_NUMBER ? (Values[Index] - ShownMin) / Span : 0.5f;
			ApplyTileColor(Index, Viridis(T), true);
		}
	}
	MarkRenderStateDirty();
}

#undef LOCTEXT_NAMESPACE
