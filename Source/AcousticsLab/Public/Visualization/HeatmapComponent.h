// Receiver-grid heatmap: instanced 1 x 1 m tiles colored by a room-acoustic metric (SPEC §3.6, "Тепловая карта").
#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "HeatmapComponent.generated.h"

class UMaterialInterface;
struct FReceiverResult;

/** Metric shown on the tiles (SPEC §3.6: direct-sound SPL, C80, T30, EDT; M6 adds the level of all arrivals for the lens focus). */
UENUM(BlueprintType)
enum class EHeatmapMetric : uint8
{
	DirectSPL UMETA(DisplayName = "Direct SPL"),
	C80,
	T30,
	EDT,
	TotalLevel UMETA(DisplayName = "Total level")
};

/** What one tile remembers of its receiver result, so the metric can be switched without the result. */
USTRUCT(BlueprintType)
struct FHeatmapSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	float DirectSPL = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	float C80 = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	float T30 = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	float EDT = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	bool bDirectValid = false;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	bool bC80Valid = false;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	bool bT30Valid = false;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	bool bEDTValid = false;

	/** Level of the whole energy histogram (direct + every arrival), same reference as DirectSPL. */
	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	float TotalLevel = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Acoustics")
	bool bTotalValid = false;
};

/**
 * UInstancedStaticMeshComponent of square tiles (engine BasicShapes/Plane) laid out in the component's XY plane,
 * NumTilesX x NumTilesY centred on the component origin, tile index = Y * NumTilesX + X. Every instance carries
 * 4 custom data floats (R, G, B, valid) read by M_HeatmapTile (unlit emissive = PerInstanceCustomData3Vector 0);
 * the viridis gradient is evaluated in C++ (Viridis) so the HUD legend uses the same colors.
 */
UCLASS(ClassGroup = (Acoustics), meta = (BlueprintSpawnableComponent))
class ACOUSTICSLAB_API UHeatmapComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UHeatmapComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap")
	EHeatmapMetric Metric = EHeatmapMetric::C80;

	/** Range from the valid tiles of the current metric; otherwise RangeMin..RangeMax. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap")
	bool bAutoRange = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap", meta = (EditCondition = "!bAutoRange"))
	float RangeMin = -10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap", meta = (EditCondition = "!bAutoRange"))
	float RangeMax = 10.0f;

	/** Sound-power level of the source for the direct SPL metric, dB. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap", meta = (ClampMin = "0.0", ClampMax = "160.0"))
	float SourcePowerLevelDb = 100.0f;

	/** Tiles without a valid value (no T30 fit, occluded direct sound). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap")
	FLinearColor InvalidColor = FLinearColor(0.12f, 0.12f, 0.14f);

	/** Color of the tiles before any result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap")
	FLinearColor EmptyColor = FLinearColor(0.05f, 0.05f, 0.06f);

	/** Tile edge as a fraction of the grid step (gaps show the grid). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TileFillFraction = 0.94f;

	/** Material of the tiles; empty = /Game/Acoustics/Materials/M_HeatmapTile, with the engine basic-shape material as the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap")
	TObjectPtr<UMaterialInterface> TileMaterial;

	/** Layout of the instances (saved with the level: the PIE copy of a native actor does not rerun OnConstruction). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap|State")
	int32 NumTilesX = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap|State")
	int32 NumTilesY = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap|State")
	float TileStepCm = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Heatmap|State")
	float TileZOffsetCm = 2.0f;

	/** Range of the colors currently shown (auto or fixed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Heatmap|State")
	float ShownMin = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Heatmap|State")
	float ShownMax = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Heatmap|State")
	int32 NumValidTiles = 0;

	/** Lays out NumX x NumY instances of TileSizeMeters at ZOffsetMeters above the component plane; keeps the samples when the count is unchanged. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Heatmap")
	void BuildTiles(int32 NumX, int32 NumY, float TileSizeMeters, float ZOffsetMeters);

	/** Tile centre in component space, centimeters. */
	FVector GetTileLocalCenter(int32 TileIndex) const;

	/** Centre of tile TileIndex (= Y * NumX + X) of a NumX x NumY grid of StepCm centred on the origin, at ZCm. */
	static FVector TileLocalCenter(int32 TileIndex, int32 NumX, int32 NumY, float StepCm, float ZCm);

	int32 NumTiles() const { return NumTilesX * NumTilesY; }

	/** One sample per tile (tile order); fewer samples clear the rest. */
	void SetSamples(const TArray<FHeatmapSample>& InSamples);
	void ClearSamples();
	bool HasSamples() const { return Samples.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Heatmap")
	void SetMetric(EHeatmapMetric InMetric);

	/** Recolors every tile from the samples, the metric and the range. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Heatmap")
	void Refresh();

	static FHeatmapSample MakeSample(const FReceiverResult& Receiver, double InSourcePowerLevelDb);
	static bool SampleValue(const FHeatmapSample& Sample, EHeatmapMetric InMetric, float& OutValue);
	static FText MetricLabel(EHeatmapMetric InMetric);
	static FString MetricUnits(EHeatmapMetric InMetric);
	static TArray<EHeatmapMetric> AllMetrics();

	/** Viridis colormap for T in [0, 1] (polynomial fit of matplotlib's viridis, returned in linear color space). */
	static FLinearColor Viridis(float T);

	static constexpr int32 CustomFloatsPerTile = 4;

private:
	void ApplyTileColor(int32 TileIndex, const FLinearColor& Color, bool bValid);

	UPROPERTY(Transient)
	TArray<FHeatmapSample> Samples;
};
