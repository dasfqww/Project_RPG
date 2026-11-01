// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/RPGBaseCharacter.h"
#include "GenericTeamAgentInterface.h"
#include "RPGNonPlayerCharacter.generated.h"

class UNPCCombatComponent;
class UNPCUIComponent;
class UWidgetComponent;
class UBoxComponent;
class UDataAsset_StartUpDataBase;
class URPGCombatContributionComponent;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGNPCStartUpDataReady);



/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGNonPlayerCharacter : public ARPGBaseCharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()
public:
	ARPGNonPlayerCharacter();
	/** A late replica must not replay an entry montage over an active control snapshot. */
	virtual float PlayAnimMontage(UAnimMontage* AnimMontage, float InPlayRate = 1.f, FName StartSectionName = NAME_None) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Encounter team is readable without an AIController, including on remote clients. */
	virtual FGenericTeamId GetGenericTeamId() const override;

	/** Server-local readiness; clients consume replicated attributes and boss state instead. */
	UFUNCTION(BlueprintPure, Category = "NPC|Initialization")
	bool IsNPCStartUpDataReady() const;

	/** Server-local, actor-lifetime drop claim. Not a respawn/reset or client RPC. */
	bool CanProcessDeathItemDrop() const;
	bool TryBeginDeathItemDrop();
	/** One-way, server-only policy for encounter summons; ordinary NPCs retain their existing drops. */
	void SuppressDeathItemDrops();
	/** Configure before FinishSpawning so auto-possession inherits the encounter team. */
	void ConfigureEncounterSummon(uint8 TeamId);
	virtual void SpawnDefaultController() override;
	AController* GetEncounterSummonController() const { return EncounterSummonController.Get(); }
	/** One-way shutdown before an encounter registry destroys this NPC, possibly on the next tick. */
	void RetireEncounterSummon();
	bool AreDeathItemDropsSuppressed() const { return bDeathItemDropsSuppressed; }

	/** Fired once after the synchronous startup grant has returned. */
	UPROPERTY(BlueprintAssignable, Category = "NPC|Initialization")
	FRPGNPCStartUpDataReady OnNPCStartUpDataReady;

	virtual UPawnCombatComponent* GetPawnCombatComponent() const override;

	virtual UPawnUIComponent* GetPawnUIComponent() const override;

	virtual UNPCUIComponent* GetNPCUIComponent()const override;

	

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void UnPossessed() override;
	virtual void HandleNPCStartUpDataReady();

	//~ Begin APawn Interface.
	virtual void PossessedBy(AController* NewController) override;
	//~ End APawn Interface

#if WITH_EDITOR
//~ Begin UObject Interface.
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif
	

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
		TObjectPtr<UNPCCombatComponent> NPCCombatComponent;

	/** Disabled by default; encounters or training content explicitly opt in. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<URPGCombatContributionComponent> CombatContributionComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
		TObjectPtr<UBoxComponent> LeftHandCollisionBox;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
		FName LeftHandCollisionBoxAttachBoneName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UBoxComponent> RightHandCollisionBox;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
		FName RightHandCollisionBoxAttachBoneName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UBoxComponent> BodyCollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
		TObjectPtr<UNPCUIComponent> NPCUIComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UWidgetComponent> NPCHealthWidgetComponent;

	UFUNCTION()
		virtual void OnBodyCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, 
			AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
			bool bFromSweep, const FHitResult& SweepResult);

private:
	void InitNPCStartUpData();
	void CompleteNPCStartUpData(uint32 RequestId, UDataAsset_StartUpDataBase* LoadedData, int32 ApplyLevel);
	void CancelNPCStartUpLoad();
	TSharedPtr<FStreamableHandle> NPCStartUpLoadHandle;
	uint32 NPCStartUpRequestId = 0;
	bool bNPCStartUpLoading = false;
	bool bNPCStartUpGrantStarted = false;
	bool bNPCStartUpReady = false;
	bool bNPCStartUpEndingPlay = false;
	bool bDeathItemDropStarted = false;
	bool bDeathItemDropsSuppressed = false;
	UPROPERTY(Replicated)
	uint8 EncounterSummonTeam = 255;
	TWeakObjectPtr<AController> EncounterSummonController;

public:
	FORCEINLINE UNPCCombatComponent* GetNPCCombatComponent() const { return NPCCombatComponent; }
	FORCEINLINE UBoxComponent* GetLeftHandCollisionBox() const { return LeftHandCollisionBox; }
	FORCEINLINE UBoxComponent* GetRightHandCollisionBox() const { return RightHandCollisionBox; }
};
