// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "URPGSkillListSlot.generated.h"

class URPGSkillSlotViewModel;

/**
 * UURPGSkillListSlot
 * 
 * 스킬 목록의 개별 항목(버튼)입니다.
 * ListView에서 사용하기 위해 IUserObjectListEntry를 구현합니다.
 */
UCLASS()
class PROJECT_RPG_API UURPGSkillListSlot : public UCommonButtonBase, public IUserObjectListEntry
{
	GENERATED_BODY()

protected:
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
	virtual void NativeOnEntryReleased() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event,
		UDragDropOperation*& OutOperation) override;

public:
	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|UI")
	void OnSlotViewModelSet(URPGSkillSlotViewModel* InViewModel);

private:
	void RefreshSkillPresentation();
	void UnbindViewModel();
	TWeakObjectPtr<URPGSkillSlotViewModel> CurrentViewModel;
};
