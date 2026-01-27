// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/RPGNonPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Component/Combat/NPCCombatComponent.h"
#include "Engine/AssetManager.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Component/UI/NPCUIComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/BoxComponent.h"
#include "UI/RPGWidgetBase.h"
#include "RPGFunctionLibrary.h"
#include "GameMode/RPGGameModeBase.h"

#include "RPGDebugHelper.h"

ARPGNonPlayerCharacter::ARPGNonPlayerCharacter()
{
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;

	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 180.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 300.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 1000.f;

	NPCCombatComponent = CreateDefaultSubobject<UNPCCombatComponent>(TEXT("NPCCombatComponent"));

	NPCUIComponent = CreateDefaultSubobject<UNPCUIComponent>(TEXT("NPCUIComponent"));

	NPCHealthWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("NPCHealthWidgetComponent"));
	NPCHealthWidgetComponent->SetupAttachment(GetMesh());

	LeftHandCollisionBox = CreateDefaultSubobject<UBoxComponent>("LeftHandCollisionBox");
	LeftHandCollisionBox->SetupAttachment(GetMesh());
	LeftHandCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftHandCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnBodyCollisionBoxBeginOverlap);

	RightHandCollisionBox = CreateDefaultSubobject<UBoxComponent>("RightHandCollisionBox");
	RightHandCollisionBox->SetupAttachment(GetMesh());
	RightHandCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightHandCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnBodyCollisionBoxBeginOverlap);

	BodyCollisionBox = CreateDefaultSubobject<UBoxComponent>("BodyCollisionBox");
	BodyCollisionBox->SetupAttachment(GetMesh());
	BodyCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnBodyCollisionBoxBeginOverlap);
}

UPawnCombatComponent* ARPGNonPlayerCharacter::GetPawnCombatComponent() const
{
	return NPCCombatComponent;
}

UPawnUIComponent* ARPGNonPlayerCharacter::GetPawnUIComponent() const
{
	return NPCUIComponent;
}

UNPCUIComponent* ARPGNonPlayerCharacter::GetNPCUIComponent() const
{
	return NPCUIComponent;
}

void ARPGNonPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (URPGWidgetBase* HealthWidget=Cast<URPGWidgetBase>(NPCHealthWidgetComponent->GetUserWidgetObject()))
	{
		HealthWidget->InitNPCCreatedWidget(this);
	}
}

void ARPGNonPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitNPCStartUpData();
}

#if WITH_EDITOR
void ARPGNonPlayerCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() ==
		GET_MEMBER_NAME_CHECKED(ThisClass, LeftHandCollisionBoxAttachBoneName))
	{
		LeftHandCollisionBox->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, LeftHandCollisionBoxAttachBoneName);
	}

	if (PropertyChangedEvent.GetMemberPropertyName() ==
		GET_MEMBER_NAME_CHECKED(ThisClass, RightHandCollisionBoxAttachBoneName))
	{
		RightHandCollisionBox->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, RightHandCollisionBoxAttachBoneName);
	}
}
#endif

void ARPGNonPlayerCharacter::OnBodyCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, 
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (APawn* HitPawn = Cast<APawn>(OtherActor))
	{
		if (URPGFunctionLibrary::IsTargetPawnHostile(this, HitPawn))
		{
			NPCCombatComponent->OnHitTargetActor(HitPawn);
		}
	}
}

void ARPGNonPlayerCharacter::InitNPCStartUpData()
{
	if (CharacterStartUpData.IsNull())
	{
		return;
	}

	int32 AbilityApplyLevel = 1;

	if (ARPGGameModeBase* BaseGameMode = GetWorld()->GetAuthGameMode<ARPGGameModeBase>())
	{
		switch (BaseGameMode->GetCurrentGameDifficulty())
		{
		case ERPGGameDifficulty::Easy:
			AbilityApplyLevel = 1;
			break;

		case ERPGGameDifficulty::Normal:
			AbilityApplyLevel = 2;
			break;

		case ERPGGameDifficulty::Hard:
			AbilityApplyLevel = 3;
			break;

		case ERPGGameDifficulty::Hell:
			AbilityApplyLevel = 4;
			break;

		default:
			break;
		}
	}

	UAssetManager::GetStreamableManager().RequestAsyncLoad(
		CharacterStartUpData.ToSoftObjectPath(),
		FStreamableDelegate::CreateLambda(
			[this, AbilityApplyLevel]()
			{
				if (UDataAsset_StartUpDataBase* LoadedData = CharacterStartUpData.Get())
				{
					LoadedData->GiveToAbilitySystemComponent(RPGAbilitySystemComponent, AbilityApplyLevel);

					//Debug::Print(TEXT("NPC Start Up Data Loaded.."), FColor::Green);
				}
			}
		)
	);
}
