// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Components/ActorComponent.h"
#include "Type/RPGStructTypes.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "Security/RPGServerRequestBudget.h"
#include "QuickSlotComponent.generated.h"

class URPGActionBarContext;
class URPGItemBase;
class URPGInventoryProjectionComponent;
class IRPGItemActionPolicyCatalog;
class URPGQuickSlotWidget;
class URPGWidgetBase;
class UTexture2D;
class APlayerController;
struct FRPGSkillQuickSlotPresentation;

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

	UFUNCTION(BlueprintCallable, Category = "QuickSlot",
		meta = (DeprecatedFunction, DeprecationMessage = "Use SetItemSlotById."))
	void SetItemSlot(int32 Index, URPGItemBase* NewItem);

	/** The server validates the ID against its owned projection and action policy. */
	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void SetItemSlotById(int32 Index, FGuid ItemId);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void UseSkillSlot(int32 Index);

	void BeginUseSkillSlot(int32 Index);
	void EndUseSkillSlot(int32 Index);
	void ReleaseAllSkillInputs(bool bKeepPhysicalState = false);
	UFUNCTION(BlueprintPure, Category="ActionBar")
	URPGActionBarContext* GetActionBarContext();
	bool CanAssignSkillSlot(int32 Index, FGameplayTag SkillTag) const;
	bool IsSkillEquipped(FGameplayTag SkillTag) const;
	/** Trusted server restore, once per Pawn. Empty records stay empty; defaults are chosen only by an explicit creation caller. */
	bool InitializeSkillSlotsFromRecord(uint32 ExpectedSkillRestoreGeneration,
		const TArray<FGameplayTag>& SlotTags, FText& OutError);
	bool HasInitializedSkillSlots() const { return bSkillSlotsInitialized; }
	FRPGSkillQuickSlotPresentation ReadSkillPresentation(FGameplayTag SkillTag) const;

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void UseItemSlot(int32 Index, const APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void ClearSlot(bool bIsSkillSlot, int32 Index);

	/** Optional policy catalog must outlive the binding; otherwise use the GameInstance catalog. */
	void BindInventoryProjection(URPGInventoryProjectionComponent* Projection,
		const IRPGItemActionPolicyCatalog* Policies = nullptr);

	UFUNCTION(BlueprintPure, Category = "QuickSlot")
	bool FindAssignedItem(int32 Index, FRPGInventoryProjectionEntry& OutEntry) const;

	const FRPGQuickSlotContent* GetSkillSlotContent(int32 Index) const;
	const FRPGQuickSlotContent* GetItemSlotContent(int32 Index) const;
	UTexture2D* GetSkillIcon(FGameplayTag AbilityTag) const;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotChanged OnSkillSlotChanged;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotChanged OnItemSlotChanged;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot")
	FOnQuickSlotChanged OnQuickSlotChanged;

	UPROPERTY(BlueprintAssignable, Category = "QuickSlot",
		meta = (DeprecatedProperty, DeprecationMessage = "Use OnItemSlotChanged; quantity comes from projection."))
	FOnQuickSlotQuantityChanged OnQuickSlotQuantityChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
private:
	UPROPERTY(EditAnywhere, Category = "QuickSlot", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<URPGWidgetBase> PlayerHUDClass;

	UPROPERTY(EditDefaultsOnly, Category = "QuickSlot", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxSkillSlots = 8;

	UPROPERTY(EditDefaultsOnly, Category = "QuickSlot", meta = (ClampMin = "1"))
	int32 MaxItemSlots = 8;

	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_SkillSlots, Category = "QuickSlot")
	TArray<FRPGQuickSlotContent> SkillSlots;

	/** A rejected request must still refresh a client's pending drag/drop presentation. */
	UPROPERTY(ReplicatedUsing = OnRep_SkillSlots)
	uint32 SkillSlotsRevision = 0;

	UPROPERTY(ReplicatedUsing = OnRep_SkillSlots)
	bool bSkillSlotsInitialized = false;

	UPROPERTY(VisibleAnywhere, ReplicatedUsing = OnRep_ItemSlots, Category = "QuickSlot")
	TArray<FRPGQuickSlotContent> ItemSlots;

	UFUNCTION()
	void OnRep_SkillSlots();

	UFUNCTION()
	void OnRep_ItemSlots();

	// --- 인벤토리 연동 핸들러 ---

	UFUNCTION()
	void HandleProjectionChanged();

	UFUNCTION()
	void HandleProjectionLoadStateChanged(ERPGInventoryProjectionLoadState LoadState);

	void UnbindInventoryProjection();
	void EnsureInventoryProjectionBinding();
	void BroadcastSlotChanged(bool bIsSkillSlot, int32 Index);
	friend struct FRPGRpcIngressTestAccess;
	bool AdmitSlotRequest();
	FRPGServerRequestBudget SlotRequestBudget{32, 16.0};

	UPROPERTY()
	TWeakObjectPtr<URPGInventoryProjectionComponent> BoundProjection;
	const IRPGItemActionPolicyCatalog* ActionPolicies = nullptr;

	/** Keeps Release routed to the exact spec that received Press. */
	TMap<int32, FGameplayAbilitySpecHandle> PressedSkillSpecHandles;
	TSet<int32> HeldSkillInputs;
	UPROPERTY(Transient) TObjectPtr<URPGActionBarContext> ActionBarContext;

	UFUNCTION(Server, Reliable)
	void Server_SetSkillSlot(int32 Index, FGameplayTag AbilityTag);

	UFUNCTION(Server, Reliable)
	void Server_SetItemSlotById(int32 Index, FGuid ItemId);

	UFUNCTION(Server, Reliable)
	void Server_ClearSlot(bool bIsSkillSlot, int32 Index);

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FORCEINLINE int32 GetMaxSkillSlots() const { return MaxSkillSlots; }
	FORCEINLINE int32 GetMaxItemSlots() const { return MaxItemSlots; }
};
