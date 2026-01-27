// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameModeBase.generated.h"

class URPGItemBase;
class ARPGPlayer;
class URPGContentClearPanel;

UENUM(BlueprintType)
enum class EGameModeType : uint8
{
	Village UMETA(DisplayName = "Village"),
	Content UMETA(DisplayName = "Content")
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARPGGameModeBase();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable)
	void SetGameDifficulty(ERPGGameDifficulty InGameDifficulty);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Game Settings")
	ERPGGameDifficulty CurrentGameDifficulty;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reward")
	TObjectPtr<UDataTable> RewardDataTable;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reward")
	TObjectPtr<UDataTable> ItemDataTable;

	UPROPERTY()
	TArray<TObjectPtr<URPGItemBase>> RewardItems;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Reward")
	TSubclassOf<URPGContentClearPanel> ClearPanelClass;

	UPROPERTY(EditDefaultsOnly, Category = "Clear Panel UI Transform")
	float CoordX;

	UPROPERTY(EditDefaultsOnly, Category = "Reward")
	bool bGiveReward;

	UFUNCTION(BlueprintCallable)
	void GiveContentReward(ARPGPlayer* Player);

	void InitializeRewardItems(FName InName);

public:
	FORCEINLINE ERPGGameDifficulty GetCurrentGameDifficulty() const { return CurrentGameDifficulty; }
	FORCEINLINE const TArray<TObjectPtr<URPGItemBase>>& GetRewardItems() const { return RewardItems; }
};
