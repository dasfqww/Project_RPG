// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/RPGSurvivalGameMode.h"
#include "Engine/AssetManager.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TargetPoint.h"
#include "NavigationSystem.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"

#include "RPGDebugHelper.h"

ARPGSurvivalGameMode::ARPGSurvivalGameMode()
{
}

void ARPGSurvivalGameMode::RegisterSpawnedNPCs(const TArray<ARPGNonPlayerCharacter*>& InNPCsToRegister)
{
	for (ARPGNonPlayerCharacter* SpawnedNPC:InNPCsToRegister)
	{
		if (SpawnedNPC)
		{
			CurrentSpawnedNPCsCounter++;

			SpawnedNPC->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnNPCDestroyed);
		}
	}
}

void ARPGSurvivalGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	

}

void ARPGSurvivalGameMode::BeginPlay()
{
	Super::BeginPlay();

	checkf(NPCWaveSpawnerDataTable, TEXT("Forgot to assign a valid datat table in survial game mode blueprint"));

	URPGCoreFunctionLibrary::ToggleInputMode(GetWorld(), ERPGInputMode::GameOnly);

	SetCurrentSurvialGameModeState(ERPGSurvialGameModeState::WaitSpawnNewWave);

	TotalWavesToSpawn = NPCWaveSpawnerDataTable->GetRowNames().Num();

	PreLoadNextWaveNPCs();
}

void ARPGSurvivalGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//���߿� ����ȭ �����غ���.
	if (CurrentSurvialGameModeState == ERPGSurvialGameModeState::WaitSpawnNewWave)
	{
		TimePassedSinceStart += DeltaTime;

		if (TimePassedSinceStart >= SpawnNewWaveWaitTime)
		{
			TimePassedSinceStart = 0.f;

			SetCurrentSurvialGameModeState(ERPGSurvialGameModeState::SpawningNewWave);
		}
	}

	if (CurrentSurvialGameModeState == ERPGSurvialGameModeState::SpawningNewWave)
	{
		TimePassedSinceStart += DeltaTime;

		if (TimePassedSinceStart >= SpawnNPCsDelayTime)
		{
			//TODO:Handle spawn new enemies
			CurrentSpawnedNPCsCounter += TrySpawnWaveNPCs();

			TimePassedSinceStart = 0.f;

			SetCurrentSurvialGameModeState(ERPGSurvialGameModeState::InProgress);
		}
	}

	if (CurrentSurvialGameModeState == ERPGSurvialGameModeState::WaveCompleted)
	{
		TimePassedSinceStart += DeltaTime;

		if (TimePassedSinceStart >= WaveCompletedWaitTime)
		{
			TimePassedSinceStart = 0.f;

			CurrentWaveCount++;

			if (HasFinishedAllWaves())
			{
				SetCurrentSurvialGameModeState(ERPGSurvialGameModeState::AllWavesDone);
			}
			else
			{
				SetCurrentSurvialGameModeState(ERPGSurvialGameModeState::WaitSpawnNewWave);
				PreLoadNextWaveNPCs();
			}
		}
	}
}

void ARPGSurvivalGameMode::SetCurrentSurvialGameModeState(ERPGSurvialGameModeState InState)
{
	CurrentSurvialGameModeState = InState;

	OnSurvialGameModeStateChanged.Broadcast(CurrentSurvialGameModeState);
}

bool ARPGSurvivalGameMode::HasFinishedAllWaves() const
{
	

	return CurrentWaveCount > TotalWavesToSpawn;
}

void ARPGSurvivalGameMode::PreLoadNextWaveNPCs()
{
	if (HasFinishedAllWaves())
	{
		return;
	}

	PreLoadedNPCClassMap.Empty();

	for (const FRPGNPCWaveSpawnerInfo& SpawnerInfo : GetCurrentWaveSpawnerTableRow()->NPCWaveSpawnerDefinitions)
	{
		if (SpawnerInfo.SoftNPCClassToSpawn.IsNull()) continue;

		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			SpawnerInfo.SoftNPCClassToSpawn.ToSoftObjectPath(),
			FStreamableDelegate::CreateLambda(
				[SpawnerInfo, this]()
				{
					if (UClass* LoadedNPCClass = SpawnerInfo.SoftNPCClassToSpawn.Get())
					{
						PreLoadedNPCClassMap.Emplace(SpawnerInfo.SoftNPCClassToSpawn, LoadedNPCClass);

						Debug::Print(LoadedNPCClass->GetName() + TEXT(" is loaded"));
					}
				}
			)
		);
	}
}

