// Fill out your copyright notice in the Description page of Project Settings.


#include "Actor/Content/RPGContentEntryActor.h"
#include "Components/BoxComponent.h"
#include "UI/Content/RPGContentEntryWidget.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Components/WidgetComponent.h"
#include "Manager/UIManager.h"

#include "RPGDebugHelper.h"

// Sets default values
ARPGContentEntryActor::ARPGContentEntryActor()
{
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
	if (ContentEntryWidgetClass)
	{
		Debug::Print("interact..");
		EntryWidget = CreateWidget<URPGContentEntryWidget>(GetWorld(), ContentEntryWidgetClass);
		if (EntryWidget)
		{
			UUIManager::Get()->PushUI(EntryWidget);
		}
		
		/*if (EntryWidget)
		{
			EntryWidget->AddToViewport();
		}*/
	}
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