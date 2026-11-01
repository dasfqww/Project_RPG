#include "Tests/RPGBossBattleRetryTestFixture.h"

#include "Tests/RPGBossStartupTestFixture.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "GameState/RPGBossBattleGameState.h"

void ARPGBossRetryTestPlayer::BeginPlay()
{
	TestStartup = NewObject<UDataAsset_StartUpDataBase>(this);
	CharacterStartUpData = TestStartup.Get();
	SetActorEnableCollision(false);
	Super::BeginPlay();
}

void ARPGBossRetryTestPlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	++PossessionCount;
	GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.f);
	GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
}

ARPGBossRetryTestGameMode::ARPGBossRetryTestGameMode()
{
	BossClass = ARPGBossStartupTestCharacter::StaticClass();
	DefaultPawnClass = ARPGBossRetryTestPlayer::StaticClass();
	BattleTimeLimit = 300.f;
}

void ARPGBossRetryTestGameMode::ConfigureStartup()
{
	// Reproduce the runtime mismatch without mutating a Blueprint asset/CDO.
	CurrentBossBattleState = EBossBattleState::InProgress;
}

APawn* ARPGBossRetryTestGameMode::SpawnDefaultPawnAtTransform_Implementation(
	AController* Controller, const FTransform& Transform)
{
	return bFailPlayerSpawn ? nullptr : Super::SpawnDefaultPawnAtTransform_Implementation(Controller, Transform);
}
