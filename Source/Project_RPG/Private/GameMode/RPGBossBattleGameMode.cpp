// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/RPGBossBattleGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TargetPoint.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "UI/RPGWidgetBase.h"
#include "Manager/SoundManager.h"
#include "GameMode/RPGGameModeBase.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "RPGDebugHelper.h"

ARPGBossBattleGameMode::ARPGBossBattleGameMode():
	BattleTimeLimit(30.f),
	DeathCount(3)
{
}

void ARPGBossBattleGameMode::BeginPlay()
{
	Super::BeginPlay();

	//OnBattleStateChanged.AddDynamic(this, &ThisClass::SetBattleStateChanged);
	//OnBattleStateChanged.AddDynamic(this, &ThisClass::SetBattleStateChanged);

	URPGCoreFunctionLibrary::ToggleInputMode(GetWorld(), ERPGInputMode::GameOnly);

	SetBattleState(EBossBattleState::InProgress);

	SpawnPoint = UGameplayStatics::GetActorOfClass(GetWorld(), ATargetPoint::StaticClass());

	SpawnBoss();
}

void ARPGBossBattleGameMode::Tick(float DeltaTime)
{
	if (CurrentBossBattleState==EBossBattleState::InProgress)
	{
		BattleTimeLimit -= DeltaTime;
		if (BattleTimeLimit<=0.f)
		{
			BattleTimeLimit = 0.f;

			SetBattleState(EBossBattleState::Defeated);
		}
	}

	
	//Debug::Print("Remain Time: ", BattleTimeLimit);
}

void ARPGBossBattleGameMode::SpawnBoss()
{
	if (!BossClass || !SpawnPoint) return;

	FActorSpawnParameters SpawnParam;
	SpawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	FVector SpawnLocation = SpawnPoint->GetActorLocation();
	FRotator SpawnRotation = SpawnPoint->GetActorRotation();

	BossCharacter = GetWorld()->SpawnActor<ARPGNonPlayerCharacter>(BossClass, SpawnLocation, SpawnRotation, SpawnParam);
	if (BossCharacter)
	{
		BossCharacter->OnDestroyed.AddUniqueDynamic(this, &ThisClass::Victory);
	}
}


void ARPGBossBattleGameMode::Victory(AActor* InActor)
{
	SetBattleState(EBossBattleState::Victory);
}

void ARPGBossBattleGameMode::Defeated()
{
	SetBattleState(EBossBattleState::Defeated);
}

void ARPGBossBattleGameMode::DisplayResultWidget(TSubclassOf<URPGWidgetBase> InWidgetClass, USoundBase* InSound)
{
	URPGWidgetBase* ResultWidget = CreateWidget<URPGWidgetBase>(GetWorld(), InWidgetClass);
	if (ResultWidget)
	{
		ResultWidget->AddToViewport();
		if (InSound)
		{
			USoundManager::Get()->Play(InSound);
		}		
	}
}

void ARPGBossBattleGameMode::SetBattleState(EBossBattleState InState)
{
	if (CurrentBossBattleState == InState) return; // ? ���� ���¸� ����

	CurrentBossBattleState = InState;
	OnBattleStateChanged.Broadcast(CurrentBossBattleState);

	Debug::Print("Current State: ", CurrentBossBattleState);
}

void ARPGBossBattleGameMode::DisablePlayerMovement()
{
	ACharacter* Character = GetWorld()->GetFirstPlayerController()->GetCharacter();
	if (Character)
	{
		Character->GetCharacterMovement()->DisableMovement();
	}
}
