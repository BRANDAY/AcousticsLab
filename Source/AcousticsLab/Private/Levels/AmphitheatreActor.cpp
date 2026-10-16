#include "Levels/AmphitheatreActor.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Levels/AmphitheatrePositionsAsset.h"
#include "Materials/MaterialInterface.h"
#include "Scene/ReceiverGrid.h"

namespace
{
	const TCHAR* PositionsAssetPath = TEXT("/Game/Acoustics/DA_AmphitheatrePositions.DA_AmphitheatrePositions");

	/** Point on the offset ellipse (a + o, b + o) at parametric angle T, z, in centimeters. */
	FVector3d EllipsePointCm(double A, double B, double Offset, double T, double Z)
	{
		const double Cm = Acoustics::CentimetersPerMeter;
		return FVector3d((A + Offset) * FMath::Cos(T) * Cm, (B + Offset) * FMath::Sin(T) * Cm, Z * Cm);
	}

	/** Appends triangle (I0, I1, I2) wound so that its UE front normal ((C - A) x (B - A)) points along Desired. */
	void AppendOriented(UE::Geometry::FDynamicMesh3& Mesh, int32 I0, int32 I1, int32 I2, const FVector3d& Desired)
	{
		const FVector3d A = Mesh.GetVertex(I0);
		const FVector3d B = Mesh.GetVertex(I1);
		const FVector3d C = Mesh.GetVertex(I2);
		const FVector3d Front = (C - A).Cross(B - A);
		if (Front.Dot(Desired) >= 0.0)
		{
			Mesh.AppendTriangle(I0, I1, I2);
		}
		else
		{
			Mesh.AppendTriangle(I0, I2, I1);
		}
	}

	/** Quad (P00, P01 along the ring; P10, P11 next profile corner) as two oriented triangles. */
	void AppendQuad(UE::Geometry::FDynamicMesh3& Mesh, int32 P00, int32 P01, int32 P10, int32 P11, const FVector3d& Desired)
	{
		AppendOriented(Mesh, P00, P01, P11, Desired);
		AppendOriented(Mesh, P00, P11, P10, Desired);
	}

	/** Outward horizontal normal of the ellipse family at angle T. */
	FVector3d OutwardAt(double A, double B, double T)
	{
		return FVector3d(FMath::Cos(T) / FMath::Max(A, 1e-6), FMath::Sin(T) / FMath::Max(B, 1e-6), 0.0).GetSafeNormal();
	}

	/** Sweeps a closed (offset, z) profile around the ellipse family: ring vertices are shared, so the shell is closed. */
	void SweepProfile(UE::Geometry::FDynamicMesh3& Mesh, double A, double B, int32 Segments, const TArray<FVector2d>& Profile,
		TFunctionRef<FVector3d(int32 SegmentIndex, double T)> DesiredNormal)
	{
		const int32 NumCorners = Profile.Num();
		TArray<int32> Ids;
		Ids.SetNum(NumCorners * Segments);
		for (int32 Corner = 0; Corner < NumCorners; ++Corner)
		{
			for (int32 Seg = 0; Seg < Segments; ++Seg)
			{
				const double T = 2.0 * UE_DOUBLE_PI * Seg / Segments;
				Ids[Corner * Segments + Seg] = Mesh.AppendVertex(EllipsePointCm(A, B, Profile[Corner].X, T, Profile[Corner].Y));
			}
		}
		for (int32 Corner = 0; Corner < NumCorners; ++Corner)
		{
			const int32 Next = (Corner + 1) % NumCorners;
			for (int32 Seg = 0; Seg < Segments; ++Seg)
			{
				const int32 SegNext = (Seg + 1) % Segments;
				const double TMid = 2.0 * UE_DOUBLE_PI * (Seg + 0.5) / Segments;
				AppendQuad(Mesh, Ids[Corner * Segments + Seg], Ids[Corner * Segments + SegNext], Ids[Next * Segments + Seg], Ids[Next * Segments + SegNext],
					DesiredNormal(Corner, TMid));
			}
		}
	}

	void SetupMeshComponent(UDynamicMeshComponent* Component)
	{
		Component->SetMobility(EComponentMobility::Static);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		// Walkable (soundwalk): the triangles themselves are the collision.
		Component->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
		Component->bEnableComplexCollision = true;
	}
}

