// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/RPGGameModeBase.h"
#include "RPGBossBattleGameMode.generated.h"


class ARPGNonPlayerCharacter;
class URPGWidgetBase;

UENUM(BlueprintType)
enum class EBossBattleState : uint8
{
	InProgress,   // 전투 진행 중
	Victory,     // 플레이어가 보스를 처치함
	Defeated        // 시간 초과 또는 전멸 등으로 실패
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBattleStateChanged, EBossBattleState, BattleState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossCleared);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGBossBattleGameMode : public ARPGGameModeBase
{
	GENERATED_BODY()
public:
	ARPGBossBattleGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void SpawnBoss();

	UFUNCTION(BlueprintCallable)
	void Victory(AActor* InActor);

	UFUNCTION(BlueprintCallable)
	void Defeated();

	UFUNCTION(BlueprintCallable)
	void DisplayResultWidget(TSubclassOf<URPGWidgetBase> InWidgetClass, USoundBase* InSound);

	UFUNCTION(BlueprintCallable)
	void SetBattleState(EBossBattleState InState);

	UFUNCTION(BlueprintCallable)
	void DisablePlayerMovement();

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnBattleStateChanged OnBattleStateChanged;

	/*UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnBossCleared OnBossCleared;*/

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Battle")
	EBossBattleState CurrentBossBattleState;

	UPROPERTY()
	TObjectPtr<AActor> SpawnPoint;

	UPROPERTY(EditDefaultsOnly, Category = "BossCharacter")
	TSubclassOf<ARPGNonPlayerCharacter> BossClass;

	UPROPERTY()
	TObjectPtr<ARPGNonPlayerCharacter> BossCharacter;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle")
	float BattleTimeLimit;

	UPROPERTY(EditDefaultsOnly, Category = "Battle")
	int32 DeathCount;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Clear")
	TSubclassOf<URPGWidgetBase> VictoryWidgetClass;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Clear")
	TSubclassOf<URPGWidgetBase> DefeatWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Clear")
	TObjectPtr<USoundBase> ClearSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Clear")
	TObjectPtr<USoundBase> FailedSound;

	FTimerHandle BattleTimerHandle;

public:
	FORCEINLINE EBossBattleState GetCurrentBossBattleState() const { return CurrentBossBattleState; }

};
