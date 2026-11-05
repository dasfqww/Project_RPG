#include "AI/RPGBossEncounterProfiles.h"
#include "AI/RPGBossDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameMode/RPGGameModeBase.h"
#include "Manager/HttpWebManager.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const URPGBossDefinition* URPGBossEncounterProfiles::Resolve(ERPGGameDifficulty Difficulty) const
{
	if (static_cast<uint8>(Difficulty) > static_cast<uint8>(ERPGGameDifficulty::Hell)) return nullptr;
	const TObjectPtr<URPGBossDefinition>* Found = Profiles.Find(Difficulty);
	return Found ? Found->Get() : Fallback.Get();
}

bool URPGBossEncounterProfiles::ValidateProfiles(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (Profiles.IsEmpty() && !Fallback)
	{
		OutError = FText::FromString(TEXT("Encounter profiles require a difficulty mapping or an explicit fallback."));
		return false;
	}
	for (const auto& Pair : Profiles)
	{
		if (static_cast<uint8>(Pair.Key) > static_cast<uint8>(ERPGGameDifficulty::Hell) || !Pair.Value)
		{
			OutError = FText::FromString(TEXT("Encounter profile mappings require a valid difficulty and definition."));
			return false;
		}
		if (!Pair.Value->ValidateDefinition(OutError)) return false;
	}
	return !Fallback || Fallback->ValidateDefinition(OutError);
}

const URPGBossDefinition* URPGBossEncounterProfiles::ResolveForWorld(const UWorld* World, FText& OutError) const
{
	const ARPGGameModeBase* Mode = World ? World->GetAuthGameMode<ARPGGameModeBase>() : nullptr;
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const UHttpWebManager* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
	if (!Mode || !Mode->HasAuthority() || (Web && Web->IsConfiguredForDungeonServer() && !Mode->IsBackendDungeonDifficultyReady()))
	{
		OutError = FText::FromString(TEXT("Boss profiles require an authoritative RPG GameMode and a confirmed online difficulty."));
		return nullptr;
	}
	if (!ValidateProfiles(OutError)) return nullptr;
	const URPGBossDefinition* Selected = Resolve(Mode->GetCurrentGameDifficulty());
	if (!Selected) OutError = FText::FromString(TEXT("No boss encounter profile is configured for the confirmed difficulty."));
	return Selected;
}

#if WITH_EDITOR
EDataValidationResult URPGBossEncounterProfiles::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	FText Error;
	if (!ValidateProfiles(Error)) { Context.AddError(Error); return EDataValidationResult::Invalid; }
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
