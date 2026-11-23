#include "RegionTravel/RPGRegionGate.h"
#include "RegionTravel/RPGRegionTravelComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"

ARPGRegionGate::ARPGRegionGate()
{
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(90, 150, 120));
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Arrival = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrival"));
	Arrival->SetupAttachment(Trigger);
	Arrival->SetRelativeLocation(FVector(240, 0, 0));
}

void ARPGRegionGate::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	ACharacter* Character = Cast<ACharacter>(OtherActor);
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (HasAuthority() && PC && IsValid(Destination) && Destination != this)
	{
		if (auto* Travel = PC->FindComponentByClass<URPGRegionTravelComponent>())
		{
			Travel->BeginTransfer(this, Character);
		}
	}
}
