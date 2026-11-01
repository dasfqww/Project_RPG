#pragma once

#include "Character/RPGPlayer.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "RPGBossBattleRetryTestFixture.generated.h"

class ARPGBossBattleGameState;

/** Real RPG player/GAS lifecycle without production input, UI or backend assets. */
UCLASS(Transient, NotBlueprintable)
class ARPGBossRetryTestPlayer : public ARPGPlayer
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	int32 PossessionCount = 0;
private:
	UPROPERTY()
	TObjectPtr<UDataAsset_StartUpDataBase> TestStartup;
};

/** Uses production startup/registration/wipe/retry; isolates backend and spawn failure. */
UCLASS(Transient, NotBlueprintable)
class ARPGBossRetryTestGameMode : public ARPGBossBattleGameMode
{
	GENERATED_BODY()
public:
	ARPGBossRetryTestGameMode();
	/** Set a legacy local state before the production BeginPlay readiness gate. */
	void ConfigureStartup();
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* Controller,
		const FTransform& Transform) override;
	int32 GetParticipantCount() const { return Participants.Num(); }
	bool bFailPlayerSpawn = false;
	bool bOnlineDungeon = false;
	int32 ClearReportCount = 0;
	virtual void ReportConfiguredDungeonClear() override { ++ClearReportCount; }
protected:
	virtual bool IsOnlineDungeonBattle() const override { return bOnlineDungeon; }
};
