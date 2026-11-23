#include "RegionTravel/RPGRegionTravelComponent.h"
#include "RegionTravel/RPGRegionGate.h"
#include "UI/Loading/RPGExperienceLoadingScreen.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	FName LevelName(const ARPGRegionGate& Gate)
	{
		return Gate.RegionLevel.IsNull() ? NAME_None :
			FName(UWorld::RemovePIEPrefix(Gate.RegionLevel.ToSoftObjectPath().GetLongPackageName()));
	}

	bool RequestLevel(UWorld* World, FName Name, bool bLoad)
	{
		if (Name.IsNone()) return true;
		ULevelStreaming* Level = UGameplayStatics::GetStreamingLevel(World, Name);
		if (!Level) return false;
		Level->SetShouldBeLoaded(bLoad);
		Level->SetShouldBeVisible(bLoad);
		return true;
	}

	bool IsVisible(UWorld* World, FName Name)
	{
		if (Name.IsNone()) return true;
		const ULevelStreaming* Level = UGameplayStatics::GetStreamingLevel(World, Name);
		return Level && Level->IsLevelVisible();
	}

	FGameplayTagContainer PlayerAbilities()
	{
		return FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability")));
	}
}

URPGRegionTravelComponent::URPGRegionTravelComponent()
{
	SetIsReplicatedByDefault(true);
}

void URPGRegionTravelComponent::BeginTransfer(ARPGRegionGate* Source, ACharacter* Character)
{
	auto* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->HasAuthority() || bServerPending || !IsValid(Character) || PC->GetPawn() != Character
		|| !IsValid(Source) || !IsValid(Source->Destination) || !Source->Trigger->IsOverlappingActor(Character))
		return;

	const FName From = LevelName(*Source);
	const FName To = LevelName(*Source->Destination);
	if (From != GetCurrentRegion() || From == To || !IsVisible(GetWorld(), From)
		|| !RequestLevel(GetWorld(), To, true)) return;

	Serial = Serial == MAX_int32 ? 1 : Serial + 1;
	bServerPending = true;
	bReady = false;
	SourceGate = Source;
	TargetGate = Source->Destination;
	LockedCharacter = Character;
	auto* Movement = Character->GetCharacterMovement();
	PreviousMode = Movement->MovementMode;
	PreviousCustomMode = Movement->CustomMovementMode;
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	BlockedAbilities = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character);
	if (BlockedAbilities.IsValid()) BlockedAbilities->BlockAbilitiesWithTags(PlayerAbilities());
	ServerDeadline = GetWorld()->GetRealTimeSeconds() + 30.0;
	GetWorld()->GetTimerManager().SetTimer(ServerTimer, this, &ThisClass::PollServer, 0.05f, true);
	ClientPrepare(Serial, From, To);
}

FName URPGRegionTravelComponent::GetCurrentRegion() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC && RegionCharacter.IsValid() && PC->GetPawn() == RegionCharacter.Get() ? CurrentRegion : NAME_None;
}

void URPGRegionTravelComponent::ClientPrepare_Implementation(int32 Id, FName Source, FName Destination)
{
	auto* PC = CastChecked<APlayerController>(GetOwner());
	ReleaseClient();
	ClientId = Id;
	ClientSource = Source;
	ClientTarget = Destination;
	Screen = CreateWidget<URPGExperienceLoadingScreen>(PC, URPGExperienceLoadingScreen::StaticClass());
	if (Screen) Screen->AddToPlayerScreen(10000);
	InputBlock = NewObject<UInputComponent>(PC);
	InputBlock->Priority = MAX_int32;
	InputBlock->bBlockInput = true;
	InputBlock->RegisterComponent();
	PC->PushInputComponent(InputBlock);
	PC->SetIgnoreMoveInput(true);
	RequestLevel(GetWorld(), Destination, true);
	UE_LOG(LogTemp, Verbose, TEXT("Region prepare %s id=%d destination=%s screen=%d"),
		*GetPathName(), Id, *Destination.ToString(), Screen && Screen->IsInViewport());
	GetWorld()->GetTimerManager().SetTimer(ClientTimer, this, &ThisClass::PollClient, 0.05f, true);
}

