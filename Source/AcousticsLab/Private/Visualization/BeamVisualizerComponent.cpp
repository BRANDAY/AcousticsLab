#include "Visualization/BeamVisualizerComponent.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Solvers/BeamTracer.h"
#include "Visualization/RayPathVisualizerComponent.h"

using namespace UE::Geometry;

UBeamVisualizerComponent::UBeamVisualizerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBeamVisualizerComponent::OnRegister()
{
	Super::OnRegister();
}

void UBeamVisualizerComponent::OnUnregister()
{
	DestroyMeshes();
	Super::OnUnregister();
}

void UBeamVisualizerComponent::DestroyMeshes()
{
	for (TObjectPtr<UDynamicMeshComponent>& Mesh : MeshPerOrder)
	{
		if (Mesh && !Mesh->IsBeingDestroyed())
		{
			Mesh->DestroyComponent();
		}
	}
	MeshPerOrder.Reset();
}

UMaterialInterface* UBeamVisualizerComponent::ResolveMaterial()
{
	if (ResolvedMaterial)
	{
		return ResolvedMaterial;
	}
	ResolvedMaterial = FrustumMaterial ? FrustumMaterial.Get() : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Acoustics/Materials/M_BeamFrustum.M_BeamFrustum"));
	if (!ResolvedMaterial)
	{
		ResolvedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		if (!bWarnedMaterial)
		{
			bWarnedMaterial = true;
			UE_LOG(LogAcoustics, Warning, TEXT("BeamVisualizer: M_BeamFrustum not found (run Tools/Editor/create_m5_content.py); beams use the engine vertex-color material, opaque"));
		}
	}
	return ResolvedMaterial;
}

UDynamicMeshComponent* UBeamVisualizerComponent::GetOrCreateMesh(int32 Order)
{
	if (!GetOwner() || !GetWorld() || Order < 0)
	{
		return nullptr;
	}
	while (MeshPerOrder.Num() <= Order)
	{
		MeshPerOrder.Add(nullptr);
	}
	if (MeshPerOrder[Order])
	{
		return MeshPerOrder[Order];
	}
	UDynamicMeshComponent* Mesh = NewObject<UDynamicMeshComponent>(GetOwner(), *FString::Printf(TEXT("BeamFrustums%d"), Order), RF_Transient);
	Mesh->SetupAttachment(GetOwner()->GetRootComponent());
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetShadowsEnabled(false);
	Mesh->SetEnableRaytracing(false);
	// Color override "VertexColors" would force the engine's opaque vertex-color material (BaseDynamicMeshSceneProxy.cpp:248);
	// with the mode None the render buffers still take the color overlay (MeshRenderBufferSet.cpp:247) and the translucent
	// material reads it through its VertexColor node.
	Mesh->SetColorOverrideMode(EDynamicMeshComponentColorOverrideMode::None);
	Mesh->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::NoTransform);
	Mesh->bIsEditorOnly = false;
	Mesh->SetHiddenInGame(false);
	if (UMaterialInterface* Material = ResolveMaterial())
	{
		Mesh->SetMaterial(0, Material);
	}
	Mesh->RegisterComponent();
	MeshPerOrder[Order] = Mesh;
	return Mesh;
}

FLinearColor UBeamVisualizerComponent::ColorForOrderAndMedium(int32 Order, int32 Medium)
{
	if (Medium <= 0)
	{
		return URayPathVisualizerComponent::ColorForOrder(Order);
	}
	FLinearColor Color;
	switch (Order)
	{
	case 0: Color = FLinearColor(0.35f, 0.95f, 1.0f); break;
	case 1: Color = FLinearColor(0.15f, 0.55f, 1.0f); break;
	case 2: Color = FLinearColor(0.45f, 0.25f, 1.0f); break;
	default: Color = FLinearColor(0.85f, 0.15f, 0.95f); break;
	}
	if (Medium > 1)
	{
		FLinearColor Hsv = Color.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + 70.0f * (Medium - 1), 360.0f);
		Color = Hsv.HSVToLinearRGB();
	}
	return Color;
}

bool UBeamVisualizerComponent::HasTree() const
{
	return Result.IsValid() && Result->bSuccess && Result->BeamTree.IsValid() && Result->BeamTree->Nodes.Num() > 0;
}

