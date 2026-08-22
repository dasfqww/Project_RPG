// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/Definition/RPGExperienceDefinition.h"

#include "DataAsset/Definition/RPGExperienceActionSet.h"
#include "GameFeatureAction.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "RPGExperience"

URPGExperienceDefinition::URPGExperienceDefinition()
{
}

#if WITH_EDITOR
EDataValidationResult URPGExperienceDefinition::IsDataValid(FDataValidationContext& Context) const
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
				LOCTEXT("NullExperienceAction", "Null entry at index {0} in Actions"),
				FText::AsNumber(ActionIndex)));
		}
	}

	if (!GetClass()->IsNative())
	{
		const UClass* ParentClass = GetClass()->GetSuperClass();
		const UClass* FirstNativeParent = ParentClass;

		while (FirstNativeParent && !FirstNativeParent->IsNative())
		{
			FirstNativeParent = FirstNativeParent->GetSuperClass();
		}

		if (FirstNativeParent != ParentClass)
		{
			Result = EDataValidationResult::Invalid;
			Context.AddError(LOCTEXT(
				"NestedBlueprintExperience",
				"Blueprint subclasses of Blueprint experiences are unsupported; use ActionSets for composition."));
		}
	}

	return Result;
}
#endif

#if WITH_EDITORONLY_DATA
void URPGExperienceDefinition::UpdateAssetBundleData()
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

