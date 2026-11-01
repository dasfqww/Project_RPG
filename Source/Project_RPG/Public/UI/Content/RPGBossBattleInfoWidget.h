// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameState/RPGBossBattleGameState.h"
#include "RPGBossBattleInfoWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGBossBattleInfoWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	/** Runs for live updates and once on construct, so late joiners see the current result. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boss|Battle")
	void OnBattleSnapshotUpdated(const FRPGBossBattleSnapshot& Snapshot);

	/** Presentation only: reads coherent replicated snapshots and the GameState clock. */
	UFUNCTION(BlueprintPure, Category = "Boss|Battle")
	FText GetSummonObjectiveStatusText() const;

	UFUNCTION()
	void HandleBattleSnapshotChanged(const FRPGBossBattleSnapshot& Snapshot);

	UFUNCTION(BlueprintCallable)
	void UpdateBattleLimitTimeText(UTextBlock* TextBlock, float InRemainingTime);

	UFUNCTION(BlueprintCallable)
	void OnRestart();

	UFUNCTION(BlueprintCallable)
	void OnExit();

protected:
	void TryBindBattleGameState();
	void UpdateSummonObjectiveStatus();
	TWeakObjectPtr<ARPGBossBattleGameState> BoundBattleGameState;

	/** A designer-authored label takes precedence over the native default label. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Boss|Battle")
	TObjectPtr<UTextBlock> SummonObjectiveText;

	UPROPERTY(EditDefaultsOnly, Category = "Battle")
	TArray<UImage*> LifeImages;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TargetImage;

	/*UPROPERTY(VisibleAnywhere,BlueprintReadOnly,meta = (BindWidget))
	TObjectPtr<UTextBlock> BossNameText;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> RemainTimeText;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DeathCountText;*/


};
