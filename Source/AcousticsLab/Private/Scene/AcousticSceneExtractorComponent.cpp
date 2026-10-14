#include "Scene/AcousticSceneExtractorComponent.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/AcousticScene.h"
#include "Scene/AcousticMaterialAsset.h"
#include "Scene/AcousticMaterialPresets.h"
#include "StaticMeshResources.h"
#include "UDynamicMesh.h"

FString FAcousticExtractionStats::ToString() const
{
	FString Result = FString::Printf(
		TEXT("%d triangles (%d degenerate skipped), %.3f m^2, %d materials; %d actors, %d static mesh components (%d instances), %d dynamic mesh components, %d skipped; bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f) m; %.1f ms"),
		NumTriangles, NumDegenerateTriangles, TotalAreaM2, NumMaterials, NumActors, NumStaticMeshComponents, NumInstances, NumDynamicMeshComponents, NumSkippedComponents,
		BoundsMinMeters.X, BoundsMinMeters.Y, BoundsMinMeters.Z, BoundsMaxMeters.X, BoundsMaxMeters.Y, BoundsMaxMeters.Z, ExtractionMilliseconds);
	for (const FString& Warning : Warnings)
	{
		Result += TEXT("\n  warning: ") + Warning;
	}
	return Result;
}

UAcousticSceneExtractorComponent::UAcousticSceneExtractorComponent()
	: AcousticTag(TEXT("Acoustic"))
	, MaterialTagPrefix(TEXT("AcMat:"))
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UAcousticSceneExtractorComponent::IsAcousticComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return false;
	}
	if (Component->ComponentHasTag(AcousticTag))
	{
		return true;
	}
	const AActor* Owner = Component->GetOwner();
	return Owner && Owner->ActorHasTag(AcousticTag);
}

bool UAcousticSceneExtractorComponent::ExtractScene(FAcousticScene& OutScene, FAcousticExtractionStats& OutStats) const
{
	return ExtractFromWorld(GetWorld(), OutScene, OutStats);
}

static FString DescribeComponent(const UPrimitiveComponent* Component)
{
	const AActor* Owner = Component ? Component->GetOwner() : nullptr;
	FString OwnerName = Owner ? Owner->GetName() : TEXT("<no actor>");
#if WITH_EDITOR
	if (Owner)
	{
		OwnerName = Owner->GetActorLabel();
	}
#endif
	return FString::Printf(TEXT("%s.%s"), *OwnerName, Component ? *Component->GetName() : TEXT("<null>"));
}

