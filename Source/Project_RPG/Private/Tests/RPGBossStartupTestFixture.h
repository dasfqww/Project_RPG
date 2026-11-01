#pragma once

#include "Character/RPGBossCharacter.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "RPGBossStartupTestFixture.generated.h"

UCLASS(Transient, NotBlueprintable)
class URPGBossStartupTestData : public UDataAsset_StartUpDataBase
{
	GENERATED_BODY()
public:
	virtual void GiveToAbilitySystemComponent(URPGAbilitySystemComponent* ASC, int32 ApplyLevel) override;
	int32 GrantCount = 0;
	int32 LastApplyLevel = 0;
	bool bEarlyStartDenied = false;
	bool bRepossessDuringGrant = false;
};

/** Real NPC/Boss inheritance, with in-memory test startup data and no production content. */
UCLASS(Transient, NotBlueprintable)
class ARPGBossStartupTestCharacter : public ARPGBossCharacter
{
	GENERATED_BODY()
public:
	ARPGBossStartupTestCharacter();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void SetAutoStartForTest(bool bEnabled) { bStartEncounterOnReady = bEnabled; }
	void SetRoleForTest(ENetRole NewRole) { SetRole(NewRole); }
	int32 ReadyCount = 0;
	UPROPERTY()
	TObjectPtr<URPGBossStartupTestData> TestData;
protected:
	virtual void HandleNPCStartUpDataReady() override;
private:
	void TickNetworkSummon();
	bool bSawSummonState = false;
	bool bSawSummonClear = false;
	TArray<TWeakObjectPtr<ARPGNonPlayerCharacter>> ObservedSummons;
	double ObservedSummonExpiry = 0.0;
	void TickNetworkDash();
	bool bSawDashState = false;
	bool bSawDashMove = false;
	bool bSawDashDamage = false;
	bool bSawDashClear = false;
	bool bSawDashQuiet = false;
	FVector FirstDashLocation = FVector::ZeroVector;
	FVector DashStopProbeLocation = FVector::ZeroVector;
	double DashInactiveObserveTime = -1.0;
	bool bReportedClientState = false;
	bool bSawTelegraph = false;
	bool bSawTelegraphCleared = false;
	bool bSawTimedStrikeTelegraph = false;
	bool bSawIndicator = false;
	bool bSawIndicatorCleared = false;
};
