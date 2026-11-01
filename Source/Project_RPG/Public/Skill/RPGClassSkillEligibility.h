#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"

class URPGSkillDefinition;

/** Read-only authorization. State stays in progression, equipment and quick slots. */
namespace RPGClassSkillEligibility
{
	PROJECT_RPG_API bool CanAssign(const AActor* Avatar, FGameplayTag SkillTag,
		const URPGSkillDefinition* Definition, const UClass* AbilityClass, FGameplayAbilitySpecHandle Handle);
	PROJECT_RPG_API bool CanActivate(const AActor* Avatar, FGameplayTag SkillTag,
		const URPGSkillDefinition* Definition, const UClass* AbilityClass, FGameplayAbilitySpecHandle Handle);
	/** Blocks existing configuration commands while a class skill is executing. */
	PROJECT_RPG_API bool CanChangeConfiguration(const AActor* Avatar);
	/** Initial native restore is allowed for backend-managed read-only configurations. */
	PROJECT_RPG_API bool CanRestoreConfiguration(const AActor* Avatar);
}
