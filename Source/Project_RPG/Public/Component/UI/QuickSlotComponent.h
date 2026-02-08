// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Type/RPGStructTypes.h" // FSlotAvailabilityResult 사용을 위해 추가
#include "QuickSlotComponent.generated.h"

class URPGItemBase;
class URPGQuickSlotWidget;
class URPGWidgetBase;
class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuickSlotChanged, int32, SlotIndex, const FRPGQuickSlotContent&, Content);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuickSlotQuantityChanged, URPGItemBase*, Item, int32, NewQuantity);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECT_RPG_API UQuickSlotComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UQuickSlotComponent();

	// 슬롯 설정 및 사용 (타입 구분)
	void SetSkillSlot(int32 Index, FGameplayTag AbilityTag);
	void SetItemSlot(int32 Index, URPGItemBase* NewItem);

	void UseSkillSlot(int32 Index);
	void UseItemSlot(int32 Index, const APlayerController* PC);

	void ClearSlot(bool bIsSkillSlot, int32 Index);

	const FRPGQuickSlotContent* GetSkillSlotContent(int32 Index) const;
	const FRPGQuickSlotContent* GetItemSlotContent(int32 Index) const;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotChanged OnSkillSlotChanged;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotChanged OnItemSlotChanged;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotChanged OnQuickSlotChanged;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotQuantityChanged OnQuickSlotQuantityChanged;

protected:
	virtual void BeginPlay() override;
	
private:
	UPROPERTY(EditAnywhere, Category = "QuickSlot", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<URPGWidgetBase> PlayerHUDClass;

	UPROPERTY(EditAnywhere, Category = "QuickSlot")
	int32 MaxSkillSlots = 8;

	UPROPERTY(EditAnywhere, Category = "QuickSlot")
	int32 MaxItemSlots = 8;

	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_SkillSlots, Category = "QuickSlot")
	TArray<FRPGQuickSlotContent> SkillSlots;

	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_ItemSlots, Category = "QuickSlot")
	TArray<FRPGQuickSlotContent> ItemSlots;

	UFUNCTION()
	void OnRep_SkillSlots();

	UFUNCTION()
	void OnRep_ItemSlots();

	// --- 인벤토리 연동 핸들러 ---

	UFUNCTION()
	void HandleOnItemRemoved(URPGItemBase* RemovedItem);

	UFUNCTION()
	void HandleOnItemQuantityChanged(const FSlotAvailabilityResult& Result);

	UFUNCTION(Server, Reliable)
	void Server_SetSkillSlot(int32 Index, FGameplayTag AbilityTag);

	UFUNCTION(Server, Reliable)
	void Server_SetItemSlot(int32 Index, URPGItemBase* NewItem);

	UFUNCTION(Server, Reliable)
	void Server_ClearSlot(bool bIsSkillSlot, int32 Index);

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FORCEINLINE int32 GetMaxSkillSlots() const { return MaxSkillSlots; }
	FORCEINLINE int32 GetMaxItemSlots() const { return MaxItemSlots; }
};
