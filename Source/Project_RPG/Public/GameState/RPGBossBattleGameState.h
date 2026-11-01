#pragma once

#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGGameStateBase.h"
#include "RPGBossBattleGameState.generated.h"

class ARPGBossCharacter;

/** One authoritative snapshot; clients (including late joiners) never read the GameMode. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossBattleSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Boss|Battle")
	EBossBattleState BattleState = EBossBattleState::Waiting;

	UPROPERTY(BlueprintReadOnly, Category = "Boss|Battle")
	TObjectPtr<ARPGBossCharacter> Boss = nullptr;

	/** GameState server clock; zero until combat starts. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Battle")
	double EndServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Boss|Battle")
	int32 Revision = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGBossBattleSnapshotChanged,
	const FRPGBossBattleSnapshot&, Snapshot);

UCLASS()
class PROJECT_RPG_API ARPGBossBattleGameState : public ARPGGameStateBase
{
	GENERATED_BODY()
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Boss|Battle")
	FRPGBossBattleSnapshot GetBattleSnapshot() const { return BattleSnapshot; }

	UFUNCTION(BlueprintPure, Category = "Boss|Battle")
	float GetRemainingBattleTime() const;

	/** Subscribe, then read GetBattleSnapshot for late-join/current state. */
	UPROPERTY(BlueprintAssignable, Category = "Boss|Battle")
	FRPGBossBattleSnapshotChanged OnBattleSnapshotChanged;

	void SetBoss(ARPGBossCharacter* NewBoss);
	void StartBattle(float DurationSeconds);
	void FinishBattle(EBossBattleState Result);
	void PrepareNextBattle();

private:
	UPROPERTY(ReplicatedUsing = OnRep_BattleSnapshot)
	FRPGBossBattleSnapshot BattleSnapshot;

	void PublishBattleSnapshot();

	UFUNCTION()
	void OnRep_BattleSnapshot();
};
