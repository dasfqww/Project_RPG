#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UDataAsset_InputConfig;
class UInputAction;
class UInputMappingContext;
class APlayerController;

struct FRPGQuickSlotInputBinding
{
	FGameplayTag InputTag;
	UInputAction* Action = nullptr;
	int32 SlotIndex = INDEX_NONE;
	bool bIsSkillSlot = false;
};

/** One data-derived mapping used by the controller and slot key labels. */
class PROJECT_RPG_API FRPGQuickSlotInputBindings
{
public:
	static bool ParseTag(FGameplayTag Tag, bool& bSkill, int32& Index);
	static TArray<FRPGQuickSlotInputBinding> Read(const UDataAsset_InputConfig* Config);
	/** Derived local context: only configured skills, leaving weapon/UI priorities intact. */
	static void RebuildSkillContext(const UDataAsset_InputConfig* Config, UInputMappingContext& Context);
	static FGameplayTag FindTag(const UDataAsset_InputConfig* Config, bool bSkill, int32 Index);
	static FText ReadKeyLabel(const APlayerController* Controller, bool bSkill, int32 Index);
	static bool OwnsAbilityInput(const UDataAsset_InputConfig* Config, FGameplayTag AbilityInputTag);
};
