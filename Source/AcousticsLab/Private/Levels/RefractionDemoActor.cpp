#include "Levels/RefractionDemoActor.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Generators/SphereGenerator.h"
#include "Materials/MaterialInterface.h"
#include "Scene/AcousticMediumPresets.h"
#include "UObject/ConstructorHelpers.h"

ARefractionDemoActor::ARefractionDemoActor()
	: WallMaterialName(TEXT("Concrete"))
	, LensOutsideMedium(TEXT("Glycerol"))
	, LensInsideMedium(TEXT("Rubber"))
	, AcousticTag(TEXT("Acoustic"))
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		QuadMesh = PlaneMesh.Object;
	}

	static const TCHAR* WallNames[5] = { TEXT("Floor"), TEXT("WallXMin"), TEXT("WallXMax"), TEXT("WallYMin"), TEXT("WallYMax") };
	for (const TCHAR* WallName : WallNames)
	{
		UStaticMeshComponent* Wall = CreateDefaultSubobject<UStaticMeshComponent>(WallName);
		Wall->SetupAttachment(RootComponent);
		Wall->SetMobility(EComponentMobility::Static);
		Wall->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		PoolWalls.Add(Wall);
	}
	WaterSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterSurface"));
	WaterSurface->SetupAttachment(RootComponent);
	WaterSurface->SetMobility(EComponentMobility::Static);
	WaterSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterSurface->SetCastShadow(false);

	LensSphere = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("LensSphere"));
	LensSphere->SetupAttachment(RootComponent);
	LensSphere->SetMobility(EComponentMobility::Static);
	LensSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LensSphere->SetCastShadow(false);
	LensSphere->SetShadowsEnabled(false);
}

void ARefractionDemoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildScenes();
}

UMaterialInterface* ARefractionDemoActor::ResolveInterfaceMaterial()
{
	if (ResolvedInterfaceMaterial)
	{
		return ResolvedInterfaceMaterial;
	}
	ResolvedInterfaceMaterial = InterfaceMaterial ? InterfaceMaterial.Get() : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Acoustics/Materials/M_Interface.M_Interface"));
	if (!ResolvedInterfaceMaterial && !bWarnedMaterial)
	{
		bWarnedMaterial = true;
		UE_LOG(LogAcoustics, Warning, TEXT("%s: M_Interface not found (run Tools/Editor/create_m6_content.py); the interfaces render opaque"), *GetName());
	}
	return ResolvedInterfaceMaterial;
}

void ARefractionDemoActor::RebuildScenes()
{
	BuildPool();
	BuildLens();
}

void ARefractionDemoActor::BuildPool()
{
	if (PoolWalls.Num() != 5 || !WaterSurface)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("%s: expected 5 pool wall components and a water surface"), *GetName());
		return;
	}
	const FVector Size(FMath::Max(1.0, PoolSizeMeters.X), FMath::Max(1.0, PoolSizeMeters.Y), FMath::Max(1.0, PoolSizeMeters.Z));
	const double Depth = FMath::Clamp(static_cast<double>(WaterDepthMeters), 0.1, Size.Z);
	const double Cm = Acoustics::CentimetersPerMeter;

	struct FWallDefinition
	{
		FVector Center;   // cm, relative to the actor
		FVector Normal;   // inward
		FVector XAxis;    // world direction the quad's local X spans
		FVector2D SizeM;  // extent along local X and local Y, meters
	};
	// FRotationMatrix::MakeFromZX puts the quad's +Z (its front) on Normal and its local X along XAxis.
	const FWallDefinition Definitions[5] = {
		{ FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(1.0, 0.0, 0.0), FVector2D(Size.X, Size.Y) },                                        // floor
		{ FVector(-0.5 * Size.X * Cm, 0.0, 0.5 * Size.Z * Cm), FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.Y) },  // wall x = -X/2
		{ FVector(0.5 * Size.X * Cm, 0.0, 0.5 * Size.Z * Cm), FVector(-1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.Y) },  // wall x = +X/2
		{ FVector(0.0, -0.5 * Size.Y * Cm, 0.5 * Size.Z * Cm), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.X) },  // wall y = -Y/2
		{ FVector(0.0, 0.5 * Size.Y * Cm, 0.5 * Size.Z * Cm), FVector(0.0, -1.0, 0.0), FVector(0.0, 0.0, 1.0), FVector2D(Size.Z, Size.X) },  // wall y = +Y/2
	};

	TArray<FName> WallTags;
	WallTags.Add(AcousticTag);
	if (!WallMaterialName.IsNone())
	{
		WallTags.Add(FName(*FString::Printf(TEXT("AcMat:%s"), *WallMaterialName.ToString())));
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		UStaticMeshComponent* Wall = PoolWalls[Index];
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
		if (Wall->GetStaticMesh() != QuadMesh)
		{
			Wall->SetStaticMesh(QuadMesh);
		}
		Wall->ComponentTags = bBuildPool ? WallTags : TArray<FName>();
		Wall->SetVisibility(bBuildPool);
		Wall->SetCollisionEnabled(bBuildPool ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}

	// Water surface: front (+Z) faces the air, so medium front = Air, back = Water; the extractor marks it as an interface
	// from the tag "Interface" (and from the WaterSurface material).
	const FTransform SurfaceTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, Depth * Cm), FVector(Size.X, Size.Y, 1.0));
	if (!WaterSurface->GetRelativeTransform().Equals(SurfaceTransform, 1e-4))
	{
		WaterSurface->SetRelativeTransform(SurfaceTransform);
	}
	if (WaterSurface->GetStaticMesh() != QuadMesh)
	{
		WaterSurface->SetStaticMesh(QuadMesh);
	}
	TArray<FName> SurfaceTags;
	if (bBuildPool)
	{
		SurfaceTags = { AcousticTag, TEXT("Interface"), TEXT("AcMat:WaterSurface"), TEXT("AcMediumFront:Air"), TEXT("AcMediumBack:Water") };
	}
	WaterSurface->ComponentTags = SurfaceTags;
	WaterSurface->SetVisibility(bBuildPool);
	if (UMaterialInterface* Material = ResolveInterfaceMaterial())
	{
		WaterSurface->SetMaterial(0, Material);
	}
}

