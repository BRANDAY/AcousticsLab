#include "Visualization/RayPathVisualizerComponent.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/LineBatchComponent.h"
#include "GameFramework/Actor.h"
#include "Results/RoomMetrics.h"

URayPathVisualizerComponent::URayPathVisualizerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URayPathVisualizerComponent::OnRegister()
{
	Super::OnRegister();
	GetOrCreateLineBatch();
}

void URayPathVisualizerComponent::OnUnregister()
{
	if (LineBatch)
	{
		LineBatch->Flush();
		if (!LineBatch->IsBeingDestroyed())
		{
			LineBatch->DestroyComponent();
		}
		LineBatch = nullptr;
	}
	Super::OnUnregister();
}

ULineBatchComponent* URayPathVisualizerComponent::GetOrCreateLineBatch()
{
	if (LineBatch || !GetOwner() || !GetWorld())
	{
		return LineBatch;
	}
	LineBatch = NewObject<ULineBatchComponent>(GetOwner(), TEXT("RayPathLines"), RF_Transient);
	LineBatch->SetupAttachment(GetOwner()->GetRootComponent());
	LineBatch->SetHiddenInGame(false);
	LineBatch->bIsEditorOnly = false;
	LineBatch->RegisterComponent();
	return LineBatch;
}

FLinearColor URayPathVisualizerComponent::ColorForOrder(int32 Order)
{
	switch (Order)
	{
	case 0: return FLinearColor::White;
	case 1: return FLinearColor(1.0f, 0.9f, 0.1f);
	case 2: return FLinearColor(1.0f, 0.5f, 0.05f);
	default: return FLinearColor(0.95f, 0.1f, 0.05f);
	}
}

float URayPathVisualizerComponent::OpacityForEnergy(double RelativeEnergy, float RangeDb, float MinAlpha)
{
	const double LevelDb = RoomAcoustics::LevelDb(RelativeEnergy);
	const double Range = FMath::Max(1.0, static_cast<double>(RangeDb));
	const double Fraction = FMath::Clamp(1.0 + LevelDb / Range, 0.0, 1.0);
	return static_cast<float>(MinAlpha + (1.0 - MinAlpha) * Fraction);
}

void URayPathVisualizerComponent::SetResult(const TSharedPtr<const FSimulationResult>& InResult)
{
	Result = InResult;
	Rebuild();
}

void URayPathVisualizerComponent::SetEnabled(bool bInEnabled)
{
	if (bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
		Rebuild();
	}
}

void URayPathVisualizerComponent::SetOnlyPathsReachingReceiver(bool bInOnly)
{
	if (bOnlyPathsReachingReceiver != bInOnly)
	{
		bOnlyPathsReachingReceiver = bInOnly;
		Rebuild();
	}
}

void URayPathVisualizerComponent::SetReceiverFilter(int32 InReceiverIndex)
{
	const int32 Clamped = FMath::Max<int32>(INDEX_NONE, InReceiverIndex);
	if (ReceiverFilter != Clamped)
	{
		ReceiverFilter = Clamped;
		if (bOnlyPathsReachingReceiver)
		{
			Rebuild();
		}
	}
}

void URayPathVisualizerComponent::SetMaxDrawOrder(int32 InMaxOrder)
{
	const int32 Clamped = FMath::Clamp(InMaxOrder, 0, 100000);
	if (MaxDrawOrder != Clamped)
	{
		MaxDrawOrder = Clamped;
		Rebuild();
	}
}

void URayPathVisualizerComponent::Clear()
{
	NumDrawnPaths = NumDrawnSegments = NumMatchingPaths = 0;
	if (LineBatch)
	{
		LineBatch->Flush();
	}
}

void URayPathVisualizerComponent::Rebuild()
{
	Clear();
	ULineBatchComponent* Batch = GetOrCreateLineBatch();
	if (!bEnabled || !Batch || !Result.IsValid() || !Result->bSuccess)
	{
		return;
	}

	const float Thickness = FMath::Max(0.0f, LineThickness);
	const int32 PathBudget = FMath::Clamp(MaxPaths, 0, Acoustics::DefaultMaxPathsForVisualization);
	TArray<FBatchedLine> Lines;
	Lines.Reserve(FMath::Min(MaxDrawSegments, PathBudget * (MaxDrawOrder + 1)));

	for (const FPathSample& Path : Result->Paths)
	{
		const FPathReceiverHit* Hit = nullptr;
		if (bOnlyPathsReachingReceiver)
		{
			// First entry into the filtered receiver, and it must happen within the drawn orders: in a reverberant room
			// almost every ray enters every receiver eventually, so a filter without the order bound keeps everything.
			Hit = Path.FindReceiverHit(ReceiverFilter);
			if (!Hit || Hit->Order > MaxDrawOrder)
			{
				continue;
			}
		}
		++NumMatchingPaths;
		if (NumDrawnPaths >= PathBudget || Lines.Num() >= MaxDrawSegments)
		{
			continue; // keep counting the matches, stop drawing
		}

		// Segment i: Points[i] -> Points[i + 1], order i; in receiver mode the last drawn segment ends at the entry point.
		const int32 LastSegment = Hit ? FMath::Min(Hit->Order, Path.NumSegments() - 1) : Path.NumSegments() - 1;
		int32 Drawn = 0;
		for (int32 Segment = 0; Segment <= LastSegment && Segment <= MaxDrawOrder && Lines.Num() < MaxDrawSegments; ++Segment)
		{
			const FVector3d Start = Path.Points[Segment];
			const FVector3d End = (Hit && Segment == Hit->Order) ? Hit->EntryPoint : Path.Points[Segment + 1];
			const double Energy = Path.SegmentEnergies.IsValidIndex(Segment) ? Path.SegmentEnergies[Segment] : Path.FinalEnergy;
			FLinearColor Color = ColorForOrder(Segment);
			Color.A = OpacityForEnergy(Energy, EnergyRangeDb, MinOpacity);
			Lines.Emplace(FVector(Start * Acoustics::CentimetersPerMeter), FVector(End * Acoustics::CentimetersPerMeter), Color, /*LifeTime*/ 0.0f, Thickness, SDPG_World);
			++Drawn;
		}
		if (Drawn > 0)
		{
			++NumDrawnPaths;
			NumDrawnSegments += Drawn;
		}
	}

	if (Lines.Num() > 0)
	{
		Batch->DrawLines(Lines);
	}
	UE_LOG(LogAcoustics, Verbose, TEXT("Ray paths: %d drawn (%d matching, %d segments, order <= %d%s)"), NumDrawnPaths, NumMatchingPaths, NumDrawnSegments, MaxDrawOrder,
		bOnlyPathsReachingReceiver ? (ReceiverFilter == INDEX_NONE ? TEXT(", reaching any receiver") : TEXT(", reaching the selected receiver")) : TEXT(""));
}