void AmphitheatreGeometry::BuildFloorMesh(const FAmphitheatreDimensions& Dims, int32 Segments, UE::Geometry::FDynamicMesh3& OutMesh)
{
	OutMesh.Clear();
	const int32 Centre = OutMesh.AppendVertex(FVector3d::ZeroVector);
	TArray<int32> Rim;
	for (int32 Seg = 0; Seg < Segments; ++Seg)
	{
		Rim.Add(OutMesh.AppendVertex(EllipsePointCm(Dims.A, Dims.B, 0.0, 2.0 * UE_DOUBLE_PI * Seg / Segments, 0.0)));
	}
	for (int32 Seg = 0; Seg < Segments; ++Seg)
	{
		AppendOriented(OutMesh, Centre, Rim[Seg], Rim[(Seg + 1) % Segments], FVector3d::UnitZ());
	}
}

void AmphitheatreGeometry::BuildCaveaMesh(const FAmphitheatreDimensions& Dims, int32 Segments, UE::Geometry::FDynamicMesh3& OutMesh)
{
	// Profile (offset, z): ground at the arena edge, then riser / tread per row, back wall, ground face back.
	TArray<FVector2d> Profile;
	Profile.Add(FVector2d(0.0, 0.0));
	for (int32 Row = 1; Row <= Dims.Rows; ++Row)
	{
		Profile.Add(FVector2d((Row - 1) * Dims.Depth, Row * Dims.Rise));
		Profile.Add(FVector2d(Row * Dims.Depth, Row * Dims.Rise));
	}
	Profile.Add(FVector2d(Dims.OuterOffset(), 0.0));

	OutMesh.Clear();
	const double Outer = Dims.OuterOffset();
	SweepProfile(OutMesh, Dims.A, Dims.B, Segments, Profile, [&Profile, &Dims, Outer](int32 Corner, double T) -> FVector3d
	{
		const FVector2d P = Profile[Corner];
		const FVector2d Q = Profile[(Corner + 1) % Profile.Num()];
		const FVector3d Out = OutwardAt(Dims.A, Dims.B, T);
		if (FMath::IsNearlyEqual(P.Y, Q.Y))
		{
			return P.Y > 0.0 ? FVector3d::UnitZ() : -FVector3d::UnitZ(); // tread up, ground face down
		}
		return FMath::IsNearlyEqual(P.X, Outer) ? Out : -Out; // back wall outward, risers toward the arena
	});
}

void AmphitheatreGeometry::BuildVelariumMesh(const FAmphitheatreDimensions& Dims, int32 Segments, double HeightMeters, UE::Geometry::FDynamicMesh3& OutMesh)
{
	OutMesh.Clear();
	const double Z0 = HeightMeters;
	const double Z1 = Z0 + 0.1;
	const double Outer = Dims.OuterOffset();
	// Thin closed ring: underside (facing down, the acoustic face), outer rim, top, inner rim.
	const TArray<FVector2d> Profile = { FVector2d(0.0, Z0), FVector2d(Outer, Z0), FVector2d(Outer, Z1), FVector2d(0.0, Z1) };
	SweepProfile(OutMesh, Dims.A, Dims.B, Segments, Profile, [&Dims](int32 Corner, double T) -> FVector3d
	{
		switch (Corner)
		{
		case 0: return -FVector3d::UnitZ();
		case 1: return OutwardAt(Dims.A, Dims.B, T);
		case 2: return FVector3d::UnitZ();
		default: return -OutwardAt(Dims.A, Dims.B, T);
		}
	});
}

int32 AmphitheatreGeometry::RowAtPlanPoint(const FAmphitheatreDimensions& Dims, double X, double Y)
{
	// F(o) = x^2/(a+o)^2 + y^2/(b+o)^2 decreases with o; the point lies on the offset ellipse where F = 1.
	auto F = [X, Y, &Dims](double O) { return X * X / FMath::Square(Dims.A + O) + Y * Y / FMath::Square(Dims.B + O); };
	if (F(0.0) <= 1.0)
	{
		return 0;
	}
	const double Outer = Dims.OuterOffset();
	if (F(Outer) > 1.0)
	{
		return -1;
	}
	double Lo = 0.0, Hi = Outer;
	for (int32 Iteration = 0; Iteration < 40; ++Iteration)
	{
		const double Mid = 0.5 * (Lo + Hi);
		(F(Mid) > 1.0 ? Lo : Hi) = Mid;
	}
	return FMath::Clamp(FMath::FloorToInt32(0.5 * (Lo + Hi) / Dims.Depth) + 1, 1, Dims.Rows);
}

