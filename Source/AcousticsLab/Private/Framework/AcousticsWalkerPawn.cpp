#include "Framework/AcousticsWalkerPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AAcousticsWalkerPawn::AAcousticsWalkerPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(30.f, 88.f);
	BaseEyeHeight = 72.f; // eyes / ears 1.6 m above the feet

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	Camera->bUsePawnControlRotation = true;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxStepHeight = 85.f; // one riser of the cavea is 75 cm
	Movement->SetWalkableFloorAngle(50.f);
	Movement->JumpZVelocity = 450.f;
	Movement->AirControl = 0.5f;
	Movement->BrakingDecelerationWalking = 2048.f;
}

void AAcousticsWalkerPawn::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void AAcousticsWalkerPawn::Move(const FVector2D& Axis)
{
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}

void AAcousticsWalkerPawn::MoveUp(float Axis)
{
	if (Axis > 0.5f)
	{
		Jump();
	}
}

void AAcousticsWalkerPawn::Look(const FVector2D& Axis)
{
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}
