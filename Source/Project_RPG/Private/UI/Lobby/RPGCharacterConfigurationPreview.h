#pragma once

#include "CoreMinimal.h"

class URPGSkillDefinition;
struct FRPGSavedProgressionSkill;
struct FRPGSkillRuntimeSpec;

/** Read-only resolution of a lobby draft against authored data; never applies growth or abilities. */
namespace RPGCharacterConfigurationPreview
{
	bool Resolve(const URPGSkillDefinition& Definition, const FRPGSavedProgressionSkill& Draft,
		FRPGSkillRuntimeSpec& OutSpec);
	FText Describe(const URPGSkillDefinition* Definition, const FRPGSavedProgressionSkill& Draft);
}
