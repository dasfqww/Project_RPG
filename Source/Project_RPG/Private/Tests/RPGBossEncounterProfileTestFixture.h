#pragma once
#include "Tests/RPGBossTestFixture.h"
#include "GameMode/RPGGameModeBase.h"
#include "RPGBossEncounterProfileTestFixture.generated.h"

class URPGBossTelegraphPresentationComponent;
class URPGBossTelegraphedStrikeAbility;

UCLASS(Transient, NotBlueprintable)
class ARPGBossProfileTestPawn : public ARPGBossTestPawn
{
	GENERATED_BODY()
public:
	ARPGBossProfileTestPawn();
	virtual void Tick(float DeltaSeconds) override;
	UPROPERTY() TObjectPtr<URPGBossTelegraphPresentationComponent> Presentation;
private:
	int32 ObservedSequence = 0;
	bool bObservedWipe = false;
	bool bObservedRestart = false;
	bool bObservedDeath = false;
};

/** Isolated test mode. Production online admission is not replaced or exercised by this fixture. */
UCLASS(Transient, NotBlueprintable)
class ARPGBossProfileTestGameMode : public ARPGGameModeBase
{
	GENERATED_BODY()
public:
	ARPGBossProfileTestGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
private:
	UPROPERTY() TObjectPtr<ARPGBossProfileTestPawn> Source;
	UPROPERTY() TObjectPtr<ARPGBossTestPawn> Target;
	UPROPERTY() TObjectPtr<URPGBossTelegraphedStrikeAbility> Attack;
	FGameplayAbilitySpecHandle Handle;
	int32 Clients = 0;
	int32 Stage = 0;
	double StageTime = 0.0;
	bool Activate(bool bShow);
};
