#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"

#include "Controller/RPGPlayerController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"

namespace
{
bool SharesMappedKey(const UInputMappingContext* Context, const UInputAction* First, const UInputAction* Second)
{
	if (!Context) return false;
	for (const auto& FirstMapping : Context->GetMappings())
	{
		if (FirstMapping.Action != First || !FirstMapping.Key.IsValid()) continue;
		for (const auto& SecondMapping : Context->GetMappings())
		{
			if (SecondMapping.Action == Second && SecondMapping.Key == FirstMapping.Key) return true;
		}
	}
	return false;
}
}

bool FRPGQuickSlotInputBindings::ParseTag(const FGameplayTag Tag, bool& bSkill, int32& Index)
{
	bSkill = false;
	Index = INDEX_NONE;
	const FString Name = Tag.ToString();
	FString Number;
	if (Name.StartsWith(TEXT("InputTag.QuickSkill.")))
	{
		bSkill = true;
		Number = Name.RightChop(20);
	}
	else if (Name.StartsWith(TEXT("InputTag.QuickItem."))) Number = Name.RightChop(19);
	else return false;
	// Compatibility alias for the existing QuickItem.F1 tag.
	if (!bSkill && Number == TEXT("F1")) Number = TEXT("1");
	if (Number.IsEmpty() || Number[0] == TCHAR('0')) return false;
	int64 OneBased = 0;
	for (const TCHAR Character : Number)
	{
		if (Character < TCHAR('0') || Character > TCHAR('9')) return false;
		OneBased = OneBased * 10 + Character - TCHAR('0');
		if (OneBased > MAX_int32) return false;
	}
	Index = static_cast<int32>(OneBased) - 1;
	return Index >= 0;
}

TArray<FRPGQuickSlotInputBinding> FRPGQuickSlotInputBindings::Read(const UDataAsset_InputConfig* Config)
{
	TArray<FRPGQuickSlotInputBinding> Result;
	if (!Config) return Result;
	for (const FWarriorInputActionConfig& Entry : Config->NativeInputActions)
	{
		FRPGQuickSlotInputBinding Binding;
		if (!Entry.IsValid() || !ParseTag(Entry.InputTag, Binding.bIsSkillSlot, Binding.SlotIndex)) continue;
		Binding.InputTag = Entry.InputTag;
		Binding.Action = Entry.InputAction;
		const bool bDuplicate = Result.ContainsByPredicate([&Binding](const auto& Item)
		{
			return Item.InputTag == Binding.InputTag && Item.Action == Binding.Action;
		});
		if (!bDuplicate) Result.Add(Binding);
	}
	// Multiple actions for one skill slot cannot safely share a held-input owner.
	// Likewise, one action/key must never trigger two different skill slots.
	// Reject both sides, regardless of row order; keep the existing item aliases.
	TSet<int32> Conflicts;
	for (int32 First = 0; First < Result.Num(); ++First)
	{
		if (!Result[First].bIsSkillSlot) continue;
		for (int32 Second = First + 1; Second < Result.Num(); ++Second)
		{
			if (!Result[Second].bIsSkillSlot) continue;
			if (Result[First].SlotIndex == Result[Second].SlotIndex ||
				Result[First].Action == Result[Second].Action ||
				SharesMappedKey(Config->DefaultMappingContext, Result[First].Action, Result[Second].Action))
			{
				Conflicts.Add(First);
				Conflicts.Add(Second);
			}
		}
	}
	for (int32 Index = Result.Num() - 1; Index >= 0; --Index)
		if (Conflicts.Contains(Index)) Result.RemoveAt(Index);
	return Result;
}

void FRPGQuickSlotInputBindings::RebuildSkillContext(const UDataAsset_InputConfig* Config,
	UInputMappingContext& Context)
{
	Context.UnmapAll();
	if (!Config || !Config->DefaultMappingContext) return;
	TSet<const UInputAction*> Actions;
	for (const auto& Binding : Read(Config))
		if (Binding.bIsSkillSlot) Actions.Add(Binding.Action);
	for (const auto& Mapping : Config->DefaultMappingContext->GetMappings())
		if (Actions.Contains(Mapping.Action.Get())) Context.MapKey(Mapping.Action, Mapping.Key) = Mapping;
}

FGameplayTag FRPGQuickSlotInputBindings::FindTag(const UDataAsset_InputConfig* Config,
	const bool bSkill, const int32 Index)
{
	FGameplayTag Alias;
	for (const auto& Binding : Read(Config))
	{
		if (Binding.bIsSkillSlot != bSkill || Binding.SlotIndex != Index) continue;
		if (Binding.InputTag.ToString().EndsWith(TEXT(".F1"))) Alias = Binding.InputTag;
		else return Binding.InputTag;
	}
	return Alias; // keep both item hotkeys working; label the numeric mapping first
}

FText FRPGQuickSlotInputBindings::ReadKeyLabel(const APlayerController* Controller,
	const bool bSkill, const int32 Index)
{
	const ARPGPlayerController* RPGController = Cast<ARPGPlayerController>(Controller);
	if (!RPGController) return FText::GetEmpty();
	const FGameplayTag Tag = FindTag(RPGController->GetInputConfig(), bSkill, Index);
	const FKey Key = Tag.IsValid() ? RPGController->GetCurrentKeyForTag(Tag) : FKey();
	return Key.IsValid() ? Key.GetDisplayName(false) : FText::GetEmpty();
}

bool FRPGQuickSlotInputBindings::OwnsAbilityInput(const UDataAsset_InputConfig* Config,
	const FGameplayTag AbilityInputTag)
{
	const UInputAction* AbilityAction = Config ? Config->FindAbilityInputActionByTag(AbilityInputTag) : nullptr;
	if (!AbilityAction) return false;
	// A configured slot owns its action even if conflicting rows are rejected.
	// Invalid quick-slot data must not silently re-enable the old fixed skill.
	for (const auto& Slot : Config->NativeInputActions)
	{
		bool bSkill = false;
		int32 Index = INDEX_NONE;
		if (!Slot.IsValid() || !ParseTag(Slot.InputTag, bSkill, Index) || !bSkill) continue;
		if (Slot.InputAction == AbilityAction) return true;
		// Two distinct actions mapped to the same key must not execute two skills.
		if (SharesMappedKey(Config->DefaultMappingContext, AbilityAction, Slot.InputAction)) return true;
	}
	return false;
}