FRPGNPCWaveSpawnerTableRow* ARPGSurvivalGameMode::GetCurrentWaveSpawnerTableRow() const
{
	const FName RowName = FName(TEXT("Wave") + FString::FromInt(CurrentWaveCount));

	FRPGNPCWaveSpawnerTableRow* FoundRow 
		= NPCWaveSpawnerDataTable->FindRow<FRPGNPCWaveSpawnerTableRow>(RowName, FString());

	checkf(FoundRow, TEXT("Could not find a valid row under the name %s in the data table"), *RowName.ToString());

	return FoundRow;
}

int32 ARPGSurvivalGameMode::TrySpawnWaveNPCs()
{
	if (TargetPointsArray.IsEmpty())
	{
		UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), TargetPointsArray);
	}

	checkf(!TargetPointsArray.IsEmpty(), TEXT("No valid target point found in level: %s for spawning enemies"), *GetWorld()->GetName());

	uint32 NPCsSpawnedThisTime = 0;

	FActorSpawnParameters SpawnParam;
	SpawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (const FRPGNPCWaveSpawnerInfo& SpawnerInfo : GetCurrentWaveSpawnerTableRow()->NPCWaveSpawnerDefinitions)
	{
		if (SpawnerInfo.SoftNPCClassToSpawn.IsNull()) continue;

		const int32 NumToSpawn = FMath::RandRange(SpawnerInfo.MinPerSpawnCount, SpawnerInfo.MaxPerSpawnCount);

		UClass* LoadedEnemyClass = PreLoadedNPCClassMap.FindChecked(SpawnerInfo.SoftNPCClassToSpawn);

		for (int32 i = 0; i < NumToSpawn; i++)
		{
			const int32 RandomTargetPointIndex = FMath::RandRange(0, TargetPointsArray.Num() - 1);
			const FVector SpawnOrigin = TargetPointsArray[RandomTargetPointIndex]->GetActorLocation();
			const FRotator SpawnRotation = TargetPointsArray[RandomTargetPointIndex]->GetActorForwardVector().ToOrientationRotator();

			FVector RandomLocation;
			UNavigationSystemV1::K2_GetRandomLocationInNavigableRadius(this, SpawnOrigin, RandomLocation, 400.f);

			RandomLocation += FVector(0.f, 0.f, 150.f);

			ARPGNonPlayerCharacter* SpawnedNPC 
				= GetWorld()->SpawnActor<ARPGNonPlayerCharacter>(
					LoadedEnemyClass, RandomLocation, SpawnRotation, SpawnParam);

			if (SpawnedNPC)
			{
				SpawnedNPC->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnNPCDestroyed);

				NPCsSpawnedThisTime++;
				TotalSpawnedNPCsThisWaveCounter++;
			}

			if (!ShouldKeepSpawnNPCs())
			{
				return NPCsSpawnedThisTime;
			}
		}
	}

	return NPCsSpawnedThisTime;
}

bool ARPGSurvivalGameMode::ShouldKeepSpawnNPCs() const
{
	return TotalSpawnedNPCsThisWaveCounter<GetCurrentWaveSpawnerTableRow()->TotalNPCToSpawnThisWave;
}

void ARPGSurvivalGameMode::OnNPCDestroyed(AActor* DestroyedActor)
{
	CurrentSpawnedNPCsCounter--;

	//Debug::Print(FString::Printf(TEXT("CurrentSpawnedEnemiesCounter:%i, TotalSpawnedEnemiesThisWaveCounter:%i"), CurrentSpawnedNPCsCounter, TotalSpawnedNPCsThisWaveCounter));

	if (ShouldKeepSpawnNPCs())
	{
		CurrentSpawnedNPCsCounter += TrySpawnWaveNPCs();
	}

	else if (CurrentSpawnedNPCsCounter==0)
	{
		TotalSpawnedNPCsThisWaveCounter = 0;
		CurrentSpawnedNPCsCounter = 0;

		SetCurrentSurvialGameModeState(ERPGSurvialGameModeState::WaveCompleted);
	}
}
