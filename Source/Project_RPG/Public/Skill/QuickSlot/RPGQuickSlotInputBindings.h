#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UDataAsset_InputConfig;
class UInputAction;
class UInputMappingContext;
class APlayerController;
struct FRPGKeyMapping;

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
	/** Called only on a controller-owned transient config, before saved user overrides. */
	static void ConfigureItemDefaults(UDataAsset_InputConfig& Config);
	/** Rebase only recognized old item bindings; explicit new-default records take priority. No disk writes. */
	static void MigrateSavedItemBindings(const UDataAsset_InputConfig& OldDefaults,
		const UDataAsset_InputConfig& NewDefaults, TArray<FRPGKeyMapping>& Saved);
	static TArray<FRPGQuickSlotInputBinding> Read(const UDataAsset_InputConfig* Config);
	/** Derived local context: only configured skills, leaving weapon/UI priorities intact. */
	static void RebuildSkillContext(const UDataAsset_InputConfig* Config, UInputMappingContext& Context);
	static FGameplayTag FindTag(const UDataAsset_InputConfig* Config, bool bSkill, int32 Index);
	static FText ReadKeyLabel(const APlayerController* Controller, bool bSkill, int32 Index);
	static bool OwnsAbilityInput(const UDataAsset_InputConfig* Config, FGameplayTag AbilityInputTag);
};
