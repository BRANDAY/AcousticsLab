#include "Scene/AcousticSource.h"

#include "AcousticsConstants.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AAcousticSource::AAcousticSource()
	: SourceId(TEXT("S1"))
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(RootComponent);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCastShadow(false);
	Marker->SetRelativeScale3D(FVector(0.3f)); // 30 cm sphere
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Marker->SetStaticMesh(SphereMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MarkerMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MarkerMaterial.Succeeded())
	{
		Marker->SetMaterial(0, MarkerMaterial.Object);
	}
}

FVector3d AAcousticSource::GetPositionMeters() const
{
	return FVector3d(GetActorLocation()) / Acoustics::CentimetersPerMeter;
}

TArray<AAcousticSource*> AAcousticSource::FindAll(UWorld* World)
{
	TArray<AAcousticSource*> Result;
	if (World)
	{
		for (TActorIterator<AAcousticSource> It(World); It; ++It)
		{
			Result.Add(*It);
		}
		Result.Sort([](const AAcousticSource& A, const AAcousticSource& B) { return A.SourceId.LexicalLess(B.SourceId); });
	}
	return Result;
}
