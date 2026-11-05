#pragma once

#include "UObject/Interface.h"
#include "RPGClassSkillPolicy.generated.h"

class URPGGameplayAbility_SkillContainer;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class URPGClassSkillPolicy : public UInterface
{
	GENERATED_BODY()
};

/** Optional native policy on a class definition's IdentityAbility CDO.
 * Implementations are stateless dispatchers: authoritative state belongs to the granted ability.
 * The container freezes the provider for an activation and reserves commit/hit hooks before callbacks.
 */
class PROJECT_RPG_API IRPGClassSkillPolicy
{
	GENERATED_BODY()
public:
	virtual bool CanCommitClassSkill(const URPGGameplayAbility_SkillContainer& Skill) const = 0;
	/** After GAS commit, once per cast. No refund on cancellation. */
	virtual bool CommitClassSkill(URPGGameplayAbility_SkillContainer& Skill,
		float& OutDamageMultiplier, bool& OutSuppressHitGain) const = 0;
	/** First server-confirmed hit only; never client target claims or per-target/tick grants. */
	virtual void OnClassSkillHitConfirmed(URPGGameplayAbility_SkillContainer& Skill) const = 0;
};
