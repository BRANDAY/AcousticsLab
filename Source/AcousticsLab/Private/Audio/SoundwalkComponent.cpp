#include "Audio/SoundwalkComponent.h"

#include "AcousticsConstants.h"
#include "AcousticsCore.h"
#include "Audio/AuralizationComponent.h"
#include "Audio/SoundwalkAmbientSource.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Levels/AmphitheatreActor.h"
#include "Levels/AmphitheatrePositionsAsset.h"
#include "Levels/LevelSetupActor.h"
#include "Results/SimulationResult.h"
#include "Simulation/AcousticSimulationManager.h"

USoundwalkComponent::USoundwalkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USoundwalkComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickInterval(UpdateIntervalSeconds);
	SetAmbientEnabled(bAmbientEnabled);
	UE_LOG(LogAcoustics, Log, TEXT("Soundwalk: started (update %.2f s, hysteresis %.2f m, %d ambient sources)"), UpdateIntervalSeconds, HysteresisMeters,
		ASoundwalkAmbientSource::FindAll(GetWorld()).Num());
}

void USoundwalkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (ASoundwalkAmbientSource* Source : ASoundwalkAmbientSource::FindAll(GetWorld()))
	{
		Source->SetPlaying(false);
	}
	Super::EndPlay(EndPlayReason);
}

void USoundwalkComponent::SetAmbientEnabled(bool bEnabled)
{
	bAmbientEnabled = bEnabled;
	for (ASoundwalkAmbientSource* Source : ASoundwalkAmbientSource::FindAll(GetWorld()))
	{
		Source->SetPlaying(bEnabled);
	}
}

void USoundwalkComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Update();
}

FVector3d USoundwalkComponent::GetListenerMeters() const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Controller && Controller->PlayerCameraManager)
	{
		return FVector3d(Controller->PlayerCameraManager->GetCameraLocation()) / Acoustics::CentimetersPerMeter;
	}
	const AActor* Owner = GetOwner();
	return Owner ? FVector3d(Owner->GetActorLocation()) / Acoustics::CentimetersPerMeter : FVector3d::ZeroVector;
}

int32 USoundwalkComponent::FindNearest(TConstArrayView<FVector3d> Positions, const FVector3d& Point, double& OutDistance)
{
	int32 Best = INDEX_NONE;
	double BestSquared = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		const double Squared = FVector3d::DistSquared(Positions[Index], Point);
		if (Squared < BestSquared)
		{
			BestSquared = Squared;
			Best = Index;
		}
	}
	OutDistance = Best == INDEX_NONE ? 0.0 : FMath::Sqrt(BestSquared);
	return Best;
}

void USoundwalkComponent::ResetFallenWalker()
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	ACharacter* Walker = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
	if (!Walker || Walker->GetActorLocation().Z >= FallResetHeightMeters * Acoustics::CentimetersPerMeter)
	{
		return;
	}
	FTransform Start;
	if (ComputeStartTransform(GetWorld(), Walker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), Start))
	{
		Walker->GetCharacterMovement()->StopMovementImmediately();
		Walker->TeleportTo(Start.GetLocation(), Start.Rotator());
		Controller->SetControlRotation(Start.Rotator());
		++NumRespawns;
		UE_LOG(LogAcoustics, Log, TEXT("Soundwalk: the walker left the cavea over the top; back on the start seat (%d)"), NumRespawns);
	}
}

void USoundwalkComponent::Update()
{
	UWorld* World = GetWorld();
	ResetFallenWalker();
	const FVector3d Listener = GetListenerMeters();
	ListenerMeters = FVector(Listener);
	if (const AAmphitheatreActor* Amphitheatre = AAmphitheatreActor::Find(World))
	{
		const FVector3d Local = Listener - FVector3d(Amphitheatre->GetActorLocation()) / Acoustics::CentimetersPerMeter;
		ListenerRow = Amphitheatre->RowAtPlanPoint(Local.X, Local.Y);
	}

	AAcousticSimulationManager* Manager = AAcousticSimulationManager::Find(World);
	UAuralizationComponent* Auralization = Manager ? Manager->Auralization.Get() : nullptr;
	const TSharedPtr<const FSimulationResult> Result = Manager ? Manager->GetLastResultShared() : nullptr;
	if (!Manager || !Auralization)
	{
		WaitingReason = TEXT("no simulation manager in this level");
		return;
	}
	if (!Result.IsValid() || !Result->bSuccess)
	{
		WaitingReason = Manager->IsRunning() ? FString::Printf(TEXT("simulation running (%.0f %%), the walk starts with its result"), Manager->Progress * 100.0f)
			: TEXT("no result yet: Run the simulation (Tab: panel)");
		return;
	}
	WaitingReason.Reset();

	if (Manager->GetRunCount() != SeenRunCount)
	{
		// A new result: the candidate cells are its receivers (point receivers and grid tiles).
		SeenRunCount = Manager->GetRunCount();
		CandidatePositions.Reset();
		CandidateIndices.Reset();
		for (int32 Index = 0; Index < Result->Receivers.Num(); ++Index)
		{
			const bool bPoint = Manager->ReceiverBindings.IsValidIndex(Index) && Manager->ReceiverBindings[Index].Kind == EReceiverBindingKind::Point;
			if (bPoint && !bIncludePointReceivers)
			{
				continue;
			}
			CandidatePositions.Add(Result->Receivers[Index].Position);
			CandidateIndices.Add(Index);
		}
		NearestCell = INDEX_NONE;
		CurrentReceiver = INDEX_NONE;
		const EAuralizationVariant Wet = UAuralizationComponent::VariantForSolver(Result->SolverName);
		if (!bVariantChosen && Wet != EAuralizationVariant::None && Auralization->Variant == EAuralizationVariant::Dry)
		{
			// The walk is about the room: start with the convolved clip (1 still switches to Dry).
			bVariantChosen = Auralization->SetVariantByKey(static_cast<int32>(Wet));
		}
		UE_LOG(LogAcoustics, Log, TEXT("Soundwalk: result of %s with %d candidate cells"), *Result->SolverName, CandidateIndices.Num());
	}

	double Distance = 0.0;
	const int32 Nearest = FindNearest(CandidatePositions, Listener, Distance);
	if (Nearest == INDEX_NONE)
	{
		WaitingReason = TEXT("the result has no receivers");
		return;
	}
	const int32 NearestIndex = CandidateIndices[Nearest];
	if (NearestIndex != NearestCell)
	{
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		const int32 CurrentPosition = CandidateIndices.IndexOfByKey(CurrentReceiver);
		const double CurrentDistance = CurrentPosition != INDEX_NONE ? FVector3d::Dist(CandidatePositions[CurrentPosition], Listener) : TNumericLimits<double>::Max();
		const bool bFarEnough = CurrentReceiver == INDEX_NONE || CurrentDistance - Distance > HysteresisMeters;
		if (bFarEnough && Now - LastSwitchSeconds >= MinSwitchIntervalSeconds)
		{
			NearestCell = NearestIndex;
			LastSwitchSeconds = Now;
			if (Auralization->SelectReceiver(NearestIndex)) // synthesizes the cell's response on first use, no solver run
			{
				CurrentReceiver = NearestIndex;
				CurrentReceiverName = Result->Receivers[NearestIndex].Name;
				++NumSwitches;
				if (!Auralization->IsPlaying() && Auralization->HasImpulseResponse())
				{
					Auralization->Play();
				}
			}
		}
	}
	const int32 CurrentPosition = CandidateIndices.IndexOfByKey(CurrentReceiver);
	DistanceMeters = CurrentPosition != INDEX_NONE ? static_cast<float>(FVector3d::Dist(CandidatePositions[CurrentPosition], Listener)) : 0.0f;
}

