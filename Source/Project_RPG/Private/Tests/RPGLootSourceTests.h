#pragma once
#include "Item/PickUp/RPGCurrencyPickup.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "AbilitySystemInterface.h"
#include "RPGLootSourceTests.generated.h"
/** Native authored currency fixture; never used in game content. */
UCLASS()
class ARPGLootTestCurrencyPickup : public ARPGCurrencyPickup
{
    GENERATED_BODY()
public:
    ARPGLootTestCurrencyPickup();
};
UCLASS()
class ARPGLootTestController : public APlayerController, public IGenericTeamAgentInterface
{
    GENERATED_BODY()
public:
    virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(0); }
};
UCLASS()
class ARPGLootTestPawn : public APawn, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    ARPGLootTestPawn();
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    UPROPERTY() TObjectPtr<UAbilitySystemComponent> TestASC;
};