int32 UBeamVisualizerComponent::GetTreeMaxOrder() const
{
	if (!HasTree())
	{
		return -1;
	}
	int32 MaxOrder = -1;
	for (int32 Order = 0; Order < Result->BeamTree->NumNodesPerOrder.Num(); ++Order)
	{
		if (Result->BeamTree->NumNodesPerOrder[Order] > 0)
		{
			MaxOrder = Order;
		}
	}
	return MaxOrder;
}

void UBeamVisualizerComponent::SetResult(const TSharedPtr<const FSimulationResult>& InResult)
{
	Result = InResult;
	Rebuild();
}

void UBeamVisualizerComponent::SetEnabled(bool bInEnabled)
{
	if (bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
		Rebuild();
	}
}

void UBeamVisualizerComponent::SetMaxDrawOrder(int32 InMaxOrder)
{
	const int32 Clamped = FMath::Clamp(InMaxOrder, 0, 10);
	if (MaxDrawOrder != Clamped)
	{
		MaxDrawOrder = Clamped;
		Rebuild();
	}
}

void UBeamVisualizerComponent::SetMinDrawOrder(int32 InMinOrder)
{
	const int32 Clamped = FMath::Clamp(InMinOrder, 0, 10);
	if (MinDrawOrder != Clamped)
	{
		MinDrawOrder = Clamped;
		Rebuild();
	}
}

void UBeamVisualizerComponent::SetOnlyBeamsContainingReceiver(bool bInOnly)
{
	if (bOnlyBeamsContainingReceiver != bInOnly)
	{
		bOnlyBeamsContainingReceiver = bInOnly;
		Rebuild();
	}
}

void UBeamVisualizerComponent::SetReceiverFilter(int32 InReceiverIndex)
{
	const int32 Clamped = FMath::Max<int32>(INDEX_NONE, InReceiverIndex);
	if (ReceiverFilter != Clamped)
	{
		ReceiverFilter = Clamped;
		if (bOnlyBeamsContainingReceiver)
		{
			Rebuild();
		}
	}
}

void UBeamVisualizerComponent::Clear()
{
	NumDrawnBeams = NumMatchingBeams = NumTriangles = 0;
	for (TObjectPtr<UDynamicMeshComponent>& Mesh : MeshPerOrder)
	{
		if (Mesh)
		{
			Mesh->SetMesh(FDynamicMesh3());
			Mesh->SetVisibility(false);
		}
	}
}

namespace
{
	/** Appends the frustum of a beam node: start cross-section (or apex) to end cross-section, vertex colors with alpha. */
	void AppendFrustum(FDynamicMesh3& Mesh, const FBeamNode& Node, const FVector4f& Color, bool bStartCap, int32& OutTriangles)
	{
		const double Scale = Acoustics::CentimetersPerMeter;
		FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();
		auto AddVertex = [&Mesh, &Colors, &Color, Scale](const FVector3d& Point) -> int32
		{
			const int32 Vertex = Mesh.AppendVertex(Point * Scale);
			Mesh.SetVertexColor(Vertex, FVector3f(Color.X, Color.Y, Color.Z));
			return Vertex;
		};
		auto AddTriangle = [&Mesh, &Colors, &Color, &OutTriangles](int32 A, int32 B, int32 C)
		{
			const int32 Triangle = Mesh.AppendTriangle(A, B, C);
			if (Triangle >= 0)
			{
				const int32 E0 = Colors->AppendElement(Color);
				const int32 E1 = Colors->AppendElement(Color);
				const int32 E2 = Colors->AppendElement(Color);
				Colors->SetTriangle(Triangle, FIndex3i(E0, E1, E2));
				++OutTriangles;
			}
		};

		const int32 End[3] = { AddVertex(Node.EndPoints[0]), AddVertex(Node.EndPoints[1]), AddVertex(Node.EndPoints[2]) };
		AddTriangle(End[0], End[1], End[2]);
		const bool bPointStart = Node.StartPoints[0].Equals(Node.StartPoints[1], 1e-9) && Node.StartPoints[1].Equals(Node.StartPoints[2], 1e-9);
		if (bPointStart)
		{
			const int32 Apex = AddVertex(Node.StartPoints[0]);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AddTriangle(Apex, End[(Index + 1) % 3], End[Index]);
			}
			return;
		}
		const int32 Start[3] = { AddVertex(Node.StartPoints[0]), AddVertex(Node.StartPoints[1]), AddVertex(Node.StartPoints[2]) };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const int32 Next = (Index + 1) % 3;
			AddTriangle(Start[Index], End[Next], End[Index]);
			AddTriangle(Start[Index], Start[Next], End[Next]);
		}
		if (bStartCap)
		{
			AddTriangle(Start[0], Start[2], Start[1]);
		}
	}
}

