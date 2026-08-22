#include "DataAsset/Definition/RPGExperienceActionSet.h"

#include "GameFeatureAction.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "RPGExperience"

URPGExperienceActionSet::URPGExperienceActionSet()
{
}

#if WITH_EDITOR
EDataValidationResult URPGExperienceActionSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 ActionIndex = 0; ActionIndex < Actions.Num(); ++ActionIndex)
	{
		const UGameFeatureAction* Action = Actions[ActionIndex];
		if (Action)
		{
			Result = CombineDataValidationResults(Result, Action->IsDataValid(Context));
		}
		else
		{
			Result = EDataValidationResult::Invalid;
			Context.AddError(FText::Format(
				LOCTEXT("NullActionSetAction", "Null entry at index {0} in Actions"),
				FText::AsNumber(ActionIndex)));
		}
	}

	return Result;
}
#endif

#if WITH_EDITORONLY_DATA
void URPGExperienceActionSet::UpdateAssetBundleData()
{
	Super::UpdateAssetBundleData();

	for (UGameFeatureAction* Action : Actions)
	{
		if (Action)
		{
			Action->AddAdditionalAssetBundleData(AssetBundleData);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
