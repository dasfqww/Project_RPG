// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actor/RPGTeamInfoBase.h"
#include "RPGTeamPublicInfo.generated.h"

class URPGTeamDisplayAsset;

/**
 * * Public team info for PvE RPG
 * - Holds display data (icon, color, etc.)
 * - Replicated to clients for UI/HUD updates
 */
UCLASS()
class PROJECT_RPG_API ARPGTeamPublicInfo : public ARPGTeamInfoBase
{
	GENERATED_BODY()
public:
	ARPGTeamPublicInfo(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
protected:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_TeamDisplayAsset();

	void SetTeamDisplayAsset(TObjectPtr<URPGTeamDisplayAsset> NewDisplayAsset);

private:
	// UI/HUD에 표시할 팀 아이콘/색상 
	UPROPERTY(ReplicatedUsing=OnRep_TeamDisplayAsset) 
	TObjectPtr<URPGTeamDisplayAsset> TeamDisplayAsset; 
	// 파티/진영 이름 (옵션)
	UPROPERTY(EditDefaultsOnly, Category="Team")
	FString TeamName;

public:
	FORCEINLINE URPGTeamDisplayAsset* GetDisplayAsset() const { return DisplayAsset; }
	FORCEINLINE FString GetTeamName() const { return TeamName; }

};
