// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/URPGSkillListSlot.h"
#include "Blueprint/WidgetTree.h"
#include "CommonTextBlock.h"
#include "Components/Image.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/Skill/RPGSkillLevelAdjuster.h"
#include "UI/Skill/RPGSkillQuickSlotDragDrop.h"
#include "UI/RPGDragDropOperation.h"
#include "UI/Tooltip/RPGSkillTooltip.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "View/MVVMView.h"

void UURPGSkillListSlot::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	UnbindViewModel();
	IUserObjectListEntry::NativeOnListItemObjectSet(ListItemObject);
	URPGSkillSlotViewModel* ViewModel = Cast<URPGSkillSlotViewModel>(ListItemObject);
	CurrentViewModel = ViewModel;

	// Existing WBPs have no compiled MVVM view. Keep native presentation as a
	// fallback, while supporting assets that do declare a manual MVVM source.
	if (UMVVMView* View = ViewModel ? GetExtension<UMVVMView>() : nullptr)
	{
		View->SetViewModelByClass(ViewModel);
	}
	if (ViewModel)
	{
		const auto Refresh = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(
			this, [this](UObject*, UE::FieldNotification::FFieldId) { RefreshSkillPresentation(); });
		ViewModel->AddFieldValueChangedDelegate(
			URPGSkillSlotViewModel::FFieldNotificationClassDescriptor::SkillName, Refresh);
		ViewModel->AddFieldValueChangedDelegate(
			URPGSkillSlotViewModel::FFieldNotificationClassDescriptor::SkillIcon, Refresh);
		ViewModel->AddFieldValueChangedDelegate(
			URPGSkillSlotViewModel::FFieldNotificationClassDescriptor::SkillLevel, Refresh);
		ViewModel->AddFieldValueChangedDelegate(
			URPGSkillSlotViewModel::FFieldNotificationClassDescriptor::bIsLocked, Refresh);
		ViewModel->AddFieldValueChangedDelegate(
			URPGSkillSlotViewModel::FFieldNotificationClassDescriptor::CurrentRefinementIndices, Refresh);
	}

	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([ViewModel](UWidget* Widget)
		{
			if (URPGSkillLevelAdjuster* Adjuster = Cast<URPGSkillLevelAdjuster>(Widget))
			{
				Adjuster->InitializeAdjuster(
					ViewModel ? ViewModel->GetTypedOuter<URPGSkillViewModel>() : nullptr,
					ViewModel);
			}
		});
	}
	RefreshSkillPresentation();
	if (ViewModel)
	{
		OnSlotViewModelSet(ViewModel);
	}
}

FReply UURPGSkillListSlot::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const URPGSkillSlotViewModel* Model = CurrentViewModel.Get();
	const UWidget* Icon = GetWidgetFromName(TEXT("SkillIcon"));
	if (Model && !Model->bIsLocked && Icon && Event.GetEffectingButton() == EKeys::LeftMouseButton &&
		Icon->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}

void UURPGSkillListSlot::NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(Geometry, Event, OutOperation);
	const URPGSkillSlotViewModel* Model = CurrentViewModel.Get();
	if (Model && !Model->bIsLocked)
	{
		OutOperation = FRPGSkillQuickSlotDragDrop::Create(this, Model->GetOwnerComponent(),
			Model->GetSkillTag(), Model->SkillIcon);
	}
}

void UURPGSkillListSlot::RefreshSkillPresentation()
{
	const URPGSkillSlotViewModel* ViewModel = CurrentViewModel.Get();
	if (UCommonTextBlock* Text = Cast<UCommonTextBlock>(GetWidgetFromName(TEXT("SkillNameText"))))
	{
		Text->SetText(ViewModel ? ViewModel->SkillName : FText::GetEmpty());
	}
	if (UImage* Image = Cast<UImage>(GetWidgetFromName(TEXT("SkillIcon"))))
	{
		Image->SetBrushFromTexture(ViewModel ? ViewModel->SkillIcon : nullptr);
	}
	FRPGSkillSaveData Selection;
	FRPGSkillRuntimeSpec Spec;
	const URPGSkillDefinition* Definition = ViewModel ? ViewModel->GetSkillDefinition() : nullptr;
	if (Definition)
	{
		Selection.SkillLevel = ViewModel->SkillLevel;
		Selection.SelectedRefinementIndices = ViewModel->CurrentRefinementIndices;
		Definition->BuildRuntimeSpec(GetOwningPlayerPawn(), Selection, Spec);
	}
	if (UCommonTextBlock* Text = Cast<UCommonTextBlock>(GetWidgetFromName(TEXT("SkillTypeText"))))
	{
		const URPGSkillExecutionPolicy* Policy = Spec.ExecutionPolicyClass
			? Spec.ExecutionPolicyClass->GetDefaultObject<URPGSkillExecutionPolicy>() : nullptr;
		Text->SetText(Policy ? Policy->GetExecutionDisplayName() : FText::GetEmpty());
	}
	URPGSkillTooltip::UpdateFor(*this, Definition
		? FRPGSkillTooltipPresentation::ForSkill(*Definition, Spec, !ViewModel->bIsLocked)
		: FRPGSkillTooltipPresentation());
}

void UURPGSkillListSlot::UnbindViewModel()
{
	if (URPGSkillSlotViewModel* ViewModel = CurrentViewModel.Get())
	{
		ViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	CurrentViewModel.Reset();
	URPGSkillTooltip::UpdateFor(*this, {});
}

void UURPGSkillListSlot::NativeOnEntryReleased()
{
	UnbindViewModel();
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([](UWidget* Widget)
		{
			if (URPGSkillLevelAdjuster* Adjuster = Cast<URPGSkillLevelAdjuster>(Widget))
			{
				Adjuster->InitializeAdjuster(nullptr, nullptr);
			}
		});
	}
	IUserObjectListEntry::NativeOnEntryReleased();
}

void UURPGSkillListSlot::NativeDestruct()
{
	UnbindViewModel();
	Super::NativeDestruct();
}
