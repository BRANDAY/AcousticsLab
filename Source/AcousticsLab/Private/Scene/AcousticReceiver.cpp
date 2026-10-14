#include "Scene/AcousticReceiver.h"

#include "AcousticsConstants.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AAcousticReceiver::AAcousticReceiver()
	: ReceiverId(TEXT("R1"))
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	RadiusShape = CreateDefaultSubobject<USphereComponent>(TEXT("RadiusShape"));
	RadiusShape->SetupAttachment(RootComponent);
	RadiusShape->InitSphereRadius(RadiusMeters * Acoustics::CentimetersPerMeter);
	RadiusShape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RadiusShape->SetHiddenInGame(false);
	RadiusShape->ShapeColor = FColor(60, 200, 255);

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(RootComponent);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCastShadow(false);
	Marker->SetRelativeScale3D(FVector(0.2f)); // 20 cm sphere
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

void AAcousticReceiver::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (RadiusShape)
	{
		RadiusShape->SetSphereRadius(RadiusMeters * Acoustics::CentimetersPerMeter, false);
	}
}

FVector3d AAcousticReceiver::GetPositionMeters() const
{
	return FVector3d(GetActorLocation()) / Acoustics::CentimetersPerMeter;
}

TArray<AAcousticReceiver*> AAcousticReceiver::FindAll(UWorld* World)
{
	TArray<AAcousticReceiver*> Result;
	if (World)
	{
		for (TActorIterator<AAcousticReceiver> It(World); It; ++It)
		{
			Result.Add(*It);
		}
		Result.Sort([](const AAcousticReceiver& A, const AAcousticReceiver& B) { return A.ReceiverId.LexicalLess(B.ReceiverId); });
	}
	return Result;
}
