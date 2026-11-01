#pragma once

#if WITH_EDITOR

#include "GameFeaturesSubsystem.h"
#include "Misc/DataValidation.h"

namespace RPGExperienceValidation
{
	/** Check dependency names without activating or changing any plugin. */
	inline EDataValidationResult ValidateFeatures(const TArray<FString>& Names,
		const UObject& Owner, FDataValidationContext& Context)
	{
		EDataValidationResult Result = EDataValidationResult::Valid;
		TSet<FString> UniqueNames;
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			const FString& Name = Names[Index];
			if (Name.IsEmpty() || Name != Name.TrimStartAndEnd())
			{
				Context.AddError(FText::Format(
					NSLOCTEXT("RPGExperience", "InvalidFeatureName",
						"{0}: GameFeaturesToEnable[{1}] must contain a plugin name without surrounding whitespace."),
					FText::FromString(Owner.GetPathName()), FText::AsNumber(Index)));
				Result = EDataValidationResult::Invalid;
				continue;
			}

			if (UniqueNames.Contains(Name))
			{
				Context.AddWarning(FText::Format(
					NSLOCTEXT("RPGExperience", "DuplicateFeatureName",
						"{0}: GameFeature {1} is listed more than once; the runtime loads it once."),
					FText::FromString(Owner.GetPathName()), FText::FromString(Name)));
				continue;
			}
			UniqueNames.Add(Name);

			FString URL;
			if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Name, URL))
			{
				Context.AddError(FText::Format(
					NSLOCTEXT("RPGExperience", "UnknownFeatureName",
						"{0}: GameFeature {1} is not registered. Check the plugin name and enabled state."),
					FText::FromString(Owner.GetPathName()), FText::FromString(Name)));
				Result = EDataValidationResult::Invalid;
			}
		}
		return Result;
	}
}

#endif