void AmphitheatreGeometry::MakeGridPoints(const FAmphitheatreDimensions& Dims, TArray<FVector>& OutCenters, TArray<int32>& OutRows)
{
	OutCenters.Reset();
	OutRows.Reset();
	const double Step = FMath::Max(0.25, Dims.GridStep);
	const double Outer = Dims.OuterOffset();
	const int32 HalfX = FMath::CeilToInt32((Dims.A + Outer) / Step);
	const int32 HalfY = FMath::CeilToInt32((Dims.B + Outer) / Step);
	for (int32 IY = -HalfY; IY <= HalfY; ++IY)
	{
		for (int32 IX = -HalfX; IX <= HalfX; ++IX)
		{
			const double X = (IX + 0.5) * Step;
			const double Y = (IY + 0.5) * Step;
			const int32 Row = RowAtPlanPoint(Dims, X, Y);
			if (Row >= 1)
			{
				OutCenters.Add(FVector(X, Y, Row * Dims.Rise)); // on the tread; the grid lifts the receiver by its ReceiverHeightMeters
				OutRows.Add(Row);
			}
		}
	}
}

double AmphitheatreGeometry::PolygonArea(const FAmphitheatreDimensions& Dims, int32 Segments, double Offset)
{
	// Polygon with vertices (a cos t, b sin t) at equal parametric steps: n/2 * sin(2 pi / n) * a * b.
	return 0.5 * Segments * FMath::Sin(2.0 * UE_DOUBLE_PI / Segments) * (Dims.A + Offset) * (Dims.B + Offset);
}

AAmphitheatreActor::AAmphitheatreActor()
	: StoneMaterialName(TEXT("Stone"))
	, AudienceMaterialName(TEXT("Audience"))
	, VelariumMaterialName(TEXT("Absorber"))
	, GridId(TEXT("Cavea"))
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);

	ArenaFloor = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ArenaFloor"));
	ArenaFloor->SetupAttachment(RootComponent);
	SetupMeshComponent(ArenaFloor);
	Cavea = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Cavea"));
	Cavea->SetupAttachment(RootComponent);
	SetupMeshComponent(Cavea);
	Velarium = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Velarium"));
	Velarium->SetupAttachment(RootComponent);
	SetupMeshComponent(Velarium);
}

const UAmphitheatrePositionsAsset* AAmphitheatreActor::ResolvePositions() const
{
	if (Positions)
	{
		return Positions;
	}
	return LoadObject<UAmphitheatrePositionsAsset>(nullptr, PositionsAssetPath);
}

FAmphitheatreDimensions AAmphitheatreActor::GetDimensions() const
{
	FAmphitheatreDimensions Dims;
	if (const UAmphitheatrePositionsAsset* Asset = ResolvePositions())
	{
		Dims.A = Asset->ArenaSemiAxesMeters.X;
		Dims.B = Asset->ArenaSemiAxesMeters.Y;
		Dims.Rows = FMath::Max(1, Asset->NumRows);
		Dims.Depth = Asset->RowDepthMeters;
		Dims.Rise = Asset->RowRiseMeters;
		Dims.GridStep = FMath::Max(0.25f, Asset->GridSpacingMeters);
	}
	return Dims;
}

void AAmphitheatreActor::GetDimensions(double& OutA, double& OutB, int32& OutRows, double& OutDepth, double& OutRise, double& OutGridStep) const
{
	const FAmphitheatreDimensions Dims = GetDimensions();
	OutA = Dims.A;
	OutB = Dims.B;
	OutRows = Dims.Rows;
	OutDepth = Dims.Depth;
	OutRise = Dims.Rise;
	OutGridStep = Dims.GridStep;
}

void AAmphitheatreActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void AAmphitheatreActor::BeginPlay()
{
	Super::BeginPlay();
	// The PIE copy of a native actor does not rerun OnConstruction.
	Rebuild();
}

void AAmphitheatreActor::Rebuild()
{
	BuildFloor();
	BuildCavea();
	BuildVelarium();
	ApplyTags();
}

void AAmphitheatreActor::SetAudience(bool bEnabled)
{
	bAudience = bEnabled;
	ApplyTags();
}

void AAmphitheatreActor::SetVelarium(bool bEnabled)
{
	bVelarium = bEnabled;
	BuildVelarium();
	ApplyTags();
}

void AAmphitheatreActor::BuildFloor()
{
	UE::Geometry::FDynamicMesh3 Mesh;
	AmphitheatreGeometry::BuildFloorMesh(GetDimensions(), FMath::Clamp(SegmentsAround, 16, 512), Mesh);
	ArenaFloor->SetMesh(MoveTemp(Mesh));
	if (StoneRenderMaterial)
	{
		ArenaFloor->SetMaterial(0, StoneRenderMaterial);
	}
}

