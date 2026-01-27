// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/RPGGameModeBase.h"
#include "RPGSurvivalGameMode.generated.h"

class ARPGNonPlayerCharacter;

UENUM(BlueprintType)
enum class ERPGSurvialGameModeState : uint8
{
	WaitSpawnNewWave,
	SpawningNewWave,
	InProgress,
	WaveCompleted,
	AllWavesDone,
	PlayerDied
};

USTRUCT(BlueprintType)
struct FRPGNPCWaveSpawnerInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TSoftClassPtr<ARPGNonPlayerCharacter> SoftNPCClassToSpawn;

	UPROPERTY(EditAnywhere)
	int32 MinPerSpawnCount = 1;

	UPROPERTY(EditAnywhere)
	int32 MaxPerSpawnCount = 3;
};

USTRUCT(BlueprintType)
struct FRPGNPCWaveSpawnerTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<FRPGNPCWaveSpawnerInfo> NPCWaveSpawnerDefinitions;

	UPROPERTY(EditAnywhere)
	int32 TotalNPCToSpawnThisWave = 1;
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSurvialGameModeStateChangedDelegate, ERPGSurvialGameModeState, CurrentState);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGSurvivalGameMode : public ARPGGameModeBase
{
	GENERATED_BODY()
public:
	ARPGSurvivalGameMode();

	UFUNCTION(BlueprintCallable)
	void RegisterSpawnedNPCs(const TArray<ARPGNonPlayerCharacter*>& InNPCsToRegister);

	

protected:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	void SetCurrentSurvialGameModeState(ERPGSurvialGameModeState InState);

	bool HasFinishedAllWaves() const;

	void PreLoadNextWaveNPCs();
	FRPGNPCWaveSpawnerTableRow* GetCurrentWaveSpawnerTableRow() const;
	int32 TrySpawnWaveNPCs();
	bool ShouldKeepSpawnNPCs() const;

	UFUNCTION()
	void OnNPCDestroyed(AActor* DestroyedActor);

	UPROPERTY()
	ERPGSurvialGameModeState CurrentSurvialGameModeState;

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnSurvialGameModeStateChangedDelegate OnSurvialGameModeStateChanged;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WaveDefinition", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDataTable> NPCWaveSpawnerDataTable;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "WaveDefinition", meta = (AllowPrivateAccess = "true"))
	int32 TotalWavesToSpawn;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "WaveDefinition", meta = (AllowPrivateAccess = "true"))
	int32 CurrentWaveCount = 1;

	UPROPERTY()
	int32 CurrentSpawnedNPCsCounter = 0;

	UPROPERTY()
	int32 TotalSpawnedNPCsThisWaveCounter = 0;

	UPROPERTY()
	TArray<AActor*> TargetPointsArray;


	UPROPERTY()
	float TimePassedSinceStart = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WaveDefinition", meta = (AllowPrivateAccess = "true"))
	float SpawnNewWaveWaitTime = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "WaveDefinition", meta = (AllowPrivateAccess = "true"))
	float SpawnNPCsDelayTime = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WaveDefinition", meta = (AllowPrivateAccess = "true"))
	float WaveCompletedWaitTime = 5.f;

	UPROPERTY()
	TMap<TSoftClassPtr<ARPGNonPlayerCharacter>, UClass*> PreLoadedNPCClassMap;
};
