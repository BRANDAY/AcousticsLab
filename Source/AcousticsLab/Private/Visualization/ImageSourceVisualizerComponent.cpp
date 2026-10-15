#include "Visualization/ImageSourceVisualizerComponent.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Visualization/HeatmapComponent.h"
#include "Visualization/RayPathVisualizerComponent.h"

UImageSourceVisualizerComponent::UImageSourceVisualizerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UImageSourceVisualizerComponent::OnRegister()
{
	Super::OnRegister();
	GetOrCreateSpheres();
	GetOrCreateLineBatch();
}

void UImageSourceVisualizerComponent::OnUnregister()
{
	if (Spheres)
	{
		if (!Spheres->IsBeingDestroyed())
		{
			Spheres->DestroyComponent();
		}
		Spheres = nullptr;
	}
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

UInstancedStaticMeshComponent* UImageSourceVisualizerComponent::GetOrCreateSpheres()
{
	if (Spheres || !GetOwner() || !GetWorld())
	{
		return Spheres;
	}
	UStaticMesh* Mesh = SphereMesh ? SphereMesh.Get() : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Material = SphereMaterial ? SphereMaterial.Get() : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Acoustics/Materials/M_HeatmapTile.M_HeatmapTile"));
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		UE_LOG(LogAcoustics, Warning, TEXT("ImageSourceVisualizer: M_HeatmapTile not found, spheres use the engine basic-shape material (no order colors)"));
	}
	Spheres = NewObject<UInstancedStaticMeshComponent>(GetOwner(), TEXT("ImageSourceSpheres"), RF_Transient);
	Spheres->SetupAttachment(GetOwner()->GetRootComponent());
	Spheres->SetStaticMesh(Mesh);
	if (Material)
	{
		Spheres->SetMaterial(0, Material);
	}
	Spheres->NumCustomDataFloats = UHeatmapComponent::CustomFloatsPerTile;
	Spheres->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Spheres->SetCastShadow(false);
	Spheres->bIsEditorOnly = false;
	Spheres->SetHiddenInGame(false);
	Spheres->RegisterComponent();
	return Spheres;
}

ULineBatchComponent* UImageSourceVisualizerComponent::GetOrCreateLineBatch()
{
	if (LineBatch || !GetOwner() || !GetWorld())
	{
		return LineBatch;
	}
	LineBatch = NewObject<ULineBatchComponent>(GetOwner(), TEXT("ImageSourceLines"), RF_Transient);
	LineBatch->SetupAttachment(GetOwner()->GetRootComponent());
	LineBatch->SetHiddenInGame(false);
	LineBatch->bIsEditorOnly = false;
	LineBatch->RegisterComponent();
	return LineBatch;
}