bool UAcousticSceneExtractorComponent::ExtractFromWorld(UWorld* World, FAcousticScene& OutScene, FAcousticExtractionStats& OutStats) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UAcousticSceneExtractorComponent::ExtractFromWorld);
	const double StartSeconds = FPlatformTime::Seconds();
	OutScene.Reset();
	OutStats = FAcousticExtractionStats();
	if (!World)
	{
		OutStats.Warnings.Add(TEXT("no world to extract"));
		return false;
	}

	// Material 0 is the configured default.
	FAcousticMaterialProps DefaultProps;
	if (DefaultMaterial)
	{
		DefaultProps = DefaultMaterial->ToProps();
	}
	else if (const AcousticMaterialPresets::FPreset* Concrete = AcousticMaterialPresets::Find(AcousticMaterialPresets::DefaultName()))
	{
		DefaultProps = AcousticMaterialPresets::ToProps(*Concrete);
	}
	OutScene.Materials[0] = DefaultProps;
	TMap<FName, int32> MaterialIds;
	MaterialIds.Add(FName(*DefaultProps.Name), 0);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		bool bActorCounted = false;
		TInlineComponentArray<UPrimitiveComponent*> Primitives;
		Actor->GetComponents(Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (!IsAcousticComponent(Primitive))
			{
				continue;
			}
			if (!bActorCounted)
			{
				++OutStats.NumActors;
				bActorCounted = true;
			}

			if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Primitive))
			{
				const UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh();
				if (!Mesh)
				{
					++OutStats.NumSkippedComponents;
					OutStats.Warnings.Add(FString::Printf(TEXT("%s: no static mesh assigned"), *DescribeComponent(Primitive)));
					continue;
				}
				const int32 MaterialId = ResolveMaterialId(Primitive, Mesh, OutScene, MaterialIds, OutStats);
				++OutStats.NumStaticMeshComponents;
				if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(StaticMeshComponent))
				{
					const int32 InstanceCount = Instanced->GetInstanceCount();
					for (int32 Instance = 0; Instance < InstanceCount; ++Instance)
					{
						FTransform InstanceToWorld;
						if (Instanced->GetInstanceTransform(Instance, InstanceToWorld, true))
						{
							AppendStaticMesh(Mesh, InstanceToWorld, MaterialId, OutScene, OutStats);
							++OutStats.NumInstances;
						}
					}
				}
				else
				{
					AppendStaticMesh(Mesh, StaticMeshComponent->GetComponentTransform(), MaterialId, OutScene, OutStats);
				}
			}
			else if (UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Primitive))
			{
				if (!bIncludeDynamicMeshes)
				{
					++OutStats.NumSkippedComponents;
					continue;
				}
				const int32 MaterialId = ResolveMaterialId(Primitive, nullptr, OutScene, MaterialIds, OutStats);
				++OutStats.NumDynamicMeshComponents;
				AppendDynamicMesh(DynamicMeshComponent->GetDynamicMesh(), DynamicMeshComponent->GetComponentTransform(), MaterialId, OutScene, OutStats);
			}
			else
			{
				++OutStats.NumSkippedComponents;
				OutStats.Warnings.Add(FString::Printf(TEXT("%s: %s is neither a static nor a dynamic mesh component"), *DescribeComponent(Primitive), *Primitive->GetClass()->GetName()));
			}
		}
	}

	if (bBuildBVH && OutScene.NumTriangles() > 0)
	{
		OutScene.BuildBVH();
	}

	OutStats.NumTriangles = OutScene.NumTriangles();
	OutStats.TotalAreaM2 = OutScene.TotalArea();
	OutStats.NumMaterials = OutScene.Materials.Num();
	const FBox3d Bounds = OutScene.ComputeBounds();
	if (Bounds.IsValid)
	{
		OutStats.BoundsMinMeters = FVector(Bounds.Min);
		OutStats.BoundsMaxMeters = FVector(Bounds.Max);
	}
	OutStats.ExtractionMilliseconds = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	return OutScene.NumTriangles() > 0;
}

int32 UAcousticSceneExtractorComponent::AppendUETriangle(const FVector& P0, const FVector& P1, const FVector& P2, bool bMirrored, int32 MaterialId, FAcousticScene& Scene, FAcousticExtractionStats& Stats)
{
	const double Scale = 1.0 / Acoustics::CentimetersPerMeter;
	const FVector3d A = FVector3d(P0) * Scale;
	const FVector3d B = FVector3d(P1) * Scale;
	const FVector3d C = FVector3d(P2) * Scale;
	// UE front normal is (C - A) x (B - A); the core uses (V1 - V0) x (V2 - V0), hence the swap of B and C.
	// A mirroring transform reverses the world-space winding once more, so it cancels the swap.
	const FAcousticTriangle Triangle = bMirrored ? FAcousticTriangle(A, B, C, MaterialId) : FAcousticTriangle(A, C, B, MaterialId);
	if (Scene.AddTriangle(Triangle) == INDEX_NONE)
	{
		++Stats.NumDegenerateTriangles;
		return 0;
	}
	return 1;
}

