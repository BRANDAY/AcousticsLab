#include "Levels/AmphitheatrePositionsAsset.h"

#include "AcousticsCore.h"
#include "AcousticsConstants.h"
#include "Scene/AcousticReceiver.h"
#include "Scene/AcousticSource.h"

UAmphitheatrePositionsAsset::UAmphitheatrePositionsAsset()
{
	Seats = MakeDefaultSeats(NumRows);
}

TArray<FAmphitheatreSeat> UAmphitheatrePositionsAsset::MakeDefaultSeats(int32 InNumRows)
{
	const int32 LastRow = FMath::Max(1, InNumRows);
	const int32 MiddleRow = FMath::Clamp(10, 1, LastRow);
	TArray<FAmphitheatreSeat> Result;
	Result.Add({ TEXT("R1"), 1, 90.0f, TEXT("1st row") });
	Result.Add({ TEXT("R2"), MiddleRow, 90.0f, FString::Printf(TEXT("row %d"), MiddleRow) });
	Result.Add({ TEXT("R3"), LastRow, 90.0f, FString::Printf(TEXT("row %d (top)"), LastRow) });
	Result.Add({ TEXT("R4"), MiddleRow, 0.0f, FString::Printf(TEXT("side sector, row %d"), MiddleRow) });
	return Result;
}

FVector UAmphitheatrePositionsAsset::GetSourcePositionMeters() const
{
	return CenterMeters + FVector(0.0, 0.0, SourceHeightMeters);
}

FVector UAmphitheatrePositionsAsset::GetSeatPositionMeters(const FAmphitheatreSeat& Seat) const
{
	// Centre line of row k lies (k - 0.5) row depths outside the arena ellipse; its step is k rises high.
	const int32 Row = FMath::Clamp(Seat.Row, 1, FMath::Max(1, NumRows));
	const double Offset = (Row - 0.5) * RowDepthMeters;
	const double Azimuth = FMath::DegreesToRadians(static_cast<double>(Seat.AzimuthDeg));
	const double X = (ArenaSemiAxesMeters.X + Offset) * FMath::Cos(Azimuth);
	const double Y = (ArenaSemiAxesMeters.Y + Offset) * FMath::Sin(Azimuth);
	const double Z = Row * RowRiseMeters + SeatedEarHeightMeters;
	return CenterMeters + FVector(X, Y, Z);
}

const FAmphitheatreSeat* UAmphitheatrePositionsAsset::FindSeat(FName ReceiverId) const
{
	return Seats.FindByPredicate([ReceiverId](const FAmphitheatreSeat& Seat) { return Seat.ReceiverId == ReceiverId; });
}

int32 UAmphitheatrePositionsAsset::ApplyToWorld(UWorld* World) const
{
	if (!World)
	{
		return 0;
	}
	int32 NumMoved = 0;
	for (AAcousticSource* Source : AAcousticSource::FindAll(World))
	{
		if (Source && Source->SourceId == SourceId)
		{
			Source->Modify();
			Source->SetActorLocation(GetSourcePositionMeters() * Acoustics::CentimetersPerMeter);
			++NumMoved;
		}
	}
	for (AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(World))
	{
		if (const FAmphitheatreSeat* Seat = Receiver ? FindSeat(Receiver->ReceiverId) : nullptr)
		{
			Receiver->Modify();
			Receiver->SetActorLocation(GetSeatPositionMeters(*Seat) * Acoustics::CentimetersPerMeter);
			++NumMoved;
		}
	}
	UE_LOG(LogAcoustics, Log, TEXT("Amphitheatre positions %s applied to %d actors"), *GetName(), NumMoved);
	return NumMoved;
}