bool UImageSourceVisualizerComponent::HasContent() const
{
	if (!Result.IsValid() || !Result->bSuccess)
	{
		return false;
	}
	if (Result->ImageSources.Num() > 0)
	{
		return true;
	}
	for (const FReceiverResult& Receiver : Result->Receivers)
	{
		if (Receiver.EarlyReflections.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

void UImageSourceVisualizerComponent::SetResult(const TSharedPtr<const FSimulationResult>& InResult)
{
	Result = InResult;
	Rebuild();
}

void UImageSourceVisualizerComponent::SetEnabled(bool bInEnabled)
{
	if (bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
		Rebuild();
	}
}

void UImageSourceVisualizerComponent::SetOnlyValidForReceiver(bool bInOnly)
{
	if (bOnlyValidForReceiver != bInOnly)
	{
		bOnlyValidForReceiver = bInOnly;
		Rebuild();
	}
}

void UImageSourceVisualizerComponent::SetReceiverFilter(int32 InReceiverIndex)
{
	const int32 Clamped = FMath::Max<int32>(INDEX_NONE, InReceiverIndex);
	if (ReceiverFilter != Clamped)
	{
		ReceiverFilter = Clamped;
		Rebuild();
	}
}

void UImageSourceVisualizerComponent::SetMaxDrawOrder(int32 InMaxOrder)
{
	const int32 Clamped = FMath::Clamp(InMaxOrder, 0, 10);
	if (MaxDrawOrder != Clamped)
	{
		MaxDrawOrder = Clamped;
		Rebuild();
	}
}

void UImageSourceVisualizerComponent::Clear()
{
	NumDrawnImages = NumValidImages = NumDrawnPaths = 0;
	if (Spheres)
	{
		Spheres->ClearInstances();
	}
	if (LineBatch)
	{
		LineBatch->Flush();
	}
}

void UImageSourceVisualizerComponent::AddSphere(const FVector3d& PositionMeters, const FLinearColor& Color)
{
	const FTransform Transform(FQuat::Identity, FVector(PositionMeters * Acoustics::CentimetersPerMeter), FVector(SphereRadiusCm / 50.0f)); // the engine sphere has a 50 cm radius
	const int32 Instance = Spheres->AddInstance(Transform, /*bWorldSpace*/ true);
	Spheres->SetCustomDataValue(Instance, 0, Color.R, false);
	Spheres->SetCustomDataValue(Instance, 1, Color.G, false);
	Spheres->SetCustomDataValue(Instance, 2, Color.B, false);
	Spheres->SetCustomDataValue(Instance, 3, 1.0f, false);
	++NumDrawnImages;
}

void UImageSourceVisualizerComponent::Rebuild()
{
	Clear();
	UInstancedStaticMeshComponent* SphereComponent = GetOrCreateSpheres();
	ULineBatchComponent* Batch = GetOrCreateLineBatch();
	if (!bEnabled || !SphereComponent || !Batch || !HasContent())
	{
		return;
	}
	const FReceiverResult* Receiver = Result->Receivers.IsValidIndex(ReceiverFilter) ? &Result->Receivers[ReceiverFilter] : nullptr;

	// Which images have a valid path: to the filtered receiver, or to any receiver when none is filtered.
	TSet<int32> ValidImages;
	if (Receiver)
	{
		for (const FEarlyReflection& Reflection : Receiver->EarlyReflections)
		{
			if (Reflection.ImageSourceIndex != INDEX_NONE)
			{
				ValidImages.Add(Reflection.ImageSourceIndex);
			}
		}
	}

	if (Result->ImageSources.Num() > 0)
	{
		for (int32 Index = 0; Index < Result->ImageSources.Num() && NumDrawnImages < MaxDrawImages; ++Index)
		{
			const FImageSourceSample& Image = Result->ImageSources[Index];
			if (Image.Order > MaxDrawOrder)
			{
				continue;
			}
			const bool bValid = Receiver ? ValidImages.Contains(Index) : Image.bValidForAnyReceiver;
			NumValidImages += bValid ? 1 : 0;
			if (!bValid && bOnlyValidForReceiver)
			{
				continue;
			}
			FLinearColor Color = URayPathVisualizerComponent::ColorForOrder(Image.Order);
			if (!bValid)
			{
				Color *= InvalidBrightness;
				Color.A = 1.0f;
			}
			AddSphere(Image.Position, Color);
		}
	}
	else if (Receiver)
	{
		// Beam tracer: the apex of every beam containing the receiver is the image source of that path.
		for (const FEarlyReflection& Reflection : Receiver->EarlyReflections)
		{
			if (Reflection.Order <= MaxDrawOrder && NumDrawnImages < MaxDrawImages)
			{
				AddSphere(Reflection.ImageSource, URayPathVisualizerComponent::ColorForOrder(Reflection.Order));
				++NumValidImages;
			}
		}
	}
	SphereComponent->MarkRenderStateDirty();

	// Paths of the filtered receiver: source -> reflection points -> receiver, each segment colored by its order.
	if (Receiver)
	{
		TArray<FBatchedLine> Lines;
		const float Thickness = FMath::Max(0.0f, LineThickness);
		for (const FEarlyReflection& Reflection : Receiver->EarlyReflections)
		{
			if (Reflection.Order > MaxDrawOrder || Reflection.Path.Num() < 2)
			{
				continue;
			}
			for (int32 Segment = 0; Segment + 1 < Reflection.Path.Num(); ++Segment)
			{
				FLinearColor Color = URayPathVisualizerComponent::ColorForOrder(Segment);
				Color.A = 0.9f;
				Lines.Emplace(FVector(Reflection.Path[Segment] * Acoustics::CentimetersPerMeter), FVector(Reflection.Path[Segment + 1] * Acoustics::CentimetersPerMeter), Color, /*LifeTime*/ 0.0f, Thickness, SDPG_World);
			}
			++NumDrawnPaths;
		}
		if (Lines.Num() > 0)
		{
			Batch->DrawLines(Lines);
		}
	}
	UE_LOG(LogAcoustics, Verbose, TEXT("Image sources: %d spheres (%d valid), %d paths, order <= %d, receiver %d"), NumDrawnImages, NumValidImages, NumDrawnPaths, MaxDrawOrder, ReceiverFilter);
}