int32 UAcousticSceneExtractorComponent::AppendStaticMesh(const UStaticMesh* Mesh, const FTransform& ToWorld, int32 MaterialId, FAcousticScene& Scene, FAcousticExtractionStats& Stats)
{
	if (!Mesh)
	{
		return 0;
	}
	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		++Stats.NumSkippedComponents;
		Stats.Warnings.Add(FString::Printf(TEXT("%s: no render data (mesh not built?)"), *Mesh->GetName()));
		return 0;
	}
	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
	const FRawStaticIndexBuffer& Indices = LOD.IndexBuffer;
	const uint32 NumVertices = Positions.GetNumVertices();
	const int32 NumIndices = Indices.GetNumIndices();
	if (NumVertices == 0 || NumIndices < 3)
	{
		++Stats.NumSkippedComponents;
		Stats.Warnings.Add(FString::Printf(TEXT("%s: LOD0 is empty"), *Mesh->GetName()));
		return 0;
	}
	if (!Positions.GetAllowCPUAccess() || !Indices.GetAllowCPUAccess())
	{
		++Stats.NumSkippedComponents;
		Stats.Warnings.Add(FString::Printf(TEXT("%s: vertex/index buffers are not CPU accessible, enable \"Allow CPU Access\" on the mesh"), *Mesh->GetName()));
		return 0;
	}

	const bool bMirrored = ToWorld.GetDeterminant() < 0.0;
	int32 Added = 0;
	for (int32 Triangle = 0; Triangle + 2 < NumIndices; Triangle += 3)
	{
		const uint32 I0 = Indices.GetIndex(Triangle);
		const uint32 I1 = Indices.GetIndex(Triangle + 1);
		const uint32 I2 = Indices.GetIndex(Triangle + 2);
		if (I0 >= NumVertices || I1 >= NumVertices || I2 >= NumVertices)
		{
			++Stats.NumDegenerateTriangles;
			continue;
		}
		const FVector P0 = ToWorld.TransformPosition(FVector(Positions.VertexPosition(I0)));
		const FVector P1 = ToWorld.TransformPosition(FVector(Positions.VertexPosition(I1)));
		const FVector P2 = ToWorld.TransformPosition(FVector(Positions.VertexPosition(I2)));
		Added += AppendUETriangle(P0, P1, P2, bMirrored, MaterialId, Scene, Stats);
	}
	return Added;
}

int32 UAcousticSceneExtractorComponent::AppendDynamicMesh(const UDynamicMesh* Mesh, const FTransform& ToWorld, int32 MaterialId, FAcousticScene& Scene, FAcousticExtractionStats& Stats)
{
	if (!Mesh)
	{
		++Stats.NumSkippedComponents;
		Stats.Warnings.Add(TEXT("dynamic mesh component without a mesh object"));
		return 0;
	}
	const bool bMirrored = ToWorld.GetDeterminant() < 0.0;
	int32 Added = 0;
	Mesh->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& DynamicMesh)
	{
		for (const int32 TriangleId : DynamicMesh.TriangleIndicesItr())
		{
			const UE::Geometry::FIndex3i Triangle = DynamicMesh.GetTriangle(TriangleId);
			const FVector P0 = ToWorld.TransformPosition(DynamicMesh.GetVertex(Triangle.A));
			const FVector P1 = ToWorld.TransformPosition(DynamicMesh.GetVertex(Triangle.B));
			const FVector P2 = ToWorld.TransformPosition(DynamicMesh.GetVertex(Triangle.C));
			Added += AppendUETriangle(P0, P1, P2, bMirrored, MaterialId, Scene, Stats);
		}
	});
	return Added;
}

bool UAcousticSceneExtractorComponent::FindMaterialTag(const TArray<FName>& Tags, FName& OutName) const
{
	if (MaterialTagPrefix.IsEmpty())
	{
		return false;
	}
	for (const FName& Tag : Tags)
	{
		const FString TagString = Tag.ToString();
		if (TagString.StartsWith(MaterialTagPrefix, ESearchCase::IgnoreCase))
		{
			const FString Value = TagString.Mid(MaterialTagPrefix.Len()).TrimStartAndEnd();
			if (!Value.IsEmpty())
			{
				OutName = FName(*Value);
				return true;
			}
		}
	}
	return false;
}