void ARefractionDemoActor::BuildLens()
{
	if (!LensSphere)
	{
		return;
	}
	const double Cm = Acoustics::CentimetersPerMeter;
	const FVector Centre = (LensOffsetMeters + LensCentreOffsetMeters) * Cm;
	const FTransform SphereTransform(FRotator::ZeroRotator, Centre, FVector::OneVector);
	if (!LensSphere->GetRelativeTransform().Equals(SphereTransform, 1e-4))
	{
		LensSphere->SetRelativeTransform(SphereTransform);
	}

	// Latitude / longitude sphere from GeometryCore (Engine/Source/Runtime/GeometryCore/Public/Generators/SphereGenerator.h),
	// outward normals in UE winding; the extractor keeps the face normal, so the front of every triangle is the outside.
	UE::Geometry::FSphereGenerator Generator;
	Generator.Radius = FMath::Max(0.05, static_cast<double>(LensRadiusMeters)) * Cm;
	Generator.NumPhi = FMath::Clamp(LensNumPhi, 4, 128);
	Generator.NumTheta = FMath::Clamp(LensNumTheta, 6, 256);
	Generator.bPolygroupPerQuad = false;
	Generator.Generate();
	UE::Geometry::FDynamicMesh3 Mesh(&Generator);
	LensSphere->SetMesh(MoveTemp(Mesh));

	TArray<FName> LensTags;
	if (bBuildLens)
	{
		LensTags = { AcousticTag, TEXT("Interface"), TEXT("AcMat:Interface"),
			FName(*FString::Printf(TEXT("AcMediumFront:%s"), *LensOutsideMedium.ToString())),
			FName(*FString::Printf(TEXT("AcMediumBack:%s"), *LensInsideMedium.ToString())) };
	}
	LensSphere->ComponentTags = LensTags;
	LensSphere->SetVisibility(bBuildLens);
	if (UMaterialInterface* Material = ResolveInterfaceMaterial())
	{
		LensSphere->SetMaterial(0, Material);
	}
}

FVector ARefractionDemoActor::GetPoolSourcePositionMeters() const
{
	const FVector Local(-PoolSizeMeters.X / 3.0, 0.0, FMath::Min(static_cast<double>(WaterDepthMeters) + 1.5, PoolSizeMeters.Z + 1.5));
	return (GetActorLocation() + GetActorRotation().RotateVector(Local * Acoustics::CentimetersPerMeter)) / Acoustics::CentimetersPerMeter;
}

FVector ARefractionDemoActor::GetLensSourcePositionMeters() const
{
	return (GetActorLocation() + GetActorRotation().RotateVector(LensOffsetMeters * Acoustics::CentimetersPerMeter)) / Acoustics::CentimetersPerMeter;
}

FVector ARefractionDemoActor::GetLensCentreMeters() const
{
	return (GetActorLocation() + GetActorRotation().RotateVector((LensOffsetMeters + LensCentreOffsetMeters) * Acoustics::CentimetersPerMeter)) / Acoustics::CentimetersPerMeter;
}

float ARefractionDemoActor::GetLensParaxialFocusMeters() const
{
	FMedium Outside = FMedium::Glycerol();
	FMedium Inside = FMedium::Rubber();
	AcousticMediumPresets::Find(LensOutsideMedium, Outside);
	AcousticMediumPresets::Find(LensInsideMedium, Inside);
	const double N = Inside.SpeedOfSound > 0.0 ? Outside.SpeedOfSound / Inside.SpeedOfSound : 1.0;
	if (FMath::Abs(N - 1.0) < 1e-9)
	{
		return TNumericLimits<float>::Max();
	}
	return static_cast<float>(N * LensRadiusMeters / (2.0 * (N - 1.0)));
}

ARefractionDemoActor* ARefractionDemoActor::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<ARefractionDemoActor> It(World);
	return It ? *It : nullptr;
}
