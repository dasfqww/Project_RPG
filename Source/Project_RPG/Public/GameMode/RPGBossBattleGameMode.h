// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/RPGGameModeBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RPGBossBattleGameMode.generated.h"


class ARPGBossCharacter;
class APlayerController;
class ACharacter;
struct FRPGBossState;
class URPGWidgetBase;

UENUM(BlueprintType)
enum class EBossBattleState : uint8
{
	InProgress,   // 전투 진행 중
	Victory,     // 플레이어가 보스를 처치함
	Defeated,       // 시간 초과 또는 전멸 등으로 실패
	Waiting         // Boss startup/ASC grant has not completed yet
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void Logout(AController* Exiting) override;

	void SpawnBoss();

	UFUNCTION(BlueprintCallable)
	void Victory(AActor* InActor);

	UFUNCTION(BlueprintCallable)
	void Defeated();

	/** Server-only restart; replaces the boss and restores registered player pawns. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Battle")
	void RestartBattle();

	/** Only a registered arena participant may request a retry from a client. */
	bool TryRestartBattleForParticipant(APlayerController* Requester, int32 ExpectedBattleRevision = INDEX_NONE);

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
	virtual bool IsOnlineDungeonBattle() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Battle")
	EBossBattleState CurrentBossBattleState;

	UPROPERTY()
	TObjectPtr<AActor> SpawnPoint;

	UPROPERTY(EditDefaultsOnly, Category = "BossCharacter")
	TSubclassOf<ARPGBossCharacter> BossClass;

	UPROPERTY()
	TObjectPtr<ARPGBossCharacter> BossCharacter;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Battle")
	float BattleTimeLimit;

	UPROPERTY(EditDefaultsOnly, Category = "Battle")
	int32 DeathCount;

	/** Only living player pawns near the arena can join this encounter. */
	UPROPERTY(EditDefaultsOnly, Category = "Battle", meta = (ClampMin = "1.0"))
	float ParticipationRadius = 15000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Clear")
	TSubclassOf<URPGWidgetBase> VictoryWidgetClass;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Clear")
	TSubclassOf<URPGWidgetBase> DefeatWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Clear")
	TObjectPtr<USoundBase> ClearSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Clear")
	TObjectPtr<USoundBase> FailedSound;

	TArray<TWeakObjectPtr<APlayerController>> Participants;
	struct FParticipantRetryState
	{
		TWeakObjectPtr<APawn> LastPawn;
		FTransform EntryTransform;
	};
	TMap<TWeakObjectPtr<APlayerController>, FParticipantRetryState> ParticipantRetryStates;

	struct FDisabledParticipantMovement
	{
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<ACharacter> Character;
		TEnumAsByte<EMovementMode> PreviousMode = MOVE_Walking;
		uint8 PreviousCustomMode = 0;
	};
	TArray<FDisabledParticipantMovement> DisabledParticipantMovement;

private:
	friend struct FRPGDungeonDifficultyTestAccess;
	UFUNCTION()
	void HandleBackendDungeonDifficultyConfirmed(ERPGGameDifficulty Difficulty);
	bool bBossSpawnEndingPlay = false;
	FTimerHandle BackendDifficultySpawnTimer;
	friend struct FRPGDungeonGameplayRevocationTestAccess;
	void TryStartBattle();
	void RegisterLivingParticipants();
	bool HasLivingParticipant() const;
	void RestoreParticipantMovement();
	bool ResetParticipantsForRetry();
	bool bRestartingBattle = false;

	UFUNCTION()
	void HandleBossStartUpReady();

	UFUNCTION()
	void HandleBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& NewState);

	UFUNCTION()
	void HandleBossDestroyed(AActor* DestroyedActor);

public:
	FORCEINLINE EBossBattleState GetCurrentBossBattleState() const { return CurrentBossBattleState; }

};
