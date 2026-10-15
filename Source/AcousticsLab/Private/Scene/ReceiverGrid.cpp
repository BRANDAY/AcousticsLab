#include "Scene/ReceiverGrid.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Visualization/HeatmapComponent.h"

AReceiverGrid::AReceiverGrid()
	: GridId(TEXT("G"))
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Heatmap = CreateDefaultSubobject<UHeatmapComponent>(TEXT("Heatmap"));
	Heatmap->SetupAttachment(RootComponent);
}

void AReceiverGrid::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildTiles();
}

void AReceiverGrid::RebuildTiles()
{
	if (Heatmap)
	{
		Heatmap->BuildTiles(NumTilesX, NumTilesY, TileSizeMeters, TileZOffsetMeters);
	}
}

void AReceiverGrid::BeginPlay()
{
	Super::BeginPlay();
	// The PIE / game copy of a native actor does not rerun OnConstruction: make sure the tiles match the grid settings.
	RebuildTiles();
}

FVector3d AReceiverGrid::GetReceiverPositionMeters(int32 TileIndex) const
{
	// From the grid's own settings, never from the component (its layout may lag behind in a freshly loaded world).
	const float Cm = static_cast<float>(Acoustics::CentimetersPerMeter);
	const FVector Local = UHeatmapComponent::TileLocalCenter(TileIndex, NumTilesX, NumTilesY, TileSizeMeters * Cm, ReceiverHeightMeters * Cm);
	return FVector3d(GetActorTransform().TransformPosition(Local)) / Acoustics::CentimetersPerMeter;
}

FString AReceiverGrid::MakeReceiverName(int32 TileIndex) const
{
	const int32 X = NumTilesX > 0 ? TileIndex % NumTilesX : 0;
	const int32 Y = NumTilesX > 0 ? TileIndex / NumTilesX : 0;
	return FString::Printf(TEXT("%s[%d,%d]"), *GridId.ToString(), X, Y);
}

double AReceiverGrid::ResolveReceiverRadius(double RadiusOverride) const
{
	if (RadiusOverride > 0.0)
	{
		return RadiusOverride;
	}
	return ReceiverRadiusMeters > 0.0f ? static_cast<double>(ReceiverRadiusMeters) : Acoustics::DefaultReceiverRadius;
}

void AReceiverGrid::AppendSolverReceivers(TArray<FSolverReceiver>& OutReceivers, double RadiusOverride) const
{
	const double Radius = ResolveReceiverRadius(RadiusOverride);
	OutReceivers.Reserve(OutReceivers.Num() + NumTiles());
	for (int32 Index = 0; Index < NumTiles(); ++Index)
	{
		OutReceivers.Emplace(MakeReceiverName(Index), GetReceiverPositionMeters(Index), Radius);
	}
}

void AReceiverGrid::ApplyResult(const FSimulationResult& Result, TConstArrayView<int32> ReceiverIndices)
{
	if (!Heatmap)
	{
		return;
	}
	if (Heatmap->NumTiles() != NumTiles())
	{
		RebuildTiles();
	}
	TArray<FHeatmapSample> Samples;
	Samples.SetNum(NumTiles());
	int32 NumBound = 0;
	for (int32 Tile = 0; Tile < NumTiles(); ++Tile)
	{
		const int32 ReceiverIndex = ReceiverIndices.IsValidIndex(Tile) ? ReceiverIndices[Tile] : INDEX_NONE;
		if (Result.Receivers.IsValidIndex(ReceiverIndex))
		{
			Samples[Tile] = UHeatmapComponent::MakeSample(Result.Receivers[ReceiverIndex], Heatmap->SourcePowerLevelDb);
			++NumBound;
		}
	}
	if (NumBound == 0)
	{
		Heatmap->ClearSamples();
		return;
	}
	Heatmap->SetSamples(Samples);
	UE_LOG(LogAcoustics, Log, TEXT("Receiver grid %s: %d of %d tiles bound, %s range %.2f..%.2f %s (%d valid)"), *GridId.ToString(), NumBound, NumTiles(),
		*UHeatmapComponent::MetricLabel(Heatmap->Metric).ToString(), Heatmap->ShownMin, Heatmap->ShownMax, *UHeatmapComponent::MetricUnits(Heatmap->Metric), Heatmap->NumValidTiles);
}

void AReceiverGrid::ClearResult()
{
	if (Heatmap)
	{
		Heatmap->ClearSamples();
	}
}

TArray<AReceiverGrid*> AReceiverGrid::FindAll(UWorld* World)
{
	TArray<AReceiverGrid*> Result;
	if (World)
	{
		for (TActorIterator<AReceiverGrid> It(World); It; ++It)
		{
			Result.Add(*It);
		}
		Result.Sort([](const AReceiverGrid& A, const AReceiverGrid& B) { return A.GridId.LexicalLess(B.GridId); });
	}
	return Result;
}
