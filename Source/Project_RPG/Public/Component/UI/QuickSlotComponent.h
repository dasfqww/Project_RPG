// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Components/ActorComponent.h"
#include "Type/RPGStructTypes.h"
#include "QuickSlotComponent.generated.h"

class URPGItemBase;
class URPGInventoryComponent;
class URPGQuickSlotWidget;
class URPGWidgetBase;
class UTexture2D;
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
	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void SetSkillSlot(int32 Index, FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void SetItemSlot(int32 Index, URPGItemBase* NewItem);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void UseSkillSlot(int32 Index);

	void BeginUseSkillSlot(int32 Index);
	void EndUseSkillSlot(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void UseItemSlot(int32 Index, const APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void ClearSlot(bool bIsSkillSlot, int32 Index);

	void BindInventory(URPGInventoryComponent* Inventory);

	const FRPGQuickSlotContent* GetSkillSlotContent(int32 Index) const;
	const FRPGQuickSlotContent* GetItemSlotContent(int32 Index) const;
	UTexture2D* GetSkillIcon(FGameplayTag AbilityTag) const;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
private:
	UPROPERTY(EditAnywhere, Category = "QuickSlot", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<URPGWidgetBase> PlayerHUDClass;

	UPROPERTY(EditDefaultsOnly, Category = "QuickSlot", meta = (ClampMin = "1"))
	int32 MaxSkillSlots = 8;

	UPROPERTY(EditDefaultsOnly, Category = "QuickSlot", meta = (ClampMin = "1"))
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
	void HandleOnItemQuantityChanged(URPGItemBase* ChangedItem);

	void UnbindInventory();
	void BroadcastSlotChanged(bool bIsSkillSlot, int32 Index);

	UPROPERTY()
	TWeakObjectPtr<URPGInventoryComponent> BoundInventory;

	/** Keeps Release routed to the exact spec that received Press. */
	TMap<int32, FGameplayAbilitySpecHandle> PressedSkillSpecHandles;

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
