#include "Framework/AcousticsExplorerPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/FloatingPawnMovement.h"

AAcousticsExplorerPawn::AAcousticsExplorerPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(30.f);
	Collision->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	RootComponent = Collision;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Collision);
	Camera->bUsePawnControlRotation = true;

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->MaxSpeed = 600.f; // 6 m/s
	Movement->Acceleration = 2400.f;
	Movement->Deceleration = 4800.f;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = true;
}

void AAcousticsExplorerPawn::Move(const FVector2D& Axis)
{
	const FRotator ControlRotation = GetControlRotation();
	const FVector Forward = ControlRotation.Vector();
	const FVector Right = FRotationMatrix(FRotator(0.f, ControlRotation.Yaw, 0.f)).GetUnitAxis(EAxis::Y);
	AddMovementInput(Forward, Axis.Y);
	AddMovementInput(Right, Axis.X);
}

void AAcousticsExplorerPawn::MoveUp(float Axis)
{
	AddMovementInput(FVector::UpVector, Axis);
}

void AAcousticsExplorerPawn::Look(const FVector2D& Axis)
{
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}