void UBeamVisualizerComponent::Rebuild()
{
	Clear();
	if (!bEnabled || !HasTree() || !GetOwner() || !GetWorld())
	{
		return;
	}
	const FBeamTree& Tree = *Result->BeamTree;
	const int32 TopOrder = FMath::Min(MaxDrawOrder, GetTreeMaxOrder());
	if (TopOrder < 0)
	{
		return;
	}

	// Containment filter: the nodes containing the receiver(s) and all their ancestors.
	TSet<int32> Selected;
	if (bOnlyBeamsContainingReceiver)
	{
		for (int32 ReceiverIndex = 0; ReceiverIndex < Result->Receivers.Num(); ++ReceiverIndex)
		{
			if (ReceiverFilter != INDEX_NONE && ReceiverIndex != ReceiverFilter)
			{
				continue;
			}
			for (int32 NodeIndex : Result->Receivers[ReceiverIndex].ContainingBeamNodes)
			{
				while (NodeIndex != INDEX_NONE && Tree.Nodes.IsValidIndex(NodeIndex))
				{
					bool bAlready = false;
					Selected.Add(NodeIndex, &bAlready);
					if (bAlready)
					{
						break;
					}
					NodeIndex = Tree.Nodes[NodeIndex].Parent;
				}
			}
		}
	}

	TArray<int32> DrawnPerOrder;
	DrawnPerOrder.Init(0, TopOrder + 1);
	TArray<FDynamicMesh3> Meshes;
	Meshes.SetNum(TopOrder + 1);
	for (FDynamicMesh3& Mesh : Meshes)
	{
		Mesh.EnableVertexColors(FVector3f::One());
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnablePrimaryColors();
	}

	for (int32 NodeIndex = 0; NodeIndex < Tree.Nodes.Num(); ++NodeIndex)
	{
		const FBeamNode& Node = Tree.Nodes[NodeIndex];
		if (Node.Order > TopOrder || Node.Order < MinDrawOrder)
		{
			continue;
		}
		if (bOnlyBeamsContainingReceiver && !Selected.Contains(NodeIndex))
		{
			continue;
		}
		if (!bDrawEscapedBeams && Node.HitTriangle == INDEX_NONE)
		{
			continue;
		}
		++NumMatchingBeams;
		if (DrawnPerOrder[Node.Order] >= MaxBeamsPerOrder)
		{
			continue;
		}
		const FLinearColor Base = ColorForOrderAndMedium(Node.Order, Node.Medium);
		float Alpha = bOnlyBeamsContainingReceiver ? HighlightOpacity : Opacity;
		if (Node.bRefracted)
		{
			Alpha = FMath::Min(1.0f, Alpha * RefractedOpacityScale);
		}
		AppendFrustum(Meshes[Node.Order], Node, FVector4f(Base.R, Base.G, Base.B, Alpha), bDrawStartCaps && Node.Order > 0, NumTriangles);
		++DrawnPerOrder[Node.Order];
		++NumDrawnBeams;
	}

	for (int32 Order = 0; Order <= TopOrder; ++Order)
	{
		if (UDynamicMeshComponent* Component = GetOrCreateMesh(Order))
		{
			Component->SetMesh(MoveTemp(Meshes[Order]));
			Component->SetVisibility(DrawnPerOrder[Order] > 0);
		}
	}
	UE_LOG(LogAcoustics, Verbose, TEXT("Beams: %d drawn of %d matching (order <= %d, %d triangles%s)"), NumDrawnBeams, NumMatchingBeams, TopOrder, NumTriangles,
		bOnlyBeamsContainingReceiver ? TEXT(", containing the receiver") : TEXT(""));
}
