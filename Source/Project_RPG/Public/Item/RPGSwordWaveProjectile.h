#pragma once

#include "Item/RPGProjectileBase.h"
#include "RPGSwordWaveProjectile.generated.h"

class URPGSwordWaveAbility;

UCLASS()
class PROJECT_RPG_API ARPGSwordWaveProjectile : public ARPGProjectileBase
{
	GENERATED_BODY()

public:
	ARPGSwordWaveProjectile();

protected:
	virtual void BeginPlay() override;
	virtual bool ApplyDamageFromImpact(APawn* Target, const FHitResult& ServerHit) override;

private:
	friend class URPGSwordWaveAbility;
	TWeakObjectPtr<URPGSwordWaveAbility> OwningCast;
};
