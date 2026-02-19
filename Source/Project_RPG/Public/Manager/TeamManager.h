// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Type/RPGEnumTypes.h"
#include "TeamManager.generated.h"

class AActor;
class APlayerState;
struct FGameplayTag;

/** 두 액터 간의 팀 관계 비교 결과 */
UENUM(BlueprintType)
enum class ERPGTeamComparison : uint8
{
	OnSameTeam,      // 같은 팀
	DifferentTeams,  // 다른 팀
	InvalidArgument  // 유효하지 않음 (팀이 없거나 객체가 잘못됨)
};

class ARPGTeamInfoBase;
class URPGTeamDisplayAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRPGTeamDisplayAssetChangedDelegate, const URPGTeamDisplayAsset*, DisplayAsset);

/** 팀별 트래킹 정보 (필요시 확장 가능) */
USTRUCT(BlueprintType)
struct FRPGTeamTrackingInfo
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<ARPGTeamInfoBase> TeamInfo = nullptr;

	UPROPERTY()
	TObjectPtr<URPGTeamDisplayAsset> DisplayAsset = nullptr;

	UPROPERTY()
	FOnRPGTeamDisplayAssetChangedDelegate OnTeamDisplayAssetChanged;
};

/**
 * UTeamManager: 프로젝트의 팀 관리 서브시스템
 */
UCLASS()
class PROJECT_RPG_API UTeamManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UTeamManager();

	//~USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End of USubsystem interface

	/** 팀 정보 액터를 등록합니다. */
	bool RegisterTeamInfo(ARPGTeamInfoBase* InTeamInfo);

	/** 팀 정보 액터 등록을 해제합니다. */
	void UnregisterTeamInfo(ARPGTeamInfoBase* InTeamInfo);

	/** 특정 팀의 디스플레이 에셋을 가져옵니다. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Teams")
	URPGTeamDisplayAsset* GetTeamDisplayAsset(ERPGTeamID TeamId) const;

	/** 특정 객체의 팀 디스플레이 에셋을 가져옵니다. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Teams")
	URPGTeamDisplayAsset* GetDisplayAssetFromObject(const UObject* TestObject) const;

	/** 팀 디스플레이 변경 델리게이트를 가져옵니다. */
	FOnRPGTeamDisplayAssetChangedDelegate& GetTeamDisplayAssetChangedDelegate(ERPGTeamID TeamId);

	/** 액터의 팀을 변경합니다. (Authority 전용) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Teams")
	bool ChangeTeamForActor(AActor* ActorToChange, ERPGTeamID NewTeamId);

	/** 객체로부터 팀 ID를 찾아 정수로 반환합니다. 팀이 없으면 255(NoTeam)를 반환합니다. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RPG|Teams")
	int32 FindTeamFromObject(const UObject* TestObject) const;

	/** 두 객체의 팀을 비교합니다. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RPG|Teams", meta = (ExpandEnumAsExecs = "ReturnValue"))
	ERPGTeamComparison CompareTeams(const UObject* A, const UObject* B) const;

	/** 아군 오사(Friendly Fire) 설정을 고려하여 데미지를 줄 수 있는지 확인합니다. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RPG|Teams")
	bool CanCauseDamage(const UObject* Instigator, const UObject* Target, bool bAllowDamageToSelf = true) const;

	/** 해당 팀이 존재하는지 확인합니다. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Teams")
	bool DoesTeamExist(ERPGTeamID TeamId) const;

	/** 등록된 모든 팀 ID 목록을 반환합니다. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Teams")
	TArray<ERPGTeamID> GetTeamIDs() const;

private:
	/** 내부적으로 팀 정보를 추적하는 맵 */
	UPROPERTY()
	TMap<ERPGTeamID, FRPGTeamTrackingInfo> TeamMap;
};