const UAcousticMaterialAsset* UAcousticSceneExtractorComponent::FindMaterialAsset(FName Name) const
{
	if (DefaultMaterial && DefaultMaterial->MaterialName == Name)
	{
		return DefaultMaterial;
	}
	for (const TObjectPtr<UAcousticMaterialAsset>& Asset : MaterialLibrary)
	{
		if (Asset && Asset->MaterialName == Name)
		{
			return Asset;
		}
	}
	return nullptr;
}

int32 UAcousticSceneExtractorComponent::ResolveMaterialId(const UPrimitiveComponent* Component, const UStaticMesh* Mesh, FAcousticScene& Scene, TMap<FName, int32>& MaterialIds, FAcousticExtractionStats& Stats) const
{
	FName Name;
	const AActor* Owner = Component->GetOwner();
	const bool bTagged = FindMaterialTag(Component->ComponentTags, Name) || (Owner && FindMaterialTag(Owner->Tags, Name));
	if (!bTagged)
	{
		const TObjectPtr<UAcousticMaterialAsset>* Bound = Mesh ? MaterialByMeshName.Find(Mesh->GetFName()) : nullptr;
		if (!Bound && Owner)
		{
			Bound = MaterialByMeshName.Find(Owner->GetFName());
#if WITH_EDITOR
			if (!Bound)
			{
				Bound = MaterialByMeshName.Find(FName(*Owner->GetActorLabel()));
			}
#endif
		}
		if (!Bound || !*Bound)
		{
			return 0;
		}
		Name = (*Bound)->MaterialName;
	}
	if (Name.IsNone())
	{
		return 0;
	}
	if (const int32* Existing = MaterialIds.Find(Name))
	{
		return *Existing;
	}

	FAcousticMaterialProps Props;
	if (const UAcousticMaterialAsset* Asset = FindMaterialAsset(Name))
	{
		Props = Asset->ToProps();
	}
	else if (const AcousticMaterialPresets::FPreset* Preset = AcousticMaterialPresets::Find(Name))
	{
		Props = AcousticMaterialPresets::ToProps(*Preset);
	}
	else
	{
		Stats.Warnings.Add(FString::Printf(TEXT("%s: material \"%s\" has neither an asset nor a preset, using the default \"%s\""), *DescribeComponent(Component), *Name.ToString(), *Scene.Materials[0].Name));
		MaterialIds.Add(Name, 0);
		return 0;
	}
	const int32 MaterialId = Scene.AddMaterial(Props);
	MaterialIds.Add(Name, MaterialId);
	return MaterialId;
}

void UAcousticSceneExtractorComponent::LogExtractedScene()
{
	FAcousticScene Scene;
	ExtractFromWorld(GetWorld(), Scene, LastStats);
	UE_LOG(LogAcoustics, Display, TEXT("%s extracted %s\n%s"), *GetPathName(), *LastStats.ToString(), *Scene.DescribeStats());
	if (DebugDrawSeconds > 0.0f)
	{
		DrawScene(GetWorld(), Scene, DebugDrawSeconds);
	}
}

void UAcousticSceneExtractorComponent::DrawScene(const UWorld* World, const FAcousticScene& Scene, float Duration)
{
	if (!World)
	{
		return;
	}
	const double Cm = Acoustics::CentimetersPerMeter;
	for (const FAcousticTriangle& Triangle : Scene.Triangles)
	{
		const FColor Color = FColor::MakeRandomSeededColor(Triangle.MaterialId * 7919 + 17);
		const FVector A = FVector(Triangle.V0 * Cm);
		const FVector B = FVector(Triangle.V1 * Cm);
		const FVector C = FVector(Triangle.V2 * Cm);
		DrawDebugLine(World, A, B, Color, false, Duration, 0, 2.0f);
		DrawDebugLine(World, B, C, Color, false, Duration, 0, 2.0f);
		DrawDebugLine(World, C, A, Color, false, Duration, 0, 2.0f);
		const FVector Centroid = FVector(Triangle.Centroid() * Cm);
		DrawDebugLine(World, Centroid, Centroid + FVector(Triangle.N) * 0.5 * Cm, FColor::Cyan, false, Duration, 0, 3.0f);
	}
}
