#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UActorComponent;
class UQuickSlotComponent;
class URPGDragDropOperation;
class UTexture2D;

/** UI gesture adapter; replicated assignment/ownership remain backend responsibilities. */
class PROJECT_RPG_API FRPGSkillQuickSlotDragDrop
{
public:
	static URPGDragDropOperation* Create(UObject* Outer, UActorComponent* Source,
		FGameplayTag SkillTag, UTexture2D* Icon, int32 SourceSlot = INDEX_NONE);
	static bool Drop(const URPGDragDropOperation& Drag, UQuickSlotComponent& Target, int32 TargetSlot);
};
