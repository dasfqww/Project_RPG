#pragma once

#include "Ability/RPGGameplayAbility.h"
#include "Character/RPGBaseCharacter.h"
#include "RPGCombatControlTestFixture.generated.h"

/** No content startup or AI auto-possession; exercises the real shared control/movement. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class ARPGStunTestCharacter : public ARPGBaseCharacter
{
	GENERATED_BODY()
public:
	ARPGStunTestCharacter();
	virtual UPawnUIComponent* GetPawnUIComponent() const override { return TestPawnUI.Get(); }
	virtual void PossessedBy(AController* NewController) override;
	virtual uint32 GetCombatPresentationRevision() const override { return Revision; }
	void AdvanceLife() { ++Revision; }
private:
	UPROPERTY(Transient) TObjectPtr<UPawnUIComponent> TestPawnUI;
	uint32 Revision = 0;
};

UCLASS(Transient, NotBlueprintable)
class URPGStunTestCombatAbility : public URPGGameplayAbility
{
	GENERATED_BODY()
public:
	URPGStunTestCombatAbility();
	void SetInterruptible(bool bValue) { SetCanBeCanceled(bValue); }
};

UCLASS(Transient, NotBlueprintable)
class URPGStunTestPassiveAbility : public URPGStunTestCombatAbility
{
	GENERATED_BODY()
public:
	URPGStunTestPassiveAbility();
};

UCLASS(Transient, NotBlueprintable)
class URPGStunTestDeathAbility : public URPGStunTestCombatAbility
{
	GENERATED_BODY()
public:
	URPGStunTestDeathAbility();
};

UCLASS(Transient, NotBlueprintable)
class URPGImmunityTestCombatAbility : public URPGStunTestCombatAbility
{
	GENERATED_BODY()
public:
	void Finish() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false); }
	bool RouteLegacy(AActor* Target, const FRPGStaggerSettings& Stagger = FRPGStaggerSettings());
};

UCLASS(Transient, NotBlueprintable)
class URPGImmunityTestReactionAbility : public URPGImmunityTestCombatAbility
{
	GENERATED_BODY()
public:
	URPGImmunityTestReactionAbility();
};
