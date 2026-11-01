#include "UI/Skill/RPGSkillQuickSlotDragDrop.h"

#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Image.h"
#include "GameFramework/Pawn.h"
#include "UI/RPGDragDropOperation.h"

URPGDragDropOperation* FRPGSkillQuickSlotDragDrop::Create(UObject* Outer, UActorComponent* Source,
	const FGameplayTag SkillTag, UTexture2D* Icon, const int32 SourceSlot)
{
	if (!IsValid(Outer) || !IsValid(Source) || !SkillTag.IsValid()) return nullptr;
	URPGDragDropOperation* Drag = NewObject<URPGDragDropOperation>(Outer);
	Drag->DraggedContent.AbilityTag = SkillTag;
	Drag->SourceComponent = Source;
	Drag->SourceSlotIndex = SourceSlot;
	Drag->bIsFromQuickSlot = SourceSlot != INDEX_NONE;
	Drag->Pivot = EDragPivot::CenterCenter;
	UImage* Visual = NewObject<UImage>(Drag);
	Visual->SetBrushFromTexture(Icon);
	Visual->SetDesiredSizeOverride(FVector2D(64.0f));
	Visual->SetVisibility(ESlateVisibility::HitTestInvisible);
	Drag->DefaultDragVisual = Visual;
	return Drag;
}

bool FRPGSkillQuickSlotDragDrop::Drop(const URPGDragDropOperation& Drag,
	UQuickSlotComponent& Target, const int32 TargetSlot)
{
	const FGameplayTag SkillTag = Drag.DraggedContent.AbilityTag;
	const UActorComponent* Source = Drag.SourceComponent;
	if (!IsValid(Source) || !IsValid(Target.GetOwner()) || Source->GetOwner() != Target.GetOwner() ||
		Drag.DraggedContent.ItemId.IsValid() || Drag.DraggedContent.Item ||
		!Target.CanAssignSkillSlot(TargetSlot, SkillTag)) return false;
	if (Drag.bIsFromQuickSlot)
	{
		const UQuickSlotComponent* SourceSlots = Cast<UQuickSlotComponent>(Source);
		const FRPGQuickSlotContent* Current = SourceSlots
			? SourceSlots->GetSkillSlotContent(Drag.SourceSlotIndex) : nullptr;
		if (SourceSlots != &Target || !Current || Current->AbilityTag != SkillTag) return false;
	}
	else if (!Cast<URPGPlayerSkillComponent>(Source) || Drag.SourceSlotIndex != INDEX_NONE) return false;
	Target.SetSkillSlot(TargetSlot, SkillTag); // client requests; server validates and confirms
	return true;
}
