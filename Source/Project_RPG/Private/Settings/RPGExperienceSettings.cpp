#include "Settings/RPGExperienceSettings.h"

#include "Actor/RPGExperienceSelectionActor.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGExperienceSelection, Log, All);

FSoftObjectPath URPGExperienceSettings::ResolveForWorld(const UWorld& World) const
{
	FSoftObjectPath Path;
	FString Error;
	if (!TryResolveForWorld(World, Path, Error))
	{
		UE_LOG(LogRPGExperienceSelection, Warning, TEXT("%s"), *Error);
	}
	return Path;
}

bool URPGExperienceSettings::TryResolveForWorld(
	const UWorld& World, FSoftObjectPath& OutPath, FString& OutError) const
{
	OutPath.Reset();
	const ARPGExperienceSelectionActor* Selection =
		ARPGExperienceSelectionActor::FindForWorld(World, OutError);
	if (!OutError.IsEmpty()) { return false; }
	if (Selection)
	{
		switch (Selection->SelectionMode)
		{
		case ERPGExperienceSelectionMode::DisableExperience:
			break;
		case ERPGExperienceSelectionMode::LoadExperience:
			OutPath = Selection->Experience.ToSoftObjectPath();
			if (!OutPath.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Experience selection actor %s has no valid Experience asset."),
					*Selection->GetPathName());
				OutPath.Reset();
				return false;
			}
			break;
		default:
			OutError = FString::Printf(
				TEXT("Experience selection actor %s has an unknown selection mode."),
				*Selection->GetPathName());
			return false;
		}
		UE_LOG(LogRPGExperienceSelection, Display,
			TEXT("RPG_EXPERIENCE MAP_SELECTION World=%s Actor=%s Mode=%s Definition=%s"),
			*World.GetName(), *Selection->GetName(),
			Selection->SelectionMode == ERPGExperienceSelectionMode::DisableExperience
				? TEXT("Disabled") : TEXT("Load"),
			*OutPath.ToString());
		return true;
	}

	FString PackageName = UWorld::RemovePIEPrefix(World.GetOutermost()->GetName());
	if (const TSoftObjectPtr<URPGExperienceDefinition>* Selected =
		MapExperiences.Find(FName(*PackageName)))
	{
		OutPath = Selected->ToSoftObjectPath();
	}
	else
	{
		OutPath = DefaultExperience.ToSoftObjectPath();
	}
	return true;
}
