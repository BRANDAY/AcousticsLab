#include "Levels/ShoeboxRoomActor.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

AShoeboxRoomActor::AShoeboxRoomActor()
	: MaterialName(TEXT("Concrete"))
	, AcousticTag(TEXT("Acoustic"))
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		WallMesh = PlaneMesh.Object;
	}

	static const TCHAR* WallNames[6] = { TEXT("Floor"), TEXT("Ceiling"), TEXT("WallXMin"), TEXT("WallXMax"), TEXT("WallYMin"), TEXT("WallYMax") };
	for (const TCHAR* WallName : WallNames)
	{
		UStaticMeshComponent* Wall = CreateDefaultSubobject<UStaticMeshComponent>(WallName);
		Wall->SetupAttachment(RootComponent);
		Wall->SetMobility(EComponentMobility::Static);
		Wall->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Walls.Add(Wall);
	}
}

void AShoeboxRoomActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRoom();
}

void AShoeboxRoomActor::RebuildRoom()
{
	if (Walls.Num() != 6)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("%s: expected 6 wall components, found %d"), *GetName(), Walls.Num());
		return;
	}

	const FVector Size(FMath::Max(0.5, RoomSizeMeters.X), FMath::Max(0.5, RoomSizeMeters.Y), FMath::Max(0.5, RoomSizeMeters.Z));
	const double Cm = Acoustics::CentimetersPerMeter;

	struct FWallDefinition
	{
		FVector Center;   // cm, relative to the actor
		FVector Normal;   // inward
		FVector XAxis;    // world direction the quad's local X spans
		FVector2D SizeM;  // extent along local X and local Y, meters
	};
	// FRotationMatrix::MakeFromZX puts the quad's +Z (its front) on Normal and its local X along XAxis, local Y along Z x X.
	const FWallDefinition Definitions[6] = {
		{ FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(1.0, 0.0, 0.0), FVector2D(Size.X, Size.Y) },                      // floor
		{ FVector(0.0, 0.0, Size.Z * Cm), FVector(0.0, 0.0, -1.0), FVector(1.0, 0.0, 0.0), FVector2D(Size.X, Size.Y) },            // ceiling
		{ FVector(-0.5 * Size.X * Cm, 0.0, 0.5 * Size.Z * Cm), FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.Y) },  // wall x = -X/2
		{ FVector(0.5 * Size.X * Cm, 0.0, 0.5 * Size.Z * Cm), FVector(-1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.Y) },  // wall x = +X/2
		{ FVector(0.0, -0.5 * Size.Y * Cm, 0.5 * Size.Z * Cm), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.X) },  // wall y = -Y/2
		{ FVector(0.0, 0.5 * Size.Y * Cm, 0.5 * Size.Z * Cm), FVector(0.0, -1.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.X) },  // wall y = +Y/2
	};

	TArray<FName> WallTags;
	WallTags.Add(AcousticTag);
	if (!MaterialName.IsNone())
	{
		WallTags.Add(FName(*FString::Printf(TEXT("AcMat:%s"), *MaterialName.ToString())));
	}

	for (int32 Index = 0; Index < 6; ++Index)
	{
		UStaticMeshComponent* Wall = Walls[Index];
		if (!Wall)
		{
			continue;
		}
		const FWallDefinition& Definition = Definitions[Index];
		const FRotator Rotation = FRotationMatrix::MakeFromZX(Definition.Normal, Definition.XAxis).Rotator();
		const FTransform WallTransform(Rotation, Definition.Center, FVector(Definition.SizeM.X, Definition.SizeM.Y, 1.0));
		if (!Wall->GetRelativeTransform().Equals(WallTransform, 1e-4))
		{
			Wall->SetRelativeTransform(WallTransform);
		}
		if (Wall->GetStaticMesh() != WallMesh)
		{
			Wall->SetStaticMesh(WallMesh);
		}
		Wall->ComponentTags = WallTags;
	}
}

AShoeboxRoomActor* AShoeboxRoomActor::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AShoeboxRoomActor> It(World);
	return It ? *It : nullptr;
}
