// Shared source / receiver positions of every amphitheatre level (SPEC §3.4, DA_AmphitheatrePositions).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AmphitheatrePositionsAsset.generated.h"

class UWorld;

/** One receiver seat: row of the cavea (1 = front row) and azimuth on the row ellipse (90 = minor axis, 0 = major axis). */
USTRUCT(BlueprintType)
struct FAmphitheatreSeat
{
	GENERATED_BODY()

	/** Matches AAcousticReceiver::ReceiverId (R1..R4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName ReceiverId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "1"))
	int32 Row = 1;

	/** Parametric angle on the row ellipse, degrees: 0 = +X end of the major axis, 90 = +Y end of the minor axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float AzimuthDeg = 90.0f;

	/** Shown in the HUD / docs ("1st row", "side sector"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FString Label;
};

/**
 * Positions shared by Level_Amphitheatre, Level_Amphitheatre_SteamAudio and Level_Amphitheatre_AudioTracing (SPEC §3.4):
 * the source at the arena centre 1.6 m high, receivers R1 (1st row), R2 (10th row), R3 (20th row), R4 (side sector), and
 * the heatmap spacing. Seats are computed from the arena and row parameters (meters), so the M10 Geometry Script builder
 * can build the cavea from the same numbers. Positions are relative to CenterMeters (world, meters, z = arena floor).
 */
UCLASS(BlueprintType)
class ACOUSTICSLAB_API UAmphitheatrePositionsAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UAmphitheatrePositionsAsset();

	/** World position of the arena centre on the arena floor, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Arena")
	FVector CenterMeters = FVector::ZeroVector;

	/** Arena semi-axes along X and Y, meters (SPEC: arena ~65 x 40 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Arena", meta = (ClampMin = "1.0"))
	FVector2D ArenaSemiAxesMeters = FVector2D(32.5, 20.0);

	/** Rows of the cavea (SPEC: 20). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Cavea", meta = (ClampMin = "1"))
	int32 NumRows = 20;

	/** Horizontal depth of one row (tread), meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Cavea", meta = (ClampMin = "0.1"))
	float RowDepthMeters = 0.8f;

	/** Height step of one row (rise), meters; 20 rows x 0.75 m = 15 m (SPEC: height ~15 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Cavea", meta = (ClampMin = "0.05"))
	float RowRiseMeters = 0.75f;

	/** Ear height of a seated listener above the step of its row, meters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Cavea", meta = (ClampMin = "0.0"))
	float SeatedEarHeightMeters = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Source")
	FName SourceId = TEXT("S1");

	/** Source height above the arena floor at the centre, meters (SPEC: 1.6 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Source", meta = (ClampMin = "0.0"))
	float SourceHeightMeters = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Receivers")
	TArray<FAmphitheatreSeat> Seats;

	/** Spacing of the heatmap receiver grid over the rows, meters (SPEC: 2 m; used by M10). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Receivers", meta = (ClampMin = "0.5"))
	float GridSpacingMeters = 2.0f;

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	FVector GetSourcePositionMeters() const;

	/** World position of a seat (listener ear), meters. */
	UFUNCTION(BlueprintPure, Category = "Acoustics")
	FVector GetSeatPositionMeters(const FAmphitheatreSeat& Seat) const;

	/** Seat of a receiver id, nullptr when unknown. */
	const FAmphitheatreSeat* FindSeat(FName ReceiverId) const;

	/** Moves the source with SourceId and every receiver listed in Seats (matched by id) to their positions; returns the number of moved actors. */
	int32 ApplyToWorld(UWorld* World) const;

	/** The SPEC §3.4 seats R1 (row 1), R2 (row 10), R3 (row NumRows), R4 (row 10, side sector on the major axis). */
	static TArray<FAmphitheatreSeat> MakeDefaultSeats(int32 InNumRows);
};