FString USoundwalkComponent::DescribeState() const
{
	FString Row;
	if (ListenerRow > 0)
	{
		Row = FString::Printf(TEXT("row %d"), ListenerRow);
	}
	else
	{
		Row = ListenerRow == 0 ? TEXT("arena") : (ListenerRow < 0 ? TEXT("outside the cavea") : TEXT("-"));
	}
	FString Text = FString::Printf(TEXT("SOUNDWALK   listener (%.1f, %.1f, %.1f) m, %s"), ListenerMeters.X, ListenerMeters.Y, ListenerMeters.Z, *Row);
	if (!WaitingReason.IsEmpty())
	{
		return Text + TEXT("   |   ") + WaitingReason;
	}
	const AAcousticSimulationManager* Manager = AAcousticSimulationManager::Find(GetWorld());
	const FSimulationResult* Result = Manager ? Manager->GetLastResult() : nullptr;
	FString Metrics;
	if (Result && Result->Receivers.IsValidIndex(CurrentReceiver))
	{
		const FBandMetrics& Band = Result->Receivers[CurrentReceiver].Metrics.Band(0);
		Metrics = FString::Printf(TEXT(", C80 %.1f dB, T30 %s"), Band.C80, Band.bT30Valid ? *FString::Printf(TEXT("%.2f s"), Band.T30) : TEXT("n/a"));
	}
	const int32 NumAmbient = ASoundwalkAmbientSource::FindAll(GetWorld()).Num();
	Text += FString::Printf(TEXT("   |   IR of %s (%.1f m away%s), %d switches   |   ambient sources: %s"),
		CurrentReceiver == INDEX_NONE ? TEXT("-") : *CurrentReceiverName, DistanceMeters, *Metrics, NumSwitches,
		NumAmbient == 0 ? TEXT("none in this level") : (bAmbientEnabled ? *FString::Printf(TEXT("%d on"), NumAmbient) : TEXT("off")));
	return Text;
}

bool USoundwalkComponent::ComputeStartTransform(UWorld* World, float CapsuleHalfHeightCm, FTransform& OutTransform)
{
	const ALevelSetupActor* Setup = ALevelSetupActor::Find(World);
	const UAmphitheatrePositionsAsset* Positions = Setup ? Setup->Positions.Get() : nullptr;
	if (!Positions)
	{
		Positions = LoadObject<UAmphitheatrePositionsAsset>(nullptr, TEXT("/Game/Acoustics/DA_AmphitheatrePositions.DA_AmphitheatrePositions"));
	}
	if (!Positions || !AAmphitheatreActor::Find(World))
	{
		return false;
	}
	const FAmphitheatreSeat* Seat = Positions->FindSeat(TEXT("R2"));
	FAmphitheatreSeat Fallback;
	Fallback.Row = FMath::Max(1, Positions->NumRows / 2);
	const FAmphitheatreSeat& Chosen = Seat ? *Seat : Fallback;
	const FVector EarMeters = Positions->GetSeatPositionMeters(Chosen);
	const double TreadZ = EarMeters.Z - Positions->SeatedEarHeightMeters;
	const double Cm = Acoustics::CentimetersPerMeter;
	const FVector Location(EarMeters.X * Cm, EarMeters.Y * Cm, TreadZ * Cm + CapsuleHalfHeightCm + 5.0);
	const FVector Centre = Positions->CenterMeters * Cm;
	const float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Centre.Y - Location.Y, Centre.X - Location.X)));
	OutTransform = FTransform(FRotator(0.f, Yaw, 0.f), Location);
	return true;
}