void URPGRegionTravelComponent::PollClient()
{
	if (bAwaitingArrival)
	{
		const APawn* Pawn = CastChecked<APlayerController>(GetOwner())->GetPawn();
		if ((Pawn && FVector::DistSquared(Pawn->GetActorLocation(), ExpectedLocation) < FMath::Square(150.0))
			|| GetWorld()->GetRealTimeSeconds() >= ArrivalDeadline)
		{
			UE_LOG(LogTemp, Verbose, TEXT("Region arrival %s id=%d location=%s"),
				*GetPathName(), ClientId, Pawn ? *Pawn->GetActorLocation().ToString() : TEXT("unpossessed"));
			if (GetNetMode() == NM_Client) RequestLevel(GetWorld(), ClientSource, false);
			ReleaseClient();
		}
	}
	else if (!bReadySent && IsVisible(GetWorld(), ClientTarget))
	{
		bReadySent = true;
		UE_LOG(LogTemp, Verbose, TEXT("Region ready %s id=%d visible=%s"),
			*GetPathName(), ClientId, *ClientTarget.ToString());
		ServerPrepared(ClientId);
	}
}

void URPGRegionTravelComponent::ServerPrepared_Implementation(int32 Id)
{
	if (bServerPending && Serial == Id) bReady = true;
}

void URPGRegionTravelComponent::PollServer()
{
	auto* PC = CastChecked<APlayerController>(GetOwner());
	ACharacter* Character = LockedCharacter.Get();
	if (!IsValid(Character) || PC->GetPawn() != Character || !SourceGate.IsValid() || !TargetGate.IsValid()
		|| !SourceGate->Trigger->IsOverlappingActor(Character) || GetWorld()->GetRealTimeSeconds() >= ServerDeadline)
	{
		FinishServer(false);
		return;
	}
	if (!bReady || !IsVisible(GetWorld(), LevelName(*TargetGate))) return;

	const UArrowComponent* Arrival = TargetGate->Arrival;
	const bool bMoved = Character->TeleportTo(Arrival->GetComponentLocation(), Arrival->GetComponentRotation());
	if (bMoved)
	{
		RegionCharacter = Character;
		CurrentRegion = LevelName(*TargetGate);
		PC->SetControlRotation(Arrival->GetComponentRotation());
		PC->ClientSetLocation(Character->GetActorLocation(), Arrival->GetComponentRotation());
		Character->ForceNetUpdate();
	}
	FinishServer(bMoved);
}

void URPGRegionTravelComponent::FinishServer(bool bCommitted)
{
	const FVector Location = LockedCharacter.IsValid() ? LockedCharacter->GetActorLocation() : FVector::ZeroVector;
	UE_LOG(LogTemp, Display, TEXT("Region travel %s id=%d %s -> %s committed=%d"),
		*GetNameSafe(GetOwner()), Serial, *GetNameSafe(SourceGate.Get()), *GetNameSafe(TargetGate.Get()), bCommitted);
	ReleaseServer();
	ClientFinish(Serial, bCommitted, Location);
}

void URPGRegionTravelComponent::ReleaseServer()
{
	if (!bServerPending) return;
	bServerPending = false;
	GetWorld()->GetTimerManager().ClearTimer(ServerTimer);
	if (BlockedAbilities.IsValid()) BlockedAbilities->UnBlockAbilitiesWithTags(PlayerAbilities());
	if (LockedCharacter.IsValid())
	{
		auto* Movement = LockedCharacter->GetCharacterMovement();
		if (Movement->MovementMode == MOVE_None)
			Movement->SetMovementMode(PreviousMode, PreviousCustomMode);
	}
	BlockedAbilities.Reset();
	LockedCharacter.Reset();
}

void URPGRegionTravelComponent::ClientFinish_Implementation(int32 Id, bool bCommitted, FVector Location)
{
	if (ClientId != Id) return;
	if (!bCommitted)
	{
		if (GetNetMode() == NM_Client) RequestLevel(GetWorld(), ClientTarget, false);
		ReleaseClient();
		return;
	}
	bAwaitingArrival = true;
	ExpectedLocation = Location;
	ArrivalDeadline = GetWorld()->GetRealTimeSeconds() + 5.0;
}

void URPGRegionTravelComponent::ReleaseClient()
{
	if (ClientId == 0) return;
	auto* PC = CastChecked<APlayerController>(GetOwner());
	GetWorld()->GetTimerManager().ClearTimer(ClientTimer);
	if (Screen) Screen->RemoveFromParent();
	if (InputBlock)
	{
		PC->PopInputComponent(InputBlock);
		InputBlock->DestroyComponent();
	}
	PC->SetIgnoreMoveInput(false);
	InputBlock = nullptr;
	Screen = nullptr;
	ClientId = 0;
	bReadySent = false;
	bAwaitingArrival = false;
}

void URPGRegionTravelComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseServer();
	ReleaseClient();
	Super::EndPlay(Reason);
}
