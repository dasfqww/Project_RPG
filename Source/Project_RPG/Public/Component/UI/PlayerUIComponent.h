// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/UI/PawnUIComponent.h"
#include "GameplayTagContainer.h"
#include "UI/Progress/RPGActionProgressTypes.h"
#include "PlayerUIComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAbilityCooldownBeginDelegate, FGameplayTag, AbilityInputTag, float, TotalCooldownTime, float, RemainingCooldownTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProgressBarShow);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProgressBarHidden);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChargeCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActivateIdentity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeactivateIdentity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProgressBarTextChanged, FName, SkillName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProgressTimeChanged, FString, TimeText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNoticeTextChanged, FText, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActionProgressChanged);


/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UPlayerUIComponent : public UPawnUIComponent
{
	GENERATED_BODY()
public:
	/** Latest valid begin owns the single display; replacing it never cancels gameplay. */
	UFUNCTION(BlueprintCallable, Category="UI|Action Progress")
	FRPGActionProgressHandle BeginActionProgress(UObject* Source, const FRPGActionProgressData& Data);

	/** Capture the returned handle in the originating task/policy, never reread a newer execution's handle. */
	UFUNCTION(BlueprintCallable, Category="UI|Action Progress")
	bool UpdateActionProgress(const FRPGActionProgressHandle& Handle, const FRPGActionProgressData& Data);

	UFUNCTION(BlueprintCallable, Category="UI|Action Progress")
	bool EndActionProgress(const FRPGActionProgressHandle& Handle, ERPGActionProgressEndReason Reason);

	UFUNCTION(BlueprintPure, Category="UI|Action Progress")
	FRPGActionProgressSnapshot GetActionProgressSnapshot() const;


	UPROPERTY(BlueprintAssignable)
		FOnPercentChangedDelegate OnCurrentRageChanged;

	UPROPERTY(BlueprintAssignable)
		FOnPercentChangedDelegate OnCurrentIdentityGaugeChanged;
		
	UPROPERTY(BlueprintAssignable)
		FOnPercentChangedDelegate OnCurrentProgressChanged;
		
	UPROPERTY(BlueprintAssignable)
	FOnProgressBarShow OnProgressBarShow;
	
	UPROPERTY(BlueprintAssignable)
	FOnProgressBarHidden OnProgressBarHidden;
	
	UPROPERTY(BlueprintAssignable)
	FOnChargeCompleted OnChargeCompleted;
	
	UPROPERTY(BlueprintAssignable)
	FOnProgressBarTextChanged OnProgressBarTextChanged;

	UPROPERTY(BlueprintAssignable)
	FOnProgressTimeChanged OnProgressTimeChanged;

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnNoticeTextChanged OnNoticeTextChanged;

	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnActivateIdentity OnActivateIdentity;
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable)
	FOnDeactivateIdentity OnDeactivateIdentity;

	UPROPERTY(BlueprintCallable, BlueprintAssignable)
	FOnAbilityCooldownBeginDelegate OnAbilityCooldownBegin;

	/** Notification only: listeners reread the current snapshot, including during reentrant callbacks. */
	UPROPERTY(BlueprintAssignable, Category="UI|Action Progress")
	FOnActionProgressChanged OnActionProgressChanged;
	FSimpleMulticastDelegate OnActionProgressChangedNative;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION() void OnProgressControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
	bool HasCurrentProgressSource() const;
	void InvalidateActionProgress();
	void NotifyActionProgressChanged();
	UPROPERTY(Transient) FRPGActionProgressSnapshot ActionProgress;
	TWeakObjectPtr<UObject> ActionProgressSource;
	TWeakObjectPtr<AController> ActionProgressController;
	bool bTrackProgressController = false;
	bool bAcceptActionProgress = false;
};
