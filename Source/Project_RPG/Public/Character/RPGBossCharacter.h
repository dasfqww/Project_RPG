#pragma once

#include "Character/RPGNonPlayerCharacter.h"
#include "RPGBossCharacter.generated.h"

class URPGBossAIComponent;
class URPGBossThreatComponent;
class URPGBossTelegraphPresentationComponent;
class URPGBossDashComponent;
class URPGBossSummonComponent;
class URPGBossSummonObjectiveComponent;
class UBrainComponent;
struct FRPGBossState;

/** Opt-in NPC boss base. Reuses the NPC ASC and startup data, without changing ordinary NPC replication. */
UCLASS()
class PROJECT_RPG_API ARPGBossCharacter : public ARPGNonPlayerCharacter
{
	GENERATED_BODY()
public:
	ARPGBossCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<URPGBossAIComponent> BossAIComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<URPGBossThreatComponent> BossThreatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<URPGBossTelegraphPresentationComponent> BossTelegraphPresentation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<URPGBossDashComponent> BossDashComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<URPGBossSummonComponent> BossSummonComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<URPGBossSummonObjectiveComponent> BossSummonObjectiveComponent;

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void HandleNPCStartUpDataReady() override;

	/** Otherwise the arena/aggro owner explicitly calls StartEncounter once ready. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	bool bStartEncounterOnReady = false;

private:
	void TryStartInitialEncounter();
	void StopDeadBoss();
	void UpdateEncounterMovement();
	TWeakObjectPtr<UBrainComponent> StoppedEncounterBrain;
	bool bStoppedEncounterMovement = false;
	uint8 SavedEncounterMovementMode = 0;
	uint8 SavedEncounterCustomMovementMode = 0;
	UFUNCTION()
	void HandleBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current);
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
