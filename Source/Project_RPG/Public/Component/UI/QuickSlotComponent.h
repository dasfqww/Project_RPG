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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuickSlotChanged, int32, SlotIndex, URPGItemBase*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuickSlotQuantityChanged, URPGItemBase*, Item, int32, NewQuantity);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECT_RPG_API UQuickSlotComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UQuickSlotComponent();

	void SetQuickSlotItem(int32 Index, URPGItemBase* NewItem);
	void UseItemInQuickSlot(int32 Index, const APlayerController* PC);
	void SwapQuickSlotItems(int32 SlotIndexA, int32 SlotIndexB);
	void ClearQuickSlot(int32 Index);

	URPGItemBase* GetItemInSlot(int32 Index) const;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotChanged OnQuickSlotChanged;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotQuantityChanged OnQuickSlotQuantityChanged;

protected:
	virtual void BeginPlay() override;
	void InitializeItemQuickSlots();
	
private:
	UPROPERTY(EditAnywhere, Category = "QuickSlot", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<URPGWidgetBase> PlayerHUDClass;

	UPROPERTY(EditAnywhere, Category = "QuickSlot", meta = (AllowPrivateAccess="true"))
	int32 MaxSlots = 4;

	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_QuickSlotItems, Category = "QuickSlot")
	TArray<URPGItemBase*> QuickSlotItems;

	UFUNCTION()
	void OnRep_QuickSlotItems();

	UPROPERTY(VisibleAnywhere, Category = "QuickSlot")
	TArray<URPGQuickSlotWidget*> QuickSlotWidgets;

	// --- 인벤토리 연동 핸들러 ---

	UFUNCTION()
	void HandleOnItemRemoved(URPGItemBase* RemovedItem);

	// 시그니처를 인벤토리의 FOnQuantityChanged와 일치시킴
	UFUNCTION()
	void HandleOnItemQuantityChanged(const FSlotAvailabilityResult& Result);

	UFUNCTION(Server, Reliable)
	void Server_SetQuickSlotItem(int32 Index, URPGItemBase* NewItem);

	UFUNCTION(Server, Reliable)
	void Server_SwapQuickSlotItems(int32 SlotIndexA, int32 SlotIndexB);

	UFUNCTION(Server, Reliable)
	void Server_ClearQuickSlot(int32 Index);

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FORCEINLINE TArray<URPGItemBase*>& GetQuickSlotItems() { return QuickSlotItems; }
	FORCEINLINE TArray<URPGQuickSlotWidget*>& GetQuickSlotWidgets() { return QuickSlotWidgets; }
	FORCEINLINE int32 GetMaxSlots() const { return MaxSlots; }
};