void AAmphitheatreActor::BuildCavea()
{
	UE::Geometry::FDynamicMesh3 Mesh;
	AmphitheatreGeometry::BuildCaveaMesh(GetDimensions(), FMath::Clamp(SegmentsAround, 16, 512), Mesh);
	Cavea->SetMesh(MoveTemp(Mesh));
	if (StoneRenderMaterial)
	{
		Cavea->SetMaterial(0, StoneRenderMaterial);
	}
}

void AAmphitheatreActor::BuildVelarium()
{
	UE::Geometry::FDynamicMesh3 Mesh;
	if (bVelarium)
	{
		AmphitheatreGeometry::BuildVelariumMesh(GetDimensions(), FMath::Clamp(SegmentsAround, 16, 512), VelariumHeightMeters, Mesh);
	}
	Velarium->SetMesh(MoveTemp(Mesh));
	Velarium->SetVisibility(bVelarium);
	Velarium->SetCastShadow(false); // the cloth would darken the rows and the heatmap under it
	if (VelariumRenderMaterial)
	{
		Velarium->SetMaterial(0, VelariumRenderMaterial);
	}
}

void AAmphitheatreActor::ApplyTags()
{
	const FName Acoustic(TEXT("Acoustic"));
	auto MatTag = [](FName Name) { return FName(*FString::Printf(TEXT("AcMat:%s"), *Name.ToString())); };
	ArenaFloor->ComponentTags = { Acoustic, MatTag(StoneMaterialName) };
	Cavea->ComponentTags = { Acoustic, MatTag(bAudience ? AudienceMaterialName : StoneMaterialName) };
	if (bVelarium)
	{
		Velarium->ComponentTags = { Acoustic, MatTag(VelariumMaterialName) };
	}
	else
	{
		Velarium->ComponentTags.Reset();
	}
}

int32 AAmphitheatreActor::RowAtPlanPoint(double X, double Y) const
{
	return AmphitheatreGeometry::RowAtPlanPoint(GetDimensions(), X, Y);
}

int32 AAmphitheatreActor::ApplyGrid(AReceiverGrid* Grid) const
{
	if (!Grid)
	{
		return 0;
	}
	const FAmphitheatreDimensions Dims = GetDimensions();
	TArray<FVector> Centers;
	TArray<int32> RowOfTile;
	AmphitheatreGeometry::MakeGridPoints(Dims, Centers, RowOfTile);
	// Grid actor at the arena centre: its local frame equals this actor's (both placed at the origin by the content script).
	Grid->SetActorLocation(GetActorLocation());
	Grid->SetCustomTiles(Centers, RowOfTile, static_cast<float>(Dims.GridStep));
	UE_LOG(LogAcoustics, Log, TEXT("Amphitheatre: grid %s gets %d cavea points (plan step %.1f m, %d rows)"), *Grid->GridId.ToString(), Centers.Num(), Dims.GridStep, Dims.Rows);
	return Centers.Num();
}

void AAmphitheatreActor::ApplyGridInWorld()
{
	for (AReceiverGrid* Grid : AReceiverGrid::FindAll(GetWorld()))
	{
		if (Grid->GridId == GridId)
		{
			ApplyGrid(Grid);
		}
	}
}

AAmphitheatreActor* AAmphitheatreActor::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AAmphitheatreActor> It(World);
	return It ? *It : nullptr;
}

namespace
{
	void SetToggle(const TArray<FString>& Args, UWorld* World, bool bVelariumToggle)
	{
		AAmphitheatreActor* Actor = AAmphitheatreActor::Find(World);
		if (!Actor)
		{
			UE_LOG(LogAcoustics, Warning, TEXT("Amphitheatre: no AmphitheatreActor in this world"));
			return;
		}
		const bool bCurrent = bVelariumToggle ? Actor->bVelarium : Actor->bAudience;
		const bool bValue = Args.Num() > 0 ? Args[0].ToBool() : !bCurrent;
		bVelariumToggle ? Actor->SetVelarium(bValue) : Actor->SetAudience(bValue);
		UE_LOG(LogAcoustics, Log, TEXT("Amphitheatre: %s %s (run the solver again)"), bVelariumToggle ? TEXT("velarium") : TEXT("audience"), bValue ? TEXT("on") : TEXT("off"));
	}

	FAutoConsoleCommandWithWorldAndArgs AudienceCommand(TEXT("Acoustics.Amphitheatre.Audience"), TEXT("Audience on the cavea rows (alpha 0.7): 1 | 0 | toggle"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { SetToggle(Args, World, false); }));
	FAutoConsoleCommandWithWorldAndArgs VelariumCommand(TEXT("Acoustics.Amphitheatre.Velarium"), TEXT("Absorbing velarium over the cavea: 1 | 0 | toggle"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { SetToggle(Args, World, true); }));
}
