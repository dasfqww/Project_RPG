// Fill out your copyright notice in the Description page of Project Settings.


#include "Actor/Content/RPGContentEntryActor.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "UI/Content/RPGContentEntryWidget.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Components/WidgetComponent.h"
#include "Manager/UIManager.h"
#include "Riding/RPGRidingLocalInteractionComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

#include "RPGDebugHelper.h"

// Sets default values
ARPGContentEntryActor::ARPGContentEntryActor()
{
	bReplicates = true;
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	RootComponent = TriggerBox;
	TriggerBox->SetCollisionProfileName("Trigger");

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnOverlapBegin);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ThisClass::OnOverlapEnd);

	ContentTextWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("ContentTextWidgetComponent"));
	ContentTextWidgetComponent->SetupAttachment(GetRootComponent());

	InteractionWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("InteractionTextWidgetComponent"));
	InteractionWidgetComponent->SetupAttachment(GetRootComponent());
}

// Called when the game starts or when spawned
void ARPGContentEntryActor::BeginPlay()
{
	Super::BeginPlay();
	
	InteractionWidgetComponent->SetVisibility(false);
}

void ARPGContentEntryActor::BeginFocus()
{
	InteractionWidgetComponent->SetVisibility(true);
}

void ARPGContentEntryActor::EndFocus()
{
	InteractionWidgetComponent->SetVisibility(false);
}

void ARPGContentEntryActor::BeginInteract()
{
}

void ARPGContentEntryActor::EndInteract()
{
}

void ARPGContentEntryActor::Interact(APlayerController* PlayerController)
{
	if (!CanApproveRidingLocalInteraction(PlayerController) || !PlayerController->IsLocalController()
		|| !PlayerController->GetLocalPlayer() || !UUIManager::Get(PlayerController->GetLocalPlayer())) return;
	if (URPGRidingLocalInteractionComponent::NeedsApproval(PlayerController))
	{
		if (auto* Gate = PlayerController->FindComponentByClass<URPGRidingLocalInteractionComponent>())
			Gate->Request(*this, [Self = TWeakObjectPtr<ARPGContentEntryActor>(this), Owner = TWeakObjectPtr<APlayerController>(PlayerController)]
				{ if (Self.IsValid() && Owner.IsValid()) Self->Interact(Owner.Get()); });
		return;
	}
	if (ContentEntryWidgetClass)
	{
		Debug::Print("interact..");
		EntryWidget = CreateWidget<URPGContentEntryWidget>(PlayerController, ContentEntryWidgetClass);
		if (EntryWidget)
		{
			UUIManager::Get(PlayerController->GetLocalPlayer())->PushUI(EntryWidget);
		}
		
		/*if (EntryWidget)
		{
			EntryWidget->AddToViewport();
		}*/
	}
}

FGuid ARPGContentEntryActor::GetRidingLocalInteractionIdentity() const
{
	return MakeRidingLocalInteractionIdentity(GetPathNameSafe(ContentEntryWidgetClass.Get()));
}
bool ARPGContentEntryActor::CanApproveRidingLocalInteraction(APlayerController* Controller) const
{
	const APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	return !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown && ContentEntryWidgetClass
		&& IsValid(Controller) && !Controller->IsActorBeingDestroyed() && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed()
		&& Pawn->GetWorld() == GetWorld() && Controller->GetWorld() == GetWorld() && TriggerBox && TriggerBox->IsRegistered()
		&& !Pawn->GetActorLocation().ContainsNaN()
		&& TriggerBox->Bounds.GetBox().ComputeSquaredDistanceToPoint(Pawn->GetActorLocation()) <= FMath::Square(40.f);
}

void ARPGContentEntryActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor && OtherActor == UGameplayStatics::GetPlayerPawn(this, 0))
	{
		
	}
}

void ARPGContentEntryActor::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, 
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor && OtherActor == UGameplayStatics::GetPlayerPawn(this, 0))
	{
		
	}
}
