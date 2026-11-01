#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"

class URPGAbilitySystemComponent;
class UTexture2D;

/** Read-only skill presentation; identity is independent of the input slot. */
struct FRPGSkillQuickSlotPresentation
{
	UTexture2D* Icon = nullptr;
	FText Name;
	float CooldownRemaining = 0.0f;
	float CooldownDuration = 0.0f;
	bool bIsGranted = false;
};

/** The held-input router needs no UI, character, or execution-policy knowledge. */
class PROJECT_RPG_API IRPGSkillQuickSlotInput
{
public:
	virtual ~IRPGSkillQuickSlotInput() = default;
	virtual void Press(FGameplayAbilitySpecHandle Handle) = 0;
	virtual void Release(FGameplayAbilitySpecHandle Handle) = 0;
};

class PROJECT_RPG_API IRPGSkillQuickSlotLookup
{
public:
	virtual ~IRPGSkillQuickSlotLookup() = default;
	virtual FGameplayAbilitySpecHandle Resolve(FGameplayTag SkillTag) const = 0;
	virtual FRPGSkillQuickSlotPresentation Read(FGameplayTag SkillTag) const = 0;
};

/** GAS adapter; owns neither grants nor activation state. */
class PROJECT_RPG_API FRPGGASSkillQuickSlotAccess final
	: public IRPGSkillQuickSlotInput, public IRPGSkillQuickSlotLookup
{
public:
	explicit FRPGGASSkillQuickSlotAccess(URPGAbilitySystemComponent* InASC);
	virtual FGameplayAbilitySpecHandle Resolve(FGameplayTag SkillTag) const override;
	virtual FRPGSkillQuickSlotPresentation Read(FGameplayTag SkillTag) const override;
	virtual void Press(FGameplayAbilitySpecHandle Handle) override;
	virtual void Release(FGameplayAbilitySpecHandle Handle) override;

private:
	TWeakObjectPtr<URPGAbilitySystemComponent> ASC;
};

/** Records Press once and releases its original spec, even after slot changes. */
class PROJECT_RPG_API FRPGSkillQuickSlotInputRouter
{
public:
	using FPressedSlots = TMap<int32, FGameplayAbilitySpecHandle>;
	static bool Press(FPressedSlots& State, int32 Slot, FGameplayAbilitySpecHandle Handle,
		IRPGSkillQuickSlotInput& Input);
	static void Release(FPressedSlots& State, int32 Slot, IRPGSkillQuickSlotInput& Input);
	static void ReleaseAll(FPressedSlots& State, IRPGSkillQuickSlotInput& Input);
};
