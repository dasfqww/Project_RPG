#include "AI/RPGBossPatternDefinition.h"
#include "Ability/RPGBossGameplayAbility.h"

bool FRPGBossPatternDefinition::IsConfigured() const
{
	return PatternId.IsValid() || AbilityClass || Settings || TelegraphPolicy != ERPGBossTelegraphPolicy::Inherit;
}

bool FRPGBossPatternDefinition::Validate(FText& OutError) const
{
	const UClass* Class = AbilityClass.Get();
	if (!PatternId.IsValid() || !Class || !Class->IsChildOf(URPGBossGameplayAbility::StaticClass()) ||
		Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
		static_cast<uint8>(TelegraphPolicy) > static_cast<uint8>(ERPGBossTelegraphPolicy::Hide))
	{
		OutError = FText::FromString(TEXT("A pattern needs a valid ID, concrete boss ability and valid telegraph policy."));
		return false;
	}
	return Class->GetDefaultObject<URPGBossGameplayAbility>()->ValidatePatternSettings(Settings, OutError);
}

bool FRPGBossPatternDefinition::ShouldShowTelegraph(bool bDefault) const
{
	return TelegraphPolicy == ERPGBossTelegraphPolicy::Show ||
		(TelegraphPolicy == ERPGBossTelegraphPolicy::Inherit && bDefault);
}
