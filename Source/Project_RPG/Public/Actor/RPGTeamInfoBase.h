// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Type/RPGEnumTypes.h"
#include "RPGTeamInfoBase.generated.h"

class UTeamManager;
class URPGTeamDisplayAsset;

/**
 * ARPGTeamInfoBase: 월드 내 특정 팀의 정보를 관리하는 액터
 */
UCLASS(Abstract)
class PROJECT_RPG_API ARPGTeamInfoBase : public AInfo
{
	GENERATED_BODY()

public:
	ARPGTeamInfoBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	ERPGTeamID GetTeamId() const { return TeamId; }

	URPGTeamDisplayAsset* GetDisplayAsset() const { return DisplayAsset; }

	//~AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of AActor interface

	/** 팀 전용 태그 컨테이너 (복제됨) */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "RPG|Teams")
	FGameplayTagContainer TeamTags;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Teams")
	TObjectPtr<URPGTeamDisplayAsset> DisplayAsset;

	virtual void RegisterWithTeamSubsystem(UTeamManager* Subsystem);
	void TryRegisterWithTeamSubsystem();

private:
	UFUNCTION()
	void OnRep_TeamId();

public:
	/** 이 함수는 서버에서 팀을 설정할 때 사용합니다. */
	void SetTeamId(ERPGTeamID NewTeamId);

private:
	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	ERPGTeamID TeamId;
};
