// A grid of receivers for the heatmap (SPEC §3.4 "сетка для тепловой карты", §3.6).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Results/SimulationResult.h"
#include "ReceiverGrid.generated.h"

class UHeatmapComponent;

/**
 * NumTilesX x NumTilesY tiles of TileSizeMeters in the actor's XY plane, centred on the actor; one receiver sphere per
 * tile at ReceiverHeightMeters above the tile centre. The grid contributes FSolverReceivers to every run of the level's
 * AAcousticSimulationManager (names "<GridId>[x,y]") and shows the chosen metric on its UHeatmapComponent.
 * Neither the tiles nor the spheres are acoustic geometry (no "Acoustic" tag).
 */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API AReceiverGrid : public AActor
{
	GENERATED_BODY()

public:
	AReceiverGrid();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName GridId;

	/** Takes part in simulation runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "1", ClampMax = "200"))
	int32 NumTilesX = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "1", ClampMax = "200"))
	int32 NumTilesY = 7;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "0.25", ClampMax = "20.0"))
	float TileSizeMeters = 1.0f;

	/** Receiver centre above the tile plane, m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float ReceiverHeightMeters = 1.2f;

	/** Receiver sphere radius, m; 0 = the solver setting / default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ReceiverRadiusMeters = 0.5f;

	/** Tiles float this high above the actor plane to avoid z-fighting with the floor, m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TileZOffsetMeters = 0.02f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UHeatmapComponent> Heatmap;

	/**
	 * Explicit tile centres in actor space, meters (M10: the amphitheatre rows). When not empty they replace the regular
	 * NumTilesX x NumTilesY layout; the receiver of a tile sits ReceiverHeightMeters above its centre.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Custom tiles")
	TArray<FVector> CustomTileCentersMeters;

	/** Optional label per custom tile (the amphitheatre row), same order as CustomTileCentersMeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Custom tiles")
	TArray<int32> CustomTileRows;

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	int32 NumTiles() const { return CustomTileCentersMeters.Num() > 0 ? CustomTileCentersMeters.Num() : NumTilesX * NumTilesY; }

	bool HasCustomTiles() const { return CustomTileCentersMeters.Num() > 0; }

	/** Switches to explicit tiles (TileSizeMeters = the tile edge) and rebuilds the heatmap. */
	void SetCustomTiles(const TArray<FVector>& CentersMeters, const TArray<int32>& Rows, float InTileSizeMeters);

	/** Row label of a custom tile (0 when there is none). */
	int32 GetTileRow(int32 TileIndex) const { return CustomTileRows.IsValidIndex(TileIndex) ? CustomTileRows[TileIndex] : 0; }

	/** World position of the receiver of a tile, meters. */
	FVector3d GetReceiverPositionMeters(int32 TileIndex) const;

	FString MakeReceiverName(int32 TileIndex) const;

	/** Appends one FSolverReceiver per tile (tile order). RadiusOverride > 0 replaces the grid's radius (the solver setting), like for point receivers. */
	void AppendSolverReceivers(TArray<FSolverReceiver>& OutReceivers, double RadiusOverride = 0.0) const;

	/** Radius used for a run: the override, else the grid's own, else the SPEC default. */
	double ResolveReceiverRadius(double RadiusOverride = 0.0) const;

	/** Feeds the heatmap: ReceiverIndices[tile] is the index of that tile's receiver in Result.Receivers (INDEX_NONE = no data). */
	void ApplyResult(const FSimulationResult& Result, TConstArrayView<int32> ReceiverIndices);

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void ClearResult();

	/** All grids of the world, sorted by GridId. */
	static TArray<AReceiverGrid*> FindAll(UWorld* World);

protected:
	virtual void BeginPlay() override;

private:
	void RebuildTiles();
};
