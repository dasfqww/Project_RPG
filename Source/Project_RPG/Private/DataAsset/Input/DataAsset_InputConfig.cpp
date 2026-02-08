// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/Input/DataAsset_InputConfig.h"

#if WITH_EDITOR
void UDataAsset_InputConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	auto UpdateDisplayNames = [](TArray<FWarriorInputActionConfig>& ConfigArray)
	{
		for (FWarriorInputActionConfig& Config : ConfigArray)
		{
			if (Config.InputTag.IsValid())
			{
				FString TagString = Config.InputTag.ToString();
				const FString Prefix = TEXT("InputTag.");

				if (TagString.StartsWith(Prefix))
 				{
					// 앞의 "Input." (6글자)을 제거하고 나머지를 입력
					FString NewName = TagString.RightChop(Prefix.Len());
					Config.InputActionName = FText::FromString(NewName);
				}
				else
				{
					// "Input."으로 시작하지 않으면 태그 전체를 이름으로 설정
					Config.InputActionName = FText::FromString(TagString);
				}
			}
		}
	};

	UpdateDisplayNames(NativeInputActions);
	UpdateDisplayNames(AbilityInputActions);
}
#endif

UInputAction* UDataAsset_InputConfig::FindNativeInputActionByTag(const FGameplayTag& InInputTag) const
{
	for (const FWarriorInputActionConfig& InputActionConfig : NativeInputActions)
	{
		if (InputActionConfig.InputTag == InInputTag && InputActionConfig.InputAction)
		{
			return InputActionConfig.InputAction;
		}
	}

	return nullptr;
}

UInputAction* UDataAsset_InputConfig::FindAbilityInputActionByTag(const FGameplayTag& InInputTag) const
{
	for (const FWarriorInputActionConfig& InputActionConfig : AbilityInputActions)
	{
		if (InputActionConfig.InputTag == InInputTag && InputActionConfig.InputAction)
		{
			return InputActionConfig.InputAction;
		}
	}

	return nullptr;
}
