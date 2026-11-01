// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/RPGBaseCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Equipment/RPGEquipComponent.h"
#include "Component/Equipment/RPGEquipmentComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "MotionWarpingComponent.h"
#include "UI/DamageFontWidget.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"

#include "RPGDebugHelper.h"

// Sets default values
ARPGBaseCharacter::ARPGBaseCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URPGCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
	//DamageFontDestroyTime(2.0f)
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	GetMesh()->bReceivesDecals = false;

	RPGAbilitySystemComponent = CreateDefaultSubobject<URPGAbilitySystemComponent>(TEXT("RPGAbilitySystemComponent"));

	RPGAttributeSet = CreateDefaultSubobject<URPGAttributeSet>(TEXT("RPGAttributeSet"));

	CombatReactionComponent = CreateDefaultSubobject<URPGCombatReactionComponent>(TEXT("CombatReactionComponent"));
	CombatControlComponent = CreateDefaultSubobject<URPGCombatControlComponent>(TEXT("CombatControlComponent"));

	EquipComponent = CreateDefaultSubobject<URPGEquipComponent>(TEXT("EquipComponent"));

	EquipmentComponent = CreateDefaultSubobject<URPGEquipmentComponent>(TEXT("EquipmentComponent"));

	AuthoritativeEquipmentComponent =
		CreateDefaultSubobject<URPGAuthoritativeEquipmentComponent>(
			TEXT("AuthoritativeEquipmentComponent"));

	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));

	bReplicates = true;
}

void ARPGBaseCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void ARPGBaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//UpdateDamageFonts();
}

UAbilitySystemComponent* ARPGBaseCharacter::GetAbilitySystemComponent() const
{
	return GetRPGAbilitySystemComponent();
}

UPawnCombatComponent* ARPGBaseCharacter::GetPawnCombatComponent() const
{
	return nullptr;
}

UPawnUIComponent* ARPGBaseCharacter::GetPawnUIComponent() const
{
	return nullptr;
}

UPlayerUIComponent* ARPGBaseCharacter::GetPlayerUIComponent() const
{
	return nullptr;
}

UNPCUIComponent* ARPGBaseCharacter::GetNPCUIComponent() const
{
	return nullptr;
}

void ARPGBaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (RPGAbilitySystemComponent)
	{
		RPGAbilitySystemComponent->InitAbilityActorInfo(this, this);

		ensureMsgf(!CharacterStartUpData.IsNull(), TEXT("Forgot to assign start up data to %s"), *GetName());
	}
}

void ARPGBaseCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (RPGAbilitySystemComponent)
	{
		RPGAbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

void ARPGBaseCharacter::UnPossessed()
{
	CombatControlComponent->ResetForAvatarReuse();
	Super::UnPossessed();
}

void ARPGBaseCharacter::ShowDamageFont(float Damage, FVector Location, bool bCriticalAttack, bool bIsPlayerDamage)
{
	// GAS damage runs on dedicated servers, where Slate and a viewport do not exist.
	// Presentation must not interrupt authoritative health/death processing.
	if (GetNetMode() == NM_DedicatedServer) return;
	if (DamageFontWidgetClass)
	{
		UDamageFontWidget* DamageWidget = CreateWidget<UDamageFontWidget>(GetWorld(), DamageFontWidgetClass);

		if (DamageWidget)
		{
			DamageWidget->InitializeDamageFont(Damage, Location, bCriticalAttack, bIsPlayerDamage);
		}
	}
}

void ARPGBaseCharacter::ShowInvincibleFont(FVector Location)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (DamageFontWidgetClass)
	{
		UDamageFontWidget* DamageWidget = CreateWidget<UDamageFontWidget>(GetWorld(), DamageFontWidgetClass);

		if (DamageWidget)
		{
			DamageWidget->InitializeInvincibleText(Location);
		}
	}
}
